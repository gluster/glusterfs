
#include "glusterfs.h"
#include "sample.h"
#include "dict.h"
#include "xlator.h"

static int
sample_getattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf)
{
  struct sample_private *priv = xl->private;
  int ret = 0;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->getattr (trav_xl, path, stbuf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_readlink (struct xlator *xl,
		 const char *path,
		 char *dest,
		 size_t size)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->readlink (trav_xl, path, dest, size);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_mknod (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->mknod (trav_xl, path, mode, dev, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_mkdir (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->mkdir (trav_xl, path, mode, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_unlink (struct xlator *xl,
	       const char *path)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->unlink (trav_xl, path);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_rmdir (struct xlator *xl,
	      const char *path)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->rmdir (trav_xl, path);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}



static int
sample_symlink (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->symlink (trav_xl, oldpath, newpath, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_rename (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->rename (trav_xl, oldpath, newpath, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_link (struct xlator *xl,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->link (trav_xl, oldpath, newpath, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_chmod (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->chmod (trav_xl, path, mode);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_chown (struct xlator *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->chown (trav_xl, path, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_truncate (struct xlator *xl,
		 const char *path,
		 off_t offset)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->truncate (trav_xl, path, offset);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_utime (struct xlator *xl,
	      const char *path,
	      struct utimbuf *buf)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->utime (trav_xl, path, buf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_open (struct xlator *xl,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *sample_ctx = calloc (1, sizeof (struct file_context));
  sample_ctx->volume = xl;
  sample_ctx->next = ctx->next;
  ctx->next = sample_ctx;

  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->open (trav_xl, path, flags, mode, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_read (struct xlator *xl,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->read (trav_xl, path, buf, size, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_write (struct xlator *xl,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->write (trav_xl, path, buf, size, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_statfs (struct xlator *xl,
	       const char *path,
	       struct statvfs *buf)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->statfs (trav_xl, path, buf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_flush (struct xlator *xl,
	      const char *path,
	      struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->flush (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_release (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->release (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  RM_MY_CTX (ctx, tmp);
  free (tmp);

  return ret;
}

static int
sample_fsync (struct xlator *xl,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->fsync (trav_xl, path, datasync, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;
 
  return ret;
}

static int
sample_setxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->setxattr (trav_xl, path, name, value, size, flags);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_getxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->getxattr (trav_xl, path, name, value, size);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_listxattr (struct xlator *xl,
		  const char *path,
		  char *list,
		  size_t size)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->listxattr (trav_xl, path, list, size);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}
		     
static int
sample_removexattr (struct xlator *xl,
		    const char *path,
		    const char *name)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->removexattr (trav_xl, path, name);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_opendir (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->opendir (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static char *
sample_readdir (struct xlator *xl,
		const char *path,
		off_t offset)
{
  char *ret = NULL;
  char *buffer = NULL;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->readdir (trav_xl, path, offset);
    trav_xl = trav_xl->next_sibling;
    buffer = ret;
  }

  return buffer;
}

static int
sample_releasedir (struct xlator *xl,
		   const char *path,
		   struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->releasedir (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_fsyncdir (struct xlator *xl,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->fsyncdir (trav_xl, path, datasync, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
sample_access (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->access (trav_xl, path, mode);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_ftruncate (struct xlator *xl,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->ftruncate (trav_xl, path, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_fgetattr (struct xlator *xl,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  
  int ret = 0;
  struct sample_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->fgetattr (trav_xl, path, buf, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
sample_stats (struct xlator_stats *stats)
{
  return 0;
}

int
init (struct xlator *xl)
{
  struct sample_private *_private = calloc (1, sizeof (*_private));
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
  return 0;
}

void
fini (struct xlator *xl)
{
  struct sample_private *priv = xl->private;
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = sample_getattr,
  .readlink    = sample_readlink,
  .mknod       = sample_mknod,
  .mkdir       = sample_mkdir,
  .unlink      = sample_unlink,
  .rmdir       = sample_rmdir,
  .symlink     = sample_symlink,
  .rename      = sample_rename,
  .link        = sample_link,
  .chmod       = sample_chmod,
  .chown       = sample_chown,
  .truncate    = sample_truncate,
  .utime       = sample_utime,
  .open        = sample_open,
  .read        = sample_read,
  .write       = sample_write,
  .statfs      = sample_statfs,
  .flush       = sample_flush,
  .release     = sample_release,
  .fsync       = sample_fsync,
  .setxattr    = sample_setxattr,
  .getxattr    = sample_getxattr,
  .listxattr   = sample_listxattr,
  .removexattr = sample_removexattr,
  .opendir     = sample_opendir,
  .readdir     = sample_readdir,
  .releasedir  = sample_releasedir,
  .fsyncdir    = sample_fsyncdir,
  .access      = sample_access,
  .ftruncate   = sample_ftruncate,
  .fgetattr    = sample_fgetattr,
  .stats       = sample_stats
};
