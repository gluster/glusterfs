/*Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _MARKER_H
#define _MARKER_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "marker-quota.h"
#include "xlator.h"
#include "defaults.h"
#include "uuid.h"
#include "call-stub.h"

#define MARKER_XATTR_PREFIX "trusted.glusterfs"
#define XTIME               "xtime"
#define VOLUME_MARK         "volume-mark"
#define VOLUME_UUID         "volume-uuid"
#define TIMESTAMP_FILE      "timestamp-file"

enum {
        GF_QUOTA=1,
        GF_XTIME=2
};

/*initialize the local variable*/
#define MARKER_INIT_LOCAL(_frame,_local) do {                   \
                _frame->local = _local;                         \
                _local->pid = _frame->root->pid;                \
                memset (&_local->loc, 0, sizeof (loc_t));       \
                _local->ref = 1;                                \
                _local->uid = -1;                               \
                _local->gid = -1;                               \
                LOCK_INIT (&_local->lock);                      \
                _local->oplocal = NULL;                         \
        } while (0)

/* try alloc and if it fails, goto label */
#define ALLOCATE_OR_GOTO(var, type, label) do {                  \
                var = GF_CALLOC (sizeof (type), 1,               \
                                 gf_marker_mt_##type);           \
                if (!var) {                                      \
                        gf_log (this->name, GF_LOG_ERROR,        \
                                "out of memory :(");             \
                        goto label;                              \
                }                                                \
        } while (0)

#define _MARKER_SET_UID_GID(dest, src)          \
        do {                                    \
                if (src->uid != -1 &&           \
                    src->gid != -1) {           \
                        dest->uid = src->uid;   \
                        dest->gid = src->gid;   \
                }                               \
        } while (0)

#define MARKER_SET_UID_GID(frame, dest, src)                    \
        do {                                                    \
                _MARKER_SET_UID_GID (dest, src);                \
                frame->root->uid = 0;                           \
                frame->root->gid = 0;                           \
                frame->cookie = (void *) _GF_UID_GID_CHANGED;   \
        } while (0)

#define MARKER_RESET_UID_GID(frame, dest, src)                  \
        do {                                                    \
                _MARKER_SET_UID_GID (dest, src);                \
                frame->cookie = NULL;                           \
        } while (0)

struct marker_local{
        uint32_t        timebuf[2];
        pid_t           pid;
        loc_t           loc;
        loc_t           parent_loc;
        loc_t          *next_lock_on;
        uid_t           uid;
        gid_t           gid;
        int32_t         ref;
        int32_t         ia_nlink;
        gf_lock_t       lock;
        mode_t          mode;
        int32_t         err;
        call_stub_t    *stub;
        int64_t         contribution;
        struct marker_local *oplocal;

        /* marker quota specific */
        int64_t delta;
        int64_t d_off;
        int64_t sum;
        int64_t size;
        int32_t hl_count;
        int32_t dentry_child_count;

        fd_t         *fd;
        call_frame_t *frame;

        quota_inode_ctx_t    *ctx;
        inode_contribution_t *contri;
};
typedef struct marker_local marker_local_t;

#define quota_local_t marker_local_t

struct marker_inode_ctx {
        struct quota_inode_ctx *quota_ctx;
};
typedef struct marker_inode_ctx marker_inode_ctx_t;

struct marker_conf{
        char         feature_enabled;
        char        *size_key;
        char        *dirty_key;
        char        *volume_uuid;
        uuid_t      volume_uuid_bin;
        char        *timestamp_file;
        char        *marker_xattr;
        uint64_t     quota_lk_owner;
        gf_lock_t    lock;
};
typedef struct marker_conf marker_conf_t;

#endif
