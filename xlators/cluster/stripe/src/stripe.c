/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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


#include "xlator.h"

struct stripe_private {
  int32_t stripe_size;
  int32_t child_count;
};

struct stripe_local {
  /* Used by _cbk functions */
  int32_t call_count;
  int32_t wind_count; // used instead of child_cound in case of read and write */
  int32_t op_ret;
  int32_t op_errno; 
  int32_t count;
  struct stat stbuf;
  struct flock lock;
  struct iovec *vector;
  dir_entry_t *entry;
  
  /* For File I/O fops */
  dict_t *ctx;

  /* General usage */
  int32_t offset;
  int32_t node_index;
};

typedef struct stripe_local   stripe_local_t;
typedef struct stripe_private stripe_private_t;

static int32_t
stripe_setxattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno)
{
  stripe_local_t *local = (stripe_local_t *) frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }

  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_setxattr (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int32_t flags)
{
  stripe_local_t *local = (void *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND(frame,
	       stripe_setxattr_cbk,
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
stripe_getxattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
stripe_getxattr (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 const char *name,
		 size_t size)
{
  xlator_t *stripexl = xl->children->xlator;

  
  STACK_WIND (frame,
	      stripe_getxattr_cbk,
	      stripexl,
	      stripexl->fops->getxattr,
	      path,
	      name,
	      size);
  return 0;
}

static int32_t
stripe_listxattr_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno,
		      void *value)
{
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
stripe_listxattr (call_frame_t *frame,
		  xlator_t *xl,
		  const char *path,
		  size_t size)
{ 
  xlator_t *stripexl = xl->children->xlator;
  STACK_WIND (frame,
	      stripe_listxattr_cbk,
	      stripexl,
	      stripexl->fops->listxattr,
	      path,
	      size);
  return 0;
}

static int32_t
stripe_removexattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  
  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_removexattr (call_frame_t *frame,
		    xlator_t *xl,
		    const char *path,
		    const char *name)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {

    STACK_WIND (frame,
		stripe_removexattr_cbk,
		trav->xlator,
		trav->xlator->fops->removexattr,
		path,
		name);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_open_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *file_ctx,
		 struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  dict_t *ctx = local->ctx;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  } else {
    dict_set (ctx, prev_frame->this->name, int_to_data((long)file_ctx));
  }

  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, ctx, stbuf);
  }
  return 0;
}

static int32_t
stripe_open (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     int32_t flags,
	     mode_t mode)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  local->ctx = get_new_dict ();
  while (trav) {
    STACK_WIND (frame,
		stripe_open_cbk,
		trav->xlator,
		trav->xlator->fops->open,
		path,
		flags,
		mode);
    trav = trav->next;
  }
  return 0;
}

//TODO
static int32_t
stripe_readv_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count)
{
  stripe_local_t *local = frame->local;
  struct iovec *temp_vec;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  local->count += count;
  //* I have to copy the vector for all the call 
  temp_vec = calloc (local->count, sizeof (struct iovec));
  
  local->vector = vector;
  
  dict_ref (frame->root->req_refs);

  if (local->call_count == local->wind_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->vector, local->count);
    //dict_copy () ;
  }
  return 0;
}

static int32_t
stripe_readv (call_frame_t *frame,
	      xlator_t *xl,
	      dict_t *file_ctx,
	      size_t size,
	      off_t offset)
{
  stripe_local_t *local = calloc (1, sizeof (stripe_local_t));
  stripe_private_t *priv = (stripe_private_t *)xl->private;
  int32_t tmp_offset = offset % priv->stripe_size;
  int32_t remaining_size = size;
  int32_t offset_offset = 0;
  int32_t fill_size = size;
  frame->local = local;

  while (1) {
    if ((tmp_offset + remaining_size) > priv->stripe_size) {
      fill_size = priv->stripe_size - tmp_offset;
      remaining_size -= fill_size;
      tmp_offset = 0;
    } else { 
      remaining_size = 0;
    }

    xlator_list_t *trav = xl->children;
    int32_t idx = (offset + offset_offset / priv->stripe_size) % priv->child_count;
    
    while (idx) {
      trav = trav->next;
      idx--;
    }
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));

    local->wind_count++; //use it for unwinding

    STACK_WIND (frame, 
		stripe_readv_cbk,
		trav->xlator,
		trav->xlator->fops->readv,
		ctx,
		fill_size,
		offset + offset_offset);
    offset_offset += fill_size;
    if (remaining_size == 0) 
      break;
  }

  return 0;

}

