/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"

int32_t
ro_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict);

int32_t
ro_fxattrop (call_frame_t *frame, xlator_t *this,
             fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict);

int32_t
ro_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, const char *basename, entrylk_cmd cmd,
            entrylk_type type);

int32_t
ro_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, const char *basename, entrylk_cmd cmd, entrylk_type
             type);

int32_t
ro_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, int32_t cmd, struct gf_flock *lock);

int32_t
ro_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, int32_t cmd, struct gf_flock *lock);

int32_t
ro_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
       struct gf_flock *flock);

int32_t
ro_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid);

int32_t
ro_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid);


int32_t
ro_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset);

int32_t
ro_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset);

int
ro_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, dict_t *params);

int
ro_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dict_t *params);

int32_t
ro_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc);

int
ro_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags);


int
ro_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, dict_t *params);

int32_t
ro_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc);

int32_t
ro_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc);

int32_t
ro_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd, dict_t *params);

int32_t
ro_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags);

int32_t
ro_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags);

int32_t
ro_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags);

int32_t
ro_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, struct iobref *iobref);

int32_t
ro_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags);

int32_t
ro_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name);
