#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"

extern int errno;

#define _FORMAT_WARN(domain, log_level, format, args...)  printf ("__DEBUG__" format, ##args);     

typedef struct trace_private
{
  int debug_flag;
} trace_private_t;

static int
trace_getattr (struct xlator *this,
		const char *path,
		struct stat *stbuf)
{
  if (!this || !path || !stbuf)
    return -1;

#if 0
    dev_t     st_dev;     /* ID of device containing file */
    ino_t     st_ino;     /* inode number */
    mode_t    st_mode;    /* protection */
    nlink_t   st_nlink;   /* number of hard links */
    uid_t     st_uid;     /* user ID of owner */
    gid_t     st_gid;     /* group ID of owner */
    dev_t     st_rdev;    /* device ID (if special file) */
    off_t     st_size;    /* total size, in bytes */
    blksize_t st_blksize; /* blocksize for filesystem I/O */
    blkcnt_t  st_blocks;  /* number of blocks allocated */
    time_t    st_atime;   /* time of last access */
    time_t    st_mtime;   /* time of last modification */
    time_t    st_ctime;   /* time of last status change */
#endif 

   gf_log ("trace", LOG_DEBUG, "trace_getattr (*this=%p, path=%s, *stbuf=%p})",
	   this, path, stbuf);

 {
   int ret;
   char atime_buf[256], mtime_buf[256], ctime_buf[256];
   ret = this->first_child->fops->getattr (this->first_child, path, stbuf);

   setlocale (LC_ALL, "");
   strftime (atime_buf, sizeof(atime_buf), nl_langinfo (D_T_FMT), localtime (&stbuf->st_atime));
   strftime (mtime_buf, sizeof(mtime_buf), nl_langinfo (D_T_FMT), localtime (&stbuf->st_mtime));
   strftime (ctime_buf, sizeof(ctime_buf), nl_langinfo (D_T_FMT), localtime (&stbuf->st_ctime));

  gf_log ("trace", LOG_DEBUG, "trace_getattr (*this=%p, path=%s, *stbuf=%p {st_dev=%lld, st_ino=%lld, st_mode=%d, st_nlink=%d, st_uid=%d, st_gid=%d, st_rdev=%llx, st_size=%lld, st_blksize=%ld, st_blocks=%lld, st_atime=%s, st_mtime=%s, st_ctime=%s})  => ret=%d, errno=%d",
	   this, path, stbuf, stbuf->st_dev, stbuf->st_ino, stbuf->st_mode, stbuf->st_nlink, stbuf->st_uid, stbuf->st_gid, stbuf->st_rdev, stbuf->st_size, stbuf->st_blksize, stbuf->st_blocks, atime_buf, mtime_buf, ctime_buf, ret, errno);
   return ret;
 }
}

