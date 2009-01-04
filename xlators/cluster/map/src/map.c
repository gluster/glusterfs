/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "map.h"

/* For <op>_cbk functions */
#include "defaults.c"


int32_t
map_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_stat_cbk,
		    subvol,
		    subvol->fops->stat,
		    loc);

	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_chmod_cbk,
		    subvol,
		    subvol->fops->chmod,
		    loc,
		    mode);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_fchmod (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    mode_t mode)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_fchmod_cbk,
		    subvol,
		    subvol->fops->fchmod,
		    fd,
		    mode);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_chown_cbk,
		    subvol,
		    subvol->fops->chown,
		    loc,
		    uid,
		    gid);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_fchown (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    uid_t uid,
	    gid_t gid)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_fchown_cbk,
		    subvol,
		    subvol->fops->fchown,
		    fd,
		    uid,
		    gid);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_truncate_cbk,
		    subvol,
		    subvol->fops->truncate,
		    loc,
		    offset);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_ftruncate_cbk,
		    subvol,
		    subvol->fops->ftruncate,
		    fd,
		    offset);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_utimens (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     struct timespec tv[2])
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_utimens_cbk,
		    subvol,
		    subvol->fops->utimens,
		    loc,
		    tv);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_access (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t mask)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_access_cbk,
		    subvol,
		    subvol->fops->access,
		    loc,
		    mask);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_readlink_cbk,
		    subvol,
		    subvol->fops->readlink,
		    loc,
		    size);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_unlink_cbk,
		    subvol,
		    subvol->fops->unlink,
		    loc);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_rmdir_cbk,
		    subvol,
		    subvol->fops->rmdir,
		    loc);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
	int32_t op_errno = 1;
	xlator_t *old_subvol = NULL;
	xlator_t *new_subvol = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (oldloc->inode, err);
        VALIDATE_OR_GOTO (oldloc->path, err);
        VALIDATE_OR_GOTO (newloc, err);

	old_subvol = get_mapping_subvol_from_ctx (this, oldloc->inode->ctx);
	if (!old_subvol) {
		op_errno = EINVAL;
		goto err;
	}

	if (newloc->path) {
		new_subvol = get_mapping_subvol_from_path (this, 
							   newloc->path);
		if (new_subvol && (new_subvol != old_subvol)) {
			op_errno = EXDEV;
			goto err;
		}
	}

	STACK_WIND (frame,
		    default_rename_cbk,
		    old_subvol,
		    old_subvol->fops->rename,
		    oldloc, newloc);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_link (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *oldloc,
	  loc_t *newloc)
{
	int32_t op_errno = 1;
	xlator_t *old_subvol = NULL;
	xlator_t *new_subvol = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (oldloc->inode, err);
        VALIDATE_OR_GOTO (oldloc->path, err);
        VALIDATE_OR_GOTO (newloc, err);

	old_subvol = get_mapping_subvol_from_ctx (this, oldloc->inode->ctx);
	if (!old_subvol) {
		op_errno = EINVAL;
		goto err;
	}

	if (newloc->path) {
		new_subvol = get_mapping_subvol_from_path (this, 
							   newloc->path);
		if (new_subvol && (new_subvol != old_subvol)) {
			op_errno = EXDEV;
			goto err;
		}
	}

	STACK_WIND (frame,
		    default_link_cbk,
		    old_subvol,
		    old_subvol->fops->link,
		    oldloc, newloc);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags, fd_t *fd)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_open_cbk,
		    subvol,
		    subvol->fops->open,
		    loc, flags, fd);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_readv_cbk,
		    subvol,
		    subvol->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t off)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_writev_cbk,
		    subvol,
		    subvol->fops->writev,
		    fd,
		    vector,
		    count,
		    off);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_flush_cbk,
		    subvol,
		    subvol->fops->flush,
		    fd);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t flags)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_fsync_cbk,
		    subvol,
		    subvol->fops->fsync,
		    fd,
		    flags);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_fstat (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_fstat_cbk,
		    subvol,
		    subvol->fops->fstat,
		    fd);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_getdents (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset,
	      int32_t flag)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_getdents_cbk,
		    subvol,
		    subvol->fops->getdents,
		    fd,
		    size,
		    offset,
		    flag);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_setdents (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags,
	      dir_entry_t *entries,
	      int32_t count)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_setdents_cbk,
		    subvol,
		    subvol->fops->setdents,
		    fd,
		    flags,
		    entries,
		    count);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_fsyncdir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_fsyncdir_cbk,
		    subvol,
		    subvol->fops->fsyncdir,
		    fd,
		    flags);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}




