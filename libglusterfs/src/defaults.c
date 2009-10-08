/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

/* libglusterfs/src/defaults.c:
   This file contains functions, which are used to fill the 'fops' and 'mops'
   structures in the xlator structures, if they are not written. Here, all the
   function calls are plainly forwared to the first child of the xlator, and
   all the *_cbk function does plain STACK_UNWIND of the frame, and returns.

   All the functions are plain enough to understand.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

static int32_t
default_lookup_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf,
                    dict_t *dict,
                    struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
                      dict,
                      postparent);
	return 0;
}

int32_t
default_lookup (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *xattr_req)
{
	STACK_WIND (frame,
		    default_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    xattr_req);
	return 0;
}


int32_t
default_forget (xlator_t *this,
		inode_t *inode)
{
	return 0;
}

static int32_t
default_stat_cbk (call_frame_t *frame,
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
default_stat (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	STACK_WIND (frame,
		    default_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}

static int32_t
default_truncate_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *prebuf,
                      struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      prebuf,
                      postbuf);
	return 0;
}

int32_t
default_truncate (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  off_t offset)
{
	STACK_WIND (frame,
		    default_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc,
		    offset);
	return 0;
}

static int32_t
default_ftruncate_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *prebuf,
                       struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      prebuf,
                      postbuf);
	return 0;
}

int32_t
default_ftruncate (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   off_t offset)
{
	STACK_WIND (frame,
		    default_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd,
		    offset);
	return 0;
}

static int32_t
default_access_cbk (call_frame_t *frame,
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
default_access (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t mask)
{
	STACK_WIND (frame,
		    default_access_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->access,
		    loc,
		    mask);
	return 0;
}


static int32_t
default_readlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      const char *path,
                      struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      path,
                      buf);
	return 0;
}

int32_t
default_readlink (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  size_t size)
{
	STACK_WIND (frame,
		    default_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc,
		    size);
	return 0;
}


static int32_t
default_mknod_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
                   struct stat *buf,
                   struct stat *preparent,
                   struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
                      buf,
                      preparent,
                      postparent);
	return 0;
}

int32_t
default_mknod (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       mode_t mode,
	       dev_t rdev)
{
	STACK_WIND (frame,
		    default_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}

static int32_t
default_mkdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
                   struct stat *buf,
                   struct stat *preparent,
                   struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
                      buf,
                      preparent,
                      postparent);
	return 0;
}

int32_t
default_mkdir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       mode_t mode)
{
	STACK_WIND (frame,
		    default_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);
	return 0;
}

static int32_t
default_unlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct stat *preparent,
                    struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
	return 0;
}

int32_t
default_unlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
	STACK_WIND (frame,
		    default_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}

static int32_t
default_rmdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
                   struct stat *preparent,
                   struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
                      preparent,
                      postparent);
	return 0;
}

int32_t
default_rmdir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
	STACK_WIND (frame,
		    default_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}


static int32_t
default_symlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
                     struct stat *buf,
                     struct stat *preparent,
                     struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf, preparent,
                      postparent);
	return 0;
}

int32_t
default_symlink (call_frame_t *frame,
		 xlator_t *this,
		 const char *linkpath,
		 loc_t *loc)
{
	STACK_WIND (frame,
		    default_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}


static int32_t
default_rename_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf,
                    struct stat *preoldparent,
                    struct stat *postoldparent,
                    struct stat *prenewparent,
                    struct stat *postnewparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf, preoldparent, postoldparent,
                      prenewparent, postnewparent);
	return 0;
}

int32_t
default_rename (call_frame_t *frame,
		xlator_t *this,
		loc_t *oldloc,
		loc_t *newloc)
{
	STACK_WIND (frame,
		    default_rename_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc);
	return 0;
}


static int32_t
default_link_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
                  struct stat *buf,
                  struct stat *preparent,
                  struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf, preparent,
                      postparent);
	return 0;
}

int32_t
default_link (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
	STACK_WIND (frame,
		    default_link_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    oldloc, newloc);
	return 0;
}


static int32_t
default_create_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd,
		    inode_t *inode,
		    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf, preparent,
                      postparent);
	return 0;
}

int32_t
default_create (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flags,
		mode_t mode, fd_t *fd)
{
	STACK_WIND (frame, default_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}

static int32_t
default_open_cbk (call_frame_t *frame,
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
default_open (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags, fd_t *fd,
              int32_t wbflags)
{
	STACK_WIND (frame,
		    default_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd, wbflags);
	return 0;
}

static int32_t
default_readv_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct iovec *vector,
		   int32_t count,
		   struct stat *stbuf,
                   struct iobref *iobref)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      vector,
		      count,
		      stbuf,
                      iobref);
	return 0;
}

int32_t
default_readv (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset)
{
	STACK_WIND (frame,
		    default_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
}


static int32_t
default_writev_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct stat *prebuf,
		    struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
                      prebuf,
		      postbuf);
	return 0;
}

int32_t
default_writev (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		struct iovec *vector,
		int32_t count,
		off_t off,
                struct iobref *iobref)
{
	STACK_WIND (frame,
		    default_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd,
		    vector,
		    count,
		    off,
                    iobref);
	return 0;
}

static int32_t
default_flush_cbk (call_frame_t *frame,
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
default_flush (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd)
{
	STACK_WIND (frame,
		    default_flush_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd);
	return 0;
}


static int32_t
default_fsync_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
                   struct stat *prebuf,
                   struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
                      prebuf,
                      postbuf);
	return 0;
}

int32_t
default_fsync (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       int32_t flags)
{
	STACK_WIND (frame,
		    default_fsync_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsync,
		    fd,
		    flags);
	return 0;
}

static int32_t
default_fstat_cbk (call_frame_t *frame,
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
default_fstat (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd)
{
	STACK_WIND (frame,
		    default_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}

static int32_t
default_opendir_cbk (call_frame_t *frame,
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
default_opendir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc, fd_t *fd)
{
	STACK_WIND (frame,
		    default_opendir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd);
	return 0;
}


static int32_t
default_getdents_cbk (call_frame_t *frame,
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
default_getdents (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  size_t size,
		  off_t offset,
		  int32_t flag)
{
	STACK_WIND (frame,
		    default_getdents_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getdents,
		    fd,
		    size,
		    offset,
		    flag);
	return 0;
}


static int32_t
default_setdents_cbk (call_frame_t *frame,
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
default_setdents (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  int32_t flags,
		  dir_entry_t *entries,
		  int32_t count)
{
	STACK_WIND (frame,
		    default_setdents_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setdents,
		    fd,
		    flags,
		    entries,
		    count);
	return 0;
}


static int32_t
default_fsyncdir_cbk (call_frame_t *frame,
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
default_fsyncdir (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  int32_t flags)
{
	STACK_WIND (frame,
		    default_fsyncdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsyncdir,
		    fd,
		    flags);
	return 0;
}


static int32_t
default_statfs_cbk (call_frame_t *frame,
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
default_statfs (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
	STACK_WIND (frame,
		    default_statfs_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->statfs,
		    loc);
	return 0;
}


static int32_t
default_setxattr_cbk (call_frame_t *frame,
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
default_setxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  dict_t *dict,
		  int32_t flags)
{
	STACK_WIND (frame,
		    default_setxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc,
		    dict,
		    flags);
	return 0;
}


static int32_t
default_fsetxattr_cbk (call_frame_t *frame,
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
default_fsetxattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   dict_t *dict,
                   int32_t flags)
{
	STACK_WIND (frame,
		    default_fsetxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetxattr,
		    fd,
		    dict,
		    flags);
	return 0;
}


static int32_t
default_fgetxattr_cbk (call_frame_t *frame,
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
default_fgetxattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   const char *name)
{
	STACK_WIND (frame,
		    default_fgetxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fgetxattr,
		    fd,
		    name);
	return 0;
}

static int32_t
default_getxattr_cbk (call_frame_t *frame,
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
default_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  const char *name)
{
	STACK_WIND (frame,
		    default_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc,
		    name);
	return 0;
}

int32_t
default_xattrop_cbk (call_frame_t *frame,
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
default_xattrop (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 gf_xattrop_flags_t flags,
		 dict_t *dict)
{
	STACK_WIND (frame,
		    default_xattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->xattrop,
		    loc,
		    flags,
		    dict);
	return 0;
}

int32_t
default_fxattrop_cbk (call_frame_t *frame,
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
default_fxattrop (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  gf_xattrop_flags_t flags,
		  dict_t *dict)
{
	STACK_WIND (frame,
		    default_fxattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fxattrop,
		    fd,
		    flags,
		    dict);
	return 0;
}


static int32_t
default_removexattr_cbk (call_frame_t *frame,
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
default_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     const char *name)
{
	STACK_WIND (frame,
		    default_removexattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc,
		    name);
	return 0;
}

static int32_t
default_lk_cbk (call_frame_t *frame,
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
default_lk (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    int32_t cmd,
	    struct flock *lock)
{
	STACK_WIND (frame,
		    default_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd,
		    cmd,
		    lock);
	return 0;
}


static int32_t
default_inodelk_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
default_inodelk (call_frame_t *frame, xlator_t *this,
		 const char *volume, loc_t *loc, int32_t cmd, 
                 struct flock *lock)
{
	STACK_WIND (frame,
		    default_inodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->inodelk,
		    volume, loc, cmd, lock);
	return 0;
}


static int32_t
default_finodelk_cbk (call_frame_t *frame, void *cookie,
		      xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
default_finodelk (call_frame_t *frame, xlator_t *this,
		  const char *volume, fd_t *fd, int32_t cmd, struct flock *lock)
{
	STACK_WIND (frame,
		    default_finodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->finodelk,
		    volume, fd, cmd, lock);
	return 0;
}


static int32_t
default_entrylk_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
default_entrylk (call_frame_t *frame, xlator_t *this,
		 const char *volume, loc_t *loc, const char *basename,
		 entrylk_cmd cmd, entrylk_type type)
{
	STACK_WIND (frame, default_entrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->entrylk,
		    volume, loc, basename, cmd, type);
	return 0;
}

static int32_t
default_fentrylk_cbk (call_frame_t *frame, void *cookie,
		      xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
default_fentrylk (call_frame_t *frame, xlator_t *this,
		  const char *volume, fd_t *fd, const char *basename,
		  entrylk_cmd cmd, entrylk_type type)
{
	STACK_WIND (frame, default_fentrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fentrylk,
		    volume, fd, basename, cmd, type);
	return 0;
}


/* Management operations */

