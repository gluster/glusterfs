/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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
#include <locale.h>
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

extern int errno;

#define _FORMAT_WARN(domain, log_level, format, args...)  printf ("__DEBUG__" format, ##args);     

typedef struct trace_private
{
  int debug_flag;
} trace_private_t;

static int
trace_getattr (struct xlator *this,
		const char *path,
		struct stat *stbuf)
{
  int ret;
  
  ERR_EINVAL_NORETURN (!this || !path || !stbuf);
  
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

   gf_log ("trace", GF_LOG_DEBUG, "trace_getattr (*this=%p, path=%s, *stbuf=%p})",
	   this, path, stbuf);

 {
   char atime_buf[256], mtime_buf[256], ctime_buf[256];
   ret = this->first_child->fops->getattr (this->first_child, path, stbuf);

   setlocale (LC_ALL, "");
   strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&stbuf->st_atime));
   strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&stbuf->st_mtime));
   strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&stbuf->st_ctime));

  gf_log ("trace", GF_LOG_DEBUG, "trace_getattr (*this=%p, path=%s, *stbuf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})  => ret=%d, errno=%d",
	   this, path, stbuf, stbuf->st_dev, stbuf->st_ino, stbuf->st_mode, stbuf->st_nlink, stbuf->st_uid, stbuf->st_gid, stbuf->st_rdev, stbuf->st_size, stbuf->st_blksize, stbuf->st_blocks, atime_buf, mtime_buf, ctime_buf, ret, errno);
   return ret;
 }
}

static int
trace_readlink (struct xlator *this,
		 const char *path,
		 char *dest,
		 size_t size)
{
  int ret;
  
  ERR_EINVAL_NORETURN (!this || !path || !dest || (size < 1));

  gf_log ("trace", GF_LOG_DEBUG, "trace_readlink (*this=%p, path=%s, *dest=%p, size=%d)",
	  this, path, dest, size);

  {
    ret = this->first_child->fops->readlink (this->first_child, path, dest, size);

  gf_log ("trace", GF_LOG_DEBUG, "trace_readlink (*this=%p, path=%s, *dest=%p, size=%d) => ret=%d, errno=%d",
	  this, path, dest, size, ret, errno);
 
  return ret;
  }
}

static int
trace_mknod (struct xlator *this,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  int ret;
  
  ERR_EINVAL_NORETURN (!this || !path);

   gf_log ("trace", GF_LOG_DEBUG, "trace_mknod (*this=%p, path=%s, mode=%d, dev=%lld, uid=%d, gid=%d)",
	     this, path, mode, dev, uid, gid);
   {
     ret = this->first_child->fops->mknod (this->first_child, path, mode, dev, uid, gid);
     gf_log ("trace", GF_LOG_DEBUG, "trace_mknod (*this=%p, path=%s, mode=%d, dev=%lld, uid=%d, gid=%d) => ret=%d, errno=%d",
	     this, path, mode, dev, uid, gid, ret, errno);
     
     return ret;
   }
}

static int
trace_mkdir (struct xlator *this,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  int ret;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_mkdir (*this=%p, path=%s, mode=%d, uid=%d, gid=%d)",
	  this, path, mode, uid, gid);
  
  {
    ret = this->first_child->fops->mkdir (this->first_child, path, mode, uid, gid);
    gf_log ("trace", GF_LOG_DEBUG, "trace_mkdir (*this=%p, path=%s, mode=%d, uid=%d, gid=%d) => ret=%d, errno=%d",
	    this, path, mode, uid, gid, ret, errno);
    return ret;
  }
}

static int
trace_unlink (struct xlator *this,
	       const char *path)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_unlink (*this=%p, path=%s)",
	  this, path);
  ret = this->first_child->fops->unlink (this->first_child, path);
  gf_log ("trace", GF_LOG_DEBUG, "trace_unlink (*this=%p, path=%s) => ret=%d, errno=%d",
	  this, path, ret, errno);
  return ret;
}

static int
trace_rmdir (struct xlator *this,
	      const char *path)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_rmdir (*this=%p, path=%s)",
	  this, path);
  ret = this->first_child->fops->rmdir (this->first_child, path);
  gf_log ("trace", GF_LOG_DEBUG, "trace_rmdir (*this=%p, path=%s) => ret=%d, errno=%d",
	  this, path, ret, errno);
  return ret;
}