int32_t
map_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      dict_t *dict,
	      int32_t flags)
{
	/* TODO: support for 'get' 'put' API */
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_setxattr_cbk,
		    subvol,
		    subvol->fops->setxattr,
		    loc,
		    dict,
		    flags);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      const char *name)
{
	/* TODO: support for 'get' 'put' API */
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_getxattr_cbk,
		    subvol,
		    subvol->fops->getxattr,
		    loc,
		    name);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_xattrop (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     gf_xattrop_flags_t flags,
	     dict_t *dict)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_xattrop_cbk,
		    subvol,
		    subvol->fops->xattrop,
		    loc,
		    flags,
		    dict);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_fxattrop (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      gf_xattrop_flags_t flags,
	      dict_t *dict)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_fxattrop_cbk,
		    subvol,
		    subvol->fops->fxattrop,
		    fd,
		    flags,
		    dict);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_removexattr_cbk,
		    subvol,
		    subvol->fops->removexattr,
		    loc,
		    name);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_lk_cbk,
		    subvol,
		    subvol->fops->lk,
		    fd,
		    cmd,
		    lock);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_inodelk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, int32_t cmd, struct flock *lock)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_inodelk_cbk,
		    subvol,
		    subvol->fops->inodelk,
		    loc, cmd, lock);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_finodelk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t cmd, struct flock *lock)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_finodelk_cbk,
		    subvol,
		    subvol->fops->finodelk,
		    fd, cmd, lock);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_entrylk (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, const char *basename,
	     entrylk_cmd cmd, entrylk_type type)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, default_entrylk_cbk,
		    subvol,
		    subvol->fops->entrylk,
		    loc, basename, cmd, type);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_fentrylk (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, const char *basename,
	      entrylk_cmd cmd, entrylk_type type)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame, default_fentrylk_cbk,
		    subvol,
		    subvol->fops->fentrylk,
		    fd, basename, cmd, type);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_checksum (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flag)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	STACK_WIND (frame,
		    default_checksum_cbk,
		    subvol,
		    subvol->fops->checksum,
		    loc,
		    flag);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

static int32_t
map_newentry_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
        call_frame_t *prev = NULL;
        prev  = cookie;
	
	map_itransform (this, prev->this, buf->st_ino, &buf->st_ino);

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;

}


int32_t
map_mknod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode,
	   dev_t rdev)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

	subvol = get_mapping_subvol_from_path (this, loc->path);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}
	
	op_errno = dict_set_static_ptr (loc->inode->ctx, this->name, subvol);
	if (op_errno != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"%s: failed to set subvolume ptr in inode ctx",
			loc->path);
	}

	STACK_WIND (frame,
		    map_newentry_cbk,
		    subvol,
		    subvol->fops->mknod,
		    loc, mode, rdev);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

	subvol = get_mapping_subvol_from_path (this, loc->path);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	op_errno = dict_set_static_ptr (loc->inode->ctx, this->name, subvol);
	if (op_errno != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"%s: failed to set subvolume ptr in inode ctx",
			loc->path);
	}

	STACK_WIND (frame,
		    map_newentry_cbk,
		    subvol,
		    subvol->fops->mkdir,
		    loc, mode);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


int32_t
map_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkpath,
	     loc_t *loc)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

	subvol = get_mapping_subvol_from_path (this, loc->path);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}
	
	op_errno = dict_set_static_ptr (loc->inode->ctx, this->name, subvol);
	if (op_errno != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"%s: failed to set subvolume ptr in inode ctx",
			loc->path);
	}

	STACK_WIND (frame,
		    map_newentry_cbk,
		    subvol,
		    subvol->fops->symlink,
		    linkpath, loc);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}


static int32_t
map_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd,
		inode_t *inode,
		struct stat *buf)
{
        call_frame_t *prev = NULL;
        prev  = cookie;
	
	map_itransform (this, prev->this, buf->st_ino, &buf->st_ino);
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}

