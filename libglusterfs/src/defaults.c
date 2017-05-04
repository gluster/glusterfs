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


#include "layout.h"
#include "xlator.h"

layout_t *
default_getlayout (struct xlator *xl,
		   layout_t *layout)
{
  /* TODO: if @layout@ is NULL, allocate and return */
  chunk_t *chunk = &layout->chunks;

  layout->chunk_count = 1;
  
  chunk->path = layout->path;
  chunk->path_dyn = 0;
  chunk->begin = 0;
  chunk->end = -1;
  chunk->child = xl->first_child;
  chunk->next = NULL;

  return layout;
}


layout_t *
default_setlayout (struct xlator *xl,
		   layout_t *layout)
{
  /* TODO: if @layout@ is NULL, allocate and return */
  chunk_t *chunk = &layout->chunks;

  layout->chunk_count = 1;
  
  chunk->path = layout->path;
  chunk->path_dyn = 0;
  chunk->begin = 0;
  chunk->end = -1;
  chunk->child = xl->first_child;
  chunk->next = NULL;

  return layout;
}

int
default_open (struct xlator *xl,
	      const char *path,
	      int flags,
	      mode_t mode,
	      struct file_context *ctx)
{
  /* TODO: set context */
  layout_t layout = {};
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  while (chunk) {
    ret = chunk->child->fops->open (chunk->child,
				    chunk->path,
				    flags,
				    mode,
				    ctx);
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }

  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}

int
default_getattr (struct xlator *xl,
		 const char *path,
		 struct stat *stbuf)
{
  /* TODO: support for multiple chunks, unref layout after use */
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    errno = ENOENT;
    return -1;
  }

  return chunk->child->fops->getattr (chunk->child,
				      chunk->path,
				      stbuf);
}

int
default_readlink (struct xlator *xl,
		  const char *path,
		  char *dest,
		  size_t size)
{
  /* TODO: handle links for large files (distributed) */
  /* TODO: support for multiple chunks, unref layout after use */
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  return chunk->child->fops->readlink (chunk->child,
				       chunk->path,
				       dest,
				       size);
}

int
default_mknod (struct xlator *xl,
	       const char *path,
	       mode_t mode,
	       dev_t dev,
	       uid_t uid,
	       gid_t gid)
{
  /* TODO: support for multiple chunks, unref layout after use */
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->setlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  return chunk->child->fops->mknod (chunk->child,
				    chunk->path,
				    mode,
				    dev,
				    uid,
				    gid);
}

int
default_mkdir (struct xlator *xl,
	       const char *path,
	       mode_t mode,
	       uid_t uid,
	       gid_t gid)
{
  layout_t layout = {};
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  while (chunk) {
    ret = chunk->child->fops->mkdir (chunk->child,
				     chunk->path,
				     mode,
				     uid,
				     gid);
    /* TODO: if mkdir fails on one node, what should happen?
     cleanup nodes created so far? */
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }

  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}

int
default_unlink (struct xlator *xl,
		const char *path)
{
  layout_t layout = {};
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  while (chunk) {
    ret = chunk->child->fops->unlink (chunk->child,
				      chunk->path);

    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }

  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}

int
default_rmdir (struct xlator *xl,
	       const char *path)
{
  layout_t layout = {};
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;


  if (!layout.chunk_count) {
    return -1;
  }

  while (chunk) {
    ret = chunk->child->fops->rmdir (chunk->child,
				     chunk->path);
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }

  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}

int
default_symlink (struct xlator *xl,
		 const char *oldpath,
		 const char *newpath,
		 uid_t uid,
		 gid_t gid)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)newpath;
  xl->setlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  return chunk->child->fops->symlink (chunk->child,
				      oldpath,
				      chunk->path,
				      uid,
				      gid);
}

int
default_rename (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)newpath;
  xl->setlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->rename (chunk->child,
				     oldpath,
				     chunk->path,
				     uid,
				     gid);
}

int
default_link (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)newpath;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  return chunk->child->fops->link (chunk->child,
				   oldpath,
				   chunk->path,
				   uid,
				   gid);
}

int
default_chmod (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  layout_t layout = {};
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  while (chunk) {
    ret = chunk->child->fops->chmod (chunk->child,
				     chunk->path,
				     mode);
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }
  
  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}
	       
int
default_chown (struct xlator *xl,
	       const char *path,
	       uid_t uid,
	       gid_t gid)
{
  layout_t layout = {};
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }


  while (chunk) {
    ret = chunk->child->fops->chown (chunk->child,
				     chunk->path,
				     uid,
				     gid);
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }
  
  layout_unref (&layout);
  errno = final_errno;
  return final_ret;

}

int
default_truncate (struct xlator *xl,
		  const char *path,
		  off_t offset)
{
  layout_t layout = {};
  chunk_t *chunk;

  if (!layout.chunk_count) {
    return -1;
  }

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->truncate (chunk->child,
				       chunk->path,
				       offset);
}

