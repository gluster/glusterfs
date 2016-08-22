/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _QUOTA_H
#define _QUOTA_H

#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "quota-mem-types.h"
#include "glusterfs.h"
#include "compat.h"
#include "logging.h"
#include "dict.h"
#include "stack.h"
#include "event.h"
#include "globals.h"
#include "rpcsvc.h"
#include "rpc-clnt.h"
#include "byte-order.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "xdr-generic.h"
#include "compat-errno.h"
#include "protocol-common.h"
#include "quota-common-utils.h"
#include "quota-messages.h"

#define DIRTY                   "dirty"
#define SIZE                    "size"
#define CONTRIBUTION            "contri"
#define VAL_LENGTH              8
#define READDIR_BUF             4096

#ifndef UUID_CANONICAL_FORM_LEN
#define UUID_CANONICAL_FORM_LEN 36
#endif

#define WIND_IF_QUOTAOFF(is_quota_on, label)     \
        if (!is_quota_on)                       \
                goto label;

#define QUOTA_WIND_FOR_INTERNAL_FOP(xdata, label)                          \
        do {                                                               \
                if (xdata && dict_get (xdata, GLUSTERFS_INTERNAL_FOP_KEY)) \
                goto label;                                                \
        } while (0)

#define DID_REACH_LIMIT(lim, prev_size, cur_size)               \
        ((cur_size) >= (lim) && (prev_size) < (lim))

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
                        gf_msg ("", GF_LOG_ERROR,       \
                                ENOMEM, Q_MSG_ENOMEM,   \
				"out of memory");       \
                        ret = -1;                       \
                        goto label;                     \
                }                                       \
        } while (0);

#define QUOTA_STACK_WIND_TAIL(frame, params...)                         \
        do {                                                            \
                quota_local_t *_local = NULL;                           \
                                                                        \
                if (frame) {                                            \
                        _local = frame->local;                          \
                        frame->local = NULL;                            \
                }                                                       \
                                                                        \
                STACK_WIND_TAIL (frame, params);                        \
                                                                        \
                if (_local)                                             \
                        quota_local_cleanup (_local);                   \
        } while (0)

#define QUOTA_STACK_UNWIND(fop, frame, params...)                       \
        do {                                                            \
                quota_local_t *_local = NULL;                           \
                if (frame) {                                            \
                        _local = frame->local;                          \
                        frame->local = NULL;                            \
                }                                                       \
                STACK_UNWIND_STRICT (fop, frame, params);               \
                quota_local_cleanup (_local);                           \
        } while (0)

#define QUOTA_FREE_CONTRIBUTION_NODE(_contribution)     \
        do {                                            \
                list_del (&_contribution->contri_list); \
                GF_FREE (_contribution);                \
        } while (0)

#define GET_CONTRI_KEY(var, _vol_name, _gfid, _ret)             \
        do {                                                    \
                char _gfid_unparsed[40];                        \
                if (_gfid != NULL) {                            \
                        gf_uuid_unparse (_gfid, _gfid_unparsed);\
                        _ret = gf_asprintf (var, QUOTA_XATTR_PREFIX     \
                                            "%s.%s." CONTRIBUTION,      \
                                            _vol_name, _gfid_unparsed); \
                } else {                                                \
                        _ret = gf_asprintf (var, QUOTA_XATTR_PREFIX     \
                                            "%s.." CONTRIBUTION,       \
                                            _vol_name);             \
                }                                                       \
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

#define QUOTA_REG_OR_LNK_FILE(ia_type)  \
    (IA_ISREG (ia_type) || IA_ISLNK (ia_type))



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
        int64_t          file_count;
        int64_t          dir_count;
        int64_t          object_hard_lim;
        int64_t          object_soft_lim;
        struct iatt      buf;
        struct list_head parents;
        struct timeval   tv;
        struct timeval   prev_log;
        gf_boolean_t     ancestry_built;
        gf_lock_t        lock;
};
typedef struct quota_inode_ctx quota_inode_ctx_t;

