
#include "glusterfs.h"



static int
try_connect (struct glusterfs_private *priv)
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
interleaved_xfer (struct glusterfs_private *priv,
		  dict_t *request, 
		  dict_t *reply)
{
  int ret = 0;
  int size = 0;
  FILE *fp;
  struct wait_queue *mine = (void *) calloc (1, sizeof (*mine));

  
  pthread_mutex_init (&mine->mutex, NULL);
  pthread_mutex_lock (&mine->mutex);

  pthread_mutex_lock (&priv->mutex);
  mine->next = priv->queue;
  priv->queue = mine;

  if (!dict_dump (priv->sock_fp, request)) {
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

  if (!dict_fill (priv->sock_fp, &reply)) {
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
brick_getattr (const char *path,
		   struct stat *stbuf)
{
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict_t request = STATIC_DICT;
  dict_t reply = STATIC_DICT;
  data_t data_st_dev = STATIC_DATA_STR ("dev");
  data_t data_st_ino = STATIC_DATA_STR ("ino");
  data_t data_st_mode = STATIC_DATA_STR ("mode");
  data_t data_st_nlink = STATIC_DATA_STR ("nlink");
  data_t data_st_uid = STATIC_DATA_STR ("uid");
  data_t data_st_gid = STATIC_DATA_STR ("gid");
  data_t data_st_rdev = STATIC_DATA_STR ("rdev");
  data_t data_st_size = STATIC_DATA_SIZE ("size");
  data_t data_st_blksize = STATIC_DATA_STR ("blksize");
  data_t data_st_blocks = STATIC_DATA_STR ("blocks");
  data_t data_st_atime = STATIC_DATA_STR ("atime");
  data_t data_st_mtime = STATIC_DATA_STR ("mtime");
  data_t data_st_ctime = STATIC_DATA_STR ("ctime");
  int ret;
  int remote_errno;

  FUNCTION_CALLED;
  
  size = strlen (path) + 1;
  dict_set (&request, DATA_OP, int_to_data (OP_GETATTR));
  dict_set (&request, DATA_PATH, str_to_data (path));

  interleaved_xfer (priv, &request, &reply);
  dict_destroy (&request);

  ret = data_to_int (dict_get (&reply, DATA_RET));
  remote_errno = data_to_int (dict_get (&reply, DATA_ERRNO));
  
  if (ret != 0) {
    dict_destroy (&reply);
    return -remote_errno;
  }

  stbuf->st_dev = data_to_int (dict_get (&reply, &data_st_dev));
  stbuf->st_ino = data_to_int (dict_get (&reply, &data_st_ino));
  stbuf->st_mode = data_to_int (dict_get (&reply, &data_st_mode));
  stbuf->st_nlink = data_to_int (dict_get (&reply, &data_st_nlink));
  stbuf->st_uid = data_to_int (dict_get (&reply, &data_st_uid));
  stbuf->st_gid = data_to_int (dict_get (&reply, &data_st_gid));
  stbuf->st_rdev = data_to_int (dict_get (&reply, &data_st_rdev));
  stbuf->st_size = data_to_int (dict_get (&reply, &data_st_size));
  stbuf->st_blksize = data_to_int (dict_get (&reply, &data_st_blksize));
  stbuf->st_blocks = data_to_int (dict_get (&reply, &data_st_blocks));
  stbuf->st_atime = data_to_int (dict_get (&reply, &data_st_atime));
  stbuf->st_mtime = data_to_int (dict_get (&reply, &data_st_mtime));
  stbuf->st_ctime = data_to_int (dict_get (&reply, &data_st_ctime));

  return ret;
}


static int
brick_readlink (const char *path,
		    char *dest,
		    size_t size)
{
  int ret = 0;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict_t dict;
  dict.count = 0;
  dict.members = NULL;
  FUNCTION_CALLED;

  dict_set (&dict, static_str_to_data (XFER_OPERATION), int_to_data (OP_READLINK));
  dict_set (&dict, static_str_to_data (XFER_LEN), int_to_data (size));
  dict_set (&dict, static_str_to_data (XFER_SIZE), int_to_data (strlen (path) +1));
  dict_set (&dict, static_str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  ret = interleaved_xfer (priv, &dict, (void *)dest);
  dest[strlen (path) + 1] = 0;
  if (ret > 0)
    return 0;
  return ret;
}

/*
static int
brick_getdir (const char *path,
		  fuse_dirh_t dirh,
		  fuse_dirfil_t dirfil)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}
*/

static int
brick_mknod (const char *path,
		 mode_t mode,
		 dev_t dev)
{
  dict_t dict;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict.count = 0;
  dict.members = NULL;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_MKNOD));
  dict_set (&dict, str_to_data (XFER_MODE), int_to_data (mode));
  dict_set (&dict, str_to_data (XFER_DEV), int_to_data (dev));
  dict_set (&dict, str_to_data (XFER_UID), int_to_data (fuse_get_context ()->uid));
  dict_set (&dict, str_to_data (XFER_GID), int_to_data (fuse_get_context ()->gid));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_mkdir (const char *path,
		 mode_t mode)
{
  dict_t dict;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  
  dict.count = 0;
  dict.members = NULL;
  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_MKDIR));
  dict_set (&dict, str_to_data (XFER_MODE), int_to_data (mode));
  dict_set (&dict, str_to_data (XFER_UID), int_to_data (fuse_get_context ()->uid));
  dict_set (&dict, str_to_data (XFER_GID), int_to_data (fuse_get_context ()->gid));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_unlink (const char *path)
{
  dict_t dict;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  dict.count = 0;
  dict.members = NULL;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_UNLINK));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_rmdir (const char *path)
{
  dict_t dict;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  dict.count = 0;
  dict.members = NULL;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_RMDIR));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}