int32_t
map_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode, fd_t *fd)
{
	int32_t op_errno = 1;
	xlator_t *subvol   = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

	subvol = get_mapping_subvol_from_path (this, loc->path);
	if (!subvol) {
		op_errno = EINVAL;
		goto err;
	}

	op_errno = dict_set_static_ptr (loc->inode->ctx, this->name, subvol);
	if (op_errno != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"%s: failed to set subvolume ptr in inode ctx",
			loc->path);
	}

	STACK_WIND (frame, map_create_cbk,
		    subvol,
		    subvol->fops->create,
		    loc, flags, mode, fd);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}

int32_t
map_single_lookup_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode,
		       struct stat *buf,
		       dict_t *dict)
{
        call_frame_t *prev = NULL;
        prev  = cookie;
	
	map_itransform (this, prev->this, buf->st_ino, &buf->st_ino);

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf, dict);

	return 0;
}

int32_t
map_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf,
		dict_t *dict)
{
	int callcnt = 0;
	map_local_t *local = NULL;
	inode_t *tmp_inode = NULL;
	dict_t *tmp_dict = NULL;

	local = frame->local;
	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if ((op_ret == 0) && (local->op_ret == -1)) {
			local->op_ret = 0;
			local->stbuf = *buf;
			if (dict)
				local->dict = dict_ref (dict);
			local->inode = inode_ref (inode);
		}
		if (op_ret == -1)
			local->op_errno = op_errno;
		
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		tmp_dict = local->dict;
		tmp_inode = local->inode;

		STACK_UNWIND (frame, local->op_ret, 
			      local->op_errno, local->inode, 
			      &local->stbuf, local->dict);

		inode_unref (local->inode);
		if (tmp_dict)
			dict_unref (tmp_dict);
	}

	return 0;
}

int32_t
map_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t need_xattr)
{
	int32_t op_errno = EINVAL;
	xlator_t *subvol   = NULL;
	map_local_t *local = NULL;
	map_private_t *priv = NULL;
	xlator_list_t *trav = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

	priv = this->private;

	if (loc->inode->ino == 1)
		goto root_inode;

	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		subvol = get_mapping_subvol_from_path (this, loc->path);
		if (!subvol) {
			goto err;
		}

		op_errno = dict_set_static_ptr (loc->inode->ctx, 
						this->name, subvol);
		if (op_errno != 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"%s: failed to set subvolume in inode ctx",
				loc->path);
		}
	}

	/* Just one callback */
	STACK_WIND (frame,
		    map_single_lookup_cbk,
		    subvol,
		    subvol->fops->lookup,
		    loc,
		    need_xattr);

	return 0;

 root_inode:
	local = CALLOC (1, sizeof (map_local_t));

	frame->local = local;
	local->call_count = priv->child_count;
	local->op_ret = -1;

	trav = this->children;
	while (trav) {
		STACK_WIND (frame,
			    map_lookup_cbk,
			    trav->xlator,
			    trav->xlator->fops->lookup,
			    loc,
			    need_xattr);
		trav = trav->next;
	}

	return 0;

 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}
/*
 * unify_normalize_stats -
 */
void
map_normalize_stats (struct statvfs *buf,
		     unsigned long bsize,
		     unsigned long frsize)
{
	double factor;

	if (buf->f_bsize != bsize) {
		factor = ((double) buf->f_bsize) / bsize;
		buf->f_bsize  = bsize;
		buf->f_bfree  = (fsblkcnt_t) (factor * buf->f_bfree);
		buf->f_bavail = (fsblkcnt_t) (factor * buf->f_bavail);
	}
  
	if (buf->f_frsize != frsize) {
		factor = ((double) buf->f_frsize) / frsize;
		buf->f_frsize = frsize;
		buf->f_blocks = (fsblkcnt_t) (factor * buf->f_blocks);
	}
}


