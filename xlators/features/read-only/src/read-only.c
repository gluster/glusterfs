/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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
            gf_xattrop_flags_t flags, dict_t *dict)
{
        STACK_UNWIND_STRICT (xattrop, frame, -1, EROFS, NULL);
        return 0;
}

int32_t
ro_fxattrop (call_frame_t *frame, xlator_t *this,
             fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict)
{
        STACK_UNWIND_STRICT (fxattrop, frame, -1, EROFS, NULL);
        return 0;
}

int32_t
ro_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, const char *basename, entrylk_cmd cmd,
            entrylk_type type)
{
        STACK_UNWIND_STRICT (entrylk, frame, -1, EROFS);
        return 0;
}

int32_t
ro_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, const char *basename, entrylk_cmd cmd, entrylk_type type)
{
        STACK_UNWIND_STRICT (fentrylk, frame, -1, EROFS);
        return 0;
}

int32_t
ro_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, int32_t cmd, struct gf_flock *lock)
{
        STACK_UNWIND_STRICT (inodelk, frame, -1, EROFS);
        return 0;
}

int32_t
ro_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, int32_t cmd, struct gf_flock *lock)
{
        STACK_UNWIND_STRICT (finodelk, frame, -1, EROFS);
        return 0;
}

int32_t
ro_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
       struct gf_flock *flock)
{
        STACK_UNWIND_STRICT (lk, frame, -1, EROFS, NULL);
        return 0;
}

int32_t
ro_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid)
{
        STACK_UNWIND_STRICT (setattr, frame, -1, EROFS, NULL, NULL);
	return 0;
}

int32_t
ro_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid)
{
        STACK_UNWIND_STRICT (fsetattr, frame, -1, EROFS, NULL, NULL);
        return 0;
}


int32_t
ro_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        STACK_UNWIND_STRICT (truncate, frame, -1, EROFS, NULL, NULL);
	return 0;
}

int32_t
ro_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        STACK_UNWIND_STRICT (ftruncate, frame, -1, EROFS, NULL, NULL);
	return 0;
}

int
ro_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, dict_t *params)
{
        STACK_UNWIND_STRICT (mknod, frame, -1, EROFS, NULL, NULL, NULL, NULL);
        return 0;
}


int
ro_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dict_t *params)
{
        STACK_UNWIND_STRICT (mkdir, frame, -1, EROFS, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
ro_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        STACK_UNWIND_STRICT (unlink, frame, -1, EROFS, NULL, NULL);
        return 0;
}


int
ro_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        STACK_UNWIND_STRICT (rmdir, frame, -1, EROFS, NULL, NULL);
        return 0;
}


int
ro_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, dict_t *params)
{
        STACK_UNWIND_STRICT (symlink, frame, -1, EROFS, NULL, NULL, NULL, NULL);
        return 0;
}



int32_t
ro_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        STACK_UNWIND_STRICT (rename, frame, -1, EROFS, NULL, NULL, NULL, NULL,
                             NULL);
        return 0;
}


int32_t
ro_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        STACK_UNWIND_STRICT (link, frame, -1, EROFS, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
ro_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd, dict_t *params)
{
        STACK_UNWIND_STRICT (create, frame, -1, EROFS, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}


static int32_t
ro_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd)
{
	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
	return 0;
}

int32_t
ro_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        if (((flags & O_ACCMODE) == O_WRONLY) ||
              ((flags & O_ACCMODE) == O_RDWR)) {
                STACK_UNWIND_STRICT (open, frame, -1, EROFS, NULL);
                return 0;
	}

	STACK_WIND (frame, ro_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);
	return 0;
}

int32_t
ro_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, EROFS);
	return 0;
}

int32_t
ro_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        STACK_UNWIND_STRICT (fsyncdir, frame, -1, EROFS);
	return 0;
}

int32_t
ro_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, struct iobref *iobref)
{
        STACK_UNWIND_STRICT (writev, frame, -1, EROFS, NULL, NULL);
        return 0;
}


int32_t
ro_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags)
{
        STACK_UNWIND_STRICT (setxattr, frame, -1, EROFS);
        return 0;
}

int32_t
ro_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name)
{
        STACK_UNWIND_STRICT (removexattr, frame, -1, EROFS);
        return 0;
}

int32_t
init (xlator_t *this)
{
	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"translator not configured with exactly one child");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	return 0;
}


void
fini (xlator_t *this)
{
	return;
}


struct xlator_fops fops = {
        .mknod       = ro_mknod,
        .mkdir       = ro_mkdir,
        .unlink      = ro_unlink,
        .rmdir       = ro_rmdir,
        .symlink     = ro_symlink,
        .rename      = ro_rename,
        .link        = ro_link,
        .truncate    = ro_truncate,
        .open        = ro_open,
        .writev      = ro_writev,
        .setxattr    = ro_setxattr,
        .fsetxattr   = ro_fsetxattr,
        .removexattr = ro_removexattr,
        .fsyncdir    = ro_fsyncdir,
        .ftruncate   = ro_ftruncate,
        .create      = ro_create,
        .setattr     = ro_setattr,
        .fsetattr    = ro_fsetattr,
        .xattrop     = ro_xattrop,
        .fxattrop    = ro_fxattrop,
        .inodelk     = ro_inodelk,
        .finodelk    = ro_finodelk,
        .entrylk     = ro_entrylk,
        .fentrylk    = ro_fentrylk,
        .lk          = ro_lk,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key = {NULL} },
};
