/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
#include "hashfn.h"
#include "logging.h"
#include "stack.h"

#define INIT_LOCK(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);
#define LOCK_NODE(xl) (((cement_private_t *)xl->private)->lock_node)

static char *
gcd_path (const char *path1, const char *path2)
{
  char *s1 = (char *)path1;
  char *s2 = (char *)path2;
  int32_t diff = -1;

  while (*s1 && *s2 && (*s1 == *s2)) {
    if (*s1 == '/')
      diff = s1 - path1;
    s1++;
    s2++;
  }

  return (diff == -1) ? NULL : strndup (path1, diff + 1);
}

char *
gf_basename (char *path)
{
  char *base = basename (path);
  if (base[0] == '/' && base[1] == '\0')
    base[0] = '.';
  
  return base;
}

/* TODO: all references to local->* should be locked */

/* setxattr */
static int32_t  
unify_setxattr_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) 
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);

    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_setxattr (call_frame_t *frame,
		xlator_t *xl,
		const char *path,
		const char *name,
		const char *value,
		size_t size,
		int32_t flags)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  xlator_list_t *trav = xl->children;

  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  while (trav) {
    STACK_WIND (frame, 
		unify_setxattr_cbk,
		trav->xlator,
		trav->xlator->fops->setxattr,
		path, 
		name, 
		value, 
		size, 
		flags);
    trav = trav->next;
  }
  return 0;
} 


/* getxattr */
static int32_t  
unify_getxattr_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    void *value)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret >= 0) {
    char *tmp_value = calloc (1, sizeof (op_ret));
    memcpy (tmp_value, value, op_ret);
    if (local->buf)
      /* if file existed in two places by corruption */
      free (local->buf);
    local->buf = tmp_value;
    local->op_ret = op_ret;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    frame->local = NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->buf ? local->buf : "");
    if (local->buf)
      free (local->buf);
    free (local);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_getxattr (call_frame_t *frame,
		xlator_t *xl,
		const char *path,
		const char *name,
		size_t size)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  xlator_list_t *trav = xl->children;
  
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_getxattr_cbk,
		trav->xlator,
		trav->xlator->fops->getxattr,
		path,
		name,
		size);
    trav = trav->next;
  }
  return 0;
} 


/* listxattr */
static int32_t  
unify_listxattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret >= 0) {
    char *tmp_value = calloc (1, sizeof (op_ret));
    memcpy (tmp_value, value, op_ret);
    if (local->buf)
      free (local->buf);
    local->buf = tmp_value;    
    local->op_ret = op_ret;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    frame->local = NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->buf ? local->buf : "");
    if (local->buf)
      free (local->buf);
    free (local);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_listxattr (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 size_t size)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  xlator_list_t *trav = xl->children;
  
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  while (trav) {
    STACK_WIND (frame, 
		unify_listxattr_cbk,
		trav->xlator,
		trav->xlator->fops->listxattr,
		path,
		size);
    trav = trav->next;
  }
  return 0;
} 


/* removexattr */     
static int32_t  
unify_removexattr_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) 
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_removexattr (call_frame_t *frame,
		   xlator_t *xl,
		   const char *path,
		   const char *name)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  xlator_list_t *trav = xl->children;

  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  while (trav) {
    STACK_WIND (frame, 
		unify_removexattr_cbk,
		trav->xlator,
		trav->xlator->fops->removexattr,
		path,
		name);
    trav = trav->next;
  }
  return 0;
} 


/* open */
static int32_t  
unify_open_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *file_ctx,
		struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;

  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret != 0 && op_errno != ENOTCONN && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret >= 0) {
    // put the child node's address in ctx->contents
    dict_set (file_ctx,
	      xl->name,
	      int_to_data ((long)prev_frame->this));

    if (local->orig_frame) {
      STACK_UNWIND (local->orig_frame,
		    op_ret,
		    op_errno,
		    file_ctx,
		    stbuf);
      local->orig_frame = NULL;
    }
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->orig_frame) {
      STACK_UNWIND (local->orig_frame,
		    local->op_ret,
		    local->op_errno,
		    file_ctx,
		    stbuf);
      local->orig_frame = NULL;
    }

    frame->local = NULL;
    
    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);

    free (local);
  }
  return 0;
}