static int
brick_symlink (const char *oldpath,
		   const char *newpath)
{
  dict_t dict;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  char *tmpbuf = NULL;
  int ret;
  dict.count = 0;
  dict.members = NULL;
  FUNCTION_CALLED;

  int old_len = strlen (oldpath);
  int new_len = strlen (newpath);

  tmpbuf = (void *) calloc (1, old_len + new_len + 2);
  strcpy (tmpbuf, oldpath);
  strcpy (&tmpbuf[old_len+1], newpath);

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_SYMLINK));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (old_len + new_len + 2));
  dict_set (&dict, str_to_data (XFER_UID), int_to_data (fuse_get_context ()->uid));
  dict_set (&dict, str_to_data (XFER_GID), int_to_data (fuse_get_context ()->gid));
  dict_set (&dict, str_to_data (XFER_LEN), int_to_data (old_len + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)tmpbuf, old_len + new_len + 2));

  ret = interleaved_xfer (priv, &dict, NULL);
  free (tmpbuf);
  return ret;
}

static int
brick_rename (const char *oldpath,
		  const char *newpath)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  char *tmpbuf = NULL;
  int ret;

  FUNCTION_CALLED;

  int old_len = strlen (oldpath);
  int new_len = strlen (newpath);

  tmpbuf = (void *) calloc (1, old_len + new_len + 2);
  strcpy (tmpbuf, oldpath);
  strcpy (&tmpbuf[old_len+1], newpath);

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_RENAME));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (old_len + new_len + 2));
  dict_set (&dict, str_to_data (XFER_UID), int_to_data (fuse_get_context ()->uid));
  dict_set (&dict, str_to_data (XFER_GID), int_to_data (fuse_get_context ()->gid));
  dict_set (&dict, str_to_data (XFER_LEN), int_to_data (old_len + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)tmpbuf, old_len + new_len + 2));

  ret = interleaved_xfer (priv, &dict, NULL);
  free (tmpbuf);
  return ret;
}

