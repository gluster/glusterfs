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
  License aint64_t with this program; if not, write to the Free
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

int
generic_xfer (struct brick_private *priv,
	      int32_t op,
	      dict_t *request, 
	      dict_t *reply,
	      int32_t type)
{
  int32_t ret = 0;
  struct wait_queue *mine = (void *) calloc (1, sizeof (*mine));

  pthread_mutex_init (&mine->mutex, NULL);
  pthread_mutex_lock (&mine->mutex);

  pthread_mutex_lock (&priv->mutex);
  mine->next = priv->queue;
  priv->queue = mine;

  {
    pthread_mutex_lock (&priv->io_mutex);
    int32_t dict_len = dict_serialized_length (request);
    char *dict_buf = malloc (dict_len);
    dict_serialize (request, dict_buf);

    gf_block *blk = gf_block_new ();
    blk->type = type;
    blk->op = op;
    blk->size = dict_len;
    blk->data = dict_buf;

    int32_t blk_len = gf_block_serialized_length (blk);
    char *blk_buf = malloc (blk_len);
    gf_block_serialize (blk, blk_buf);
    
    int32_t ret = full_write (priv->sock, blk_buf, blk_len);

    free (blk_buf);
    free (dict_buf);
    free (blk);

    if (ret == -1) {
      gf_log ("transport/tcp", GF_LOG_DEBUG, "full_write failed");
      goto write_err;
    }

    pthread_mutex_unlock (&priv->io_mutex);
  }

  pthread_mutex_unlock (&priv->mutex);

  if (mine->next)
    pthread_mutex_lock (&mine->next->mutex);

  {
    pthread_mutex_lock (&priv->io_mutex);
    gf_block *blk = gf_block_unserialize (priv->sock);
    if (blk == NULL) {
      gf_log ("transport/tcp", GF_LOG_DEBUG, "gf_block_unserialize failed");
      ret = -1;
      goto write_err;
    }
      
    if (!((blk->type == OP_TYPE_FOP_REPLY) || (blk->type == OP_TYPE_MGMT_REPLY))) {
      gf_log ("transport/tcp", GF_LOG_DEBUG, "unexpected block type %d recieved", blk->type);
      ret = -1;
      goto write_err;
    }
    
    dict_unserialize (blk->data, blk->size, &reply);

    if (reply == NULL) {
      gf_log ("transport-socket", GF_LOG_DEBUG, "dict_unserialize failed");
      ret = -1;
      goto write_err;
    }
    
    pthread_mutex_unlock (&priv->io_mutex);
    free (blk->data);    
    free (blk);
  }
  goto ret;

 write_err:
  pthread_mutex_unlock (&priv->mutex);
  pthread_mutex_unlock (&priv->io_mutex);
    
 ret:
  if (mine->next) {
    pthread_mutex_unlock (&mine->next->mutex);
    pthread_mutex_destroy (&mine->next->mutex);
    free (mine->next);
  }

  pthread_mutex_unlock (&mine->mutex);
  return ret;
}

int
fops_xfer (struct brick_private *priv,
	   glusterfs_op_t op,
	   dict_t *request, 
	   dict_t *reply)
{
  return  generic_xfer (priv, 
			op, 
			request, 
			reply, 
			OP_TYPE_FOP_REQUEST);
}

int
mgmt_xfer (struct brick_private *priv,
	   glusterfs_mgmt_op_t op,
	   dict_t *request, 
	   dict_t *reply)
{
  return generic_xfer (priv,
		       op,
		       request,
		       reply,
		       OP_TYPE_MGMT_REQUEST);
}

