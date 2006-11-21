/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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


#include "layout.h"
#include "xlator.h"


/* getattr */
static int32_t
default_getattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
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
		 const char *path)
{
  STACK_WIND (frame,
	      default_getattr_cbk,
	      this->first_child,
	      this->first_child->fops->getattr,
	      path);
  return 0;
}

/* chmod */
static int32_t
default_chmod_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
	       const char *path,
	       mode_t mode)
{
  STACK_WIND (frame,
	      default_chmod_cbk,
	      this->first_child,
	      this->first_child->fops->chmod,
	      path,
	      mode);
  return 0;
}

/* chown */
static int32_t
default_chown_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
	       const char *path,
	       uid_t uid,
	       gid_t gid)
{
  STACK_WIND (frame,	      
	      default_chown_cbk,
	      this->first_child,
	      this->first_child->fops->chown,
	      path,
	      uid,
	      gid);
  return 0;
}

/* truncate */
static int32_t
default_truncate_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
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
		  const char *path,
		  off_t offset)
{
  STACK_WIND (frame,
	      default_truncate_cbk,
	      this->first_child,
	      this->first_child->fops->truncate,
	      path,
	      offset);
  return 0;
}

/* ftruncate */
static int32_t
default_ftruncate_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
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
		   dict_t *fd,
		   off_t offset)
{
  STACK_WIND (frame,
	      default_ftruncate_cbk,
	      this->first_child,
	      this->first_child->fops->ftruncate,
	      fd,
	      offset);
  return 0;
}

/* utimes */
static int32_t
default_utimes_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
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
default_utimes (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		struct timespec *buf)
{
  STACK_WIND (frame,
	      default_utimes_cbk,
	      this->first_child,
	      this->first_child->fops->utimes,
	      path,
	      buf);
  return 0;
}

/* access */
static int32_t
default_access_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
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
		const char *path,
		mode_t mode)
{
  STACK_WIND (frame,
	      default_access_cbk,
	      this->first_child,
	      this->first_child->fops->access,
	      path,
	      mode);
  return 0;
}


/* readlink */
static int32_t
default_readlink_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      char *dest)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		dest);
  return 0;
}

int32_t
default_readlink (call_frame_t *frame,
		  xlator_t *this,
		  const char *path,
		  size_t size)
{
  STACK_WIND (frame,
	      default_readlink_cbk,
	      this->first_child,
	      this->first_child->fops->readlink,
	      path,
	      size);
  return 0;
}


/* mknod */
static int32_t
default_mknod_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
default_mknod (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       mode_t mode,
	       dev_t dev)
{
  STACK_WIND (frame,
	      default_mknod_cbk,
	      this->first_child,
	      this->first_child->fops->mknod,
	      path,
	      mode,
	      dev);
  return 0;
}


/* mkdir */
static int32_t
default_mkdir_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
default_mkdir (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       mode_t mode)
{
  STACK_WIND (frame,
	      default_mkdir_cbk,
	      this->first_child,
	      this->first_child->fops->mkdir,
	      path,
	      mode);
  return 0;
}


/* unlink */
static int32_t
default_unlink_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
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
		const char *path)
{
  STACK_WIND (frame,
	      default_unlink_cbk,
	      this->first_child,
	      this->first_child->fops->unlink,
	      path);
  return 0;
}



/* rmdir */
static int32_t
default_rmdir_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
	       const char *path)
{
  STACK_WIND (frame,
	      default_rmdir_cbk,
	      this->first_child,
	      this->first_child->fops->rmdir,
	      path);
  return 0;
}

/* symlink */
static int32_t
default_symlink_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
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
default_symlink (call_frame_t *frame,
		 xlator_t *this,
		 const char *oldpath,
		 const char *newpath)
{
  STACK_WIND (frame,
	      default_symlink_cbk,
	      this->first_child,
	      this->first_child->fops->symlink,
	      oldpath,
	      newpath);
  return 0;
}


