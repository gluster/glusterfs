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


/* {{{ chmod */

int32_t
afr_chmod_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (child_went_down (op_ret, op_errno))
			local->transaction.failure_count++;

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;
			if (buf)
				local->cont.chmod.buf = *buf;

			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_chmod_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_chmod_wind_cbk, (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->chmod,
					   &local->loc, 
					   local->cont.chmod.mode); 
		}
	}
	
	return 0;
}


int32_t
afr_chmod_done (call_frame_t *frame, xlator_t *this, 
		int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;

	local = frame->local;

	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	} else {
		local->cont.chmod.buf.st_ino = local->cont.chmod.ino;

		AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->cont.chmod.buf);
	}
	
	return 0;
}


int32_t
afr_chmod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
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

	local->cont.chmod.mode = mode;
	local->cont.chmod.ino  = loc->inode->ino;

	local->transaction.fop   = afr_chmod_wind;
	local->transaction.done  = afr_chmod_done;

	loc_copy (&local->loc, loc);
	
	local->transaction.start   = 0;
	local->transaction.len     = 0;
	local->transaction.pending = AFR_METADATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ chown */

int32_t
afr_chown_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
 
	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (child_went_down (op_ret, op_errno))
			local->transaction.failure_count++;

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;

			if (buf)
				local->cont.chown.buf = *buf;
			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_chown_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_chown_wind_cbk, (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->chown,
					   &local->loc, local->cont.chown.uid,
					   local->cont.chown.gid); 
		}
	}
	
	return 0;
}


int32_t
afr_chown_done (call_frame_t *frame, xlator_t *this, 
		int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	} else {
		local->cont.chown.buf.st_ino = local->cont.chown.ino;

		AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->cont.chown.buf);
	}

	return 0;
}


int32_t
afr_chown (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, uid_t uid, gid_t gid)
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

	local->cont.chown.uid  = uid;
	local->cont.chown.gid  = gid;
	local->cont.chown.ino  = loc->inode->ino;

	local->transaction.fop   = afr_chown_wind;
	local->transaction.done  = afr_chown_done;

	loc_copy (&local->loc, loc);
	
	local->transaction.start   = 0;
	local->transaction.len     = 0;
	local->transaction.pending = AFR_METADATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}


/* }}} */

/* {{{ writev */

int32_t
afr_writev_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
 
	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (child_went_down (op_ret, op_errno))
			local->transaction.failure_count++;

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret   = op_ret;

			if (buf)
				local->cont.writev.buf = *buf;
			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_writev_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	if (local->call_count < priv->child_count) {
		local->transaction.failure_count = 1;
	}

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_writev_wind_cbk, 
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->writev,
					   local->fd, 
					   local->cont.writev.vector,
					   local->cont.writev.count, 
					   local->cont.writev.offset); 
		}
	}
	
	return 0;
}


int32_t
afr_writev_done (call_frame_t *frame, xlator_t *this, 
		 int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	} else {
		local->cont.writev.buf.st_ino = local->cont.writev.ino;

		AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->cont.writev.buf);
	}

	return 0;
}


int32_t
afr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, 
	    struct iovec *vector, int32_t count, off_t offset)
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

	local->cont.writev.vector  = iov_dup (vector, count);
	local->cont.writev.count   = count;
	local->cont.writev.offset  = offset;
	local->cont.writev.ino     = fd->inode->ino;

	if (frame->root->req_refs)
		dict_ref (frame->root->req_refs);

	local->transaction.fop   = afr_writev_wind;
	local->transaction.done  = afr_writev_done;

	local->fd                  = fd_ref (fd);

	if (fd->flags & O_APPEND) {
		local->transaction.start   = 0;
		local->transaction.len     = 0;
	} else {
		local->transaction.start   = offset;
		local->transaction.len     = iov_length (vector, count);
	}

	local->transaction.pending = AFR_DATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}


/* }}} */

/* {{{ truncate */

int32_t
afr_truncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
 
	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (child_went_down (op_ret, op_errno))
			local->transaction.failure_count++;

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;

			if (buf)
				local->cont.truncate.buf = *buf;

			local->success_count++;
		}
		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_truncate_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_truncate_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->truncate,
					   &local->loc, 
					   local->cont.truncate.offset);
		}
	}
	
	return 0;
}


