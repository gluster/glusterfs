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

#include "xlator.h"

int32_t
default_getattr (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path);

int32_t
default_chmod (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       mode_t mode);

int32_t
default_chown (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       uid_t uid,
	       gid_t gid);

int32_t
default_truncate (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path,
		  off_t offset);

int32_t
default_ftruncate (call_frame_t *frame,
		   xlator_t *this,
		   struct file_context *fd,
		   off_t offset);

int32_t
default_utime (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       struct utimbuf *buf);

int32_t
default_access (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path,
		mode_t mode);

int32_t
default_readlink (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path,
		  size_t size);

int32_t
default_mknod (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       mode_t mode,
	       dev_t dev);

int32_t
default_mkdir (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path,
	       mode_t mode);

int32_t
default_unlink (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path);

int32_t
default_rmdir (call_frame_t *frame,
	       xlator_t *this,
	       const int8_t *path);

int32_t
default_symlink (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *oldpath,
		 const int8_t *newpath);

int32_t
default_rename (call_frame_t *frame,
		xlator_t *this,
		const int8_t *oldpath,
		const int8_t *newpath);

int32_t
default_link (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *oldpath,
	      const int8_t *newpath);

int32_t
default_create (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path,
		mode_t mode);

int32_t
default_open (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path,
	      int32_t flags,
	      mode_t mode);

int32_t
default_read (call_frame_t *frame,
	      xlator_t *this,
	      struct file_context *fd,
	      size_t size,
	      off_t offset);

int32_t
default_write (call_frame_t *frame,
	       xlator_t *this,
	       struct file_context *fd,
	       int8_t *buf,
	       size_t size,
	       off_t offset);

int32_t
default_flush (call_frame_t *frame,
	       xlator_t *this,
	       struct file_context *fd);

int32_t
default_release (call_frame_t *frame,
		 xlator_t *this,
		 struct file_context *fd);

int32_t
default_fsync (call_frame_t *frame,
	       xlator_t *this,
	       struct file_context *fd,
	       int32_t flags);
int32_t
default_fgetattr (call_frame_t *frame,
		  xlator_t *this,
		  struct file_context *fd);

int32_t
default_opendir (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path);

int32_t
default_readdir (call_frame_t *frame,
		 xlator_t *this,
		 const int8_t *path);

int32_t
default_releasedir (call_frame_t *frame,
		    xlator_t *this,
		    struct file_context *fd);

int32_t
default_fsyncdir (call_frame_t *frame,
		  xlator_t *this,
		  struct file_context *fd,
		  int32_t flags);

int32_t
default_statfs (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path);

int32_t
default_setxattr (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path,
		  const int8_t *name,
		  const int8_t *value,
		  size_t size,
		  int32_t flags);

int32_t
default_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path,
		  const int8_t *name,
		  size_t size);

int32_t
default_listxattr (call_frame_t *frame,
		   xlator_t *this,
		   const int8_t *path,
		   size_t size);

int32_t
default_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     const int8_t *path,
		     const int8_t *name);

int32_t
default_stats (call_frame_t *frame,
	       xlator_t *this,
	       int32_t flags);

int32_t
default_fsck (call_frame_t *frame,
	      xlator_t *this,
	      int32_t flags);

int32_t
default_lock (call_frame_t *frame,
	      xlator_t *this,
	      const int8_t *path);

int32_t
default_unlock (call_frame_t *frame,
		xlator_t *this,
		const int8_t *path);

int32_t
default_listlocks (call_frame_t *frame,
		   xlator_t *this,
		   const int8_t *pattern);

int32_t
default_nslookup (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path);

int32_t
default_nsupdate (call_frame_t *frame,
		  xlator_t *this,
		  const int8_t *path,
		  dict_t *ns);

#endif /* _DEFAULTS_H */
