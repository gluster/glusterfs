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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

typedef struct {
	int op_count;
} eg_t;

int error_gen (xlator_t *this)
{
	eg_t *egp = NULL;
	int count = 0;
	egp = this->private;
	count = ++egp->op_count;
	if((count % 10) == 0) {
		count = count / 10;
		if ((count % 2) == 0)
			return ENOTCONN;
		else
			return EIO;
	}
	return 0;
}

static int32_t
error_gen_lookup_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf,
		      dict_t *dict)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
		      dict);
	return 0;
}

int32_t
error_gen_lookup (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t need_xattr)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    need_xattr);
	return 0;
}


int32_t
error_gen_forget (xlator_t *this,
		  inode_t *inode)
{
	return 0;
}

int32_t
error_gen_stat_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_stat (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}

int32_t
error_gen_chmod_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_chmod (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 mode_t mode)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_chmod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->chmod,
		    loc,
		    mode);
	return 0;
}


int32_t
error_gen_fchmod_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_fchmod (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  mode_t mode)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_fchmod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fchmod,
		    fd,
		    mode);
	return 0;
}

int32_t
error_gen_chown_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_chown (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 uid_t uid,
		 gid_t gid)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_chown_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->chown,
		    loc,
		    uid,
		    gid);
	return 0;
}

int32_t
error_gen_fchown_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_fchown (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  uid_t uid,
		  gid_t gid)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_fchown_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fchown,
		    fd,
		    uid,
		    gid);
	return 0;
}

int32_t
error_gen_truncate_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_truncate (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    off_t offset)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc,
		    offset);
	return 0;
}

int32_t
error_gen_ftruncate_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_ftruncate (call_frame_t *frame,
		     xlator_t *this,
		     fd_t *fd,
		     off_t offset)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd,
		    offset);
	return 0;
}

int32_t
error_gen_utimens_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}


int32_t
error_gen_utimens (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   struct timespec tv[2])
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_utimens_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->utimens,
		    loc,
		    tv);
	return 0;
}

int32_t
error_gen_access_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_access (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t mask)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_access_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->access,
		    loc,
		    mask);
	return 0;
}


int32_t
error_gen_readlink_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			const char *path)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      path);
	return 0;
}

int32_t
error_gen_readlink (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    size_t size)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc,
		    size);
	return 0;
}


int32_t
error_gen_mknod_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf);
	return 0;
}

int32_t
error_gen_mknod (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 mode_t mode,
		 dev_t rdev)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}

int32_t
error_gen_mkdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf);
	return 0;
}

int32_t
error_gen_mkdir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 mode_t mode)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);
	return 0;
}

int32_t
error_gen_unlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_unlink (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}

int32_t
error_gen_rmdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_rmdir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}


int32_t
error_gen_symlink_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode,
		       struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode,	buf);
	return 0;
}

int32_t
error_gen_symlink (call_frame_t *frame,
		   xlator_t *this,
		   const char *linkpath,
		   loc_t *loc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}


int32_t
error_gen_rename_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
error_gen_rename (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *oldloc,
		  loc_t *newloc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_rename_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc);
	return 0;
}


int32_t
error_gen_link_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode,	buf);
	return 0;
}

int32_t
error_gen_link (call_frame_t *frame,
		xlator_t *this,
		loc_t *oldloc,
		loc_t *newloc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_link_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    oldloc, newloc);
	return 0;
}


int32_t
error_gen_create_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd,
		      inode_t *inode,
		      struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}

int32_t
error_gen_create (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t flags,
		  mode_t mode, fd_t *fd)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}

int32_t
error_gen_open_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      fd);
	return 0;
}

int32_t
error_gen_open (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flags, fd_t *fd)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd);
	return 0;
}

int32_t
error_gen_readv_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct iovec *vector,
		     int32_t count,
		     struct stat *stbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      vector,
		      count,
		      stbuf);
	return 0;
}

int32_t
error_gen_readv (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t offset)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, 0, NULL);
		return 0;
	}


	STACK_WIND (frame,
		    error_gen_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
}


int32_t
error_gen_writev_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *stbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      stbuf);
	return 0;
}

int32_t
error_gen_writev (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  struct iovec *vector,
		  int32_t count,
		  off_t off)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}


	STACK_WIND (frame,
		    error_gen_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd,
		    vector,
		    count,
		    off);
	return 0;
}

int32_t
error_gen_flush_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_flush (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_flush_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd);
	return 0;
}


int32_t
error_gen_fsync_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_fsync (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fsync_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsync,
		    fd,
		    flags);
	return 0;
}

int32_t
error_gen_fstat_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_fstat (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}

