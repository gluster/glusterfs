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
#include "transaction.h"


void
build_parent_loc (loc_t *parent, loc_t *child)
{
	char *tmp = NULL;
	
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

int32_t
afr_create_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno, 
		     fd_t *fd, inode_t *inode, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		if (buf)
			local->cont.create.buf = *buf;
		local->cont.create.inode = inode;
		local->cont.create.fd    = fd;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_create_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_create_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->create,
				&local->cont.create.loc, local->cont.create.flags, 
				local->cont.create.mode, local->cont.create.fd); 
		}
	}
	
	return 0;
}


int32_t
afr_create_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;

	local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno, local->cont.create.fd,
		      local->cont.create.inode, &local->cont.create.buf);
	
	return 0;
}


int32_t
afr_create_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL, NULL);
	return 0;
}


int32_t
afr_create (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.create.loc, loc);

	local->cont.create.flags = flags;
	local->cont.create.mode  = mode;
	local->cont.create.fd    = fd;

	local->transaction.fop     = afr_create_wind;
	local->transaction.success = afr_create_success;
	local->transaction.error   = afr_create_error;

	build_parent_loc (&local->transaction.loc, loc);

	tmp = strdup (loc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ mknod */

int32_t
afr_mknod_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno, 
		    inode_t *inode, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		if (buf)
			local->cont.mknod.buf = *buf;
		local->cont.mknod.inode = inode;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_mknod_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_mknod_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->mknod,
				&local->cont.mknod.loc, local->cont.mknod.mode,
				local->cont.mknod.dev);
		}
	}
	
	return 0;
}


int32_t
afr_mknod_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;

	local = frame->local;

	local->cont.mknod.buf.st_ino = local->cont.mknod.ino;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno, local->cont.mknod.inode, 
		      &local->cont.mknod.buf);
	
	return 0;
}


int32_t
afr_mknod_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL, NULL);
	return 0;
}


int32_t
afr_mknod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dev_t dev)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.mknod.loc, loc);


	local->cont.mknod.mode  = mode;
	local->cont.mknod.dev   = dev;

	local->transaction.fop     = afr_mknod_wind;
	local->transaction.success = afr_mknod_success;
	local->transaction.error   = afr_mknod_error;

	build_parent_loc (&local->transaction.loc, loc);

	tmp = strdup (loc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ mkdir */

int32_t
afr_mkdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		    int32_t op_ret, int32_t op_errno, 
		    inode_t *inode, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		if (buf)
			local->cont.mkdir.buf = *buf;
		local->cont.mkdir.inode = inode;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_mkdir_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_mkdir_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->mkdir,
				&local->cont.mkdir.loc, local->cont.mkdir.mode);
		}
	}
	
	return 0;
}


int32_t
afr_mkdir_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;

	local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno, local->cont.mkdir.loc.inode, 
		      &local->cont.mkdir.buf);
	
	return 0;
}


int32_t
afr_mkdir_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL, NULL);
	return 0;
}


int32_t
afr_mkdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.mkdir.loc, loc);
	local->cont.mkdir.mode  = mode;

	local->transaction.fop     = afr_mkdir_wind;
	local->transaction.success = afr_mkdir_success;
	local->transaction.error   = afr_mkdir_error;

	build_parent_loc (&local->transaction.loc, loc);

	tmp = strdup (loc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ link */

int32_t
afr_link_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		   int32_t op_ret, int32_t op_errno, inode_t *inode,
		   struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		local->cont.link.inode = inode;
		local->cont.link.buf   = *buf;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_link_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_link_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->link,
				&local->cont.link.oldloc,
				local->transaction.new_basename);
		}
	}
	
	return 0;
}


int32_t
afr_link_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	local->cont.link.buf.st_ino = local->cont.link.ino;

	STACK_UNWIND (frame, op_ret, op_errno, local->cont.link.inode,
		      &local->cont.link.buf);
	
	return 0;
}


int32_t
afr_link_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
	return 0;
}


int32_t
afr_link (call_frame_t *frame, xlator_t *this,
	  loc_t *oldloc, const char *path)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.link.oldloc, oldloc);

	local->cont.link.ino = oldloc->inode->ino;

	local->transaction.fop     = afr_link_wind;
	local->transaction.success = afr_link_success;
	local->transaction.error   = afr_link_error;

	build_parent_loc (&local->transaction.loc, oldloc);

	tmp = strdup (oldloc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	tmp = strdup (path);
	local->transaction.new_basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_link_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ symlink */

int32_t
afr_symlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		      int32_t op_ret, int32_t op_errno, inode_t *inode,
		      struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		local->cont.symlink.inode = inode;
		local->cont.symlink.buf   = *buf;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_symlink_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_symlink_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->symlink,
				local->transaction.new_basename,
				&local->cont.symlink.oldloc);
		}
	}
	
	return 0;
}