int32_t
map_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *stbuf)
{
	struct statvfs *dict_buf = NULL;
	map_local_t  *local = NULL;
	int           this_call_cnt = 0;
	unsigned long bsize;
	unsigned long frsize;

	local = frame->local;

	LOCK (&frame->lock);
	{
		this_call_cnt = --local->call_count;

		if (op_ret == -1) {
			local->op_errno = op_errno;
			goto unlock;
		}
		local->op_ret = 0;
		
		/* when a call is successfull, add it to local->dict */
		dict_buf = &local->statvfs;
		
		if (dict_buf->f_bsize != 0) {
			bsize  = max (dict_buf->f_bsize, 
				      stbuf->f_bsize);
			
			frsize = max (dict_buf->f_frsize, 
				      stbuf->f_frsize);
			map_normalize_stats(dict_buf, bsize, frsize);
			map_normalize_stats(stbuf, bsize, frsize);
		} else {
			dict_buf->f_bsize   = stbuf->f_bsize;
			dict_buf->f_frsize  = stbuf->f_frsize;
		}
		
		dict_buf->f_blocks += stbuf->f_blocks;
		dict_buf->f_bfree  += stbuf->f_bfree;
		dict_buf->f_bavail += stbuf->f_bavail;
		dict_buf->f_files  += stbuf->f_files;
		dict_buf->f_ffree  += stbuf->f_ffree;
		dict_buf->f_favail += stbuf->f_favail;
		dict_buf->f_fsid    = stbuf->f_fsid;
		dict_buf->f_flag    = stbuf->f_flag;
		dict_buf->f_namemax = stbuf->f_namemax;
	}
unlock:
	UNLOCK (&frame->lock);

	if (!this_call_cnt) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno,
			      &local->statvfs);
	}

	return 0;
}

int32_t
map_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
	int32_t op_errno = EINVAL;
	xlator_t *subvol   = NULL;
	map_local_t *local = NULL;
	map_private_t *priv = NULL;
	xlator_list_t *trav = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

	if (loc->inode->ino == 1)
		goto root_inode;
	subvol = get_mapping_subvol_from_ctx (this, loc->inode->ctx);
	if (!subvol) {
		goto err;
	}
	
	/* Just one callback */
	STACK_WIND (frame,
		    default_statfs_cbk,
		    subvol,
		    subvol->fops->statfs,
		    loc);

	return 0;

 root_inode:
	local = CALLOC (1, sizeof (map_local_t));

	priv = this->private;
	frame->local = local;
	local->call_count = priv->child_count;
	local->op_ret = -1;

	trav = this->children;
	while (trav) {
		STACK_WIND (frame,
			    map_statfs_cbk,
			    trav->xlator,
			    trav->xlator->fops->statfs,
			    loc);
		trav = trav->next;
	}

	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}

int32_t
map_opendir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
	int callcnt = 0;
	map_local_t *local = NULL;
	fd_t *local_fd = NULL;

	local = frame->local;
	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret == -1) {
			local->op_errno = op_errno;
			goto unlock;
		}

		local->op_ret = 0;
	}
 unlock:
	UNLOCK (&frame->lock);

	if (!callcnt) {
		local_fd = local->fd;
		local->fd = NULL;

		STACK_UNWIND (frame, local->op_ret, 
			      local->op_errno, local_fd);

		fd_unref (local_fd);
	}
	return 0;
}


int32_t
map_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc, fd_t *fd)
{
	int32_t op_errno = EINVAL;
	xlator_t *subvol   = NULL;
	map_local_t *local = NULL;
	map_private_t *priv = NULL;
	xlator_list_t *trav = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	if (loc->inode->ino == 1) 
		goto root_inode;
	
	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		goto err;
	}
	
	/* Just one callback */
	STACK_WIND (frame,
		    default_opendir_cbk,
		    subvol,
		    subvol->fops->opendir,
		    loc, fd);
	return 0;

 root_inode:
	local = CALLOC (1, sizeof (map_local_t));

	priv = this->private;
	frame->local = local;
	local->call_count = priv->child_count;
	local->op_ret = -1;
	local->fd = fd_ref (fd);

	trav = this->children;
	while (trav) {
		STACK_WIND (frame,
			    map_opendir_cbk,
			    trav->xlator,
			    trav->xlator->fops->opendir,
			    loc, fd);
		trav = trav->next;
	}
	
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


int32_t
map_single_readdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			gf_dirent_t *entries)
{
        call_frame_t *prev = NULL;
	gf_dirent_t  *orig_entry = NULL;

        prev  = cookie;

	list_for_each_entry (orig_entry, &entries->list, list) {
		map_itransform (this, prev->this, orig_entry->d_ino, 
				&orig_entry->d_ino);
	}
	STACK_UNWIND (frame, op_ret, op_errno, entries);
	
	return 0;
}


