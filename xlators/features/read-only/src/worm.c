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
#include "read-only-common.h"

static int32_t
worm_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
               int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int32_t
worm_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           fd_t *fd, dict_t *xdata)
{
        if ((((flags & O_ACCMODE) == O_WRONLY) ||
              ((flags & O_ACCMODE) == O_RDWR)) &&
              !(flags & O_APPEND)) {
                STACK_UNWIND_STRICT (open, frame, -1, EROFS, NULL, NULL);
                return 0;
        }

        STACK_WIND (frame, worm_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
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
        .open        = worm_open,

        .unlink      = ro_unlink,
        .rmdir       = ro_rmdir,
        .rename      = ro_rename,
        .truncate    = ro_truncate,
        .removexattr = ro_removexattr,
        .fsyncdir    = ro_fsyncdir,
        .xattrop     = ro_xattrop,
        .inodelk     = ro_inodelk,
        .finodelk    = ro_finodelk,
        .entrylk     = ro_entrylk,
        .fentrylk    = ro_fentrylk,
        .lk          = ro_lk,
};

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key = {NULL} },
};

