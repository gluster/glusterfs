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

#include <time.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"

#define ERR_EINVAL_NORETURN(cond)                         \
do                                               \
  {						 \
    if ((cond))					 \
      {						 \
	gf_log ("ERROR", 			 \
		GF_LOG_ERROR, 			 \
		"%s: %s: (%s) is true", 	 \
		__FILE__, __FUNCTION__, #cond);	 \
      }                                          \
  } while (0)

extern int32_t errno;

#define _FORMAT_WARN(domain, log_level, format, args...)  printf ("__DEBUG__" format, ##args);     

typedef struct trace_private
{
  int32_t debug_flag;
} trace_private_t;



static int32_t 
trace_create_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd,
		  inode_t *inode,
		  struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this);
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_create_cbk (*this=%p, op_ret=%d, op_errno=%d, *fd=%p), *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, fd, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
  return 0;
}

static int32_t 
trace_open_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
  ERR_EINVAL_NORETURN (!this);

  gf_log ("trace", GF_LOG_DEBUG, "trace_open_cbk (*this=%p, op_ret=%d, op_errno=%d, *fd=%p)",
	  this, op_ret, op_errno, fd);

  STACK_UNWIND (frame, op_ret, op_errno, fd);
  return 0;
}

static int32_t 
trace_getattr_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this);
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_getattr_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_readv_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct iovec *vector,
		 int32_t count)
{
  ERR_EINVAL_NORETURN (!this);

  gf_log ("trace", GF_LOG_DEBUG, "trace_read_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t 
trace_writev_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this);

  gf_log ("trace", GF_LOG_DEBUG, "trace_writev_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_readdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dir_entry_t *entries,
		   int32_t count)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_readdir_cbk (*this=%p, op_ret=%d, op_errno=%d, count=%d)",
	  this, op_ret, op_errno, count);
  
  STACK_UNWIND (frame, op_ret, op_errno, entries, count);
  return 0;
}

static int32_t 
trace_fsync_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_fsync_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_chown_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_chown_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_chmod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_chmod_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_unlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_unlink_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_rename_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_rename_cbk (*this=%p, op_ret=%d, op_errno=%d, inode=%p, buf=%p)",
	  this, op_ret, op_errno, inode, buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
  return 0;
}

static int32_t 
trace_readlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    char *buf)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_readlink_cbk (*this=%p, op_ret=%d, op_errno=%d, buf=%s)",
	  this, op_ret, op_errno, buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_symlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );

  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_symlink_cbk (*this=%p, op_ret=%d, op_errno=%d, *inode=%p, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
  return 0;
}

static int32_t 
trace_mknod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_mknod_cbk (*this=%p, op_ret=%d, op_errno=%d, *inode=%p, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
  return 0;
}
  

static int32_t 
trace_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  ERR_EINVAL_NORETURN (!this );
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_mkdir_cbk (*this=%p, op_ret=%d, op_errno=%d",
	  this, op_ret, op_errno);

  STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
  return 0;
}
  
static int32_t 
trace_link_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_link_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
  return 0;
}

static int32_t 
trace_flush_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_flush_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_release_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_release_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_opendir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_opendir_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, fd);
  return 0;
}

static int32_t 
trace_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_rmdir_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_truncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_truncate_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_utimens_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_utimes_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_statfs_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *buf)
{
  ERR_EINVAL_NORETURN (!this);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_statfs_cbk (*this=%p, *buf=%p {f_bsize=%u, f_frsize=%u, f_blocks=%lu, f_bfree=%lu, f_bavail=%lu, f_files=%lu, f_ffree=%lu, f_favail=%lu, f_fsid=%u, f_flag=%u, f_namemax=%u}) => ret=%d, errno=%d",
	  this, buf, buf->f_bsize, buf->f_frsize, buf->f_blocks, buf->f_bfree, buf->f_bavail, buf->f_files, buf->f_ffree, buf->f_favail, buf->f_fsid, buf->f_flag, buf->f_namemax, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_setxattr_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_getxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    void *value)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_getxattr_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t 
