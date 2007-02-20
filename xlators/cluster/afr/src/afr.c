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
 * 2) AFR node has to be specified in the spec file, it defaults to the
 *    first child in case it is not specified. We use AFR node for mops->lock,
 *    readdir, read etc calls where we do not modify any file, ie we just
 *    do a 'read' access. This will make response time for those calls
 *    faster. As of now AFR node can not be shared between two AFR groups.
 *    this is because consider a case where M1 and M2 have C1 and C2 as
 *    their children, and M1's AFR node is C1 and M2's AFR node is C2.
 *    If a file foo.c is present on both the nodes then readdir will
 *    get entry for foo.c from both C1 and C2, hence 'ls' will list it twice.
 *    We can handle this problem by creating a hidden directory '.afr'
 *    and have a directory tree structure with empty files just to know
 *    what all files belong to this particular AFR node so that readdir
 *    open getattr etc calls can behave accordingly.
 * 3) how to handle if AFR node goes down.
 * 4) Check the FIXMEs
 */


#include <libgen.h>
#include <unistd.h>

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"

#define AFR_ERR_CHECK() do {\
if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOENT) {\
  local->op_errno = op_errno;\
}\
if (op_ret == 0) {\
  local->op_ret = op_ret;\
  local->op_errno = op_errno;\
}\
} while(0);

#define AFR_LOCAL_INIT() \
afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));\
frame->local = local;\
local->op_ret = -1;\
local->op_errno = ENOENT;

/* FIXME: All references to local->* should be locked */

