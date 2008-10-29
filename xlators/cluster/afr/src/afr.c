/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"

#include "afr-self-heal.h"


/**
 * afr_local_cleanup - cleanup everything in frame->local
 */

void 
afr_local_cleanup (call_frame_t *frame)
{
	afr_local_t * local = NULL;

	local = frame->local;

	if (!local)
		return;

	loc_wipe (&local->loc);
	loc_wipe (&local->newloc);

	loc_wipe (&local->transaction.parent_loc);	
	loc_wipe (&local->transaction.new_parent_loc);

	if (local->fd)
		fd_unref (local->fd);

	FREE (local->child_up);

	switch (frame->op) {
	case GF_FOP_GETXATTR:
		FREE (local->cont.getxattr.name);
		break;
	case GF_FOP_CREATE:
		fd_unref (local->cont.create.fd);
		break;
	case GF_FOP_WRITE:
		FREE (local->cont.writev.vector);
		break;
	case GF_FOP_SETXATTR:
		dict_unref (local->cont.setxattr.dict);
		break;
	case GF_FOP_REMOVEXATTR:
		FREE (local->cont.removexattr.name);
		break;
	}

}


/**
 * first_up_child - return the index of the first child that is up
 */

int
first_up_child (afr_private_t *priv)
{
	xlator_t ** children = NULL;
	int         ret      = -1;
	int         i        = 0;

	LOCK (&priv->lock);
	{
		children = priv->children;
		for (i = 0; i < priv->child_count; i++) {
			if (priv->child_up[i]) {
				ret = i;
				break;
			}
		}
	}
	UNLOCK (&priv->lock);

	return ret;
}


/**
 * up_children_count - return the number of children that are up
 */

int
up_children_count (int child_count, unsigned char *child_up)
{
	int i   = 0;
	int ret = 0;

	for (i = 0; i < child_count; i++)
		if (child_up[i])
			ret++;
	return ret;
}


ino64_t
afr_itransform (ino64_t ino, int child_count, int child_index)
{
	ino64_t scaled_ino = -1;

	if (ino == ((uint64_t) -1)) {
		scaled_ino = ((uint64_t) -1);
		goto out;
	}

	scaled_ino = (ino * child_count) + child_index;

out:
	return scaled_ino;
}


int
afr_deitransform (ino64_t ino, int child_count)
{
	int index = -1;

	index = ino % child_count;

	return index;
}


int
afr_self_heal_cbk (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno,
			  local->cont.lookup.inode,
			  &local->cont.lookup.buf,
			  local->cont.lookup.xattr);

	return 0;
}


int32_t
afr_lookup_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this,	int32_t op_ret,	int32_t op_errno,
		inode_t *inode,	struct stat *buf, dict_t *xattr)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	struct stat *   lookup_buf = NULL;
	int call_count       = -1;
	int child_index = (long) cookie;

	priv = this->private;

	LOCK (&frame->lock);
	{
		local = frame->local;
		call_count = --local->call_count;

		if (op_ret == 0) {
			lookup_buf = &local->cont.lookup.buf;
/*
			if (afr_sh_has_metadata_pending (xattr, this))
				local->need_metadata_self_heal = 1;

			if (afr_sh_has_entry_pending (xattr, this))
				local->need_entry_self_heal = 1;

			if (afr_sh_has_data_pending (xattr, this))
				local->need_data_self_heal = 1;
*/
			if (local->success_count == 0) {
				local->op_ret   = op_ret;

				local->cont.lookup.inode = inode;
				local->cont.lookup.xattr = dict_ref (xattr);

				*lookup_buf = *buf;
				lookup_buf->st_ino = afr_itransform (buf->st_ino,
								     priv->child_count,
								     child_index);
			} else {
				if (FILETYPE_DIFFERS (buf, lookup_buf)) {
					/* mismatching filetypes with same name
					   -- Govinda !! GOvinda !!!
					*/
					local->govinda_gOvinda = 1;
				}

				if (PERMISSION_DIFFERS (buf, lookup_buf)) {
					/* mismatching permissions */
					local->need_metadata_self_heal = 1;
				}

				if (OWNERSHIP_DIFFERS (buf, lookup_buf)) {
					/* mismatching permissions */
					local->need_metadata_self_heal = 1;
				}

				if (SIZE_DIFFERS (buf, lookup_buf)) {
					local->need_data_self_heal = 1;
				}
			}

			local->success_count++;

			if (local->reval_child_index == child_index) {
				*lookup_buf = *buf;
				lookup_buf->st_ino = afr_itransform (buf->st_ino, 
								     priv->child_count, 
								     child_index);

				gf_log (this->name, GF_LOG_DEBUG,
					"scaling inode %"PRId64" to %"PRId64,
					buf->st_ino, lookup_buf->st_ino);
			}
		}

		if (op_ret == -1) {
			if (op_errno == ENOENT)
				local->enoent_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (local->success_count && local->enoent_count) {
			local->need_metadata_self_heal = 1;
			local->need_data_self_heal = 1;
			local->need_entry_self_heal = 1;
		}

		if (local->need_metadata_self_heal
		    || local->need_data_self_heal
		    || local->need_entry_self_heal) {
			afr_self_heal (frame, this, afr_self_heal_cbk);
		} else {
			AFR_STACK_UNWIND (frame, local->op_ret,
					  local->op_errno,
					  local->cont.lookup.inode, 
					  &local->cont.lookup.buf,
					  local->cont.lookup.xattr);
		}
	}

	return 0;
}


