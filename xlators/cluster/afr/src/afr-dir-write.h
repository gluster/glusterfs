/*
   Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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
            loc_t *loc, int32_t flags, mode_t mode,
            mode_t umask, fd_t *fd, dict_t *xdata);

int32_t
afr_mknod (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, dev_t dev, mode_t umask, dict_t *xdata);

int32_t
afr_mkdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata);

int32_t
afr_unlink (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int xflag, dict_t *xdata);

int32_t
afr_rmdir (call_frame_t *frame, xlator_t *this,
	   loc_t *loc, int flags, dict_t *xdata);

int32_t
afr_link (call_frame_t *frame, xlator_t *this,
	  loc_t *oldloc, loc_t *newloc, dict_t *xdata);

int32_t
afr_rename (call_frame_t *frame, xlator_t *this,
	    loc_t *oldloc, loc_t *newloc, dict_t *xdata);

int
afr_symlink (call_frame_t *frame, xlator_t *this,
	     const char *linkpath, loc_t *oldloc, mode_t umask, dict_t *params);

#endif /* __DIR_WRITE_H__ */
