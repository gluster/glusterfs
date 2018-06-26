/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX_SNAP_H
#define _POSIX_SNAP_H

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/vfs.h> /* for statfs syscall */
#include <sys/ioctl.h>
#ifdef GF_LINUX_HOST_OS
#include <alloca.h>
#endif

#include <linux/fs.h>
#include <linux/btrfs.h>

#include "common-utils.h"

#include "posix-handle.h"
#include "posix.h"
#include "xlator.h"
#include "syscall.h"
#include "posix-messages.h"
#include "posix-metadata.h"
#include "compat-errno.h"
#include "posix-inode-handle.h"


#define POSIX_SNAP_PATH(this, _buf)  do {                               \
                struct posix_private *_priv = NULL;                     \
                _priv = this->private;                                  \
                snprintf (_buf, sizeof (_buf), "%s/%s",                 \
                          _priv->base_path, GF_SNAPS_PATH);             \
        } while (0)

#define SNAP_CONT_ABSPATH_LEN(this) (POSIX_BASE_PATH_LEN(this) +        \
                                     SLEN("/" GF_SNAPS_PATH "/00/00/"   \
                                     UUID0_STR) + 1)

#define SNAP_MAKE_NAME(name, gfid) do { \
        snprintf (name, NAME_MAX, "%s-%"PRIu64,                 \
                  uuid_utoa (gfid), (uint64_t) time (NULL));    \
        } while (0)

#define SNAP_MAKE_CONT_ABSPATH(var, this, gfid) do {                       \
        struct posix_private * __priv = this->private;                  \
        int __len = SNAP_CONT_ABSPATH_LEN(this);                      \
        var = alloca(__len);                                            \
        snprintf(var, __len, "%s/" GF_SNAPS_PATH "/%02x/%02x/%s",      \
                 __priv->base_path, gfid[0], gfid[1], uuid_utoa(gfid)); \
        } while (0)

int
posix_handle_snap_path (xlator_t *this, uuid_t gfid, const char *basename,
                        char *buf, size_t buflen);

int
posix_take_snap (xlator_t *this, const char *path, char *snap,
                         char *snap_name);

int
posix_snap (xlator_t *this, const char *oldpath, uuid_t gfid, char *name);

int32_t
posix_file_snap_create (xlator_t *this, loc_t *loc, fd_t *fd,
                        dict_t *dict, dict_t *xdata);

int
posix_remove_file_snapshots (xlator_t *this, uuid_t gfid);

int
posix_snap_remove (xlator_t *this, const char *oldpath, uuid_t gfid,
                   char *name);

int32_t
posix_file_snap_remove (xlator_t *this, loc_t *loc, fd_t *fd, dict_t *xattr,
                        dict_t *xdata);

#endif /* !_POSIX_SNAP_H */
