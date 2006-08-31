#include "glusterfs.h"
#include "xlator.h"

typedef struct trace_private
{
  int debug_flag;
  int trace_flag;
} trace_private_t;

static int
trace_getattr (struct xlator *this,
		const char *path,
		struct stat *stbuf)
{
  if (!this || !path || !stbuf)
    return -1;
  
  return this->first_child->fops->getattr (this->first_child, path, stbuf);
}

static int
trace_readlink (struct xlator *this,
		 const char *path,
		 char *dest,
		 size_t size)
{
  if (!this || !path || !dest || (size < 1))
    return -1;

  return this->first_child->fops->readlink (this->first_child, path, dest, size);
}

static int
trace_mknod (struct xlator *this,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->mknod (this->first_child, path, mode, dev, uid, gid);
}

static int
trace_mkdir (struct xlator *this,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->mkdir (this->first_child, path, mode, uid, gid);
}

static int
trace_unlink (struct xlator *this,
	       const char *path)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->unlink (this->first_child, path);
}

static int
trace_rmdir (struct xlator *this,
	      const char *path)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->rmdir (this->first_child, path);
}

static int
trace_symlink (struct xlator *this,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  if (!this || !oldpath || *newpath)
    return -1;

  return this->first_child->fops->symlink (this->first_child, oldpath, newpath, uid, gid);
}

static int
trace_rename (struct xlator *this,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  if (!this || !oldpath || *newpath)
    return -1;

  return this->first_child->fops->rename (this->first_child, oldpath, newpath, uid, gid);
}

static int
trace_link (struct xlator *this,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  if (!this || !oldpath || *newpath)
    return -1;

  return this->first_child->fops->link (this->first_child, oldpath, newpath, uid, gid);
}

static int
trace_chmod (struct xlator *this,
	      const char *path,
	      mode_t mode)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->chmod (this->first_child, path, mode);
}

static int
trace_chown (struct xlator *this,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->chown (this->first_child, path, uid, gid);
}

static int
trace_truncate (struct xlator *this,
		 const char *path,
		 off_t offset)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->truncate (this->first_child, path, offset);
}

static int
trace_utime (struct xlator *this,
	      const char *path,
	      struct utimbuf *buf)
{
  if (!this || !path || !buf)
    return -1;

  return this->first_child->fops->utime (this->first_child, path, buf);
}

static int
trace_open (struct xlator *this,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->open (this->first_child, path, flags, mode, ctx);
}

static int
trace_read (struct xlator *this,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  if (!this || !path || !buf || (size < 1) || !ctx)
    return -1;

  return this->first_child->fops->read (this->first_child, path, buf, size, offset, ctx);
}

static int
trace_write (struct xlator *this,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  if (!this || !path || !buf || (size < 1) || !ctx)
    return -1;

  return this->first_child->fops->write (this->first_child, path, buf, size, offset, ctx);
}

static int
trace_statfs (struct xlator *this,
	       const char *path,
	       struct statvfs *buf)
{
  if (!this || !path || !buf)
    return -1;

  return this->first_child->fops->statfs (this->first_child, path, buf);
}

static int
trace_flush (struct xlator *this,
	      const char *path,
	      struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->flush (this->first_child, path, ctx);
}

static int
trace_release (struct xlator *this,
		const char *path,
		struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->release (this->first_child, path, ctx);
}

static int
trace_fsync (struct xlator *this,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->fsync (this->first_child, path, datasync, ctx);
}

static int
trace_setxattr (struct xlator *this,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  if (!this || !path || !name || !value || (size < 1))
    return -1;

  return this->first_child->fops->setxattr (this->first_child, path, name, value, size, flags);
}

static int
trace_getxattr (struct xlator *this,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  if (!this || !path || !name)
    return -1;

  return this->first_child->fops->getxattr (this->first_child, path, name, value, size);
}

static int
trace_listxattr (struct xlator *this,
		  const char *path,
		  char *list,
		  size_t size)
{
  if (!this || !path || !list || (size < 1))
    return -1;

  return this->first_child->fops->listxattr (this->first_child, path, list, size);
}
		     
