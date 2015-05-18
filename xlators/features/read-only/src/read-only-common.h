/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "xlator.h"
#include "defaults.h"

gf_boolean_t
is_readonly_or_worm_enabled (xlator_t *this);

int32_t
ro_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata);

int32_t
ro_fxattrop (call_frame_t *frame, xlator_t *this,
             fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata);

int32_t
ro_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, const char *basename, entrylk_cmd cmd,
            entrylk_type type, dict_t *xdata);

int32_t
ro_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, const char *basename, entrylk_cmd cmd, entrylk_type
             type, dict_t *xdata);

int32_t
ro_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata);

int32_t
ro_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata);

int32_t
ro_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
       struct gf_flock *flock, dict_t *xdata);

int32_t
ro_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid, dict_t *xdata);

int32_t
ro_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid, dict_t *xdata);


int32_t
ro_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset, dict_t *xdata);

int32_t
ro_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset, dict_t *xdata);

int
ro_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata);

int
ro_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata);

int32_t
ro_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
           dict_t *xdata);

int
ro_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          dict_t *xdata);


int
ro_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, mode_t umask, dict_t *xdata);

int32_t
ro_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc, dict_t *xdata);

int32_t
ro_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc, dict_t *xdata);

int32_t
ro_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata);

int32_t
ro_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata);

int32_t
ro_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata);

int32_t
ro_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags, dict_t *xdata);

int32_t
ro_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, uint32_t flags, struct iobref *iobref, dict_t *xdata);

int32_t
ro_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata);

int32_t
ro_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata);
