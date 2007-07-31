/*
   Copyright (c) 2006 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "glusterfs.h"
#include "dict.h"
#include "xlator.h"

#include "meta.h"
#include "view.h"

static int32_t
meta_getattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

int32_t
meta_getattr (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  
  if (file) {
    if (file->fops && file->fops->getattr) {
      STACK_WIND (frame, meta_getattr_cbk,
		  this, file->fops->getattr, path);
      return 0;
    }
    else {
      STACK_UNWIND (frame, 0, 0, file->stbuf);
      return 0;
    }
  }
  else {
    STACK_WIND (frame, meta_getattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getattr,
	      path);
    return 0;
  }
}

static int32_t
meta_chmod_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_chmod (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    mode_t mode)
{
  STACK_WIND (frame,
	      meta_chmod_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->chmod,
	      path,
	      mode);
  return 0;
}

static int32_t
meta_chown_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_chown (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    uid_t uid,
	    gid_t gid)
{
  STACK_WIND (frame,	      
	      meta_chown_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->chown,
	      path,
	      uid,
	      gid);
  return 0;
}


static int32_t
meta_truncate_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_truncate (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       off_t offset)
{
  STACK_WIND (frame,
	      meta_truncate_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->truncate,
	      path,
	      offset);
  return 0;
}


static int32_t
meta_ftruncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_ftruncate (call_frame_t *frame,
		xlator_t *this,
		dict_t *fd,
		off_t offset)
{
  STACK_WIND (frame,
	      meta_ftruncate_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->ftruncate,
	      fd,
	      offset);
  return 0;
}


static int32_t
meta_utimes_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_utimes (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     struct timespec *buf)
{
  STACK_WIND (frame,
	      meta_utimes_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->utimes,
	      path,
	      buf);
  return 0;
}


static int32_t
meta_access_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_access (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode)
{
  STACK_WIND (frame,
	      meta_access_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->access,
	      path,
	      mode);
  return 0;
}

static int32_t
meta_readlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   char *dest)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		dest);
  return 0;
}

int32_t
meta_readlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       size_t size)
{
  STACK_WIND (frame,
	      meta_readlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->readlink,
	      path,
	      size);
  return 0;
}

static int32_t
meta_mknod_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_mknod (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    mode_t mode,
	    dev_t dev)
{
  STACK_WIND (frame,
	      meta_mknod_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mknod,
	      path,
	      mode,
	      dev);
  return 0;
}

static int32_t
meta_mkdir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_mkdir (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    mode_t mode)
{
  STACK_WIND (frame,
	      meta_mkdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mkdir,
	      path,
	      mode);
  return 0;
}

static int32_t
meta_unlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_unlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *path)
{
  STACK_WIND (frame,
	      meta_unlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->unlink,
	      path);
  return 0;
}

static int32_t
meta_rmdir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_rmdir (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  STACK_WIND (frame,
	      meta_rmdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->rmdir,
	      path);
  return 0;
}

static int32_t
meta_symlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_symlink (call_frame_t *frame,
	      xlator_t *this,
	      const char *oldpath,
	      const char *newpath)
{
  STACK_WIND (frame,
	      meta_symlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->symlink,
	      oldpath,
	      newpath);
  return 0;
}

static int32_t
meta_rename_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_rename (call_frame_t *frame,
	     xlator_t *this,
	     const char *oldpath,
	     const char *newpath)
{
  STACK_WIND (frame,
	      meta_rename_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->rename,
	      oldpath,
	      newpath);
  return 0;
}

static int32_t
meta_link_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_link (call_frame_t *frame,
	   xlator_t *this,
	   const char *oldpath,
	   const char *newpath)
{
  STACK_WIND (frame,
	      meta_link_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->link,
	      oldpath,
	      newpath);
  return 0;
}

struct _open_local {
  const char *path;
};

static int32_t
meta_open_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int32_t op_ret, int32_t op_errno,
	       dict_t *ctx, struct stat *buf)
{
  struct _open_local *local = frame->local;
  if (local)
    dict_set (ctx, this->name, str_to_data (local->path));
  STACK_UNWIND (frame, op_ret, op_errno, ctx, buf);
  return 0;
}

int32_t
meta_open (call_frame_t *frame, xlator_t *this,
	   const char *path, int32_t flags, mode_t mode)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  meta_dirent_t *file = lookup_meta_entry (root, path, NULL);

  if (file) {
    if (file->fops && file->fops->open) {
      struct _open_local *local = calloc (1, sizeof (struct _open_local));
      local->path = strdup (path);
      frame->local = local;
      STACK_WIND (frame, meta_open_cbk,
		  this, file->fops->open,
		  path, flags, mode);
      return 0;
    }
    else {
      dict_t *ctx = get_new_dict ();
      dict_ref (ctx);
      dict_set (ctx, this->name, str_to_data (strdup (path)));
      STACK_UNWIND (frame, 0, 0, ctx, file->stbuf);
      return 0;
    }
  }
  else {  
    STACK_WIND (frame, meta_open_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
		path, flags, mode);
    return 0;
  }
}

int32_t
meta_create (call_frame_t *frame, xlator_t *this,
	     const char *path, int32_t flags, mode_t mode)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  meta_dirent_t *file = lookup_meta_entry (root, path, NULL);

  if (file) {
    if (file->fops && file->fops->create) {
      struct _open_local *local = calloc (1, sizeof (struct _open_local));
      local->path = strdup (path);
      frame->local = local;
      STACK_WIND (frame, meta_open_cbk,
		  this, file->fops->create,
		  path, flags, mode);
      return 0;
    }
    else {
      STACK_UNWIND (frame, -1, 0, NULL, NULL);
      return 0;
    }
  }
  else {
    STACK_WIND (frame, meta_open_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
		path, flags, mode);
    return 0;
  }
}

static int32_t
meta_readv_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct iovec *vector,
		int32_t count)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		vector,
		count);
  return 0;
}

int32_t
meta_readv (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *fd,
	    size_t size,
	    off_t offset)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);

  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  
    if (file && file->fops && file->fops->readv) {
      STACK_WIND (frame, meta_readv_cbk, 
		  this, file->fops->readv,
		  fd, size, offset);
      return 0;
    }
  }
  else {
    STACK_WIND (frame, meta_readv_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
		fd, size, offset);
    return 0;
  }
}

static int32_t
meta_writev_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
meta_writev (call_frame_t *frame, xlator_t *this,
	     dict_t *fd, 
	     struct iovec *vector, int32_t count, off_t offset)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);

  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  
    if (file && file->fops && file->fops->writev) {
      STACK_WIND (frame, meta_writev_cbk, 
		  this, file->fops->writev,
		  fd, vector, count, offset);
      return 0;
    }
  }
  else {
    STACK_WIND (frame, meta_readv_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
		fd, vector, count, offset);
    return 0;
  }
}

static int32_t
meta_flush_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_flush (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *fd)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);
 
  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  
    if (file) {
      if (file->fops && file->fops->flush) {
	STACK_WIND (frame, meta_flush_cbk,
		    this, file->fops->flush,
		    fd);
	return 0;
      }
      else {
	STACK_UNWIND (frame, 0, 0);
	return 0;
      }
    }
  }
  else {
    STACK_WIND (frame, meta_flush_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->flush,
		fd);
    return 0;
  }
}

static int32_t
meta_release_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_release (call_frame_t *frame,
	      xlator_t *this,
	      dict_t *fd)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  data_t *path_data = dict_get (fd, this->name);

  if (path_data) {
    const char *path = data_to_str (path_data);
    meta_dirent_t *file = lookup_meta_entry (root, path, NULL);
  
    if (file) {
      dict_unref (fd);
      STACK_UNWIND (frame, 0, 0);
      return 0;
    }
  }
  else {
    STACK_WIND (frame, meta_release_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->release,
		fd);
    return 0;
  }
}

static int32_t
meta_fsync_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_fsync (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *fd,
	    int32_t flags)
{
  STACK_WIND (frame,
	      meta_fsync_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fsync,
	      fd,
	      flags);
  return 0;
}

static int32_t
meta_fgetattr_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_fgetattr (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *fd)
{
  STACK_WIND (frame,
	      meta_fgetattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fgetattr,
	      fd);
  return 0;
}

static int32_t
meta_opendir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *fd)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		fd);
  return 0;
}

int32_t
meta_opendir (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;
  meta_dirent_t *dir = lookup_meta_entry (root, path, NULL);
  
  if (dir) {
    dict_t *ctx = get_new_dict ();
    dict_set (ctx, this->name, str_to_data (strdup (path)));
    STACK_UNWIND (frame, 0, 0, ctx);
    return 0;
  }
  else {  
    STACK_WIND (frame, meta_opendir_cbk,
		FIRST_CHILD(this), FIRST_CHILD(this)->fops->opendir,
		path);
    return 0;
  }
}

static int32_t
meta_readdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dir_entry_t *entries,
		  int32_t count)
{
  meta_private_t *priv = (meta_private_t *)this->private;

  if ((int) cookie == 1) {
    dir_entry_t *dir = calloc (1, sizeof (dir_entry_t));

    dir->name = strdup (".meta");
    memcpy (&dir->buf, priv->tree->stbuf, sizeof (struct stat));
    dir->next = entries->next;
    entries->next = dir;

    STACK_UNWIND (frame, op_ret, op_errno, entries, count+1);
    return 0;
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, entries, count);
  return 0;
}

int32_t
meta_readdir (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  meta_dirent_t *root = priv->tree;

  meta_dirent_t *dir = lookup_meta_entry (root, path, NULL);
  if (dir) {
    if (dir->fops && dir->fops->readdir) {
      STACK_WIND (frame, meta_readdir_cbk, 
		  this, dir->fops->readdir, path);
      return 0;
    }
    else {
      int count = 0;
      dir = dir->children;
      dir_entry_t *entries = NULL;

      while (dir) {
	dir_entry_t *d = calloc (1, sizeof (dir_entry_t));
	d->name = dir->name;
	d->buf  = *dir->stbuf;
	d->next = entries;
	entries = d;
	count++;
	dir = dir->next;
      }

      dir_entry_t *header = calloc (1, sizeof (dir_entry_t));
      header->next = entries;
      STACK_UNWIND (frame, 0, 0, header, count);
      return 0;
    }
  }
  else {
    if (!strcmp (path, "/")) {
      _STACK_WIND (frame, meta_readdir_cbk, 
		   (int) 1, /* cookie to tell _cbk to add .meta entry */
		   FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdir,
		   path);
    }
    else {
      STACK_WIND (frame, meta_readdir_cbk, 
		  FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdir,
		  path);
    }
  }
  return 0;
}

