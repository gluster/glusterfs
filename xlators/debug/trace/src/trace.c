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
#include <langinfo.h>
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
		  call_frame_t *prev_frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *ctx,
		  struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this);
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_create_cbk (*this=%p, op_ret=%d, op_errno=%d, *ctx=%p), *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, ctx, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, ctx, buf);
  return 0;
}

static int32_t 
trace_open_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *ctx,
		struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this);
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_open_cbk (*this=%p, op_ret=%d, op_errno=%d, *ctx=%p), *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, ctx, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, ctx, buf);
  return 0;
}

static int32_t 
trace_getattr_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this);
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_getattr_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_readv_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
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
		  call_frame_t *prev_frame,
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
		   call_frame_t *prev_frame,
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
		 call_frame_t *prev_frame,
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
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_chown_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_chmod_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_chmod_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_unlink_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
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
		  call_frame_t *prev_frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_rename_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trace_readlink_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    char *buf)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_readlink_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_symlink_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_symlink_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_mknod_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_mknod_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}
  

static int32_t 
trace_mkdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  ERR_EINVAL_NORETURN (!this );
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_mkdir_cbk (*this=%p, op_ret=%d, op_errno=%d",
	  this, op_ret, op_errno);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}
  
static int32_t 
trace_link_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_link_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_flush_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
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
		   call_frame_t *prev_frame,
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
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dict_t *ctx)
{
  ERR_EINVAL_NORETURN (!this );

  gf_log ("trace", GF_LOG_DEBUG, "trace_opendir_cbk (*this=%p, op_ret=%d, op_errno=%d)",
	  this, op_ret, op_errno);
  
  STACK_UNWIND (frame, op_ret, op_errno, ctx);
  return 0;
}

static int32_t 
trace_rmdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
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
		    call_frame_t *prev_frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_truncate_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_utimes_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_utimes_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_statfs_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
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
		    call_frame_t *prev_frame,
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
		    call_frame_t *prev_frame,
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
		     call_frame_t *prev_frame,
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
		       call_frame_t *prev_frame,
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
		      call_frame_t *prev_frame,
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
		    call_frame_t *prev_frame,
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
		  call_frame_t *prev_frame,
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
		     call_frame_t *prev_frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_ftruncate_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_fgetattr_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  ERR_EINVAL_NORETURN (!this );
  
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_fgetattr_cbk (*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
	  this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
trace_lk_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
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
	       const char *path)
{
  ERR_EINVAL_NORETURN (!this || !path );
  
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

  gf_log ("trace", GF_LOG_DEBUG, "trace_getattr (*this=%p, path=%s)\n",
	  this, path);
  STACK_WIND (frame, 
	      trace_getattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->getattr, 
	      path);
  
  return 0;
}

static int32_t 
trace_readlink (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		size_t size)
{
  ERR_EINVAL_NORETURN (!this || !path || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readlink (*this=%p, path=%s, size=%d)",
	  this, path, size);
  
  STACK_WIND (frame, 
	      trace_readlink_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readlink, 
	      path, 
	      size);
  
  return 0;
}

static int32_t 
trace_mknod (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode,
	     dev_t dev)
{
  
  
  ERR_EINVAL_NORETURN (!this || !path);

  gf_log ("trace", GF_LOG_DEBUG, "trace_mknod (*this=%p, path=%s, mode=%d, dev=%lld)",
	     this, path, mode, dev);
  
  STACK_WIND (frame, 
	      trace_mknod_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->mknod, 
	      path, 
	      mode, 
	      dev);
  
  return 0;
}

static int32_t 
trace_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode)
{
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_mkdir (*this=%p, path=%s, mode=%d)",
	  this, path, mode);
  
  STACK_WIND (frame, 
	      trace_mkdir_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->mkdir, 
	      path, 
	      mode);
  return 0;
}

static int32_t 
trace_unlink (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_unlink (*this=%p, path=%s)",
	  this, path);
  
  STACK_WIND (frame, 
	      trace_unlink_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->unlink, 
	      path);
  return 0;
}

static int32_t 
trace_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     const char *path)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_rmdir (*this=%p, path=%s)",
	  this, path);
  
  STACK_WIND (frame, 
	      trace_rmdir_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->rmdir, 
	      path);
  
  return 0;
}

static int32_t 
trace_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *oldpath,
	       const char *newpath)
{
  ERR_EINVAL_NORETURN (!this || !oldpath || *newpath);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_symlink (*this=%p, oldpath=%s, newpath=%s)",
	  this, oldpath, newpath);
  
  STACK_WIND (frame, 
	      trace_symlink_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->symlink, 
	      oldpath,
	      newpath);
  
  return 0;
}

