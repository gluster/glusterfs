/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/**
 * xlators/debug/trace :
 *    This translator logs all the arguments to the fops/mops and also 
 *    their _cbk functions, which later passes the call to next layer. 
 *    Very helpful translator for debugging.
 */

#include <time.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"
#include "common-utils.h"

#define ERR_EINVAL_NORETURN(cond)                \
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

#define _FORMAT_WARN(domain, log_level, format, args...)  printf ("__DEBUG__" format, ##args);     

typedef struct trace_private {
	int32_t debug_flag;
} trace_private_t;

struct {
	char *name;
	int enabled;
} trace_fop_names[GF_FOP_MAXVALUE];

int32_t 
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

	if (trace_fop_names[GF_FOP_CREATE].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

			gf_log (this->name, GF_LOG_NORMAL, 
				"(*this=%s, op_ret=%d, op_errno=%d, fd=%p, inode=%p), *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this->name, op_ret, op_errno, fd, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}

int32_t 
trace_open_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this);

	if (trace_fop_names[GF_FOP_OPEN].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, *fd=%p)",
			this, op_ret, op_errno, fd);
	}

	STACK_UNWIND (frame, op_ret, op_errno, fd);
	return 0;
}

int32_t 
trace_stat_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this);
  
	if (trace_fop_names[GF_FOP_STAT].enabled) {

		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}    

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_readv_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct iovec *vector,
		 int32_t count,
		 struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this);

	if (trace_fop_names[GF_FOP_READ].enabled) {

		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}
  
	STACK_UNWIND (frame, op_ret, op_errno, vector, count, buf);
	return 0;
}

int32_t 
trace_writev_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this);

	if (trace_fop_names[GF_FOP_WRITE].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));

			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_getdents_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entries,
		    int32_t count)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_GETDENTS].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, count=%d)",
			this, op_ret, op_errno, count);
	}
  
	STACK_UNWIND (frame, op_ret, op_errno, entries, count);
	return 0;
}

int32_t 
trace_readdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   gf_dirent_t *buf)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_READDIR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(op_ret=%d, op_errno=%d)",
			op_ret, op_errno);
	}
  
	STACK_UNWIND (frame, op_ret, op_errno, buf);

	return 0;
}

int32_t 
trace_fsync_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FSYNC].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_chown_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_CHOWN].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_chmod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_CHMOD].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_fchmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FCHMOD].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_fchown_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FCHOWN].enabled) {  
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_unlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_UNLINK].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_rename_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_RENAME].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, buf=%p)",
			this, op_ret, op_errno, buf);
	}
  
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_readlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    const char *buf)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_READLINK].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, buf=%s)",
			this, op_ret, op_errno, buf);
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_lookup_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf,
		  dict_t *xattr)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_LOOKUP].enabled) {
		if (op_ret >= 0) {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"callid: %lld (*this=%p, op_ret=%d, op_errno=%d, inode=%p, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld})",
				(long long) frame->root->unique, this, op_ret, op_errno, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf, xattr);
	return 0;
}

int32_t 
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

	if (trace_fop_names[GF_FOP_SYMLINK].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, inode=%p, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t 
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

	if (trace_fop_names[GF_FOP_MKNOD].enabled) {  
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, inode=%p, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}
  

int32_t 
trace_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_MKDIR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, inode=%p",
			this, op_ret, op_errno, inode);
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}
  
int32_t 
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

	if (trace_fop_names[GF_FOP_LINK].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, inode=%p, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, inode, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t 
trace_flush_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FLUSH].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t 
trace_opendir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_OPENDIR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, fd=%p)",
			this, op_ret, op_errno, fd);
	}

	STACK_UNWIND (frame, op_ret, op_errno, fd);
	return 0;
}

