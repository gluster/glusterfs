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

#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#include "layout.h"
#include "xlator.h"


layout_t *
default_getlayout (struct xlator *xl,
		   layout_t *layout);
layout_t *
default_setlayout (struct xlator *xl,
		   layout_t *layout);
int32_t 
default_open (struct xlator *xl,
	      const int8_t *path,
	      int32_t flags,
	      mode_t mode,
	      struct file_context *ctx);
int32_t 
default_getattr (struct xlator *xl,
		 const int8_t *path,
		 struct stat *stbuf);
int32_t 
default_readlink (struct xlator *xl,
		  const int8_t *path,
		  int8_t *dest,
		  size_t size);
int32_t 
default_mknod (struct xlator *xl,
	       const int8_t *path,
	       mode_t mode,
	       dev_t dev,
	       uid_t uid,
	       gid_t gid);

int32_t 
default_mkdir (struct xlator *xl,
	       const int8_t *path,
	       mode_t mode,
	       uid_t uid,
	       gid_t gid);
int32_t 
default_unlink (struct xlator *xl,
		const int8_t *path);
int32_t 
default_rmdir (struct xlator *xl,
	       const int8_t *path);
int32_t 
default_symlink (struct xlator *xl,
		 const int8_t *oldpath,
		 const int8_t *newpath,
		 uid_t uid,
		 gid_t gid);
int32_t 
default_rename (struct xlator *xl,
		const int8_t *oldpath,
		const int8_t *newpath,
		uid_t uid,
		gid_t gid);
int32_t 
default_link (struct xlator *xl,
	      const int8_t *oldpath,
	      const int8_t *newpath,
	      uid_t uid,
	      gid_t gid);
int32_t 
default_chmod (struct xlator *xl,
	       const int8_t *path,
	       mode_t mode);
int32_t 
default_chown (struct xlator *xl,
	       const int8_t *path,
	       uid_t uid,
	       gid_t gid);
int32_t 
default_truncate (struct xlator *xl,
		  const int8_t *path,
		  off_t offset);
int32_t 
default_utime (struct xlator *xl,
	       const int8_t *path,
	       struct utimbuf *buf);
int32_t 
default_read (struct xlator *xl,
	      const int8_t *path,
	      int8_t *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx);

int32_t 
default_write (struct xlator *xl,
	       const int8_t *path,
	       const int8_t *buf,
	       size_t size,
	       off_t offset,
	       struct file_context *ctx);

int32_t 
default_statfs (struct xlator *xl,
		const int8_t *path,
		struct statvfs *buf);
int32_t 
default_flush (struct xlator *xl,
	       const int8_t *path,
	       struct file_context *ctx);
int32_t 
default_release (struct xlator *xl,
		 const int8_t *path,
		 struct file_context *ctx);
int32_t 
default_fsync (struct xlator *xl,
	       const int8_t *path,
	       int32_t flags,
	       struct file_context *ctx);
int32_t 
default_setxattr (struct xlator *xl,
		  const int8_t *path,
		  const int8_t *name,
		  const int8_t *value,
		  size_t size,
		  int32_t flags);
int32_t 
default_getxattr (struct xlator *xl,
		  const int8_t *path,
		  const int8_t *name,
		  int8_t *value,
		  size_t size);
int32_t 
default_listxattr (struct xlator *xl,
		   const int8_t *path,
		   int8_t *list,
		   size_t size);
int32_t 
default_removexattr (struct xlator *xl,
		     const int8_t *path,
		     const int8_t *name);
int32_t 
default_opendir (struct xlator *this,
		 const int8_t *path,
		 struct file_context *ctx);
int8_t *
default_readdir (struct xlator *this,
		 const int8_t *path,
		 off_t offset);
int32_t 
default_releasedir (struct xlator *this,
		    const int8_t *path,
		    struct file_context *ctx);
int32_t 
default_fsyncdir (struct xlator *this,
		  const int8_t *path,
		  int32_t flags,
		  struct file_context *ctx);
int32_t 
default_access (struct xlator *xl,
		const int8_t *path,
		mode_t mode);
int32_t 
default_ftruncate (struct xlator *xl,
		   const int8_t *path,
		   off_t offset,
		   struct file_context *ctx);
int32_t 
default_fgetattr (struct xlator *xl,
		  const int8_t *path,
		  struct stat *buf,
		  struct file_context *ctx);
int32_t 
default_bulk_getattr (struct xlator *xl,
		      const int8_t *path,
		      struct bulk_stat *bstbuf);

int32_t 
default_stats (struct xlator *this,
	       struct xlator_stats *stats);

int32_t 
default_fsck (struct xlator *this);

int32_t 
default_lock (struct xlator *this, 
	      const int8_t *name);

int32_t 
default_unlock (struct xlator *this, 
		const int8_t *name);

int32_t 
default_nslookup (struct xlator *this, 
		  const int8_t *name,
		  dict_t *ns);

int32_t 
default_nsupdate (struct xlator *this, 
		  const int8_t *name,
		  dict_t *ns);

#endif /* _DEFAULTS_H */
