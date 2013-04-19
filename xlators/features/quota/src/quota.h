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
#include "call-stub.h"
#include "defaults.h"
#include "byte-order.h"
#include "common-utils.h"
#include "quota-mem-types.h"

#define QUOTA_XATTR_PREFIX      "trusted."
#define DIRTY                   "dirty"
#define SIZE                    "size"
#define CONTRIBUTION            "contri"
#define VAL_LENGTH              8
#define READDIR_BUF             4096
#define QUOTA_UPDATE_USAGE_KEY  "quota-update-usage"

#define GET_THIS_VOL(list_ptr) ((list_ptr)->my_vol)

#define QUOTA_SAFE_INCREMENT(lock, var)         \
        do {                                    \
                LOCK (lock);                    \
                var ++;                         \
                UNLOCK (lock);                  \
        } while (0)

#define QUOTA_SAFE_DECREMENT(lock, var)         \
        do {                                    \
                LOCK (lock);                    \
                var --;                         \
                UNLOCK (lock);                  \
        } while (0)

#define QUOTA_ALLOC_OR_GOTO(var, type, label)           \
        do {                                            \
                var = GF_CALLOC (sizeof (type), 1,      \
                                 gf_quota_mt_##type);   \
                if (!var) {                             \
                        gf_log ("", GF_LOG_ERROR,       \
                                "out of memory");    \
                        ret = -1;                       \
                        goto label;                     \
                }                                       \
        } while (0);

#define QUOTA_STACK_UNWIND(fop, frame, params...)                       \
        do {                                                            \
                quota_local_t *_local = NULL;                           \
                xlator_t      *_this  = NULL;                           \
                if (frame) {                                            \
                        _local = frame->local;                          \
                        _this  = frame->this;                           \
                        frame->local = NULL;                            \
                }                                                       \
                STACK_UNWIND_STRICT (fop, frame, params);               \
                quota_local_cleanup (_this, _local);                    \
        } while (0)

#define QUOTA_FREE_CONTRIBUTION_NODE(_contribution)     \
        do {                                            \
                list_del (&_contribution->contri_list); \
                GF_FREE (_contribution);                \
        } while (0)

#define GET_CONTRI_KEY(var, _vol_name, _gfid, _ret)             \
        do {                                                    \
                char _gfid_unparsed[40];                        \
                uuid_unparse (_gfid, _gfid_unparsed);           \
                _ret = gf_asprintf (var, QUOTA_XATTR_PREFIX     \
                                    "%s.%s." CONTRIBUTION,      \
                                    _vol_name, _gfid_unparsed); \
        } while (0)


#define GET_CONTRI_KEY_OR_GOTO(var, _vol_name, _gfid, label)    \
        do {                                                    \
                GET_CONTRI_KEY(var, _vol_name, _gfid, ret);     \
                if (ret == -1)                                  \
                        goto label;                             \
        } while (0)

#define GET_DIRTY_KEY_OR_GOTO(var, _vol_name, label)            \
        do {                                                    \
                ret = gf_asprintf (var, QUOTA_XATTR_PREFIX      \
                                   "%s." DIRTY, _vol_name);     \
                if (ret == -1)                                  \
                        goto label;                             \
        } while (0)



struct quota_dentry {
        char            *name;
        uuid_t           par;
        struct list_head next;
};
typedef struct quota_dentry quota_dentry_t;

struct quota_inode_ctx {
        int64_t          size;
        int64_t          hard_lim;
        int64_t          soft_lim;
        struct iatt      buf;
        struct list_head parents;
        struct timeval   tv;
        gf_lock_t        lock;
};
typedef struct quota_inode_ctx quota_inode_ctx_t;

struct quota_local {
        gf_lock_t    lock;
        uint32_t     validate_count;
        uint32_t     link_count;
        loc_t        loc;
        loc_t        oldloc;
        loc_t        newloc;
        loc_t        validate_loc;
        int64_t      delta;
        int32_t      op_ret;
        int32_t      op_errno;
        int64_t      size;
        int64_t      hard_lim;
        int64_t      soft_lim;
        char         just_validated;
        inode_t     *inode;
        call_stub_t *stub;
};
typedef struct quota_local quota_local_t;


struct qc_vols_conf {
        char                    *name;
        inode_table_t           *itable;
        struct limits_level {
                struct list_head         limit_head;
                uint64_t                 time_out;
                struct qc_vols_conf     *my_vol;
        } below_soft, above_soft;
};
typedef struct qc_vols_conf qc_vols_conf_t;


struct quota_priv {
        int64_t                 timeout;
        gf_boolean_t            consider_statfs;
        struct list_head        limit_head;
        qc_vols_conf_t        **qc_vols_conf;
        gf_lock_t               lock;
};
typedef struct quota_priv quota_priv_t;


struct limits {
        struct list_head  limit_list;
        char             *path;
        int64_t           value;
        uuid_t            gfid;
        uint64_t          prev_size;
        int64_t           hard_lim;
        int64_t           soft_lim;
};
typedef struct limits     limits_t;

uint64_t cn = 1;

int32_t
quota_construct_loc (xlator_t *this, limits_t *lim_entry, loc_t *entry_loc)
{
        if (!entry_loc || !lim_entry)
                goto err;

        if (!(entry_loc->path = gf_strdup (lim_entry->path))) {
                gf_log (this->name, GF_LOG_ERROR, "String duplication failed, "
                        "increasing your RAM is suggested.");
                goto err;
        }

        if (!(entry_loc->name = strrchr (entry_loc->path, '/'))) {
                gf_log (this->name, GF_LOG_ERROR, "Hmmmm, error again!! %s",
                        entry_loc->path);
                goto err;
        }
        entry_loc->name++;

        return 0;
err:
        loc_wipe (entry_loc);
        return -1;
}
