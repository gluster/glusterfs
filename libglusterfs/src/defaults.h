/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/* libglusterfs/src/defaults.h:
       This file contains definition of default fops and mops functions.
*/

#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#include "xlator.h"

/* Management Operations */

int32_t default_stats (call_frame_t *frame,
		       xlator_t *this,
		       int32_t flags);

int32_t default_fsck (call_frame_t *frame,
		      xlator_t *this,
		      int32_t flags);

int32_t default_lock (call_frame_t *frame,
		      xlator_t *this,
		      const char *name);

int32_t default_unlock (call_frame_t *frame,
			xlator_t *this,
			const char *name);

int32_t default_listlocks (call_frame_t *frame,
			   xlator_t *this,
			   const char *pattern);

int32_t default_getspec (call_frame_t *frame,
			 xlator_t *this,
			 int32_t flag);


/* FileSystem operations */
int32_t default_lookup (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc);

int32_t default_forget (call_frame_t *frame,
			xlator_t *this,
			inode_t *inode);

int32_t default_stat (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc);

int32_t default_fstat (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd);

int32_t default_chmod (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       mode_t mode);

int32_t default_fchmod (call_frame_t *frame,
			xlator_t *this,
			fd_t *fd,
			mode_t mode);

int32_t default_chown (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       uid_t uid,
		       gid_t gid);

int32_t default_fchown (call_frame_t *frame,
			xlator_t *this,
			fd_t *fd,
			uid_t uid,
			gid_t gid);

int32_t default_truncate (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  off_t offset);

int32_t default_ftruncate (call_frame_t *frame,
			   xlator_t *this,
			   fd_t *fd,
			   off_t offset);

int32_t default_utimens (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc,
			 struct timespec tv[2]);

int32_t default_access (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			int32_t mask);

int32_t default_readlink (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  size_t size);

int32_t default_mknod (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       mode_t mode,
		       dev_t rdev);

int32_t default_mkdir (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       mode_t mode);

int32_t default_unlink (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc);

int32_t default_rmdir (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc);

int32_t default_symlink (call_frame_t *frame,
			 xlator_t *this,
			 const char *linkpath,
			 loc_t *loc);

int32_t default_rename (call_frame_t *frame,
			xlator_t *this,
			loc_t *oldloc,
			loc_t *newloc);

int32_t default_link (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      const char *newpath);

int32_t default_create (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			int32_t flags,
			mode_t mode, fd_t *fd);

int32_t default_open (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      int32_t flags, fd_t *fd);

int32_t default_readv (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd,
		       size_t size,
		       off_t offset);

int32_t default_writev (call_frame_t *frame,
			xlator_t *this,
			fd_t *fd,
			struct iovec *vector,
			int32_t count,
			off_t offset);

int32_t default_flush (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd);

int32_t default_close (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd);

int32_t default_fsync (call_frame_t *frame,
		       xlator_t *this,
		       fd_t *fd,
		       int32_t datasync);

int32_t default_opendir (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc, fd_t *fd);

int32_t default_readdir (call_frame_t *frame,
			 xlator_t *this,
			 size_t size,
			 off_t offset,
			 fd_t *fd);

int32_t default_closedir (call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd);

int32_t default_fsyncdir (call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd,
			  int32_t datasync);

int32_t default_statfs (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc);

int32_t default_setxattr (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  dict_t *dict,
			  int32_t flags);

int32_t default_getxattr (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc);

int32_t default_removexattr (call_frame_t *frame,
			     xlator_t *this,
			     loc_t *loc,
			     const char *name);

int32_t default_lk (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    int32_t cmd,
		    struct flock *flock);
		 
int32_t default_writedir (call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd,
			  int32_t flags,
			  dir_entry_t *entries,
			  int32_t count);

int32_t default_notify (xlator_t *this,
			int32_t event,
			void *data,
			...);

#endif /* _DEFAULTS_H */