static int32_t
stripe_writev_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  } else {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  
  if (local->call_count == local->wind_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_writev (call_frame_t *frame,
	       xlator_t *xl,
	       dict_t *file_ctx,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset)
{
  /* TODO: check for memory leak */
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  frame->local = local;
  int32_t total_size = 0;
  int32_t i = 0;
  int32_t tmp_count = count;
  struct iovec *tmp_vec = vector;
  stripe_private_t *priv = xl->private;
  for (i = 0; i< count; i++) {
    total_size += tmp_vec[i].iov_len;
  }
  int32_t offset_offset = 0;
  int32_t remaining_size = total_size;
  int32_t fill_size = 0;
  while (1) {
    xlator_list_t *trav = xl->children;
    
    int32_t idx = ((offset + offset_offset) / priv->stripe_size) % priv->child_count;
    while (idx) {
      trav = trav->next;
      idx--;
    }
    fill_size = priv->stripe_size - (offset % priv->stripe_size);
    if (fill_size > remaining_size)
      fill_size = remaining_size;
    tmp_count = iov_subset (vector, count, offset_offset, 
			    offset_offset + fill_size, NULL);
    tmp_vec = calloc (tmp_count, sizeof (struct iovec));
    tmp_count = iov_subset (vector, count, offset_offset, 
			      offset_offset + fill_size, tmp_vec);
    remaining_size -= fill_size;
    
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    local->wind_count++;
    STACK_WIND(frame,
	       stripe_writev_cbk,
	       trav->xlator,
	       trav->xlator->fops->writev,
	       ctx,
	       tmp_vec,
	       tmp_count,
	       offset + offset_offset);
    offset_offset += fill_size;
    if (remaining_size == 0)
      break;
  }
  return 0;
}

static int32_t
stripe_ftruncate_cbk (call_frame_t *frame,
		      call_frame_t *prev_frame,
		      xlator_t *xl,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_ftruncate (call_frame_t *frame,
		  xlator_t *xl,
		  dict_t *file_ctx,
		  off_t offset)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    STACK_WIND (frame,
		stripe_ftruncate_cbk,
		trav->xlator,
		trav->xlator->fops->ftruncate,
		ctx,
		offset);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_fgetattr_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  } 
  if (local->call_count == 1) 
    local->stbuf = *stbuf;

  local->stbuf.st_size += stbuf->st_size;
  local->stbuf.st_blocks += stbuf->st_blocks;
  local->stbuf.st_blksize += stbuf->st_blksize;

  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }

  return 0;
}

static int32_t
stripe_fgetattr (call_frame_t *frame,
		 xlator_t *xl,
		 dict_t *file_ctx)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    STACK_WIND (frame,
		stripe_fgetattr_cbk,
		trav->xlator,
		trav->xlator->fops->fgetattr,
		ctx);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_flush_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_flush (call_frame_t *frame,
	   xlator_t *xl,
	   dict_t *file_ctx)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while(trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    STACK_WIND (frame,
		stripe_flush_cbk,
		trav->xlator,
		trav->xlator->fops->flush,
		ctx);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_release_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_release (call_frame_t *frame,
		xlator_t *xl,
		dict_t *file_ctx)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    STACK_WIND (frame,
		stripe_release_cbk,
		trav->xlator,
		trav->xlator->fops->flush,
		ctx);
    trav = trav->next;
  }
  dict_destroy (file_ctx);
  return 0;
}

static int32_t
stripe_fsync_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_fsync (call_frame_t *frame,
	      xlator_t *xl,
	      dict_t *file_ctx,
	      int32_t flags)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    STACK_WIND (frame,
		stripe_fsync_cbk,
		trav->xlator,
		trav->xlator->fops->fsync,
		ctx,
		flags);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_lk_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *xl,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct flock *lock)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    local->lock = *lock;
    local->op_ret = 0;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->lock);
  }
  return 0;
}

static int32_t
stripe_lk (call_frame_t *frame,
	   xlator_t *xl,
	   dict_t *file_ctx,
	   int32_t cmd,
	   struct flock *lock)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  local->op_ret = -1;
  while (trav) { 
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    dict_t *ctx = (void *)((long)data_to_int(ctx_data));
    
    STACK_WIND (frame,
		stripe_lk_cbk,
		trav->xlator,
		trav->xlator->fops->lk,
		ctx,
		cmd,
		lock);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_getattr_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  } 
  if (local->call_count == 1) 
    local->stbuf = *stbuf;

  local->stbuf.st_size += stbuf->st_size;
  local->stbuf.st_blocks += stbuf->st_blocks;
  local->stbuf.st_blksize += stbuf->st_blksize;

  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  
  return 0;
}