static int
trace_symlink (struct xlator *this,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !oldpath || *newpath);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_symlink (*this=%p, oldpath=%s, newpath=%s, uid=%d, gid=%d)",
	  this, oldpath, newpath, uid, gid);
  ret = this->first_child->fops->symlink (this->first_child, oldpath, newpath, uid, gid);
  gf_log ("trace", GF_LOG_DEBUG, "trace_symlink (*this=%p, oldpath=%s, newpath=%s, uid=%d, gid=%d) => ret=%d, errno=%d",
	  this, oldpath, newpath, uid, gid, ret, errno);
  return ret;
}

static int
trace_rename (struct xlator *this,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !oldpath || *newpath);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_rename (*this=%p, oldpath=%s, newpath=%s, uid=%d, gid=%d)",
	  this, oldpath, newpath, uid, gid);
  ret = this->first_child->fops->rename (this->first_child, oldpath, newpath, uid, gid);
  gf_log ("trace", GF_LOG_DEBUG, "trace_rename (*this=%p, oldpath=%s, newpath=%s, uid=%d, gid=%d) => ret=%d, errno=%d",
	  this, oldpath, newpath, uid, gid, ret, errno);
  return ret;
}

static int
trace_link (struct xlator *this,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !oldpath || *newpath);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_link (*this=%p, oldpath=%s, newpath=%s, uid=%d, gid=%d)",
	  this, oldpath, newpath, uid, gid);
  ret = this->first_child->fops->link (this->first_child, oldpath, newpath, uid, gid);
  gf_log ("trace", GF_LOG_DEBUG, "trace_link (*this=%p, oldpath=%s, newpath=%s, uid=%d, gid=%d) => ret=%d, errno=%d",
	  this, oldpath, newpath, uid, gid, ret, errno);
  return ret;
}

static int
trace_chmod (struct xlator *this,
	      const char *path,
	      mode_t mode)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_chmod (*this=%p, path=%s, mode=%o)",
	  this, path, mode);
  ret = this->first_child->fops->chmod (this->first_child, path, mode);
  gf_log ("trace", GF_LOG_DEBUG, "trace_chmod (*this=%p, path=%s, mode=%o) => ret=%d, errno=%d",
	  this, path, mode, ret, errno);
  return ret;
}

static int
trace_chown (struct xlator *this,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_chown (*this=%p, path=%s, uid=%d, gid=%d)",
	  this, path, uid, gid);
  ret = this->first_child->fops->chown (this->first_child, path, uid, gid);
  gf_log ("trace", GF_LOG_DEBUG, "trace_chown (*this=%p, path=%s, uid=%d, gid=%d) => ret=%d, errno=%d",
	  this, path, uid, gid, ret, errno);
  return ret;
}

static int
trace_truncate (struct xlator *this,
		 const char *path,
		 off_t offset)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_truncate (*this=%p, path=%s, offset=%ld)",
	  this, path, offset);
  ret = this->first_child->fops->truncate (this->first_child, path, offset);
  gf_log ("trace", GF_LOG_DEBUG, "trace_truncate (*this=%p, path=%s, offset=%ld) => ret=%d, errno=%d",
	  this, path, offset, ret, errno);
  return ret;
}

static int
trace_utime (struct xlator *this,
	     const char *path,
	     struct utimbuf *buf)
{
  int ret = 0;
  char actime_str[256];
  char modtime_str[256];
  
  ERR_EINVAL_NORETURN (!this || !path || !buf);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_utime (*this=%p, path=%s, *buf=%p)",
	  this, path, buf);
  ret = this->first_child->fops->utime (this->first_child, path, buf);
  setlocale (LC_ALL, "");
  strftime (actime_str, sizeof(actime_str), nl_langinfo (D_T_FMT), localtime (&buf->actime));
  strftime (modtime_str, sizeof(modtime_str), nl_langinfo (D_T_FMT), localtime (&buf->modtime));
  gf_log ("trace", GF_LOG_DEBUG, "trace_utime (*this=%p, path=%s, *buf=%p {actime=%s, modtime=%d}) => ret=%d, errno=%d",
	  this, path, buf, actime_str, modtime_str, ret, errno);
  return ret;
}