static int32_t
afr_setxattr_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *xl,
		  int32_t op_ret,
		  int32_t op_errno)
{
  afr_local_t *local = (afr_local_t *) frame->local;
  local->call_count++;
  AFR_ERR_CHECK();

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
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
  AFR_LOCAL_INIT();
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
  xlator_t *afrxl;
  afrxl = ((afr_private_t*) xl->private)->afr_node;
  
  STACK_WIND (frame,
	      afr_getxattr_cbk,
	      afrxl,
	      afrxl->fops->getxattr,
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
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
afr_listxattr (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path,
	       size_t size)
{
  xlator_t *afrxl;

  afrxl = ((afr_private_t*) xl->private)->afr_node;
  STACK_WIND (frame,
	      afr_listxattr_cbk,
	      afrxl,
	      afrxl->fops->listxattr,
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
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
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  dict_t *ctx = local->ctx;
  local->call_count++;
  AFR_ERR_CHECK();
  if (op_ret == 0)
    dict_set (ctx, prev_frame->this->name, int_to_data((long)file_ctx));

  /* FIXME: When one of the open fails, we need to
   * call close on xlators on which open had succeeded
   */

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, ctx, stbuf);
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
  AFR_LOCAL_INIT();
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
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;
  data_t *ctx_data = dict_get (file_ctx, afrxl->name);
  if (ctx_data == NULL) {
    gf_log ("afr", GF_LOG_ERROR, "afr_readv: dict_get returned NULL :O");
    STACK_UNWIND (frame, -1, ENOENT, NULL, 0);
    return 0;
  }
  dict_t *ctx = (void *)((long)data_to_int(ctx_data));

  STACK_WIND (frame, 
	      afr_readv_cbk,
	      afrxl,
	      afrxl->fops->readv,
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
  afr_local_t *local = frame->local;
  local->call_count++;

  AFR_ERR_CHECK();
  if (op_ret != -1) { /* check if op_ret is 0 means not write error */
    /* FIXME: assuming that all child write call writes all data */
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }

  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
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
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      local->call_count--;
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
afr_ftruncate (call_frame_t *frame,
	       xlator_t *xl,
	       dict_t *file_ctx,
	       off_t offset)
{
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      local->call_count--;
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
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_fgetattr (call_frame_t *frame,
	      xlator_t *xl,
	      dict_t *file_ctx)
{
  xlator_t *afrxl = ((afr_private_t*)xl->private)->afr_node;
  data_t *ctx_data = dict_get (file_ctx, afrxl->name);
  if (ctx_data == NULL) {
    gf_log ("afr", GF_LOG_ERROR, "afr_fgetattr: dict_get returned NULL, AFR is node down :O\n");
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  dict_t *ctx = (void *)((long)data_to_int(ctx_data));

  STACK_WIND (frame,
	      afr_fgetattr_cbk,
	      afrxl,
	      afrxl->fops->fgetattr,
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_flush (call_frame_t *frame,
	   xlator_t *xl,
	   dict_t *file_ctx)
{
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  while(trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if(ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      local->call_count--;
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_release (call_frame_t *frame,
	     xlator_t *xl,
	     dict_t *file_ctx)
{
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if(ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      local->call_count--;
      STACK_WIND (frame,
		  afr_release_cbk,
		  trav->xlator,
		  trav->xlator->fops->flush,
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
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
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;
  local->call_count = ((afr_private_t *)xl->private)->child_count;
  while (trav) {
    data_t *ctx_data = dict_get (file_ctx, trav->xlator->name);
    if (ctx_data) {
      dict_t *ctx = (void *)((long)data_to_int(ctx_data));
      local->call_count--;
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
  STACK_UNWIND (frame, op_ret, op_errno, lock);
  return 0;
}

static int32_t
afr_lk (call_frame_t *frame,
	xlator_t *xl,
	dict_t *file_ctx,
	int32_t cmd,
	struct flock *lock)
{
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;
  data_t *ctx_data = dict_get (file_ctx, afrxl->name);
  if (ctx_data == NULL) {
    gf_log ("afr", GF_LOG_DEBUG, "afr_lk: dict_get returned NULL :O");
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  dict_t *ctx = (void *)((long)data_to_int(ctx_data));

  STACK_WIND (frame,
	      afr_lk_cbk,
	      afrxl,
	      afrxl->fops->lk,
	      ctx,
	      cmd,
	      lock);
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
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_getattr (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;
  STACK_WIND (frame,
	      afr_getattr_cbk,
	      afrxl,
	      afrxl->fops->getattr,
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
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_statfs (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path)
{
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;
  STACK_WIND (frame,
	      afr_statfs_cbk,
	      afrxl,
	      afrxl->fops->statfs,
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
afr_truncate (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      off_t offset)
{
  AFR_LOCAL_INIT();
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
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_utimes (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path,
	    struct timespec *buf)
{
  xlator_t *afrxl = ((afr_private_t *) xl->private)->afr_node;
  STACK_WIND (frame,
	      afr_utimes_cbk,
	      afrxl,
	      afrxl->fops->utimes,
	      path,
	      buf);
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
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
afr_opendir (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  xlator_t *afrxl = ((afr_private_t *) xl->private)->afr_node;

  STACK_WIND (frame,
	      afr_opendir_cbk,
	      afrxl,
	      afrxl->fops->opendir,
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
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
afr_readlink (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      size_t size)
{
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;

  STACK_WIND (frame,
	      afr_readlink_cbk,
	      afrxl,
	      afrxl->fops->readlink,
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
  STACK_UNWIND (frame, op_ret, op_errno, entry, count);
  return 0;
}

static int32_t
afr_readdir (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path)
{
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;

  STACK_WIND (frame,
	      afr_readdir_cbk,
	      afrxl,
	      afrxl->fops->readdir,
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path,
	   mode_t mode)
{
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_unlink (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path)
{
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*) xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_rmdir (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path)
{
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  local->call_count++;
  dict_t *ctx = local->ctx;
  AFR_ERR_CHECK();
  if(op_ret == 0)
    dict_set (ctx, prev_frame->this->name, int_to_data((long)file_ctx));

  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, ctx, stbuf);
  }
  return 0;
}

static int32_t
afr_create (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path,
	    mode_t mode)
{
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;
  local->ctx = get_new_dict ();
  while (trav) {
    STACK_WIND (frame,
		afr_create_cbk,
		trav->xlator,
		trav->xlator->fops->create,
		path,
		mode);
    trav = trav->next;
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t *)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
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
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}


static int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *xl,
	     const char *oldpath,
	     const char *newpath)
{
  AFR_LOCAL_INIT();
  xlator_list_t *trav = xl->children;

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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
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
  AFR_LOCAL_INIT();
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

static int32_t
afr_link_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *xl,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
afr_link (call_frame_t *frame,
	  xlator_t *xl,
	  const char *oldpath,
	  const char *newpath)
{
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
  }
  return 0;
}

static int32_t
afr_chmod (call_frame_t *frame,
	   xlator_t *xl,
	   const char *path,
	   mode_t mode)
{
  AFR_LOCAL_INIT();
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
  afr_local_t *local = frame->local;
  local->call_count++;
  AFR_ERR_CHECK();
  if (local->call_count == ((afr_private_t*)xl->private)->child_count) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stbuf);
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
  AFR_LOCAL_INIT();
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
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
afr_lock (call_frame_t *frame,
	  xlator_t *xl,
	  const char *path)
{
  xlator_t *afrxl = ((afr_private_t *) xl->private)->afr_node;

  STACK_WIND (frame,
	      afr_lock_cbk,
	      afrxl,
	      afrxl->mops->lock,
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
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
afr_unlock (call_frame_t *frame,
	    xlator_t *xl,
	    const char *path)
{
  xlator_t *afrxl = ((afr_private_t *)xl->private)->afr_node;
  STACK_WIND (frame,
	      afr_unlock_cbk,
	      afrxl,
	      afrxl->mops->unlock,
	      path);
  return 0;
}

static int32_t
afr_stats (call_frame_t *frame,
	   struct xlator *xl,
	   int32_t flags)
{
  STACK_UNWIND (frame, -1, ENOSYS, NULL);
  return -1;
}

int32_t 
init (xlator_t *xl)
{
  afr_private_t *pvt = calloc (1, sizeof (afr_private_t));
  data_t * afr_node = dict_get (xl->options, "afr-node");
  int count = 0;
  xlator_list_t *trav = xl->children;

  while (trav) {
    count++;
    trav = trav->next;
  }
  pvt->child_count = count;
  if (afr_node) {
    trav = xl->children;
    while (trav) {
      if(strcmp (trav->xlator->name, afr_node->data) == 0)
        break;
      trav = trav->next;
    }
    if(trav == NULL) {
      gf_log ("afr", GF_LOG_ERROR, "afr->init: afr server not found among the children");
      return -1;
    }
    gf_log ("afr", GF_LOG_DEBUG, "afr node is %s", trav->xlator->name);
    pvt->afr_node = trav->xlator;
  } else {
    gf_log ("afr", GF_LOG_DEBUG, "afr->init: afr server not specified, defaulting to %s", xl->children->xlator->name);
    pvt->afr_node = xl->children->xlator;
  }
  xl->private = pvt;
  return 0;
}

void
fini(xlator_t *xl)
{
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