int
map_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, gf_dirent_t *orig_entries)
{
	map_local_t  *local = NULL;
	gf_dirent_t   entries;
	gf_dirent_t  *orig_entry = NULL;
	gf_dirent_t  *entry = NULL;
	call_frame_t *prev = NULL;
	xlator_t     *subvol = NULL;
	xlator_t     *next = NULL;
	int           count = 0;
	fd_t         *local_fd = NULL;

	INIT_LIST_HEAD (&entries.list);
	prev = cookie;
	local = frame->local;

	if (op_ret < 0)
		goto done;

	list_for_each_entry (orig_entry, &orig_entries->list, list) {
		subvol = prev->this;

		entry = gf_dirent_for_name (orig_entry->d_name);
		if (!entry) {
			gf_log (this->name, GF_LOG_ERROR,
				"memory allocation failed :(");
			goto unwind;
		}
		
		map_itransform (this, subvol, orig_entry->d_ino,
				&entry->d_ino);
		map_itransform (this, subvol, orig_entry->d_off,
				&entry->d_off);
		
		entry->d_type = orig_entry->d_type;
		entry->d_len  = orig_entry->d_len;
		
		list_add_tail (&entry->list, &entries.list);
		count++;
	}

	op_ret = count;

done:
	if (count == 0) {
		next = map_subvol_next (this, prev->this);
		if (!next) {
			goto unwind;
		}

		STACK_WIND (frame, map_readdir_cbk,
			    next, next->fops->readdir,
			    local->fd, local->size, 0);
		return 0;
	}

unwind:
	if (op_ret < 0)
		op_ret = 0;

	local_fd = local->fd;
	local->fd = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, &entries);

	fd_unref (local_fd);

	gf_dirent_free (&entries);

        return 0;
}


int32_t
map_readdir (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t yoff)
{
	int32_t        op_errno = EINVAL;
	xlator_t      *subvol = NULL;
	map_local_t   *local = NULL;
	map_private_t *priv = NULL;
	xlator_t      *xvol = NULL;
	off_t          xoff = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

	if (fd->inode->ino == 1) 
		goto root_inode;

	subvol = get_mapping_subvol_from_ctx (this, fd->inode->ctx);
	if (!subvol) {
		goto err;
	}
	
	/* Just one callback */
	
	STACK_WIND (frame,
		    map_single_readdir_cbk,
		    subvol,
		    subvol->fops->readdir,
		    fd, size, yoff);
	return 0;

 root_inode:
	/* readdir on '/' */
	local = CALLOC (1, sizeof (map_local_t));
	if (!local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}
	
	priv = this->private;
	frame->local = local;
	local->op_errno = ENOENT;
	local->op_ret = -1;

	local->fd = fd_ref (fd);
	local->size = size;

	map_deitransform (this, yoff, &xvol, (uint64_t *)&xoff);

	STACK_WIND (frame, map_readdir_cbk,
		    xvol, xvol->fops->readdir,
		    fd, size, xoff);

	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	return 0;
}


#if 0 
/* TODO : do it later as currently only unify uses this mop and mostly 
   unify will be used below map  */
int32_t
map_stats_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct xlator_stats *stats)
{
	STACK_UNWIND (frame, op_ret, op_errno, stats);
	return 0;
}


int32_t
map_stats (call_frame_t *frame,
	   xlator_t *this,
	   int32_t flags)
{
	STACK_WIND (frame,
		    map_stats_cbk,
		    subvol,
		    subvol->mops->stats,
		    flags);
	return 0;
 err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}
#endif /* if 0 */


/* TODO: define the behavior of notify */


void
fini (xlator_t *this)
{
	map_private_t *priv = NULL;
	struct map_pattern *trav_map = NULL;
	struct map_pattern *tmp_map  = NULL;

	priv = this->private;

	if (priv) {
		if (priv->xlarray)
			FREE (priv->xlarray);

		trav_map = priv->map;
		while (trav_map) {
			tmp_map = trav_map;
			trav_map = trav_map->next;
			FREE (tmp_map);
		}

		FREE(priv);
	}

	return;
}

