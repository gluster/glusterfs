
#include "glusterfs.h"
#include "posix.h"
#include "dict.h"
#include "xlator.h"

static int
posix_getattr (struct xlator *xl,
	       const char *path,
	       struct stat *stbuf)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return lstat (RELATIVE(path), stbuf);
}


static int
posix_readlink (struct xlator *xl,
		const char *path,
		char *dest,
		size_t size)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return readlink (RELATIVE(path), dest, size);
}


/*
static int
posix_getdir (const char *path,
              fuse_dirh_t dirh,
              fuse_dirfil_t dirfil)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return ret;
}
*/

static int
posix_mknod (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     dev_t dev,
	     uid_t uid,
	     gid_t gid)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = mknod (RELATIVE (path), mode, dev);

  if (ret == 0) {
    chown (RELATIVE (path), uid, gid);
  }

  return ret;
}

static int
posix_mkdir (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     uid_t uid,
	     gid_t gid)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = mkdir (RELATIVE (path), mode);

  if (ret == 0) {
    chown (RELATIVE (path), uid, gid);
  }

  return ret;
}


static int
posix_unlink (struct xlator *xl,
	      const char *path)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return unlink (RELATIVE (path));
}


static int
posix_rmdir (struct xlator *xl,
	     const char *path)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return rmdir (RELATIVE (path));
}



static int
posix_symlink (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = symlink (oldpath, RELATIVE (newpath));

  if (ret == 0) {
    lchown (RELATIVE (newpath), uid, gid);
  }
  return ret;
}

static int
posix_rename (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = rename (RELATIVE (oldpath), RELATIVE (newpath));

  if (ret == 0) {
    chown (RELATIVE (newpath), uid, gid);
  }
  return ret;
}

static int
posix_link (struct xlator *xl,
	    const char *oldpath,
	    const char *newpath,
	    uid_t uid,
	    gid_t gid)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = link (RELATIVE (oldpath), RELATIVE (newpath));

  if (ret == 0) {
    chown (RELATIVE (newpath), uid, gid);
  }
  return ret;
}


static int
posix_chmod (struct xlator *xl,
	     const char *path,
	     mode_t mode)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return chmod (RELATIVE (path), mode);
}


static int
posix_chown (struct xlator *xl,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return lchown (RELATIVE (path), uid, gid);
}


static int
posix_truncate (struct xlator *xl,
		const char *path,
		off_t offset)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return truncate (RELATIVE (path), offset);
}


static int
posix_utime (struct xlator *xl,
	     const char *path,
	     struct utimbuf *buf)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return utime (RELATIVE (path), buf);
}


static int
posix_open (struct xlator *xl,
	    const char *path,
	    int flags,
	    mode_t mode,
	    struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *trav = ctx;
  struct file_context *posix_ctx = calloc (1, sizeof (struct file_context));
  int fd = open (RELATIVE (path), flags, mode);

  {
    posix_ctx->volume = xl;
    posix_ctx->next = NULL;
    *(int *)&posix_ctx->context = fd;
    
    if (trav->next)
      posix_ctx->next = trav->next->next;
    
    trav->next = posix_ctx;
  }

  return 0;
}

static int
posix_read (struct xlator *xl,
	    const char *path,
	    char *buf,
	    size_t size,
	    off_t offset,
	    struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int len = 0;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int fd = (int)tmp->context;
  {
    lseek (fd, offset, SEEK_SET);
    len = read(fd, buf, size);
  }
  return len;
}

static int
posix_write (struct xlator *xl,
	     const char *path,
	     const char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int len = 0;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int fd = (int)tmp->context;

  {
    lseek (fd, offset, SEEK_SET);
    len = write (fd, buf, size);
  }

  return len;
}

static int
posix_statfs (struct xlator *xl,
	      const char *path,
	      struct statvfs *buf)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return statvfs (RELATIVE (path), buf);
}

static int
posix_flush (struct xlator *xl,
	     const char *path,
	     struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  //int fd = (int)tmp->context;
  return 0;
}

static int
posix_release (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int fd = (int)tmp->context;

  RM_MY_CTX (ctx, tmp);
  free (tmp);
  return close (fd);
}

static int
posix_fsync (struct xlator *xl,
	     const char *path,
	     int datasync,
	     struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = 0;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int fd = (int)tmp->context; 
 
  if (datasync)
    ret = fdatasync (fd);
  else
    ret = fsync (fd);
  
  return ret;
}

