/*
  (C) 2006 Gluster core team <http://www.gluster.org/>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "xlator.h"
#include <sys/time.h>

static int
posix_getattr (struct xlator *xl,
	       const char *path,
	       struct stat *stbuf)
{
  struct posix_private *priv = xl->private;
  
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (stbuf);

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  WITH_DIR_PREPENDED (path, real_path, 
    return lstat (real_path, stbuf);
  )		      
}


static int
posix_readlink (struct xlator *xl,
		const char *path,
		char *dest,
		size_t size)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (dest);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return readlink (real_path, dest, size);
  )		      
}

static int
posix_mknod (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     dev_t dev,
	     uid_t uid,
	     gid_t gid)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path, 
    int ret = mknod (real_path, mode, dev);

    if (ret == 0) {
      chown (real_path, uid, gid);
    }
    return ret;
  )		      
}

static int
posix_mkdir (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     uid_t uid,
	     gid_t gid)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path, 
    int ret = mkdir (real_path, mode);

    if (ret == 0) {
      chown (real_path, uid, gid);
    }
    return ret;
  )
}


static int
posix_unlink (struct xlator *xl,
	      const char *path)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return unlink (real_path);
  )		      
}


static int
posix_rmdir (struct xlator *xl,
	     const char *path)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path, 
    return rmdir (real_path);
  )
}



static int
posix_symlink (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (oldpath);
  GF_ERROR_IF_NULL (newpath);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  WITH_DIR_PREPENDED (newpath, real_newpath,
    int ret = symlink (oldpath, real_newpath);

    if (ret == 0) {
      lchown (real_newpath, uid, gid);
    }
    return ret;
  )
}

static int
posix_rename (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (oldpath);
  GF_ERROR_IF_NULL (newpath);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (oldpath, real_oldpath,
    WITH_DIR_PREPENDED (newpath, real_newpath,		      
      int ret = rename (real_oldpath, real_newpath);
			/*
      if (ret == 0) {
        chown (real_newpath, uid, gid);
      }
			*/
      return ret;
    )
  )
}

static int
posix_link (struct xlator *xl,
	    const char *oldpath,
	    const char *newpath,
	    uid_t uid,
	    gid_t gid)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (oldpath);
  GF_ERROR_IF_NULL (newpath);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (oldpath, real_oldpath,
    WITH_DIR_PREPENDED (newpath, real_newpath, 		      
      int ret = link (real_oldpath, real_newpath);

      if (ret == 0) {
        chown (real_newpath, uid, gid);
      }
      return ret;
    )
  )
}


static int
posix_chmod (struct xlator *xl,
	     const char *path,
	     mode_t mode)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return chmod (real_path, mode);
  )
}


static int
posix_chown (struct xlator *xl,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path, 
    return lchown (real_path, uid, gid);
  )
}


static int
posix_truncate (struct xlator *xl,
		const char *path,
		off_t offset)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return truncate (real_path, offset);
  )
}


static int
posix_utime (struct xlator *xl,
	     const char *path,
	     struct utimbuf *buf)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return utime (real_path, buf);
  )
}


static int
posix_open (struct xlator *xl,
	    const char *path,
	    int flags,
	    mode_t mode,
	    struct file_context *ctx)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  int ret = -1;
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *posix_ctx = calloc (1, sizeof (struct file_context));
  WITH_DIR_PREPENDED (path, real_path,
    int fd = open (real_path, flags, mode);

    {
      posix_ctx->volume = xl;
      posix_ctx->next = ctx->next;
      *(int *)&posix_ctx->context = fd;
    
      ctx->next = posix_ctx;
    }
    
    ret = fd;
    if (fd > 0) {
      ((struct posix_private *)xl->private)->stats.nr_files++;
    }
			
  )
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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

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
  priv->read_value += size;
  priv->interval_read += size;
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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

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
  priv->write_value += size;
  priv->interval_write += size;

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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (buf);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return statvfs (real_path, buf);
  )
}

