#include "glusterfs.h"
#include "xlator.h"

typedef struct dummy_private
{
  int debug_flag;
} dummy_private_t;

static int
dummy_getattr (struct xlator *this,
		const char *path,
		struct stat *stbuf)
{
  if (!this || !path || !stbuf)
    return -1;

  if (this->first_child)
    return this->first_child->fops->getattr (this->first_child, path, stbuf);
  
  return 0;
}

static int
dummy_readlink (struct xlator *this,
		 const char *path,
		 char *dest,
		 size_t size)
{
  if (!this || !path || !dest || (size < 1))
    return -1;

  if (this->first_child)
    return this->first_child->fops->readlink (this->first_child, path, dest, size);
  
  return 0;
}

static int
dummy_mknod (struct xlator *this,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->mknod (this->first_child, path, mode, dev, uid, gid);
  
  return 0;
}

static int
dummy_mkdir (struct xlator *this,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->mkdir (this->first_child, path, mode, uid, gid);
  
  return 0;
}

static int
dummy_unlink (struct xlator *this,
	       const char *path)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->unlink (this->first_child, path);
  
  return 0;
}

static int
dummy_rmdir (struct xlator *this,
	      const char *path)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->rmdir (this->first_child, path);
  
  return 0;
}

static int
dummy_symlink (struct xlator *this,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  if (!this || !oldpath || *newpath)
    return -1;

  if (this->first_child)
    return this->first_child->fops->symlink (this->first_child, oldpath, newpath, uid, gid);
  
  return 0;
}

static int
dummy_rename (struct xlator *this,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  if (!this || !oldpath || *newpath)
    return -1;

  if (this->first_child)
    return this->first_child->fops->rename (this->first_child, oldpath, newpath, uid, gid);
  
  return 0;
}

static int
dummy_link (struct xlator *this,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  if (!this || !oldpath || *newpath)
    return -1;
  
  if (this->first_child)
    return this->first_child->fops->link (this->first_child, oldpath, newpath, uid, gid);
  
  return 0;
}

static int
dummy_chmod (struct xlator *this,
	      const char *path,
	      mode_t mode)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->chmod (this->first_child, path, mode);
  
  return 0;
}

static int
dummy_chown (struct xlator *this,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->chown (this->first_child, path, uid, gid);

  return 0;
}

static int
dummy_truncate (struct xlator *this,
		 const char *path,
		 off_t offset)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->truncate (this->first_child, path, offset);
  
  return 0;
}

static int
dummy_utime (struct xlator *this,
	      const char *path,
	      struct utimbuf *buf)
{
  if (!this || !path || !buf)
    return -1;

  if (this->first_child)
    return this->first_child->fops->utime (this->first_child, path, buf);

  return 0;
}

static int
dummy_open (struct xlator *this,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->open (this->first_child, path, flags, mode, ctx);

  return 0;
}