static int
brick_link (const char *oldpath,
		const char *newpath)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  char *tmpbuf = NULL;
  int ret;

  FUNCTION_CALLED;

  int old_len = strlen (oldpath);
  int new_len = strlen (newpath);

  tmpbuf = (void *) calloc (1, old_len + new_len + 2);
  strcpy (tmpbuf, oldpath);
  strcpy (&tmpbuf[old_len+1], newpath);

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_RENAME));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (old_len + new_len + 2));
  dict_set (&dict, str_to_data (XFER_UID), int_to_data (fuse_get_context ()->uid));
  dict_set (&dict, str_to_data (XFER_GID), int_to_data (fuse_get_context ()->gid));
  dict_set (&dict, str_to_data (XFER_LEN), int_to_data (old_len + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)tmpbuf, old_len + new_len + 2));

  ret = interleaved_xfer (priv, &dict, NULL);
  free (tmpbuf);
  return ret;

}

static int
brick_chmod (const char *path,
		 mode_t mode)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_CHMOD));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_MODE), int_to_data (mode));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_chown (const char *path,
		 uid_t uid,
		 gid_t gid)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_CHOWN));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_UID), int_to_data (fuse_get_context ()->uid));
  dict_set (&dict, str_to_data (XFER_GID), int_to_data (fuse_get_context ()->gid));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_truncate (const char *path,
		    off_t offset)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_TRUNCATE));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_OFFSET), int_to_data (offset));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_utime (const char *path,
		 struct utimbuf *buf)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_UTIME));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_ACTIME), int_to_data (buf->actime));
  dict_set (&dict, str_to_data (XFER_MODTIME), int_to_data (buf->modtime));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_open (const char *path,
		struct fuse_file_info *info)
{
  int ret;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict_t dict = {0,};

  FUNCTION_CALLED;
  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_OPEN));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_FLAGS), int_to_data (info->flags));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  ret = interleaved_xfer (priv, &dict, NULL);
  if (ret >= 0)
    info->fh = ret;
  return 0;
}

static int
brick_read (const char *path,
		char *buf,
		size_t size,
		off_t offset,
		struct fuse_file_info *info)
{
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict_t dict = {0,};

  //  FUNCTION_CALLED;
  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_READ));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (&dict, str_to_data (XFER_OFFSET), int_to_data (offset));
  dict_set (&dict, str_to_data (XFER_FD), int_to_data (info->fh));
  dict_set (&dict, str_to_data (XFER_LEN), int_to_data (size));

  return interleaved_xfer (priv, &dict, (void *)buf);
}

static int
brick_write (const char *path,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 struct fuse_file_info *info)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_WRITE));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (size));
  dict_set (&dict, str_to_data (XFER_OFFSET), int_to_data (offset));
  dict_set (&dict, str_to_data (XFER_FD), int_to_data (info->fh));
  dict_set (&dict, str_to_data (XFER_LEN), int_to_data (size));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)buf, size));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_statfs (const char *path,
		  struct statvfs *buf)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_STATFS));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  return interleaved_xfer (priv, &dict, (void *)buf);
}

static int
brick_flush (const char *path,
		 struct fuse_file_info *info)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_FLUSH));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (&dict, str_to_data (XFER_FD), int_to_data (info->fh));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_release (const char *path,
		   struct fuse_file_info *info)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_RELEASE));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (&dict, str_to_data (XFER_FD), int_to_data (info->fh));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_fsync (const char *path,
		 int datasync,
		 struct fuse_file_info *info)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_FSYNC));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (&dict, str_to_data (XFER_FD), int_to_data (info->fh));
  dict_set (&dict, str_to_data (XFER_FLAGS), int_to_data (datasync));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_setxattr (const char *path,
		    const char *name,
		    const char *value,
		    size_t size,
		    int flags)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_getxattr (const char *path,
		    const char *name,
		    char *value,
		    size_t size)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_listxattr (const char *path,
		     char *list,
		     size_t size)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}
		     