static int
posix_flush (struct xlator *xl,
	     const char *path,
	     struct file_context *ctx)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (ctx);

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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (ctx);

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
  ((struct posix_private *)xl->private)->stats.nr_files--;
  return close (fd);
}

static int
posix_fsync (struct xlator *xl,
	     const char *path,
	     int datasync,
	     struct file_context *ctx)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (ctx);

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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (name);
  GF_ERROR_IF_NULL (value);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return lsetxattr (real_path, name, value, size, flags);
  )
}

static int
posix_getxattr (struct xlator *xl,
		const char *path,
		const char *name,
		char *value,
		size_t size)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (name);
  GF_ERROR_IF_NULL (value);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return lgetxattr (real_path, name, value, size);
  )
}

static int
posix_listxattr (struct xlator *xl,
		 const char *path,
		 char *list,
		 size_t size)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (list);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return llistxattr (real_path, list, size);
  )
}
		     
static int
posix_removexattr (struct xlator *xl,
		   const char *path,
		   const char *name)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (name);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return lremovexattr (real_path, name);
  )
}

static int
posix_opendir (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = 0;
  WITH_DIR_PREPENDED (path, real_path,
    DIR *dir = opendir (real_path);
  if (!dir)
    ret = -1;
  else
    closedir (dir);
  )		      
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

  if (!buf){
    gf_log ("posix", GF_LOG_DEBUG, "posix.c: posix_readdir: failed to allocate buf for dir %s", path);
    return buf;
  }

  WITH_DIR_PREPENDED (path, real_path,
    dir = opendir (real_path);
  )
  
  if (!dir){
    gf_log ("posix", GF_LOG_DEBUG, "posix.c: posix_readdir: failed to do opendir for %s", path);
    return NULL;
  }

  while ((dirent = readdir (dir))) {
    if (!dirent)
      break;
    length += strlen (dirent->d_name) + 1;
    if (length > alloced) {
      alloced = length * 2;
      buf = realloc (buf, alloced);
      if (!buf){
	gf_log ("posix", GF_LOG_DEBUG, "posix.c: posix_readdir: failed realloc for buf");
	return buf;
      }
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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (ctx);

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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (ctx);

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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);

  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  WITH_DIR_PREPENDED (path, real_path,
    return access (real_path, mode);
  )
}

static int
posix_ftruncate (struct xlator *xl,
		 const char *path,
		 off_t offset,
		 struct file_context *ctx)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (ctx);

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
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF_NULL (ctx);

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


static int
posix_bulk_getattr (struct xlator *xl,
		    const char *path,
		    struct bulk_stat *bstbuf)
{
  GF_ERROR_IF_NULL (xl);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (bstbuf);

  char rel_pathname[PATH_MAX] = {0,};
  struct posix_private *priv = xl->private;
  char *curr_pathname = calloc (sizeof (char), PATH_MAX);
  char *dirents = NULL;
  int index = 0;

  char real_path[PATH_MAX]; 
  strcpy (real_path, ((struct posix_private *)xl->private)->base_path); 
  strcpy (real_path+((struct posix_private *)xl->private)->base_path_length, path); 

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  /*  GET_DIR_PRENDED(path, real_path);*/
  
  /* get stats for all the entries in the current directory */
  dirents = posix_readdir (xl, path, 0);
 
  if (dirents){
    char *filename = NULL;          
    filename = strtok (dirents, "/");
    /*filename = strtok (NULL, "/");*/
    while (filename){
      if (1/*strcmp (filename, "..")*/){
	struct bulk_stat *curr = calloc (sizeof (struct bulk_stat), 1);
	struct stat *stbuf = calloc (sizeof (struct stat), 1);
	struct bulk_stat *prev_node = NULL, *ind_node = NULL;
	curr->stbuf = stbuf;
	ind_node = bstbuf;
	while (ind_node){
	  prev_node = ind_node;
	  ind_node = ind_node->next;
	}
	//	curr->next = bstbuf->next;
	if (strcmp ("/", path)){
	  sprintf (rel_pathname, "%s/%s", path, filename);
	} else {
	  sprintf (rel_pathname, "%s%s", path, filename);
	}
	curr->pathname = strdup (filename);
	memset (rel_pathname, 0, PATH_MAX);
	prev_node->next = curr;
	sprintf (curr_pathname, "%s/%s", real_path, filename);
	lstat (curr_pathname, stbuf);
	index++;
      }else{
	// NOPS: computer dictionary name for not doing anything :)
	gf_log ("posix", GF_LOG_DEBUG, "posix.c->posix_bulk_getattr: failed to do readdir for %s\n", path);
	free (curr_pathname);
	return -1;
      }
      filename = strtok (NULL, "/");
    }
  }
  //return index; //index is number of files
  return 0;
}

static int
posix_stats (struct xlator *xl,
	     struct xlator_stats *stats)
{
  struct statvfs buf;
  struct timeval tv;
  struct posix_private *priv = (struct posix_private *)xl->private;
  long avg_read = 0;
  long avg_write = 0;
  long _time_ms = 0; 
  
  WITH_DIR_PREPENDED ("/", real_path,
		      statvfs (real_path, &buf); // Get the file system related information.
		      )

  stats->nr_files = priv->stats.nr_files;
  stats->nr_clients = priv->stats.nr_clients; /* client info is maintained at FSd */
  stats->free_disk = buf.f_bfree * buf.f_bsize; // Number of Free block in the filesystem.
  stats->disk_usage = (buf.f_bfree - buf.f_bavail) * buf.f_bsize;

  /* Calculate read and write usage */
  gettimeofday (&tv, NULL);
  
  /* Read */
  _time_ms = (tv.tv_sec - priv->init_time.tv_sec) * 1000 +
             ((tv.tv_usec - priv->init_time.tv_usec) / 1000);

  avg_read = (_time_ms) ? (priv->read_value / _time_ms) : 0; /* KBps */
  avg_write = (_time_ms) ? (priv->write_value / _time_ms) : 0; /* KBps */
  
  _time_ms = (tv.tv_sec - priv->prev_fetch_time.tv_sec) * 1000 +
             ((tv.tv_usec - priv->prev_fetch_time.tv_usec) / 1000);
  if (_time_ms && ((priv->interval_read / _time_ms) > priv->max_read)) {
    priv->max_read = (priv->interval_read / _time_ms);
  }
  if (_time_ms && ((priv->interval_write / _time_ms) > priv->max_write)) {
    priv->max_write = priv->interval_write / _time_ms;
  }

  stats->read_usage = avg_read / priv->max_read;
  stats->write_usage = avg_write / priv->max_write;

  gettimeofday (&(priv->prev_fetch_time), NULL);
  priv->interval_read = 0;
  priv->interval_write = 0;
  return 0;
}

int
init (struct xlator *xl)
{
  struct posix_private *_private = calloc (1, sizeof (*_private));

  data_t *directory = dict_get (xl->options, "directory");
  data_t *debug = dict_get (xl->options, "debug");

  if (!directory){
    gf_log ("posix", GF_LOG_ERROR, "posix.c->init: export directory not specified in spec file\n");
    exit (1);
  }
  umask (022);
  if (mkdir (directory->data, 0777) == 0) {
    gf_log ("posix", GF_LOG_NORMAL, "directory specified not exists, created");
  }

  strcpy (_private->base_path, directory->data);
  _private->base_path_length = strlen (_private->base_path);

  _private->is_debug = 0;
  if (debug && strcasecmp (debug->data, "on") == 0) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("posix", GF_LOG_DEBUG, "Directory: %s", directory->data);
  }

  {
    /* Stats related variables */
    gettimeofday (&_private->init_time, NULL);
    gettimeofday (&_private->prev_fetch_time, NULL);
    _private->max_read = 1;
    _private->max_write = 1;
  }

  xl->private = (void *)_private;
  return 0;
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

struct xlator_mgmt_ops mgmt_ops = {
  .stats = posix_stats
};

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
  .fgetattr    = posix_fgetattr,
  .bulk_getattr = posix_bulk_getattr
};