static int
trace_open (struct xlator *this,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);

  gf_log ("trace", GF_LOG_DEBUG, "trace_open (*this=%p, path=%s, flags=%d, mode=%o, *ctx=%p)",
	  this, path, flags, mode, ctx);
  ret = this->first_child->fops->open (this->first_child, path, flags, mode, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_open (*this=%p, path=%s, flags=%d, mode=%o, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, flags, mode, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_read (struct xlator *this,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !buf || (size < 1) || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_read (*this=%p, path=%s, *buf=%p, size=%d, offset=%ld, *ctx=%p)",
	  this, path, buf, size, offset, ctx);
  ret = this->first_child->fops->read (this->first_child, path, buf, size, offset, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_read (*this=%p, path=%s, *buf=%p, size=%d, offset=%ld, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, buf, size, offset, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_write (struct xlator *this,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !buf || (size < 1) || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_write (*this=%p, path=%s, *buf=%p, size=%d, offset=%ld, *ctx=%p)",
	  this, path, buf, size, offset, ctx);
  ret = this->first_child->fops->write (this->first_child, path, buf, size, offset, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_write (*this=%p, path=%s, *buf=%p, size=%d, offset=%ld, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, buf, size, offset, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_statfs (struct xlator *this,
	       const char *path,
	       struct statvfs *buf)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !buf);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_statfs (*this=%p, path=%s, *buf=%p)",
	  this, path, buf);
  ret = this->first_child->fops->statfs (this->first_child, path, buf);
  gf_log ("trace", GF_LOG_DEBUG, "trace_statfs (*this=%p, path=%s, *buf=%p {f_bsize=%u, f_frsize=%u, f_blocks=%lu, f_bfree=%lu, f_bavail=%lu, f_files=%lu, f_ffree=%lu, f_favail=%lu, f_fsid=%u, f_flag=%u, f_namemax=%u}) => ret=%d, errno=%d",
	  this, path, buf, buf->f_bsize, buf->f_frsize, buf->f_blocks, buf->f_bfree, buf->f_bavail, buf->f_files, buf->f_ffree, buf->f_favail, buf->f_fsid, buf->f_flag, buf->f_namemax, ret, errno);
  return ret; 
}

static int
trace_flush (struct xlator *this,
	      const char *path,
	      struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_flush (*this=%p, path=%s, *ctx=%p)",
	  this, path, ctx);
  ret = this->first_child->fops->flush (this->first_child, path, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_flush (*this=%p, path=%s, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_release (struct xlator *this,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_release (*this=%p, path=%s, *ctx=%p)",
	  this, path, ctx);
  ret = this->first_child->fops->release (this->first_child, path, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_release (*this=%p, path=%s, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_fsync (struct xlator *this,
	     const char *path,
	     int datasync,
	     struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsync (*this=%p, path=%s, datasync=%d, *ctx=%p)",
	  this, path, datasync, ctx);
  ret = this->first_child->fops->fsync (this->first_child, path, datasync, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsync (*this=%p, path=%s, datasync=%d, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, datasync, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_setxattr (struct xlator *this,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !name || !value || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_setxattr (*this=%p, path=%s, name=%s, value=%s, size=%ld, flags=%d)",
	  this, path, name, value, size, flags);
  ret = this->first_child->fops->setxattr (this->first_child, path, name, value, size, flags);
  gf_log ("trace", GF_LOG_DEBUG, "trace_setxattr (*this=%p, path=%s, name=%s, value=%s, size=%ld, flags=%d) => ret=%d, errno=%d",
	  this, path, name, value, size, flags, ret, errno);
  return ret;
}

static int
trace_getxattr (struct xlator *this,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !name);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_getxattr (*this=%p, path=%s, name=%s, value=%s, size=%ld)",
	  this, path, name, value, size);
  ret = this->first_child->fops->getxattr (this->first_child, path, name, value, size);
  gf_log ("trace", GF_LOG_DEBUG, "trace_getxattr (*this=%p, path=%s, name=%s, value=%s, size=%ld) => ret=%d, errno=%d",
	  this, path, name, value, size, ret, errno);
  return ret;
}

static int
trace_listxattr (struct xlator *this,
		 const char *path,
		 char *list,
		 size_t size)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !list || (size < 1));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_listxattr (*this=%p, path=%s, list=%s, size=%ld)",
	  this, path, list, size);
  ret = this->first_child->fops->listxattr (this->first_child, path, list, size);
  gf_log ("trace", GF_LOG_DEBUG, "trace_listxattr (*this=%p, path=%s, list=%s, size=%ld) => ret=%d, errno=%d",
	  this, path, list, size, ret, errno);
  return ret;
}

static int
trace_removexattr (struct xlator *this,
		    const char *path,
		    const char *name)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !name);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_removexattr (*this=%p, path=%s, name=%s)",
	  this, path, name);
  ret = this->first_child->fops->removexattr (this->first_child, path, name);
  gf_log ("trace", GF_LOG_DEBUG, "trace_removexattr (*this=%p, path=%s, name=%s) => ret=%d, errno=%d",
	  this, path, name, ret, errno);
  return ret;
}

