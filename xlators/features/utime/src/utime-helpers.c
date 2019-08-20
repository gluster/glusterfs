/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "utime-helpers.h"
#include "utime.h"

void
gl_timespec_get(struct timespec *ts)
{
#ifdef TIME_UTC
    timespec_get(ts, TIME_UTC);
#else
    timespec_now_realtime(ts);
#endif
}

void
utime_update_attribute_flags(call_frame_t *frame, xlator_t *this,
                             glusterfs_fop_t fop)
{
    utime_priv_t *utime_priv = NULL;

    if (!frame || !this) {
        goto out;
    }

    utime_priv = this->private;

    switch (fop) {
        case GF_FOP_SETXATTR:
        case GF_FOP_FSETXATTR:
            frame->root->flags |= MDATA_CTIME;
            break;

        case GF_FOP_FALLOCATE:
        case GF_FOP_ZEROFILL:
            frame->root->flags |= MDATA_MTIME;
            frame->root->flags |= MDATA_ATIME;
            break;

        case GF_FOP_OPENDIR:
        case GF_FOP_OPEN:
        case GF_FOP_READ:
            if (!utime_priv->noatime) {
                frame->root->flags |= MDATA_ATIME;
            }
            break;
        case GF_FOP_MKNOD:
        case GF_FOP_MKDIR:
        case GF_FOP_SYMLINK:
        case GF_FOP_CREATE:
            frame->root->flags |= MDATA_ATIME;
            frame->root->flags |= MDATA_CTIME;
            frame->root->flags |= MDATA_MTIME;
            frame->root->flags |= MDATA_PAR_CTIME;
            frame->root->flags |= MDATA_PAR_MTIME;
            break;

        case GF_FOP_UNLINK:
        case GF_FOP_RMDIR:
            frame->root->flags |= MDATA_CTIME;
            frame->root->flags |= MDATA_PAR_CTIME;
            frame->root->flags |= MDATA_PAR_MTIME;
            break;

        case GF_FOP_WRITE:
            frame->root->flags |= MDATA_MTIME;
            frame->root->flags |= MDATA_CTIME;
            break;

        case GF_FOP_LINK:
        case GF_FOP_RENAME:
            frame->root->flags |= MDATA_CTIME;
            frame->root->flags |= MDATA_PAR_CTIME;
            frame->root->flags |= MDATA_PAR_MTIME;
            break;

        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
            frame->root->flags |= MDATA_CTIME;
            frame->root->flags |= MDATA_MTIME;
            break;

        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
            frame->root->flags |= MDATA_CTIME;
            break;

        case GF_FOP_COPY_FILE_RANGE:
            /* Below 2 are for destination fd */
            frame->root->flags |= MDATA_CTIME;
            frame->root->flags |= MDATA_MTIME;
            /* Below flag is for the source fd */
            if (!utime_priv->noatime) {
                frame->root->flags |= MDATA_ATIME;
            }
            break;
        default:
            frame->root->flags = 0;
    }
out:
    return;
}
