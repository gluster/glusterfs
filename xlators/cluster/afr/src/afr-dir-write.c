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

#include "afr.h"
#include "afr-transaction.h"


void
afr_build_parent_loc (loc_t *parent, loc_t *child)
{
	char *tmp = NULL;

	if (!child->parent) {
		loc_copy (parent, child);
		return;
	}

	tmp = strdup (child->path);
	parent->path   = strdup (dirname (tmp));
	FREE (tmp);

        parent->name   = strrchr (parent->path, '/');
	if (parent->name)
		parent->name++;

	parent->inode  = inode_ref (child->parent);
	parent->parent = inode_parent (parent->inode, 0, NULL);
	parent->ino    = parent->inode->ino;
}


/* {{{ create */

int
afr_create_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame)
		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno,
				  local->cont.create.fd,
				  local->cont.create.inode,
				  &local->cont.create.buf);
	return 0;
}


int
afr_create_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno, 
		     fd_t *fd, inode_t *inode, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;
	int child_index = -1;

	local = frame->local;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if (op_ret != -1) {
			local->op_ret = op_ret;

			if ((local->success_count == 0)
			    || (child_index == priv->read_child)) {
				local->cont.create.buf        = *buf;
				local->cont.create.buf.st_ino = 
					afr_itransform (buf->st_ino,
							priv->child_count,
							child_index);
			}
			local->cont.create.inode = inode;

			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_create_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_create_wind_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->create,
					   &local->loc, 
					   local->cont.create.flags, 
					   local->cont.create.mode, 
					   local->cont.create.fd);
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_create_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_create (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc, loc);

	local->cont.create.flags = flags;
	local->cont.create.mode  = mode;
	local->cont.create.fd    = fd_ref (fd);

	local->transaction.fop    = afr_create_wind;
	local->transaction.done   = afr_create_done;
	local->transaction.unwind = afr_create_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.basename = AFR_BASENAME (loc->path);
	local->transaction.pending  = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (frame);
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ mknod */

int
afr_mknod_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame)
		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno,
				  local->cont.mknod.inode,
				  &local->cont.mknod.buf);
	return 0;
}


int
afr_mknod_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno, 
		    inode_t *inode, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;
	int child_index = -1;

	local = frame->local;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);
		
		if (op_ret != -1) {
			local->op_ret = op_ret;

			if ((local->success_count == 0)
			    || (child_index == priv->read_child)) {	
				local->cont.mknod.buf   = *buf;
				local->cont.mknod.buf.st_ino = 
					afr_itransform (buf->st_ino,
							priv->child_count,
							child_index);
			}
			local->cont.mknod.inode = inode;

			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_mknod_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_mknod_wind_cbk, (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->mknod,
					   &local->loc, local->cont.mknod.mode,
					   local->cont.mknod.dev);
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_mknod_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);
	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_mknod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dev_t dev)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc, loc);

	local->cont.mknod.mode  = mode;
	local->cont.mknod.dev   = dev;

	local->transaction.fop    = afr_mknod_wind;
	local->transaction.done   = afr_mknod_done;
	local->transaction.unwind = afr_mknod_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.basename = AFR_BASENAME (loc->path);
	local->transaction.pending  = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ mkdir */


int
afr_mkdir_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame)
		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno,
				  local->cont.mkdir.inode,
				  &local->cont.mkdir.buf);
	return 0;
}


int
afr_mkdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno, 
		    inode_t *inode, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;
	int child_index = -1;

	local = frame->local;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if (op_ret != -1) {
			local->op_ret           = op_ret;

			if ((local->success_count == 0)
			    || (child_index == priv->read_child)) {
				local->cont.mkdir.buf   = *buf;
				local->cont.mkdir.buf.st_ino = 
					afr_itransform (buf->st_ino, priv->child_count,
							child_index);
			}
			local->cont.mkdir.inode = inode;

			local->success_count++;
		}

		local->op_errno         = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_mkdir_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	
	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_mkdir_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->mkdir,
					   &local->loc, local->cont.mkdir.mode);
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_mkdir_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_mkdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc, loc);

	local->cont.mkdir.mode  = mode;

	local->transaction.fop    = afr_mkdir_wind;
	local->transaction.done   = afr_mkdir_done;
	local->transaction.unwind = afr_mkdir_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.basename = AFR_BASENAME (loc->path);
	local->transaction.pending  = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);

		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ link */