static int32_t  
unify_open (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path,
	    int32_t flags,
	    mode_t mode)
{
  call_frame_t *open_frame = copy_frame (frame);
  unify_local_t *local = calloc (1, sizeof (unify_local_t));  
  xlator_list_t *trav = xl->children;

  open_frame->local = local;

  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->orig_frame = frame;

  INIT_LOCK (&frame->mutex);  
  while (trav) {
    STACK_WIND (open_frame,
		unify_open_cbk,
		trav->xlator,
		trav->xlator->fops->open,
		path,
		flags,
		mode);
    trav = trav->next;
  }

  return 0;
} 


/* read */
static int32_t  
unify_readv_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct iovec *vector,
		 int32_t count)
{
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t  
unify_readv (call_frame_t *frame,
	     xlator_t *xl,
	     dict_t *file_ctx,
	     size_t size,
	     off_t offset)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, "");
    return -1;
  }  
  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_readv_cbk,
	      child,
	      child->fops->readv,
	      file_ctx,
	      size,
	      offset);
  return 0;
} 

/* write */
static int32_t  
unify_writev_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t  
unify_writev (call_frame_t *frame,
	      xlator_t *xl,
	      dict_t *file_ctx,
	      struct iovec *vector,
	      int32_t count,
	      off_t offset)

{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_writev_cbk,
	      child,
	      child->fops->writev,
	      file_ctx,
	      vector,
	      count,
	      offset);
  return 0;
} 


/* ftruncate */
static int32_t  
unify_ftruncate_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t  
unify_ftruncate (call_frame_t *frame,
		 xlator_t *xl,
		 dict_t *file_ctx,
		 off_t offset)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, EBADFD, &nullbuf);
    return -1;
  }

  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_ftruncate_cbk,
	      child,
	      child->fops->ftruncate,
	      file_ctx,
	      offset);
  return 0;
} 


/* fgetattr */
static int32_t  
unify_fgetattr_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t  
unify_fgetattr (call_frame_t *frame,
		xlator_t *xl,
		dict_t *file_ctx)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, EBADFD, &nullbuf);
    return -1;
  }
  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_fgetattr_cbk,
	      child,
	      child->fops->fgetattr,
	      file_ctx);
  return 0;
} 

/* flush */
static int32_t  
unify_flush_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t  
unify_flush (call_frame_t *frame,
	     xlator_t *xl,
	     dict_t *file_ctx)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }
  xlator_t *child = (void *)((long) data_to_int (fd_data));
  
  STACK_WIND (frame,
	      unify_flush_cbk,
	      child,
	      child->fops->flush,
	      file_ctx);
  return 0;
} 

/* release */
static int32_t  
unify_release_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t  
unify_release (call_frame_t *frame,
	       xlator_t *xl,
	       dict_t *file_ctx)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    /* TODO: gf_log and do more cleanup */
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_release_cbk,
	      child,
	      child->fops->release,
	      file_ctx);

  return 0;
} 


/* fsync */
static int32_t  
unify_fsync_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t  
unify_fsync (call_frame_t *frame,
	     xlator_t *xl,
	     dict_t *file_ctx,
	     int32_t flags)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  xlator_t *child = (void *)((long) data_to_int (fd_data));
  
  STACK_WIND (frame, 
	      unify_fsync_cbk,
	      child,
	      child->fops->fsync,
	      file_ctx,
	      flags);
  
  return 0;
} 

/* lk */
static int32_t  
unify_lk_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *xl,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct flock *lock)
{
  STACK_UNWIND (frame, op_ret, op_errno, lock);
  return 0;
}

static int32_t  
unify_lk (call_frame_t *frame,
	  xlator_t *xl,
	  dict_t *file_ctx,
	  int32_t cmd,
	  struct flock *lock)
{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, "");
    return -1;
  }  
  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_lk_cbk,
	      child,
	      child->fops->lk,
	      file_ctx,
	      cmd,
	      lock);
  return 0;
} 

/* getattr */
static int32_t  
unify_getattr_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0) {
    call_frame_t *orig_frame;

    LOCK (&frame->mutex);
    orig_frame = local->orig_frame;
    local->orig_frame = NULL;
    UNLOCK (&frame->mutex);

    if (orig_frame) {
      STACK_UNWIND (orig_frame, op_ret, op_errno, stbuf);
    }
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->orig_frame)
      STACK_UNWIND (local->orig_frame,
		    local->op_ret,
		    local->op_errno,
		    &local->stbuf);
    STACK_DESTROY (frame->root);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_getattr (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  call_frame_t *getattr_frame = copy_frame (frame);
  unify_local_t *local = (void *)calloc (1, sizeof (unify_local_t));
  xlator_list_t *trav = xl->children;
  
  INIT_LOCK (&frame->mutex);
  getattr_frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->orig_frame = frame;
  
  while (trav) {
    STACK_WIND (getattr_frame,
		unify_getattr_cbk,
		trav->xlator,
		trav->xlator->fops->getattr,
		path);
    trav = trav->next;
  }
  return 0;
} 