static int32_t
meta_releasedir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_releasedir (call_frame_t *frame,
		 xlator_t *this,
		 dict_t *fd)
{
  STACK_WIND (frame,
	      meta_releasedir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->releasedir,
	      fd);
  return 0;
}

static int32_t
meta_fsyncdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_fsyncdir (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *fd,
	       int32_t flags)
{
  STACK_WIND (frame,
	      meta_fsyncdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fsyncdir,
	      fd,
	      flags);
  return 0;
}

static int32_t
meta_statfs_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct statvfs *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
meta_statfs (call_frame_t *frame,
	     xlator_t *this,
	     const char *path)
{
  STACK_WIND (frame,
	      meta_statfs_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->statfs,
	      path);
  return 0;
}

static int32_t
meta_setxattr_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_setxattr (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       const char *name,
	       const char *value,
	       size_t size,
	       int32_t flags)
{
  STACK_WIND (frame,
	      meta_setxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->setxattr,
	      path,
	      name,
	      value,
	      size,
	      flags);
  return 0;
}

static int32_t
meta_getxattr_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   char *value)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		value);
  return 0;
}

int32_t
meta_getxattr (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       const char *name,
	       size_t size)
{
  STACK_WIND (frame,
	      meta_getxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getxattr,
	      path,
	      name,
	      size);
  return 0;
}

