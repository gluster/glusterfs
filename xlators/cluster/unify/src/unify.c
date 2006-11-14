#include <libgen.h>
#include <unistd.h>

#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"

#define INIT_LOCK(x)   ;
#define LOCK(x)        ;
#define UNLOCK(x)      ;
#define LOCK_DESTROY(x) ;


#define GF_LOCK(xl, path) 

#define GF_UNLOCK(xl, path)


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
  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
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
  
  xlator_t *trav = xl->first_child;

  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  while (trav) {
    STACK_WIND (frame, 
		unify_setxattr_cbk,
		trav,
		trav->fops->setxattr,
		path, 
		name, 
		value, 
		size, 
		flags);
    trav = trav->next_sibling;
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

  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
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
    local->op_ret = 0;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->buf ? local->buf : "");
    if (local->buf)
      free (local->buf);
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
  xlator_t *trav = xl->first_child;
  
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_getxattr_cbk,
		trav,
		trav->fops->getxattr,
		path,
		name,
		size);
    trav = trav->next_sibling;
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

  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
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
    local->op_ret = 0;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->buf ? local->buf : "");
    if (local->buf)
      free (local->buf);
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
  xlator_t *trav = xl->first_child;
  
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  while (trav) {
    STACK_WIND (frame, 
		unify_listxattr_cbk,
		trav,
		trav->fops->listxattr,
		path,
		size);
    trav = trav->next_sibling;
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
  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
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
  xlator_t *trav = xl->first_child;

  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  while (trav) {
    STACK_WIND (frame, 
		unify_removexattr_cbk,
		xl->first_child,
		xl->first_child->fops->removexattr,
		path,
		name);
    trav = trav->next_sibling;
  }
  return 0;
} 


/* read */
static int32_t  
unify_read_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		char *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t  
unify_read (call_frame_t *frame,
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
	      unify_read_cbk,
	      child,
	      child->fops->read,
	      file_ctx,
	      size,
	      offset);
  return 0;
} 

/* write */
static int32_t  
unify_write_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t  
unify_write (call_frame_t *frame,
	     xlator_t *xl,
	     dict_t *file_ctx,
	     char *buf,
	     size_t size,
	     off_t offset)

{
  data_t *fd_data = dict_get (file_ctx, xl->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  xlator_t *child = (void *)((long) data_to_int (fd_data));

  STACK_WIND (frame, 
	      unify_write_cbk,
	      child,
	      child->fops->write,
	      file_ctx,
	      buf,
	      size,
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

  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  } else if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->stbuf = *stbuf;
    local->op_ret = 0;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
    LOCK_DESTROY (&frame->lock);
  }
  return 0;
}

static int32_t  
unify_getattr (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  xlator_t *trav = xl->first_child;
  
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_getattr_cbk,
		trav,
		trav->fops->getattr,
		path);
    trav = trav->next_sibling;
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
  if (op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret   = op_ret;
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
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs_buf);
    LOCK_DESTROY (&frame->lock);
  }
  return 0;
}


static int32_t  
unify_statfs (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  xlator_t *trav = xl->first_child;

  INIT_LOCK (&frame->mutex);

  while (trav) {
    STACK_WIND (frame, 
		unify_statfs_cbk,
		trav,
		trav->fops->statfs,
		path);
    trav = trav->next_sibling;
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

  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
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
    LOCK_DESTROY (&frame->lock);
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
  
  xlator_t *trav = xl->first_child;
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_truncate_cbk,
		trav,
		trav->fops->truncate,
		path,
		offset);
    trav = trav->next_sibling;
  }
  return 0;
} 

/* utime */
static int32_t  
unify_utime_cbk (call_frame_t *frame,
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

  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
    local->op_errno = op_errno;
  }

  if (op_ret == 0) {
    LOCK (&mutex->lock);
    local->stbuf = *stbuf;
    local->op_ret = 0;    
    UNLOCK (&mutex->lock);
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
    LOCK_DESTROY (&frame->lock);
  }
  return 0;
}

