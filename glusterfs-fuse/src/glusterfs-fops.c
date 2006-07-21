
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
  pthread_mutex_init (&priv->mutex, NULL);
  return 0;
}

static int
interleaved_xfer (struct glusterfs_private *priv,
		   dict_t *dict,
		   void *recv_buf)
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

  fp = fdopen (priv->sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  pthread_mutex_unlock (&priv->mutex);

  if (mine->next)
      pthread_mutex_lock (&mine->next->mutex);

  fp = fdopen (priv->sock, "a+");
  dict = dict_load (fp);
  
  if (!dict)
    goto read_err;

  size = data_to_int (dict_get (dict, str_to_data (XFER_SIZE)));

  if (recv_buf && size)
    memcpy (recv_buf, 
	    data_to_bin (dict_get (dict, str_to_data (XFER_DATA))), 
	    size);
  ret = data_to_int (dict_get (dict, str_to_data (XFER_REMOTE_RET)));
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
glusterfs_getattr (const char *path,
		   struct stat *stbuf)
{
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict_t dict;
  int size = 0;
  dict.count = 0;
  dict.members = NULL;
  FUNCTION_CALLED;
  
  size = strlen (path) + 1;
  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_GETATTR));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (size));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, size));

  return interleaved_xfer (priv, &dict, (void *)stbuf);
}


static int
glusterfs_readlink (const char *path,
		    char *dest,
		    size_t size)
{
  int ret = 0;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;
  dict_t dict;
  dict.count = 0;
  dict.members = NULL;
  FUNCTION_CALLED;

  dict_set (&dict, str_to_data (XFER_OPERATION), int_to_data (OP_READLINK));
  dict_set (&dict, str_to_data (XFER_LEN), int_to_data (size));
  dict_set (&dict, str_to_data (XFER_SIZE), int_to_data (strlen (path) +1));
  dict_set (&dict, str_to_data (XFER_DATA), bin_to_data ((void *)path, strlen (path) + 1));

  ret = interleaved_xfer (priv, &dict, (void *)dest);
  dest[strlen (path) + 1] = 0;
  if (ret > 0)
    return 0;
  return ret;
}

/*
static int
glusterfs_getdir (const char *path,
		  fuse_dirh_t dirh,
		  fuse_dirfil_t dirfil)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}
*/

static int
glusterfs_mknod (const char *path,
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
glusterfs_mkdir (const char *path,
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
glusterfs_unlink (const char *path)
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
glusterfs_rmdir (const char *path)
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
glusterfs_symlink (const char *oldpath,
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
glusterfs_rename (const char *oldpath,
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
glusterfs_link (const char *oldpath,
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
glusterfs_chmod (const char *path,
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
glusterfs_chown (const char *path,
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
glusterfs_truncate (const char *path,
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
glusterfs_utime (const char *path,
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
glusterfs_open (const char *path,
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
glusterfs_read (const char *path,
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
glusterfs_write (const char *path,
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
glusterfs_statfs (const char *path,
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
glusterfs_flush (const char *path,
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
glusterfs_release (const char *path,
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
glusterfs_fsync (const char *path,
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
glusterfs_setxattr (const char *path,
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
glusterfs_getxattr (const char *path,
		    const char *name,
		    char *value,
		    size_t size)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_listxattr (const char *path,
		     char *list,
		     size_t size)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}
		     
static int
glusterfs_removexattr (const char *path,
		       const char *name)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_opendir (const char *path,
		   struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_readdir (const char *path,
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
glusterfs_releasedir (const char *path,
		      struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_fsyncdir (const char *path,
		    int datasync,
		    struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static void *
glusterfs_init (void)
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
glusterfs_destroy (void *data)
{
  struct glusterfs_private *priv = data;

  if (priv->sock != -1)
    close (priv->sock);
  free (priv);
  return;
}

static int
glusterfs_access (const char *path,
		  int mode)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_create (const char *path,
		  mode_t mode,
		  struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_ftruncate (const char *path,
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
glusterfs_fgetattr (const char *path,
		    struct stat *buf,
		    struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static struct fuse_operations glusterfs_fops = {
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
glusterfs_fops_register (int argc, char *argv[])
{
  return fuse_main (argc, argv, &glusterfs_fops);
}