int32_t
afr_symlink_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	STACK_UNWIND (frame, op_ret, op_errno, local->cont.symlink.inode,
		      &local->cont.symlink.buf);
	
	return 0;
}


int32_t
afr_symlink_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
	return 0;
}


int32_t
afr_symlink (call_frame_t *frame, xlator_t *this,
	     const char *linkpath, loc_t *oldloc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.symlink.oldloc, oldloc);

	local->cont.symlink.ino = oldloc->inode->ino;

	local->transaction.fop     = afr_symlink_wind;
	local->transaction.success = afr_symlink_success;
	local->transaction.error   = afr_symlink_error;

	build_parent_loc (&local->transaction.loc, oldloc);

	tmp = strdup (oldloc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	tmp = strdup (linkpath);
	local->transaction.new_basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_link_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ rename */

int32_t
afr_rename_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		local->cont.rename.buf = *buf;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_rename_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_rename_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->rename,
				&local->cont.rename.oldloc,
				&local->cont.rename.newloc);
		}
	}
	
	return 0;
}


int32_t
afr_rename_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	local->cont.rename.buf.st_ino = local->cont.rename.ino;

	STACK_UNWIND (frame, op_ret, op_errno, &local->cont.rename.buf);
	
	return 0;
}


int32_t
afr_rename_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	STACK_UNWIND (frame, op_ret, op_errno, NULL);
	return 0;
}


int32_t
afr_rename (call_frame_t *frame, xlator_t *this,
	    loc_t *oldloc, loc_t *newloc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.rename.oldloc, oldloc);
	loc_copy (&local->cont.rename.newloc, newloc);

	local->cont.rename.ino     = oldloc->inode->ino;

	local->transaction.fop     = afr_rename_wind;
	local->transaction.success = afr_rename_success;
	local->transaction.error   = afr_rename_error;

	build_parent_loc (&local->transaction.loc, newloc);

	tmp = strdup (oldloc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	tmp = strdup (newloc->path);
	local->transaction.new_basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_link_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ unlink */

int32_t
afr_unlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (!local->success_count) {
			local->cont.unlink.op_ret   = op_ret;
			local->cont.unlink.op_errno = op_errno;
		}

		if (op_ret == 0)
			local->success_count++;

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_unlink_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_unlink_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->unlink,
				&local->cont.unlink.loc);
		}
	}
	
	return 0;
}


int32_t
afr_unlink_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno);
	
	return 0;
}


int32_t
afr_unlink_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
afr_unlink (call_frame_t *frame, xlator_t *this,
	    loc_t *loc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.unlink.loc, loc);

	local->transaction.fop     = afr_unlink_wind;
	local->transaction.success = afr_unlink_success;
	local->transaction.error   = afr_unlink_error;

	build_parent_loc (&local->transaction.loc, loc);

	tmp = strdup (loc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ rmdir */

int32_t
afr_rmdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		     int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = -1;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (!local->success_count) {
			local->cont.rmdir.op_ret   = op_ret;
			local->cont.rmdir.op_errno = op_errno;
		}

		if (op_ret == 0)
			local->success_count++;

	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.resume (frame, priv);
	}
	
	return 0;
}


int32_t
afr_rmdir_wind (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_rmdir_wind_cbk,	
				priv->children[i], 
				priv->children[i]->fops->rmdir,
				&local->cont.rmdir.loc);
		}
	}
	
	return 0;
}


int32_t
afr_rmdir_success (call_frame_t *frame, int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, local->cont.rmdir.op_ret, local->cont.rmdir.op_errno);
	
	return 0;
}


int32_t
afr_rmdir_error (call_frame_t *frame, xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = frame->local;

	FREE (local->transaction.basename);

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
afr_rmdir (call_frame_t *frame, xlator_t *this,
	    loc_t *loc)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	
	char *tmp = NULL;

	int op_ret   = -1;
	int op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

	loc_copy (&local->cont.rmdir.loc, loc);

	local->transaction.fop     = afr_rmdir_wind;
	local->transaction.success = afr_rmdir_success;
	local->transaction.error   = afr_rmdir_error;

	build_parent_loc (&local->transaction.loc, loc);

	tmp = strdup (loc->path);
	local->transaction.basename = strdup (basename (tmp));
	FREE (tmp);

	local->transaction.pending = AFR_ENTRY_PENDING;

	afr_dir_transaction (frame, priv);

	op_ret = 0;
out:
	if (op_ret == -1) {
		STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */

/* {{{ setdents */

/* }}} */