static int32_t  
unify_utime (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     struct utimbuf *buf)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  xlator_t *trav = xl->first_child;
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  while (trav) {
    STACK_WIND (frame, 
		unify_utime_cbk,
		trav,
		trav->fops->utime,
		path,
		buf);
    trav = trav->next_sibling;
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
  STACK_WIND (frame, 
	      unify_opendir_getattr_cbk,
	      xl->first_child,
	      xl->first_child->fops->getattr,
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

  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
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
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->buf ? local->buf : "");

    if (local->buf)
      free (local->buf);

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
  
  xlator_t *trav = xl->first_child;
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  
  while (trav) {
    STACK_WIND (frame, 
		unify_readlink_cbk,
		trav,
		trav->fops->readlink,
		path,
		size);
    trav = trav->next_sibling;
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
    dir_entry_t *trav = entry->next;
    dir_entry_t *prev = entry;
    dir_entry_t *tmp;
    if (local->call_count == 1) {
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
	if (S_ISDIR (tmp->buf.st_mode) || S_ISLNK (tmp->buf.st_mode)) {
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
  }
  if (op_ret == -1 && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->entry, local->count);
    dir_entry_t *prev = local->entry;
    dir_entry_t *trav = prev->next;
    while (trav) {
      prev->next = trav->next;
      free (trav->name);
      free (trav);
      trav = prev->next;
    }
    free (local->entry);
  }
  return 0;
}

static int32_t  
unify_readdir (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  xlator_t *trav = xl->first_child;
  
  INIT_LOCK (&frame->mutex);

  while (trav) {
    STACK_WIND (frame,
		unify_readdir_cbk,
		trav,
		trav->fops->readdir,
		path);
    trav = trav->next_sibling;
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
  STACK_UNWIND (frame, 
		local->op_ret,
		local->op_errno,
		&local->stbuf);
  free (local->path);
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
		xl->first_child,
		xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = 0;
    local->op_errno = 0;
    while (trav) {
      STACK_WIND (frame,
		  unify_mkdir_cbk,
		  trav,
		  trav->fops->mkdir,
		  local->path,
		  local->mode);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->path);
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
  if (op_ret == -1 && (op_errno != ENOENT || op_errno != ENOTCONN)) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_unlink_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    while (trav) {
      STACK_WIND (frame,
		  unify_unlink_cbk,
		  trav,
		  trav->fops->unlink,
		  local->path);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->path);
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
		xl->first_child,
		xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = 0;
    local->op_errno = 0;
    while (trav) {
      STACK_WIND (frame,
		  unify_rmdir_cbk,
		  trav,
		  trav->fops->rmdir,
		  local->path);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
	      path);
  return 0;
} 


/* open */
static int32_t  
unify_open_unlock_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;

  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		local->file_ctx,
		&local->stbuf);

  free (local->path);
  return 0;
}

		
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
  if (op_ret != 0 && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret >= 0) {
    // put the child node's address in ctx->contents
    dict_set (file_ctx,
	      xl->name,
	      int_to_data ((long)prev_frame->this));
    local->file_ctx = file_ctx;
    local->stbuf = *stbuf;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_open_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
		local->path);
  }
  return 0;
}