/* statfs */
static int32_t  
unify_statfs_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    struct statvfs *dict_buf = &local->statvfs_buf;
    dict_buf->f_bsize   = stbuf->f_bsize;
    dict_buf->f_frsize  = stbuf->f_frsize;
    dict_buf->f_blocks += stbuf->f_blocks;
    dict_buf->f_bfree  += stbuf->f_bfree;
    dict_buf->f_bavail += stbuf->f_bavail;
    dict_buf->f_files  += stbuf->f_files;
    dict_buf->f_ffree  += stbuf->f_ffree;
    dict_buf->f_favail += stbuf->f_favail;
    dict_buf->f_fsid    = stbuf->f_fsid;
    dict_buf->f_flag    = stbuf->f_flag;
    dict_buf->f_namemax = stbuf->f_namemax;
    local->op_ret = 0;
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs_buf);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}


static int32_t  
unify_statfs (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  xlator_list_t *trav = xl->children;

  ((unify_local_t *)frame->local)->op_ret = -1;
  ((unify_local_t *)frame->local)->op_errno = ENOTCONN;

  INIT_LOCK (&frame->mutex);

  while (trav) {
    STACK_WIND (frame, 
		unify_statfs_cbk,
		trav->xlator,
		trav->xlator->fops->statfs,
		path);
    trav = trav->next;
  }
  return 0;
} 


/* truncate */
static int32_t  
unify_truncate_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->stbuf = *stbuf;
    local->op_ret = 0;
    UNLOCK (&frame->mutex);
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_truncate (call_frame_t *frame,
		xlator_t *xl,
		const char *path,
		off_t offset)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  xlator_list_t *trav = xl->children;
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_truncate_cbk,
		trav->xlator,
		trav->xlator->fops->truncate,
		path,
		offset);
    trav = trav->next;
  }
  return 0;
} 

/* utimes */
static int32_t  
unify_utimes_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->stbuf = *stbuf;
    local->op_ret = 0;  
    UNLOCK (&frame->mutex);
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_utimes (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      struct timespec *buf)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  xlator_list_t *trav = xl->children;
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  while (trav) {
    STACK_WIND (frame, 
		unify_utimes_cbk,
		trav->xlator,
		trav->xlator->fops->utimes,
		path,
		buf);
    trav = trav->next;
  }
  return 0;
}


/* opendir */
static int32_t  
unify_opendir_getattr_cbk (call_frame_t *frame,
			   call_frame_t *prev_frame,
			   xlator_t *xl,
			   int32_t op_ret,
			   int32_t op_errno,
			   struct stat *buf)
{
  /* this call comes here from getattr */
  if (op_ret == 0) {
    if (!S_ISDIR (buf->st_mode)) {
      op_ret = -1;
      op_errno = ENOTDIR;
    }
  }
  STACK_UNWIND (frame, op_ret, op_errno, NULL);
  return 0;
}

static int32_t  
unify_opendir (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  /* TODO: If LOCK Server is down, this will fail */
  STACK_WIND (frame, 
	      unify_opendir_getattr_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->fops->getattr,
	      path);
  return 0;
} 


/* readlink */
static int32_t  
unify_readlink_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    char *buf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  } else if (op_ret >= 0) {
    LOCK (&frame->mutex);
    if (local->buf)
      free (local->buf);
    local->buf = strdup (buf);
    local->op_ret = op_ret;
    UNLOCK (&frame->mutex);
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    frame->local = NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->buf ? local->buf : "");

    if (local->buf)
      free (local->buf);
    free (local);

    LOCK_DESTROY (&frame->mutex);
  }
  return 0;
}

static int32_t  
unify_readlink (call_frame_t *frame,
		xlator_t *xl,
		const char *path,
		size_t size)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  xlator_list_t *trav = xl->children;
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_readlink_cbk,
		trav->xlator,
		trav->xlator->fops->readlink,
		path,
		size);
    trav = trav->next;
  }
  return 0;
} 