int
afr_link_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		local->cont.link.buf.st_ino = local->cont.link.ino;

		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno, 
				  local->cont.link.inode,
				  &local->cont.link.buf);
	}

	return 0;
}


int
afr_link_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		   int32_t op_ret, int32_t op_errno, inode_t *inode,
		   struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;
	int child_index = -1;

	local = frame->local;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if (op_ret != -1) {
			local->op_ret   = op_ret;

			if ((local->success_count == 0)
			    || (child_index == priv->read_child)) {
				local->cont.link.buf        = *buf;
				local->cont.link.buf.st_ino = 
					afr_itransform (buf->st_ino, priv->child_count,
							child_index);
			}
			local->cont.link.inode    = inode;

			local->success_count++;
		}

		local->op_errno = op_errno;		
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_link_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_link_wind_cbk, (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->link,
					   &local->loc,
					   &local->newloc);
			
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_link_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_link (call_frame_t *frame, xlator_t *this,
	  loc_t *oldloc, loc_t *newloc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc,    oldloc);
	loc_copy (&local->newloc, newloc);

	local->cont.link.ino = oldloc->inode->ino;

	local->transaction.fop    = afr_link_wind;
	local->transaction.done   = afr_link_done;
	local->transaction.unwind = afr_link_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, oldloc);

	local->transaction.main_frame   = frame;
	local->transaction.basename     = AFR_BASENAME (oldloc->path);
	local->transaction.new_basename = AFR_BASENAME (newloc->path);
	local->transaction.pending      = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ symlink */


int
afr_symlink_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame)
		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno,
				  local->cont.symlink.inode,
				  &local->cont.symlink.buf);
	return 0;
}


int
afr_symlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		      int32_t op_ret, int32_t op_errno, inode_t *inode,
		      struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;
	int child_index = -1;

	local = frame->local;
	priv = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);
		
		if (op_ret != -1) {
			local->op_ret   = op_ret;

			if ((local->success_count == 0)
			    || (child_index == priv->read_child)) {
				local->cont.symlink.buf        = *buf;
				local->cont.symlink.buf.st_ino = 
					afr_itransform (buf->st_ino, priv->child_count,
							child_index);
			}
			local->cont.symlink.inode    = inode;

			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_symlink_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_symlink_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->symlink,
					   local->cont.symlink.linkpath,
					   &local->loc);

			if (!--call_count)
				break;

		}
	}
	
	return 0;
}


int
afr_symlink_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);
	
	return 0;
}


int
afr_symlink (call_frame_t *frame, xlator_t *this,
	     const char *linkpath, loc_t *loc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;
	
	loc_copy (&local->loc, loc);

	local->cont.symlink.ino      = loc->inode->ino;
	local->cont.symlink.linkpath = strdup (linkpath);

	local->transaction.fop    = afr_symlink_wind;
	local->transaction.done   = afr_symlink_done;
	local->transaction.unwind = afr_symlink_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, loc);

	local->transaction.main_frame   = frame;
	local->transaction.basename     = AFR_BASENAME (loc->path);
	local->transaction.pending      = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ rename */

int
afr_rename_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		local->cont.rename.buf.st_ino = local->cont.rename.ino;

		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno, 
				  &local->cont.rename.buf);
	}

	return 0;
}


