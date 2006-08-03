
#include "glusterfs.h"
#include "filter.h"
#include "dict.h"
#include "xlator.h"

/*
  This filter currently only makes its child read-only.
  In the future it'll be extended to handle other types of filtering
  (filtering certain file types, for example)
*/

static int
filter_getattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf)
{
  struct filter_private *priv = xl->private;
  int ret = 0;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->getattr (trav_xl, path, stbuf);

  return ret;
}


static int
filter_readlink (struct xlator *xl,
		 const char *path,
		 char *dest,
		 size_t size)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->readlink (trav_xl, path, dest, size);
  return ret;
}


static int
filter_mknod (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  errno = EROFS;
  return -1;
}

static int
filter_mkdir (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  errno = EROFS;
  return -1;
}


static int
filter_unlink (struct xlator *xl,
	       const char *path)
{
  errno = EROFS;
  return -1;
}


static int
filter_rmdir (struct xlator *xl,
	      const char *path)
{
  errno = EROFS;
  return -1;
}

static int
filter_symlink (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  errno = EROFS;
  return -1;
}

static int
filter_rename (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  errno = EROFS;
  return -1;
}

static int
filter_link (struct xlator *xl,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  errno = EROFS;
  return -1;
}


static int
filter_chmod (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  errno = EROFS;
  return -1;
}


static int
filter_chown (struct xlator *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  errno = EROFS;
  return -1;
}


static int
filter_truncate (struct xlator *xl,
		 const char *path,
		 off_t offset)
{
  errno = EROFS;
  return -1;
}


static int
filter_utime (struct xlator *xl,
	      const char *path,
	      struct utimbuf *buf)
{
  errno = EROFS;
  return -1;
}


static int
filter_open (struct xlator *xl,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *filter_ctx = calloc (1, sizeof (struct file_context));
  filter_ctx->volume = xl;
  filter_ctx->next = ctx->next;
  ctx->next = filter_ctx;

  if ((flags & O_WRONLY) || (flags & O_RDWR)) {
    errno = EROFS;
    return -1;
  }
  
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->open (trav_xl, path, flags, mode, ctx);
  return ret;
}

static int
filter_read (struct xlator *xl,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    printf ("tmp = null\n");
    return -1;
  }
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->read (trav_xl, path, buf, size, offset, ctx);
  return ret;
}

static int
filter_write (struct xlator *xl,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  errno = EROFS;
  return -1;
}

static int
filter_statfs (struct xlator *xl,
	       const char *path,
	       struct statvfs *buf)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->statfs (trav_xl, path, buf);
  return ret;
}

static int
filter_flush (struct xlator *xl,
	      const char *path,
	      struct file_context *ctx)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->flush (trav_xl, path, ctx);
  return ret;
}

static int
filter_release (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->release (trav_xl, path, ctx);

  RM_MY_CTX (ctx, tmp);
  free (tmp);

  return ret;
}

static int
filter_fsync (struct xlator *xl,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  errno = EROFS;
  return -1;
}

static int
filter_setxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  errno = EROFS;
  return -1;
}

static int
filter_getxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->getxattr (trav_xl, path, name, value, size);
  return ret;
}

static int
filter_listxattr (struct xlator *xl,
		  const char *path,
		  char *list,
		  size_t size)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->listxattr (trav_xl, path, list, size);
  return ret;
}
		     
static int
filter_removexattr (struct xlator *xl,
		    const char *path,
		    const char *name)
{
  errno = EROFS;
  return -1;
}

static int
filter_opendir (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->opendir (trav_xl, path, ctx);
  return ret;
}

static char *
filter_readdir (struct xlator *xl,
		const char *path,
		off_t offset)
{
  char *ret = NULL;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->readdir (trav_xl, path, offset);
  return ret;
}

static int
filter_releasedir (struct xlator *xl,
		   const char *path,
		   struct file_context *ctx)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->releasedir (trav_xl, path, ctx);
  return ret;
}

static int
filter_fsyncdir (struct xlator *xl,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  errno = EROFS;
  return -1;
}


static int
filter_access (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  if (mode & W_OK) {
    errno = EROFS;
    return -1;
  }
    
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->access (trav_xl, path, mode);
  return ret;
}

static int
filter_ftruncate (struct xlator *xl,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  errno = EROFS;
  return -1;
}

static int
filter_fgetattr (struct xlator *xl,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  
  int ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  ret = trav_xl->fops->fgetattr (trav_xl, path, buf, ctx);
  return ret;
}

int
init (struct xlator *xl)
{
  struct filter_private *_private = calloc (1, sizeof (*_private));
  data_t *debug = dict_get (xl->options, str_to_data ("Debug"));
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
    printf ("Debug mode on\n");
  }
  xl->private = (void *)_private;

  if (!xl->first_child) {
    printf ("filter xlator should have exactly one child (0 given)\n");
    return -1;
  }

  if (xl->first_child->next_sibling != NULL) {
    printf ("filter xlator should have exactly one child (more than 1 given)\n");
    return -1;
  }
    
  return 0;
}

void
fini (struct xlator *xl)
{
  struct filter_private *priv = xl->private;
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = filter_getattr,
  .readlink    = filter_readlink,
  .mknod       = filter_mknod,
  .mkdir       = filter_mkdir,
  .unlink      = filter_unlink,
  .rmdir       = filter_rmdir,
  .symlink     = filter_symlink,
  .rename      = filter_rename,
  .link        = filter_link,
  .chmod       = filter_chmod,
  .chown       = filter_chown,
  .truncate    = filter_truncate,
  .utime       = filter_utime,
  .open        = filter_open,
  .read        = filter_read,
  .write       = filter_write,
  .statfs      = filter_statfs,
  .flush       = filter_flush,
  .release     = filter_release,
  .fsync       = filter_fsync,
  .setxattr    = filter_setxattr,
  .getxattr    = filter_getxattr,
  .listxattr   = filter_listxattr,
  .removexattr = filter_removexattr,
  .opendir     = filter_opendir,
  .readdir     = filter_readdir,
  .releasedir  = filter_releasedir,
  .fsyncdir    = filter_fsyncdir,
  .access      = filter_access,
  .ftruncate   = filter_ftruncate,
  .fgetattr    = filter_fgetattr
};
