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

/**
 * Return true if attributes of any two children do not match


static int
attr_mismatch_p ()
{
	return 1;
}
*/


/**
 * select_source - select a source and return it
 * TODO: take into account option 'favorite-child'
 */

static int
select_source (int sources[], int child_count)
{
	int i;
	for (i = 0; i < child_count; i++)
		if (sources[i])
			return i;

	return -1;
}


/**
 * sink_count - return number of sinks in sources array
 */

static int
sink_count (int sources[], int child_count)
{
	int i;
	int sinks = 0;
	for (i = 0; i < child_count; i++)
		if (!sources[i])
			sinks++;
	return sinks;
}


/**
 * sh_cleanup_and_terminate - do necessary cleanup and call the completion function
 */

static int
sh_cleanup_and_terminate (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *     local  = NULL;
	afr_self_heal_t * sh     = NULL;
	afr_private_t *   priv   = NULL;

	int i = 0;

	local = frame->local;
	sh    = &local->self_heal;
	priv  = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->pending_matrix[i])
			FREE (sh->pending_matrix[i]);
		if (sh->xattr[i])
			dict_unref (sh->xattr[i]);
	}
	
	if (sh->healing_fd)
		fd_unref (sh->healing_fd);

	FREE (sh->sources);

	gf_log (this->name, GF_LOG_DEBUG,
		"terminating self heal");

	sh->completion_cbk (frame, this);
	return 0;
}


static int
sh_unlock_inode_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;
	afr_self_heal_t * sh = NULL;

	int child_index = (int) cookie;
	int call_count = 0;

	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG, 
				"unlocking inode on child %d failed: %s",
				child_index, strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG, "inode on child %d unlocked",
				child_index);
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		sh_cleanup_and_terminate (frame, this);
	}
	
	return 0;
}


static int
sh_unlock_inode (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			
	int i = 0;				
	int call_count = 0;		     

	int source;
	int *sources;

	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;
	afr_self_heal_t * sh  = NULL;

	local = frame->local;
	sh = &local->self_heal;

	source = sh->source;
	sources = sh->sources;

	call_count = sink_count (sources, priv->child_count) + 1; 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = 0;
		flock.l_len   = 0;
		flock.l_type  = F_UNLCK;			

		if ((i == source) || (sources[i] == 0)) {
			STACK_WIND_COOKIE (frame, sh_unlock_inode_cbk, (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->gf_file_lk, 
					   &local->cont.open.loc, 
					   NULL, F_SETLK, &flock); 
			call_count--;
		}

		if (call_count == 0)
			break;
	}

	return 0;
}


static int
sh_erase_pending (call_frame_t *frame, xlator_t *this)
{
	sh_unlock_inode (frame, this);
	return 0;
}


static int32_t
sh_close_fds_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = NULL;
	afr_self_heal_t * sh = NULL;

	int call_count = -1;

	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		sh_erase_pending (frame, this);
	}

	return 0;
}


static int
sh_close_fds (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     

	int source = -1;
	int *sources = NULL;

	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;
	afr_self_heal_t * sh  = NULL;

	local = frame->local;
	sh = &local->self_heal;

	call_count = sink_count (local->self_heal.sources, priv->child_count) + 1; 

	local->call_count = call_count;		

	source  = local->self_heal.source;
	sources = local->self_heal.sources;

	for (i = 0; i < priv->child_count; i++) {				
		if ((i == source) || (sources[i] == 0)) {
			STACK_WIND_COOKIE (frame, sh_close_fds_cbk, (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->flush,
					   sh->healing_fd);
			call_count--;
		}

		if (call_count == 0)
			break;
	}

	return 0;
}

static int
sh_read_write (call_frame_t *frame, xlator_t *this);

static int
sh_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
	      int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	int child_index = (int) cookie;
	int call_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	gf_log (this->name, GF_LOG_DEBUG, 
		"wrote %d bytes of data to child %d, offset %d", 
		op_ret, child_index, sh->offset - op_ret);

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (sh->offset < sh->file_size) {
			sh_read_write (frame, this);
		} else {
			gf_log (this->name, GF_LOG_DEBUG, 
				"closing fd's");
		
			sh_close_fds (frame, this);
		}
	}

	return 0;
}


