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

#include "glusterfs.h"
#include "transport-socket.h"
#include "dict.h"
#include "protocol.h"
#include "xlator.h"
#include "logging.h"
#include "layout.h"
#include <signal.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

int32_t 
client_getattr (struct xlator *xl,
		const int8_t *path,
		struct stat *stbuf)
{
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  int32_t ret;
  int32_t remote_errno;
  int8_t *buf = NULL;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  ret = fops_xfer (priv, OP_GETATTR, request, reply);

  if (ret != 0) 
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));

  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  buf = data_to_bin (dict_get (reply, "BUF"));

  uint64_t dev;
  uint64_t ino;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t rdev;
  uint64_t size;
  uint32_t blksize;
  uint64_t blocks;
  uint32_t atime;
  uint32_t atime_nsec;
  uint32_t mtime;
  uint32_t mtime_nsec;
  uint32_t ctime;
  uint32_t ctime_nsec;

  sscanf (buf, GF_STAT_PRINT_FMT_STR,
	  &dev,
	  &ino,
	  &mode,
	  &nlink,
	  &uid,
	  &gid,
	  &rdev,
	  &size,
	  &blksize,
	  &blocks,
	  &atime,
	  &atime_nsec,
	  &mtime,
	  &mtime_nsec,
	  &ctime,
	  &ctime_nsec);

  stbuf->st_dev = dev;
  stbuf->st_ino = ino;
  stbuf->st_mode = mode;
  stbuf->st_nlink = nlink;
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  stbuf->st_rdev = rdev;
  stbuf->st_size = size;
  stbuf->st_blksize = blksize;
  stbuf->st_blocks = blocks;
  stbuf->st_atime = atime;
  stbuf->st_atim.tv_nsec = atime_nsec;
  stbuf->st_mtime = mtime;
  stbuf->st_mtim.tv_nsec = mtime_nsec;
  stbuf->st_ctime = ctime;
  stbuf->st_ctim.tv_nsec = ctime_nsec;

 ret:
  dict_destroy (request);
  dict_destroy (reply);
  return ret;
}

int32_t 
client_getattr_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
}

int32_t 
brick_readlink (struct xlator *xl,
		const int8_t *path,
		int8_t *dest,
		size_t size)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    //    data_t *prefilled = bin_to_data (dest, size);
    //    dict_set (reply, "PATH", prefilled);

    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "LEN", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_READLINK, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0){
    errno = remote_errno;
    goto ret;
  }
  memcpy (dest, data_to_bin (dict_get (reply, "PATH")), ret);
  
  if (ret < 0) {
    errno = remote_errno;
  }

 ret:
  dict_destroy (reply);

  return ret;
}

int32_t 
client_readlink_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     int8_t *buf)
{
}

int32_t 
brick_mknod (struct xlator *xl,
	     const int8_t *path,
	     mode_t mode,
	     dev_t dev,
	     uid_t uid,
	     gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "MODE", int_to_data (mode));
    dict_set (request, "DEV", int_to_data (dev));
    dict_set (request, "UID", int_to_data (uid));
    dict_set (request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_MKNOD, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t
client_mknod_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
brick_mkdir (struct xlator *xl,
	     const int8_t *path,
	     mode_t mode,
	     uid_t uid,
	     gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "MODE", int_to_data (mode));
    dict_set (request, "UID", int_to_data (uid));
    dict_set (request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_MKDIR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_mkdir_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
brick_unlink (struct xlator *xl,
	      const int8_t *path)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
  }

  ret = fops_xfer (priv, OP_UNLINK, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_unlink_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
}

int32_t 
brick_rmdir (struct xlator *xl,
	     const int8_t *path)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
  }

  ret = fops_xfer (priv, OP_RMDIR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t
client_rmdir_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
}


int32_t 
brick_symlink (struct xlator *xl,
	       const int8_t *oldpath,
	       const int8_t *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)oldpath));
    dict_set (request, "BUF", str_to_data ((int8_t *)newpath));
    dict_set (request, "UID", int_to_data (uid));
    dict_set (request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_SYMLINK, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_symlink_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
}