static int32_t 
do_handshake (struct xlator *xl)
{

  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int32_t ret;
  int32_t remote_errno;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (&request, 
	    "remote-subvolume",
	    dict_get (xl->options, "remote-subvolume"));

  ret = mgmt_xfer (priv, OP_SETVOLUME, &request, &reply);
  
  dict_destroy (&request);

  if (ret != 0) 
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
try_connect (struct xlator *xl)
{
  struct brick_private *priv = xl->private;
  struct sockaddr_in sin;
  struct sockaddr_in sin_src;
  int32_t ret = 0;
  int32_t try_port = CLIENT_PORT_CIELING;

  if (priv->sock == -1)
    priv->sock = socket (priv->addr_family, SOCK_STREAM, 0);

  if (priv->sock == -1) {
    gf_log ("transport/tcp", GF_LOG_ERROR, "try_connect: error: %s", strerror (errno));
    return -errno;
  }

  while (try_port){ 
    sin_src.sin_family = PF_INET;
    sin_src.sin_port = htons (try_port); //FIXME: have it a #define or configurable
    sin_src.sin_addr.s_addr = INADDR_ANY;
    
    if ((ret = bind (priv->sock, (struct sockaddr *)&sin_src, sizeof (sin_src))) == 0) {
      break;
    }
    
    try_port--;
  }
  
  if (ret != 0){
      gf_log ("transport/tcp", GF_LOG_ERROR, "try_connect: error: %s", strerror (errno));
      close (priv->sock);
      return -errno;
  }

  sin.sin_family = priv->addr_family;
  sin.sin_port = priv->port;
  sin.sin_addr.s_addr = priv->addr;

  if (connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    gf_log ("transport/tcp", GF_LOG_ERROR, "try_connect: error: %s", strerror (errno));
    close (priv->sock);
    priv->sock = -1;
    return -errno;
  }

  priv->connected = 1;
/*   priv->sock_fp = fdopen (priv->sock, "a+"); */
/*   setvbuf (priv->sock_fp, NULL, _IONBF, 0); */

  ret = do_handshake (xl);

  pthread_mutex_init (&priv->mutex, NULL);
  pthread_mutex_init (&priv->io_mutex, NULL);
  return ret;
}


static int
brick_getattr (struct xlator *xl,
	       const char *path,
	       struct stat *stbuf)
{
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int32_t ret;
  int32_t remote_errno;
  char *buf = NULL;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (&request, "PATH", str_to_data ((char *)path));

  ret = fops_xfer (priv, OP_GETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0) 
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));

  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  buf = data_to_bin (dict_get (&reply, "BUF"));

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
  dict_destroy (&reply);
  return ret;
}

