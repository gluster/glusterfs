/*
 * (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/*
 * TODO:
 * 1) writev assumes that the calls on the children will write entire
 *    buffer. We need to see how we can handle the case where
 *    one of the children writes less than the buffer.
 * 2) Check the FIXMEs
 *
 */

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"

#define LOCK_INIT(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);

/* #define AFR_DEBUG(format, args...) gf_log ("afr", GF_LOG_DEBUG, "%s() " format, __func__, ##args); */
#define AFR_DEBUG(format, args...) ;

static int32_t
afr_get_num_copies (const char *path, xlator_t *xl)
{
  pattern_info_t *tmp = ((afr_private_t *)xl->private)->pattern_info_list;
  int32_t pil_num = ((afr_private_t *)xl->private)->pil_num;
  int32_t count = 0;
  char *fname1, *fname2;
  fname1 = fname2 = (char *) path;
  while (*fname2) {
    if (*fname2 == '/')
      fname1 = fname2 + 1;
    fname2++;
  }

  for (count = 0; count < pil_num; count++) {
    if (fnmatch (tmp->pattern, fname1, 0) == 0) {
      gf_log ("afr", GF_LOG_DEBUG, "matched! pattern = %s, filename = %s,", tmp->pattern, fname1);
      return tmp->copies;
    }
    tmp++;
  }
  return 1;
}

static int32_t
afr_setxattr_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = (afr_local_t *) frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_setxattr (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      const char *name,
	      const char *value,
	      size_t size,
	      int32_t flags)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);

  xlator_list_t *trav = xl->children;
  while(trav) {
    STACK_WIND(frame,
	       afr_setxattr_cbk,
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

static int32_t
afr_getxattr_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  void *value)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_getxattr_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->getxattr,
		local->path,
		local->name,
		local->size);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
afr_getxattr (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      const char *name,
	      size_t size)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  local->name = name;
  local->size = size;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_getxattr_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->getxattr,
	      path,
	      name,
	      size);
  return 0;
}

static int32_t
afr_listxattr_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   void *value)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_listxattr_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->listxattr,
		local->path,
		local->size);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
afr_listxattr (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path,
	       size_t size)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof(afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  local->size = size;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_listxattr_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->listxattr,
	      path,
	      size);
  return 0;
}

static int32_t
afr_removexattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_removexattr (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 const char *name)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  while(trav) {
    STACK_WIND (frame,
		afr_removexattr_cbk,
		trav->xlator,
		trav->xlator->fops->removexattr,
		path,
		name);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_open_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *xl,
	      int32_t op_ret,
	      int32_t op_errno,
	      dict_t *file_ctx,
	      struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  dict_t *ctx = local->ctx;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (op_ret == 0)
    dict_set (ctx, prev_frame->this->name, int_to_data((long)file_ctx));

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, ctx, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_open (call_frame_t *frame,
	  xlator_t *xl,
	  const char *path,
	  int32_t flags,
	  mode_t mode)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  local->ctx = get_new_dict ();
  while (trav) {
    STACK_WIND (frame,
		afr_open_cbk,
		trav->xlator,
		trav->xlator->fops->open,
		path,
		flags,
		mode);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_readv_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct iovec *vector,
	       int32_t count)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    data_t *ctx_data = NULL;
    LOCK(&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK(&frame->mutex);
    while (local->xlnodeptr) {
      ctx_data = dict_get (local->ctx, local->xlnodeptr->xlator->name);
      if (ctx_data)
	break;
      LOCK(&frame->mutex);
      local->xlnodeptr = local->xlnodeptr->next;
      UNLOCK(&frame->mutex);
    }
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      STACK_WIND (frame,
		  afr_readv_cbk,
		  local->xlnodeptr->xlator,
		  local->xlnodeptr->xlator->fops->readv,
		  ctx,
		  local->size,
		  local->offset);
      return 0;
    }
  }
      
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t
afr_readv (call_frame_t *frame,
	   xlator_t *xl,
	   dict_t *file_ctx,
	   size_t size,
	   off_t offset)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof(afr_local_t));
  data_t *ctx_data = NULL;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK(&frame->mutex);
  local->xlnodeptr = xl->children;
  local->ctx = file_ctx;
  local->size = size;
  local->offset = offset;
  UNLOCK(&frame->mutex);
  while(local->xlnodeptr){
    ctx_data = dict_get (file_ctx, local->xlnodeptr->xlator->name);
    if (ctx_data)
      break;
    LOCK(&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK(&frame->mutex);
  }
  if (ctx_data == NULL) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, -1, ENOENT, NULL, 0);
    return 0;
  }
  dict_t *ctx = (void *)((long)data_to_int(ctx_data));

  STACK_WIND (frame, 
	      afr_readv_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->readv,
	      ctx,
	      size,
	      offset);
  return 0;
}