int32_t 
brick_rename (struct xlator *xl,
	      const int8_t *oldpath,
	      const int8_t *newpath,
	      uid_t uid,
	      gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)oldpath));
    dict_set (request, "BUF", str_to_data ((int8_t *)newpath));
    dict_set (request, "UID", int_to_data (uid));
    dict_set (request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_RENAME, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_rename_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
}

int32_t 
brick_link (struct xlator *xl,
	    const int8_t *oldpath,
	    const int8_t *newpath,
	    uid_t uid,
	    gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)oldpath));
    dict_set (request, "BUF", str_to_data ((int8_t *)newpath));
    dict_set (request, "UID", int_to_data (uid));
    dict_set (request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_LINK, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_link_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
}

int32_t 
brick_chmod (struct xlator *xl,
	     const int8_t *path,
	     mode_t mode)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "MODE", int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_CHMOD, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t
client_chmod_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
brick_chown (struct xlator *xl,
	     const int8_t *path,
	     uid_t uid,
	     gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "UID", int_to_data (uid));
    dict_set (request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_CHOWN, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t
client_chown_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
brick_truncate (struct xlator *xl,
		const int8_t *path,
		off_t offset)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "OFFSET", int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_TRUNCATE, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t client_truncate_rsp (call_frame_t *frame,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     struct stat *buf)
{
}

int32_t 
brick_utime (struct xlator *xl,
	     const int8_t *path,
	     struct utimbuf *buf)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "ACTIME", int_to_data (buf->actime));
    dict_set (request, "MODTIME", int_to_data (buf->modtime));
  }

  ret = fops_xfer (priv, OP_UTIME, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t 
client_utime_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
client_open (call_frame_t *frame,
	     struct xlator *xl,
	     const int8_t *path,
	     int32_t flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FLAGS", int_to_data (flags));
    dict_set (request, "MODE", int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_OPEN, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  ret = 0;
  {
    void **tmp;
    struct file_context *trav = ctx;
    struct file_context *brick_ctx = calloc (1, sizeof (struct file_context));
    brick_ctx->volume = xl;
    brick_ctx->next = NULL;
    tmp = &(brick_ctx->context);
    *(long *)tmp = data_to_int (dict_get (reply, "FD"));
    
    while (trav->next)
      trav = trav->next;
    
    trav->next = brick_ctx;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_open_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 file_ctx_t *ctx,
		 struct stat *buf)
{
}

int32_t 
brick_read (struct xlator *xl,
	    const int8_t *path,
	    int8_t *buf,
	    size_t size,
	    off_t offset,
	    struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  long fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  }
  fd = (long)tmp->context;

  {
    //    data_t *prefilled = bin_to_data (buf, size);
    //    dict_set (reply, "BUF", prefilled);
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data (fd));
    dict_set (request, "OFFSET", int_to_data (offset));
    dict_set (request, "LEN", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_READ, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  memcpy (buf, data_to_bin (dict_get (reply, "BUF")), ret);

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t
client_read_rsp (call_frame_t *frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 int8_t *buf)
{
}

int32_t 
brick_write (struct xlator *xl,
	     const int8_t *path,
	     const int8_t *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  long fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 
  fd = (long)tmp->context;

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "OFFSET", int_to_data (offset));
    dict_set (request, "FD", int_to_data (fd));
    dict_set (request, "BUF", bin_to_data ((void *)buf, size));
  }

  ret = fops_xfer (priv, OP_WRITE, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t
client_write_rsp (call_frame_t *frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
}

int32_t 
brick_statfs (struct xlator *xl,
	      const int8_t *path,
	      struct statvfs *stbuf)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
  }

  ret = fops_xfer (priv, OP_STATFS, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    int8_t *buf = data_to_bin (dict_get (reply, "BUF"));

    uint32_t bsize;
    uint32_t frsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint64_t favail;
    uint32_t fsid;
    uint32_t flag;
    uint32_t namemax;

    sscanf (buf, GF_STATFS_SCAN_FMT_STR,
	    &bsize,
	    &frsize,
	    &blocks,
	    &bfree,
	    &bavail,
	    &files,
	    &ffree,
	    &favail,
	    &fsid,
	    &flag,
	    &namemax);

    stbuf->f_bsize = bsize;
    stbuf->f_frsize = frsize;
    stbuf->f_blocks = blocks;
    stbuf->f_bfree = bfree;
    stbuf->f_bavail = bavail;
    stbuf->f_files = files;
    stbuf->f_ffree = ffree;
    stbuf->f_favail = favail;
    stbuf->f_fsid = fsid;
    stbuf->f_flag = flag;
    stbuf->f_namemax = namemax;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t
client_statfs_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *buf)
{
}

int32_t 
brick_flush (struct xlator *xl,
	     const int8_t *path,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  long fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  }
  fd = (long)tmp->context;

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_FLUSH, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
}

int32_t 
brick_release (struct xlator *xl,
	       const int8_t *path,
	       struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  long fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);  
  if (tmp == NULL) {
    return -1;
  } 
  fd = (long)tmp->context;

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_RELEASE, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    /* Free the file_context struct for brick node */
    RM_MY_CTX (ctx, tmp);
    free (tmp);
  }

  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t 
client_release (call_frame_t *frame,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
}

int32_t 
brick_fsync (struct xlator *xl,
	     const int8_t *path,
	     int32_t datasync,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  long fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);  
  if (tmp == NULL) {
    return -1;
  }
  fd = (long)tmp->context;

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FLAGS", int_to_data (datasync));
    dict_set (request, "FD", int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_FSYNC, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
}

int32_t 
brick_setxattr (struct xlator *xl,
		const int8_t *path,
		const int8_t *name,
		const int8_t *value,
		size_t size,
		int flags)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FLAGS", int_to_data (flags));
    dict_set (request, "COUNT", int_to_data (size));
    dict_set (request, "BUF", str_to_data ((int8_t *)name));
    dict_set (request, "FD", str_to_data ((int8_t *)value));
  }

  ret = fops_xfer (priv, OP_SETXATTR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_setxattr_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
}