int32_t 
afr_lookup (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t need_xattr)
{
	afr_private_t *priv = NULL;
	afr_local_t   *local = NULL;
	int            ret = -1;
	int            i = 0;
	int32_t        op_errno = 0;
	int            child_index = -1;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	local->op_ret = -1;

	frame->local = local;

	loc_copy (&local->loc, loc);

	local->reval_child_index = -1;

	if (loc->inode->ino != 0) {
		/* revalidate */

		child_index = afr_deitransform (loc->inode->ino,
						priv->child_count);

		local->reval_child_index = child_index;
	}

	local->call_count = priv->child_count;

	local->child_up = memdup (priv->child_up, priv->child_count);
	local->child_count = up_children_count (priv->child_count,
						local->child_up);

	for (i = 0; i < priv->child_count; i++) {
		STACK_WIND_COOKIE (frame, afr_lookup_cbk, (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->lookup,
				   loc, 1);
	}

	ret = 0;
out:
	if (ret == -1)
		AFR_STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL, NULL);

	return 0;
}


/* {{{ open */

int32_t
afr_open_cbk (call_frame_t *frame, void *cookie,
	      xlator_t *this, int32_t op_ret, int32_t op_errno,
	      fd_t *fd)
{
	afr_local_t *local = NULL;

	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0)
		AFR_STACK_UNWIND (frame, 0, op_errno, fd);

	return 0;
}


#ifdef AFR_OPEN_SELFHEAL
int
afr_open_self_heal_completion_cbk (call_frame_t *frame, xlator_t *this)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;

	int i = 0;

	int call_count = 0;

	priv = this->private;
	local = frame->local;

	call_count = up_children_count (priv->child_count, local->child_up); 

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_open_cbk,
				    priv->children[i],
				    priv->children[i]->fops->open,
				    &local->loc, local->cont.open.flags, 
				    local->fd);
		}
	}

	return 0;
}
#endif


int32_t
afr_open (call_frame_t *frame, xlator_t *this,
	  loc_t *loc, int32_t flags, fd_t *fd)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;
	
	int     i = 0;
	int   ret = -1;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);
	VALIDATE_OR_GOTO (loc, out);
	
	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	
	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	frame->local = local;
	call_count = local->call_count;

#ifdef AFR_OPEN_SELFHEAL
	loc_copy (&local->loc, loc);
	local->cont.open.flags = flags;
	local->fd    = fd_ref (fd);

	afr_self_heal_data (frame, this);

#else
	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_open_cbk,
				    priv->children[i],
				    priv->children[i]->fops->open,
				    loc, flags, fd);
			if (!--call_count)
				break;
		}
	}

#endif

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, fd);
	}

	return 0;
}

/* }}} */


/* {{{ flush */

int32_t
afr_flush_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0)
		AFR_STACK_UNWIND (frame, 0, op_errno);

	return 0;
}


int32_t
afr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_flush_cbk,
				    priv->children[i],
				    priv->children[i]->fops->flush,
				    fd);
			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}
	return 0;
}

/* }}} */

int32_t
afr_statfs_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this, int32_t op_ret, int32_t op_errno,
		struct statvfs *statvfs)
{
	afr_local_t *local = NULL;

	int call_count = 0;

	LOCK (&frame->lock);
	{
		local = frame->local;
		call_count = --local->call_count;

		if (local->cont.statfs.buf_set) {
			if (statvfs->f_bavail < local->cont.statfs.buf.f_bavail)
				local->cont.statfs.buf = *statvfs;
		} else {
			local->cont.statfs.buf = *statvfs;
			local->cont.statfs.buf_set = 1;
		}
	}
	UNLOCK (&frame->lock);