trace_listxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_listxattr_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t 
trace_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_removexattr_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_releasedir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_releasedir_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_fsyncdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_fsyncdir_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_access_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_ftruncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_ftruncate_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_fgetattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
  strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
  strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_fgetattr_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_lk_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct flock *lock)
{
  ERR_EINVAL_NORETURN (!this );
  
  gf_log ("trace",
	  GF_LOG_DEBUG,
	  "trace_lk_cbk (*this=%p, op_ret=%d, op_errno=%d, *lock=%p {l_type=%d, l_whence=%d, l_start=%lld, l_len=%lld, l_pid=%ld})",
	  this, op_ret, op_errno, lock, 
	  lock->l_type, lock->l_whence, lock->l_start, lock->l_len, lock->l_pid);

  STACK_UNWIND (frame, op_ret, op_errno, lock);
  return 0;
}

static int32_t 
trace_getattr (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  ERR_EINVAL_NORETURN (!this || !inode );

#if 0
    dev_t     st_dev;     /* ID of device containing file */
    ino_t     st_ino;     /* inode number */
    mode_t    st_mode;    /* protection */
    nlink_t   st_nlink;   /* number of hard links */
    uid_t     st_uid;     /* user ID of owner */
    gid_t     st_gid;     /* group ID of owner */
    dev_t     st_rdev;    /* device ID (if special file) */
    off_t     st_size;    /* total size, in bytes */
    blksize_t st_blksize; /* blocksize for filesystem I/O */
    blkcnt_t  st_blocks;  /* number of blocks allocated */
    time_t    st_atime;   /* time of last access */
    time_t    st_mtime;   /* time of last modification */
    time_t    st_ctime;   /* time of last status change */
#endif 

  gf_log ("trace", GF_LOG_DEBUG, "trace_getattr (*this=%p, inode=%p)\n",
	  this, inode);
  STACK_WIND (frame, 
	      trace_getattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->getattr, 
	      inode);
  
  return 0;
}

static int32_t 
trace_readlink (call_frame_t *frame,
		xlator_t *this,
		inode_t *inode,
		size_t size)
{
  ERR_EINVAL_NORETURN (!this || !inode || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readlink (*this=%p, inode=%p, size=%d)",
	  this, inode, size);
  
  STACK_WIND (frame, 
	      trace_readlink_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readlink, 
	      inode, 
	      size);
  
  return 0;
}

static int32_t 
trace_mknod (call_frame_t *frame,
	     xlator_t *this,
	     inode_t *parent,
	     const char *name,
	     mode_t mode,
	     dev_t dev)
{
  
  
  ERR_EINVAL_NORETURN (!this || !parent);

  gf_log ("trace", GF_LOG_DEBUG, "trace_mknod (*this=%p, parent=%p, name=%s, mode=%d, dev=%lld)",
	     this, parent, name, mode, dev);
  
  STACK_WIND (frame, 
	      trace_mknod_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->mknod, 
	      parent,
	      name, 
	      mode, 
	      dev);
  
  return 0;
}

static int32_t 
trace_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     inode_t *parent
	     const char *name,
	     mode_t mode)
{
  
  ERR_EINVAL_NORETURN (!this || !parent);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_mkdir (*this=%p, parent=%p, name=%s, mode=%d)",
	  this, parent, name, mode);
  
  STACK_WIND (frame, 
	      trace_mkdir_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->mkdir, 
	      parent,
	      name, 
	      mode);
  return 0;
}

static int32_t 
trace_unlink (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *parent,
	      const char *name)
{
  
  ERR_EINVAL_NORETURN (!this || !parent);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_unlink (*this=%p, parent=%p, name=%s)",
	  this, parent, name);
  
  STACK_WIND (frame, 
	      trace_unlink_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->unlink, 
	      parent,
	      name);
  return 0;
}