int32_t 
brick_getxattr (struct xlator *xl,
		const int8_t *path,
		const int8_t *name,
		int8_t *value,
		size_t size)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "BUF", str_to_data ((int8_t *)name));
    dict_set (request, "COUNT", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_GETXATTR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  
  {
    strcpy (value, data_to_str (dict_get (reply, "BUF")));
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t 
client_getxattr_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
}

int32_t 
brick_listxattr (struct xlator *xl,
		 const int8_t *path,
		 int8_t *list,
		 size_t size)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "COUNT", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_LISTXATTR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    memcpy (list, data_to_str (dict_get (reply, "BUF")), ret);
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_listxattr_rsp (call_frame_t *frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      void *value)
{
}
		     
int32_t 
brick_removexattr (struct xlator *xl,
		   const int8_t *path,
		   const int8_t *name)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "BUF", str_to_data ((int8_t *)name));
  }

  ret = fops_xfer (priv, OP_REMOVEXATTR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t 
client_removexattr_rsp (call_frame_t *frame,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
}

int32_t 
brick_opendir (struct xlator *xl,
	       const int8_t *path,
	       struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  if (!ctx)
    return 0;

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data ((long)tmp->context));
  }

  ret = fops_xfer (priv, OP_OPENDIR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_opendir_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    file_ctx_t *ctx)
{
}

static int8_t *
brick_readdir (struct xlator *xl,
	       const int8_t *path,
	       off_t offset)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  data_t *datat = NULL;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "OFFSET", int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_READDIR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    gf_log ("tcp", GF_LOG_NORMAL, "tcp.c->readdir: readdir failed for %s\n", path);
    goto ret;
  }

  {
    /* Here I get a data in ASCII, with '/' as the IFS, now I need to process them */
    datat = dict_get (reply, "BUF");
    datat->is_static = 1;
  }

 ret:
  dict_destroy (reply);
  if (datat && ret == 0)
    return (int8_t *)datat->data;
  else 
    return NULL;
}

