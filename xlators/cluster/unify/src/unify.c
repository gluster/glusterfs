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


/* setxattr */
int32_t 
unify_setxattr_cbk (call_frame_t *frame,
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
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_getxattr_cbk (call_frame_t *frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    void *value)
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
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, value);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_listxattr_cbk (call_frame_t *frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
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
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, value);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_removexattr_cbk (call_frame_t *frame,
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
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_read_cbk (call_frame_t *frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		char *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

int32_t 
unify_read (call_frame_t *frame,
	    xlator_t *xl,
	    file_ctx_t *ctx,
	    size_t size,
	    off_t offset)
{
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT, "");
    return -1;
  }  
  xlator_t *child = (xlator_t *)tmp->context;

  STACK_WIND (frame, 
	      unify_read_cbk,
	      child,
	      child->fops->read,
	      ctx,
	      size,
	      offset);
  return 0;
} 

/* write */
int32_t 
unify_write_cbk (call_frame_t *frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
unify_write (call_frame_t *frame,
	     xlator_t *xl,
	     file_ctx_t *ctx,
	     char *buf,
	     size_t size,
	     off_t offset)

{
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT);
    return -1;
  }
  xlator_t *child = (xlator_t *)tmp->context;

  STACK_WIND (frame, 
	      unify_write_cbk,
	      child,
	      child->fops->write,
	      ctx,
	      buf,
	      size,
	      offset);
  return 0;
} 


/* ftruncate */
int32_t 
unify_ftruncate_cbk (call_frame_t *frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
unify_ftruncate (call_frame_t *frame,
		 xlator_t *xl,
		 file_ctx_t *ctx,
		 off_t offset)
{
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT);
    return -1;
  }
  xlator_t *child = (xlator_t *)tmp->context;

  STACK_WIND (frame, 
	      unify_ftruncate_cbk,
	      child,
	      child->fops->ftruncate,
	      ctx,
	      offset);
  return 0;
} 


/* fgetattr */
int32_t 
unify_fgetattr_cbk (call_frame_t *frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

int32_t 
unify_fgetattr (call_frame_t *frame,
		xlator_t *xl,
		file_ctx_t *ctx)
{
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT);
    return -1;
  }
  xlator_t *child = (xlator_t *)tmp->context;

  STACK_WIND (frame, 
	      unify_fgetattr_cbk,
	      child,
	      child->fops->fgetattr,
	      ctx);
  return 0;
} 

/* flush */
int32_t 
unify_flush_cbk (call_frame_t *frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
unify_flush (call_frame_t *frame,
	     xlator_t *xl,
	     file_ctx_t *ctx)
{
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT);
    return -1;
  }
  xlator_t *child = (xlator_t *)tmp->context;
  
  STACK_WIND (frame, 
	      unify_flush_cbk,
	      child,
	      child->fops->flush,
	      ctx);
  return 0;
} 

/* release */
int32_t 
unify_release_cbk (call_frame_t *frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  file_ctx_t *tmp = NULL;
  RM_MY_CTX ((local->ctx), tmp);
  free (tmp);

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
unify_release (call_frame_t *frame,
	       xlator_t *xl,
	       file_ctx_t *ctx)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  unify_local_t *local = (unify_local_t *)frame->local;
  
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT);
    return -1;
  }
  xlator_t *child = (xlator_t *)tmp->context;
  local->ctx = ctx;

  STACK_WIND (frame, 
	      unify_release_cbk,
	      child,
	      child->fops->release,
	      ctx);

  return 0;
} 


/* fsync */
int32_t 
unify_fsync_cbk (call_frame_t *frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
unify_fsync (call_frame_t *frame,
	     xlator_t *xl,
	     file_ctx_t *ctx,
	     int32_t flags)
{
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  if (!tmp) {
    STACK_UNWIND (frame, -1, ENOENT);
    return -1;
  }
  xlator_t *child = (xlator_t *)tmp->context;
  
  STACK_WIND (frame, 
	      unify_fsync_cbk,
	      child,
	      child->fops->fsync,
	      ctx,
	      flags);
  
  return 0;
} 

/* getattr */
int32_t 
unify_getattr_cbk (call_frame_t *frame,
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
    LOCK (&frame->mutex);
    local->op_ret = 0;
    local->stbuf = stbuf;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stbuf);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_statfs_cbk (call_frame_t *frame,
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
    struct statvfs *dict_buf = local->statvfs_buf;
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
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->statvfs_buf);
    free (local->statvfs_buf);
  }
  return 0;
}