int
default_utime (struct xlator *xl,
	       const char *path,
	       struct utimbuf *buf)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  if (!layout.chunk_count) {
    return -1;
  }

  return chunk->child->fops->utime (chunk->child,
				    chunk->path,
				    buf);
}

int
default_read (struct xlator *xl,
	      const char *path,
	      char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  return xl->first_child->fops->read (xl->first_child,
				      path,
				      buf,
				      size,
				      offset,
				      ctx);
				    
}

int
default_write (struct xlator *xl,
	       const char *path,
	       const char *buf,
	       size_t size,
	       off_t offset,
	       struct file_context *ctx)
{
  return xl->first_child->fops->write (xl->first_child,
				       path,
				       buf,
				       size,
				       offset,
				       ctx);
}

int
default_statfs (struct xlator *xl,
		const char *path,
		struct statvfs *buf)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->setlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->statfs (chunk->child,
				     chunk->path,
				     buf);
  
}


int
default_flush (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  return xl->first_child->fops->flush (xl->first_child,
				       path,
				       ctx);
}

int
default_release (struct xlator *xl,
		 const char *path,
		 struct file_context *ctx)
{
  return xl->first_child->fops->release (xl->first_child,
					 path,
					 ctx);
}

int
default_fsync (struct xlator *xl,
	       const char *path,
	       int flags,
	       struct file_context *ctx)
{
  return xl->first_child->fops->fsync (xl->first_child,
				       path,
				       flags,
				       ctx);
}

int
default_setxattr (struct xlator *xl,
		  const char *path,
		  const char *name,
		  const char *value,
		  size_t size,
		  int flags)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->setxattr (chunk->child,
				       chunk->path,
				       name,
				       value,
				       size,
				       flags);

}

int
default_getxattr (struct xlator *xl,
		  const char *path,
		  const char *name,
		  char *value,
		  size_t size)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->getxattr (chunk->child,
				       chunk->path,
				       name,
				       value,
				       size);
}

int
default_listxattr (struct xlator *xl,
		   const char *path,
		   char *list,
		   size_t size)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->listxattr (chunk->child,
					chunk->path,
					list,
					size);
}

int
default_removexattr (struct xlator *xl,
		     const char *path,
		     const char *name)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->removexattr (chunk->child,
					  chunk->path,
					  name);

}

int
default_opendir (struct xlator *this,
		 const char *path,
		 struct file_context *ctx)
{
  return this->first_child->fops->opendir (this->first_child, 
					   path, 
					   ctx);
}

char *
default_readdir (struct xlator *this,
		 const char *path,
		 off_t offset)
{
  return this->first_child->fops->readdir (this->first_child, 
					   path, 
					   offset);
}
		 
int
default_releasedir (struct xlator *this,
		    const char *path,
		    struct file_context *ctx)
{
  return this->first_child->fops->releasedir (this->first_child, 
					      path, 
					      ctx);
}

int
default_fsyncdir (struct xlator *this,
		  const char *path,
		  int flags,
		  struct file_context *ctx)
{
  return this->first_child->fops->fsyncdir (this->first_child,
					    path,
					    flags,
					    ctx);
}

int
default_access (struct xlator *xl,
		const char *path,
		mode_t mode)
{
  layout_t layout = {};
  chunk_t *chunk;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  return chunk->child->fops->access (chunk->child,
				     chunk->path,
				     mode);

}

int
default_ftruncate (struct xlator *xl,
		   const char *path,
		   off_t offset,
		   struct file_context *ctx)
{
  return xl->first_child->fops->ftruncate (xl->first_child,
					   path,
					   offset,
					   ctx);
}

int
default_fgetattr (struct xlator *xl,
		  const char *path,
		  struct stat *buf,
		  struct file_context *ctx)
{
  return xl->first_child->fops->fgetattr (xl->first_child,
					  path,
					  buf,
					  ctx);
}

int
default_bulk_getattr (struct xlator *xl,
		      const char *path,
		      struct bulk_stat *bstbuf)
{
  return xl->first_child->fops->bulk_getattr (xl->first_child,
					      path,
					      bstbuf);
}

int
default_stats (struct xlator *xl,
	       struct xlator_stats *stats)
{
  return xl->first_child->mgmt_ops->stats (xl->first_child,
					   stats);
}

int
default_fsck (struct xlator *xl)
{
  return xl->first_child->mgmt_ops->fsck (xl->first_child);
}

int
default_lock (struct xlator *xl,
	      const char *name)
{
  return xl->first_child->mgmt_ops->lock (xl->first_child,
					  name);
}

int
default_unlock (struct xlator *xl,
		const char *name)
{
  return xl->first_child->mgmt_ops->unlock (xl->first_child,
					    name);
}

int
default_nslookup (struct xlator *xl,
		  const char *name,
		  dict_t *ns)
{
  return xl->first_child->mgmt_ops->nslookup (xl->first_child,
					      name,
					      ns);
}

int
default_nsupdate (struct xlator *xl,
		  const char *name,
		  dict_t *ns)
{
  return xl->first_child->mgmt_ops->nsupdate (xl->first_child,
					      name,
					      ns);
}