int32_t 
client_readdir_rsp (call_frame_t *frame,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entries,
		    int32_t count)
{
}

int32_t 
brick_releasedir (struct xlator *xl,
		  const int8_t *path,
		  struct file_context *ctx)
{
  int32_t ret = 0;
  /*int remote_errno = 0;
    struct brick_private *priv = xl->private;
    dict_t *request = get_new_dict ();
    dict_t *reply = get_new_dict ();

    if (priv->is_debug) {
    FUNCTION_CALLED;
    }

    {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    }

    ret = fops_xfer (priv, OP_RELEASE, request, reply);
    dict_destroy (request);

    if (ret != 0)
    goto ret;

    ret = data_to_int (dict_get (reply, "RET"));
    remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
    if (ret < 0) {
    errno = remote_errno;
    goto ret;
    }

    ret:
    dict_destroy (reply);*/
  return ret;
}

int32_t 
client_releasedir_rsp (call_frame_t *frame,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
}

int32_t 
brick_fsyncdir (struct xlator *xl,
		const int8_t *path,
		int datasync,
		struct file_context *ctx)
{
  int32_t ret = 0;
  /*  int32_t remote_errno = 0;
      struct brick_private *priv = xl->private;
      dict_t *request = get_new_dict ();
      dict_t *reply = get_new_dict ();

      if (priv->is_debug) {
      FUNCTION_CALLED;
      }

      {
      dict_set (request, "PATH", str_to_data ((int8_t *)path));
      dict_set (request, "FLAGS", int_to_data (datasync));
      }

      ret = fops_xfer (priv, OP_FSYNCDIR, request, reply);
      dict_destroy (request);

      if (ret != 0)
      goto ret;

      ret = data_to_int (dict_get (reply, "RET"));
      remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
      if (ret < 0) {
      errno = remote_errno;
      goto ret;
      }

      ret:
      dict_destroy (reply); */
  return ret;
}


int32_t 
client_fsyncdir_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
}


int32_t 
brick_access (struct xlator *xl,
	      const int8_t *path,
	      mode_t mode)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "MODE", int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_ACCESS, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t 
client_access_rsp (call_frame_t *frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
}

int32_t 
brick_ftruncate (struct xlator *xl,
		 const int8_t *path,
		 off_t offset,
		 struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  long fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 
  fd = (long)tmp->context;

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data (fd));
    dict_set (request, "OFFSET", int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_FTRUNCATE, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}


int32_t 
client_ftruncate_rsp (call_frame_t *frame,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
}

