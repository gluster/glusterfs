
#include "glusterfs.h"
#include "brick.h"
#include "dict.h"
#include "xlator.h"


int
generic_xfer (struct brick_private *priv,
	      int op,
	      dict_t *request, 
	      dict_t *reply,
	      const char *begin,
	      const char *end)
{
  int ret = 0;
  char op_str[16];
  struct wait_queue *mine = (void *) calloc (1, sizeof (*mine));

  
  pthread_mutex_init (&mine->mutex, NULL);
  pthread_mutex_lock (&mine->mutex);

  pthread_mutex_lock (&priv->mutex);
  mine->next = priv->queue;
  priv->queue = mine;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    pthread_mutex_lock (&priv->io_mutex);

    if (fprintf (priv->sock_fp, begin) != strlen (begin)) {
      ret = -errno;
      goto write_err;
    }

    sprintf (op_str, "%d\n", op);
    if (fprintf (priv->sock_fp, "%s", op_str) != strlen (op_str)) {
      ret  = -errno;
      goto  write_err;
    }

    if (dict_dump (priv->sock_fp, request) != 0) {
      ret = -errno;
      goto write_err;
    }

    if (fprintf (priv->sock_fp, end) != strlen (end)) {
      ret = -errno;
      goto write_err;
    }

    if (fflush (priv->sock_fp) != 0) {
      ret = -errno;
      goto write_err;
    }
    pthread_mutex_unlock (&priv->io_mutex);
  }

  pthread_mutex_unlock (&priv->mutex);

  if (mine->next)
    pthread_mutex_lock (&mine->next->mutex);

  {

    pthread_mutex_lock (&priv->io_mutex);
    int _ret = dict_fill (priv->sock_fp, reply);
    pthread_mutex_unlock (&priv->io_mutex);
    if (!_ret) {
      if (priv->is_debug) {
	printf ("dict_fill failed\n");
      }
      ret = -1;
      goto read_err;
    }
  }
  goto ret;

 write_err:
  pthread_mutex_unlock (&priv->mutex);
  pthread_mutex_unlock (&priv->io_mutex);
    
 read_err:
  if (mine->next) {
    pthread_mutex_unlock (&mine->next->mutex);
    pthread_mutex_destroy (&mine->next->mutex);
    free (mine->next);
  }

 ret:
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
			"BeginFops\n",
			"EndFops\n");
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
		       "BeginMgmt\n",
		       "EndMgmt\n");
}

static int 
do_handshake (struct xlator *xl)
{

  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  data_t *volume = str_to_data ("ExpVolume");
  int ret;
  int remote_errno;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (&request, 
	    volume,
	    dict_get (xl->options, volume));

  ret = mgmt_xfer (priv, OP_SETVOLUME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0) 
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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

  if (priv->sock == -1)
    priv->sock = socket (priv->addr_family, SOCK_STREAM, 0);

  if (priv->sock == -1) {
    perror ("socket()");
    return -errno;
  }

  sin.sin_family = priv->addr_family;
  sin.sin_port = priv->port;
  sin.sin_addr.s_addr = priv->addr;

  if (connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    perror ("connect()");
    close (priv->sock);
    priv->sock = -1;
    return -errno;
  }

  priv->connected = 1;
  priv->sock_fp = fdopen (priv->sock, "a+");
  setvbuf (priv->sock_fp, NULL, _IONBF, 0);

  do_handshake (xl);

  pthread_mutex_init (&priv->mutex, NULL);
  pthread_mutex_init (&priv->io_mutex, NULL);
  return 0;
}

static int
brick_getattr (struct xlator *xl,
	       const char *path,
	       struct stat *stbuf)
{
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int ret;
  int remote_errno;
  char *buf = NULL;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  dict_set (&request, DATA_PATH, str_to_data ((char *)path));