static int32_t 
trace_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     inode_t *parent,
	     const char *name)
{
  ERR_EINVAL_NORETURN (!this || !parent);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_rmdir (*this=%p, parent=%p, name=%s)",
	  this, parent, name);
  
  STACK_WIND (frame, 
	      trace_rmdir_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->rmdir, 
	      parent,
	      name);
  
  return 0;
}

static int32_t 
trace_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkname,
	       inode_t *parent,
	       const char *name)
{
  ERR_EINVAL_NORETURN (!this || !linkname || parent);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_symlink (*this=%p, linkname=%s, inode=%p, name=%s)",
	  this, linkname, inode, name);
  
  STACK_WIND (frame, 
	      trace_symlink_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->symlink, 
	      linkname,
	      parent,
	      name);
  
  return 0;
}

static int32_t 
trace_rename (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *olddir,
	      const char *oldname,
	      inote_t *newdir,
	      const char *newname)
{  
  ERR_EINVAL_NORETURN (!this || !olddir || newdir);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_rename (*this=%p, olddir=%p, oldname=%s, newdir=%p, newname=%s)",
	  this, olddir, oldname, newdir, newname);
  
  STACK_WIND (frame, 
	      trace_rename_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->rename, 
	      olddir,
	      oldname,
	      newdir,
	      newname);
  
  return 0;
}

static int32_t 
trace_link (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode,
	    inode_t *newparent,
	    const char *newname)
{
  
  ERR_EINVAL_NORETURN (!this || !inode || *newparent);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_link (*this=%p, oldpath=%s, newpath=%s)",
	  this, oldpath, newpath);

  STACK_WIND (frame, 
	      trace_link_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->link, 
	      inode,
	      newparent,
	      newname);
  return 0;
}

static int32_t 
trace_chmod (call_frame_t *frame,
	     xlator_t *this,
	     inode_t *inode,
	     mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_chmod (*this=%p, inode=%p, mode=%o)",
	  this, inode, mode);

  STACK_WIND (frame, 
	      trace_chmod_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->chmod, 
	      inode,
	      mode);
  
  return 0;
}

static int32_t 
trace_chown (call_frame_t *frame,
	     xlator_t *this,
	     inode_t *inode,
	     uid_t uid,
	     gid_t gid)
{
  ERR_EINVAL_NORETURN (!this || !inode);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_chown (*this=%p, inode=%p, uid=%d, gid=%d)",
	  this, inode, uid, gid);
  
  STACK_WIND (frame, 
	      trace_chown_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->chown, 
	      inode,
	      uid,
	      gid);

  return 0;
}

static int32_t 
trace_truncate (call_frame_t *frame,
		xlator_t *this,
		inode_t *inode,
		off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !inode);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_truncate (*this=%p, inode=%p, offset=%lld)",
	  this, inode, offset);

  STACK_WIND (frame, 
	      trace_truncate_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->truncate, 
	      inode,
	      offset);
  
  return 0;
}

static int32_t 
trace_utimes (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode,
	      struct timespec *tv[2])
{
  char actime_str[256];
  char modtime_str[256];
  
  ERR_EINVAL_NORETURN (!this || !inode || !tv);
  
  strftime (actime_str, 256, "[%b %d %H:%M:%S]", localtime (&tv[0].tv_sec));
  strftime (modtime_str, 256, "[%b %d %H:%M:%S]", localtime (&tv[1].tv_sec));
  gf_log ("trace", GF_LOG_DEBUG, "trace_utimes (*this=%p, inode=%p, *tv=%p {actime=%s, modtime=%s})",
	  this, inode, tv, actime_str, modtime_str);

  STACK_WIND (frame, 
	      trace_utimes_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->utimes, 
	      inode,
	      tv);

  return 0;
}

static int32_t 
trace_open (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode,
	    int32_t flags,
	    mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !inode);

  gf_log ("trace", GF_LOG_DEBUG, "trace_open (*this=%p, inode=%p, flags=%d, mode=%o)",
	  this, inode, flags, mode);
  
  STACK_WIND (frame, 
	      trace_open_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->open, 
	      inode,
	      flags,
	      mode);
  return 0;
}