/* rename */
static int32_t
default_rename_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
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
default_rename (call_frame_t *frame,
		xlator_t *this,
		const char *oldpath,
		const char *newpath)
{
  STACK_WIND (frame,
	      default_rename_cbk,
	      this->first_child,
	      this->first_child->fops->rename,
	      oldpath,
	      newpath);
  return 0;
}


/* link */
static int32_t
default_link_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
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
default_link (call_frame_t *frame,
	      xlator_t *this,
	      const char *oldpath,
	      const char *newpath)
{
  STACK_WIND (frame,
	      default_link_cbk,
	      this->first_child,
	      this->first_child->fops->link,
	      oldpath,
	      newpath);
  return 0;
}


/* create*/
static int32_t
default_create_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *ctx,
		    struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		ctx,
		buf);
  return 0;
}

int32_t
default_create (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		mode_t mode)
{
  STACK_WIND (frame,
	      default_create_cbk,
	      this->first_child,
	      this->first_child->fops->create,
	      path,
	      mode);
  return 0;
}

/* open */
static int32_t
default_open_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *ctx,
		  struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		ctx,
		buf);
  return 0;
}

int32_t
default_open (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      int32_t flags,
	      mode_t mode)
{
  STACK_WIND (frame,
	      default_open_cbk,
	      this->first_child,
	      this->first_child->fops->open,
	      path,
	      flags,
	      mode);
  return 0;
}

/* read */
static int32_t
default_read_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  void *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
default_read (call_frame_t *frame,
	      xlator_t *this,
	      dict_t *fd,
	      size_t size,
	      off_t offset)
{
  STACK_WIND (frame,
	      default_read_cbk,
	      this->first_child,
	      this->first_child->fops->read,
	      fd,
	      size,
	      offset);
  return 0;
}


/* write */
static int32_t
default_write_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
default_write (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *fd,
	       char *buf,
	       size_t size,
	       mode_t mode)
{
  STACK_WIND (frame,
	      default_write_cbk,
	      this->first_child,
	      this->first_child->fops->write,
	      fd,
	      buf,
	      size,
	      mode);
  return 0;
}

/* flush */
static int32_t
default_flush_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
	       dict_t *fd)
{
  STACK_WIND (frame,
	      default_flush_cbk,
	      this->first_child,
	      this->first_child->fops->flush,
	      fd);
  return 0;
}

/* release */
static int32_t
default_release_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
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
		 dict_t *fd)
{
  STACK_WIND (frame,
	      default_release_cbk,
	      this->first_child,
	      this->first_child->fops->release,
	      fd);
  return 0;
}


/* fsync */
static int32_t
default_fsync_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
	       dict_t *fd,
	       int32_t flags)
{
  STACK_WIND (frame,
	      default_fsync_cbk,
	      this->first_child,
	      this->first_child->fops->fsync,
	      fd,
	      flags);
  return 0;
}

/* fgetattr */
static int32_t
default_fgetattr_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
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
		  dict_t *fd)
{
  STACK_WIND (frame,
	      default_fgetattr_cbk,
	      this->first_child,
	      this->first_child->fops->fgetattr,
	      fd);
  return 0;
}

/* opendir */
static int32_t
default_opendir_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dict_t *fd)
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
		 const char *path)
{
  STACK_WIND (frame,
	      default_opendir_cbk,
	      this->first_child,
	      this->first_child->fops->opendir,
	      path);
  return 0;
}


/* readdir */
static int32_t
default_readdir_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
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
		 const char *path)
{
  STACK_WIND (frame,
	      default_readdir_cbk,
	      this->first_child,
	      this->first_child->fops->readdir,
	      path);
  return 0;
}


/* releasedir */
static int32_t
default_releasedir_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
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
		    dict_t *fd)
{
  STACK_WIND (frame,
	      default_releasedir_cbk,
	      this->first_child,
	      this->first_child->fops->releasedir,
	      fd);
  return 0;
}