static int
trace_opendir (struct xlator *this,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  
  //  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_opendir (*this=%p, path=%s, *ctx=%p)",
	  this, path, ctx);
  ret = this->first_child->fops->opendir (this->first_child, path, ctx);

  if (ctx) {
    gf_log ("trace", GF_LOG_DEBUG, "trace_opendir (*this=%p, path=%s, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	    this, path, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  }
  else {
    gf_log ("trace", GF_LOG_DEBUG, "trace_opendir (*this=%p, path=%s, *ctx=(null)) => ret=%d, errno=%d",
	    this, path, ret, errno);
  }
  return ret;
}

static char *
trace_readdir (struct xlator *this,
	       const char *path,
	       off_t offset)
{
  char *ret = NULL;
  
  if (!this || !path)
    {
      gf_log ("ERROR", 
	      GF_LOG_ERROR, 
	      "%s: %s: (%s) is true", 
	      __FILE__, __FUNCTION__, "(!this || !path)");
    }
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_readdir (*this=%p, path=%s, offset=%ld)",
	  this, path, offset);
  ret = this->first_child->fops->readdir (this->first_child, path, offset);
  gf_log ("trace", GF_LOG_DEBUG, "trace_readdir (*this=%p, path=%s, offset=%llud) => ret=[%s], errno=%d",
	  this, path, offset, ret, errno);
  return ret;
}

static int
trace_releasedir (struct xlator *this,
		   const char *path,
		   struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_releasedir (*this=%p, path=%s, *ctx=%p)",
	  this, path, ctx);
  ret = this->first_child->fops->releasedir (this->first_child, path, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_releasedir (*this=%p, path=%s, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_fsyncdir (struct xlator *this,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsyncdir (*this=%p, path=%s, datasync=%d, *ctx=%p)",
	  this, path, datasync, ctx);
  ret = this->first_child->fops->fsyncdir (this->first_child, path, datasync, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_fsyncdir (*this=%p, path=%s, datasync=%d, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, datasync, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_access (struct xlator *this,
	       const char *path,
	       mode_t mode)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_access (*this=%p, path=%s, mode=%o)",
	  this, path, mode);
  ret = this->first_child->fops->access (this->first_child, path, mode);
  gf_log ("trace", GF_LOG_DEBUG, "trace_access (*this=%p, path=%s, mode=%o) => ret=%d, errno=%d",
	  this, path, mode, ret, errno);
  return ret;
}

static int
trace_ftruncate (struct xlator *this,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  int ret = 0;
  
  ERR_EINVAL_NORETURN (!this || !path || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_ftruncate (*this=%p, path=%s, offset=%ld, *ctx=%p)",
	  this, path, offset, ctx);
  ret = this->first_child->fops->ftruncate (this->first_child, path, offset, ctx);
  gf_log ("trace", GF_LOG_DEBUG, "trace_ftruncate (*this=%p, path=%s, offset=%ld, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, offset, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
  return ret;
}

static int
trace_fgetattr (struct xlator *this,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  int ret = 0;
  char atime_buf[256], mtime_buf[256], ctime_buf[256];
  
  ERR_EINVAL_NORETURN (!this || !path || !buf || !ctx);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fgetattr (*this=%p, path=%s, *buf=%p, *ctx=%p)",
	  this, path, buf, ctx);
  ret = this->first_child->fops->fgetattr (this->first_child, path, buf, ctx);
  setlocale (LC_ALL, "");
  strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_atime));
  strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_mtime));
  strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&buf->st_ctime));
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_fgetattr (*this=%p, path=%s, *buf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s}, *ctx=%p {*next=%p, *volume=%p, path=%s, *context=%p}) => ret=%d, errno=%d",
	  this, path, buf, buf->st_dev, buf->st_ino, buf->st_mode, buf->st_nlink, buf->st_uid, buf->st_gid, buf->st_rdev, buf->st_size, buf->st_blksize, buf->st_blocks, atime_buf, mtime_buf, ctime_buf, ctx, ctx->next, ctx->volume, ctx->path, ctx->context, ret, errno);
   return ret;
}

static int
trace_bulk_getattr (struct xlator *this,
		      const char *path,
		      struct bulk_stat *bstbuf)
{
  int ret;
  