static int32_t
sh_read_cbk (call_frame_t *frame, void *cookie,
	     xlator_t *this, int32_t op_ret, int32_t op_errno,
	     struct iovec *vector, int32_t count, struct stat *buf)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	int child_index = (int) cookie;
	int i = 0;
	int call_count = 0;

	off_t offset;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	call_count = sink_count (sh->sources, priv->child_count);
	local->call_count = call_count;

	gf_log (this->name, GF_LOG_DEBUG, 
		"read %d bytes of data from child %d, offset %d", 
		op_ret, child_index, sh->offset);

	/* what if we read less than block size? */
	offset = sh->offset;
	sh->offset += op_ret;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] == 0) {
			/* this is a sink, so write to it */
			STACK_WIND_COOKIE (frame, sh_write_cbk, (void *) i,
					   priv->children[i],
					   priv->children[i]->fops->writev,
					   sh->healing_fd, vector, count, offset);
			call_count--;
		}

		if (call_count == 0)
			break;
	}

	return 0;
}


static int
sh_read_write (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	STACK_WIND_COOKIE (frame, sh_read_cbk, (void *) sh->source,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->readv,
			   sh->healing_fd, sh->block_size,
			   sh->offset);

	return 0;
}


static int
sh_open_source_and_sinks_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			      int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	afr_local_t * local = NULL;
	afr_self_heal_t *sh = NULL;

	int child_index = (int) cookie;

	local = frame->local;
	sh = &local->self_heal;

	/* TODO: some of the open's might fail.
	   In that case, modify cleanup fn to send flush on those 
	   fd's which are already open */

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG,
				"open failed on child %d: %s",
				child_index, strerror (op_errno));
			sh_cleanup_and_terminate (frame, this);
		}

		if ((op_ret == 0) && !sh->healing_fd) {
			fd_bind (fd);
			sh->healing_fd = fd;
		}

		local->call_count--;
	}
	UNLOCK (&frame->lock);

	if (local->call_count == 0) {
		gf_log (this->name, GF_LOG_DEBUG, "fd's opened, commencing sync");
		sh_read_write (frame, this);
	}

	return 0;
}


static int
sh_open_source_and_sinks (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     

	int source = -1;
	int *sources = NULL;

	fd_t *fd = NULL;

	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;

	local = frame->local;

	call_count = sink_count (local->self_heal.sources, priv->child_count) + 1; 

	local->call_count = call_count;		

	fd = fd_create (local->cont.open.loc.inode, frame->root->pid);
	fd = fd_ref (fd);

	source  = local->self_heal.source;
	sources = local->self_heal.sources;

	for (i = 0; i < priv->child_count; i++) {				
		if ((i == source) || (sources[i] == 0)) {
			STACK_WIND_COOKIE (frame, sh_open_source_and_sinks_cbk, 
					   (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->open,
					   &local->cont.open.loc, 
					   O_RDWR|O_LARGEFILE, fd); 

			call_count--;
		}

		if (call_count == 0)
			break;
	}

	return 0;
}


static int
sh_lock_inode_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;

	int child_index = (int) cookie;

	/* TODO: what if lock fails? */
	
	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG, 
				"locking inode on child %d failed: %s",
				child_index, strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG, "inode on child %d locked",
				child_index);
		}

		local->call_count--;
	}
	UNLOCK (&frame->lock);

	if (local->call_count == 0) {
		sh_open_source_and_sinks (frame, this);
	}

	return 0;
}