  ret = fops_xfer (priv, OP_GETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0) 
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  buf = data_to_bin (dict_get (&reply, DATA_BUF));
  sscanf (buf, "%llx,%llx,%x,%x,%x,%x,%llx,%llx,%lx,%llx,%lx,%lx,%lx\n",
	  &stbuf->st_dev,
	  &stbuf->st_ino,
	  &stbuf->st_mode,
	  &stbuf->st_nlink,
	  &stbuf->st_uid,
	  &stbuf->st_gid,
	  &stbuf->st_rdev,
	  &stbuf->st_size,
	  &stbuf->st_blksize,
	  &stbuf->st_blocks,
	  &stbuf->st_atime,
	  &stbuf->st_mtime,
	  &stbuf->st_ctime);

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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    data_t *prefilled = bin_to_data (dest, size);
    dict_set (&reply, DATA_PATH, prefilled);

    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_LEN, int_to_data (size));
  }

  ret = fops_xfer (priv, OP_READLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
    dict_set (&request, DATA_DEV, int_to_data (dev));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_MKNOD, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_MKDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_UNLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_RMDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)oldpath));
    dict_set (&request, DATA_BUF, str_to_data ((char *)newpath));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_SYMLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)oldpath));
    dict_set (&request, DATA_BUF, str_to_data ((char *)newpath));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_RENAME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)oldpath));
    dict_set (&request, DATA_BUF, str_to_data ((char *)newpath));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_LINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_CHMOD, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = fops_xfer (priv, OP_CHOWN, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_TRUNCATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_ACTIME, int_to_data (buf->actime));
    dict_set (&request, DATA_MODTIME, int_to_data (buf->modtime));
  }

  ret = fops_xfer (priv, OP_UTIME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
	    int flags,
	    mode_t mode,
	    struct file_context *ctx)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (flags));
    dict_set (&request, DATA_MODE, int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_OPEN, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
    *(int *)&brick_ctx->context = data_to_int (dict_get (&reply, DATA_FD));
    
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  }
  fd = (int)tmp->context;

  {
    data_t *prefilled = bin_to_data (buf, size);
    dict_set (&reply, DATA_BUF, prefilled);
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
    dict_set (&request, DATA_LEN, int_to_data (size));
  }

  ret = fops_xfer (priv, OP_READ, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 
  fd = (int)tmp->context;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
    dict_set (&request, DATA_FD, int_to_data (fd));
    dict_set (&request, DATA_BUF, bin_to_data ((void *)buf, size));
  }

  ret = fops_xfer (priv, OP_WRITE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_STATFS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    char *buf = data_to_bin (dict_get (&reply, DATA_BUF));
    sscanf (buf, "%lx,%lx,%llx,%llx,%llx,%llx,%llx,%llx,%lx,%lx,%lx\n",
	    &stbuf->f_bsize,
	    &stbuf->f_frsize,
	    &stbuf->f_blocks,
	    &stbuf->f_bfree,
	    &stbuf->f_bavail,
	    &stbuf->f_files,
	    &stbuf->f_ffree,
	    &stbuf->f_favail,
	    &stbuf->f_fsid,
	    &stbuf->f_flag,
	    &stbuf->f_namemax);
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  }
  fd = (int)tmp->context;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_FLUSH, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);  
  if (tmp == NULL) {
    return -1;
  } 
  fd = (int)tmp->context;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_RELEASE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    /* Free the file_context struct for brick node */
    RM_MY_CTX (ctx, tmp);
    free (tmp);
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_fsync (struct xlator *xl,
	     const char *path,
	     int datasync,
	     struct file_context *ctx)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);  
  if (tmp == NULL) {
    return -1;
  }
  fd = (int)tmp->context;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (datasync));
    dict_set (&request, DATA_FD, int_to_data (fd));
  }

  ret = fops_xfer (priv, OP_FSYNC, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
		int flags)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (flags));
    dict_set (&request, DATA_COUNT, int_to_data (size));
    dict_set (&request, DATA_BUF, str_to_data ((char *)name));
    dict_set (&request, DATA_FD, str_to_data ((char *)value));
  }

  ret = fops_xfer (priv, OP_SETXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_BUF, str_to_data ((char *)name));
    dict_set (&request, DATA_COUNT, int_to_data (size));
  }

  ret = fops_xfer (priv, OP_GETXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }
  
  {
    strcpy (value, data_to_str (dict_get (&reply, DATA_BUF)));
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_COUNT, int_to_data (size));
  }

  ret = fops_xfer (priv, OP_LISTXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    memcpy (list, data_to_str (dict_get (&reply, DATA_BUF)), ret);
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_BUF, str_to_data ((char *)name));
  }

  ret = fops_xfer (priv, OP_REMOVEXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
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
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data ((int)tmp->context));
  }

  ret = fops_xfer (priv, OP_OPENDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  data_t *datat = NULL;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_READDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    /* Here I get a data in ASCII, with '/' as the IFS, now I need to process them */
    datat = dict_get (&reply, DATA_BUF);
    datat->is_static = 1;
  }

 ret:
  dict_destroy (&reply);
  if (datat)
    return (char *)datat->data;
  else 
    return NULL;
}

