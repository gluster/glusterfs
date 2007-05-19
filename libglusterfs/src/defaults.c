/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

/* libglusterfs/src/defaults.c:
     This file contains functions, which are used to fill the 'fops' and 'mops' 
     structures in the xlator structures, if they are not written. Here, all the 
     function calls are plainly forwared to the first child of the xlator, and 
     all the *_cbk function does plain STACK_UNWIND of the frame, and returns.

     All the functions are plain enough to understand.
*/

#include "xlator.h"

static int32_t 
default_lookup_cbk (call_frame_t *frame,
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
default_lookup (call_frame_t *frame,
		xlator_t *this,
		inode_t *parent,
		const char *name)
{
  STACK_WIND (frame,
	      default_lookup_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->lookup,
	      parent,
	      name);
  return 0;
}

static int32_t 
default_forget_cbk (call_frame_t *frame,
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
default_forget (call_frame_t *frame,
		xlator_t *this,
		inode_t *inode,
		uint64_t nlookup)
{
  STACK_WIND (frame,
	      default_forget_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->forget,
	      inode,
	      nlookup);
  return 0;
}

static int32_t
default_getattr_cbk (call_frame_t *frame,
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
default_getattr (call_frame_t *frame,
		 xlator_t *this,
		 inode_t *inode)
{
  STACK_WIND (frame,
	      default_getattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getattr,
	      inode);
  return 0;
}

static int32_t
default_chmod_cbk (call_frame_t *frame,
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
default_chmod (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode,
	       mode_t mode)
{
  STACK_WIND (frame,
	      default_chmod_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->chmod,
	      inode,
	      mode);
  return 0;
}


static int32_t
default_fchmod_cbk (call_frame_t *frame,
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
default_fchmod (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		mode_t mode)
{
  STACK_WIND (frame,
	      default_fchmod_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fchmod,
	      fd,
	      mode);
  return 0;
}

static int32_t
default_chown_cbk (call_frame_t *frame,
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
default_chown (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode,
	       uid_t uid,
	       gid_t gid)
{
  STACK_WIND (frame,	      
	      default_chown_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->chown,
	      inode,
	      uid,
	      gid);
  return 0;
}

static int32_t
default_fchown_cbk (call_frame_t *frame,
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
default_fchown (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		uid_t uid,
		gid_t gid)
{
  STACK_WIND (frame,	      
	      default_fchown_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fchown,
	      fd,
	      uid,
	      gid);
  return 0;
}

static int32_t
default_truncate_cbk (call_frame_t *frame,
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
default_truncate (call_frame_t *frame,
		  xlator_t *this,
		  inode_t *inode,
		  off_t offset)
{
  STACK_WIND (frame,
	      default_truncate_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->truncate,
	      inode,
	      offset);
  return 0;
}

static int32_t
default_ftruncate_cbk (call_frame_t *frame,
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

int32_t 
default_utimens_cbk (call_frame_t *frame,
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
default_utimens (call_frame_t *frame,
		 xlator_t *this,
		 inode_t *inode,
		 struct timespec tv[2])
{
  STACK_WIND (frame,
	      default_utimens_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->utimens,
	      inode,
	      tv);
  return 0;
}

int32_t 
default_futimens_cbk (call_frame_t *frame,
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
default_futimens (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  struct timespec tv[2])
{
  STACK_WIND (frame,
	      default_futimens_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->futimens,
	      fd,
	      tv);
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
		inode_t *inode,
		int32_t mask)
{
  STACK_WIND (frame,
	      default_access_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->access,
	      inode,
	      mask);
  return 0;
}


static int32_t
default_readlink_cbk (call_frame_t *frame,
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
default_readlink (call_frame_t *frame,
		  xlator_t *this,
		  inode_t *inode,
		  size_t size)
{
  STACK_WIND (frame,
	      default_readlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->readlink,
	      inode,
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
default_mknod (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *parent,
	       const char *name,
	       mode_t mode,
	       dev_t rdev)
{
  STACK_WIND (frame,
	      default_mknod_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mknod,
	      parent,
	      name,
	      mode,
	      rdev);
  return 0;
}

static int32_t
default_mkdir_cbk (call_frame_t *frame,
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
default_mkdir (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *parent,
	       const char *name,
	       mode_t mode)
{
  STACK_WIND (frame,
	      default_mkdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mkdir,
	      parent,
	      name,
	      mode);
  return 0;
}

static int32_t
default_unlink_cbk (call_frame_t *frame,
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
default_unlink (call_frame_t *frame,
		xlator_t *this,
		inode_t *parent,
		const char *name)
{
  STACK_WIND (frame,
	      default_unlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->unlink,
	      parent,
	      name);
  return 0;
}

static int32_t
default_rmdir_cbk (call_frame_t *frame,
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
default_rmdir (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *parent,
	       const char *name)
{
  STACK_WIND (frame,
	      default_rmdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->rmdir,
	      parent,
	      name);
  return 0;
}

static int32_t
default_symlink_cbk (call_frame_t *frame,
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
default_symlink (call_frame_t *frame,
		 xlator_t *this,
		 const char *linkname,
		 inode_t *parent,
		 const char *name)
{
  STACK_WIND (frame,
	      default_symlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->symlink,
	      linkname,
	      parent,
	      name);
  return 0;
}


static int32_t
default_rename_cbk (call_frame_t *frame,
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
default_rename (call_frame_t *frame,
		xlator_t *this,
		inode_t *olddir,
		const char *oldname,
		inode_t *newdir,
		const char *newname)
{
  STACK_WIND (frame,
	      default_rename_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->rename,
	      olddir,
	      oldname,
	      newdir,
	      newname);
  return 0;
}


static int32_t
default_link_cbk (call_frame_t *frame,
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
default_link (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode,
	      inode_t *newparent,
	      const char *newname)
{
  STACK_WIND (frame,
	      default_link_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->link,
	      inode,
	      newparent,
	      newname);
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
		    struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		fd,
		inode,
		buf);
  return 0;
}

int32_t
default_create (call_frame_t *frame,
		xlator_t *this,
		inode_t *inode,
		const char *name,
		int32_t flags,
		mode_t mode)
{
  STACK_WIND (frame,
	      default_create_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      inode,
	      name,
	      flags,
	      mode);
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
	      inode_t *inode,
	      int32_t flags)
{
  STACK_WIND (frame,
	      default_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      inode,
	      flags);
  return 0;
}

static int32_t
default_readv_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct iovec *vector,
		   int32_t count)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		vector,
		count);
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
		    int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
default_writev (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		struct iovec *vector,
		int32_t count,
		off_t off)
{
  STACK_WIND (frame,
	      default_writev_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->writev,
	      fd,
	      vector,
	      count,
	      off);
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
default_release_cbk (call_frame_t *frame,
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
default_release (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
  STACK_WIND (frame,
	      default_release_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->release,
	      fd);
  return 0;
}


static int32_t
default_fsync_cbk (call_frame_t *frame,
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
default_fgetattr_cbk (call_frame_t *frame,
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
default_fgetattr (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd)
{
  STACK_WIND (frame,
	      default_fgetattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fgetattr,
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
		 inode_t *inode)
{
  STACK_WIND (frame,
	      default_opendir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->opendir,
	      inode);
  return 0;
}


static int32_t
default_readdir_cbk (call_frame_t *frame,
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
default_readdir (call_frame_t *frame,
		 xlator_t *this,
		 size_t size,
		 off_t offset,
		 fd_t *fd)
{
  STACK_WIND (frame,
	      default_readdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->readdir,
	      size,
	      offset,
	      fd);
  return 0;
}


static int32_t
default_releasedir_cbk (call_frame_t *frame,
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
default_releasedir (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd)
{
  STACK_WIND (frame,
	      default_releasedir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->releasedir,
	      fd);
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
		inode_t *inode)
{
  STACK_WIND (frame,
	      default_statfs_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->statfs,
	      inode);
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
		  inode_t *inode,
		  const char *name,
		  const char *value,
		  size_t size,
		  int32_t flags)
{
  STACK_WIND (frame,
	      default_setxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->setxattr,
	      inode,
	      name,
	      value,
	      size,
	      flags);
  return 0;
}

static int32_t
default_getxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      void *value)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		value);
  return 0;
}

int32_t
default_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  inode_t *inode,
		  const char *name,
		  size_t size)
{
  STACK_WIND (frame,
	      default_getxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getxattr,
	      inode,
	      name,
	      size);
  return 0;
}

static int32_t
default_listxattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       void *value)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		value);
  return 0;
}

int32_t
default_listxattr (call_frame_t *frame,
		   xlator_t *this,
		   inode_t *inode,
		   size_t size)
{
  STACK_WIND (frame,
	      default_listxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->listxattr,
	      inode,
	      size);
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
		     inode_t *inode,
		     const char *name)
{
  STACK_WIND (frame,
	      default_removexattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->removexattr,
	      inode,
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
default_fsck_cbk (call_frame_t *frame,
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
default_fsck (call_frame_t *frame,
	      xlator_t *this,
	      int32_t flags)
{
  STACK_WIND (frame,
	      default_fsck_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->fsck,
	      flags);
  return 0;
}


static int32_t
default_lock_cbk (call_frame_t *frame,
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
default_lock (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  STACK_WIND (frame,
	      default_lock_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->lock,
	      path);
  return 0;
}

static int32_t
default_unlock_cbk (call_frame_t *frame,
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
default_unlock (call_frame_t *frame,
		xlator_t *this,
		const char *path)
{
  STACK_WIND (frame,
	      default_unlock_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->unlock,
	      path);
  return 0;
}


static int32_t
default_listlocks_cbk (call_frame_t *frame,
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
default_listlocks (call_frame_t *frame,
		   xlator_t *this,
		   const char *pattern)
{
  STACK_WIND (frame,
	      default_listlocks_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->listlocks,
	      pattern);
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
		 int32_t flags)
{
  STACK_WIND (frame,
	      default_getspec_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->getspec,
	      flags);
  return 0;
}