static int32_t 
trace_create (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *parent,
	      const char *name,
	      int32_t flags,
	      mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !inode);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_create (*this=%p, inode=%p, flags=0%o mode=0%o)",
	  this, inode, flags, mode);
  
  STACK_WIND (frame, 
	      trace_create_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->create, 
	      parent,
	      name,
	      flags,
	      mode);
  return 0;
}

static int32_t 
trace_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !fd || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readv (*this=%p, *fd=%p, size=%d, offset=%ld)",
	  this, fd, size, offset);
  
  STACK_WIND (frame, 
	      trace_readv_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readv,
	      fd,
	      size,
	      offset);
  return 0;
}

static int32_t 
trace_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !fd || !vector || (count < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_write (*this=%p, *fd=%p, *vector=%p, count=%d, offset=%ld)",
	  this, fd, vector, count, offset);

  STACK_WIND (frame, 
	      trace_writev_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->writev, 
	      fd,
	      vector,
	      count,
	      offset);
  return 0;
}

static int32_t 
trace_statfs (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode)
{
  ERR_EINVAL_NORETURN (!this || !inode);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_statfs (*this=%p, inode=%p)",
	  this, inode);

  STACK_WIND (frame, 
	      trace_statfs_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->statfs, 
	      inode);
  return 0; 
}

static int32_t 
trace_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_flush (*this=%p, *fd=%p)",
	  this, fd);
  STACK_WIND (frame, 
	      trace_flush_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->flush, 
	      fd);
  return 0;
}

static int32_t 
trace_release (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_release (*this=%p, *fd=%p)",
	  this, fd);
  
  STACK_WIND (frame, 
	      trace_release_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->release, 
	      fd);
  return 0;
}

static int32_t 
trace_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsync (*this=%p, flags=%d, *fd=%p)",
	  this, flags, fd);

  STACK_WIND (frame, 
	      trace_fsync_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->fsync, 
	      fd,
	      flags);
  return 0;
}

static int32_t 
trace_setxattr (call_frame_t *frame,
		xlator_t *this,
		inode_t *inode,
		const char *name,
		const char *value,
		size_t size,
		int32_t flags)
{
  ERR_EINVAL_NORETURN (!this || !inode || !name || !value || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_setxattr (*this=%p, inode=%p, name=%s, value=%s, size=%ld, flags=%d)",
	  this, inode, name, value, size, flags);
  
  STACK_WIND (frame, 
	      trace_setxattr_cbk, 
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
trace_getxattr (call_frame_t *frame,
		xlator_t *this,
		inode_t *inode,
		const char *name,
		size_t size)
{
  ERR_EINVAL_NORETURN (!this || !inode || !name);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_getxattr (*this=%p, inode=%s, name=%s, size=%ld)",
	  this, inode, name, size);

  STACK_WIND (frame, 
	      trace_getxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->getxattr,
	      inode,
	      name,
	      size);
  return 0;
}

static int32_t 
trace_listxattr (call_frame_t *frame,
		 xlator_t *this,
		 inode_t *inode,
		 size_t size)
{
  ERR_EINVAL_NORETURN (!this || !inode || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_listxattr (*this=%p, inode=%p, size=%ld)",
	  this, inode, size);

  STACK_WIND (frame, 
	      trace_listxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->listxattr, 
	      inode,
	      size);

  return 0;
}

static int32_t 
trace_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   inode_t *inode,
		   const char *name)
{
  ERR_EINVAL_NORETURN (!this || !inode || !name);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_removexattr (*this=%p, inode=%p, name=%s)",
	  this, path, name);

  STACK_WIND (frame, 
	      trace_removexattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->removexattr, 
	      inode,
	      name);

  return 0;
}

