/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _MARKER_QUOTA_H
#define _MARKER_QUOTA_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "marker-mem-types.h"

#define QUOTA_XATTR_PREFIX "trusted.glusterfs"
#define QUOTA_DIRTY_KEY "trusted.glusterfs.quota.dirty"

#define CONTRIBUTION "contri"
#define CONTRI_KEY_MAX 512
#define READDIR_BUF 4096


#define QUOTA_STACK_DESTROY(_frame, _this)              \
        do {                                            \
                quota_local_t *_local = NULL;           \
                _local = _frame->local;                 \
                _frame->local = NULL;                   \
                STACK_DESTROY (_frame->root);           \
                mq_local_unref (_this, _local);         \
        } while (0)


#define QUOTA_ALLOC(var, type, ret)                     \
        do {                                            \
                ret = 0;                                \
                var = GF_CALLOC (sizeof (type), 1,      \
                                 gf_marker_mt_##type);  \
                if (!var) {                             \
                        ret = -1;                       \
                }                                       \
        } while (0);

#define QUOTA_ALLOC_OR_GOTO(var, type, ret, label)      \
        do {                                            \
                var = GF_CALLOC (sizeof (type), 1,      \
                                 gf_marker_mt_##type);  \
                if (!var) {                             \
                        gf_log ("", GF_LOG_ERROR,       \
                                "out of memory");       \
                        ret = -1;                       \
                        goto label;                     \
                }                                       \
                ret = 0;                                \
        } while (0);

#define GET_CONTRI_KEY(var, _gfid, _ret)              \
        do {                                          \
                if (_gfid != NULL) {                  \
                        char _gfid_unparsed[40];      \
                        uuid_unparse (_gfid, _gfid_unparsed);           \
                        _ret = snprintf (var, CONTRI_KEY_MAX,           \
                                         QUOTA_XATTR_PREFIX             \
                                         ".%s.%s." CONTRIBUTION, "quota", \
                                         _gfid_unparsed);               \
                } else {                                                \
                        _ret = snprintf (var, CONTRI_KEY_MAX,           \
                                         QUOTA_XATTR_PREFIX             \
                                         ".%s.." CONTRIBUTION, "quota"); \
                }                                                       \
        } while (0);

#define QUOTA_SAFE_INCREMENT(lock, var)         \
        do {                                    \
                LOCK (lock);                    \
                var ++;                         \
                UNLOCK (lock);                  \
        } while (0)

struct quota_inode_ctx {
        int64_t                size;
        int8_t                 dirty;
        gf_boolean_t           updation_status;
        gf_lock_t              lock;
        struct list_head       contribution_head;
};
typedef struct quota_inode_ctx quota_inode_ctx_t;

struct inode_contribution {
        struct list_head contri_list;
        int64_t          contribution;
        uuid_t           gfid;
  gf_lock_t lock;
};
typedef struct inode_contribution inode_contribution_t;

int32_t
mq_get_lock_on_parent (call_frame_t *, xlator_t *);

int32_t
mq_req_xattr (xlator_t *, loc_t *, dict_t *);

int32_t
init_quota_priv (xlator_t *);

int32_t
mq_xattr_state (xlator_t *, loc_t *, dict_t *, struct iatt);

int32_t
mq_set_inode_xattr (xlator_t *, loc_t *);

int
mq_initiate_quota_txn (xlator_t *, loc_t *);

int32_t
mq_dirty_inode_readdir (call_frame_t *, void *, xlator_t *,
                        int32_t, int32_t, fd_t *, dict_t *);

int32_t
mq_reduce_parent_size (xlator_t *, loc_t *, int64_t);

int32_t
mq_rename_update_newpath (xlator_t *, loc_t *);

int32_t
mq_inspect_file_xattr (xlator_t *this, loc_t *loc, dict_t *dict, struct iatt buf);

int32_t
mq_forget (xlator_t *, quota_inode_ctx_t *);
#endif