int32_t 
trace_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_RMDIR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_truncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_TRUNCATE].enabled) {  
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_utimens_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_UTIMENS].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_statfs_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *buf)
{
	ERR_EINVAL_NORETURN (!this);

	if (trace_fop_names[GF_FOP_STATFS].enabled) {
		if (op_ret >= 0) {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, *buf=%p {f_bsize=%u, f_frsize=%u, f_blocks=%lu, f_bfree=%lu, f_bavail=%lu, f_files=%lu, f_ffree=%lu, f_favail=%lu, f_fsid=%u, f_flag=%u, f_namemax=%u}) => ret=%d, errno=%d",
				this, buf, buf->f_bsize, buf->f_frsize, buf->f_blocks, buf->f_bfree, buf->f_bavail, buf->f_files, buf->f_ffree, buf->f_favail, buf->f_fsid, buf->f_flag, buf->f_namemax, op_ret, op_errno);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_SETXATTR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_getxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *dict)
{
	ERR_EINVAL_NORETURN (!this || !dict);

	if (trace_fop_names[GF_FOP_GETXATTR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d, dict=%p)",
			this, op_ret, op_errno, dict);
	}

	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t 
trace_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t 
trace_fsyncdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_ACCESS].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, op_ret=%d, op_errno=%d)",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_ftruncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_fstat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
	char atime_buf[256], mtime_buf[256], ctime_buf[256];
	ERR_EINVAL_NORETURN (!this );
  
	if (trace_fop_names[GF_FOP_FSTAT].enabled) {
		if (op_ret >= 0) {
			strftime (atime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_atime));
			strftime (mtime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_mtime));
			strftime (ctime_buf, 256, "[%b %d %H:%M:%S]", localtime (&buf->st_ctime));
    
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})",
				this, op_ret, op_errno, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
trace_lk_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct flock *lock)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_LK].enabled) {
		if (op_ret >= 0) {
			gf_log (this->name,
				GF_LOG_NORMAL,
				"(*this=%p, op_ret=%d, op_errno=%d, *lock=%p {l_type=%d, l_whence=%d, l_start=%lld, l_len=%lld, l_pid=%ld})",
				this, op_ret, op_errno, lock, 
				lock->l_type, lock->l_whence, lock->l_start, lock->l_len, lock->l_pid);
		} else {
			gf_log (this->name, 
				GF_LOG_NORMAL, 
				"(*this=%p, op_ret=%d, op_errno=%d)",
				this, op_ret, op_errno);
		}    
	}

	STACK_UNWIND (frame, op_ret, op_errno, lock);
	return 0;
}


int32_t 
trace_setdents_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_SETDENTS].enabled) {  
		gf_log (this->name,
			GF_LOG_NORMAL,
			"*this=%p, op_ret=%d, op_errno=%d",
			this, op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t 
trace_entrylk_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL,
			"op_ret=%d, op_errno=%d",
			op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t 
trace_xattrop_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dict_t *dict)
{
	ERR_EINVAL_NORETURN (!this || !dict);

	if (trace_fop_names[GF_FOP_XATTROP].enabled) {
		gf_log (this->name, GF_LOG_NORMAL, 
			"(op_ret=%d, op_errno=%d)",
			op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t 
trace_fxattrop_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *dict)
{
	ERR_EINVAL_NORETURN (!this || !dict);

	if (trace_fop_names[GF_FOP_FXATTROP].enabled) {
		gf_log (this->name, GF_LOG_NORMAL, 
			"(op_ret=%d, op_errno=%d)",
			op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t 
trace_inodelk_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_INODELK].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL,
			"op_ret=%d, op_errno=%d",
			op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
trace_entrylk (call_frame_t *frame, xlator_t *this,
	       loc_t *loc, const char *basename,
	       gf_dir_lk_cmd cmd, gf_dir_lk_type type)
{
	ERR_EINVAL_NORETURN (!this || !loc || !basename);

	if (trace_fop_names[GF_FOP_ENTRYLK].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL, 
			"callid: %lld (loc=%p {path=%s, inode=%p} basename=%s, cmd=%s, type=%s)",
			(long long) frame->root->unique, loc, loc->path,
			loc->inode, basename, cmd == GF_DIR_LK_LOCK ? "GF_DIR_LK_LOCK" : "GF_DIR_LK_UNLOCK", type == GF_DIR_LK_RDLCK ? "GF_DIR_LK_RDLCK" : "GF_DIR_LK_WRLCK");
	}

	STACK_WIND (frame, 
		    trace_entrylk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->entrylk,
		    loc, basename, cmd, type);
	return 0;
}

int32_t
trace_inodelk (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc, int32_t cmd, struct flock *flock)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_INODELK].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL, 
			"callid: %lld (loc {path=%s, inode=%p}, fd=%p, cmd=%s)",
			(long long) frame->root->unique, loc ? loc->path : NULL, 
			loc ? loc->inode : NULL, cmd == F_SETLK ? "F_SETLK" : "unknown");
	}

	STACK_WIND (frame, 
		    trace_inodelk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->inodelk,
		    loc, cmd, flock);
	return 0;
}