static int
brick_releasedir (struct xlator *xl,
		  const char *path,
		  struct file_context *ctx)
{
  int ret = 0;
  /*int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = fops_xfer (priv, OP_RELEASE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
		int datasync,
		struct file_context *ctx)
{
  int ret = 0;
  /*  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (datasync));
  }

  ret = fops_xfer (priv, OP_FSYNCDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
  }

  ret = fops_xfer (priv, OP_ACCESS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  int fd;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (tmp == NULL) {
    return -1;
  } 
  fd = (int)tmp->context;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
  }

  ret = fops_xfer (priv, OP_FTRUNCATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
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
  int ret = 0;
  int remote_errno = 0;
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
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data ((int)tmp->context));
  }

  ret = fops_xfer (priv, OP_FGETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    char *buf = data_to_bin (dict_get (&reply, DATA_BUF));
    sscanf (buf, "%llx,%llx,%x,%x,%x,%x,%llx,%llx,%lx,%llx,%lx,%lx,%lx\n",
	    &stbuf->st_dev,
	    &stbuf->st_ino,
	    &stbuf->st_mode,
	    &stbuf->st_nlink,
	    &stbuf->st_uid,
	    &stbuf->st_gid,
	    &stbuf->st_rdev,
	    &stbuf->st_size,
	    &stbuf->st_blksize,
	    &stbuf->st_blocks,
	    &stbuf->st_atime,
	    &stbuf->st_mtime,
	    &stbuf->st_ctime);
  }

  ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_stats (struct xlator *xl, struct xlator_stats *stats)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  dict_set (&request, DATA_LEN, int_to_data (0)); // without this dummy key the server crashes
  ret = fops_xfer (priv, OP_STATS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    errno = remote_errno;
    goto ret;
  }

  {
    char *buf = data_to_bin (dict_get (&reply, DATA_BUF));
    sscanf (buf, "%lx,%lx,%llx,%llx\n",
	    &stats->nr_files,
	    &stats->free_mem,
	    &stats->free_disk,
	    &stats->nr_clients);
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

  host_data = dict_get (xl->options, str_to_data ("Host"));
  port_data = dict_get (xl->options, str_to_data ("Port"));
  debug_data = dict_get (xl->options, str_to_data ("Debug"));
  addr_family_data = dict_get (xl->options, str_to_data ("AddressFamily"));
  volume_data = dict_get (xl->options, str_to_data ("ExpVolume"));
  
  if (!host_data) {
    fprintf (stderr, "Volume %s does not have 'Host' section\n",  xl->name);
    return -1;
  }
  _private->addr = resolve_ip (data_to_str (host_data));

  if (!volume_data) {
    fprintf (stderr, "Volume %s does not have 'Volume' section\n", xl->name);
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
      fprintf (stderr, "Unsupported address family: %s\n", data_to_str (addr_family_data));
      return -1;
    }
  }

  if (_private->is_debug) {
    FUNCTION_CALLED;
    printf ("Host(:Port) = %s:%s\n", data_to_str (host_data), port_str);
    printf ("Debug mode on\n");
  }

  _private->port = htons (strtol (port_str, NULL, 0));
  _private->sock = -1;

  xl->private = (void *)_private;
  try_connect (xl);

  return 0;
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
  .stats       = brick_stats
};