int32_t
error_gen_opendir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      fd);
	return 0;
}

int32_t
error_gen_opendir (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc, fd_t *fd)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_opendir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd);
	return 0;
}


int32_t
error_gen_getdents_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dir_entry_t *entries,
			int32_t count)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      entries,
		      count);
	return 0;
}

int32_t
error_gen_getdents (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    size_t size,
		    off_t offset,
		    int32_t flag)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, 0);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_getdents_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getdents,
		    fd,
		    size,
		    offset,
		    flag);
	return 0;
}


int32_t
error_gen_setdents_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_setdents (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    int32_t flags,
		    dir_entry_t *entries,
		    int32_t count)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, 0);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_setdents_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setdents,
		    fd,
		    flags,
		    entries,
		    count);
	return 0;
}


int32_t
error_gen_fsyncdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_fsyncdir (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    int32_t flags)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fsyncdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsyncdir,
		    fd,
		    flags);
	return 0;
}


int32_t
error_gen_statfs_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct statvfs *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_statfs (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_statfs_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->statfs,
		    loc);
	return 0;
}


int32_t
error_gen_setxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_setxattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    dict_t *dict,
		    int32_t flags)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_setxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc,
		    dict,
		    flags);
	return 0;
}

int32_t
error_gen_getxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      dict);
	return 0;
}

int32_t
error_gen_getxattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc,
		    name);
	return 0;
}

int32_t
error_gen_xattrop_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       dict_t *dict)
{
	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
error_gen_xattrop (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   gf_xattrop_flags_t flags,
		   dict_t *dict)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_xattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->xattrop,
		    loc, flags, dict);
	return 0;
}

int32_t
error_gen_fxattrop_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict)
{
	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
error_gen_fxattrop (call_frame_t *frame,
  		    xlator_t *this,
  		    fd_t *fd,
  		    gf_xattrop_flags_t flags,
  		    dict_t *dict)
{
  	int op_errno = 0;
  	op_errno = error_gen(this);
  	if (op_errno) {
  		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
  		STACK_UNWIND (frame, -1, op_errno, NULL);
  		return 0;
  	}

  	STACK_WIND (frame,
  		    error_gen_fxattrop_cbk,
  		    FIRST_CHILD(this),
  		    FIRST_CHILD(this)->fops->fxattrop,
  		    fd, flags, dict);
  	return 0;
}

int32_t
error_gen_removexattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_removexattr (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       const char *name)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_removexattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc,
		    name);
	return 0;
}

int32_t
error_gen_lk_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct flock *lock)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      lock);
	return 0;
}

int32_t
error_gen_lk (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t cmd,
	      struct flock *lock)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd,
		    cmd,
		    lock);
	return 0;
}


int32_t
error_gen_inodelk_cbk (call_frame_t *frame, void *cookie,
		       xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
error_gen_inodelk (call_frame_t *frame, xlator_t *this,
		   loc_t *loc, int32_t cmd, struct flock *lock)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_inodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->inodelk,
		    loc, cmd, lock);
	return 0;
}


int32_t
error_gen_finodelk_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
error_gen_finodelk (call_frame_t *frame, xlator_t *this,
		    fd_t *fd, int32_t cmd, struct flock *lock)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_finodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->finodelk,
		    fd, cmd, lock);
	return 0;
}


int32_t
error_gen_entrylk_cbk (call_frame_t *frame, void *cookie,
		       xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_entrylk (call_frame_t *frame, xlator_t *this,
		   loc_t *loc, const char *basename,
		   gf_dir_lk_cmd cmd, gf_dir_lk_type type)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_entrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->entrylk,
		    loc, basename, cmd, type);
	return 0;
}

int32_t
error_gen_fentrylk_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_fentrylk (call_frame_t *frame, xlator_t *this,
		    fd_t *fd, const char *basename,
		    gf_dir_lk_cmd cmd, gf_dir_lk_type type)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_fentrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fentrylk,
		    fd, basename, cmd, type);
	return 0;
}


/* Management operations */

int32_t
error_gen_stats_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct xlator_stats *stats)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      stats);
	return 0;
}


int32_t
error_gen_stats (call_frame_t *frame,
		 xlator_t *this,
		 int32_t flags)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_stats_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->stats,
		    flags);
	return 0;
}


int32_t
error_gen_fsck_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}


int32_t
error_gen_fsck (call_frame_t *frame,
		xlator_t *this,
		int32_t flags)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fsck_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->fsck,
		    flags);
	return 0;
}

int32_t
error_gen_lock_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}


int32_t
error_gen_lock (call_frame_t *frame,
		xlator_t *this,
		const char *path)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_lock_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->lock,
		    path);
	return 0;
}