/* readdir */
static int32_t  
unify_readdir_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   dir_entry_t *entry,
		   int32_t count)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret >= 0) {
    LOCK (&frame->mutex);
    dir_entry_t *trav = entry->next;
    dir_entry_t *prev = entry;
    dir_entry_t *tmp;
    if (local->entry == NULL) {
      dir_entry_t *unify_entry = calloc (1, sizeof (dir_entry_t));
      unify_entry->next = trav;

      while (trav->next) 
	trav = trav->next;
      local->entry = unify_entry;
      local->last = trav;
      local->count = count;
    } else {
      // copy only file names
      int32_t tmp_count = count;
      while (trav) {
	tmp = trav;
	if (S_ISDIR (tmp->buf.st_mode)) {
	  prev->next = tmp->next;
	  trav = tmp->next;
	  free (tmp->name);
	  free (tmp);
	  tmp_count--;
	  continue;
	}
	prev = trav;
	trav = trav->next;
      }
      // append the current dir_entry_t at the end of the last node
      local->last->next = entry->next;
      local->count += tmp_count;
      while (local->last->next)
	local->last = local->last->next;
    }
    entry->next = NULL;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == -1 && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    dir_entry_t *prev = local->entry;
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->entry, local->count);
    dir_entry_t *trav = prev->next;
    while (trav) {
      prev->next = trav->next;
      free (trav->name);
      free (trav);
      trav = prev->next;
    }
    free (prev);
  }
  return 0;
}

static int32_t  
unify_readdir (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  xlator_list_t *trav = xl->children;
  
  INIT_LOCK (&frame->mutex);

  while (trav) {
    STACK_WIND (frame,
		unify_readdir_cbk,
		trav->xlator,
		trav->xlator->fops->readdir,
		path);
    trav = trav->next;
  }
  return 0;
} 

/* FOPS with LOCKs */

/* Start of mkdir */
static int32_t  
unify_mkdir_unlock_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, 
		local->op_ret,
		local->op_errno,
		&local->stbuf);

  LOCK_DESTROY (&frame->mutex);
  free (local->path);
  free (local);
  return 0;
}

static int32_t  
unify_mkdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  /* TODO: need to handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0 && local->call_count == 1) {
    /* no need to lock here since local->call_count == 1 is checked */
    local->stbuf = *stbuf;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_mkdir_unlock_cbk,
		LOCK_NODE(xl),
		LOCK_NODE(xl)->mops->unlock,
		local->path);
  }
  return 0;
}

static int32_t  
unify_mkdir_lock_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = 0;
    local->op_errno = 0;
    while (trav) {
      STACK_WIND (frame,
		  unify_mkdir_cbk,
		  trav->xlator,
		  trav->xlator->fops->mkdir,
		  local->path,
		  local->mode);
      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_mkdir (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     mode_t mode)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;

  local->mode = mode;
  local->path = strdup (path);
  STACK_WIND (frame, 
	      unify_mkdir_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);
  return 0;
} /* End of mkdir */


/* unlink */
static int32_t  
unify_unlink_unlock_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *xl,
			 int32_t op_ret,
			 int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  LOCK_DESTROY (&frame->mutex);
  free (local->path);
  free (local);
  return 0;
}

static int32_t  
unify_unlink_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) 
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_unlink_unlock_cbk,
		LOCK_NODE(xl),
		LOCK_NODE(xl)->mops->unlock,
		local->path);
  }
  return 0;
}

static int32_t  
unify_unlink_lock_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    while (trav) {
      STACK_WIND (frame,
		  unify_unlink_cbk,
		  trav->xlator,
		  trav->xlator->fops->unlink,
		  local->path);
      trav = trav->next;
    }
  } else {
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_unlink (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;

  local->path = strdup (path);
  STACK_WIND (frame, 
	      unify_unlink_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);
  return 0;
} /* End of unlink */


/* rmdir */
static int32_t  
unify_rmdir_unlock_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno);

  LOCK_DESTROY (&frame->mutex);
  free (local->path);
  free (local);
  return 0;
}

static int32_t  
unify_rmdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_rmdir_unlock_cbk,
		LOCK_NODE(xl),
		LOCK_NODE(xl)->mops->unlock,
		local->path);
  }
  return 0;
}

static int32_t  
unify_rmdir_lock_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = 0;
    local->op_errno = 0;
    while (trav) {
      STACK_WIND (frame,
		  unify_rmdir_cbk,
		  trav->xlator,
		  trav->xlator->fops->rmdir,
		  local->path);
      trav = trav->next;
    }
  } else {
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_rmdir (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  local->path = strdup (path);
  
  STACK_WIND (frame, 
	      unify_rmdir_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);
  return 0;
} 


/* create */
static int32_t  
unify_create_unlock_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *xl,
			 int32_t op_ret,
			 int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		local->file_ctx,
		&local->stbuf);
  
  free (local->path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}