int32_t 
trace_finodelk_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	ERR_EINVAL_NORETURN (!this );

	if (trace_fop_names[GF_FOP_FINODELK].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL,
			"op_ret=%d, op_errno=%d",
			op_ret, op_errno);
	}

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
trace_finodelk (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd, int32_t cmd, struct flock *flock)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FINODELK].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL, 
			"callid: %lld (fd=%p, cmd=%s)",
			(long long) frame->root->unique, fd,
			cmd == F_SETLK ? "F_SETLK" : "unknown");
	}

	STACK_WIND (frame, 
		    trace_finodelk_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->finodelk,
		    fd, cmd, flock);
	return 0;
}


int32_t
trace_xattrop (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       gf_xattrop_flags_t flags,
	       dict_t *dict)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_XATTROP].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL, 
			"callid: %lld (path=%s, flags=%d)",
			(long long) frame->root->unique, loc->path, flags);
			
	}
  
	STACK_WIND (frame, trace_xattrop_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->xattrop, 
		    loc, flags, dict);

	return 0;
}

int32_t
trace_fxattrop (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		gf_xattrop_flags_t flags,
		dict_t *dict)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FXATTROP].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL, 
			"callid: %lld (fd=%p, flags=%d)",
			(long long) frame->root->unique, fd, flags);
			
	}
  
	STACK_WIND (frame, trace_fxattrop_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->fxattrop, 
		    fd, flags, dict);

	return 0;
}

int32_t 
trace_lookup (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t need_xattr)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_LOOKUP].enabled) {  
		gf_log (this->name, GF_LOG_NORMAL, 
			"callid: %lld (loc=%p {path=%s, inode=%p} need_xattr=%d)",
			(long long) frame->root->unique, loc, loc->path,
			loc->inode, need_xattr);
	}
  
	STACK_WIND (frame, trace_lookup_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->lookup, 
		    loc, need_xattr);

	return 0;
}

int32_t 
trace_stat (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
	ERR_EINVAL_NORETURN (!this || !loc );

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

	if (trace_fop_names[GF_FOP_STAT].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"callid: %lld (*this=%p, loc=%p {path=%s, inode=%p})\n",
			(long long) frame->root->unique, this, loc, loc->path, loc->inode);
	}

	STACK_WIND (frame, 
		    trace_stat_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->stat, 
		    loc);
  
	return 0;
}

int32_t 
trace_readlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		size_t size)
{
	ERR_EINVAL_NORETURN (!this || !loc || (size < 1));
	if (trace_fop_names[GF_FOP_READLINK].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, size=%d)",
			this, loc, loc->path, loc->inode, size);
	}

	STACK_WIND (frame, 
		    trace_readlink_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->readlink, 
		    loc, 
		    size);
  
	return 0;
}

int32_t 
trace_mknod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode,
	     dev_t dev)
{
	ERR_EINVAL_NORETURN (!this || !loc->path);

	if (trace_fop_names[GF_FOP_MKNOD].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, mode=%d, dev=%lld)",
			this, loc, loc->path, loc->inode, mode, dev);
	}

	STACK_WIND (frame, 
		    trace_mknod_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->mknod, 
		    loc,
		    mode, 
		    dev);
  
	return 0;
}

int32_t 
trace_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
	ERR_EINVAL_NORETURN (!this || !loc->path);

	if (trace_fop_names[GF_FOP_MKDIR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, path=%s, loc=%p {path=%s, inode=%p}, mode=%d)",
			this, loc->path, loc, loc->inode, mode);
	}
  
	STACK_WIND (frame, 
		    trace_mkdir_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->mkdir, 
		    loc,
		    mode);
	return 0;
}

