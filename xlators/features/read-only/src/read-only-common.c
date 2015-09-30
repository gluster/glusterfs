/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"

int32_t
ro_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        STACK_UNWIND_STRICT (xattrop, frame, -1, EROFS, NULL, xdata);
        return 0;
}

int32_t
ro_fxattrop (call_frame_t *frame, xlator_t *this,
             fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fxattrop, frame, -1, EROFS, NULL, xdata);
        return 0;
}

int32_t
ro_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, const char *basename, entrylk_cmd cmd,
            entrylk_type type, dict_t *xdata)
{
        STACK_UNWIND_STRICT (entrylk, frame, -1, EROFS, xdata);
        return 0;
}

int32_t
ro_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, const char *basename, entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fentrylk, frame, -1, EROFS, xdata);
        return 0;
}

int32_t
ro_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        STACK_UNWIND_STRICT (inodelk, frame, -1, EROFS, xdata);
        return 0;
}

int32_t
ro_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        STACK_UNWIND_STRICT (finodelk, frame, -1, EROFS, xdata);
        return 0;
}

int32_t
ro_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
       struct gf_flock *flock, dict_t *xdata)
{
        STACK_UNWIND_STRICT (lk, frame, -1, EROFS, NULL, xdata);
        return 0;
}

int32_t
ro_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setattr, frame, -1, EROFS, NULL, NULL, xdata);
	return 0;
}

int32_t
ro_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetattr, frame, -1, EROFS, NULL, NULL, xdata);
        return 0;
}


int32_t
ro_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset, dict_t *xdata)
{
        STACK_UNWIND_STRICT (truncate, frame, -1, EROFS, NULL, NULL, xdata);
	return 0;
}

int32_t
ro_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset, dict_t *xdata)
{
        STACK_UNWIND_STRICT (ftruncate, frame, -1, EROFS, NULL, NULL, xdata);
	return 0;
}

int
ro_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata)
{
        STACK_UNWIND_STRICT (mknod, frame, -1, EROFS, NULL, NULL, NULL, NULL, xdata);
        return 0;
}


int
ro_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
        STACK_UNWIND_STRICT (mkdir, frame, -1, EROFS, NULL, NULL, NULL, NULL, xdata);
        return 0;
}

int32_t
ro_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
           dict_t *xdata)
{
        STACK_UNWIND_STRICT (unlink, frame, -1, EROFS, NULL, NULL, xdata);
        return 0;
}


int
ro_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          dict_t *xdata)
{
        STACK_UNWIND_STRICT (rmdir, frame, -1, EROFS, NULL, NULL, xdata);
        return 0;
}


int
ro_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
        STACK_UNWIND_STRICT (symlink, frame, -1, EROFS, NULL, NULL, NULL,
                             NULL, xdata);
        return 0;
}



int32_t
ro_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        STACK_UNWIND_STRICT (rename, frame, -1, EROFS, NULL, NULL, NULL, NULL,
                             NULL, xdata);
        return 0;
}


int32_t
ro_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        STACK_UNWIND_STRICT (link, frame, -1, EROFS, NULL, NULL, NULL, NULL, xdata);
        return 0;
}

int32_t
ro_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (create, frame, -1, EROFS, NULL, NULL, NULL,
                             NULL, NULL, xdata);
        return 0;
}


static int32_t
ro_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd, dict_t *xdata)
{
	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
	return 0;
}

int32_t
ro_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        if (((flags & O_ACCMODE) == O_WRONLY) ||
              ((flags & O_ACCMODE) == O_RDWR)) {
                STACK_UNWIND_STRICT (open, frame, -1, EROFS, NULL, xdata);
                return 0;
	}

	STACK_WIND (frame, ro_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
	return 0;
}

int32_t
ro_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, EROFS, xdata);
	return 0;
}

int32_t
ro_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsyncdir, frame, -1, EROFS, xdata);
	return 0;
}

int32_t
ro_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        STACK_UNWIND_STRICT (writev, frame, -1, EROFS, NULL, NULL, xdata);
        return 0;
}


int32_t
ro_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setxattr, frame, -1, EROFS, xdata);
        return 0;
}

int32_t
ro_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        STACK_UNWIND_STRICT (removexattr, frame, -1, EROFS, xdata);
        return 0;
}
