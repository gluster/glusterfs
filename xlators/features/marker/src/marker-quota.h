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

#include "xlator.h"
#include "marker-mem-types.h"
#include "refcount.h"
#include "quota-common-utils.h"
#include "call-stub.h"

#define QUOTA_XATTR_PREFIX "trusted.glusterfs"
#define QUOTA_DIRTY_KEY "trusted.glusterfs.quota.dirty"

#define CONTRIBUTION  "contri"
#define QUOTA_KEY_MAX 512
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

#define GET_QUOTA_KEY(_this, var, key, _ret)                              \
        do {                                                              \
                marker_conf_t  *_priv = _this->private;                   \
                if (_priv->version > 0)                                   \
                        _ret = snprintf (var, QUOTA_KEY_MAX, "%s.%d",     \
                                         key, _priv->version);            \
                else                                                      \
                        _ret = snprintf (var, QUOTA_KEY_MAX, "%s", key);  \
        } while (0)

#define GET_CONTRI_KEY(_this, var, _gfid, _ret)                           \
        do {                                                              \
                char  _tmp_var[QUOTA_KEY_MAX] = {0, };                   \
                if (_gfid != NULL) {                                      \
                        char _gfid_unparsed[40];                          \
                        gf_uuid_unparse (_gfid, _gfid_unparsed);          \
                        _ret = snprintf (_tmp_var, QUOTA_KEY_MAX,         \
                                         QUOTA_XATTR_PREFIX               \
                                         ".%s.%s." CONTRIBUTION,          \
                                         "quota", _gfid_unparsed);        \
                } else {                                                  \
                        _ret = snprintf (_tmp_var, QUOTA_KEY_MAX,         \
                                         QUOTA_XATTR_PREFIX               \
                                         ".%s.." CONTRIBUTION,            \
                                         "quota");                        \
                }                                                         \
                GET_QUOTA_KEY (_this, var, _tmp_var, _ret);               \
        } while (0)

#define GET_SIZE_KEY(_this, var, _ret)                                    \
        {                                                                 \
                GET_QUOTA_KEY (_this, var, QUOTA_SIZE_KEY, _ret);         \
        }

#define QUOTA_SAFE_INCREMENT(lock, var)         \
        do {                                    \
                LOCK (lock);                    \
                var ++;                         \
                UNLOCK (lock);                  \
        } while (0)

struct quota_inode_ctx {
        int64_t                size;
        int64_t                file_count;
        int64_t                dir_count;
        int8_t                 dirty;
        gf_boolean_t           create_status;
        gf_boolean_t           updation_status;
        gf_boolean_t           dirty_status;
        gf_lock_t              lock;
        struct list_head       contribution_head;
};
typedef struct quota_inode_ctx quota_inode_ctx_t;

struct quota_synctask {
        xlator_t      *this;
        loc_t          loc;
        quota_meta_t   contri;
        gf_boolean_t   is_static;
        uint32_t       ia_nlink;
        call_stub_t   *stub;
};
typedef struct quota_synctask quota_synctask_t;

struct inode_contribution {
        struct list_head contri_list;
        int64_t          contribution;
        int64_t          file_count;
        int64_t          dir_count;
        uuid_t           gfid;
        gf_lock_t        lock;
        GF_REF_DECL;
};
typedef struct inode_contribution inode_contribution_t;

int32_t
mq_req_xattr (xlator_t *, loc_t *, dict_t *, char *, char *);

int32_t
mq_xattr_state (xlator_t *, loc_t *, dict_t *, struct iatt);

int
mq_initiate_quota_txn (xlator_t *, loc_t *, struct iatt *);

int
mq_initiate_quota_blocking_txn (xlator_t *, loc_t *, struct iatt *);

int
mq_create_xattrs_txn (xlator_t *this, loc_t *loc, struct iatt *buf);

int32_t
mq_reduce_parent_size_txn (xlator_t *, loc_t *, quota_meta_t *,
                           uint32_t nlink, call_stub_t *stub);

int32_t
mq_forget (xlator_t *, quota_inode_ctx_t *);
#endif