int32_t 
trace_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_UNLINK].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p{path=%s, inode=%p})",
			this, loc, loc->path, loc->inode);
	}

	STACK_WIND (frame, 
		    trace_unlink_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->unlink, 
		    loc);
	return 0;
}

int32_t 
trace_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_RMDIR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p})",
			this, loc, loc->path, loc->inode);
	}

	STACK_WIND (frame, 
		    trace_rmdir_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->rmdir, 
		    loc);
  
	return 0;
}

int32_t 
trace_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       loc_t *loc)
{
	ERR_EINVAL_NORETURN (!this || !linkpath || !loc->path);

	if (trace_fop_names[GF_FOP_SYMLINK].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, linkpath=%s, loc=%p {path=%s, inode=%p})",
			this, linkpath, loc, loc->path, loc->inode);
	}

	STACK_WIND (frame, 
		    trace_symlink_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->symlink, 
		    linkpath,
		    loc);
  
	return 0;
}

int32_t 
trace_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{  
	ERR_EINVAL_NORETURN (!this || !oldloc || !newloc);

	if (trace_fop_names[GF_FOP_RENAME].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(oldloc=%p{path=%s, inode=%p, ino=%lld}, newloc=%p{path=%s, inode=%p, ino=%lld})",
			oldloc, oldloc->path, oldloc->inode, oldloc->ino, newloc, newloc->path, newloc->inode, newloc->ino);
	}

	STACK_WIND (frame, 
		    trace_rename_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->rename, 
		    oldloc,
		    newloc);
  
	return 0;
}

int32_t 
trace_link (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
	ERR_EINVAL_NORETURN (!this || !oldloc || !newloc);

	if (trace_fop_names[GF_FOP_LINK].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, oldloc=%p {path=%s, inode=%p}, newloc=%p {path=%s, inode=%p})",
			this, oldloc, oldloc->path, oldloc->inode, 
			newloc, newloc->path, newloc->inode);
	}

	STACK_WIND (frame, 
		    trace_link_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->link, 
		    oldloc,
		    newloc);
	return 0;
}

int32_t 
trace_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_CHMOD].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, mode=%o)",
			this, loc, loc->path, loc->inode, mode);
	}

	STACK_WIND (frame, 
		    trace_chmod_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->chmod, 
		    loc,
		    mode);
  
	return 0;
}

int32_t 
trace_chown (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     uid_t uid,
	     gid_t gid)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_CHOWN].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, uid=%d, gid=%d)",
			this, loc, loc->path, loc->inode, uid, gid);
	}

	STACK_WIND (frame, 
		    trace_chown_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->chown, 
		    loc,
		    uid,
		    gid);

	return 0;
}

int32_t 
trace_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_TRUNCATE].enabled) { 
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, offset=%lld)",
			this, loc, loc->path, loc->inode, offset);
	}

	STACK_WIND (frame, 
		    trace_truncate_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->truncate, 
		    loc,
		    offset);
  
	return 0;
}

int32_t 
trace_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec tv[2])
{
	char actime_str[256];
	char modtime_str[256];
  
	ERR_EINVAL_NORETURN (!this || !loc || !tv);

	if (trace_fop_names[GF_FOP_UTIMENS].enabled) {  
		strftime (actime_str, 256, "[%b %d %H:%M:%S]", localtime (&tv[0].tv_sec));
		strftime (modtime_str, 256, "[%b %d %H:%M:%S]", localtime (&tv[1].tv_sec));

		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, *tv=%p {actime=%s, modtime=%s})",
			this, loc, loc->path, loc->inode, tv, actime_str, modtime_str);
	}

	STACK_WIND (frame, 
		    trace_utimens_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->utimens, 
		    loc,
		    tv);

	return 0;
}

int32_t 
trace_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_OPEN].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, flags=%d, fd=%p)",
			this, loc, loc->path, loc->inode, flags, fd);
	}

	STACK_WIND (frame, 
		    trace_open_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->open, 
		    loc,
		    flags,
		    fd);
	return 0;
}