int32_t 
brick_fgetattr (struct xlator *xl,
		const int8_t *path,
		struct stat *stbuf,
		struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "FD", int_to_data ((long)tmp->context));
  }

  ret = fops_xfer (priv, OP_FGETATTR, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    int8_t *buf = data_to_bin (dict_get (reply, "BUF"));

    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;
    uint64_t size;
    uint32_t blksize;
    uint64_t blocks;
    uint32_t atime;
    uint32_t atime_nsec;
    uint32_t mtime;
    uint32_t mtime_nsec;
    uint32_t ctime;
    uint32_t ctime_nsec;

    sscanf (buf, GF_STAT_PRINT_FMT_STR,
	    &dev,
	    &ino,
	    &mode,
	    &nlink,
	    &uid,
	    &gid,
	    &rdev,
	    &size,
	    &blksize,
	    &blocks,
	    &atime,
	    &atime_nsec,
	    &mtime,
	    &mtime_nsec,
	    &ctime,
	    &ctime_nsec);

    stbuf->st_dev = dev;
    stbuf->st_ino = ino;
    stbuf->st_mode = mode;
    stbuf->st_nlink = nlink;
    stbuf->st_uid = uid;
    stbuf->st_gid = gid;
    stbuf->st_rdev = rdev;
    stbuf->st_size = size;
    stbuf->st_blksize = blksize;
    stbuf->st_blocks = blocks;
    stbuf->st_atime = atime;
    stbuf->st_atim.tv_nsec = atime_nsec;
    stbuf->st_mtime = mtime;
    stbuf->st_mtim.tv_nsec = mtime_nsec;
    stbuf->st_ctime = ctime;
    stbuf->st_ctim.tv_nsec = ctime_nsec;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
client_fgetattr_rsp (call_frame_t *frame,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
}

int32_t 
brick_bulk_getattr (struct xlator *xl,
		    const int8_t *path,
		    struct bulk_stat *bstbuf)
{
  struct bulk_stat *curr = NULL;
  struct stat *stbuf = NULL;
  int8_t *buffer_ptr = NULL;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  int32_t ret;
  int32_t remote_errno;
  int8_t *buf = NULL;
  uint32_t nr_entries = 0;
  int8_t pathname[PATH_MAX] = {0,};

  /* play it safe */
  bstbuf->stbuf = NULL;
  bstbuf->next = NULL;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (request, "PATH", str_to_data ((int8_t *)path));

  ret = fops_xfer (priv, OP_BULKGETATTR, request, reply);
  dict_destroy (request);

  if (ret != 0) 
    goto fail;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    gf_log ("tcp", GF_LOG_ERROR, "tcp.c->bulk_getattr: remote bulk_getattr returned \"%d\"\n", remote_errno);
    errno = remote_errno;
    goto fail;
  }
  
  nr_entries = data_to_int (dict_get (reply, "NR_ENTRIES"));
  buf = data_to_bin (dict_get (reply, "BUF"));

  buffer_ptr = buf;
  while (nr_entries) {
    int32_t bread = 0;
    int8_t tmp_buf[512] = {0,};
    curr = calloc (sizeof (struct bulk_stat), 1);
    curr->stbuf = calloc (sizeof (struct stat), 1);
    
    stbuf = curr->stbuf;
    nr_entries--;
    /*    sscanf (buffer_ptr, "%s", pathname);*/
    int8_t *ender = strchr (buffer_ptr, '/');
    int32_t count = ender - buffer_ptr;
    strncpy (pathname, buffer_ptr, count);
    bread = count + 1;
    buffer_ptr += bread;

    ender = strchr (buffer_ptr, '\n');
    count = ender - buffer_ptr;
    if (!ender) {
      gf_log ("transport-tcp", GF_LOG_ERROR, "BUF: %s", buf);
      raise (SIGSEGV);
    }

    strncpy (tmp_buf, buffer_ptr, count);
    bread = count + 1;
    buffer_ptr += bread;

    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;
    uint64_t size;
    uint32_t blksize;
    uint64_t blocks;
    uint32_t atime;
    uint32_t atime_nsec;
    uint32_t mtime;
    uint32_t mtime_nsec;
    uint32_t ctime;
    uint32_t ctime_nsec;

    sscanf (tmp_buf, GF_STAT_PRINT_FMT_STR,
	    &dev,
	    &ino,
	    &mode,
	    &nlink,
	    &uid,
	    &gid,
	    &rdev,
	    &size,
	    &blksize,
	    &blocks,
	    &atime,
	    &atime_nsec,
	    &mtime,
	    &mtime_nsec,
	    &ctime,
	    &ctime_nsec);

    stbuf->st_dev = dev;
    stbuf->st_ino = ino;
    stbuf->st_mode = mode;
    stbuf->st_nlink = nlink;
    stbuf->st_uid = uid;
    stbuf->st_gid = gid;
    stbuf->st_rdev = rdev;
    stbuf->st_size = size;
    stbuf->st_blksize = blksize;
    stbuf->st_blocks = blocks;
    stbuf->st_atime = atime;
    stbuf->st_atim.tv_nsec = atime_nsec;
    stbuf->st_mtime = mtime;
    stbuf->st_mtim.tv_nsec = mtime_nsec;
    stbuf->st_ctime = ctime;
    stbuf->st_ctim.tv_nsec = ctime_nsec;

    curr->pathname = strdup (pathname);
    curr->next = bstbuf->next;
    bstbuf->next = curr;
    memset (pathname, 0, PATH_MAX);
  }

 fail:
  dict_destroy (request);
  dict_destroy (reply);
  return ret;
}