int32_t 
unify_statfs (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path)
{
  frame->local = (void *)calloc (1, sizeof (unify_local_t));
  struct statvfs *stbuf = calloc (1, sizeof (*stbuf));
  unify_local_t *local = (unify_local_t *)frame->local;
  xlator_t *trav = xl->first_child;

  INIT_LOCK (&frame->mutex);
  local->statvfs_buf = stbuf;

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
int32_t 
unify_truncate_cbk (call_frame_t *frame,
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
    local->stbuf = stbuf;
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stbuf);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_utime_cbk (call_frame_t *frame,
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
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    local->op_ret = 0;
    local->stbuf = stbuf;
  }
  
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stbuf);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_opendir_getattr_cbk (call_frame_t *frame,
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

int32_t 
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
int32_t 
unify_readlink_cbk (call_frame_t *frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    char *buf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret == -1 && op_errno != ENOENT) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  } else if (op_ret == 0) {
    local->op_ret = 0;
    local->buf = buf;
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->buf);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_readdir_cbk (call_frame_t *frame,
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
    dir_entry_t *trav = entry;
    dir_entry_t *tmp;
    if (local->call_count == 1) {
      while (trav->next) 
	trav = trav->next;
      local->entry = entry;
      local->last = trav;
      local->count = count;
    } else {
      // copy only file names
      while (trav->next) {
	tmp  = trav->next;
	if (S_ISDIR (tmp->buf.st_mode)) {
	  trav->next = tmp->next;
	  free (tmp->name);
	  free (tmp);
	  count--;
	}
	trav = trav->next;
      }
      // append the current dir_entry_t at the end of the last node
      local->last->next = entry->next;
      local->count += count;
      local->last = trav;
    }
  }
  if (op_ret == -1) {
    LOCK (&frame->mutex);
    local->op_ret = -1;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->entry);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_mkdir_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->path);
  return 0;
}

int32_t 
unify_mkdir_cbk (call_frame_t *frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
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
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_mkdir_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
		local->path);
  }
  return 0;
}

int32_t 
unify_mkdir_lock_cbk (call_frame_t *frame,
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
		  unify_mkdir_cbk,
		  trav,
		  trav->fops->mkdir,
		  local->path,
		  local->mode);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_unlink_unlock_cbk (call_frame_t *frame,
			 xlator_t *xl,
			 int32_t op_ret,
			 int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);

  return 0;
}

int32_t 
unify_unlink_cbk (call_frame_t *frame,
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

int32_t 
unify_unlink_lock_cbk (call_frame_t *frame,
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

int32_t 
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
int32_t 
unify_rmdir_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->path);
  return 0;
}

int32_t 
unify_rmdir_cbk (call_frame_t *frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
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

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_rmdir_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
		local->path);
  }
  return 0;
}

int32_t 
unify_rmdir_lock_cbk (call_frame_t *frame,
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

int32_t 
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
int32_t 
unify_open_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		local->ctx,
		local->stbuf);

  free (local->path);
  return 0;
}

		
int32_t 
unify_open_cbk (call_frame_t *frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		file_ctx_t *ctx,
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
    LOCK (&frame->mutex);
    local->op_ret = 0;
    local->ctx = ctx;
    UNLOCK (&frame->mutex);
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

int32_t 
unify_open_lock_cbk (call_frame_t *frame,
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
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
  }
  
  return 0;
}

int32_t 
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
int32_t 
unify_create_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		local->ctx,
		local->stbuf);
  
  free (local->path);
  return 0;
}


int32_t 
unify_create_cbk (call_frame_t *frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  file_ctx_t *ctx,
		  struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  local->op_ret = op_ret;
  local->op_errno = op_errno;
  local->ctx = ctx;
  local->stbuf = stbuf;

  STACK_WIND (frame,
	      unify_create_unlock_cbk,
	      xl->first_child,
	      xl->first_child->mops->unlock,
	      local->path);
  return 0;
}

int32_t 
unify_create_getattr_cbk (call_frame_t *frame,
			  xlator_t *xl,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if ((op_ret == -1 && op_errno != ENOENT) || op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = 0;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1) {
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
    } else {
      local->op_ret = -1;
      local->op_errno = EEXIST;
      STACK_WIND (frame,
		  unify_create_unlock_cbk,
		  xl->first_child,
		  xl->first_child->mops->unlock,
		  local->path);
    }
  }
  return 0;
}

int32_t 
unify_create_lock_cbk (call_frame_t *frame,
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
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
  }
  
  return 0;
}