static int32_t
default_stats_cbk (call_frame_t *frame,
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
default_stats (call_frame_t *frame,
	       xlator_t *this,
	       int32_t flags)
{
	STACK_WIND (frame,
		    default_stats_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->stats,
		    flags);
	return 0;
}

static int32_t
default_getspec_cbk (call_frame_t *frame,
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
default_getspec (call_frame_t *frame,
		 xlator_t *this,
		 const char *key,
		 int32_t flags)
{
	STACK_WIND (frame,
		    default_getspec_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->getspec,
		    key, flags);
	return 0;
}


static int32_t
default_log_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
default_log (call_frame_t *frame,
             xlator_t *this,
             const char *msg)
{
	STACK_WIND (frame,
		    default_log_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->log,
		    msg);
	return 0;
}


static int32_t
default_checksum_cbk (call_frame_t *frame,
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
default_checksum (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t flag)
{
	STACK_WIND (frame,
		    default_checksum_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->checksum,
		    loc,
		    flag);
	return 0;
}


static int32_t
default_rchecksum_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       uint32_t weak_checksum,
                       uint8_t *strong_checksum)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      weak_checksum,
		      strong_checksum);
	return 0;
}


int32_t
default_rchecksum (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd, off_t offset,
                   int32_t len)
{
	STACK_WIND (frame,
		    default_rchecksum_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rchecksum,
		    fd, offset, len);
	return 0;
}