  ERR_EINVAL_NORETURN (!this || !path || !bstbuf);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_bulk_getattr (*this=%p, path=%s, *bstbuf=%p)",
	  this, path, bstbuf);
  ret = this->first_child->fops->bulk_getattr (this->first_child, path, bstbuf);
  gf_log ("trace", GF_LOG_DEBUG, "trace_bulk_getattr (*this=%p, path=%s, *bstbuf=%p {*stbuf=%p, pathname=%s, *next=%p}) => ret=%d, errno=%d",
	  this, path, bstbuf, bstbuf->stbuf, bstbuf->pathname, bstbuf->next, ret, errno);
  return ret;
}

int
init (struct xlator *this)
{
  trace_private_t private;

  if (!this)
    return -1;


  if (!this->first_child)
    {
      gf_log ("trace", GF_LOG_ERROR, "trace translator requires one subvolume");
      exit (-1);
    }
    
  if (this->first_child->next_sibling)
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
  
  void gf_log_xlator (struct xlator *this)
  {
    int len;
    char *buf;
    
    if (!this)
      return;
    
    len = dict_serialized_length (this->options);
    buf = alloca (len);
    dict_serialize (this->options, buf);
    
    gf_log ("trace", GF_LOG_DEBUG, 
	    "init (struct xlator *this=%p {name=%s, *next=%p, *parent=%p, *first_child=%p, *next_sibling=%p, *fops=%p {*open=%p, getattr=%p, *readlink=%p, *mknod=%p, *mkdir=%p, *unlink=%p, *rmdir=%p, *symlink=%p, *rename=%p, *link=%p, *chmod=%p, *chown=%p, *truncate=%p, *utime=%p, *read=%p, *write=%p, *statfs=%p, *flush=%p, *release=%p, *fsync=%p, *setxattr=%p, *getxattr=%p, *listxattr=%p, *removexattr=%p, *opendir=%p, *readdir=%p, *releasedir=%p, *fsyncdir=%p, *access=%p, *ftruncate=%p, *fgetattr=%p, *bulk_getattr=%p}, *mgmt_ops=%p {*stats=%p, *fsck=%p, *lock=%p, *unlock=%p}, *fini()=%p, *init()=%p, *getlayout()=%p, *options=%p {%s}, *private=%p)", 
	    this, this->name, this->next, this->parent, this->first_child, this->next_sibling, this->fops, this->fops->open, this->fops->getattr, this->fops->readlink, this->fops->mknod, this->fops->mkdir, this->fops->unlink, this->fops->rmdir, this->fops->symlink, this->fops->rename, this->fops->link, this->fops->chmod, this->fops->chown, this->fops->truncate, this->fops->utime, this->fops->read, this->fops->write, this->fops->statfs, this->fops->flush, this->fops->release, this->fops->fsync, this->fops->setxattr, this->fops->getxattr, this->fops->listxattr, this->fops->removexattr, this->fops->opendir, this->fops->readdir, this->fops->releasedir, this->fops->fsyncdir, this->fops->access, this->fops->ftruncate, this->fops->fgetattr, this->fops->bulk_getattr, this->mgmt_ops, this->mgmt_ops->stats,  this->mgmt_ops->fsck, this->mgmt_ops->lock, this->mgmt_ops->unlock, this->fini, this->init, this->getlayout, this->options, buf, this->private);
  }
  
  //xlator_foreach (this, gf_log_xlator);

  this->private = &private;
  return 0;
}

void
fini (struct xlator *this)
{
  if (!this)
    return;

  gf_log ("trace", GF_LOG_DEBUG, "fini (struct xlator *this=%p)", this);

  /* Free up the dictionary options */
  dict_destroy (this->first_child->options);

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
  .utime       = trace_utime,
  .open        = trace_open,
  .read        = trace_read,
  .write       = trace_write,
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
  .bulk_getattr = trace_bulk_getattr
};

static int
trace_stats (struct xlator *this, struct xlator_stats *stats)
{
  ERR_EINVAL_NORETURN (!this || !stats);
  
  gf_log ("trace", GF_LOG_DEBUG, "trace_stats (struct xlator *this=%p)", this, stats);

  {
    int ret;
    ret = (this->first_child->mgmt_ops->stats (this->first_child, stats));
    gf_log ("trace", GF_LOG_DEBUG, "trace_stats (*this=%p, *stats=%p {nr_files=%ld, free_disk=%lld, disk_usage=%lld, disk_speed=%lu, nr_clients=%ld, write_usage=%llu, read_usage=%llu}) => ret=%d, errno=%d", this, stats, stats->nr_files, stats->free_disk, stats->disk_usage, stats->disk_speed, stats->nr_clients, stats->write_usage, stats->read_usage, ret, errno);
    return ret;
  }
}

struct xlator_mgmt_ops mgmt_ops = {
  .stats = trace_stats
};