int32_t 
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
	      unify_open_lock_cbk,
	      xl->first_child,
	      xl->first_child->mops->lock,
	      path);
  return 0;
} 


/* mknod */
int32_t 
unify_mknod_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  free (local->path);
  return 0;
}

int32_t 
unify_mknod_cbk (call_frame_t *frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_WIND (frame,
	      unify_mknod_unlock_cbk,
	      xl->first_child,
	      xl->first_child->mops->unlock,
	      local->path);
  return 0;
}

int32_t 
unify_mknod_getattr_cbk (call_frame_t *frame,
			 xlator_t *xl,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *stbuf)
{
  unify_local_t *local = (unify_local_t *)frame->local;
  
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if ((op_ret == -1 && op_errno != ENOENT) || op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = 0;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    if (local->op_ret == -1) {
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
      local->op_ret = -1;
      STACK_WIND (frame,
		  unify_mknod_unlock_cbk,
		  xl->first_child,
		  xl->first_child->mops->unlock,
		  local->path);
    }
  }
  return 0;
}


int32_t 
unify_mknod_lock_cbk (call_frame_t *frame,
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
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_symlink_unlock_cbk (call_frame_t *frame,
			  xlator_t *xl,
			  int32_t op_ret,
			  int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stbuf);
  free (local->path);
  free (local->new_path);
  return 0;
}

int32_t 
unify_symlink_cbk (call_frame_t *frame,
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
    LOCK (&frame->mutex);
    local->op_ret = 0;
    local->stbuf = stbuf;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_symlink_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
		local->new_path);
  }

  return 0;
}

int32_t 
unify_symlink_lock_cbk (call_frame_t *frame,
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
		  unify_symlink_cbk,
		  trav,
		  trav->fops->symlink,
		  local->path,
		  local->new_path);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno, NULL);
    free (local->path);
    free (local->new_path);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_rename_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  return 0;
}

int32_t 
unify_rename_cbk (call_frame_t *frame,
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
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_rename_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
		local->buf);
  }
  return 0;
}

int32_t 
unify_rename_lock_cbk (call_frame_t *frame,
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
		  unify_rename_cbk,
		  trav,
		  trav->fops->rename,
		  local->path,
		  local->new_path);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
    free (local->new_path);
    free (local->buf);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_link_unlock_cbk (call_frame_t *frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno);
  return 0;
}

int32_t 
unify_link_cbk (call_frame_t *frame,
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
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0)
    local->op_ret = 0;

  if (local->call_count == ((struct cement_private *)xl->private)->child_count) {
    STACK_WIND (frame,
		unify_link_unlock_cbk,
		xl->first_child,
		xl->first_child->mops->unlock,
		local->buf);
  }
  return 0;
}

int32_t 
unify_link_lock_cbk (call_frame_t *frame,
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
		  unify_link_cbk,
		  trav,
		  trav->fops->link,
		  local->path,
		  local->new_path);
      trav = trav->next_sibling;
    }
  } else {
    STACK_UNWIND (frame, -1, op_errno);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_chmod_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stbuf);
  free (local->path);
  return 0;
}

int32_t 
unify_chmod_cbk (call_frame_t *frame,
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
    local->stbuf = stbuf;
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

int32_t 
unify_chmod_lock_cbk (call_frame_t *frame,
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
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_chown_unlock_cbk (call_frame_t *frame,
			xlator_t *xl,
			int32_t op_ret,
			int32_t op_errno)
{ 
  unify_local_t *local = (unify_local_t *)frame->local;
  
  STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stbuf);
  free (local->path);
  return 0;
}

int32_t 
unify_chown_cbk (call_frame_t *frame,
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
    local->op_ret = 0;
    local->stbuf = stbuf;
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

int32_t 
unify_chown_lock_cbk (call_frame_t *frame,
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
    STACK_UNWIND (frame, -1, op_errno);
    free (local->path);
  }
  return 0;
}

int32_t 
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
int32_t 
unify_releasedir (call_frame_t *frame,
		  xlator_t *xl,
		  file_ctx_t *ctx)
{
  return 0;
} 

/* fsyncdir */ 
int32_t 
unify_fsyncdir (call_frame_t *frame,
		xlator_t *xl,
		file_ctx_t *ctx,
		int32_t flags)
{
  return 0;
}

/* access */
int32_t 
unify_access (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  return 0;
}

int32_t 
unify_stats (call_frame_t *frame,
	     struct xlator *xl,
	     int32_t flags)
{
  errno = ENOSYS;
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
    exit (1);
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