static int32_t 
trace_opendir (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  ERR_EINVAL_NORETURN (!this || !inode );
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_opendir (*this=%p, inode=%p)",
	  this, inode);

  STACK_WIND (frame, 
	      trace_opendir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->opendir, 
	      inode);
  return 0;
}

static int32_t 
trace_readdir (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  if (!this || !inode)
    {
      gf_log ("ERROR", 
	      GF_LOG_ERROR, 
	      "%s: %s: (%s) is true", 
	      __FILE__, __FUNCTION__, "(!this || !inode)");
    }
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readdir (*this=%p, inode=%p)",
	  this, inode);

  STACK_WIND (frame, 
	      trace_readdir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readdir, 
	      inode);
  return 0;
}

static int32_t 
trace_releasedir (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd)
{  
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_releasedir (*this=%p, *fd=%p)",
	  this, fd);
  
  STACK_WIND (frame, 
	      trace_releasedir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->releasedir, 
	      fd);
  return 0;
}

static int32_t 
trace_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t datasync)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsyncdir (*this=%p, datasync=%d, *fd=%p)",
	  this, datasync, fd);

  STACK_WIND (frame, 
	      trace_fsyncdir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->fsyncdir, 
	      fd,
	      datasync);
  return 0;
}

static int32_t 
trace_access (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode,
	      mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !inode);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_access (*this=%p, *fd=%p, mode=%o)",
	  this, fd, mode);

  STACK_WIND (frame, 
	      trace_access_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->access, 
	      inode,
	      mode);
  return 0;
}

static int32_t 
trace_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_ftruncate (*this=%p, offset=%ld, *fd=%p)",
	  this, offset, fd);

  STACK_WIND (frame, 
	      trace_ftruncate_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->ftruncate, 
	      fd,
	      offset);
  return 0;
}

static int32_t 
trace_fgetattr (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fgetattr (*this=%p, *fd=%p)",
	  this, fd);
  STACK_WIND (frame, 
	      trace_fgetattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->fgetattr, 
	      fd);
  return 0;
}

static int32_t 
trace_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
  ERR_EINVAL_NORETURN (!this || !fd);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_lk (*this=%p, *fd=%p, cmd=%d, lock=%p {l_type=%d, l_whence=%d, l_start=%lld, l_len=%lld, l_pid=%ld})",
	  this, fd, cmd, lock,
	  lock->l_type, lock->l_whence, lock->l_start, lock->l_len, lock->l_pid);

  STACK_WIND (frame, 
	      trace_lk_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->lk, 
	      fd,
	      cmd,
	      lock);
  return 0;
}