int32_t 
trace_create (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode,
	      fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this || !loc->path);

	if (trace_fop_names[GF_FOP_CREATE].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, flags=0%o mode=0%o)",
			this, loc, loc->path, loc->inode, flags, mode);
	}

	STACK_WIND (frame, 
		    trace_create_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->create, 
		    loc,
		    flags,
		    mode,
		    fd);
	return 0;
}

int32_t 
trace_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
	ERR_EINVAL_NORETURN (!this || !fd || (size < 1));

	if (trace_fop_names[GF_FOP_READ].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p, size=%d, offset=%lld)",
			this, fd, size, offset);
	}

	STACK_WIND (frame, 
		    trace_readv_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
}

int32_t 
trace_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t offset)
{
	ERR_EINVAL_NORETURN (!this || !fd || !vector || (count < 1));

	if (trace_fop_names[GF_FOP_WRITE].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p, *vector=%p, count=%d, offset=%lld)",
			this, fd, vector, count, offset);
	}

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

int32_t 
trace_statfs (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_STATFS].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p})",
			this, loc, loc->path, loc->inode);
	}

	STACK_WIND (frame, 
		    trace_statfs_cbk, 
		    FIRST_CHILD(this), FIRST_CHILD(this)->fops->statfs, 
		    loc);
	return 0; 
}

int32_t 
trace_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FLUSH].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p)",
			this, fd);
	}

	STACK_WIND (frame, 
		    trace_flush_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->flush, 
		    fd);
	return 0;
}


int32_t 
trace_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FSYNC].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, flags=%d, *fd=%p)",
			this, flags, fd);
	}

	STACK_WIND (frame, 
		    trace_fsync_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->fsync, 
		    fd,
		    flags);
	return 0;
}

int32_t 
trace_setxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *dict,
		int32_t flags)
{
	ERR_EINVAL_NORETURN (!this || !loc || !dict);

	if (trace_fop_names[GF_FOP_SETXATTR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, dict=%p, flags=%d)",
			this, loc, loc->path, loc->inode, dict, flags);
	}

	STACK_WIND (frame, 
		    trace_setxattr_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->setxattr, 
		    loc,
		    dict,
		    flags);
	return 0;
}

int32_t 
trace_getxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_GETXATTR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}), name=%s",
			this, loc, loc->path, loc->inode, name);
	}

	STACK_WIND (frame, 
		    trace_getxattr_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->getxattr,
		    loc,
		    name);
	return 0;
}

int32_t 
trace_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   const char *name)
{
	ERR_EINVAL_NORETURN (!this || !loc || !name);

	if (trace_fop_names[GF_FOP_REMOVEXATTR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, loc=%p {path=%s, inode=%p}, name=%s)",
			this, loc, loc->path, loc->inode, name);
	}

	STACK_WIND (frame, 
		    trace_removexattr_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->removexattr, 
		    loc,
		    name);

	return 0;
}

int32_t 
trace_opendir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this || !loc );

	if (trace_fop_names[GF_FOP_OPENDIR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"callid: %lld (*this=%p, loc=%p {path=%s, inode=%p}, fd=%p)",
			(long long) frame->root->unique, this, loc, loc->path, loc->inode, fd);
	}

	STACK_WIND (frame, 
		    trace_opendir_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->opendir, 
		    loc,
		    fd);
	return 0;
}

int32_t 
trace_getdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		size_t size,
		off_t offset,
		int32_t flag)
{
	ERR_EINVAL_NORETURN (!this || !fd);  

	if (trace_fop_names[GF_FOP_GETDENTS].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"callid: %lld (*this=%p, fd=%p, size=%d, offset=%lld, flag=0x%x)",
			(long long) frame->root->unique, this, fd, size, offset, flag);
	}

	STACK_WIND (frame, 
		    trace_getdents_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->getdents, 
		    fd,
		    size, 
		    offset, 
		    flag);
	return 0;
}


