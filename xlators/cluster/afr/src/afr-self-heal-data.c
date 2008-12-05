/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"



int
afr_sh_data_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	/* 
	   TODO: cleanup sh->* 
	 */

	gf_log (this->name, GF_LOG_WARNING,
		"self heal of %s completed",
		local->loc.path);

	sh->completion_cbk (frame, this);

	return 0;
}


int
afr_sh_data_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_private_t   *priv  = NULL;
	afr_self_heal_t *sh = NULL;
	int              call_count = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		fd_unref (sh->healing_fd);
		sh->healing_fd = NULL;
		afr_sh_data_done (frame, this);
	}

	return 0;
}


int
afr_sh_data_close (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_private_t   *priv  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              i = 0;
	int              call_count = 0;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	if (!sh->healing_fd) {
		afr_sh_data_done (frame, this);
		return 0;
	}

	call_count = sh->active_sinks + 1;
	local->call_count = call_count;


	/* closed source */
	gf_log (this->name, GF_LOG_DEBUG,
		"closing fd of %s on %s",
		local->loc.path, priv->children[sh->source]->name);

	STACK_WIND_COOKIE (frame, afr_sh_data_flush_cbk,
			   (void *) (long) sh->source,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->flush,
			   sh->healing_fd);
	call_count--;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] || !local->child_up[i])
			continue;

		gf_log (this->name, GF_LOG_DEBUG,
			"closing fd of %s on %s",
			local->loc.path, priv->children[i]->name);

		STACK_WIND_COOKIE (frame, afr_sh_data_flush_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->flush,
				   sh->healing_fd);
		if (!--call_count)
			break;
	}

	return 0;
}


int
afr_sh_data_unlck_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;
	int           call_count = 0;
	int           child_index = (long) cookie;

	/* TODO: what if lock fails? */
	
	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"locking inode of %s on child %d failed: %s",
				local->loc.path, child_index,
				strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"inode of %s on child %d locked",
				local->loc.path, child_index);
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		afr_sh_data_close (frame, this);
	}

	return 0;
}


int
afr_sh_data_unlock (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			
	int i = 0;				
	int call_count = 0;		     

	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	afr_self_heal_t * sh  = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = local->child_count;

	local->call_count = call_count;		

	flock.l_start = 0;
	flock.l_len   = 0;
	flock.l_type  = F_UNLCK;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_DEBUG,
				"unlocking %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame, afr_sh_data_unlck_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->inodelk,
					   &local->loc, F_SETLK, &flock); 
			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_sh_data_finish (call_frame_t *frame, xlator_t *this)
{
	afr_local_t   *local = NULL;

	local = frame->local;

	gf_log (this->name, GF_LOG_DEBUG,
		"finishing data selfheal of %s", local->loc.path);

	afr_sh_data_unlock (frame, this);

	return 0;
}



int
afr_sh_data_erase_pending_cbk (call_frame_t *frame, void *cookie,
			       xlator_t *this, int32_t op_ret,
			       int32_t op_errno, dict_t *xattr)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int             call_count = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_sh_data_finish (frame, this);

	return 0;
}


int
afr_sh_data_erase_pending (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              i = 0;
	dict_t          **erase_xattr = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;


	afr_sh_pending_to_delta (sh->pending_matrix, sh->delta_matrix,
				 sh->success, priv->child_count);

	erase_xattr = calloc (sizeof (*erase_xattr), priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->xattr[i]) {
			call_count++;

			erase_xattr[i] = get_new_dict();
			dict_ref (erase_xattr[i]);
		}
	}

	afr_sh_delta_to_xattr (sh->delta_matrix, erase_xattr,
			       priv->child_count, AFR_DATA_PENDING);

	local->call_count = call_count;
	for (i = 0; i < priv->child_count; i++) {
		if (!erase_xattr[i])
			continue;

		gf_log (this->name, GF_LOG_DEBUG,
			"erasing pending flags from %s on %s",
			local->loc.path, priv->children[i]->name);

		STACK_WIND_COOKIE (frame, afr_sh_data_erase_pending_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->xattrop,
				   &local->loc,
				   GF_XATTROP_ADD_ARRAY, erase_xattr[i]);
		if (!--call_count)
			break;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (erase_xattr[i]) {
			dict_unref (erase_xattr[i]);
		}
	}
	FREE (erase_xattr);

	return 0;
}


int
afr_sh_data_trim_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              call_count = 0;
	int              child_index = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (op_ret == -1)
			gf_log (this->name, GF_LOG_ERROR,
				"ftruncate of %s on subvolume %s failed (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));
		else
			gf_log (this->name, GF_LOG_DEBUG,
				"ftruncate of %s on subvolume %s completed",
				local->loc.path,
				priv->children[child_index]->name);
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		afr_sh_data_erase_pending (frame, this);
	}

	return 0;
}