int
afr_rename_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;
	int child_index = -1;

	local = frame->local;
	priv  = this->private;

	child_index = (long) cookie;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;
		
			if (buf) {
				local->cont.rename.buf = *buf;
				local->cont.rename.buf.st_ino = 
					afr_itransform (buf->st_ino, priv->child_count,
							child_index);
			}
			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_rename_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_rename_wind_cbk, 
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->rename,
					   &local->loc,
					   &local->newloc);
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_rename_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);
	
	return 0;
}


int32_t
afr_rename (call_frame_t *frame, xlator_t *this,
	    loc_t *oldloc, loc_t *newloc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc,    oldloc);
	loc_copy (&local->newloc, newloc);

	local->cont.rename.ino = oldloc->inode->ino;

	local->transaction.fop    = afr_rename_wind;
	local->transaction.done   = afr_rename_done;
	local->transaction.unwind = afr_rename_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, oldloc);
	afr_build_parent_loc (&local->transaction.new_parent_loc, newloc);

	local->transaction.main_frame   = frame;
	local->transaction.basename     = AFR_BASENAME (oldloc->path);
	local->transaction.new_basename = AFR_BASENAME (newloc->path);
	local->transaction.pending      = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_RENAME_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);

		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ unlink */

int
afr_unlink_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame)
		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno);

	return 0;
}


int
afr_unlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;
	int need_unwind = 0;

	local = frame->local;
	priv  = this->private;
	
	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret   = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->wait_count) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_unlink_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_unlink_wind_cbk,	
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->unlink,
					   &local->loc);
			
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int32_t
afr_unlink_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);
	
	return 0;
}


int32_t
afr_unlink (call_frame_t *frame, xlator_t *this,
	    loc_t *loc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc, loc);

	local->transaction.fop    = afr_unlink_wind;
	local->transaction.done   = afr_unlink_done;
	local->transaction.unwind = afr_unlink_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.basename = AFR_BASENAME (loc->path);
	local->transaction.pending  = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ rmdir */



int
afr_rmdir_unwind (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *main_frame = NULL;
	afr_local_t  *local = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame) {
			main_frame = local->transaction.main_frame;
		}
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame)
		AFR_STACK_UNWIND (main_frame, local->op_ret, local->op_errno);

	return 0;
}


int
afr_rmdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;
	int need_unwind = 0;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->wait_count)
				need_unwind = 1;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.unwind (frame, this);

		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_rmdir_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_rmdir_wind_cbk,	
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->rmdir,
					   &local->loc);

			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_rmdir_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);
	
	return 0;
}


int
afr_rmdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t  * transaction_frame = NULL;
	
	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

	loc_copy (&local->loc, loc);

	local->transaction.fop    = afr_rmdir_wind;
	local->transaction.done   = afr_rmdir_done;
	local->transaction.unwind = afr_rmdir_unwind;

	afr_build_parent_loc (&local->transaction.parent_loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.basename = AFR_BASENAME (loc->path);
	local->transaction.pending  = AFR_ENTRY_PENDING;

	afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ setdents */

int32_t
afr_setdents_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (child_went_down (op_ret, op_errno))
			afr_transaction_child_died (frame, this, child_index);

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;
			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_setdents_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_setdents_wind_cbk,	
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->setdents,
					   local->fd, local->cont.setdents.flags,
					   local->cont.setdents.entries, 
					   local->cont.setdents.count);
			
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int32_t
afr_setdents_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno);
	
	return 0;
}


int32_t
afr_setdents (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t flags, dir_entry_t *entries, int32_t count)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

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

	frame->local = local;

	local->fd = fd_ref (fd);

	local->cont.setdents.flags   = flags;
	local->cont.setdents.entries = entries;
	local->cont.setdents.count   = count;

	local->transaction.fop  = afr_setdents_wind;
	local->transaction.done = afr_setdents_done;

	local->transaction.basename = NULL;
	local->transaction.pending  = AFR_ENTRY_PENDING;

	afr_transaction (frame, this, AFR_ENTRY_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */
