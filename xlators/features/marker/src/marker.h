/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _MARKER_H
#define _MARKER_H

#include "marker-quota.h"
#include "xlator.h"
#include "defaults.h"
#include "compat-uuid.h"
#include "call-stub.h"

#define MARKER_XATTR_PREFIX "trusted.glusterfs"
#define XTIME               "xtime"
#define VOLUME_MARK         "volume-mark"
#define VOLUME_UUID         "volume-uuid"
#define TIMESTAMP_FILE      "timestamp-file"

enum {
        GF_QUOTA             = 1,
        GF_XTIME             = 2,
        GF_XTIME_GSYNC_FORCE = 4,
        GF_INODE_QUOTA       = 8,
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

#define MARKER_STACK_UNWIND(fop, frame, params...)              \
        do {                                                    \
                quota_local_t *_local = NULL;                   \
                if (frame) {                                    \
                        _local = frame->local;                  \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                if (_local)                                     \
                        marker_local_unref (_local);            \
        } while (0)

struct marker_local{
        uint32_t        timebuf[2];
        pid_t           pid;
        loc_t           loc;
        loc_t           parent_loc;
        uid_t           uid;
        gid_t           gid;
        int32_t         ref;
        uint32_t        ia_nlink;
        struct iatt     buf;
        gf_lock_t       lock;
        mode_t          mode;
        int32_t         err;
        call_stub_t    *stub;
        call_frame_t   *lk_frame;
        quota_meta_t    contribution;
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

        int xflag;
        dict_t *xdata;
        gf_boolean_t skip_txn;
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
        int32_t      version;
};
typedef struct marker_conf marker_conf_t;

#endif
