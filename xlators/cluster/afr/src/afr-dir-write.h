/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __DIR_WRITE_H__
#define __DIR_WRITE_H__

int32_t
afr_create (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t flags, mode_t mode, fd_t *fd);

int32_t
afr_mknod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dev_t dev);

int32_t
afr_mkdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode);

int32_t
afr_unlink (call_frame_t *frame, xlator_t *this,
	    loc_t *loc);

int32_t
afr_rmdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc);

int32_t
afr_link (call_frame_t *frame, xlator_t *this,
	  loc_t *oldloc, loc_t *newloc);

int32_t
afr_rename (call_frame_t *frame, xlator_t *this,
	    loc_t *oldloc, loc_t *newloc);

int32_t
afr_symlink (call_frame_t *frame, xlator_t *this,
	     const char *linkpath, loc_t *oldloc);

int32_t
afr_setdents (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, int32_t flags, dir_entry_t *entries, int32_t count);

#endif /* __DIR_WRITE_H__ */