int32_t
afr_truncate_done (call_frame_t *frame, xlator_t *this,
		   int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	} else {
		local->cont.truncate.buf.st_ino = local->cont.truncate.ino;

		AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->cont.truncate.buf);
	}

	return 0;
}


int32_t
afr_truncate (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, off_t offset)
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

	local->op_ret = -1;

	local->cont.truncate.offset  = offset;
	local->cont.truncate.ino     = loc->inode->ino;

	local->transaction.fop  = afr_truncate_wind;
	local->transaction.done = afr_truncate_done;

	loc_copy (&local->loc, loc);

	local->transaction.start   = 0;
	local->transaction.len     = offset;
	local->transaction.pending = AFR_DATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}


/* }}} */

/* {{{ utimens */

int32_t
afr_utimens_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		      int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		
		if (child_went_down (op_ret, op_errno))
			local->transaction.failure_count++;

		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;

			if (buf)
				local->cont.utimens.buf = *buf;
			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_utimens_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_utimens_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->utimens,
					   &local->loc, 
					   local->cont.utimens.tv); 
		}
	}
	
	return 0;
}


int32_t
afr_utimens_done (call_frame_t *frame, xlator_t *this,
		  int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;

	local = frame->local;

	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	} else {
		local->cont.utimens.buf.st_ino = local->cont.utimens.ino;

		AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->cont.utimens.buf);
	}
	
	return 0;
}


int32_t
afr_utimens (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, struct timespec tv[2])
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

	local->op_ret = -1;

	local->cont.utimens.tv[0] = tv[0];
	local->cont.utimens.tv[1] = tv[1];

	local->cont.utimens.ino  = loc->inode->ino;

	local->transaction.fop  = afr_utimens_wind;
	local->transaction.done = afr_utimens_done;

	loc_copy (&local->loc, loc);
	
	local->transaction.start   = 0;
	local->transaction.len     = 0;
	local->transaction.pending = AFR_METADATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ setxattr */


int32_t
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;

			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_setxattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_setxattr_wind_cbk,
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->setxattr,
					   &local->loc, 
					   local->cont.setxattr.dict,
					   local->cont.setxattr.flags); 
		}
	}
	
	return 0;
}


int32_t
afr_setxattr_done (call_frame_t *frame, xlator_t *this,
		   int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno);
	
	return 0;
}


int32_t
afr_setxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, dict_t *dict, int32_t flags)
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

	local->op_ret = -1;

	local->cont.setxattr.dict  = dict_ref (dict);
	local->cont.setxattr.flags = flags;

	local->transaction.fop   = afr_setxattr_wind;
	local->transaction.done  = afr_setxattr_done;

	loc_copy (&local->loc, loc);
	
	local->transaction.start   = 0;
	local->transaction.len     = 0;
	local->transaction.pending = AFR_METADATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ removexattr */


int32_t
afr_removexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
			  int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		if ((op_ret != -1) && (local->success_count == 0)) {
			local->op_ret = op_ret;

			local->success_count++;
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int32_t
afr_removexattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->call_count = up_children_count (priv->child_count, local->child_up);

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_removexattr_wind_cbk, 
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->removexattr,
					   &local->loc, 
					   local->cont.removexattr.name);
		}
	}
	
	return 0;
}


int32_t
afr_removexattr_done (call_frame_t *frame, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	AFR_STACK_UNWIND (frame, local->op_ret, local->op_errno);
	
	return 0;
}


int32_t
afr_removexattr (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, const char *name)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	frame->local = local;

	local->op_ret = -1;

	local->cont.removexattr.name = strdup (name);

	local->transaction.fop   = afr_removexattr_wind;
	local->transaction.done  = afr_removexattr_done;

	loc_copy (&local->loc, loc);
	
	local->transaction.start   = 0;
	local->transaction.len     = 0;
	local->transaction.pending = AFR_METADATA_PENDING;

	afr_inode_transaction (frame, this);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}