int
afr_sh_data_trim_sinks (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int             *sources = NULL;
	int              call_count = 0;
	int              i = 0;


	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	sources = sh->sources;
	call_count = sh->active_sinks;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (sources[i] || !local->child_up[i])
			continue;

		STACK_WIND_COOKIE (frame, afr_sh_data_trim_cbk,
				   (void *) (long) i,
				   priv->children[i], 
				   priv->children[i]->fops->ftruncate,
				   sh->healing_fd, sh->file_size); 

		if (!--call_count)
			break;
	}

	return 0;
}


int
afr_sh_data_read_write (call_frame_t *frame, xlator_t *this);

int
afr_sh_data_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	int child_index = (long) cookie;
	int call_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	gf_log (this->name, GF_LOG_DEBUG, 
		"wrote %d bytes of data from %s to child %d, offset %"PRId64"", 
		op_ret, local->loc.path, child_index, sh->offset - op_ret);

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"write to %s failed on subvolume %s (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));
			sh->op_failed = 1;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		if (sh->op_failed) {
			afr_sh_data_finish (frame, this);
			return 0;
		}

		if (sh->offset < sh->file_size) {
			afr_sh_data_read_write (frame, this);
		} else {
			gf_log (this->name, GF_LOG_DEBUG, 
				"closing fd's of %s",
				local->loc.path);
		
			afr_sh_data_trim_sinks (frame, this);
		}
	}

	return 0;
}


int
afr_sh_data_read_cbk (call_frame_t *frame, void *cookie,
		      xlator_t *this, int32_t op_ret, int32_t op_errno,
		      struct iovec *vector, int32_t count, struct stat *buf)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	int child_index = (long) cookie;
	int i = 0;
	int call_count = 0;

	off_t offset;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	call_count = sh->active_sinks;

	local->call_count = call_count;

	gf_log (this->name, GF_LOG_DEBUG, 
		"read %d bytes of data from %s on child %d, offset %"PRId64"",
		op_ret, local->loc.path, child_index, sh->offset);

	if (op_ret <= 0) {
		afr_sh_data_trim_sinks (frame, this);
		return 0;
	}

	/* what if we read less than block size? */
	offset = sh->offset;
	sh->offset += op_ret;

	frame->root->req_refs = frame->root->rsp_refs;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] || !local->child_up[i])
			continue;

		/* this is a sink, so write to it */
		STACK_WIND_COOKIE (frame, afr_sh_data_write_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->writev,
				   sh->healing_fd, vector, count, offset);

		if (!--call_count)
			break;
	}

	return 0;
}


int
afr_sh_data_read_write (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	STACK_WIND_COOKIE (frame, afr_sh_data_read_cbk,
			   (void *) (long) sh->source,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->readv,
			   sh->healing_fd, sh->block_size,
			   sh->offset);

	return 0;
}


int
afr_sh_data_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              child_index = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	child_index = (long) cookie;

	/* TODO: some of the open's might fail.
	   In that case, modify cleanup fn to send flush on those 
	   fd's which are already open */

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"open of %s failed on child %s (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));
			sh->op_failed = 1;
		}

	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		if (sh->op_failed) {
			afr_sh_data_finish (frame, this);
			return 0;
		}
		gf_log (this->name, GF_LOG_DEBUG,
			"fd for %s opened, commencing sync",
			local->loc.path);

		gf_log (this->name, GF_LOG_WARNING,
			"sourcing file %s from %s to other sinks",
			local->loc.path, priv->children[sh->source]->name);

		afr_sh_data_read_write (frame, this);
	}

	return 0;
}


int
afr_sh_data_open (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     

	int source = -1;
	int *sources = NULL;

	fd_t *fd = NULL;

	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	afr_self_heal_t *sh = NULL;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = sh->active_sinks + 1;
	local->call_count = call_count;

	fd = fd_create (local->loc.inode, frame->root->pid);
	sh->healing_fd = fd;

	source  = local->self_heal.source;
	sources = local->self_heal.sources;

	sh->block_size = sh->buf[source].st_blksize;
	sh->file_size  = sh->buf[source].st_size;

	/* open source */
	STACK_WIND_COOKIE (frame, afr_sh_data_open_cbk,
			   (void *) (long) source,
			   priv->children[source],
			   priv->children[source]->fops->open,
			   &local->loc, O_RDONLY|O_LARGEFILE, fd);
	call_count--;

	/* open sinks */
	for (i = 0; i < priv->child_count; i++) {
		if(sources[i] || !local->child_up[i])
			continue;

		STACK_WIND_COOKIE (frame, afr_sh_data_open_cbk,
				   (void *) (long) i,
				   priv->children[i], 
				   priv->children[i]->fops->open,
				   &local->loc, 
				   O_WRONLY|O_LARGEFILE, fd); 

		if (!--call_count)
			break;
	}

	return 0;
}


