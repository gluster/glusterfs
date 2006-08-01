
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
  int ret = 0;
  FUNCTION_CALLED;

  return ret;
}


static int
posix_readlink (struct xlator *xl,
		const char *path,
		char *dest,
		size_t size)
{
  int ret = 0;
  struct posix_private *priv = xl->private;

  FUNCTION_CALLED;
  return ret;
}


/*
static int
posix_getdir (const char *path,
              fuse_dirh_t dirh,
              fuse_dirfil_t dirfil)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
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
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_mkdir (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_unlink (struct xlator *xl,
	      const char *path)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_rmdir (struct xlator *xl,
	     const char *path)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}



static int
posix_symlink (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_rename (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_link (struct xlator *xl,
	    const char *oldpath,
	    const char *newpath,
	    uid_t uid,
	    gid_t gid)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_chmod (struct xlator *xl,
	     const char *path,
	     mode_t mode)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_chown (struct xlator *xl,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_truncate (struct xlator *xl,
		const char *path,
		off_t offset)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_utime (struct xlator *xl,
	     const char *path,
	     struct utimbuf *buf)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_open (struct xlator *xl,
	    const char *path,
	    int flags,
	    struct file_context *cxt)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_read (struct xlator *xl,
	    const char *path,
	    char *buf,
	    size_t size,
	    off_t offset,
	    struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
posix_write (struct xlator *xl,
	     const char *path,
	     const char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
posix_statfs (struct xlator *xl,
	      const char *path,
	      struct statvfs *buf)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_flush (struct xlator *xl,
	     const char *path,
	     struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
posix_release (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
posix_fsync (struct xlator *xl,
	     const char *path,
	     int datasync,
	     struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
 
  FUNCTION_CALLED;

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
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_getxattr (struct xlator *xl,
		const char *path,
		const char *name,
		char *value,
		size_t size)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_listxattr (struct xlator *xl,
		 const char *path,
		 char *list,
		 size_t size)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;

  return ret;
}
		     
static int
posix_removexattr (struct xlator *xl,
		   const char *path,
		   const char *name)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_opendir (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static char *
posix_readdir (struct xlator *xl,
	       const char *path,
	       off_t offset)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return NULL;
}

static int
posix_releasedir (struct xlator *xl,
		  const char *path,
		  struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_fsyncdir (struct xlator *xl,
		const char *path,
		int datasync,
		struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}


static int
posix_access (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_create (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

static int
posix_ftruncate (struct xlator *xl,
		 const char *path,
		 off_t offset,
		 struct file_context *ctx)
{
  int ret = 0;
  struct posix_private *priv = xl->private;
  struct file_context *tmp;
  FILL_MY_CXT (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }

  FUNCTION_CALLED;

  return ret;
}

static int
posix_fgetattr (struct xlator *xl,
		const char *path,
		struct stat *buf,
		struct file_context *ctx)
{
  
  int ret = 0;
  struct posix_private *priv = xl->private;
  FUNCTION_CALLED;
  return ret;
}

void
init (struct xlator *xl)
{
  FUNCTION_CALLED;
  struct posix_private *_private = calloc (1, sizeof (*_private));
  xl->private = (void *)_private;
  return;
}

void
fini (struct xlator *xl)
{
  struct posix_private *priv = xl->private;
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
  .create      = posix_create,
  .ftruncate   = posix_ftruncate,
  .fgetattr    = posix_fgetattr
};