/* fsyncdir */
static int32_t
default_fsyncdir_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
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
		  dict_t *fd,
		  int32_t flags)
{
  STACK_WIND (frame,
	      default_fsyncdir_cbk,
	      this->first_child,
	      this->first_child->fops->fsyncdir,
	      fd,
	      flags);
  return 0;
}


/* statfs */
static int32_t
default_statfs_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
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
		const char *path)
{
  STACK_WIND (frame,
	      default_statfs_cbk,
	      this->first_child,
	      this->first_child->fops->statfs,
	      path);
  return 0;
}


/* setxattr */
static int32_t
default_setxattr_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
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
		  const char *path,
		  const char *name,
		  const char *value,
		  size_t size,
		  int32_t flags)
{
  STACK_WIND (frame,
	      default_setxattr_cbk,
	      this->first_child,
	      this->first_child->fops->setxattr,
	      path,
	      name,
	      value,
	      size,
	      flags);
  return 0;
}

/* getxattr */
static int32_t
default_getxattr_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      char *value)
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
		  const char *path,
		  const char *name,
		  size_t size)
{
  STACK_WIND (frame,
	      default_getxattr_cbk,
	      this->first_child,
	      this->first_child->fops->getxattr,
	      path,
	      name,
	      size);
  return 0;
}

/* listxattr */
static int32_t
default_listxattr_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       char *value)
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
		   const char *path,
		   size_t size)
{
  STACK_WIND (frame,
	      default_listxattr_cbk,
	      this->first_child,
	      this->first_child->fops->listxattr,
	      path,
	      size);
  return 0;
}

/* removexattr */
static int32_t
default_removexattr_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
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
		     const char *path,
		     const char *name)
{
  STACK_WIND (frame,
	      default_removexattr_cbk,
	      this->first_child,
	      this->first_child->fops->removexattr,
	      path,
	      name);
  return 0;
}

/* lk */
static int32_t
default_lk_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
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
	    dict_t *file,
	    int32_t cmd,
	    struct flock *lock)
{
  STACK_WIND (frame,
	      default_lk_cbk,
	      this->first_child,
	      this->first_child->fops->lk,
	      file,
	      cmd,
	      lock);
  return 0;
}

/* management ops */


/* stats */
static int32_t
default_stats_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
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
	      this->first_child,
	      this->first_child->mops->stats,
	      flags);
  return 0;
}


/* fsck */
static int32_t
default_fsck_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
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
	      this->first_child,
	      this->first_child->mops->fsck,
	      flags);
  return 0;
}


/* lock */
static int32_t
default_lock_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
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
	      this->first_child,
	      this->first_child->mops->lock,
	      path);
  return 0;
}

/* unlock */
static int32_t
default_unlock_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
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
	      this->first_child,
	      this->first_child->mops->unlock,
	      path);
  return 0;
}


/* listlocks */
static int32_t
default_listlocks_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
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
	      this->first_child,
	      this->first_child->mops->listlocks,
	      pattern);
  return 0;
}


/* nslookup */
static int32_t
default_nslookup_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dict_t *ns)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		ns);
  return 0;
}

int32_t
default_nslookup (call_frame_t *frame,
		  xlator_t *this,
		  const char *path)
{
  STACK_WIND (frame,
	      default_nslookup_cbk,
	      this->first_child,
	      this->first_child->mops->nslookup,
	      path);
  return 0;
}

/* nsupdate */
static int32_t
default_nsupdate_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dict_t *ns)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
default_nsupdate (call_frame_t *frame,
		  xlator_t *this,
		  const char *path,
		  dict_t *ns)
{
  STACK_WIND (frame,
	      default_nsupdate_cbk,
	      this->first_child,
	      this->first_child->mops->nsupdate,
	      path,
	      ns);
  return 0;
}