int32_t 
init (xlator_t *this)
{
  trace_private_t private;

  if (!this)
    return -1;


  if (!this->children)
    {
      gf_log ("trace", GF_LOG_ERROR, "trace translator requires one subvolume");
      exit (-1);
    }
    
  if (this->children->next)
    {
      gf_log ("trace", GF_LOG_ERROR, "trace translator does not support more than one sub-volume");
      exit (-1);
    }
    
  private.debug_flag = 1;
  gf_log_set_loglevel (GF_LOG_DEBUG);
  data_t *debug = dict_get (this->options, "debug");
  if (debug && (strcasecmp (debug->data, "off") == 0))
    {
      private.debug_flag = 0;
      gf_log ("trace", GF_LOG_DEBUG, "trace translator debug option is disabled loaded");
    }
  
  void gf_log_xlator (xlator_t *this)
  {
    int32_t len;
    char *buf;
    
    if (!this)
      return;
    
    len = dict_serialized_length (this->options);
    buf = alloca (len);
    dict_serialize (this->options, buf);
    
    gf_log ("trace", GF_LOG_DEBUG, 
	    "init (xlator_t *this=%p {name=%s, *next=%p, *parent=%p, *children=%p {xlator=%p, next=%p}, *fops=%p {*open=%p, getattr=%p, *readlink=%p, *mknod=%p, *mkdir=%p, *unlink=%p, *rmdir=%p, *symlink=%p, *rename=%p, *link=%p, *chmod=%p, *chown=%p, *truncate=%p, *utimes=%p, *read=%p, *write=%p, *statfs=%p, *flush=%p, *release=%p, *fsync=%p, *setxattr=%p, *getxattr=%p, *listxattr=%p, *removexattr=%p, *opendir=%p, *readdir=%p, *releasedir=%p, *fsyncdir=%p, *access=%p, *ftruncate=%p, *fgetattr=%p}, *mops=%p {*stats=%p, *fsck=%p, *lock=%p, *unlock=%p}, *fini()=%p, *init()=%p, *options=%p {%s}, *private=%p)", 
	    this, this->name, this->next, this->parent, this->children, this->children->xlator, this->children->next, this->fops, this->fops->open, this->fops->getattr, this->fops->readlink, this->fops->mknod, this->fops->mkdir, this->fops->unlink, this->fops->rmdir, this->fops->symlink, this->fops->rename, this->fops->link, this->fops->chmod, this->fops->chown, this->fops->truncate, this->fops->utimes, this->fops->readv, this->fops->writev, this->fops->statfs, this->fops->flush, this->fops->release, this->fops->fsync, this->fops->setxattr, this->fops->getxattr, this->fops->listxattr, this->fops->removexattr, this->fops->opendir, this->fops->readdir, this->fops->releasedir, this->fops->fsyncdir, this->fops->access, this->fops->ftruncate, this->fops->fgetattr, this->mops, this->mops->stats,  this->mops->fsck, this->mops->lock, this->mops->unlock, this->fini, this->init, this->options, buf, this->private);
  }
  
  //xlator_foreach (this, gf_log_xlator);

  this->private = &private;
  return 0;
}

void
fini (xlator_t *this)
{
  if (!this)
    return;

  gf_log ("trace", GF_LOG_DEBUG, "fini (xlator_t *this=%p)", this);

  /* Free up the dictionary options */
  dict_destroy (FIRST_CHILD(this)->options);

  gf_log ("trace", GF_LOG_DEBUG, "trace translator unloaded");
  return;
}

struct xlator_fops fops = {
  .getattr     = trace_getattr,
  .readlink    = trace_readlink,
  .mknod       = trace_mknod,
  .mkdir       = trace_mkdir,
  .unlink      = trace_unlink,
  .rmdir       = trace_rmdir,
  .symlink     = trace_symlink,
  .rename      = trace_rename,
  .link        = trace_link,
  .chmod       = trace_chmod,
  .chown       = trace_chown,
  .truncate    = trace_truncate,
  .utimes      = trace_utimes,
  .open        = trace_open,
  .readv       = trace_readv,
  .writev      = trace_writev,
  .statfs      = trace_statfs,
  .flush       = trace_flush,
  .release     = trace_release,
  .fsync       = trace_fsync,
  .setxattr    = trace_setxattr,
  .getxattr    = trace_getxattr,
  .listxattr   = trace_listxattr,
  .removexattr = trace_removexattr,
  .opendir     = trace_opendir,
  .readdir     = trace_readdir,
  .releasedir  = trace_releasedir,
  .fsyncdir    = trace_fsyncdir,
  .access      = trace_access,
  .ftruncate   = trace_ftruncate,
  .fgetattr    = trace_fgetattr,
  .create      = trace_create,
  .lk          = trace_lk,
};

static int32_t 
trace_stats_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct xlator_stats *stats)
{
  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}

static int32_t 
trace_stats (call_frame_t *frame,
	     xlator_t *this, 
	     int32_t flags)
{
  ERR_EINVAL_NORETURN (!this);
  
  {
    gf_log ("trace", GF_LOG_DEBUG, "trace_stats (*this=%p, flags=%d\n", this, flags);

    STACK_WIND (frame, 
		trace_stats_cbk, 
		FIRST_CHILD(this), 
		FIRST_CHILD(this)->mops->stats, 
		flags);
  }
  return 0;
}

struct xlator_mops mops = {
  .stats = trace_stats
};
