
#include "glusterfs.h"
#include "brick.h"
#include "dict.h"
#include "xlator.h"


static int
try_connect (struct brick_private *priv)
{
  struct sockaddr_in sin;

  if (priv->sock == -1)
    priv->sock = socket (PF_INET, SOCK_STREAM, 0);

  if (priv->sock == -1) {
    perror ("socket()");
    return -errno;
  }

  sin.sin_family = PF_INET;
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
  pthread_mutex_init (&priv->mutex, NULL);
  return 0;
}


int
interleaved_xfer (struct brick_private *priv,
		  glusterfs_op_t op,
		  dict_t *request, 
		  dict_t *reply)
{
  int ret = 0;
  char op_str[16];
  struct wait_queue *mine = (void *) calloc (1, sizeof (*mine));

  
  pthread_mutex_init (&mine->mutex, NULL);
  pthread_mutex_lock (&mine->mutex);

  pthread_mutex_lock (&priv->mutex);
  mine->next = priv->queue;
  priv->queue = mine;

  sprintf (op_str, "%d\n", op);
  if (fprintf (priv->sock_fp, "%s", op_str) != strlen (op_str)) {
    ret  = -errno;
    goto  write_err;
  }

  if (dict_dump (priv->sock_fp, request) != 0) {
    ret = -errno;
    goto write_err;
  }
  if (fflush (priv->sock_fp) != 0) {
    ret = -errno;
    goto write_err;
  }

  pthread_mutex_unlock (&priv->mutex);

  if (mine->next)
    pthread_mutex_lock (&mine->next->mutex);

  if (!dict_fill (priv->sock_fp, reply)) {
    ret = -1;
    goto read_err;
  }
  goto ret;

 write_err:
  pthread_mutex_unlock (&priv->mutex);
    
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
  FUNCTION_CALLED;
  
  dict_set (&request, DATA_PATH, str_to_data ((char *)path));

  ret = interleaved_xfer (priv, OP_GETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0) 
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&reply, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_READLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

  {
    data_t *d = dict_get (&reply, DATA_PATH);

    ret = d->len;    
    if (ret > size)
      ret = size;
    memcpy (dest, d->data, ret);
  }

 ret:
  dict_destroy (&reply);
  return ret;
}


