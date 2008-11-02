/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

typedef struct {
	int op_count;
} eg_t;


/* Management Operations */

int32_t error_gen_stats (call_frame_t *frame,
			 xlator_t *this,
			 int32_t flags);

int32_t error_gen_fsck (call_frame_t *frame,
			xlator_t *this,
			int32_t flags);

int32_t error_gen_lock (call_frame_t *frame,
			xlator_t *this,
			const char *name);

int32_t error_gen_unlock (call_frame_t *frame,
			  xlator_t *this,
			  const char *name);

int32_t error_gen_listlocks (call_frame_t *frame,
			     xlator_t *this,
			     const char *pattern);

int32_t error_gen_getspec (call_frame_t *frame,
			   xlator_t *this,
			   int32_t flag);

int32_t error_gen_checksum (call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    int32_t flag);


/* FileSystem operations */
int32_t error_gen_lookup (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  int32_t need_xattr);

int32_t error_gen_stat (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc);

int32_t error_gen_fstat (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd);

int32_t error_gen_chmod (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc,
			 mode_t mode);

int32_t error_gen_fchmod (call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd,
			  mode_t mode);

int32_t error_gen_chown (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc,
			 uid_t uid,
			 gid_t gid);

int32_t error_gen_fchown (call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd,
			  uid_t uid,
			  gid_t gid);

int32_t error_gen_truncate (call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    off_t offset);

int32_t error_gen_ftruncate (call_frame_t *frame,
			     xlator_t *this,
			     fd_t *fd,
			     off_t offset);

int32_t error_gen_utimens (call_frame_t *frame,
			   xlator_t *this,
			   loc_t *loc,
			   struct timespec tv[2]);

int32_t error_gen_access (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  int32_t mask);

int32_t error_gen_readlink (call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    size_t size);

int32_t error_gen_mknod (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc,
			 mode_t mode,
			 dev_t rdev);

int32_t error_gen_mkdir (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc,
			 mode_t mode);

int32_t error_gen_unlink (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc);

int32_t error_gen_rmdir (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc);

int32_t error_gen_rmelem (call_frame_t *frame,
			  xlator_t *this,
			  const char *path);

int32_t error_gen_symlink (call_frame_t *frame,
			   xlator_t *this,
			   const char *linkpath,
			   loc_t *loc);

int32_t error_gen_rename (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *oldloc,
			  loc_t *newloc);

int32_t error_gen_link (call_frame_t *frame,
			xlator_t *this,
			loc_t *oldloc,
			loc_t *newloc);

int32_t error_gen_create (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  int32_t flags,
			  mode_t mode, fd_t *fd);

int32_t error_gen_open (call_frame_t *frame,
			xlator_t *this,
			loc_t *loc,
			int32_t flags, fd_t *fd);

int32_t error_gen_readv (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd,
			 size_t size,
			 off_t offset);

int32_t error_gen_writev (call_frame_t *frame,
			  xlator_t *this,
			  fd_t *fd,
			  struct iovec *vector,
			  int32_t count,
			  off_t offset);

int32_t error_gen_flush (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd);

int32_t error_gen_fsync (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd,
			 int32_t datasync);

int32_t error_gen_opendir (call_frame_t *frame,
			   xlator_t *this,
			   loc_t *loc, fd_t *fd);

int32_t error_gen_getdents (call_frame_t *frame,
			    xlator_t *this,
			    fd_t *fd,
			    size_t size,
			    off_t offset,
			    int32_t flag);

int32_t error_gen_fsyncdir (call_frame_t *frame,
			    xlator_t *this,
			    fd_t *fd,
			    int32_t datasync);

int32_t error_gen_statfs (call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc);

int32_t error_gen_setxattr (call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    dict_t *dict,
			    int32_t flags);

int32_t error_gen_getxattr (call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    const char *name);

int32_t error_gen_removexattr (call_frame_t *frame,
			       xlator_t *this,
			       loc_t *loc,
			       const char *name);

int32_t error_gen_lk (call_frame_t *frame,
		      xlator_t *this,
		      fd_t *fd,
		      int32_t cmd,
		      struct flock *flock);

int32_t error_gen_inodelk (call_frame_t *frame, xlator_t *this,
			   loc_t *loc, int32_t cmd, struct flock *flock);

int32_t error_gen_finodelk (call_frame_t *frame, xlator_t *this,
			    fd_t *fd, int32_t cmd, struct flock *flock);

int32_t error_gen_entrylk (call_frame_t *frame, xlator_t *this,
			   loc_t *loc, const char *basename,
			   gf_dir_lk_cmd cmd, gf_dir_lk_type type);

int32_t error_gen_fentrylk (call_frame_t *frame, xlator_t *this,
			    fd_t *fd, const char *basename,
			    gf_dir_lk_cmd cmd, gf_dir_lk_type type);

int32_t error_gen_readdir (call_frame_t *frame,
			   xlator_t *this,
			   fd_t *fd,
			   size_t size, off_t off);
		 
int32_t error_gen_setdents (call_frame_t *frame,
			    xlator_t *this,
			    fd_t *fd,
			    int32_t flags,
			    dir_entry_t *entries,
			    int32_t count);

int32_t error_gen_xattrop (call_frame_t *frame,
			   xlator_t *this,
			   loc_t *loc,
			   gf_xattrop_flags_t flags,
			   dict_t *dict);

int32_t error_gen_fxattrop (call_frame_t *frame,
			    xlator_t *this,
			    fd_t *fd,
			    gf_xattrop_flags_t flags,
			    dict_t *dict);

int32_t error_gen_notify (xlator_t *this,
			  int32_t event,
			  void *data,
			  ...);

int32_t error_gen_forget (xlator_t *this,
			  inode_t *inode);

int32_t error_gen_release (xlator_t *this,
			   fd_t *fd);

int32_t error_gen_releasedir (xlator_t *this,
			      fd_t *fd);

#endif /* _DEFAULTS_H */