static int
sh_lock_inode (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			
	int i = 0;				
	int call_count = 0;		     

	int source;
	int *sources;

	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;
	afr_self_heal_t * sh  = NULL;

	local = frame->local;
	sh = &local->self_heal;

	source = sh->source;
	sources = sh->sources;

	call_count = sink_count (sources, priv->child_count) + 1; 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = 0;
		flock.l_len   = 0;
		flock.l_type  = F_WRLCK;			

		if ((i == source) || (sources[i] == 0)) {
			STACK_WIND_COOKIE (frame, sh_lock_inode_cbk, (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->gf_file_lk, 
					   &local->cont.open.loc, 
					   NULL, F_SETLK, &flock); 
		}
	}

	return 0;
}


static int
sh_source_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno,
		    struct stat *buf)
{
	afr_local_t * local = NULL;

	local = frame->local;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_DEBUG,
			"getting stat of source child failed: %s", 
			strerror (op_errno));
		sh_cleanup_and_terminate (frame, this);
		return -1;
	}

	local->self_heal.block_size = buf->st_blksize;
	local->self_heal.file_size  = buf->st_size;

	gf_log (this->name, GF_LOG_DEBUG, "got stat from source child: "
		"(block size = %d, file size = %d)", buf->st_blksize, buf->st_size);

	sh_lock_inode (frame, this);

	return 0;
}


static int
sh_get_source_stat (call_frame_t *frame, xlator_t *this,
		    int source)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;

	priv  = this->private;
	local = frame->local;

	STACK_WIND (frame, sh_source_stat_cbk,
		    priv->children[source],
		    priv->children[source]->fops->stat,
		    &local->cont.open.loc);
	
	return 0;
}


static int
sh_sync_source_and_sinks (call_frame_t *frame, xlator_t *this,
			   int sources[])
{
	afr_local_t * local  = NULL;
	afr_private_t * priv = NULL;
	afr_self_heal_t * sh = NULL;

	local = frame->local;
	priv  = this->private;
	sh    = &local->self_heal;

	/* select a source */
	sh->source = select_source (sources, priv->child_count);

	gf_log (this->name, GF_LOG_DEBUG,
		"selecting child %d as source",
		sh->source);

	/* stat on source */
	sh_get_source_stat (frame, this, sh->source);

	return 0;
}



static void
print_pending_matrix (int32_t *pending_matrix[], xlator_t *this)
{
	afr_private_t * priv = this->private;

	char *buf = NULL;
	char *ptr = NULL;

	int i, j;

        /* 10 digits per entry + 1 space + '[' and ']' */
	buf = malloc (priv->child_count * 11 + 8); 

	for (i = 0; i < priv->child_count; i++) {
		ptr = buf;
		ptr += sprintf (ptr, "[ ");
		for (j = 0; j < priv->child_count; j++) {
			ptr += sprintf (ptr, "%d ", pending_matrix[i][j]);
		}
		ptr += sprintf (ptr, "]");
		gf_log (this->name, GF_LOG_DEBUG,
			"pending_matrix: %s", buf);
	}

	FREE (buf);
}


static void
build_pending_matrix (int32_t *pending_matrix[], dict_t *xattr[],
		      int child_count)
{
	data_t *data = NULL;
	int i = 0;
	int j = 0;

	for (i = 0; i < child_count; i++) {
		if (xattr[i]) {
			data = dict_get (xattr[i], AFR_DATA_PENDING);
			if (data) {
				for (j = 0; j < child_count; j++) {
					pending_matrix[i][j] = 
						ntoh32 (((int32_t *)(data->data))[j]);
				}
			}
		}
	}
}


/**
 * mark_sources: Mark all 'source' nodes and return number of source
 * nodes found
 */

static int
mark_sources (int32_t *pending_matrix[], int sources[], int child_count)
{
	int i = 0;
	int j = 0;

	int nsources = 0;

	/*
	  Let's 'normalize' the pending matrix first,
	  by disregarding all pending entries that refer
	  to themselves
	*/
	for (i = 0; i < child_count; i++) {
		pending_matrix[i][i] = 0;
	}

	for (i = 0; i < child_count; i++) {
		for (j = 0; j < child_count; j++) {
			if (pending_matrix[j][i])
				break;
		}

		if (j == child_count) {
			nsources++;
			sources[i] = 1;
		}
	}

	return nsources;
}