static int32_t
stripe_getattr (call_frame_t *frame,
		xlator_t *xl,
		const char *path)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_getattr_cbk,
		trav->xlator,
		trav->xlator->fops->getattr,
		path);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_statfs_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *xl,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}
/* TODO */
static int32_t
stripe_statfs (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  //TODO:
  xlator_t *stripexl = xl->children->xlator;
  STACK_WIND (frame,
	      stripe_statfs_cbk,
	      stripexl,
	      stripexl->fops->statfs,
	      path);
  return 0;
}

static int32_t
stripe_truncate_cbk (call_frame_t *frame,
		     call_frame_t *prev_frame,
		     xlator_t *xl,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_truncate (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 off_t offset)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_truncate_cbk,
		trav->xlator,
		trav->xlator->fops->truncate,
		path,
		offset);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_utimes_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_utimes (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path,
	       struct timespec *buf)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_utimes_cbk,
		trav->xlator,
		trav->xlator->fops->utimes,
		path,
		buf);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_readlink_cbk (call_frame_t *frame,
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
stripe_readlink (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 size_t size)
{
  xlator_t *stripexl = xl->children->xlator;
  
  STACK_WIND (frame,
	      stripe_readlink_cbk,
	      stripexl,
	      stripexl->fops->readlink,
	      path,
	      size);
  return 0;
}

static int32_t
stripe_readdir_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entry,
		    int32_t count)
{
  /* TODO: Currently the assumption is the direntry_t structure will be 
     having file info in same order in all the nodes. If its not, then 
     this wont work. */
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  } 
  if (op_ret == 0) {
    if (!local->entry) {
      dir_entry_t *trav = entry->next;
      local->entry = calloc (1, sizeof (dir_entry_t));
      entry->next = NULL;
      local->entry->next = trav;
      local->count = count;
    } else {
      /* update stat of all the entries */
      dir_entry_t *trav_local = local->entry->next;
      dir_entry_t *trav = entry->next;
      while (trav) {
	trav_local->buf.st_size += trav->buf.st_size;
	trav_local->buf.st_blocks += trav->buf.st_blocks;
	trav_local->buf.st_blksize += trav->buf.st_blksize;

	trav = trav->next;
	trav_local = trav_local->next;
      }
    }
  }
  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
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
stripe_readdir (call_frame_t *frame,
		xlator_t *xl,
		const char *path)
{
  gf_log ("stripe-readdir", GF_LOG_DEBUG, "");
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_readdir_cbk,
		trav->xlator,
		trav->xlator->fops->readdir,
		path);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_mkdir_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_mkdir (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_mkdir_cbk,
		trav->xlator,
		trav->xlator->fops->mkdir,
		path,
		mode);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_unlink_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if(op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_unlink (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_unlink_cbk,
		trav->xlator,
		trav->xlator->fops->unlink,
		path);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_rmdir_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*) xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_rmdir (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_rmdir_cbk,
		trav->xlator,
		trav->xlator->fops->rmdir,
		path);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_create_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno,
		   dict_t *file_ctx,
		   struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  dict_t *ctx = local->ctx;
  if(op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  } else
    dict_set (ctx, prev_frame->this->name, int_to_data((long)file_ctx));
  
  if (local->call_count == ((stripe_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, ctx, stbuf);
  }
  return 0;
}

static int32_t
stripe_create (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path,
	       mode_t mode)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  local->ctx = get_new_dict (); 
  while (trav) {
    STACK_WIND (frame,
		stripe_create_cbk,
		trav->xlator,
		trav->xlator->fops->create,
		path,
		mode);
    trav = trav->next;
  }
  return 0;
}


static int32_t
stripe_symlink_cbk (call_frame_t *frame,
		    call_frame_t *prev_frame,
		    xlator_t *xl,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}


static int32_t
stripe_symlink (call_frame_t *frame,
		xlator_t *xl,
		const char *oldpath,
		const char *newpath)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_symlink_cbk,
		trav->xlator,
		trav->xlator->fops->symlink,
		oldpath,
		newpath);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_rename_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