typedef void
(*quota_ancestry_built_t) (struct list_head *parents, inode_t *inode,
                           int32_t op_ret, int32_t op_errno, void *data);

typedef void
(*quota_fop_continue_t) (call_frame_t *frame);

struct quota_local {
        gf_lock_t               lock;
        uint32_t                link_count;
        loc_t                   loc;
        loc_t                   oldloc;
        loc_t                   newloc;
        loc_t                   validate_loc;
        int64_t                 delta;
        int8_t                  object_delta;
        int32_t                 op_ret;
        int32_t                 op_errno;
        int64_t                 size;
        char                    just_validated;
        fop_lookup_cbk_t        validate_cbk;
        quota_fop_continue_t    fop_continue_cbk;
        inode_t                *inode;
        uuid_t                  common_ancestor; /* Used by quota_rename */
        call_stub_t            *stub;
        struct iobref          *iobref;
        quota_limits_t          limit;
        quota_limits_t          object_limit;
        int64_t                 space_available;
        quota_ancestry_built_t  ancestry_cbk;
        void                   *ancestry_data;
        dict_t                 *xdata;
        dict_t                 *validate_xdata;
        int32_t                 quotad_conn_retry;
        xlator_t               *this;
        call_frame_t           *par_frame;
};
typedef struct quota_local      quota_local_t;

struct quota_priv {
        uint32_t                soft_timeout;
        uint32_t                hard_timeout;
        uint32_t               log_timeout;
        double                 default_soft_lim;
        gf_boolean_t           is_quota_on;
        gf_boolean_t           consider_statfs;
        gf_lock_t              lock;
        rpc_clnt_prog_t       *quota_enforcer;
        struct rpcsvc_program *quotad_aggregator;
        struct rpc_clnt       *rpc_clnt;
        rpcsvc_t              *rpcsvc;
        inode_table_t         *itable;
        char                  *volume_uuid;
        uint64_t               validation_count;
        int32_t                quotad_conn_status;
};
typedef struct quota_priv      quota_priv_t;

int
quota_enforcer_lookup (call_frame_t *frame, xlator_t *this, dict_t *xdata,
                       fop_lookup_cbk_t cbk);

void
_quota_enforcer_lookup (void *data);

struct rpc_clnt *
quota_enforcer_init (xlator_t *this, dict_t *options);

void
quota_log_usage (xlator_t *this, quota_inode_ctx_t *ctx, inode_t *inode,
                 int64_t delta);

int
quota_build_ancestry (inode_t *inode, quota_ancestry_built_t ancestry_cbk,
                      void *data);

void
quota_get_limit_dir (call_frame_t *frame, inode_t *cur_inode, xlator_t *this);

int32_t
quota_check_limit (call_frame_t *frame, inode_t *inode, xlator_t *this);

inode_t *
do_quota_check_limit (call_frame_t *frame, inode_t *inode, xlator_t *this,
                      quota_dentry_t *dentry, gf_boolean_t force);
int
quota_fill_inodectx (xlator_t *this, inode_t *inode, dict_t *dict,
                     loc_t *loc, struct iatt *buf, int32_t *op_errno);

int32_t
quota_check_size_limit (call_frame_t *frame, quota_inode_ctx_t *ctx,
                          quota_priv_t *priv, inode_t *_inode, xlator_t *this,
                          int32_t *op_errno, int just_validated, int64_t delta,
                          quota_local_t *local, gf_boolean_t *skip_check);

int32_t
quota_check_object_limit (call_frame_t *frame, quota_inode_ctx_t *ctx,
                          quota_priv_t *priv, inode_t *_inode, xlator_t *this,
                          int32_t *op_errno, int just_validated,
                          quota_local_t *local, gf_boolean_t *skip_check);
#endif