int32_t 
trace_readdir (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset)
{
	ERR_EINVAL_NORETURN (!this || !fd);  

	if (trace_fop_names[GF_FOP_READDIR].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"callid: %lld (fd=%p, size=%d, offset=%lld)",
			(long long) frame->root->unique, fd, size, offset);
	}

	STACK_WIND (frame, 
		    trace_readdir_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->readdir,
		    fd,
		    size, 
		    offset);

	return 0;
}


int32_t 
trace_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t datasync)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FSYNCDIR].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, datasync=%d, *fd=%p)",
			this, datasync, fd);
	}

	STACK_WIND (frame, 
		    trace_fsyncdir_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->fsyncdir, 
		    fd,
		    datasync);
	return 0;
}

int32_t 
trace_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
	ERR_EINVAL_NORETURN (!this || !loc);

	if (trace_fop_names[GF_FOP_ACCESS].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *loc=%p {path=%s, inode=%p}, mask=%d)",
			this, loc, loc->path, loc->inode, mask);
	}

	STACK_WIND (frame, 
		    trace_access_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->access, 
		    loc,
		    mask);
	return 0;
}

int32_t 
trace_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FTRUNCATE].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, offset=%lld, *fd=%p)",
			this, offset, fd);
	}

	STACK_WIND (frame, 
		    trace_ftruncate_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->ftruncate, 
		    fd,
		    offset);

	return 0;
}

int32_t 
trace_fchown (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      uid_t uid,
	      gid_t gid)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FCHOWN].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p, uid=%d, gid=%d)",
			this, fd, uid, gid);
	}

	STACK_WIND (frame, 
		    trace_fchown_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->fchown, 
		    fd,
		    uid,
		    gid);
	return 0;
}

int32_t 
trace_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FCHMOD].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, mode=%o, *fd=%p)",
			this, mode, fd);
	}

	STACK_WIND (frame, 
		    trace_fchmod_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->fchmod, 
		    fd,
		    mode);
	return 0;
}

int32_t 
trace_fstat (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_FSTAT].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p)",
			this, fd);
	}

	STACK_WIND (frame, 
		    trace_fstat_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->fstat, 
		    fd);
	return 0;
}

int32_t 
trace_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
	ERR_EINVAL_NORETURN (!this || !fd);

	if (trace_fop_names[GF_FOP_LK].enabled) {  
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p, cmd=%d, lock=%p {l_type=%d, l_whence=%d, l_start=%lld, l_len=%lld, l_pid=%ld})",
			this, fd, cmd, lock,
			lock->l_type, lock->l_whence, lock->l_start, lock->l_len, lock->l_pid);
	}

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
trace_setdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags,
		dir_entry_t *entries,
		int32_t count)
{
	if (trace_fop_names[GF_FOP_SETDENTS].enabled) {
		gf_log (this->name, 
			GF_LOG_NORMAL, 
			"(*this=%p, *fd=%p, flags=%d, entries=%p count=%d",
			this, fd, flags, entries, count);
	}

	STACK_WIND (frame, 
		    trace_setdents_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->setdents, 
		    fd,
		    flags,
		    entries,
		    count);
	return 0;
}


int32_t
trace_checksum_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    uint8_t *fchecksum,
		    uint8_t *dchecksum)
{
	gf_log (this->name, GF_LOG_NORMAL, 
		"op_ret (%d), op_errno(%d)", op_ret, op_errno);

	STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);

	return 0;
}

int32_t
trace_checksum (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flag)
{
	gf_log (this->name, GF_LOG_NORMAL, 
		"loc->path (%s) flag (%d)", loc->path, flag);
  
	STACK_WIND (frame,
		    trace_checksum_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->checksum, 
		    loc,
		    flag);

	return 0;
}


#ifndef GF_SOLARIS_HOST_OS
void
enable_all_calls (int enabled)
{
  int i;
  for (i = 0; i < GF_FOP_MAXVALUE; i++)
    trace_fop_names[i].enabled = enabled;
}

void 
enable_call (const char *name, int enabled)
{
  int i;
  for (i = 0; i < GF_FOP_MAXVALUE; i++)
    if (!strcasecmp(trace_fop_names[i].name, name))
      trace_fop_names[i].enabled = enabled;
}