stripe_rename (call_frame_t *frame,
	       xlator_t *xl,
	       const char *oldpath,
	       const char *newpath)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_rename_cbk,
		trav->xlator,
		trav->xlator->fops->rename,
		oldpath,
		newpath);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_link_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_errno = op_errno;
    local->op_ret = op_ret;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_link (call_frame_t *frame,
	     xlator_t *xl,
	     const char *oldpath,
	     const char *newpath)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_link_cbk,
		trav->xlator,
		trav->xlator->fops->link,
		oldpath,
		newpath);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_chmod_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_chmod (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  while (trav) {
    STACK_WIND (frame,
		stripe_chmod_cbk,
		trav->xlator,
		trav->xlator->fops->chmod,
		path,
		mode);
    trav = trav->next;
  }
  return 0;
}

static int32_t
stripe_chown_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  stripe_local_t *local = frame->local;
  local->call_count++;
  if (op_ret == -1) {
    if (op_errno == ENOTCONN)
      op_errno = ENOENT;
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (local->call_count == ((stripe_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
stripe_chown (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = xl->children;
  frame->local = local;
  
  while (trav) {
    STACK_WIND (frame,
		stripe_chown_cbk,
		trav->xlator,
		trav->xlator->fops->chown,
		path,
		uid,
		gid);
    trav = trav->next;
  }
  return 0;
}



/* access */
static int32_t
stripe_access (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path,
	       mode_t mode)
{
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

static int32_t
stripe_lock_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *xl,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
stripe_lock (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  xlator_t *stripexl = xl->children->xlator;

  STACK_WIND (frame,
	      stripe_lock_cbk,
	      stripexl,
	      stripexl->mops->lock,
	      path);
  return 0;
}

static int32_t
stripe_unlock_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *xl,
		   int32_t op_ret,
		   int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
stripe_unlock (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  xlator_t *stripexl = xl->children->xlator;
  STACK_WIND (frame,
	      stripe_unlock_cbk,
	      stripexl,
	      stripexl->mops->unlock,
	      path);
  return 0;
}

static int32_t
stripe_stats (call_frame_t *frame,
	      struct xlator *xl,
	      int32_t flags)
{
  /* TODO */
  STACK_UNWIND (frame, -1, ENOSYS, NULL);
  return 0;
}

int32_t
init (xlator_t *xl)
{
  stripe_private_t *priv = calloc (1, sizeof (stripe_private_t));
  xlator_list_t *trav = xl->children;
  int count = 0;
  
  while (trav) {
    count++;
    trav = trav->next;
  }
  if (count == 1) {
    // TODO: remove this line :p
    gf_log ("stripe/init", 
	    GF_LOG_ERROR, 
	    "I don't understand why you need striping translator");
    free (priv);
    return -1;
  }

  priv->child_count = count;

  /* option stripe-size xxxx (in bytes) */
  data_t *stripe_data = dict_get (xl->options, "stripe-size");
  if (!stripe_data) {
    gf_log ("stripe/init", 
	    GF_LOG_NORMAL, 
	    "\"option stripe-size\" not given, defaulting to 128k");
    priv->stripe_size = 131072;
  } else {
    priv->stripe_size = data_to_int (stripe_data);
  }
  
  xl->private = priv;

  return 0;
} 

void 
fini (xlator_t *xl)
{
  free (xl->private);
  return;
}


struct xlator_fops fops = {
  .getattr     = stripe_getattr,
  .unlink      = stripe_unlink,
  .symlink     = stripe_symlink,
  .rename      = stripe_rename,
  .link        = stripe_link,
  .chmod       = stripe_chmod,
  .chown       = stripe_chown,
  .truncate    = stripe_truncate,
  .utimes      = stripe_utimes,
  .create      = stripe_create,
  .open        = stripe_open,
  .readv       = stripe_readv,
  .writev      = stripe_writev,
  .statfs      = stripe_statfs,
  .flush       = stripe_flush,
  .release     = stripe_release,
  .fsync       = stripe_fsync,
  .setxattr    = stripe_setxattr,
  .getxattr    = stripe_getxattr,
  .listxattr   = stripe_listxattr,
  .removexattr = stripe_removexattr,
  .access      = stripe_access,
  .ftruncate   = stripe_ftruncate,
  .fgetattr    = stripe_fgetattr,
  .readdir     = stripe_readdir,
  .readlink    = stripe_readlink,
  .mkdir       = stripe_mkdir,
  .rmdir       = stripe_rmdir,
  .lk          = stripe_lk,
};

struct xlator_mops mops = {
  .stats  = stripe_stats,
  .lock   = stripe_lock,
  .unlock = stripe_unlock,
};