static int
trace_readlink (struct xlator *this,
		 const char *path,
		 char *dest,
		 size_t size)
{
  if (!this || !path || !dest || (size < 1))
    return -1;

  gf_log ("trace", LOG_DEBUG, "trace_readlink (*this=%p, path=%s, *dest=%p, size=%d)",
	  this, path, dest, size);

  {
    int ret;
    ret = this->first_child->fops->readlink (this->first_child, path, dest, size);

  gf_log ("trace", LOG_DEBUG, "trace_readlink (*this=%p, path=%s, *dest=%p, size=%d) => ret=%d, errno=%d",
	  this, path, dest, size, ret, errno);
 
  return ret;
  }
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

   gf_log ("trace", LOG_DEBUG, "trace_mknod (*this=%p, path=%s, mode=%d, dev=%lld, uid=%d, gid=%d)",
	     this, path, mode, dev, uid, gid);
   {
     int ret;
     ret = this->first_child->fops->mknod (this->first_child, path, mode, dev, uid, gid);
     gf_log ("trace", LOG_DEBUG, "trace_mknod (*this=%p, path=%s, mode=%d, dev=%lld, uid=%d, gid=%d) => ret=%d, errno=%d",
	     this, path, mode, dev, uid, gid, ret, errno);
     
     return ret;
   }
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
  
  gf_log ("trace", LOG_DEBUG, "trace_mkdir (*this=%p, path=%s, mode=%d, uid=%d, gid=%d)",
	  this, path, mode, uid, gid);
  
  {
    int ret;
    ret = this->first_child->fops->mkdir (this->first_child, path, mode, uid, gid);
    gf_log ("trace", LOG_DEBUG, "trace_mkdir (*this=%p, path=%s, mode=%d, uid=%d, gid=%d) => ret=%d, errno=%d",
	    this, path, mode, uid, gid, ret, errno);
    return ret;
  }
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

  /* __FIXME__ should be removed after gowda implements global init log */
  gf_log_init ("/tmp/gluster-trace");


  if (this->first_child->next_sibling)
    {
      gf_log ("trace", LOG_CRITICAL, "trace translator does not support more than one sub-volume");
      exit (-1);
    }
    
  data_t *debug = dict_get (this->options, "debug");
  if (debug && (strcasecmp (debug->data, "on") == 0))
    {
      private.debug_flag = 1;
      gf_log_set_loglevel (LOG_DEBUG);
      gf_log ("trace", LOG_DEBUG, "trace translator loaded");
    }

  gf_log ("trace", LOG_DEBUG, 
	  "init (struct xlator *this=%p {name=%s, *next=%p, *parent=%p, *first_child=%p, *next_sibling=%p, *fops=%p {*open=%p, getattr=%p, *readlink=%p, *mknod=%p, *mkdir=%p, *unlink=%p, *rmdir=%p, *symlink=%p, *rename=%p, *link=%p, *chmod=%p, *chown=%p, *truncate=%p, *utime=%p, *read=%p, *write=%p, *statfs=%p, *flush=%p, *release=%p, *fsync=%p, *setxattr=%p, *getxattr=%p, *listxattr=%p, *removexattr=%p, *opendir=%p, *readdir=%p, *releasedir=%p, *fsyncdir=%p, *access=%p, *ftruncate=%p, *fgetattr=%p, *bulk_getattr=%p}, *mgmt_ops=%p {*stats=%p, *fsck=%p, *lock=%p, *unlock=%p}, *fini()=%p, *init()=%p, *getlayout()=%p, *options=%p, *private=%p)", 
	  this, this->name, this->next, this->parent, this->first_child, this->next_sibling, this->fops, this->fops->open, this->fops->getattr, this->fops->readlink, this->fops->mknod, this->fops->mkdir, this->fops->unlink, this->fops->rmdir, this->fops->symlink, this->fops->rename, this->fops->link, this->fops->chmod, this->fops->chown, this->fops->truncate, this->fops->utime, this->fops->read, this->fops->write, this->fops->statfs, this->fops->flush, this->fops->release, this->fops->fsync, this->fops->setxattr, this->fops->getxattr, this->fops->listxattr, this->fops->removexattr, this->fops->opendir, this->fops->readdir, this->fops->releasedir, this->fops->fsyncdir, this->fops->access, this->fops->ftruncate, this->fops->fgetattr, this->fops->bulk_getattr, this->mgmt_ops, this->mgmt_ops->stats,  this->mgmt_ops->fsck, this->mgmt_ops->lock, this->mgmt_ops->unlock, this->fini, this->init, this->getlayout, this->options, this->private);

  this->private = &private;
  return 0;
}

void
fini (struct xlator *this)
{
  if (!this)
    return;

  gf_log ("trace", LOG_DEBUG, "fini (struct xlator *this=%p)", this);

  /* Free up the dictionary options */
  dict_destroy (this->first_child->options);

  gf_log ("trace", LOG_DEBUG, "trace translator unloaded");
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

  gf_log ("trace", LOG_DEBUG, "trace_stats (struct xlator *this=%p)", this, stats);

  {
    int ret;
    ret = (this->first_child->mgmt_ops->stats (this->first_child, stats));
    gp_log ("trace", LOG_DEBUG, "trace_stats (*this=%p, *stats=%p {nr_files=%ld, free_disk=%lld, disk_usage=%lld, disk_speed=%lu, nr_clients=%ld, write_usage=%llu, read_usage=%llu}) => ret=%d, errno=%d", this, stats, stats->nr_files, stats->free_disk, stats->disk_usage, stats->disk_speed, stats->nr_clients, stats->write_usage, stats->read_usage, ret, errno);
    return ret;
  }
}

struct xlator_mgmt_ops mgmt_ops = {
  .stats = trace_stats
};