/* 
   include = 1 for "include"
           = 0 for "exclude" 
*/
void
process_call_list (const char *list, int include)
{
  enable_all_calls (include ? 0 : 1);

  char *call = strsep ((char **)&list, ",");
  while (call) {
    enable_call (call, include);
    call = strsep ((char **)&list, ",");
  }
}
#endif /* GF_SOLARIS_HOST_OS */

int32_t 
init (xlator_t *this)
{
  dict_t *options = this->options;
  char *includes = NULL, *excludes = NULL;
  
  if (!this)
    return -1;

  if (!this->children) {
    gf_log (this->name, 
	    GF_LOG_ERROR, 
	    "trace translator requires one subvolume");
    return -1;
  }
    
  if (this->children->next) {
    gf_log (this->name, 
	    GF_LOG_ERROR, 
	    "trace translator does not support more than one sub-volume");
    return -1;
  }

//  enable_all_calls (1);

#ifndef GF_SOLARIS_HOST_OS
  includes = data_to_str (dict_get (options, "include"));
  excludes = data_to_str (dict_get (options, "exclude"));

  {
	  int i;
	  for (i = 0; i < GF_FOP_MAXVALUE; i++) {
		  trace_fop_names[i].name = (gf_fop_list[i])?gf_fop_list[i]:":O";
		  trace_fop_names[i].enabled = 1;
	  }
  }
  
  if (includes && excludes) {
    gf_log (this->name, 
	    GF_LOG_ERROR,
	    "must specify only one of 'include' and 'exclude'");
    return -1;
  }
  if (includes)
    process_call_list (includes, 1);
  if (excludes)
    process_call_list (excludes, 0);
#endif /* GF_SOLARIS_HOST_OS */

  gf_log_set_loglevel (GF_LOG_NORMAL);
 
  /* Set this translator's inode table pointer to child node's pointer. */
  this->itable = FIRST_CHILD (this)->itable;

  return 0;
}

void
fini (xlator_t *this)
{
  if (!this)
    return;

  gf_log (this->name, GF_LOG_NORMAL, 
	  "trace translator unloaded");
  return;
}

struct xlator_fops fops = {
  .stat        = trace_stat,
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
  .utimens     = trace_utimens,
  .open        = trace_open,
  .readv       = trace_readv,
  .writev      = trace_writev,
  .statfs      = trace_statfs,
  .flush       = trace_flush,
  .fsync       = trace_fsync,
  .setxattr    = trace_setxattr,
  .getxattr    = trace_getxattr,
  .removexattr = trace_removexattr,
  .opendir     = trace_opendir,
  .readdir     = trace_readdir, 
  .fsyncdir    = trace_fsyncdir,
  .access      = trace_access,
  .ftruncate   = trace_ftruncate,
  .fstat       = trace_fstat,
  .create      = trace_create,
  .fchown      = trace_fchown,
  .fchmod      = trace_fchmod,
  .lk          = trace_lk,
  .inodelk     = trace_inodelk,
  .finodelk    = trace_finodelk,
  .entrylk     = trace_entrylk,
  .lookup      = trace_lookup,
  .setdents    = trace_setdents,
  .getdents    = trace_getdents,
  .checksum    = trace_checksum,
  .xattrop     = trace_xattrop,
  .fxattrop    = trace_fxattrop,
};

int32_t 
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

int32_t 
trace_stats (call_frame_t *frame,
	     xlator_t *this, 
	     int32_t flags)
{
	ERR_EINVAL_NORETURN (!this);
  
	{
		gf_log (this->name, GF_LOG_NORMAL, "trace_stats (*this=%p, flags=%d\n", this, flags);

		STACK_WIND (frame, 
			    trace_stats_cbk, 
			    FIRST_CHILD(this), 
			    FIRST_CHILD(this)->mops->stats, 
			    flags);
	}
	return 0;
}

struct xlator_mops mops = {
	.stats    = trace_stats,
};


struct xlator_options options[] = {
	{ "include", GF_OPTION_TYPE_STR, 0, 0, 0 },
	{ "exclude", GF_OPTION_TYPE_STR, 0, 0, 0 },
	{ NULL, 0, 0, 0, 0 },
};

struct xlator_cbks cbks = {
};
