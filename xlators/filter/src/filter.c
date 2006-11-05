/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
#include "filter.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

/*
  This filter currently only makes its child read-only.
  In the future it'll be extended to handle other types of filtering
  (filtering certain file types, for example)
*/

/* Calls which return at this level */

int32_t 
filter_mknod (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode,
	      dev_t dev)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}

int32_t 
filter_mkdir (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  STACK_UNWIND (frame, -1, EROFS);
  errno = EROFS;
  return -1;
}


int32_t 
filter_unlink (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}


int32_t 
filter_rmdir (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}

int32_t 
filter_symlink (call_frame_t *frame,
		xlator_t *xl,
		const char *oldpath,
		const char *newpath)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}

int32_t 
filter_rename (call_frame_t *frame,
	       xlator_t *xl,
	       const char *oldpath,
	       const char *newpath)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}

int32_t 
filter_link (call_frame_t *frame,
	     xlator_t *xl,
	     const char *oldpath,
	     const char *newpath)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}


int32_t 
filter_chmod (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      mode_t mode)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}


int32_t 
filter_chown (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}


int32_t 
filter_truncate (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 off_t offset)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}


int32_t 
filter_utime (call_frame_t *frame,
	      xlator_t *xl,
	      const char *path,
	      struct utimbuf *buf)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}


int32_t 
filter_write (call_frame_t *frame,
	      xlator_t *xl,
	      file_ctx_t *ctx,
	      char *buf,
	      size_t size,
	      off_t offset)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}


int32_t 
filter_flush (call_frame_t *frame,
	      xlator_t *xl,
	      file_ctx_t *ctx)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}

int32_t 
filter_fsync (call_frame_t *frame,
	      xlator_t *xl,
	      file_ctx_t *ctx,
	      int32_t datasync)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}

int32_t 
filter_setxattr (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int32_t flags)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}
		     
int32_t 
filter_removexattr (call_frame_t *frame,
		    xlator_t *xl,
		    const char *path,
		    const char *name)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}


int32_t 
filter_fsyncdir (call_frame_t *frame,
		 xlator_t *xl,
		 file_ctx_t *ctx,
		 int32_t datasync)
{
  STACK_UNWIND (frame, -1, EROFS);
  return -1;
}

int32_t 
filter_ftruncate (call_frame_t *frame,
		  xlator_t *xl,
		  file_ctx_t *ctx,
		  off_t offset)
{
  STACK_UNWIND (frame, -1, EROFS, NULL);
  return -1;
}


/* Implemented Calls */

int32_t 
filter_getattr (call_frame_t *frame,
		xlator_t *xl,
		const char *path)
{
  struct filter_private *priv = xl->private;
  int32_t ret = 0;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->getattr (trav_xl, path, stbuf);

  return ret;
}


int32_t 
filter_readlink (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 size_t size)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->readlink (trav_xl, path, dest, size);
  return ret;
}


int32_t 
filter_open (call_frame_t *frame,
	     xlator_t *xl,
	     const char *path,
	     int32_t flags,
	     mode_t mode)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  file_ctx_t *filter_ctx = calloc (1, sizeof (file_ctx_t));
  filter_ctx->volume = xl;
  filter_ctx->next = ctx->next;
  ctx->next = filter_ctx;

  if ((flags & O_WRONLY) || (flags & O_RDWR)) {
    errno = EROFS;
    return -1;
  }
  
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->open (trav_xl, path, flags, mode, ctx);
  return ret;
}

int32_t 
filter_read (call_frame_t *frame,
	     xlator_t *xl,
	     file_ctx_t *ctx,
	     size_t size,
	     off_t offset)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    // this file is not opened
    return -1;
  }
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->read (trav_xl, path, buf, size, offset, ctx);
  return ret;
}

int32_t 
filter_statfs (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->statfs (trav_xl, path, buf);
  return ret;
}


int32_t 
filter_release (call_frame_t *frame,
		xlator_t *xl,
		file_ctx_t *ctx)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  file_ctx_t *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->release (trav_xl, path, ctx);

  RM_MY_CTX (ctx, tmp);
  free (tmp);

  return ret;
}