static int32_t  
unify_create_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *file_ctx,
		  struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  if (op_ret >= 0) {
    dict_set (file_ctx,
	      xl->name,
	      int_to_data ((long)prev_frame->this));
    local->file_ctx = file_ctx;
    local->stbuf = *stbuf;
  }

  STACK_WIND (frame,
	      unify_create_unlock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->unlock,
	      local->path);
  return 0;
}

static int32_t  
unify_create_getattr_cbk (call_frame_t *frame,
			  call_frame_t *prev_frame,
			  xlator_t *xl,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *stbuf)
{
  /* TODO: Handle ENOTCONN, so that all the editors work seemlessly */
  unify_local_t *local = (unify_local_t *)frame->local;

  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = -1;
    local->op_errno = EEXIST;
  }

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1 && local->op_errno == ENOENT) {
      xlator_t *sched_xl = NULL;
      struct cement_private *priv = xl->private;
      struct sched_ops *ops = priv->sched_ops;
      
      sched_xl = ops->schedule (xl, 0);
      
      STACK_WIND (frame,
		  unify_create_cbk,
		  sched_xl,
		  sched_xl->fops->create,
		  local->path,
		  local->mode);

      local->sched_xl = sched_xl;
    } else {
      STACK_WIND (frame,
		  unify_create_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->path);
    }
  }
  return 0;
}

static int32_t  
unify_create_lock_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;

  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    
    while (trav) {
      STACK_WIND (frame,
		  unify_create_getattr_cbk,
		  trav->xlator,
		  trav->xlator->fops->getattr,
		  local->path);
      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local);
  }
  
  return 0;
}

static int32_t  
unify_create (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  frame->local = calloc (1, sizeof (unify_local_t));
  
  unify_local_t *local = (unify_local_t *)frame->local;
  
  local->path = strdup (path);
  local->mode = mode;
  
  STACK_WIND (frame, 
	      unify_create_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);
  return 0;
} 


/* mknod */
static int32_t  
unify_mknod_unlock_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}

static int32_t  
unify_mknod_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0)
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
  local->op_ret = op_ret;
  local->op_errno = op_errno;

  STACK_WIND (frame,
	      unify_mknod_unlock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->unlock,
	      local->path);
  return 0;
}

static int32_t  
unify_mknod_getattr_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *xl,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *stbuf)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = -1;
    local->op_errno = EEXIST;
  }

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1 && local->op_errno == ENOENT) {
      xlator_t *sched_xl = NULL;
      struct cement_private *priv = xl->private;
      struct sched_ops *ops = priv->sched_ops;

      sched_xl = ops->schedule (xl, 0);
      
      STACK_WIND (frame,
		  unify_mknod_cbk,
		  sched_xl,
		  sched_xl->fops->mknod,
		  local->path,
		  local->mode,
		  local->dev);
    } else {
      STACK_WIND (frame,
		  unify_mknod_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->path);
    }
  }
  return 0;
}


static int32_t  
unify_mknod_lock_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_mknod_getattr_cbk,
		  trav->xlator,
		  trav->xlator->fops->getattr,
		  local->path);
      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_mknod (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     mode_t mode,
	     dev_t dev)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;

  local->dev = dev;
  local->mode = mode;
  local->path = strdup (path);
  STACK_WIND (frame, 
	      unify_mknod_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);

  return 0;
} 

/* symlink */
static int32_t  
unify_symlink_unlock_cbk (call_frame_t *frame,
			  call_frame_t *prev_frame,
			  xlator_t *xl,
			  int32_t op_ret,
			  int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
  free (local->new_path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}

static int32_t  
unify_symlink_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  if (op_ret == 0)
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  STACK_WIND (frame,
	      unify_symlink_unlock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->unlock,
	      local->new_path);

  return 0;
}

static int32_t 
unify_symlink_getattr_cbk (call_frame_t *frame,
			   call_frame_t *prev_frame,
			   xlator_t *xl,
			   int32_t op_ret,
			   int32_t op_errno)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;

  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = -1;
    local->op_errno = EEXIST;
  }

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1 && local->op_errno == ENOENT) {
      xlator_t *sched_xl = NULL;
      struct cement_private *priv = xl->private;
      struct sched_ops *ops = priv->sched_ops;

      sched_xl = ops->schedule (xl, 0);
            
      STACK_WIND (frame,
		  unify_symlink_cbk,
		  sched_xl,
		  sched_xl->fops->symlink,
		  local->path,
		  local->new_path);
    } else {
      STACK_WIND (frame,
		  unify_symlink_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->new_path);
    }
  }

  return 0;
}