	if (call_count == 0)
		AFR_STACK_UNWIND (frame, op_ret, op_errno, &local->cont.statfs.buf);

	return 0;
}


int32_t
afr_statfs (call_frame_t *frame, xlator_t *this,
	    loc_t *loc)
{
	afr_private_t *  priv        = NULL;
	int              child_count = 0;
	afr_local_t   *  local       = NULL;
	int              i           = 0;

	int ret = -1;
	int              call_count = 0;
	int32_t          op_ret      = -1;
	int32_t          op_errno    = 0;

	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);
	VALIDATE_OR_GOTO (loc, out);

	priv = this->private;
	child_count = priv->child_count;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	
	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	frame->local = local;
	call_count = local->call_count;

	for (i = 0; i < child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_statfs_cbk,
				    priv->children[i],
				    priv->children[i]->fops->statfs, 
				    loc);
			if (!--call_count)
				break;
		}
	}
	
	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}
	return 0;
}


int32_t
afr_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
	    int32_t op_ret, int32_t op_errno, struct flock *lock)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count  = -1;
	int child_index = -1;

	local = frame->local;
	priv  = this->private;

	child_index = (long) cookie;

	call_count = --local->call_count;

	if (!child_went_down (op_ret, op_errno) && (op_ret == -1)) {
		local->op_ret         = op_ret;
		local->op_errno       = op_errno;
		local->cont.lk.flock  = *lock;
		local->success_count  = -1;

		goto out;
	}

	child_index++;

	if (child_index < priv->child_count) {
		STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) child_index,
				   priv->children[child_index],
				   priv->children[child_index]->fops->lk,
				   local->fd, local->cont.lk.cmd, 
				   &local->cont.lk.flock);
	}

out:
	if ((call_count == 0) || (local->success_count == -1))
		AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno, &
			      local->cont.lk.flock);

	return 0;
}


int
afr_lk (call_frame_t *frame, xlator_t *this,
	fd_t *fd, int32_t cmd,
	struct flock *flock)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;

	int i = 0;

	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	AFR_LOCAL_INIT (local, priv);

	if (local->call_count == 0) {
		op_errno = ENOTCONN;
		goto out;
	}

	frame->local      = local;

	local->fd    = fd_ref (fd);
	local->cont.lk.cmd   = cmd;
	local->cont.lk.flock = *flock;

	i = first_up_child (priv);

	STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) i,
			   priv->children[i],
			   priv->children[i]->fops->lk,
			   fd, cmd, flock);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}
	return 0;
}


/**
 * find_child_index - find the child's index in the array of subvolumes
 * @this: AFR
 * @child: child
 */

static int
find_child_index (xlator_t *this, xlator_t *child)
{
	afr_private_t *priv = NULL;

	int i = -1;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if ((xlator_t *) child == priv->children[i])
			break;
	}

	return i;
}


int32_t
notify (xlator_t *this, int32_t event,
	void *data, ...)
{
	afr_private_t *     priv     = NULL;
	unsigned char *     child_up = NULL;

	int i           = -1;
	int up_children = 0;

	priv = this->private;

	if (!priv)
		return 0;

	child_up = priv->child_up;

	switch (event) {
	case GF_EVENT_CHILD_UP:
		i = find_child_index (this, data);

		child_up[i] = 1;

		/* 
		   if all the children were down, and one child came up, 
		   send notify to parent
		*/

		for (i = 0; i < priv->child_count; i++)
			if (child_up[i])
				up_children++;

		if (up_children == 1)
			default_notify (this, event, data);

		break;

	case GF_EVENT_CHILD_DOWN:
		i = find_child_index (this, data);

		child_up[i] = 0;
		
		/* 
		   if all children are down, and this was the last to go down,
		   send notify to parent
		*/

		for (i = 0; i < priv->child_count; i++)
			if (child_up[i])
				up_children++;

		if (up_children == 0)
			default_notify (this, event, data);

		break;

	case GF_EVENT_PARENT_UP:
		for (i = 0; i < priv->child_count; i++)
			priv->children[i]->notify (priv->children[i], 
						   GF_EVENT_PARENT_UP, this);
		break;
		
	default:
		default_notify (this, event, data);
	}

	return 0;
}