/**
 * is_zero - return true if pending matrix is all zeroes
 */

static int
is_zero (int32_t *pending_matrix[], int child_count)
{
	int i, j;

	for (i = 0; i < child_count; i++) 
		for (j = 0; j < child_count; j++) 
			if (pending_matrix[i][j]) 
				return 0;
	return 1;
}


static int
sh_do_data_self_heal (call_frame_t *frame, xlator_t *this)
{
	int nsources = -1;
	int i = 0;
	
	int32_t op_ret = -1;

	afr_local_t    * local = NULL;
	afr_private_t  * priv  = this->private;
	afr_self_heal_t * sh   = NULL;

	local = frame->local;
	sh = &local->self_heal;

	sh->pending_matrix = calloc (sizeof (int32_t *), priv->child_count);
	sh->sources = calloc (sizeof (*sh->sources), priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		sh->pending_matrix[i] = calloc (sizeof (int32_t), priv->child_count);
	}

	build_pending_matrix (sh->pending_matrix, sh->xattr, priv->child_count);

	print_pending_matrix (sh->pending_matrix, this);

	if (is_zero (sh->pending_matrix, priv->child_count)) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no self heal needed");
		goto out;
	}

	priv = this->private;

	nsources = mark_sources (sh->pending_matrix, sh->sources, priv->child_count);

	if (nsources == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"split brain detected ... Govinda, Govinda!");
		goto out;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"starting self heal on %s", local->cont.open.loc.path);

	gf_log (this->name, GF_LOG_DEBUG,
		"%d sources found", nsources);

	sh_sync_source_and_sinks (frame, this, sh->sources);

	op_ret = 0;
out:
	if (op_ret == -1)
		sh->completion_cbk (frame, this);

	return 0;
}


int
afr_inode_data_self_heal_lookup_cbk (call_frame_t *frame, void *cookie,
				     xlator_t *this, int32_t op_ret, int32_t op_errno,
				     inode_t *inode, struct stat *buf, dict_t *xattr)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;

	int call_count  = -1;
	int child_index = (int) cookie;

	priv = this->private;

	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (op_ret != -1) {
			local->self_heal.xattr[child_index] = dict_ref (xattr);
		}
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		sh_do_data_self_heal (frame, this);
	}

	return 0;
}


int
afr_inode_data_self_heal (call_frame_t *frame, xlator_t *this, 
			  int (*completion_cbk) (call_frame_t *, xlator_t *))
{
	afr_self_heal_t * sh    = NULL; 
	afr_local_t    *  local = NULL;
	afr_private_t  *  priv  = NULL;

	unsigned char *child_up = NULL;

	int NEED_XATTR_YES = 1;

	int op_ret   = -1;
	int op_errno = 0;

	int i;

	priv  = this->private;
	local = frame->local;
	sh    = &local->self_heal;

	sh->completion_cbk = completion_cbk;

	sh->xattr = calloc (sizeof (dict_t *), priv->child_count);
	if (!sh->xattr) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	child_up = calloc (priv->child_count, sizeof (*child_up));
	if (!child_up) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	memcpy (child_up, priv->child_up, priv->child_count * sizeof (*child_up));

	local->call_count = up_children_count (priv->child_count, child_up);
	for (i = 0; i < priv->child_count; i++) {
		if (child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_inode_data_self_heal_lookup_cbk,
					   (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->lookup,
					   &local->cont.open.loc, NEED_XATTR_YES);
		}
	}

	op_ret = 0;
out:
	FREE (child_up);

	if (op_ret == -1) {
		completion_cbk (frame, this);
	}
	
	return 0;
}