int
afr_sh_data_sync_prepare (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              active_sinks = 0;
	int              source = 0;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	source = sh->source;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] == 0 && local->child_up[i] == 1) {
			active_sinks++;
			sh->success[i] = 1;
		}
	}
	sh->success[source] = 1;

	if (active_sinks == 0) {
		gf_log (this->name, GF_LOG_WARNING,
			"no active sinks for performing self-heal on file %s",
			local->loc.path);
		afr_sh_data_finish (frame, this);
		return 0;
	}
	sh->active_sinks = active_sinks;

	gf_log (this->name, GF_LOG_DEBUG,
		"syncing data of %s from subvolume %s to %d active sinks",
		local->loc.path, priv->children[source]->name, active_sinks);

	afr_sh_data_open (frame, this);

	return 0;
}


int
afr_sh_data_fix (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              nsources = 0;
	int              source = 0;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	afr_sh_build_pending_matrix (sh->pending_matrix, sh->xattr, 
				     priv->child_count, AFR_DATA_PENDING);

	afr_sh_print_pending_matrix (sh->pending_matrix, this);


	afr_sh_mark_sources (sh->pending_matrix, sh->sources, 
			     priv->child_count);

	afr_sh_supress_empty_children (sh->sources, sh->xattr,
				       priv->child_count, AFR_DATA_PENDING);

	afr_sh_supress_errenous_children (sh->sources, sh->child_errno,
					  priv->child_count);

	nsources = afr_sh_source_count (sh->sources, priv->child_count);

	if ((nsources == 0)
	    && (priv->favorite_child != -1)
	    && (sh->child_errno[priv->favorite_child] == 0)) {

		gf_log (this->name, GF_LOG_WARNING,
			"Picking favorite child %s as authentic source to resolve conflicting data of %s",
			priv->children[priv->favorite_child]->name,
			local->loc.path);

		sh->sources[priv->favorite_child] = 1;

		nsources = afr_sh_source_count (sh->sources,
						priv->child_count);
	}

	if (nsources == 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"Unable to resolve conflicting data of %s. Please resolve manually by deleting the file %s from all but the preferred subvolume",
			local->loc.path, local->loc.path);

		local->govinda_gOvinda = 1;

		afr_sh_data_finish (frame, this);
		return 0;
	}

	source = afr_sh_select_source (sh->sources, priv->child_count);
	sh->source = source;

	/* detect changes not visible through pending flags -- JIC */
	for (i = 0; i < priv->child_count; i++) {
		if (i == source || sh->child_errno[i])
			continue;

		if (SIZE_DIFFERS (&sh->buf[i], &sh->buf[source]))
			sh->sources[i] = 0;
	}

	afr_sh_data_sync_prepare (frame, this);

	return 0;
}



int
afr_sh_data_lookup_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno,
			inode_t *inode, struct stat *buf, dict_t *xattr)
{
	afr_private_t   *priv  = NULL;
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret != -1) {
			sh->xattr[child_index] = dict_ref (xattr);
			sh->buf[child_index] = *buf;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		afr_sh_data_fix (frame, this);
	}

	return 0;
}



int
afr_sh_data_lookup (call_frame_t *frame, xlator_t *this)
{
	afr_self_heal_t * sh    = NULL; 
	afr_local_t    *  local = NULL;
	afr_private_t  *  priv  = NULL;

	int NEED_XATTR_YES = 1;
	int call_count = 0;
	int i = 0;

	priv  = this->private;
	local = frame->local;
	sh    = &local->self_heal;

	call_count = local->child_count;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame,
					   afr_sh_data_lookup_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->lookup,
					   &local->loc, NEED_XATTR_YES);
			if (!--call_count)
				break;
		}
	}

	return 0;
}



int
afr_sh_data_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	int              call_count = 0;
	int              child_index = (long) cookie;

	/* TODO: what if lock fails? */
	
	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			sh->op_failed = 1;

			if (op_errno != EAGAIN)
				gf_log (this->name, GF_LOG_ERROR, 
					"locking of %s on child %d failed: %s",
					local->loc.path, child_index,
					strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"inode of %s on child %d locked",
				local->loc.path, child_index);
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		if (sh->op_failed) {
			afr_sh_data_finish (frame, this);
			return 0;
		}

		afr_sh_data_lookup (frame, this);
	}

	return 0;
}


int
afr_sh_data_lock (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			
	int i = 0;				
	int call_count = 0;		     

	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	afr_self_heal_t * sh  = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = local->child_count;

	local->call_count = call_count;		

	flock.l_start = 0;
	flock.l_len   = 0;
	flock.l_type  = F_WRLCK;			

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_DEBUG,
				"locking %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame, afr_sh_data_lock_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->inodelk,
					   &local->loc, F_SETLK, &flock); 
			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_self_heal_data (call_frame_t *frame, xlator_t *this)
{
	afr_local_t   *local = NULL;
	afr_self_heal_t *sh = NULL;


	local = frame->local;
	sh = &local->self_heal;

	if (local->need_data_self_heal) {
		afr_sh_data_lock (frame, this);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"proceeding to data check on %s",
			local->loc.path);
		afr_sh_data_done (frame, this);
	}

	return 0;
}