static int32_t
meta_listxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    char *value)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		value);
  return 0;
}

int32_t
meta_listxattr (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		size_t size)
{
  STACK_WIND (frame,
	      meta_listxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->listxattr,
	      path,
	      size);
  return 0;
}

static int32_t
meta_removexattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
meta_removexattr (call_frame_t *frame,
		  xlator_t *this,
		  const char *path,
		  const char *name)
{
  STACK_WIND (frame,
	      meta_removexattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->removexattr,
	      path,
	      name);
  return 0;
}

static int32_t
meta_lk_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     struct flock *lock)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		lock);
  return 0;
}

int32_t
meta_lk (call_frame_t *frame,
	 xlator_t *this,
	 dict_t *file,
	 int32_t cmd,
	 struct flock *lock)
{
  STACK_WIND (frame,
	      meta_lk_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->lk,
	      file,
	      cmd,
	      lock);
  return 0;
}

static void
add_xlator_to_tree (meta_dirent_t *tree, xlator_t *this,
		    const char *prefix)
{
  char *dir;
  asprintf (&dir, "%s/%s", prefix, this->name);

  char *children;
  asprintf (&children, "%s/%s", dir, "subvolumes");

  char *type;
  asprintf (&type, "%s/%s", dir, "type");

  char *view;
  asprintf (&view, "%s/%s", dir, "view");

  insert_meta_entry (tree, dir, S_IFDIR, NULL, NULL);
  insert_meta_entry (tree, children, S_IFDIR, NULL, NULL);
  meta_dirent_t *v = insert_meta_entry (tree, view, S_IFDIR, NULL, 
					&meta_xlator_view_fops);
  v->view_xlator = this;
  meta_dirent_t *t = insert_meta_entry (tree, type, S_IFREG, NULL, 
					&meta_xlator_type_fops);
  t->view_xlator = this;

  xlator_list_t *trav = this->children;
  while (trav) {
    add_xlator_to_tree (tree, trav->xlator, children);
    trav = trav->next;
  }
}