static int
brick_removexattr (const char *path,
		       const char *name)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_opendir (const char *path,
		   struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_readdir (const char *path,
		   void *buf,
		   fuse_fill_dir_t fill,
		   off_t offset,
		   struct fuse_file_info *info)
{
  struct dirent *dir;
  dict_t dict = {0,};
  dict_t *dictp;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  FILE *fp;
  int size;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_READDIR));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) + 1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  /* */
  int ret = 0;
  struct wait_queue *mine = (void *) calloc (1, sizeof (*mine));

  pthread_mutex_init (&mine->mutex, NULL);
  pthread_mutex_lock (&mine->mutex);

  pthread_mutex_lock (&priv->mutex);
  mine->next = priv->queue;
  priv->queue = mine;
  /* */

  fp = fdopen (priv->sock, "a+");
  dict_dump (fp, &dict);
  fflush (fp);

  /* */
  pthread_mutex_unlock (&priv->mutex);

  if (mine->next)
    pthread_mutex_lock (&mine->next->mutex);
  /* */

  dictp = dict_load (fp);
  fflush (fp);
  if (!dictp)
    goto read_err;

  size = data_to_int (dict_get (dictp, str_to_data (XFER_SIZE)));
  dir = (void *) calloc (1, size + 1);
  
  memcpy (dir, data_to_bin (dict_get (dictp, str_to_data (XFER_DATA))), size);
  {
    int i = 0;
    while (i < (size / sizeof (struct dirent))) {
      fill (buf, strdup (dir[i].d_name), NULL, 0);
      i++;
    }
  }
  free (dir);

  gprintf ("%s: successfully returning\n", __FUNCTION__);
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
brick_releasedir (const char *path,
		      struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_fsyncdir (const char *path,
		    int datasync,
		    struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static void *
brick_init (void)
{
  FUNCTION_CALLED;
  struct glusterfs_private *_private = (void *) calloc (1, sizeof (*_private));
  _private->addr = inet_addr ("192.168.0.113");
  _private->port = htons (5252);
  _private->sock = -1;
  try_connect (_private);
  return (void *)_private;
}

static void
brick_destroy (void *data)
{
  struct glusterfs_private *priv = data;

  if (priv->sock != -1)
    close (priv->sock);
  free (priv);
  return;
}

static int
brick_access (const char *path,
		  int mode)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_create (const char *path,
		  mode_t mode,
		  struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
brick_ftruncate (const char *path,
		     off_t offset,
		     struct fuse_file_info *info)
{
  dict_t dict = {0,};
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_FTRUNCATE));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (&dict, str_to_data (XFER_FD), int_to_data (info->fh));
  dict_set (&dict, str_to_data (XFER_OFFSET), int_to_data (offset));

  return interleaved_xfer (priv, &dict, NULL);
}

static int
brick_fgetattr (const char *path,
		    struct stat *buf,
		    struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static struct xlator_fops brick_fops = {
  .getattr     = glusterfs_getattr,
  .readlink    = glusterfs_readlink,
  .getdir      = NULL /*glusterfs_getdir */,
  .mknod       = glusterfs_mknod,
  .mkdir       = glusterfs_mkdir,
  .unlink      = glusterfs_unlink,
  .rmdir       = glusterfs_rmdir,
  .symlink     = glusterfs_symlink,
  .rename      = glusterfs_rename,
  .link        = glusterfs_link,
  .chmod       = glusterfs_chmod,
  .chown       = glusterfs_chown,
  .truncate    = glusterfs_truncate,
  .utime       = glusterfs_utime,
  .open        = glusterfs_open,
  .read        = glusterfs_read,
  .write       = glusterfs_write,
  .statfs      = glusterfs_statfs,
  .flush       = glusterfs_flush,
  .release     = glusterfs_release,
  .fsync       = glusterfs_fsync,
  .setxattr    = glusterfs_setxattr,
  .getxattr    = glusterfs_getxattr,
  .listxattr   = glusterfs_listxattr,
  .removexattr = glusterfs_removexattr,
  .opendir     = glusterfs_opendir,
  .readdir     = glusterfs_readdir,
  .releasedir  = glusterfs_releasedir,
  .fsyncdir    = glusterfs_fsyncdir,
  .init        = glusterfs_init,
  .destroy     = glusterfs_destroy,
  .access      = glusterfs_access,
  .create      = NULL /*glusterfs_create */,
  .ftruncate   = glusterfs_ftruncate,
  .fgetattr    = glusterfs_fgetattr
};

int
brick_fops_register (int argc, char *argv[])
{
  return fuse_main (argc, argv, &glusterfs_fops);
}