static int
brick_readlink (struct xlator *xl,
		const char *path,
		char *dest,
		size_t size)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    //    data_t *prefilled = bin_to_data (dest, size);
    //    dict_set (&reply, "PATH", prefilled);

    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "LEN", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_READLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0){
    errno = remote_errno;
    goto ret;
  }
  memcpy (dest, data_to_bin (dict_get (&reply, "PATH")), ret);
  
  if (ret < 0) {
    errno = remote_errno;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_mknod (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     dev_t dev,
	     uid_t uid,
	     gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "MODE", int_to_data (mode));
    dict_set (&request, "DEV", int_to_data (dev));
    dict_set (&request, "UID", int_to_data (uid));
    dict_set (&request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_MKNOD, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_mkdir (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     uid_t uid,
	     gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "MODE", int_to_data (mode));
    dict_set (&request, "UID", int_to_data (uid));
    dict_set (&request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_MKDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_unlink (struct xlator *xl,
	      const char *path)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_UNLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_rmdir (struct xlator *xl,
	     const char *path)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_RMDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}



static int
brick_symlink (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)oldpath));
    dict_set (&request, "BUF", str_to_data ((char *)newpath));
    dict_set (&request, "UID", int_to_data (uid));
    dict_set (&request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_SYMLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_rename (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)oldpath));
    dict_set (&request, "BUF", str_to_data ((char *)newpath));
    dict_set (&request, "UID", int_to_data (uid));
    dict_set (&request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_RENAME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_link (struct xlator *xl,
	    const char *oldpath,
	    const char *newpath,
	    uid_t uid,
	    gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)oldpath));
    dict_set (&request, "BUF", str_to_data ((char *)newpath));
    dict_set (&request, "UID", int_to_data (uid));
    dict_set (&request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_LINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_chmod (struct xlator *xl,
	     const char *path,
	     mode_t mode)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "MODE", int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_CHMOD, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_chown (struct xlator *xl,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "UID", int_to_data (uid));
    dict_set (&request, "GID", int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_CHOWN, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_truncate (struct xlator *xl,
		const char *path,
		off_t offset)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "OFFSET", int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_TRUNCATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_utime (struct xlator *xl,
	     const char *path,
	     struct utimbuf *buf)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "ACTIME", int_to_data (buf->actime));
    dict_set (&request, "MODTIME", int_to_data (buf->modtime));
  }

  ret = fops_xfer (priv, OP_UTIME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


static int
brick_open (struct xlator *xl,
	    const char *path,
	    int32_t flags,
	    mode_t mode,
	    struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FLAGS", int_to_data (flags));
    dict_set (&request, "MODE", int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_OPEN, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  ret = 0;
  {
    struct file_context *trav = ctx;
    struct file_context *brick_ctx = calloc (1, sizeof (struct file_context));
    brick_ctx->volume = xl;
    brick_ctx->next = NULL;
    void **tmp;
    tmp = &(brick_ctx->context);
    *(long *)tmp = data_to_int (dict_get (&reply, "FD"));
    
    while (trav->next)
      trav = trav->next;
    
    trav->next = brick_ctx;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_read (struct xlator *xl,
	    const char *path,
	    char *buf,
	    size_t size,
	    off_t offset,
	    struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int64_t fd;
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
    //    dict_set (&reply, "BUF", prefilled);
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FD", int_to_data (fd));
    dict_set (&request, "OFFSET", int_to_data (offset));
    dict_set (&request, "LEN", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_READ, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  memcpy (buf, data_to_bin (dict_get (&reply, "BUF")), ret);

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_write (struct xlator *xl,
	     const char *path,
	     const char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int64_t fd;
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
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "OFFSET", int_to_data (offset));
    dict_set (&request, "FD", int_to_data (fd));
    dict_set (&request, "BUF", bin_to_data ((void *)buf, size));
  }

  ret = fops_xfer (priv, OP_WRITE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_statfs (struct xlator *xl,
	      const char *path,
	      struct statvfs *stbuf)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_STATFS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    char *buf = data_to_bin (dict_get (&reply, "BUF"));

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
  dict_destroy (&reply);
  return ret;
}

static int
brick_flush (struct xlator *xl,
	     const char *path,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int64_t fd;
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
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FD", int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_FLUSH, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_release (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int64_t fd;
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
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FD", int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_RELEASE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
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
  dict_destroy (&reply);
  return ret;
}

static int
brick_fsync (struct xlator *xl,
	     const char *path,
	     int32_t datasync,
	     struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int64_t fd;
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
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FLAGS", int_to_data (datasync));
    dict_set (&request, "FD", int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_FSYNC, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_setxattr (struct xlator *xl,
		const char *path,
		const char *name,
		const char *value,
		size_t size,
		int32_t flags)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FLAGS", int_to_data (flags));
    dict_set (&request, "COUNT", int_to_data (size));
    dict_set (&request, "BUF", str_to_data ((char *)name));
    dict_set (&request, "FD", str_to_data ((char *)value));
  }

  ret = fops_xfer (priv, OP_SETXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_getxattr (struct xlator *xl,
		const char *path,
		const char *name,
		char *value,
		size_t size)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "BUF", str_to_data ((char *)name));
    dict_set (&request, "COUNT", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_GETXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  
  {
    strcpy (value, data_to_str (dict_get (&reply, "BUF")));
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_listxattr (struct xlator *xl,
		 const char *path,
		 char *list,
		 size_t size)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "COUNT", int_to_data (size));
  }

  ret = fops_xfer (priv, OP_LISTXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    memcpy (list, data_to_str (dict_get (&reply, "BUF")), ret);
  }

 ret:
  dict_destroy (&reply);
  return ret;
}
		     
static int
brick_removexattr (struct xlator *xl,
		   const char *path,
		   const char *name)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "BUF", str_to_data ((char *)name));
  }

  ret = fops_xfer (priv, OP_REMOVEXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_opendir (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
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
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FD", int_to_data ((long)tmp->context));
  }

  ret = fops_xfer (priv, OP_OPENDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static char *
brick_readdir (struct xlator *xl,
	       const char *path,
	       off_t offset)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  data_t *datat = NULL;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "OFFSET", int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_READDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    gf_log ("tcp", GF_LOG_NORMAL, "tcp.c->readdir: readdir failed for %s\n", path);
    goto ret;
  }

  {
    /* Here I get a data in ASCII, with '/' as the IFS, now I need to process them */
    datat = dict_get (&reply, "BUF");
    datat->is_static = 1;
  }

 ret:
  dict_destroy (&reply);
  if (datat && ret == 0)
    return (char *)datat->data;
  else 
    return NULL;
}

static int
brick_releasedir (struct xlator *xl,
		  const char *path,
		  struct file_context *ctx)
{
  int32_t ret = 0;
  /*int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_RELEASE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);*/
  return ret;
}

static int
brick_fsyncdir (struct xlator *xl,
		const char *path,
		int32_t datasync,
		struct file_context *ctx)
{
  int32_t ret = 0;
  /*  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FLAGS", int_to_data (datasync));
  }

  ret = fops_xfer (priv, OP_FSYNCDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply); */
  return ret;
}


static int
brick_access (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "MODE", int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_ACCESS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_ftruncate (struct xlator *xl,
		 const char *path,
		 off_t offset,
		 struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int64_t fd;
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
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FD", int_to_data (fd));
    dict_set (&request, "OFFSET", int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_FTRUNCATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_fgetattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf,
		struct file_context *ctx)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 

  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "FD", int_to_data ((long)tmp->context));
  }

  ret = fops_xfer (priv, OP_FGETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    char *buf = data_to_bin (dict_get (&reply, "BUF"));

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
  dict_destroy (&reply);
  return ret;
}


static int
brick_bulk_getattr (struct xlator *xl,
		    const char *path,
		    struct bulk_stat *bstbuf)
{
  struct bulk_stat *curr = NULL;
  struct stat *stbuf = NULL;
  char *buffer_ptr = NULL;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int32_t ret;
  int32_t remote_errno;
  char *buf = NULL;
  uint32_t nr_entries = 0;
  char pathname[PATH_MAX] = {0,};

  /* play it safe */
  bstbuf->stbuf = NULL;
  bstbuf->next = NULL;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (&request, "PATH", str_to_data ((char *)path));

  ret = fops_xfer (priv, OP_BULKGETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0) 
    goto fail;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    gf_log ("tcp", GF_LOG_ERROR, "tcp.c->bulk_getattr: remote bulk_getattr returned \"%d\"\n", remote_errno);
    errno = remote_errno;
    goto fail;
  }
  
  nr_entries = data_to_int (dict_get (&reply, "NR_ENTRIES"));
  buf = data_to_bin (dict_get (&reply, "BUF"));

  buffer_ptr = buf;
  while (nr_entries) {
    int32_t bread = 0;
    char tmp_buf[512] = {0,};
    curr = calloc (sizeof (struct bulk_stat), 1);
    curr->stbuf = calloc (sizeof (struct stat), 1);
    
    stbuf = curr->stbuf;
    nr_entries--;
    /*    sscanf (buffer_ptr, "%s", pathname);*/
    char *ender = strchr (buffer_ptr, '/');
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
  dict_destroy (&reply);
  return ret;
}

/*
 * MGMT_OPS
 */

static int
brick_stats (struct xlator *xl, struct xlator_stats *stats)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  dict_set (&request, "LEN", int_to_data (0)); // without this dummy key the server crashes
  ret = mgmt_xfer (priv, OP_STATS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    char *buf = data_to_bin (dict_get (&reply, "BUF"));
    sscanf (buf, GF_MGMT_STATS_SCAN_FMT_STR,
	    &stats->nr_files,
	    &stats->disk_usage,
	    &stats->free_disk,
	    &stats->read_usage,
	    &stats->write_usage,
	    &stats->disk_speed,
	    &stats->nr_clients);
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_lock (struct xlator *xl,
	    const char *name)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)name));
  }

  ret = mgmt_xfer (priv, OP_LOCK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_unlock (struct xlator *xl,
	      const char *name)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, "PATH", str_to_data ((char *)name));
  }

  ret = mgmt_xfer (priv, OP_UNLOCK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_listlocks (struct xlator *xl)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    printf ("tcp listlocks called");
  }
  
  dict_set (&request, "OP", int_to_data (0xcafebabe));
  ret = mgmt_xfer (priv, OP_LISTLOCKS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  int32_t junk = data_to_int (dict_get (&reply, "RET_OP"));
 
  printf ("returned junk is %x\n", junk);
 
  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  
  {
    /* now, recieve the locks and pass them to the person who called us */
    ;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_nslookup (struct xlator *xl,
		const char *path,
		dict_t *ns)
{
  return -1;
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  char *ns_str;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
  }

  ret = mgmt_xfer (priv, OP_NSLOOKUP, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));
  ns_str = data_to_str (dict_get (&reply, "NS"));

  if (ns_str && strlen (ns_str) > 0)
    dict_unserialize (ns_str, strlen (ns_str), &ns);
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_nsupdate (struct xlator *xl,
		const char *path,
		dict_t *ns)
{
  return -1;
  int32_t ret = 0;
  int32_t remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  char *ns_str = calloc (1, dict_serialized_length (ns));
  dict_serialize (ns, ns_str);
  {
    dict_set (&request, "PATH", str_to_data ((char *)path));
    dict_set (&request, "NS", str_to_data (ns_str));
  }

  ret = mgmt_xfer (priv, OP_NSUPDATE, &request, &reply);
  dict_destroy (&request);
  free (ns_str);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, "RET"));
  remote_errno = data_to_int (dict_get (&reply, "ERRNO"));

  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