static int32_t  
unify_symlink_lock_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    while (trav) {
      STACK_WIND (frame,
		  unify_symlink_getattr_cbk,
		  trav->xlator,
		  trav->xlator->fops->getattr,
		  local->new_path);
      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local->new_path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_symlink (call_frame_t *frame,
	       xlator_t *xl,
	       const char *oldpath,
	       const char *newpath)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;

  local->path = strdup (oldpath);
  local->new_path = strdup (newpath);
  STACK_WIND (frame, 
	      unify_symlink_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      newpath);
  return 0;
} 


/* rename */
static int32_t  
unify_rename_unlock_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->buf);
  free (local->path);
  free (local->new_path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}

static int32_t
unify_rename_unlink_newpath_cbk (call_frame_t *frame,
				 call_frame_t *prev_frame,
				 xlator_t *xl,
				 int32_t op_ret,
				 int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  STACK_WIND (frame,
	      unify_rename_unlock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->unlock,
	      local->buf);
  return 0;
}


static int32_t
unify_rename_dir_cbk (call_frame_t *frame,
                      call_frame_t *prev_frame,
                      xlator_t *xl,
                      int32_t op_ret,
                      int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
                unify_rename_unlock_cbk,
                LOCK_NODE(xl),
                LOCK_NODE(xl)->mops->unlock,
                local->buf);
  }
  return 0;
}


static int32_t  
unify_rename_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  if (!op_ret && local->found_xl && local->found_xl != local->sched_xl)
    STACK_WIND (frame,
		unify_rename_unlink_newpath_cbk,
		local->found_xl,
		local->found_xl->fops->unlink,
		local->new_path);
  else
    STACK_WIND (frame,
		unify_rename_unlock_cbk,
		LOCK_NODE(xl),
		LOCK_NODE(xl)->mops->unlock,
		local->buf);

  return 0;
}


static int32_t  
unify_rename_newpath_lookup_cbk (call_frame_t *frame,
				 call_frame_t *prev_frame,
				 xlator_t *xl,
				 int32_t op_ret,
				 int32_t op_errno,
				 struct stat *stbuf)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->found_xl = prev_frame->this;
    if (S_ISDIR(stbuf->st_mode) && !S_ISDIR(local->stbuf.st_mode))
      local->op_errno = EISDIR;
    else if (S_ISDIR(stbuf->st_mode))
      local->op_errno = EEXIST;
  }

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1 && local->op_errno == ENOENT) {
      if (!S_ISDIR(local->stbuf.st_mode)) {
        STACK_WIND (frame,
                    unify_rename_cbk,
                    local->sched_xl,
                    local->sched_xl->fops->rename,
                    local->path,
                    local->new_path);
      } else {
        xlator_list_t *trav = xl->children;
        local->call_count = 0;
        local->op_ret = 0;
        local->op_errno = 0;
        while (trav) {
          STACK_WIND (frame,
                      unify_rename_dir_cbk,
                      trav->xlator,
                      trav->xlator->fops->rename,
                      local->path,
                      local->new_path);
          trav = trav->next;
        }
      }
    } else {
      STACK_WIND (frame,
		  unify_rename_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->buf);
    }
  }
  return 0;
}

static int32_t  
unify_rename_oldpath_lookup_cbk (call_frame_t *frame,
				 call_frame_t *prev_frame,
				 xlator_t *xl,
				 int32_t op_ret,
				 int32_t op_errno,
				 struct stat *stbuf)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    local->op_ret = 0;
    local->sched_xl = prev_frame->this;
    local->stbuf = *stbuf;
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == 0) {
      xlator_list_t *trav = xl->children;
      local->op_ret = -1;
      local->op_errno = ENOENT;

      local->call_count = 0;

      while (trav) {
	STACK_WIND (frame,
		    unify_rename_newpath_lookup_cbk,
		    trav->xlator,
		    trav->xlator->fops->getattr,
		    local->new_path);
	trav = trav->next;
      } 
    } else {
      local->op_ret = -1;
      local->op_errno = ENOENT;

      STACK_WIND (frame,
		  unify_rename_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->buf);
    }
  }
  return 0;
}

static int32_t  
unify_rename_lock_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_rename_oldpath_lookup_cbk,
		  trav->xlator,
		  trav->xlator->fops->getattr,
		  local->path);
      trav = trav->next;
    }
  } else {
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno);
    free (local->new_path);
    free (local->buf);
    free (local->path);
    free (local);
  }
  return 0;
}


