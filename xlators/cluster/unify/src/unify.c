
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
#include "unify.h"
#include "dict.h"
#include "xlator.h"





static int
cement_mkdir (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  // acquire lock
  // refer layout from namespace
  // delete namespace entry
  // delete actual file
  // unlock
  int ret = 0;
  struct cement_private *priv = xl->private;
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
cement_unlink (struct xlator *xl,
	       const char *path)
{
  // acquire lock
  // refer layout from namespace
  // delete namespace entry
  // delete actual file
  // unlock
  int ret = 0;
  struct cement_private *priv = xl->private;
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
cement_rmdir (struct xlator *xl,
	      const char *path)
{
  // acquire lock
  // delete from everywere
  // unlock
  int ret = 0;
  struct cement_private *priv = xl->private;
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
cement_open (struct xlator *xl,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int ret = 0;
  int flag = -1;
  int create_flag = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *cement_ctx = calloc (1, sizeof (struct file_context));
  cement_ctx->volume = xl;
  cement_ctx->next = ctx->next;
  ctx->next = cement_ctx;
  
  if ((flags & O_CREAT) == O_CREAT)
    create_flag = 1;
  struct xlator *trav_xl = xl->first_child;
  if (create_flag) {
    struct sched_ops *sched = ((struct cement_private *)xl->private)->sched_ops;
    struct xlator *sched_xl = sched->schedule (xl, 0);
    flag = sched_xl->fops->open (sched_xl, path, flags, mode, ctx);
  } else {
    while (trav_xl) {
      ret = trav_xl->fops->open (trav_xl, path, flags, mode, ctx);
      trav_xl = trav_xl->next_sibling;
      if (ret >= 0)
	flag = ret;
    }
  }
  ret = flag;
  
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
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->read (trav_xl, path, buf, size, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret > 0)
      break;
  }

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
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->write (trav_xl, path, buf, size, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      break;
  }

  return ret;
}

static int
cement_statfs (struct xlator *xl,
	       const char *path,
	       struct statvfs *stbuf)
{
  int ret = 0;
  struct statvfs buf = {0,};
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  /* Initialize structure variable */
  stbuf->f_bsize = 0;
  stbuf->f_frsize = 0;
  stbuf->f_blocks = 0;
  stbuf->f_bfree = 0;
  stbuf->f_bavail = 0;
  stbuf->f_files = 0;
  stbuf->f_ffree = 0;
  stbuf->f_favail = 0;
  stbuf->f_fsid = 0;
  stbuf->f_flag = 0;
  stbuf->f_namemax = 0;
  
  while (trav_xl) {
    ret = trav_xl->fops->statfs (trav_xl, path, &buf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0) {
      flag = ret;
      stbuf->f_bsize = buf.f_bsize;
      stbuf->f_frsize = buf.f_frsize;
      stbuf->f_blocks += buf.f_blocks;
      stbuf->f_bfree += buf.f_bfree;
      stbuf->f_bavail += buf.f_bavail;
      stbuf->f_files += buf.f_files;
      stbuf->f_ffree += buf.f_ffree;
      stbuf->f_favail += buf.f_favail;
      stbuf->f_fsid = buf.f_fsid;
      stbuf->f_flag = buf.f_flag;
      stbuf->f_namemax = buf.f_namemax;
    }
  }
  ret = flag;

  return ret;
}


static int
cement_release (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->release (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      break;
  }
  
  if (tmp != NULL) {
    RM_MY_CTX (ctx, tmp);
    free (tmp);
  }

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
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
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

char * 
update_buffer (char *buf, char *names)
{
  // This works partially :-(
  int str_len;
  

  /* check if the buf is big enough to hold the complete dir listing */
  {
    int buf_len = strlen (buf);
    
    int remaining_buf_len = buf_len % MAX_DIR_ENTRY_STRING;
    int names_len = strlen (names);
    if ((( buf_len != 0) || names_len >= MAX_DIR_ENTRY_STRING) && 
	(((remaining_buf_len + names_len) >= MAX_DIR_ENTRY_STRING) || (remaining_buf_len == 0))){
      int no_of_new_chunks = names_len/MAX_DIR_ENTRY_STRING + 1;
      int no_of_existing = buf_len/MAX_DIR_ENTRY_STRING + 1;
      char *new_buf = calloc (MAX_DIR_ENTRY_STRING, (no_of_new_chunks + no_of_existing));

      if (new_buf){
	strcat (new_buf, buf);

	free (buf);
	buf = new_buf;
      }
    }
  }
  strcat (buf, names);
  str_len = strlen (buf);
  buf[str_len] = '/';
  buf[str_len + 1] = '\0';
  return buf;
}

static char *
cement_readdir (struct xlator *xl,
		const char *path,
		off_t offset)
{
  char *ret = NULL;
  char *buffer = calloc (1, MAX_DIR_ENTRY_STRING); //FIXME: How did I arrive at this value? (32k)

  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  
  while (trav_xl) {
    ret = trav_xl->fops->readdir (trav_xl, path, offset);
    trav_xl = trav_xl->next_sibling;
    if (ret != NULL) {
      buffer = update_buffer (buffer, ret);
      free (ret); 
      ret = NULL;
    }
  }


  return buffer;
}


static int
cement_ftruncate (struct xlator *xl,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

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
cement_fgetattr (struct xlator *xl,
		 const char *path,
		 struct stat *stbuf,
		 struct file_context *ctx)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;

  while (trav_xl) {
    ret = trav_xl->fops->fgetattr (trav_xl, path, stbuf, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
cement_getattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf)
{
  int ret = 0;
  struct cement_private *priv = xl->private;
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
cement_bulk_getattr (struct xlator *xl,
		     const char *path,
		     struct stat *bstbuf)
{
  return 0;
}

static int
cement_stats (struct xlator_stats *stats)
{
  return 0;
}

int
init (struct xlator *xl)
{
  struct cement_private *_private = calloc (1, sizeof (*_private));
  data_t *debug = dict_get (xl->options, "debug");
  data_t *scheduler = dict_get (xl->options, "scheduler");

  if (!scheduler) {
    gf_log ("unify", LOG_CRITICAL, "unify.c->init: scheduler option is not provided\n");
    exit (1);
  }
  _private->sched_ops = get_scheduler (scheduler->data);

  _private->is_debug = 0;
  if (debug && strcasecmp (debug->data, "on") == 0) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("unify", LOG_CRITICAL, "unify.c->init: debug mode on\n");
  }

  xl->private = (void *)_private;
  _private->sched_ops->init (xl); // Initialize the schedular 
  return 0;
}

void
fini (struct xlator *xl)
{
  struct cement_private *priv = xl->private;
  priv->sched_ops->fini (xl);
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = cement_getattr,
  .mkdir       = cement_mkdir,
  .unlink      = cement_unlink,
  .rmdir       = cement_rmdir,
  .open        = cement_open,
  .read        = cement_read,
  .write       = cement_write,
  .statfs      = cement_statfs,
  .release     = cement_release,
  .fsync       = cement_fsync,
  .readdir     = cement_readdir,
  .ftruncate   = cement_ftruncate,
  .fgetattr    = cement_fgetattr,
  .bulk_getattr = cement_bulk_getattr
};

struct xlator_mgmt_ops mgmt_ops = {
  .stats = cement_stats
};