int
init (struct xlator *xl)
{
  struct brick_private *_private = calloc (1, sizeof (*_private));
  data_t *host_data, *port_data, *debug_data, *addr_family_data, *volume_data;
  char *port_str = "5252";

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
  .getattr     = brick_getattr,
  .readlink    = brick_readlink,
  .mknod       = brick_mknod,
  .mkdir       = brick_mkdir,
  .unlink      = brick_unlink,
  .rmdir       = brick_rmdir,
  .symlink     = brick_symlink,
  .rename      = brick_rename,
  .link        = brick_link,
  .chmod       = brick_chmod,
  .chown       = brick_chown,
  .truncate    = brick_truncate,
  .utime       = brick_utime,
  .open        = brick_open,
  .read        = brick_read,
  .write       = brick_write,
  .statfs      = brick_statfs,
  .flush       = brick_flush,
  .release     = brick_release,
  .fsync       = brick_fsync,
  .setxattr    = brick_setxattr,
  .getxattr    = brick_getxattr,
  .listxattr   = brick_listxattr,
  .removexattr = brick_removexattr,
  .opendir     = brick_opendir,
  .readdir     = brick_readdir,
  .releasedir  = brick_releasedir,
  .fsyncdir    = brick_fsyncdir,
  .access      = brick_access,
  .ftruncate   = brick_ftruncate,
  .fgetattr    = brick_fgetattr,
  .bulk_getattr = brick_bulk_getattr
};

struct xlator_mgmt_ops mgmt_ops = {
  .stats = brick_stats,
  .lock = brick_lock,
  .unlock = brick_unlock,
  .listlocks = brick_listlocks,
  .nslookup = brick_nslookup,
  .nsupdate = brick_nsupdate
};