int32_t
default_readdir_cbk (call_frame_t *frame,
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
default_readdirp_cbk (call_frame_t *frame,
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
default_readdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t off)
{
	STACK_WIND (frame,
		    default_readdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readdir,
		    fd, size, off);
	return 0;
}


int32_t
default_readdirp (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  size_t size,
		  off_t off)
{
	STACK_WIND (frame,
		    default_readdirp_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readdirp,
		    fd, size, off);
	return 0;
}

int32_t
default_lock_notify_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
default_lock_fnotify_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			  int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
default_lock_notify (call_frame_t *frame, xlator_t *this, 
		     loc_t *loc, int32_t timeout)
{
	STACK_WIND (frame, 
		    default_lock_notify_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->lock_notify,
		    loc, timeout);
	return 0;
}


int32_t
default_lock_fnotify (call_frame_t *frame, xlator_t *this, 
		      fd_t *fd, int32_t timeout)
{
	STACK_WIND (frame, 
		    default_lock_notify_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->lock_fnotify,
		    fd, timeout);
	return 0;
}


/* notify */
int
default_notify (xlator_t *this, int32_t event, void *data, ...)
{
	switch (event)
	{
	case GF_EVENT_PARENT_UP:
	{
		xlator_list_t *list = this->children;

		while (list)
		{
			xlator_notify (list->xlator, event, this);
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
                        if (parent->xlator->ready)
                                xlator_notify (parent->xlator, event,
                                               this, NULL);
			parent = parent->next;
		}
	}
	}

	return 0;
}

int32_t
default_releasedir (xlator_t *this,
		    fd_t *fd)
{
	return 0;
}

int32_t
default_release (xlator_t *this,
		 fd_t *fd)
{
	return 0;
}

int32_t
default_setattr_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct stat *statpre,
                     struct stat *statpost)
{
	STACK_UNWIND (frame, op_ret, op_errno, statpre, statpost);
	return 0;
}

int32_t
default_setattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 struct stat *stbuf,
                 int32_t valid)
{
	STACK_WIND (frame,
		    default_setattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->setattr,
		    loc, stbuf, valid);
	return 0;
}

int32_t
default_fsetattr_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      struct stat *statpre,
                      struct stat *statpost)
{
	STACK_UNWIND (frame, op_ret, op_errno, statpre, statpost);
	return 0;
}

int32_t
default_fsetattr (call_frame_t *frame,
                  xlator_t *this,
                  fd_t *fd,
                  struct stat *stbuf,
                  int32_t valid)
{
	STACK_WIND (frame,
		    default_fsetattr_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsetattr,
		    fd, stbuf, valid);
	return 0;
}