static int32_t
afr_writev_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
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
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (op_ret != -1 && local->op_ret == -1) { /* check if op_ret is 0 means not write error */
    /* FIXME: assuming that all child write call writes all data */
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_writev (call_frame_t *frame,
	    xlator_t *xl,
	    dict_t *file_ctx,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  xlator_list_t *trav = xl->children;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      LOCK (&frame->mutex);
      local->call_count--;
      UNLOCK (&frame->mutex);
      STACK_WIND(frame,
		 afr_writev_cbk,
		 trav->xlator,
		 trav->xlator->fops->writev,
		 ctx,
		 vector,
		 count,
		 offset);
    }
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_ftruncate_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_ftruncate (call_frame_t *frame,
	       xlator_t *xl,
	       dict_t *file_ctx,
	       off_t offset)
{
  AFR_DEBUG();
  xlator_list_t *trav = xl->children;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      LOCK (&frame->mutex);
      local->call_count--;
      UNLOCK (&frame->mutex);
      STACK_WIND (frame,
		  afr_ftruncate_cbk,
		  trav->xlator,
		  trav->xlator->fops->ftruncate,
		  ctx,
		  offset);
    }
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_fgetattr_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    data_t *ctx_data = NULL;
    LOCK(&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK(&frame->mutex);
    while (local->xlnodeptr) {
      ctx_data = dict_get (local->ctx, local->xlnodeptr->xlator->name);
      if (ctx_data)
	break;
      LOCK(&frame->mutex);
      local->xlnodeptr = local->xlnodeptr->next;
      UNLOCK(&frame->mutex);
    }
    if (ctx_data) { /* if local->xlnodeptr is NULL then ctx_data is also NULL */
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      STACK_WIND (frame,
		  afr_fgetattr_cbk,
		  local->xlnodeptr->xlator,
		  local->xlnodeptr->xlator->fops->fgetattr,
		  ctx);
      return 0;
    }
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_fgetattr (call_frame_t *frame,
	      xlator_t *xl,
	      dict_t *file_ctx)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof(afr_local_t));
  data_t *ctx_data = NULL;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK(&frame->mutex);
  local->xlnodeptr = xl->children;
  UNLOCK(&frame->mutex);
  while(local->xlnodeptr){
    ctx_data = dict_get (file_ctx, local->xlnodeptr->xlator->name);
    if (ctx_data)
      break;
    LOCK(&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK(&frame->mutex);
  }
  if (ctx_data == NULL) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  dict_t *ctx = (void *)((long)data_to_int(ctx_data));

  STACK_WIND (frame, 
	      afr_fgetattr_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->fgetattr,
	      ctx);
  return 0;
}

static int32_t
afr_flush_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_flush (call_frame_t *frame,
	   xlator_t *xl,
	   dict_t *file_ctx)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  xlator_list_t *trav = xl->children;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while(trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if(ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      LOCK (&frame->mutex);
      local->call_count--;
      UNLOCK (&frame->mutex);
      STACK_WIND (frame,
		  afr_flush_cbk,
		  trav->xlator,
		  trav->xlator->fops->flush,
		  ctx);
    }
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_release_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_release (call_frame_t *frame,
	     xlator_t *xl,
	     dict_t *file_ctx)
{
  AFR_DEBUG();
  xlator_list_t *trav = xl->children;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  frame->local = local;
  LOCK_INIT (&frame->mutex);
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if(ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      LOCK (&frame->mutex);
      local->call_count--;
      UNLOCK (&frame->mutex);
      STACK_WIND (frame,
		  afr_release_cbk,
		  trav->xlator,
		  trav->xlator->fops->release,
		  ctx);
    }
    trav = trav->next;
  }
  dict_destroy (file_ctx);
  return 0;
}

static int32_t
afr_fsync_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_fsync (call_frame_t *frame,
	   xlator_t *xl,
	   dict_t *file_ctx,
	   int32_t flags)
{
  AFR_DEBUG();
  xlator_list_t *trav = xl->children;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      LOCK (&frame->mutex);
      local->call_count--;
      UNLOCK (&frame->mutex);
      STACK_WIND (frame,
		  afr_fsync_cbk,
		  trav->xlator,
		  trav->xlator->fops->fsync,
		  ctx,
		  flags);
    }
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_lk_cbk (call_frame_t *frame,
	    call_frame_t *prev_frame,
	    xlator_t *xl,
	    int32_t op_ret,
	    int32_t op_errno,
	    struct flock *lock)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->lock, lock, sizeof (struct flock));
    UNLOCK (&frame->mutex);
  }
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->lock);
  }
  return 0;
}