/*
static int
brick_getdir (const char *path,
		  fuse_dirh_t dirh,
		  fuse_dirfil_t dirfil)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {

  }

  ret = interleaved_xfer (priv, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret != 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:

  dict_destroy (&reply);
  return ret;
}
*/

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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
    dict_set (&request, DATA_DEV, int_to_data (dev));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = interleaved_xfer (priv, OP_MKNOD, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = interleaved_xfer (priv, OP_MKDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_UNLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_RMDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)oldpath));
    dict_set (&request, DATA_BUF, str_to_data ((char *)newpath));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = interleaved_xfer (priv, OP_SYMLINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)oldpath));
    dict_set (&request, DATA_BUF, str_to_data ((char *)newpath));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = interleaved_xfer (priv, OP_RENAME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)oldpath));
    dict_set (&request, DATA_BUF, str_to_data ((char *)newpath));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = interleaved_xfer (priv, OP_LINK, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
  }

  ret = interleaved_xfer (priv, OP_CHMOD, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_UID, int_to_data (uid));
    dict_set (&request, DATA_GID, int_to_data (gid));
  }

  ret = interleaved_xfer (priv, OP_CHOWN, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
  }

  ret = interleaved_xfer (priv, OP_TRUNCATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_ACTIME, int_to_data (buf->actime));
    dict_set (&request, DATA_MODTIME, int_to_data (buf->modtime));
  }

  ret = interleaved_xfer (priv, OP_UTIME, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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
	    struct file_context *cxt)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (flags));
  }

  ret = interleaved_xfer (priv, OP_OPEN, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }
  ret = 0;
  {
    struct file_context *trav = cxt;
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
  struct file_context *tmp = ctx->next;

  while (tmp != NULL && tmp->volume != xl)
    tmp = tmp->next;
  
  if (tmp == NULL) {
    /* Don't have open file descriptor for the file */
    return -1;
  }
 
  fd = (int)tmp->context;
  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
    dict_set (&request, DATA_LEN, int_to_data (size));
  }

  ret = interleaved_xfer (priv, OP_READ, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

  {
    memcpy (buf, data_to_bin (dict_get (&reply, DATA_BUF)), ret);
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
  struct file_context *tmp = ctx->next;

  while (tmp != NULL && tmp->volume != xl)
    tmp = tmp->next;
  
  if (tmp == NULL) {
    /* Don't have open file descriptor for the file */
    return -1;
  }
 
  fd = (int)tmp->context;

  FUNCTION_CALLED;

  {
    //dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
    dict_set (&request, DATA_FD, int_to_data (fd));
    dict_set (&request, DATA_BUF, bin_to_data ((void *)buf, size));
  }

  ret = interleaved_xfer (priv, OP_WRITE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_statfs (struct xlator *xl,
	      const char *path,
	      struct statvfs *buf)
{
  int ret = 0;
  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_STATFS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

  {
    data_t *datat = data_to_bin (dict_get (&reply, DATA_BUF));
    memcpy (buf, datat->data, datat->len);
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
  struct file_context *tmp = ctx->next;

  while (tmp != NULL && tmp->volume != xl)
    tmp = tmp->next;
  
  if (tmp == NULL) {
    /* Don't have open file descriptor for the file */
    return -1;
  }
 
  fd = (int)tmp->context;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
  }

  ret = interleaved_xfer (priv, OP_FLUSH, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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
  struct file_context *tmp = ctx;

  while (tmp != NULL && tmp->volume != xl)
    tmp = tmp->next;
  
  if (tmp == NULL) {
    /* Don't have open file descriptor for the file */
    return -1;
  }
 
  fd = (int)tmp->context;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FD, int_to_data (fd));
  }

  ret = interleaved_xfer (priv, OP_RELEASE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

  {
    /* Free the file_context struct for brick node */
    struct file_context *trav = ctx;
    
    while (trav && trav->next != tmp)
      trav = trav->next;

    trav->next = tmp->next;
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
  struct file_context *tmp = ctx->next;

  while (tmp != NULL && tmp->volume != xl)
    tmp = tmp->next;
  
  if (tmp == NULL) {
    /* Don't have open file descriptor for the file */
    return -1;
  }
 
  fd = (int)tmp->context;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_FLAGS, int_to_data (datasync));
    dict_set (&request, DATA_FD, int_to_data (fd));
  }

  ret = interleaved_xfer (priv, OP_FSYNC, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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
  /*  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (flags));
  }

  ret = interleaved_xfer (priv, OP_SETXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);*/
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
  /*  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_GETXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);*/
  return ret;
}

static int
brick_listxattr (struct xlator *xl,
		 const char *path,
		 char *list,
		 size_t size)
{
  int ret = 0;
  /*int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_LISTXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);*/
  return ret;
}
		     
static int
brick_removexattr (struct xlator *xl,
		   const char *path,
		   const char *name)
{
  int ret = 0;
  /*  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_REMOVEXATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);*/
  return ret;
}

static int
brick_opendir (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  int ret = 0;
  /*  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_OPENDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply); */
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
  data_t *datat = {0,};
  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_READDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

  {
    /* Here I get a data in ASCII, with '/' as the IFS, now I need to process them */
    datat = dict_get (&reply, DATA_BUF);
    datat->is_static = 1;
  }

 ret:
  dict_destroy (&reply);
  return (char *)datat->data;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
  }

  ret = interleaved_xfer (priv, OP_RELEASE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (datasync));
  }

  ret = interleaved_xfer (priv, OP_FSYNCDIR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
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
  /*  int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_MODE, int_to_data (mode));
  }

  ret = interleaved_xfer (priv, OP_ACCESS, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply); */
  return ret;
}

static int
brick_create (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      struct file_context *ctx)
{
  int ret = 0;
  /*int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (mode));
  }

  ret = interleaved_xfer (priv, OP_CREATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply); */
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
  struct file_context *tmp = ctx->next;

  while (tmp != NULL && tmp->volume != xl)
    tmp = tmp->next;
  
  if (tmp == NULL) {
    /* Don't have open file descriptor for the file */
    return -1;
  }
 
  fd = (int)tmp->context;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_FD, int_to_data (fd));
    dict_set (&request, DATA_OFFSET, int_to_data (offset));
  }

  ret = interleaved_xfer (priv, OP_FTRUNCATE, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

 ret:
  dict_destroy (&reply);
  return ret;
}

static int
brick_fgetattr (struct xlator *xl,
		const char *path,
		struct stat *buf,
		struct file_context *ctx)
{
  
  int ret = 0;
  /*int remote_errno = 0;
  struct brick_private *priv = xl->private;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;

  FUNCTION_CALLED;

  {
    dict_set (&request, DATA_PATH, str_to_data ((char *)path));
    dict_set (&request, DATA_FLAGS, int_to_data (flags));
  }

  ret = interleaved_xfer (priv, OP_FGETATTR, &request, &reply);
  dict_destroy (&request);

  if (ret != 0)
    goto ret;

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret < 0) {
    ret = -remote_errno;
    goto ret;
  }

  ret:
  dict_destroy (&reply);*/
  return ret;
}

void
init (struct xlator *xl)
{
  FUNCTION_CALLED;
  struct brick_private *_private = (void *) calloc (1, sizeof (*_private));
  data_t *host_data, *port_data;
  char *port_str = "5252";

  host_data = dict_get (xl->options, str_to_data ("Host"));
  port_data = dict_get (xl->options, str_to_data ("Port"));

  if (!host_data) {
    fprintf (stderr, "Volume %s does not have 'Host' section\n",  xl->name);
    exit (1);
  }
  _private->addr = resolve_ip (data_to_str (host_data));

  if (port_data)
    port_str = data_to_str (port_data);

  _private->port = htons (strtol (port_str, NULL, 0));
  _private->sock = -1;
  try_connect (_private);

  xl->private = (void *)_private;
  return;
}

void
fini (struct xlator *xl)
{
  /*
  struct glusterfs_private *priv = data;

  if (priv->sock != -1)
    close (priv->sock);
  free (priv);
  return;
  */
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
  .create      = brick_create,
  .ftruncate   = brick_ftruncate,
  .fgetattr    = brick_fgetattr
};

