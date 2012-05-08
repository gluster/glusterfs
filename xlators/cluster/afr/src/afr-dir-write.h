/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