int32_t
error_gen_unlock_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}


int32_t
error_gen_unlock (call_frame_t *frame,
		  xlator_t *this,
		  const char *path)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_unlock_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->unlock,
		    path);
	return 0;
}


int32_t
error_gen_listlocks_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 char *locks)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      locks);
	return 0;
}


int32_t
error_gen_listlocks (call_frame_t *frame,
		     xlator_t *this,
		     const char *pattern)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_listlocks_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->listlocks,
		    pattern);
	return 0;
}


int32_t
error_gen_getspec_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       char *spec_data)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      spec_data);
	return 0;
}


int32_t
error_gen_getspec (call_frame_t *frame,
		   xlator_t *this,
		   int32_t flags)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_getspec_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->getspec,
		    flags);
	return 0;
}


int32_t
error_gen_checksum_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			uint8_t *file_checksum,
			uint8_t *dir_checksum)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      file_checksum,
		      dir_checksum);
	return 0;
}


int32_t
error_gen_checksum (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    int32_t flag)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_checksum_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->checksum,
		    loc,
		    flag);
	return 0;
}

int32_t
error_gen_readdir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       gf_dirent_t *entries)
{
	STACK_UNWIND (frame, op_ret, op_errno, entries);
	return 0;
}


int32_t
error_gen_readdir (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   size_t size,
		   off_t off)
{
	int op_errno = 0;
	op_errno = error_gen(this);
	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_readdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readdir,
		    fd, size, off);
	return 0;
}

/* notify */
int32_t
error_gen_notify (xlator_t *this,
		  int32_t event,
		  void *data,
		  ...)
{
	switch (event)
	{
	case GF_EVENT_PARENT_UP:
	{
		xlator_list_t *list = this->children;

		while (list)
		{
			list->xlator->notify (list->xlator, event, this);
			list = list->next;
		}
	}
	break;
	case GF_EVENT_CHILD_DOWN:
	case GF_EVENT_CHILD_UP:
	default:
	{
		xlator_list_t *parent = this->parents;
		while (parent) {
			parent->xlator->notify (parent->xlator, event, this, NULL);
			parent = parent->next;
		}
	}
	}

	return 0;
}

int32_t
error_gen_closedir (xlator_t *this,
		    fd_t *fd)
{
	return 0;
}

int32_t
error_gen_close (xlator_t *this,
		 fd_t *fd)
{
	return 0;
}

int
init (xlator_t *this)
{
	eg_t *pvt = NULL;
	pvt = calloc (1, sizeof (eg_t));
	this->private = pvt;
	return 0;
}

void
fini (xlator_t *this)
{
	return;
}


struct xlator_fops fops = {
	.lookup      = error_gen_lookup,
	.stat        = error_gen_stat,
	.readlink    = error_gen_readlink,
	.mknod       = error_gen_mknod,
	.mkdir       = error_gen_mkdir,
	.unlink      = error_gen_unlink,
	.rmdir       = error_gen_rmdir,
	.symlink     = error_gen_symlink,
	.rename      = error_gen_rename,
	.link        = error_gen_link,
	.chmod       = error_gen_chmod,
	.chown       = error_gen_chown,
	.truncate    = error_gen_truncate,
	.utimens     = error_gen_utimens,
	.create      = error_gen_create,
	.open        = error_gen_open,
	.readv       = error_gen_readv,
	.writev      = error_gen_writev,
	.statfs      = error_gen_statfs,
	.flush       = error_gen_flush,
	.fsync       = error_gen_fsync,
	.setxattr    = error_gen_setxattr,
	.getxattr    = error_gen_getxattr,
	.removexattr = error_gen_removexattr,
	.opendir     = error_gen_opendir,
	.readdir     = error_gen_readdir,
	.getdents    = error_gen_getdents,
	.fsyncdir    = error_gen_fsyncdir,
	.access      = error_gen_access,
	.ftruncate   = error_gen_ftruncate,
	.fstat       = error_gen_fstat,
	.lk          = error_gen_lk,
	.fchmod      = error_gen_fchmod,
	.fchown      = error_gen_fchown,
	.setdents    = error_gen_setdents,
	.lookup_cbk  = error_gen_lookup_cbk,
	.checksum    = error_gen_checksum,
	.xattrop     = error_gen_xattrop,
	.fxattrop    = error_gen_fxattrop,
};

struct xlator_mops mops = {
	.stats = error_gen_stats,
	.fsck = error_gen_fsck,
	.listlocks = error_gen_listlocks,
	.getspec = error_gen_getspec,
};

struct xlator_cbks cbks = {
	.release = error_gen_close,
	.releasedir = error_gen_closedir,
};