static int32_t 
trace_rename (call_frame_t *frame,
	      xlator_t *this,
	      const char *oldpath,
	      const char *newpath)
{  
  ERR_EINVAL_NORETURN (!this || !oldpath || *newpath);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_rename (*this=%p, oldpath=%s, newpath=%s)",
	  this, oldpath, newpath);
  
  STACK_WIND (frame, 
	      trace_rename_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->rename, 
	      oldpath,
	      newpath);
  
  return 0;
}

static int32_t 
trace_link (call_frame_t *frame,
	    xlator_t *this,
	    const char *oldpath,
	    const char *newpath)
{
  
  ERR_EINVAL_NORETURN (!this || !oldpath || *newpath);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_link (*this=%p, oldpath=%s, newpath=%s)",
	  this, oldpath, newpath);

  STACK_WIND (frame, 
	      trace_link_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->link, 
	      oldpath,
	      newpath);
  return 0;
}

static int32_t 
trace_chmod (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_chmod (*this=%p, path=%s, mode=%o)",
	  this, path, mode);

  STACK_WIND (frame, 
	      trace_chmod_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->chmod, 
	      path,
	      mode);
  
  return 0;
}

static int32_t 
trace_chown (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_chown (*this=%p, path=%s, uid=%d, gid=%d)",
	  this, path, uid, gid);
  
  STACK_WIND (frame, 
	      trace_chown_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->chown, 
	      path,
	      uid,
	      gid);

  return 0;
}

static int32_t 
trace_truncate (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_truncate (*this=%p, path=%s, offset=%lld)",
	  this, path, offset);

  STACK_WIND (frame, 
	      trace_truncate_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->truncate, 
	      path,
	      offset);
  
  return 0;
}

static int32_t 
trace_utimes (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      struct timespec *buf)
{
  char actime_str[256];
  char modtime_str[256];
  
  ERR_EINVAL_NORETURN (!this || !path || !buf);
  
  strftime (actime_str, sizeof(actime_str), nl_langinfo (D_T_FMT), localtime (&buf[0].tv_sec));
  strftime (modtime_str, sizeof(modtime_str), nl_langinfo (D_T_FMT), localtime (&buf[1].tv_sec));
  gf_log ("trace", GF_LOG_DEBUG, "trace_utimes (*this=%p, path=%s, *buf=%p {actime=%s, modtime=%d}) => ret=%d, errno=%d",
	  this, path, buf, actime_str, modtime_str);

  STACK_WIND (frame, 
	      trace_utimes_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->utimes, 
	      path,
	      buf);

  return 0;
}

static int32_t 
trace_open (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    int32_t flags,
	    mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !path);

  gf_log ("trace", GF_LOG_DEBUG, "trace_open (*this=%p, path=%s, flags=%d, mode=%o)",
	  this, path, flags, mode);
  
  STACK_WIND (frame, 
	      trace_open_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->open, 
	      path,
	      flags,
	      mode);
  return 0;
}


static int32_t 
trace_create (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_create (*this=%p, path=%s, mode=%o)",
	  this, path, mode);
  
  STACK_WIND (frame, 
	      trace_create_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->create, 
	      path,
	      mode);
  return 0;
}

static int32_t 
trace_readv (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *ctx,
	     size_t size,
	     off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !ctx || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readv (*this=%p, *ctx=%p, size=%d, offset=%ld)",
	  this, ctx, size, offset);
  
  STACK_WIND (frame, 
	      trace_readv_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readv,
	      ctx,
	      size,
	      offset);
  return 0;
}

static int32_t 
trace_writev (call_frame_t *frame,
	      xlator_t *this,
	      dict_t *ctx,
	      struct iovec *vector,
	      int32_t count,
	      off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !ctx || !vector || (count < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_write (*this=%p, *ctx=%p, *vector=%p, count=%d, offset=%ld)",
	  this, ctx, vector, count, offset);

  STACK_WIND (frame, 
	      trace_writev_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->writev, 
	      ctx,
	      vector,
	      count,
	      offset);
  return 0;
}

static int32_t 
trace_statfs (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_statfs (*this=%p, path=%s)",
	  this, path);

  STACK_WIND (frame, 
	      trace_statfs_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->statfs, 
	      path);
  return 0; 
}

static int32_t 
trace_flush (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *ctx)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_flush (*this=%p, *ctx=%p)",
	  this, ctx);
  STACK_WIND (frame, 
	      trace_flush_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->flush, 
	      ctx);
  return 0;
}

static int32_t 
trace_release (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *ctx)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_release (*this=%p, *ctx=%p)",
	  this, ctx);
  
  STACK_WIND (frame, 
	      trace_release_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->release, 
	      ctx);
  return 0;
}

static int32_t 
trace_fsync (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *ctx,
	     int32_t flags)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsync (*this=%p, flags=%d, *ctx=%p)",
	  this, flags, ctx);

  STACK_WIND (frame, 
	      trace_fsync_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->fsync, 
	      ctx,
	      flags);
  return 0;
}

