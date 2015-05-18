/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "read-only.h"
#include "read-only-mem-types.h"
#include "defaults.h"

gf_boolean_t
is_readonly_or_worm_enabled (xlator_t *this)
{
        read_only_priv_t  *priv                     = NULL;
        gf_boolean_t       readonly_or_worm_enabled = _gf_false;

        priv = this->private;
        GF_ASSERT (priv);

        readonly_or_worm_enabled = priv->readonly_or_worm_enabled;

        return readonly_or_worm_enabled;
}

static int
_check_key_is_zero_filled (dict_t *d, char *k, data_t *v,
                           void *tmp)
{
        if (mem_0filled ((const char *)v->data, v->len)) {
                /* -1 means, no more iterations, treat as 'break' */
                return -1;
        }
        return 0;
}

int32_t
ro_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        gf_boolean_t allzero = _gf_false;
        int     ret = 0;

        ret = dict_foreach (dict, _check_key_is_zero_filled, NULL);
        if (ret == 0)
                allzero = _gf_true;

        if (is_readonly_or_worm_enabled (this) && !allzero)
                STACK_UNWIND_STRICT (xattrop, frame, -1, EROFS, NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->xattrop,
                                 loc, flags, dict, xdata);
        return 0;
}

int32_t
ro_fxattrop (call_frame_t *frame, xlator_t *this,
             fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        gf_boolean_t allzero = _gf_false;
        int     ret = 0;

        ret = dict_foreach (dict, _check_key_is_zero_filled, NULL);
        if (ret == 0)
                allzero = _gf_true;

        if (is_readonly_or_worm_enabled (this) && !allzero)
                STACK_UNWIND_STRICT (fxattrop, frame, -1, EROFS, NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->fxattrop,
                                 fd, flags, dict, xdata);

        return 0;
}

int32_t
ro_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, const char *basename, entrylk_cmd cmd,
            entrylk_type type, dict_t *xdata)
{
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD(this)->fops->entrylk,
                         volume, loc, basename, cmd, type, xdata);

        return 0;
}

int32_t
ro_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD(this)->fops->fentrylk,
                         volume, fd, basename, cmd, type, xdata);

        return 0;
}

int32_t
ro_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD(this)->fops->inodelk,
                         volume, loc, cmd, lock, xdata);

        return 0;
}

int32_t
ro_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD(this)->fops->finodelk,
                         volume, fd, cmd, lock, xdata);

        return 0;
}

int32_t
ro_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int cmd,
       struct gf_flock *flock, dict_t *xdata)
{
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD(this)->fops->lk, fd, cmd, flock,
                         xdata);

        return 0;
}

int32_t
ro_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (setattr, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->setattr, loc, stbuf,
                                 valid, xdata);

        return 0;
}

int32_t
ro_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (fsetattr, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->fsetattr, fd, stbuf,
                                 valid, xdata);

        return 0;
}


int32_t
ro_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (truncate, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->truncate, loc, offset,
                                 xdata);

	return 0;
}

int32_t
ro_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (ftruncate, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->ftruncate, fd, offset,
                                 xdata);

	return 0;
}

int
ro_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (mknod, frame, -1, EROFS, NULL, NULL, NULL,
                                     NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->mknod, loc, mode,
                                 rdev, umask, xdata);

	return 0;
}


int
ro_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (mkdir, frame, -1, EROFS, NULL, NULL, NULL,
                                     NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->mkdir, loc, mode,
                                 umask, xdata);

        return 0;
}

int32_t
ro_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
           dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (unlink, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->unlink, loc, xflag,
                                 xdata);

        return 0;
}


int
ro_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (rmdir, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->rmdir, loc, flags,
                                 xdata);

        return 0;
}


int
ro_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (symlink, frame, -1, EROFS, NULL, NULL,
                                     NULL, NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->symlink, linkpath,
                                 loc, umask, xdata);

        return 0;
}



int32_t
ro_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (rename, frame, -1, EROFS, NULL, NULL, NULL,
                                     NULL, NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->rename, oldloc,
                                 newloc, xdata);

        return 0;
}


int32_t
ro_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (link, frame, -1, EROFS, NULL, NULL, NULL,
                                     NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->link, oldloc, newloc,
                                 xdata);

        return 0;
}

int32_t
ro_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (create, frame, -1, EROFS, NULL, NULL, NULL,
                                     NULL, NULL, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->create, loc, flags,
                                 mode, umask, fd, xdata);

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
        if (is_readonly_or_worm_enabled (this) &&
            (((flags & O_ACCMODE) == O_WRONLY) ||
              ((flags & O_ACCMODE) == O_RDWR))) {
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
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, EROFS, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->fsetxattr, fd, dict,
                                 flags, xdata);

        return 0;
}

int32_t
ro_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, EROFS, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->fsyncdir, fd, flags,
                                 xdata);

	return 0;
}

int32_t
ro_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (writev, frame, -1, EROFS, NULL, NULL,
                                     xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->writev, fd, vector,
                                 count, off, flags, iobref, xdata);

	return 0;
}


int32_t
ro_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (setxattr, frame, -1, EROFS, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->setxattr, loc, dict,
                                 flags, xdata);

	return 0;
}

int32_t
ro_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this))
                STACK_UNWIND_STRICT (removexattr, frame, -1, EROFS, xdata);
        else
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD(this)->fops->removexattr, loc,
                                 name, xdata);

        return 0;
}
