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
int
default_open (struct xlator *xl,
	      const char *path,
	      int flags,
	      mode_t mode,
	      struct file_context *ctx);
int
default_getattr (struct xlator *xl,
		 const char *path,
		 struct stat *stbuf);
int
default_readlink (struct xlator *xl,
		  const char *path,
		  char *dest,
		  size_t size);
int
default_mknod (struct xlator *xl,
	       const char *path,
	       mode_t mode,
	       dev_t dev,
	       uid_t uid,
	       gid_t gid);

int
default_mkdir (struct xlator *xl,
	       const char *path,
	       mode_t mode,
	       uid_t uid,
	       gid_t gid);
int
default_unlink (struct xlator *xl,
		const char *path);
int
default_rmdir (struct xlator *xl,
	       const char *path);
int
default_symlink (struct xlator *xl,
		 const char *oldpath,
		 const char *newpath,
		 uid_t uid,
		 gid_t gid);
int
default_rename (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid);
int
default_link (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid);
int
default_chmod (struct xlator *xl,
	       const char *path,
	       mode_t mode);
int
default_chown (struct xlator *xl,
	       const char *path,
	       uid_t uid,
	       gid_t gid);
int
default_truncate (struct xlator *xl,
		  const char *path,
		  off_t offset);
int
default_utime (struct xlator *xl,
	       const char *path,
	       struct utimbuf *buf);
int
default_read (struct xlator *xl,
	      const char *path,
	      char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx);

int
default_write (struct xlator *xl,
	       const char *path,
	       const char *buf,
	       size_t size,
	       off_t offset,
	       struct file_context *ctx);

int
default_statfs (struct xlator *xl,
		const char *path,
		struct statvfs *buf);
int
default_flush (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx);
int
default_release (struct xlator *xl,
		 const char *path,
		 struct file_context *ctx);
int
default_fsync (struct xlator *xl,
	       const char *path,
	       int flags,
	       struct file_context *ctx);
int
default_setxattr (struct xlator *xl,
		  const char *path,
		  const char *name,
		  const char *value,
		  size_t size,
		  int flags);
int
default_getxattr (struct xlator *xl,
		  const char *path,
		  const char *name,
		  char *value,
		  size_t size);
int
default_listxattr (struct xlator *xl,
		   const char *path,
		   char *list,
		   size_t size);
int
default_removexattr (struct xlator *xl,
		     const char *path,
		     const char *name);
int
default_opendir (struct xlator *this,
		 const char *path,
		 struct file_context *ctx);
char *
default_readdir (struct xlator *this,
		 const char *path,
		 off_t offset);
int
default_releasedir (struct xlator *this,
		    const char *path,
		    struct file_context *ctx);
int
default_fsyncdir (struct xlator *this,
		  const char *path,
		  int flags,
		  struct file_context *ctx);
int
default_access (struct xlator *xl,
		const char *path,
		mode_t mode);
int
default_ftruncate (struct xlator *xl,
		   const char *path,
		   off_t offset,
		   struct file_context *ctx);
int
default_fgetattr (struct xlator *xl,
		  const char *path,
		  struct stat *buf,
		  struct file_context *ctx);
int
default_bulk_getattr (struct xlator *xl,
		      const char *path,
		      struct bulk_stat *bstbuf);

int 
default_stats (struct xlator *this,
	       struct xlator_stats *stats);

int 
default_fsck (struct xlator *this);

int 
default_lock (struct xlator *this, 
	      const char *name);

int 
default_unlock (struct xlator *this, 
		const char *name);

int 
default_nslookup (struct xlator *this, 
		  const char *name,
		  dict_t *ns);

int 
default_nsupdate (struct xlator *this, 
		  const char *name,
		  dict_t *ns);

#endif /* _DEFAULTS_H */