int32_t 
filter_getxattr (call_frame_t *frame,
		 xlator_t *xl,
		 const char *path,
		 const char *name,
		 size_t size)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->getxattr (trav_xl, path, name, value, size);
  return ret;
}

int32_t 
filter_listxattr (call_frame_t *frame,
		  xlator_t *xl,
		  const char *path,
		  size_t size)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->listxattr (trav_xl, path, list, size);
  return ret;
}

int32_t 
filter_opendir (call_frame_t *frame,
		xlator_t *xl,
		const char *path)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->opendir (trav_xl, path, ctx);
  return ret;
}

static int32_t 
filter_readdir (call_frame_t *frame,
		xlator_t *xl,
		const char *path)
{
  char *ret = NULL;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->readdir (trav_xl, path, offset);
  return ret;
}

int32_t 
filter_releasedir (call_frame_t *frame,
		   xlator_t *xl,
		   file_ctx_t *ctx)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->releasedir (trav_xl, path, ctx);
  return ret;
}

int32_t 
filter_access (call_frame_t *frame,
	       xlator_t *xl,
	       const char *path,
	       mode_t mode)
{
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }

  if (mode & W_OK) {
    errno = EROFS;
    return -1;
  }
    
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->access (trav_xl, path, mode);
  return ret;
}

int32_t 
filter_fgetattr (call_frame_t *frame,
		 xlator_t *xl,
		 file_ctx_t *ctx)
{
  
  int32_t ret = 0;
  struct filter_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  xlator_t *trav_xl = xl->first_child;
  ret = trav_xl->fops->fgetattr (trav_xl, path, buf, ctx);
  return ret;
}


int32_t 
filter_stats (call_frame_t *frame,
	      xlator_t *xl,
	      int32_t flags)
{
  return 0;
}

int32_t 
init (xlator_t *xl)
{
  struct filter_private *_private = calloc (1, sizeof (*_private));
  data_t *debug = dict_get (xl->options, "debug");
  _private->is_debug = 0;
  if (debug && strcasecmp (debug->data, "on") == 0) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("filter", GF_LOG_DEBUG, "filter.c->init: debug mode on\n");
  }
  xl->private = (void *)_private;

  if (!xl->first_child) {
    gf_log ("filter", GF_LOG_ERROR, "filter.c->init: filter xlator should have exactly one child (0 given)");
    return -1;
  }

  if (xl->first_child->next_sibling != NULL) {
    gf_log ("filter", GF_LOG_ERROR, "filter.c->init: filter xlator should have exactly one child (more than 1 given)");
    return -1;
  }
    
  return 0;
}

void
fini (xlator_t *xl)
{
  struct filter_private *priv = xl->private;
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = filter_getattr,
  .readlink    = filter_readlink,
  .mknod       = filter_mknod,
  .mkdir       = filter_mkdir,
  .unlink      = filter_unlink,
  .rmdir       = filter_rmdir,
  .symlink     = filter_symlink,
  .rename      = filter_rename,
  .link        = filter_link,
  .chmod       = filter_chmod,
  .chown       = filter_chown,
  .truncate    = filter_truncate,
  .utime       = filter_utime,
  .open        = filter_open,
  .read        = filter_read,
  .write       = filter_write,
  .statfs      = filter_statfs,
  .flush       = filter_flush,
  .release     = filter_release,
  .fsync       = filter_fsync,
  .setxattr    = filter_setxattr,
  .getxattr    = filter_getxattr,
  .listxattr   = filter_listxattr,
  .removexattr = filter_removexattr,
  .opendir     = filter_opendir,
  .readdir     = filter_readdir,
  .releasedir  = filter_releasedir,
  .fsyncdir    = filter_fsyncdir,
  .access      = filter_access,
  .ftruncate   = filter_ftruncate,
  .fgetattr    = filter_fgetattr,
};

struct xlator_mops mops = {
  .stats = filter_stats
};