static int32_t  
unify_rename (call_frame_t *frame,
	      xlator_t *xl,
	      const char *oldpath,
	      const char *newpath)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  local->buf = gcd_path (oldpath, newpath);
  local->new_path = strdup (newpath);
  local->path = strdup (oldpath);
  STACK_WIND (frame, 
	      unify_rename_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      local->buf);
  return 0;
} 

/* link */
static int32_t  
unify_link_unlock_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->buf);
  free (local->path);
  free (local->new_path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}

static int32_t  
unify_link_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  if (op_ret == 0) 
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  STACK_WIND (frame,
	      unify_link_unlock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->unlock,
	      local->buf);

  return 0;
}


static int32_t  
unify_link_newpath_lookup_cbk (call_frame_t *frame,
			       call_frame_t *prev_frame,
			       xlator_t *xl,
			       int32_t op_ret,
			       int32_t op_errno,
			       struct stat *stbuf)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (!op_ret) {
    local->op_ret = -1;
    local->op_errno = EEXIST;
  }

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1 && local->op_errno == ENOENT) {
      STACK_WIND (frame,
		  unify_link_cbk,
		  local->sched_xl,
		  local->sched_xl->fops->link,
		  local->path,
		  local->new_path);
    } else {
      STACK_WIND (frame,
		  unify_link_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->buf);
    }
  }
  return 0;
}

static int32_t  
unify_link_oldpath_lookup_cbk (call_frame_t *frame,
			       call_frame_t *prev_frame,
			       xlator_t *xl,
			       int32_t op_ret,
			       int32_t op_errno,
			       struct stat *stbuf)
{
  /* TODO: Handle ENOTCONN */
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    local->op_ret = 0;
    local->sched_xl = prev_frame->this;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == 0) {
      xlator_list_t *trav = xl->children;
      INIT_LOCK (&frame->mutex);
      local->op_ret = -1;
      local->op_errno = ENOENT;
      local->call_count = 0;
      
      while (trav) {
	STACK_WIND (frame,
		    unify_link_newpath_lookup_cbk,
		    trav->xlator,
		    trav->xlator->fops->getattr,
		    local->new_path);
	trav = trav->next;
      } 
    } else {
      /* op_ret == -1; */
      STACK_WIND (frame,
		  unify_link_unlock_cbk,
		  LOCK_NODE(xl),
		  LOCK_NODE(xl)->mops->unlock,
		  local->buf);
    }
  }
  return 0;
}

static int32_t  
unify_link_lock_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_link_oldpath_lookup_cbk,
		  trav->xlator,
		  trav->xlator->fops->getattr,
		  local->path);
      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->buf);
    free (local->path);
    free (local->new_path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_link (call_frame_t *frame,
	    xlator_t *xl,
	    const char *oldpath,
	    const char *newpath)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  local->buf = gcd_path (oldpath, newpath);
  local->path = strdup (oldpath);
  local->new_path = strdup (newpath);
  STACK_WIND (frame, 
	      unify_link_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      local->buf);
  return 0;
} 


/* chmod */
static int32_t  
unify_chmod_unlock_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}

static int32_t  
unify_chmod_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0) {
    local->op_ret = 0;
    local->stbuf = *stbuf;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_chmod_unlock_cbk,
		LOCK_NODE(xl),
		LOCK_NODE(xl)->mops->unlock,
		local->path);
  }
  return 0;
}

static int32_t  
unify_chmod_lock_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    while (trav) {
      STACK_WIND (frame,
		  unify_chmod_cbk,
		  trav->xlator,
		  trav->xlator->fops->chmod,
		  local->path,
		  local->mode);
      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_chmod (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     mode_t mode)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  local->mode = mode;
  local->path = strdup (path);

  STACK_WIND (frame, 
	      unify_chmod_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);
  return 0;
} 

/* chown */
static int32_t  
unify_chown_unlock_cbk (call_frame_t *frame,
			call_frame_t *prev_frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
  free (local);
  LOCK_DESTROY (&frame->mutex);
  return 0;
}

static int32_t  
unify_chown_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    local->op_ret = 0;
    local->stbuf = *stbuf;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_chown_unlock_cbk,
		LOCK_NODE(xl),
		LOCK_NODE(xl)->mops->unlock,
		local->path);
  }
  return 0;
}