static void
build_meta_tree (xlator_t *this)
{
  meta_private_t *priv = (meta_private_t *) this->private;
  priv->tree = calloc (1, sizeof (meta_dirent_t));
  priv->tree->name = strdup (".meta");
  priv->tree->stbuf = new_stbuf ();
  priv->tree->stbuf->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH |
    S_IXUSR | S_IXGRP | S_IXOTH;

  insert_meta_entry (priv->tree, "/.meta/version", 
		     S_IFREG, NULL, &meta_version_fops);

  insert_meta_entry (priv->tree, "/.meta/xlators",
		     S_IFDIR, NULL, NULL);

  xlator_list_t *trav = this->children;
  while (trav) {
    add_xlator_to_tree (priv->tree, trav->xlator, "/.meta/xlators");
    trav = trav->next;
  }
}

int32_t
init (xlator_t *this)
{
  if (this->parent != NULL) {
    gf_log ("meta", GF_LOG_ERROR, "FATAL: meta should be the root of the xlator tree");
    return -1;
  }
  
  meta_private_t *priv = calloc (1, sizeof (meta_private_t));
  
  data_t *directory = dict_get (this->options, "directory");
  if (directory) {
    priv->directory = strdup (data_to_str (directory));
  }
  else {
    priv->directory = ".meta";
  }
  
  this->private = priv;
  build_meta_tree (this);

  return 0;
}

int32_t
fini (xlator_t *this)
{
  return 0;
}

struct xlator_fops fops = {
  .getattr     = meta_getattr,
  .readlink    = meta_readlink,
  .mknod       = meta_mknod,
  .mkdir       = meta_mkdir,
  .unlink      = meta_unlink,
  .rmdir       = meta_rmdir,
  .symlink     = meta_symlink,
  .rename      = meta_rename,
  .link        = meta_link,
  .chmod       = meta_chmod,
  .chown       = meta_chown,
  .truncate    = meta_truncate,
  .utimes      = meta_utimes,
  .open        = meta_open,
  .readv       = meta_readv,
  .writev      = meta_writev,
  .statfs      = meta_statfs,
  .flush       = meta_flush,
  .release     = meta_release,
  .fsync       = meta_fsync,
  .setxattr    = meta_setxattr,
  .getxattr    = meta_getxattr,
  .listxattr   = meta_listxattr,
  .removexattr = meta_removexattr,
  .opendir     = meta_opendir,
  .readdir     = meta_readdir,
  .releasedir  = meta_releasedir,
  .fsyncdir    = meta_fsyncdir,
  .access      = meta_access,
  .ftruncate   = meta_ftruncate,
  .fgetattr    = meta_fgetattr,
  .create      = meta_create,
  .lk          = meta_lk,
};

struct xlator_mops mops = {
};