static int32_t
afr_lk (call_frame_t *frame,
	xlator_t *xl,
	dict_t *file_ctx,
	int32_t cmd,
	struct flock *lock)
{
  AFR_DEBUG();
  xlator_list_t *trav = xl->children;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      LOCK (&frame->mutex);
      local->call_count--;
      UNLOCK (&frame->mutex);
      STACK_WIND (frame,
		  afr_lk_cbk,
		  trav->xlator,
		  trav->xlator->fops->lk,
		  ctx,
		  cmd,
		  lock);
    }
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_getattr_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_getattr_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->getattr,
		local->path);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_getattr (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_getattr_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->getattr,
	      path);
  return 0;
}

static int32_t
afr_statfs_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_statfs_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->statfs,
		local->path);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_statfs (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof(afr_local_t));
  frame->local = local;
  LOCK_INIT (&frame->mutex);
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_statfs_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->statfs,
	      path);
  return 0;
}

static int32_t
afr_truncate_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_truncate (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      off_t offset)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  while (trav) {
    STACK_WIND (frame,
		afr_truncate_cbk,
		trav->xlator,
		trav->xlator->fops->truncate,
		path,
		offset);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_utimes_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_utimes (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path,
	    struct timespec *buf)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof(afr_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  LOCK_INIT (&frame->mutex);
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);

  while (trav) {
    STACK_WIND (frame,
		afr_utimes_cbk,
		trav->xlator,
		trav->xlator->fops->utimes,
		path,
		buf);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_opendir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_opendir_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->opendir,
		local->path);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
afr_opendir (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  AFR_DEBUG();
  afr_local_t *local = calloc (1, sizeof(afr_local_t));
  frame->local = local;
  LOCK_INIT (&frame->mutex);
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_opendir_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->opendir,
	      path);
  return 0;
}

static int32_t
afr_readlink_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  char *buf)
{
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_readlink_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->readlink,
		local->path,
		local->size);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
afr_readlink (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      size_t size)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT(&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  local->size = size;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_readlink_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->readlink,
	      path,
	      size);
  return 0;
}

static int32_t
afr_readdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 dir_entry_t *entry,
		 int32_t count)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_readdir_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->readdir,
		local->path);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, entry, count);
  return 0;
}

static int32_t
afr_readdir (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->path = path;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_readdir_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->readdir,
	      path);
  return 0;
}