static const char *favorite_child_warning_str = "You have specified subvolume '%s' "
	"as the 'favorite child'. This means that if a discrepancy in the content "
	"or attributes (ownership, permission, etc.) of a file is detected among "
	"the subvolumes, the file on '%s' will be considered the definitive "
	"version and its contents will OVERWRITE the contents of the file on other "
	"subvolumes. All versions of the file except that on '%s' "
	"WILL BE LOST.";

int32_t 
init (xlator_t *this)
{
	afr_private_t * priv        = NULL;
	int             child_count = 0;
	xlator_list_t * trav        = NULL;
	int             i           = 0;
	int             ret         = -1;
	int             op_errno    = 0;

	char * read_subvol = NULL;
	char * fav_child   = NULL;

	int    read_ret    = -1;
	int    fav_ret     = -1;

	ALLOC_OR_GOTO (this->private, afr_private_t, out);

	priv = this->private;

	read_ret = dict_get_str (this->options, "read-subvolume", &read_subvol);
	priv->read_child = -1;

	fav_ret = dict_get_str (this->options, "favorite-child", &fav_child);
	priv->favorite_child = -1;

	trav = this->children;
	while (trav) {
		if (read_ret == 0 && !strcmp (read_subvol, trav->xlator->name)) {
			priv->read_child = child_count;
		}

		if (fav_ret == 0 && !strcmp (fav_child, trav->xlator->name)) {
			gf_log (this->name, GF_LOG_WARNING,
				favorite_child_warning_str, trav->xlator->name,
				trav->xlator->name, trav->xlator->name);
			priv->favorite_child = child_count;
		}

		child_count++;
		trav = trav->next;
	}

	priv->child_count = child_count;
	LOCK_INIT (&priv->lock);

	priv->child_up = calloc (sizeof (unsigned char), child_count);
	if (!priv->child_up) {
		gf_log (this->name, GF_LOG_ERROR,	
			"out of memory :(");		
		op_errno = ENOMEM;			
		goto out;
	}

	priv->children = calloc (sizeof (xlator_t *), child_count);
	if (!priv->children) {
		gf_log (this->name, GF_LOG_ERROR,	
			"out of memory :(");		
		op_errno = ENOMEM;			
		goto out;
	}

	priv->pending_inc_array = calloc (sizeof (int32_t), child_count);
	if (!priv->pending_inc_array) {
		gf_log (this->name, GF_LOG_ERROR,	
			"out of memory :(");		
		op_errno = ENOMEM;			
		goto out;
	}

	priv->pending_dec_array = calloc (sizeof (int32_t), child_count);
	if (!priv->pending_dec_array) {
		gf_log (this->name, GF_LOG_ERROR,	
			"out of memory :(");		
		op_errno = ENOMEM;			
		goto out;
	}

	trav = this->children;
	while (i < child_count) {
		priv->children[i] = trav->xlator;
		priv->pending_inc_array[i] = hton32 (1);
		priv->pending_dec_array[i] = hton32 (-1);

		trav = trav->next;
		i++;
	}

	ret = 0;
out:
	return ret;
}


int32_t
fini (xlator_t *this)
{
	return 0;
}


struct xlator_fops fops = {
  .lookup      = afr_lookup,
  .open        = afr_open,
  .lk          = afr_lk,
  .flush       = afr_flush,
  .statfs      = afr_statfs,

  /* inode read */
  .access      = afr_access,
  .stat        = afr_stat,
  .fstat       = afr_fstat,
  .readlink    = afr_readlink,
  .getxattr    = afr_getxattr,
  .readv       = afr_readv,

  /* inode write */
  .chmod       = afr_chmod,
  .chown       = afr_chown,
  .writev      = afr_writev,
  .truncate    = afr_truncate,
  .utimens     = afr_utimens,
  .setxattr    = afr_setxattr,
  .removexattr = afr_removexattr,

  /* dir read */
  .opendir     = afr_opendir,
  .readdir     = afr_readdir,
  .getdents    = afr_getdents,

  /* dir write */
  .create      = afr_create,
  .mknod       = afr_mknod,
  .mkdir       = afr_mkdir,
  .unlink      = afr_unlink,
  .rmdir       = afr_rmdir,
  .link        = afr_link,
  .symlink     = afr_symlink,
  .rename      = afr_rename,
  .setdents    = afr_setdents,
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
};

struct xlator_options options[] = {
	{ "read-subvolume", GF_OPTION_TYPE_XLATOR, 0, },
	{ "favorite-child", GF_OPTION_TYPE_XLATOR, 0, },
	{ NULL, 0, },
};