static int
trace_removexattr (struct xlator *this,
		    const char *path,
		    const char *name)
{
  if (!this || !path || !name)
    return -1;

  return this->first_child->fops->removexattr (this->first_child, path, name);
}

static int
trace_opendir (struct xlator *this,
		const char *path,
		struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->opendir (this->first_child, path, ctx);
}

static char *
trace_readdir (struct xlator *this,
		const char *path,
		off_t offset)
{
  if (!this || !path)
    return NULL;

  return this->first_child->fops->readdir (this->first_child, path, offset);
}

static int
trace_releasedir (struct xlator *this,
		   const char *path,
		   struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->releasedir (this->first_child, path, ctx);
}

static int
trace_fsyncdir (struct xlator *this,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->fsyncdir (this->first_child, path, datasync, ctx);
}


static int
trace_access (struct xlator *this,
	       const char *path,
	       mode_t mode)
{
  if (!this || !path)
    return -1;

  return this->first_child->fops->access (this->first_child, path, mode);
}

static int
trace_ftruncate (struct xlator *this,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  if (!this || !path || !ctx)
    return -1;

  return this->first_child->fops->ftruncate (this->first_child, path, offset, ctx);
}

static int
trace_fgetattr (struct xlator *this,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  if (!this || !path || !buf || !ctx)
    return -1;

  return this->first_child->fops->fgetattr (this->first_child, path, buf, ctx);
}

static int
trace_bulk_getattr (struct xlator *this,
		      const char *path,
		      struct bulk_stat *bstbuf)
{
  if (!this || !path || !bstbuf)
    return -1;

  return this->first_child->fops->bulk_getattr (this->first_child, path, bstbuf);
}

int
init (struct xlator *this)
{
  trace_private_t private;

  if (!this)
    return -1;

  if (this->first_child->next_sibling)
    {
      gf_log ("trace", LOG_CRITICAL, "trace translator does not support more than one sub-volume");
      exit (-1);
    }
    
  data_t *debug = dict_get (this->options, "debug");
  if (debug && (strcasecmp (debug->data, "on") == 0))
    private.debug_flag = 1;

  data_t *trace_file = dict_get (this->options, "trace-file");
  if (trace_file && (gf_log_init (trace_file->data) == 0))
    {
      gf_log_init (trace_file->data);
      gf_log_set_loglevel (LOG_DEBUG);   
      gf_log ("trace", LOG_DEBUG, "trace translator loaded");      
      private.trace_flag = 1;
    }

  this->private = &private;
  return 0;
}

void
fini (struct xlator *this)
{
  if (!this)
    return;

  /* struct trace_private *private = this->private; */
  /* README: Free up resources held by *private   */

  /* Free up the dictionary options */
  dict_destroy (this->first_child->options);

  return;
}

struct xlator_fops fops = {
  .getattr     = trace_getattr,
  .readlink    = trace_readlink,
  .mknod       = trace_mknod,
  .mkdir       = trace_mkdir,
  .unlink      = trace_unlink,
  .rmdir       = trace_rmdir,
  .symlink     = trace_symlink,
  .rename      = trace_rename,
  .link        = trace_link,
  .chmod       = trace_chmod,
  .chown       = trace_chown,
  .truncate    = trace_truncate,
  .utime       = trace_utime,
  .open        = trace_open,
  .read        = trace_read,
  .write       = trace_write,
  .statfs      = trace_statfs,
  .flush       = trace_flush,
  .release     = trace_release,
  .fsync       = trace_fsync,
  .setxattr    = trace_setxattr,
  .getxattr    = trace_getxattr,
  .listxattr   = trace_listxattr,
  .removexattr = trace_removexattr,
  .opendir     = trace_opendir,
  .readdir     = trace_readdir,
  .releasedir  = trace_releasedir,
  .fsyncdir    = trace_fsyncdir,
  .access      = trace_access,
  .ftruncate   = trace_ftruncate,
  .fgetattr    = trace_fgetattr,
  .bulk_getattr = trace_bulk_getattr
};

static int
trace_stats (struct xlator *this, struct xlator_stats *stats)
{
  if (!this || !stats)
    return -1;

  return (this->first_child->mgmt_ops->stats (this->first_child, stats));
}

struct xlator_mgmt mgmt_ops = {
  .stats = trace_stats
};