/*
 * MGMT_OPS
 */

int32_t 
brick_stats (struct xlator *xl, struct xlator_stats *stats)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  dict_set (request, "LEN", int_to_data (0)); // without this dummy key the server crashes
  ret = mgmt_xfer (priv, OP_STATS, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    int8_t *buf = data_to_bin (dict_get (reply, "BUF"));
    sscanf (buf, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
	    &stats->nr_files,
	    &stats->disk_usage,
	    &stats->free_disk,
	    &stats->read_usage,
	    &stats->write_usage,
	    &stats->disk_speed,
	    &stats->nr_clients);
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
brick_lock (struct xlator *xl,
	    const int8_t *name)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)name));
  }

  ret = mgmt_xfer (priv, OP_LOCK, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
brick_unlock (struct xlator *xl,
	      const int8_t *name)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (request, "PATH", str_to_data ((int8_t *)name));
  }

  ret = mgmt_xfer (priv, OP_UNLOCK, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
brick_listlocks (struct xlator *xl)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    printf ("tcp listlocks called");
  }
  
  dict_set (request, "OP", int_to_data (0xcafebabe));
  ret = mgmt_xfer (priv, OP_LISTLOCKS, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  int32_t junk = data_to_int (dict_get (reply, "RET_OP"));
 
  printf ("returned junk is %x\n", junk);
 
  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  
  {
    /* now, recieve the locks and pass them to the person who called us */
    ;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
brick_nslookup (struct xlator *xl,
		const int8_t *path,
		dict_t *ns)
{
  return -1;
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  int8_t *ns_str;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
  }

  ret = mgmt_xfer (priv, OP_NSLOOKUP, request, reply);
  dict_destroy (request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  ns_str = data_to_str (dict_get (reply, "NS"));

  if (ns_str && strlen (ns_str) > 0)
    dict_unserialize (ns_str, strlen (ns_str), &ns);
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t 
brick_nsupdate (struct xlator *xl,
		const int8_t *path,
		dict_t *ns)
{
  return -1;
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  int8_t *ns_str = calloc (1, dict_serialized_length (ns));
  dict_serialize (ns, ns_str);
  {
    dict_set (request, "PATH", str_to_data ((int8_t *)path));
    dict_set (request, "NS", str_to_data (ns_str));
  }

  ret = mgmt_xfer (priv, OP_NSUPDATE, request, reply);
  dict_destroy (request);
  free (ns_str);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));

  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (reply);
  return ret;
}

int32_t client_create (call_frame_t *frame,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       file_ctx_t *ctx,
		       struct stat *buf)
{
}






int32_t client_readlink (call_frame_t *frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 int8_t *buf)
{
}

int32_t 
init (struct xlator *xl)
{
  struct brick_private *_private = calloc (1, sizeof (*_private));
  data_t *host_data, *port_data, *debug_data, *addr_family_data, *volume_data;
  int8_t *port_str = "5252";

  host_data = dict_get (xl->options, "host");
  port_data = dict_get (xl->options, "port");
  debug_data = dict_get (xl->options, "debug");
  addr_family_data = dict_get (xl->options, "address-family");
  volume_data = dict_get (xl->options, "remote-subvolume");
  
  if (!host_data) {
    gf_log ("brick", GF_LOG_ERROR, "volume %s does not have 'Host' section",  xl->name);
    return -1;
  }
  _private->addr = resolve_ip (data_to_str (host_data));

  if (!volume_data) {
    gf_log ("brick", GF_LOG_ERROR, "volume %s does not have 'remote-subvolume' section", xl->name);
    return -1;
  }
  _private->volume = data_to_str (volume_data);

  _private->is_debug = 0;
  if (debug_data && (strcasecmp (debug_data->data, "on") == 0))
    _private->is_debug = 1;

  if (port_data)
    port_str = data_to_str (port_data);

  _private->addr_family = PF_INET;
  if (addr_family_data) {
    if (strcasecmp (data_to_str (addr_family_data), "inet") == 0)
      _private->addr_family = PF_INET;
    else {
      gf_log ("brick", GF_LOG_ERROR, "unsupported address family: %s", data_to_str (addr_family_data));
      return -1;
    }
  }

  if (_private->is_debug) {
    FUNCTION_CALLED;
    gf_log ("tcp", GF_LOG_DEBUG, "tcp.c->init: host(:port) = %s:%s\n", 
	    data_to_str (host_data), port_str);
    gf_log ("tcp", GF_LOG_DEBUG, "tcp.c->init: debug mode on\n");
  }

  _private->port = htons (strtol (port_str, NULL, 0));
  _private->sock = -1;

  xl->private = (void *)_private;
  return try_connect (xl);
}

void
fini (struct xlator *xl)
{
  struct brick_private *priv = xl->private;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  if (priv->sock != -1)
    close (priv->sock);
  free (priv);
  return;
}

struct xlator_fops fops = {
  .getattr     = client_getattr,
  .readlink    = client_readlink,
  .mknod       = client_mknod,
  .mkdir       = client_mkdir,
  .unlink      = client_unlink,
  .rmdir       = client_rmdir,
  .symlink     = client_symlink,
  .rename      = client_rename,
  .link        = client_link,
  .chmod       = client_chmod,
  .chown       = client_chown,
  .truncate    = client_truncate,
  .utime       = client_utime,
  .open        = client_open,
  .read        = client_read,
  .write       = client_write,
  .statfs      = client_statfs,
  .flush       = client_flush,
  .release     = client_release,
  .fsync       = client_fsync,
  .setxattr    = client_setxattr,
  .getxattr    = client_getxattr,
  .listxattr   = client_listxattr,
  .removexattr = client_removexattr,
  .opendir     = client_opendir,
  .readdir     = client_readdir,
  .releasedir  = client_releasedir,
  .fsyncdir    = client_fsyncdir,
  .access      = client_access,
  .ftruncate   = client_ftruncate,
  .fgetattr    = client_fgetattr,
  .bulk_getattr = client_bulk_getattr
};

struct xlator_fop_rsps fop_rsps = {
  .getattr     = client_getattr_rsp,
  .readlink    = client_readlink_rsp,
  .mknod       = client_mknod_rsp,
  .mkdir       = client_mkdir_rsp,
  .unlink      = client_unlink_rsp,
  .rmdir       = client_rmdir_rsp,
  .symlink     = client_symlink_rsp,
  .rename      = client_rename_rsp,
  .link        = client_link_rsp,
  .chmod       = client_chmod_rsp,
  .chown       = client_chown_rsp,
  .truncate    = client_truncate_rsp,
  .utime       = client_utime_rsp,
  .open        = client_open_rsp,
  .read        = client_read_rsp,
  .write       = client_write_rsp,
  .statfs      = client_statfs_rsp,
  .flush       = client_flush_rsp,
  .release     = client_release_rsp,
  .fsync       = client_fsync_rsp,
  .setxattr    = client_setxattr_rsp,
  .getxattr    = client_getxattr_rsp,
  .listxattr   = client_listxattr_rsp,
  .removexattr = client_removexattr_rsp,
  .opendir     = client_opendir_rsp,
  .readdir     = client_readdir_rsp,
  .releasedir  = client_releasedir_rsp,
  .fsyncdir    = client_fsyncdir_rsp,
  .access      = client_access_rsp,
  .ftruncate   = client_ftruncate_rsp,
  .fgetattr    = client_fgetattr_rsp,
  .bulk_getattr = client_bulk_getattr_rsp
};

struct xlator_mgmt_ops mgmt_ops = {
  .stats = client_stats,
  .lock = client_lock,
  .unlock = client_unlock,
  .listlocks = client_listlocks,
  .nslookup = client_nslookup,
  .nsupdate = client_nsupdate
};

struct xlator_mgmt_rsps mgmt_op_rsps = {
  .stats = client_stats_rsp,
  .lock = client_lock_rsp,
  .unlock = client_unlock_rsp,
  .listlocks = client_listlocks_rsp,
  .nslookup = client_nslookup_rsp,
  .nsupdate = client_nsupdate_rsp
};