static int
dummy_read (struct xlator *this,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  if (!this || !path || !buf || (size < 1) || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->read (this->first_child, path, buf, size, offset, ctx);
  
  return 0;
}

static int
dummy_write (struct xlator *this,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  if (!this || !path || !buf || (size < 1) || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->write (this->first_child, path, buf, size, offset, ctx);
  
  return 0;
}

static int
dummy_statfs (struct xlator *this,
	       const char *path,
	       struct statvfs *buf)
{
  if (!this || !path || !buf)
    return -1;

  if (this->first_child)
    return this->first_child->fops->statfs (this->first_child, path, buf);

  return 0;
}

static int
dummy_flush (struct xlator *this,
	      const char *path,
	      struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->flush (this->first_child, path, ctx);

  return 0;
}

static int
dummy_release (struct xlator *this,
		const char *path,
		struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->release (this->first_child, path, ctx);
  
  return 0;
}

static int
dummy_fsync (struct xlator *this,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;
  
  if (this->first_child)
    return this->first_child->fops->fsync (this->first_child, path, datasync, ctx);

  return 0;
}

static int
dummy_setxattr (struct xlator *this,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  if (!this || !path || !name || !value || (size < 1))
    return -1;

  if (this->first_child)
    return this->first_child->fops->setxattr (this->first_child, path, name, value, size, flags);
  
  return 0;
}

static int
dummy_getxattr (struct xlator *this,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  if (!this || !path || !name)
    return -1;

  if (this->first_child)
    return this->first_child->fops->getxattr (this->first_child, path, name, value, size);

  return 0;
}

static int
dummy_listxattr (struct xlator *this,
		  const char *path,
		  char *list,
		  size_t size)
{
  if (!this || !path || !list || (size < 1))
    return -1;
  
  if (this->first_child)
    return this->first_child->fops->listxattr (this->first_child, path, list, size);
  
  return 0;
}
		     
static int
dummy_removexattr (struct xlator *this,
		    const char *path,
		    const char *name)
{
  if (!this || !path || !name)
    return -1;

  if (this->first_child)
    return this->first_child->fops->removexattr (this->first_child, path, name);
  
  return 0;
}

static int
dummy_opendir (struct xlator *this,
		const char *path,
		struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->opendir (this->first_child, path, ctx);

  return 0;
}

static char *
dummy_readdir (struct xlator *this,
		const char *path,
		off_t offset)
{
  if (!this || !path)
    return NULL;

  if (this->first_child)
    return this->first_child->fops->readdir (this->first_child, path, offset);

  return NULL;
}

static int
dummy_releasedir (struct xlator *this,
		   const char *path,
		   struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->releasedir (this->first_child, path, ctx);

  return 0;
}

static int
dummy_fsyncdir (struct xlator *this,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->fsyncdir (this->first_child, path, datasync, ctx);
  
  return 0;
}


static int
dummy_access (struct xlator *this,
	       const char *path,
	       mode_t mode)
{
  if (!this || !path)
    return -1;

  if (this->first_child)
    return this->first_child->fops->access (this->first_child, path, mode);
  
  return 0;
}

static int
dummy_ftruncate (struct xlator *this,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->ftruncate (this->first_child, path, offset, ctx);
  
  return 0;
}

static int
dummy_fgetattr (struct xlator *this,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  if (!this || !path || !buf || !ctx)
    return -1;

  if (this->first_child)
    return this->first_child->fops->fgetattr (this->first_child, path, buf, ctx);
  
  return 0;
}

static int
dummy_bulk_getattr (struct xlator *this,
		      const char *path,
		      struct bulk_stat *bstbuf)
{
  if (!this || !path || !bstbuf)
    return -1;

  if (this->first_child)
    return this->first_child->fops->bulk_getattr (this->first_child, path, bstbuf);
  
  return 0;
}

int
init (struct xlator *this)
{
  dummy_private_t private;

  if (!this)
    return -1;

  /* Uncomment the following lines, if your translator does not support more than one sub-volumes */ 
  /*
  if (this->first_child->next_sibling)
    {
      gf_log ("dummy", LOG_CRITICAL, "dummy translator does not support more than one sub-volume");
      exit (-1);
    }
  */

  data_t *debug = dict_get (this->options, "debug");
  if (debug && (strcasecmp (debug->data, "on") == 0))
    private.debug_flag = 1;

  this->private = &private;
  return 0;
}

void
fini (struct xlator *this)
{
  if (!this)
    return;

  /* struct dummy_private *private = this->private; */
  /* README: Free up resources held by *private   */

  /* Free up the dictionary options */
  dict_destroy (this->options);

  return;
}

struct xlator_fops fops = {
  .getattr     = dummy_getattr,
  .readlink    = dummy_readlink,
  .mknod       = dummy_mknod,
  .mkdir       = dummy_mkdir,
  .unlink      = dummy_unlink,
  .rmdir       = dummy_rmdir,
  .symlink     = dummy_symlink,
  .rename      = dummy_rename,
  .link        = dummy_link,
  .chmod       = dummy_chmod,
  .chown       = dummy_chown,
  .truncate    = dummy_truncate,
  .utime       = dummy_utime,
  .open        = dummy_open,
  .read        = dummy_read,
  .write       = dummy_write,
  .statfs      = dummy_statfs,
  .flush       = dummy_flush,
  .release     = dummy_release,
  .fsync       = dummy_fsync,
  .setxattr    = dummy_setxattr,
  .getxattr    = dummy_getxattr,
  .listxattr   = dummy_listxattr,
  .removexattr = dummy_removexattr,
  .opendir     = dummy_opendir,
  .readdir     = dummy_readdir,
  .releasedir  = dummy_releasedir,
  .fsyncdir    = dummy_fsyncdir,
  .access      = dummy_access,
  .ftruncate   = dummy_ftruncate,
  .fgetattr    = dummy_fgetattr,
  .bulk_getattr = dummy_bulk_getattr
};

static int
dummy_stats (struct xlator *this, struct xlator_stats *stats)
{
  if (!this || !stats)
    return -1;
  
  if (this->first_child)
    return (this->first_child->mgmt_ops->stats (this->first_child, stats));
  
  return 0;
}

struct xlator_mgmt_ops mgmt_ops = {
  .stats = dummy_stats
};