static int32_t
afr_mkdir_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path,
	   mode_t mode)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  while (trav) {
    STACK_WIND (frame,
		afr_mkdir_cbk,
		trav->xlator,
		trav->xlator->fops->mkdir,
		path,
		mode);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_unlink_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_unlink (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  while (trav) {
    STACK_WIND (frame,
		afr_unlink_cbk,
		trav->xlator,
		trav->xlator->fops->unlink,
		path);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_rmdir_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*) xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_rmdir (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  while (trav) {
    STACK_WIND (frame,
		afr_rmdir_cbk,
		trav->xlator,
		trav->xlator->fops->rmdir,
		path);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_create_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *file_ctx,
		struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  dict_t *ctx = local->ctx;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if(op_ret == 0)
    dict_set (ctx, prev_frame->this->name, int_to_data((long)file_ctx));

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, ctx, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_create (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path,
	    mode_t mode)
{
  AFR_DEBUG("path=%s, mode=%x", path, mode);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->ctx = get_new_dict ();
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  int32_t num_copies = afr_get_num_copies (path, xl);
  if (num_copies == 0)
    num_copies = 1;
  LOCK (&frame->mutex);
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  UNLOCK (&frame->mutex);
  while (trav) {
    LOCK (&frame->mutex);
    local->call_count--;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_create_cbk,
		trav->xlator,
		trav->xlator->fops->create,
		path,
		mode);
    trav = trav->next;
    num_copies--;
    if (num_copies == 0)
      break;
  }
  return 0;
}

static int32_t
afr_mknod_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_mknod (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path,
	   mode_t mode,
	   dev_t dev)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT(&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;

  while (trav) {
    STACK_WIND (frame,
		afr_mknod_cbk,
		trav->xlator,
		trav->xlator->fops->mknod,
		path,
		mode,
		dev);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_symlink_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *xl,
	     const char *oldpath,
	     const char *newpath)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  /* FIXME: need to check the existance of dest file before creating
   * the link
   */

  while (trav) {
    STACK_WIND (frame,
		afr_symlink_cbk,
		trav->xlator,
		trav->xlator->fops->symlink,
		oldpath,
		newpath);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_rename_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_rename (call_frame_t *frame,
	    xlator_t *xl,
	    const char *oldpath,
	    const char *newpath)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;

  while (trav) {
    STACK_WIND (frame,
		afr_rename_cbk,
		trav->xlator,
		trav->xlator->fops->rename,
		oldpath,
		newpath);
    trav = trav->next;
  }
  return 0;
}

/* FIXME: check FIXME of symlink call */

static int32_t
afr_link_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *xl,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_link (call_frame_t *frame,
	  xlator_t *xl,
	  const char *oldpath,
	  const char *newpath)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;
  while (trav) {
    STACK_WIND (frame,
		afr_link_cbk,
		trav->xlator,
		trav->xlator->fops->link,
		oldpath,
		newpath);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_chmod_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret !=0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_chmod (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path,
	   mode_t mode)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;

  while (trav) {
    STACK_WIND (frame,
		afr_chmod_cbk,
		trav->xlator,
		trav->xlator->fops->chmod,
		path,
		mode);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_chown_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  LOCK (&frame->mutex);
  local->call_count++;
  UNLOCK (&frame->mutex);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    LOCK (&frame->mutex);
    local->op_errno = op_errno;
    UNLOCK (&frame->mutex);
  }
  if (op_ret == 0 && local->op_ret != 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    memcpy (&local->stbuf, stbuf, sizeof (struct stat));
    UNLOCK (&frame->mutex);
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_chown (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path,
	   uid_t uid,
	   gid_t gid)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  UNLOCK (&frame->mutex);
  xlator_list_t *trav = xl->children;

  while (trav) {
    STACK_WIND (frame,
		afr_chown_cbk,
		trav->xlator,
		trav->xlator->fops->chown,
		path,
		uid,
		gid);
    trav = trav->next;
  }
  return 0;
}

/* releasedir */
static int32_t
afr_releasedir (call_frame_t *frame,
		xlator_t *xl,
		dict_t *ctx)
{
  AFR_DEBUG();
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

/* fsyncdir */
static int32_t
afr_fsyncdir (call_frame_t *frame,
	      xlator_t *xl,
	      dict_t *ctx,
	      int32_t flags)
{
  AFR_DEBUG();
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

/* access */
static int32_t
afr_access (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path,
	    mode_t mode)
{
  AFR_DEBUG();
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

static int32_t
afr_lock_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *xl,
	      int32_t op_ret,
	      int32_t op_errno)
{
  AFR_DEBUG();
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
afr_lock (call_frame_t *frame,
	  xlator_t *xl,
	  const char *path)
{
  AFR_DEBUG();
  xlator_t *lock_node = ((afr_private_t *)xl->private)->lock_node;

  STACK_WIND (frame,
	      afr_lock_cbk,
	      lock_node,
	      lock_node->mops->lock,
	      path);
  return 0;
}

static int32_t
afr_unlock_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG();
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
afr_unlock (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path)
{
  xlator_t *lock_node = ((afr_private_t*) xl->private)->lock_node;
  STACK_WIND (frame,
	      afr_unlock_cbk,
	      lock_node,
	      lock_node->mops->unlock,
	      path);
  return 0;
}

static int32_t
afr_stats_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct xlator_stats *stats)
{
  AFR_DEBUG();
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_stats_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->mops->stats,
		local->flags);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}

static int32_t
afr_stats (call_frame_t *frame,
	   struct xlator *xl,
	   int32_t flags)
{
  AFR_DEBUG();
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  LOCK (&frame->mutex);
  local->xlnodeptr = xl->children;
  local->flags = flags;
  UNLOCK (&frame->mutex);
  STACK_WIND (frame,
	      afr_stats_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->mops->stats,
	      flags);
  return 0;
}

void
afr_parse_replicate (char *data, xlator_t *xl)
{
  AFR_DEBUG();
  char *tok, *colon;
  int32_t num_tokens = 0;
  pattern_info_t *pattern_info_list;
  tok = data;
  while (*tok++){
    if(*tok == ',')
      num_tokens++;
  }
  num_tokens++; /* num_tokens is one more than number of ',' */
  tok = strtok (data, ",");
  if (!tok)
    return;
  pattern_info_list = calloc (num_tokens, sizeof (pattern_info_t));
  ((afr_private_t *)xl->private)->pattern_info_list = pattern_info_list;
  num_tokens = 0;
  do {
    colon = tok;
    while(*colon != ':')
      colon++;
    *colon = '\0';
    pattern_info_list[num_tokens].pattern = strdup (tok);
    pattern_info_list[num_tokens].copies = atoi (colon+1);
    AFR_DEBUG ("pattern \"%s\" copies \"%d\"", pattern_info_list[num_tokens].pattern,
	       pattern_info_list[num_tokens].copies);
    num_tokens++;
    tok = strtok (NULL, ",");
  } while(tok);  
  ((afr_private_t*)xl->private)->pil_num = num_tokens;
}

int32_t 
init (xlator_t *xl)
{
  afr_private_t *pvt = calloc (1, sizeof (afr_private_t));
  data_t *lock_node = dict_get (xl->options, "lock-node");
  data_t *replicate = dict_get (xl->options, "replicate");
  int32_t count = 0;
  xlator_list_t *trav = xl->children;

  xl->private = pvt;
  while (trav) {
    gf_log ("afr", GF_LOG_DEBUG, "xlator name is %s", trav->xlator->name);
    count++;
    trav = trav->next;
  }
  gf_log ("afr", GF_LOG_DEBUG, "child count %d", count);
  pvt->child_count = count;

  if (lock_node) {
    trav = xl->children;
    while (trav) {
      if (strcmp (trav->xlator->name, lock_node->data) == 0)
	break;
      trav = trav->next;
    }
    if (trav == NULL) {
      gf_log ("afr", GF_LOG_ERROR, "afr->init: lock-node not found among the children");
      return -1;
    }
    gf_log ("afr", GF_LOG_DEBUG, "lock node is %s\n", trav->xlator->name);
    pvt->lock_node = trav->xlator;
  } else {
    gf_log ("afr", GF_LOG_DEBUG, "afr->init: lock node not specified, defaulting to %s", xl->children->xlator->name);
    pvt->lock_node = xl->children->xlator;
  }

  if(replicate)
    afr_parse_replicate (replicate->data, xl);
  return 0;
}

void
fini(xlator_t *xl)
{
  free (((afr_private_t *)xl->private)->pattern_info_list);
  free (xl->private);
  return;
}

struct xlator_fops fops = {
  .getattr     = afr_getattr,
  .readlink    = afr_readlink,
  .mknod       = afr_mknod,
  .mkdir       = afr_mkdir,
  .unlink      = afr_unlink,
  .rmdir       = afr_rmdir,
  .symlink     = afr_symlink,
  .rename      = afr_rename,
  .link        = afr_link,
  .chmod       = afr_chmod,
  .chown       = afr_chown,
  .truncate    = afr_truncate,
  .utimes      = afr_utimes,
  .create      = afr_create,
  .open        = afr_open,
  .readv       = afr_readv,
  .writev      = afr_writev,
  .statfs      = afr_statfs,
  .flush       = afr_flush,
  .release     = afr_release,
  .fsync       = afr_fsync,
  .setxattr    = afr_setxattr,
  .getxattr    = afr_getxattr,
  .listxattr   = afr_listxattr,
  .removexattr = afr_removexattr,
  .opendir     = afr_opendir,
  .readdir     = afr_readdir,
  .releasedir  = afr_releasedir,
  .fsyncdir    = afr_fsyncdir,
  .access      = afr_access,
  .ftruncate   = afr_ftruncate,
  .fgetattr    = afr_fgetattr,
  .lk          = afr_lk,
};

struct xlator_mops mops = {
  .stats = afr_stats,
  .lock = afr_lock,
  .unlock = afr_unlock,
};