static int
posix_setxattr (struct xlator *xl,
		const char *path,
		const char *name,
		const char *value,
		size_t size,
		int flags)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return lsetxattr (RELATIVE (path), name, value, size, flags);
}

static int
posix_getxattr (struct xlator *xl,
		const char *path,
		const char *name,
		char *value,
		size_t size)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return lgetxattr (RELATIVE (path), name, value, size);
}

static int
posix_listxattr (struct xlator *xl,
		 const char *path,
		 char *list,
		 size_t size)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return llistxattr (RELATIVE (path), list, size);
}
		     
static int
posix_removexattr (struct xlator *xl,
		   const char *path,
		   const char *name)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return lremovexattr (RELATIVE (path), name);
}

static int
posix_opendir (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = 0;
  DIR *dir = opendir (RELATIVE (path));
  if (!dir)
    ret = -1;
  else
    closedir (dir);
  return ret;
}

static char *
posix_readdir (struct xlator *xl,
	       const char *path,
	       off_t offset)
{
  DIR *dir;
  struct dirent *dirent = NULL;
  int length = 0;
  int buf_len = 0;
  char *buf = calloc (1, 4096); // #define the value
  int alloced = 4096;
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  dir = opendir (RELATIVE (path));
  if (!dir)
    return NULL;
  while ((dirent = readdir (dir))) {
    if (!dirent)
      break;
    length += strlen (dirent->d_name) + 1;
    if (length > alloced) {
      alloced = length * 2;
      buf = realloc (buf, alloced);
    }
    memcpy (&buf[buf_len], dirent->d_name, strlen (dirent->d_name) + 1);
    buf_len = length;
    buf[length - 1] = '/';
  }
  buf[length - 1] = '\0';

  closedir (dir);
  return buf;
}

static int
posix_releasedir (struct xlator *xl,
		  const char *path,
		  struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return 0;
}

static int
posix_fsyncdir (struct xlator *xl,
		const char *path,
		int datasync,
		struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return 0;
}


static int
posix_access (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  return access (RELATIVE (path), mode);
}


static int
posix_ftruncate (struct xlator *xl,
		 const char *path,
		 off_t offset,
		 struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int fd = (int)tmp->context;

  return ftruncate (fd, offset);
}

static int
posix_fgetattr (struct xlator *xl,
		const char *path,
		struct stat *buf,
		struct file_context *ctx)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int fd = (int)tmp->context;

  return fstat (fd, buf);
}

void
init (struct xlator *xl)
{
  struct posix_private *_private = calloc (1, sizeof (*_private));

  data_t *directory = dict_get (xl->options, str_to_data ("Directory"));
  data_t *debug = dict_get (xl->options, str_to_data ("Debug"));

  chdir (directory->data);
  if (debug) {
    if (strcasecmp (debug->data, "on") == 0)
      _private->is_debug = 1;
    else
      _private->is_debug = 0;
  } else {
    _private->is_debug = 0;
  }

  if (_private->is_debug) {
    FUNCTION_CALLED;
    printf ("Directory-%s\n", directory->data);
    printf ("Debug mode on\n");
  }

  xl->private = (void *)_private;
  return;
}

void
fini (struct xlator *xl)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = posix_getattr,
  .readlink    = posix_readlink,
  .mknod       = posix_mknod,
  .mkdir       = posix_mkdir,
  .unlink      = posix_unlink,
  .rmdir       = posix_rmdir,
  .symlink     = posix_symlink,
  .rename      = posix_rename,
  .link        = posix_link,
  .chmod       = posix_chmod,
  .chown       = posix_chown,
  .truncate    = posix_truncate,
  .utime       = posix_utime,
  .open        = posix_open,
  .read        = posix_read,
  .write       = posix_write,
  .statfs      = posix_statfs,
  .flush       = posix_flush,
  .release     = posix_release,
  .fsync       = posix_fsync,
  .setxattr    = posix_setxattr,
  .getxattr    = posix_getxattr,
  .listxattr   = posix_listxattr,
  .removexattr = posix_removexattr,
  .opendir     = posix_opendir,
  .readdir     = posix_readdir,
  .releasedir  = posix_releasedir,
  .fsyncdir    = posix_fsyncdir,
  .access      = posix_access,
  .ftruncate   = posix_ftruncate,
  .fgetattr    = posix_fgetattr
};
