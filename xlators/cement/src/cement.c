
#include "glusterfs.h"
#include "cement.h"
#include "dict.h"
#include "xlator.h"

static int
cement_getattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf)
{
  struct cement_private *priv = xl->private;
  int ret = 0;
  FUNCTION_CALLED;

  return ret;
}


static int
cement_readlink (struct xlator *xl,
		 const char *path,
		 char *dest,
		 size_t size)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


/*
static int
cement_getdir (const char *path,
               fuse_dirh_t dirh,
	       fuse_dirfil_t dirfil)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;

  return ret;
}
*/

static int
cement_mknod (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}

static int
cement_mkdir (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_unlink (struct xlator *xl,
	       const char *path)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_rmdir (struct xlator *xl,
	      const char *path)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}



static int
cement_symlink (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}

static int
cement_rename (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_link (struct xlator *xl,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_chmod (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_chown (struct xlator *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_truncate (struct xlator *xl,
		 const char *path,
		 off_t offset)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_utime (struct xlator *xl,
	      const char *path,
	      struct utimbuf *buf)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}


static int
cement_open (struct xlator *xl,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *cxt)
{
  int ret = 0;
  struct cement_private *priv = xl->private;

  FUNCTION_CALLED;

  return ret;
}

static int
cement_read (struct xlator *xl,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
cement_write (struct xlator *xl,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
cement_statfs (struct xlator *xl,
	       const char *path,
	       struct statvfs *buf)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_flush (struct xlator *xl,
	      const char *path,
	      struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
cement_release (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
cement_fsync (struct xlator *xl,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
 
  FUNCTION_CALLED;

  return ret;
}

static int
cement_setxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_getxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_listxattr (struct xlator *xl,
		  const char *path,
		  char *list,
		  size_t size)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;

  return ret;
}
		     
static int
cement_removexattr (struct xlator *xl,
		    const char *path,
		    const char *name)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_opendir (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static char *
cement_readdir (struct xlator *xl,
		const char *path,
		off_t offset)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return NULL;
}

static int
cement_releasedir (struct xlator *xl,
		   const char *path,
		   struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_fsyncdir (struct xlator *xl,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
cement_access (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
cement_ftruncate (struct xlator *xl,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  int fd;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
 
  fd = (int)tmp->context;

  FUNCTION_CALLED;

  return ret;
}

static int
cement_fgetattr (struct xlator *xl,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  
  int ret = 0;
  struct cement_private *priv = xl->private;
  FUNCTION_CALLED;

  return ret;
}

void
init (struct xlator *xl)
{
  FUNCTION_CALLED;
  struct cement_private *_private = calloc (1, sizeof (*_private));
  xl->private = (void *)_private;
  return;
}

void
fini (struct xlator *xl)
{
  struct cement_private *priv = xl->private;
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = cement_getattr,
  .readlink    = cement_readlink,
  .mknod       = cement_mknod,
  .mkdir       = cement_mkdir,
  .unlink      = cement_unlink,
  .rmdir       = cement_rmdir,
  .symlink     = cement_symlink,
  .rename      = cement_rename,
  .link        = cement_link,
  .chmod       = cement_chmod,
  .chown       = cement_chown,
  .truncate    = cement_truncate,
  .utime       = cement_utime,
  .open        = cement_open,
  .read        = cement_read,
  .write       = cement_write,
  .statfs      = cement_statfs,
  .flush       = cement_flush,
  .release     = cement_release,
  .fsync       = cement_fsync,
  .setxattr    = cement_setxattr,
  .getxattr    = cement_getxattr,
  .listxattr   = cement_listxattr,
  .removexattr = cement_removexattr,
  .opendir     = cement_opendir,
  .readdir     = cement_readdir,
  .releasedir  = cement_releasedir,
  .fsyncdir    = cement_fsyncdir,
  .access      = cement_access,
  .ftruncate   = cement_ftruncate,
  .fgetattr    = cement_fgetattr
};
