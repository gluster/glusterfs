
#include "glusterfs.h"
#include "xlator.h"
#include "dict.h"
#include "cement.h"

#if 0 /* You know... following parts are not working */

int
cement_getattr (struct xlator *xl)
{
  return xl->fops.getattr (xl);
}

int
cement_readlink (struct xlator *xl)
{
  return xl->fops.readlink (xl);
}

int
cement_mknod (struct xlator *xl)
{
  return xl->fops.mknod (xl);
}

int
cement_mkdir (struct xlator *xl)
{
  return xl->fops.mkdir (xl);
}

int
cement_unlink (struct xlator *xl)
{
  return xl->fops.unlink (xl);
}

int
cement_rmdir (struct xlator *xl)
{
  return xl->fops.rmdir (xl);
}

int
cement_symlink (struct xlator *xl)
{
  return xl->fops.symlink (xl);
}

int
cement_rename (struct xlator *xl)
{
  return xl->fops.rename (xl);
}

int
cement_link (struct xlator *xl)
{
  return xl->fops.link (xl);
}

int
cement_chmod (struct xlator *xl)
{
  return xl->fops.chmod (xl);
}

int
cement_chown (struct xlator *xl)
{
  return xl->fops.chown (xl);
}

int
cement_truncate (struct xlator *xl)
{
  return xl->fops.truncate (xl);
}

int
cement_utime (struct xlator *xl)
{
  return xl->fops.utime (xl);
}

int
cement_open (struct xlator *xl)
{
  return xl->fops.open (xl);
}

int
cement_read (struct xlator *xl)
{
  return xl->fops.read (xl);
}

int
cement_write (struct xlator *xl)
{
  return xl->fops.write (xl);
}

int
cement_statfs (struct xlator *xl)
{
  return xl->fops.statfs (xl);
}

int
cement_flush (struct xlator *xl)
{
  return xl->fops.flush (xl);
}

int
cement_release (struct xlator *xl)
{
  return xl->fops.release (xl);
}

int
cement_fsync (struct xlator *xl)
{
  return xl->fops.fsync (xl);
}

int
cement_setxattr (struct xlator *xl)
{
  return xl->fops.setxattr (xl);
}

int
cement_getxattr (struct xlator *xl)
{
  return xl->fops.getxattr (xl);
}

int
cement_listxattr (struct xlator *xl)
{
  return xl->fops.listxattr (xl);
}

int
cement_removexattr (struct xlator *xl)
{
  return xl->fops.removexattr (xl);
}

int
cement_opendir (struct xlator *xl)
{
  return xl->fops.opendir (xl);
}

char *
cement_readdir (struct xlator *xl)
{
  return xl->fops.readdir (xl);
}

int
cement_releasedir (struct xlator *xl)
{
  return xl->fops.releasedir (xl);
}

int
cement_fsyncdir (struct xlator *xl)
{
  return xl->fops.fsyncdir (xl);
}

int
cement_access (struct xlator *xl)
{
  return xl->fops.access (xl);
}

int
cement_create (struct xlator *xl)
{
  return xl->fops.create (xl);
}

int
cement_ftruncate (struct xlator *xl)
{
  return xl->fops.ftruncate (xl);
}

int
cement_fgetattr (struct xlator *xl)
{
  return xl->fops.fgetattr (xl);
}

void
init (struct xlator *xl)
{
  struct cement_private *_private = calloc (1, sizeof (struct cement_private));
  xl->private = (void *)_private;
}

void
fini (struct xlator *xl)
{
  free (xl->private);
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
  .create      = cement_create,
  .ftruncate   = cement_ftruncate,
  .fgetattr    = cement_fgetattr
};

#endif /* You know.. */
