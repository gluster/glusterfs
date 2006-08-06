
#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"

const char *specfile;
struct xlator *specfile_tree;
const char *mount_options;

static int
glusterfs_getattr (const char *path,
		   struct stat *stbuf)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->getattr (xlator, path, stbuf);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_readlink (const char *path,
		    char *dest,
		    size_t size)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->readlink (xlator, path, dest, size);
  if (ret < 0)
    ret = -errno;
  else {
    dest[ret] = '\0';
    ret = 0;
  }
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
  if (ret < 0)
    ret = -errno;
  return ret;
}
*/

static int
glusterfs_mknod (const char *path,
		 mode_t mode,
		 dev_t dev)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->mknod (xlator, path, mode, dev, 
				 fuse_get_context ()->uid, 
				 fuse_get_context ()->gid);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_mkdir (const char *path,
		 mode_t mode)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->mkdir (xlator, path, mode,
				 fuse_get_context ()->uid,
				 fuse_get_context ()->gid);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_unlink (const char *path)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->unlink (xlator, path);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_rmdir (const char *path)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->rmdir (xlator, path);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_symlink (const char *oldpath,
		   const char *newpath)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->symlink (xlator, oldpath, newpath,
				   fuse_get_context ()->uid,
				   fuse_get_context ()->gid);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_rename (const char *oldpath,
		  const char *newpath)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->rename (xlator, oldpath, newpath,
				  fuse_get_context ()->uid,
				  fuse_get_context ()->gid);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_link (const char *oldpath,
		const char *newpath)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->link (xlator, oldpath, newpath,
				fuse_get_context ()->uid,
				fuse_get_context ()->gid);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_chmod (const char *path,
		 mode_t mode)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->chmod (xlator, path, mode);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_chown (const char *path,
		 uid_t uid,
		 gid_t gid)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->chown (xlator, path, uid, gid);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_truncate (const char *path,
		    off_t offset)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->truncate (xlator, path, offset);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_utime (const char *path,
		 struct utimbuf *buf)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->utime (xlator, path, buf);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_open (const char *path,
		struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  struct file_context *ctx = (void *) calloc (1, sizeof (*ctx));
  int ret = xlator->fops->open (xlator, path, info->flags, 0, ctx);

  if (ret < 0) {
    free (ctx);
    ret = -errno;
  } else {
    info->fh = ctx;
  }

  return ret;
}

static int
glusterfs_read (const char *path,
		char *buf,
		size_t size,
		off_t offset,
		struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->read (xlator, path, buf, size, offset, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_write (const char *path,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->write (xlator, path, buf, size, offset, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_statfs (const char *path,
		  struct statvfs *buf)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->statfs (xlator, path, buf);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_flush (const char *path,
		 struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->flush (xlator, path, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_release (const char *path,
		   struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->release (xlator, path, (void *) info->fh);

  free ((void *)info->fh);
  info->fh = 0;
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_fsync (const char *path,
		 int datasync,
		 struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->fsync (xlator, path, datasync, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_setxattr (const char *path,
		    const char *name,
		    const char *value,
		    size_t size,
		    int flags)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->setxattr (xlator, path, name, value, size, flags);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_getxattr (const char *path,
		    const char *name,
		    char *value,
		    size_t size)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->getxattr (xlator, path, name, value, size);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_listxattr (const char *path,
		     char *list,
		     size_t size)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->listxattr (xlator, path, list, size);
  if (ret < 0)
    ret = -errno;
  return ret;
}
		     
static int
glusterfs_removexattr (const char *path,
		       const char *name)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->removexattr (xlator, path, name);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_opendir (const char *path,
		   struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->opendir (xlator, path, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_releasedir (const char *path,
		      struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->releasedir (xlator, path, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_fsyncdir (const char *path,
		    int datasync,
		    struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->fsyncdir (xlator, path, datasync, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_access (const char *path,
		  int mode)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->access (xlator, path, mode);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_create (const char *path,
		  mode_t mode,
		  struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  struct file_context *cxt = (void *) calloc (1, sizeof (*cxt));
  int ret = xlator->fops->open (xlator, path, info->flags | O_CREAT, mode, cxt);

  if (ret < 0) {
    free (cxt);
    ret = -errno;
  } else {
    info->fh = cxt;
  }

  return ret;
}

static int
glusterfs_ftruncate (const char *path,
		     off_t offset,
		     struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->ftruncate (xlator, path, offset, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_fgetattr (const char *path,
		    struct stat *buf,
		    struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  int ret = xlator->fops->fgetattr (xlator, path, buf, (void *)info->fh);
  if (ret < 0)
    ret = -errno;
  return ret;
}

static int
glusterfs_readdir (const char *path,
		   void *buf,
		   fuse_fill_dir_t fill,
		   off_t offset,
		   struct fuse_file_info *info)
{
  struct xlator *xlator = fuse_get_context ()->private_data;
  char *ret = xlator->fops->readdir (xlator, path, offset);

  char *ret_orig = ret;
  {
    int i = 0; 
    char *tmp;
    char *tmp_lock;

    tmp = strtok_r (ret, "/", &tmp_lock);

    while (tmp) {
      fill (buf, tmp, NULL, 0);
      tmp = strtok_r (NULL, "/", &tmp_lock);
    }
  }
  free (ret_orig);
  return 0;
}

static void *
glusterfs_init (void)
{
  return specfile_tree;
}

static void
glusterfs_destroy (void *data)
{
  struct xlator *xlator = data;

  free (xlator);
  return;
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
  .create      = glusterfs_create,
  .ftruncate   = glusterfs_ftruncate,
  .fgetattr    = glusterfs_fgetattr
};

int
glusterfs_mount (char *spec, char *mount_point, char *options)
{
  char *argv[] = {
    "glusterfs",
    "-o", "default_permissions",
    "-o", "allow_other",
    "-o", "nonempty",
    "-o", "hard_remove",
    "-f",
    mount_point,
    NULL };
  specfile = spec;

  FILE *conf = fopen (specfile, "r");

  if (!conf) {
    perror ("open()");
    exit (1);
  }
  gluster_log ("glusterfs-fuse", LOG_NORMAL, "loading spec from %s", specfile);
  specfile_tree = file_to_xlator_tree (conf);
  struct xlator *trav = specfile_tree;

  if (specfile_tree == NULL) {
    gluster_log ("glusterfs-fuse", LOG_CRITICAL, "specification file parsing failed, exiting");
    exit (-1);
  }

  while (trav) {
    if (trav->init)
      if (trav->init (trav) != 0) {
	struct xlator *node = specfile_tree;
	while (node != trav) {
	  node->fini (node);
	  node = node->next;
	}
	gluster_log ("glusterfs-fuse", LOG_CRITICAL, "%s xlator initialization failed\n", trav->name);
	exit (1);
      }
    trav = trav->next;
  }

  while (specfile_tree->parent)
    specfile_tree = specfile_tree->parent;

  fclose (conf);

  return fuse_main ((sizeof (argv) / sizeof (char *)) - 1, argv, &glusterfs_fops);
}