static int32_t 
trace_setxattr (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		const char *name,
		const char *value,
		size_t size,
		int32_t flags)
{
  ERR_EINVAL_NORETURN (!this || !path || !name || !value || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_setxattr (*this=%p, path=%s, name=%s, value=%s, size=%ld, flags=%d)",
	  this, path, name, value, size, flags);
  
  STACK_WIND (frame, 
	      trace_setxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->setxattr, 
	      path,
	      name,
	      value,
	      size,
	      flags);
  return 0;
}

static int32_t 
trace_getxattr (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		const char *name,
		size_t size)
{
  ERR_EINVAL_NORETURN (!this || !path || !name);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_getxattr (*this=%p, path=%s, name=%s, size=%ld)",
	  this, path, name, size);

  STACK_WIND (frame, 
	      trace_getxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->getxattr,
	      path,
	      name,
	      size);
  return 0;
}

static int32_t 
trace_listxattr (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 size_t size)
{
  ERR_EINVAL_NORETURN (!this || !path || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_listxattr (*this=%p, path=%s, size=%ld)",
	  this, path, size);

  STACK_WIND (frame, 
	      trace_listxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->listxattr, 
	      path,
	      size);

  return 0;
}

static int32_t 
trace_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   const char *path,
		   const char *name)
{
  ERR_EINVAL_NORETURN (!this || !path || !name);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_removexattr (*this=%p, path=%s, name=%s)",
	  this, path, name);

  STACK_WIND (frame, 
	      trace_removexattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->removexattr, 
	      path,
	      name);

  return 0;
}

static int32_t 
trace_opendir (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  ERR_EINVAL_NORETURN (!this || !path );
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_opendir (*this=%p, path=%s)",
	  this, path);

  STACK_WIND (frame, 
	      trace_opendir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->opendir, 
	      path);
  return 0;
}

static int32_t 
trace_readdir (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  if (!this || !path)
    {
      gf_log ("ERROR", 
	      GF_LOG_ERROR, 
	      "%s: %s: (%s) is true", 
	      __FILE__, __FUNCTION__, "(!this || !path)");
    }
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readdir (*this=%p, path=%s)",
	  this, path);

  STACK_WIND (frame, 
	      trace_readdir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readdir, 
	      path);
  return 0;
}

static int32_t 
trace_releasedir (call_frame_t *frame,
		  xlator_t *this,
		  dict_t *ctx)
{  
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_releasedir (*this=%p, *ctx=%p)",
	  this, ctx);
  
  STACK_WIND (frame, 
	      trace_releasedir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->releasedir, 
	      ctx);
  return 0;
}

static int32_t 
trace_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		dict_t *ctx,
		int32_t datasync)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsyncdir (*this=%p, datasync=%d, *ctx=%p)",
	  this, datasync, ctx);

  STACK_WIND (frame, 
	      trace_fsyncdir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->fsyncdir, 
	      ctx,
	      datasync);
  return 0;
}

static int32_t 
trace_access (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_access (*this=%p, path=%s, mode=%o)",
	  this, path, mode);

  STACK_WIND (frame, 
	      trace_access_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->access, 
	      path,
	      mode);
  return 0;
}

static int32_t 
trace_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 dict_t *ctx,
		 off_t offset)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_ftruncate (*this=%p, offset=%ld, *ctx=%p)",
	  this, offset, ctx);

  STACK_WIND (frame, 
	      trace_ftruncate_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->ftruncate, 
	      ctx,
	      offset);
  return 0;
}

static int32_t 
trace_fgetattr (call_frame_t *frame,
		xlator_t *this,
		dict_t *ctx)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fgetattr (*this=%p, *ctx=%p)",
	  this, ctx);
  STACK_WIND (frame, 
	      trace_fgetattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->fgetattr, 
	      ctx);
  return 0;
}

static int32_t 
trace_lk (call_frame_t *frame,
	  xlator_t *this,
	  dict_t *ctx,
	  int32_t cmd,
	  struct flock *lock)
{
  ERR_EINVAL_NORETURN (!this || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_lk (*this=%p, *ctx=%p, cmd=%d, lock=%p {l_type=%d, l_whence=%d, l_start=%lld, l_len=%lld, l_pid=%ld})",
	  this, ctx, cmd, lock,
	  lock->l_type, lock->l_whence, lock->l_start, lock->l_len, lock->l_pid);

  STACK_WIND (frame, 
	      trace_lk_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->lk, 
	      ctx,
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
    
  data_t *debug = dict_get (this->options, "debug");
  if (debug && (strcasecmp (debug->data, "on") == 0))
    {
      private.debug_flag = 1;
      gf_log_set_loglevel (GF_LOG_DEBUG);
      gf_log ("trace", GF_LOG_DEBUG, "trace translator loaded");
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
		 xlator_t *this,
		 struct xlator_stats *stats)
{
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
