/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"

#include "meta.h"

/* 
 * This file contains fops for the files and directories in 
 * an xlator directory
 */

/* /.meta/xlators/.../type */

int32_t
meta_xlator_type_readv (call_frame_t *frame, xlator_t *this,
			dict_t *fd, size_t size, off_t offset)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);

  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
    xlator_t *view_xlator = file->view_xlator;

    int type_size;
    type_size = strlen (view_xlator->type);
  
    struct iovec vec;
    vec.iov_base = view_xlator->type + offset;
    vec.iov_len  = min (type_size - offset, size);

    STACK_UNWIND (frame, vec.iov_len, 0, &vec, 1);
    return 0;
  }
}

int32_t
meta_xlator_type_getattr (call_frame_t *frame, 
			  xlator_t *this,
			  const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;

  meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  xlator_t *view_xlator = file->view_xlator;
  file->stbuf->st_size = strlen (view_xlator->type);

  STACK_UNWIND (frame, 0, 0, file->stbuf);
  return 0;
}

struct xlator_fops meta_xlator_type_fops = {
  .readv   = meta_xlator_type_readv,
  .getattr = meta_xlator_type_getattr
};

/* 
 * fops for the "view" directory
 * {xlator}/view shows the filesystem as it appears
 * to {xlator}
 */

static int32_t
meta_xlator_view_getattr_cbk (call_frame_t *frame, void *cookie,
			      xlator_t *this, int32_t op_ret, int32_t op_errno,
			      struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

int32_t
meta_xlator_view_getattr (call_frame_t *frame, 
			  xlator_t *this,
			  const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  char *op_path = NULL;

  meta_dirent_t *file = lookup_meta_entry (root, path, &op_path);

  if (op_path) {
    STACK_WIND (frame, meta_xlator_view_getattr_cbk, file->view_xlator,
		file->view_xlator->fops->getattr,
		op_path);
  }
  else {
    STACK_UNWIND (frame, 0, 0, file->stbuf);
  }

  return 0;
}

static int32_t
meta_xlator_view_readdir_cbk (call_frame_t *frame, void *cookie,
			      xlator_t *this, int32_t op_ret, int32_t op_errno,
			      dir_entry_t *entries, int32_t count)
{
  STACK_UNWIND (frame, op_ret, op_errno, entries, count);
  return 0;
}

int32_t
meta_xlator_view_readdir (call_frame_t *frame,
			  xlator_t *this,
			  const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  char *op_path = NULL;

  meta_dirent_t *dir = lookup_meta_entry (root, path, &op_path);

  STACK_WIND (frame, meta_xlator_view_readdir_cbk, 
	      dir->view_xlator, dir->view_xlator->fops->readdir,
	      op_path ? op_path : "/");
  return 0;
}

static int32_t
meta_xlator_view_open_cbk (call_frame_t *frame, void *cookie,
			   xlator_t *this, 
			   int32_t op_ret, int32_t op_errno,
			   dict_t *ctx, struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, ctx, buf);
  return 0;
}

int32_t
meta_xlator_view_open (call_frame_t *frame, xlator_t *this,
		       const char *path, int32_t flags, mode_t mode)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  char *op_path = NULL;

  meta_dirent_t *file = lookup_meta_entry (root, path, &op_path);
  STACK_WIND (frame, meta_xlator_view_open_cbk,
	      file->view_xlator, file->view_xlator->fops->open,
	      op_path, flags, mode);
  return 0;
}

int32_t
meta_xlator_view_create (call_frame_t *frame, xlator_t *this,
			 const char *path, int32_t flags, mode_t mode)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  char *op_path = NULL;

  meta_dirent_t *file = lookup_meta_entry (root, path, &op_path);
  STACK_WIND (frame, meta_xlator_view_open_cbk,
	      file->view_xlator, file->view_xlator->fops->create,
	      op_path, flags, mode);
  return 0;
}

static int32_t
meta_xlator_view_readv_cbk (call_frame_t *frame, void *cookie,
			    xlator_t *this, int32_t op_ret,
			    int32_t op_errno, struct iovec *vector,
			    int32_t count)
{
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

int32_t
meta_xlator_view_readv (call_frame_t *frame, xlator_t *this,
			dict_t *fd, size_t size, off_t offset)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);

  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);

    STACK_WIND (frame, meta_xlator_view_readv_cbk,
		file->view_xlator, file->view_xlator->fops->readv,
		fd, size, offset);
    return 0;
  }

  STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
  return 0;
}

static int32_t
meta_xlator_view_writev_cbk (call_frame_t *frame, void *cookie,
			     xlator_t *this, int32_t op_ret,
			     int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
meta_xlator_view_writev (call_frame_t *frame, xlator_t *this,
			 dict_t *fd, 
			 struct iovec *vector, int32_t count, off_t offset)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);

  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);

    STACK_WIND (frame, meta_xlator_view_writev_cbk,
		file->view_xlator, file->view_xlator->fops->writev,
		fd, vector, count, offset);
    return 0;
  }

  STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
  return 0;
}

struct xlator_fops meta_xlator_view_fops = {
  .getattr = meta_xlator_view_getattr,
  .readdir = meta_xlator_view_readdir,
  .open    = meta_xlator_view_open,
  .create  = meta_xlator_view_create,
  .readv   = meta_xlator_view_readv,
  .writev  = meta_xlator_view_writev
};