static int32_t  
unify_open_lock_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  if (op_ret == 0) {
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_open_cbk,
		  trav,
		  trav->fops->open,
		  local->path,
		  local->flags,
		  local->mode);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
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
  frame->local = calloc (1, sizeof (unify_local_t));
  
  unify_local_t *local = (unify_local_t *)frame->local;
  
  local->path = strdup (path);
  local->flags = flags;
  local->mode = mode;

  STACK_WIND (frame, 
	      unify_open_lock_cbk,
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		local->file_ctx,
		&local->stbuf);
  
  free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->unlock,
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
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    
    while (trav) {
      STACK_WIND (frame,
		  unify_create_getattr_cbk,
		  trav,
		  trav->fops->getattr,
		  local->path);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->unlock,
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
    local->op_ret = 0;
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
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_mknod_getattr_cbk,
		  trav,
		  trav->fops->getattr,
		  local->path);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
  free (local->new_path);
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
  if (op_ret == 0) { 
    LOCK (&frame->mutex);
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  STACK_WIND (frame,
	      unify_symlink_unlock_cbk,
	      xl->first_child,
	      xl->first_child->mops->unlock,
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
  unify_local_t *local = (unify_local_t *)frame->local;

  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = -1;
    local->op_errno = EEXIST;
  }

  if ((op_ret == -1 && op_errno != ENOENT) || (op_ret == 0)) {
    LOCK (&frame->mutex);
    local->op_ret = 0;
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
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    while (trav) {
      STACK_WIND (frame,
		  unify_symlink_getattr_cbk,
		  trav,
		  trav->fops->getattr,
		  local->new_path);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
    free (local->new_path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->buf);
  free (local->path);
  free (local->new_path);
  return 0;
}

static int32_t
unify_rename_unlink_newpath_cbk (call_frame_t *frame,
				 call_frame_t *prev_frame,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  STACK_WIND (frame,
	      unify_rename_unlock_cbk,
	      this->first_child,
	      this->first_child->mops->unlock,
	      local->buf);
  return 0;
}

static int32_t  
unify_rename_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
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
		xl->first_child,
		xl->first_child->mops->unlock,
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
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->found_xl = prev_frame->this;
  }

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1 && local->op_errno == ENOENT) {
      STACK_WIND (frame,
		  unify_rename_cbk,
		  local->sched_xl,
		  local->sched_xl->fops->rename,
		  local->path,
		  local->new_path);
    } else {
      STACK_WIND (frame,
		  unify_rename_unlock_cbk,
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    if (op_ret == 0) {
      xlator_t *trav = xl->first_child;
      local->op_ret = -1;
      local->op_errno = ENOENT;

      local->call_count = 0;

      while (trav) {
	STACK_WIND (frame,
		    unify_rename_newpath_lookup_cbk,
		    trav,
		    trav->fops->getattr,
		    local->new_path);
	trav = trav->next_sibling;
      } 
    } else {
      local->op_ret = -1;
      local->op_errno = ENOENT;

      STACK_WIND (frame,
		  unify_rename_unlock_cbk,
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_rename_oldpath_lookup_cbk,
		  trav,
		  trav->fops->getattr,
		  local->path);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno);
    free (local->new_path);
    free (local->buf);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->buf);
  free (local->path);
  free (local->new_path);
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
  if (op_ret == 0) {
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
  }
  local->op_ret = op_ret;
  local->op_errno = op_errno;
  STACK_WIND (frame,
	      unify_link_unlock_cbk,
	      xl->first_child,
	      xl->first_child->mops->unlock,
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
    local->op_ret = 0;
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
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    if (op_ret == 0) {
      xlator_t *trav = xl->first_child;
      INIT_LOCK (&frame->mutex);
      local->op_ret = -1;
      local->op_errno = ENOENT;
      local->call_count = 0;
      
      while (trav) {
	STACK_WIND (frame,
		    unify_link_newpath_lookup_cbk,
		    trav,
		    trav->fops->getattr,
		    local->new_path);
	trav = trav->next_sibling;
      } 
    } else {
      /* op_ret == -1; */
      STACK_WIND (frame,
		  unify_link_unlock_cbk,
		  xl->first_child,
		  xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_link_oldpath_lookup_cbk,
		  trav,
		  trav->fops->getattr,
		  local->path);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->buf);
    free (local->path);
    free (local->new_path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
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

  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0) {
    local->stbuf = *stbuf;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_chmod_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;
    while (trav) {
      STACK_WIND (frame,
		  unify_chmod_cbk,
		  trav,
		  trav->fops->chmod,
		  local->path,
		  local->mode);
      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  free (local->path);
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
  if (op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    local->stbuf = *stbuf;
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_chown_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
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
    xlator_t *trav = xl->first_child;
    INIT_LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = ENOENT;

    while (trav) {
      STACK_WIND (frame,
		  unify_chown_cbk,
		  trav,
		  trav->fops->chown,
		  local->path,
		  local->uid,
		  local->gid);

      trav = trav->next_sibling;
    }
  } else {
    struct stat nullbuf = {0, };
    STACK_UNWIND (frame, -1, op_errno, &nullbuf);
    free (local->path);
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
	      xl->first_child,
	      xl->first_child->mops->lock,
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
  data_t *debug = dict_get (xl->options, "debug");
  data_t *scheduler = dict_get (xl->options, "scheduler");

  if (!scheduler) {
    gf_log ("unify", GF_LOG_ERROR, "unify.c->init: scheduler option is not provided\n");
    return -1;
  }

  if (!xl->first_child) {
    gf_log ("unify",
	    GF_LOG_ERROR,
	    "FATAL - unify configured without children. cannot continue");
    return -1;
  }

  _private->sched_ops = get_scheduler (scheduler->data);

  _private->is_debug = 0;
  if (debug && strcasecmp (debug->data, "on") == 0) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("unify", GF_LOG_DEBUG, "unify.c->init: debug mode on\n");
  }
  
  /* update _private structure */
  {
    struct xlator *trav_xl = xl->first_child;
    int32_t count = 0;
    /* Get the number of child count */
    while (trav_xl) {
      count++;
      trav_xl = trav_xl->next_sibling;
    }
    _private->child_count = count;
    _private->array = (struct xlator **)calloc (1, sizeof (struct xlator *) * count);
    count = 0;
    trav_xl = xl->first_child;
    /* Update the child array */
    while (trav_xl) {
      _private->array[count++] = trav_xl;
      trav_xl = trav_xl->next_sibling;
    }
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
  .utime       = unify_utime,
  .create      = unify_create,
  .open        = unify_open,
  .read        = unify_read,
  .write       = unify_write,
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
};

struct xlator_mops mops = {
  .stats = unify_stats
};