static int32_t  
unify_chown_lock_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret == 0) {
    xlator_list_t *trav = xl->children;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_chown_cbk,
		  trav->xlator,
		  trav->xlator->fops->chown,
		  local->path,
		  local->uid,
		  local->gid);

      trav = trav->next;
    }
  } else {
    struct stat nullbuf = {0, };
    frame->local = NULL;
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local);
  }
  return 0;
}

static int32_t  
unify_chown (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  local->uid = uid;
  local->gid = gid;
  local->path = strdup(path);
  STACK_WIND (frame, 
	      unify_chown_lock_cbk,
	      LOCK_NODE(xl),
	      LOCK_NODE(xl)->mops->lock,
	      path);

  return 0;
} 

/* FOPS not implemented */

/* releasedir */
static int32_t  
unify_releasedir (call_frame_t *frame,
		  xlator_t *xl,
		  dict_t *ctx)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
} 

/* fsyncdir */ 
static int32_t  
unify_fsyncdir (call_frame_t *frame,
		xlator_t *xl,
		dict_t *ctx,
		int32_t flags)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

/* access */
static int32_t  
unify_access (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

static int32_t  
unify_stats (call_frame_t *frame,
	     struct xlator *xl,
	     int32_t flags)
{
  STACK_UNWIND (frame, -1, ENOSYS, NULL);
  return -1;
}

int32_t 
init (struct xlator *xl)
{
  struct cement_private *_private = calloc (1, sizeof (*_private));
  data_t *scheduler = dict_get (xl->options, "scheduler");
  data_t *lock_node = dict_get (xl->options, "lock-node");
  
  if (!scheduler) {
    gf_log ("unify", GF_LOG_ERROR, "unify.c->init: scheduler option is not provided\n");
    return -1;
  }

  if (!xl->children) {
    gf_log ("unify",
	    GF_LOG_ERROR,
	    "FATAL - unify configured without children. cannot continue");
    return -1;
  }

  _private->sched_ops = get_scheduler (scheduler->data);

  /* update _private structure */
  {
    xlator_list_t *trav = xl->children;
    int32_t count = 0;
    /* Get the number of child count */
    while (trav) {
      count++;
      trav = trav->next;
    }
    _private->child_count = count;
    gf_log ("unify", GF_LOG_DEBUG, "unify.c->init: Child node count is %d", count);
    _private->array = (struct xlator **)calloc (1, sizeof (struct xlator *) * count);
    count = 0;
    trav = xl->children;
    /* Update the child array */
    while (trav) {
      _private->array[count++] = trav->xlator;
      trav = trav->next;
    }
  }

  if(lock_node) {
    xlator_list_t *trav = xl->children;

    gf_log ("unify", GF_LOG_DEBUG, "unify->init: lock server specified as %s", lock_node->data);

    while (trav) {
      if(strcmp (trav->xlator->name, lock_node->data) == 0)
	break;
      trav = trav->next;
    }
    if (trav == NULL) {
      gf_log("unify", GF_LOG_ERROR, "unify.c->init: lock server not found among the children");
      return -1;
    }
    _private->lock_node = trav->xlator;
  } else {
    gf_log ("unify", GF_LOG_DEBUG, "unify->init: lock server not specified, defaulting to %s", xl->children->xlator->name);
    _private->lock_node = xl->children->xlator;
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
  .getattr     = unify_getattr,
  .readlink    = unify_readlink,
  .mknod       = unify_mknod,
  .mkdir       = unify_mkdir,
  .unlink      = unify_unlink,
  .rmdir       = unify_rmdir,
  .symlink     = unify_symlink,
  .rename      = unify_rename,
  .link        = unify_link,
  .chmod       = unify_chmod,
  .chown       = unify_chown,
  .truncate    = unify_truncate,
  .utimes      = unify_utimes,
  .create      = unify_create,
  .open        = unify_open,
  .readv       = unify_readv,
  .writev      = unify_writev,
  .statfs      = unify_statfs,
  .flush       = unify_flush,
  .release     = unify_release,
  .fsync       = unify_fsync,
  .setxattr    = unify_setxattr,
  .getxattr    = unify_getxattr,
  .listxattr   = unify_listxattr,
  .removexattr = unify_removexattr,
  .opendir     = unify_opendir,
  .readdir     = unify_readdir,
  .releasedir  = unify_releasedir,
  .fsyncdir    = unify_fsyncdir,
  .access      = unify_access,
  .ftruncate   = unify_ftruncate,
  .fgetattr    = unify_fgetattr,
  .lk          = unify_lk,
};

struct xlator_mops mops = {
  .stats = unify_stats
};