int
init (xlator_t *this)
{
	map_private_t *priv = NULL;
	xlator_list_t *trav = NULL;
	int   count = 0;
	int   ret = -1;
	char *pattern_string = NULL;
	char *map_pair_str = NULL;
	char *tmp_str = NULL;
	char *tmp_str1 = NULL;
	char *dup_map_pair = NULL;
	char *dir_str = NULL;
	char *subvol_str = NULL;
	char *default_xl = NULL;

	if (!this->children) {
		gf_log (this->name,GF_LOG_ERROR,
			"FATAL: map should have one or more child defined");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	priv = CALLOC (1, sizeof (map_private_t));
	this->private = priv;

	/* allocate xlator array */
	trav = this->children;
	while (trav) {
		count++;
		trav = trav->next;
	}
	priv->xlarray = CALLOC (1, sizeof (struct map_xlator_array) * count);
	priv->child_count = count;

	/* build xlator array */
	count = 0;
	trav = this->children;
	while (trav) {
		priv->xlarray[count++].xl = trav->xlator;
		trav = trav->next;
	}

	/* map dir1:brick1;dir2:brick2;dir3:brick3;*:brick4 */
	ret = dict_get_str (this->options, "map-directory", &pattern_string);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, 
			"map.pattern not given, can't continue");
		goto err;
	}
	map_pair_str = strtok_r (pattern_string, ";", &tmp_str);
	while (map_pair_str) {
		dup_map_pair = strdup (map_pair_str);
		dir_str = strtok_r (dup_map_pair, ":", &tmp_str1);
		if (!dir_str) {
			gf_log (this->name, GF_LOG_ERROR, 
				"directory string invalid");
			goto err;
		}
		subvol_str = strtok_r (NULL, ":", &tmp_str1);
		if (!subvol_str) {
			gf_log (this->name, GF_LOG_ERROR, 
				"mapping subvolume string invalid");
			goto err;
		}
		ret = verify_dir_and_assign_subvol (this, 
						    dir_str, 
						    subvol_str);
		if (ret != 0) {
			gf_log (this->name, GF_LOG_ERROR, 
				"verification failed");
			goto err;
		}
		
		FREE (dup_map_pair);

		map_pair_str = strtok_r (NULL, ";", &tmp_str);
	}

	/* default-volume brick4 */
	ret = dict_get_str (this->options, "default-volume", &default_xl);
	if (ret == 0) {
		ret = assign_default_subvol (this, default_xl);
		if (ret != 0) {
			gf_log (this->name, GF_LOG_ERROR, 
				"assigning default failed");
			goto err;
		}
	}

	verify_if_all_subvolumes_got_used (this);
	
	return 0;
 err:
	fini (this);
	return -1;
}


struct xlator_fops fops = {
	.lookup      = map_lookup,
	.mknod       = map_mknod,
	.create      = map_create,

	.stat        = map_stat,
	.chmod       = map_chmod,
	.chown       = map_chown,
	.fchown      = map_fchown,
	.fchmod      = map_fchmod,
	.fstat       = map_fstat,
	.utimens     = map_utimens,
	.truncate    = map_truncate,
	.ftruncate   = map_ftruncate,
	.access      = map_access,
	.readlink    = map_readlink,
	.setxattr    = map_setxattr,
	.getxattr    = map_getxattr,
	.removexattr = map_removexattr,
	.open        = map_open,
	.readv       = map_readv,
	.writev      = map_writev,
	.flush       = map_flush,
	.fsync       = map_fsync,
	.statfs      = map_statfs,
	.lk          = map_lk,
	.opendir     = map_opendir,
	.readdir     = map_readdir,
	.fsyncdir    = map_fsyncdir,
	.symlink     = map_symlink,
	.unlink      = map_unlink,
	.link        = map_link,
	.mkdir       = map_mkdir,
	.rmdir       = map_rmdir,
	.rename      = map_rename,
	.inodelk     = map_inodelk,
	.finodelk    = map_finodelk,
	.entrylk     = map_entrylk,
	.fentrylk    = map_fentrylk,
	.xattrop     = map_xattrop,
	.fxattrop    = map_fxattrop,
	.setdents    = map_setdents,
	.getdents    = map_getdents,
	.checksum    = map_checksum,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key   = {"map-directory"},  
	  .type  = GF_OPTION_TYPE_ANY 
	},
	{ .key   = {"default-volume"},  
	  .type  = GF_OPTION_TYPE_XLATOR 
	},
	
	{ .key = {NULL} }
};
