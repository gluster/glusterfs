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

#ifndef _MARKER_QUOTA_H
#define _MARKER_QUOTA_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "marker.h"
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
                quota_local_unref (_this, _local);      \
		GF_FREE (_local);			\
        } while (0)


#define QUOTA_ALLOC(var, type, ret)                     \
        do {                                            \
                ret = 0;                                \
                var = GF_CALLOC (sizeof (type), 1,      \
                                 gf_marker_mt_##type);  \
                if (!var) {                             \
                        gf_log ("", GF_LOG_ERROR,       \
                                "out of memory");       \
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

#define GET_CONTRI_KEY(var, _gfid, _ret)        \
        do {                                    \
                char _gfid_unparsed[40];        \
                uuid_unparse (_gfid, _gfid_unparsed); \
                _ret = snprintf (var, CONTRI_KEY_MAX, QUOTA_XATTR_PREFIX \
                                 ".%s.%s." CONTRIBUTION, "quota", \
                                 _gfid_unparsed); \
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

struct quota_local {
        int64_t delta;
        int64_t d_off;
        int32_t err;
        int32_t ref;
        int64_t sum;
        int64_t size;
        int32_t hl_count;
        int32_t dentry_child_count;

        fd_t         *fd;
        call_frame_t *frame;
        gf_lock_t     lock;

        loc_t loc;
        loc_t parent_loc;

        quota_inode_ctx_t    *ctx;
        inode_contribution_t *contri;
};
typedef struct quota_local quota_local_t;

int32_t
get_lock_on_parent (call_frame_t *, xlator_t *);

int32_t
quota_req_xattr (xlator_t *, loc_t *, dict_t *);

int32_t
init_quota_priv (xlator_t *);

int32_t
quota_xattr_state (xlator_t *, loc_t *, dict_t *, struct iatt);

int32_t
quota_set_inode_xattr (xlator_t *, loc_t *);

int
initiate_quota_txn (xlator_t *, loc_t *);

int32_t
quota_dirty_inode_readdir (call_frame_t *, void *, xlator_t *,
                           int32_t, int32_t, fd_t *);

int32_t
reduce_parent_size (xlator_t *, loc_t *, int64_t);

int32_t
quota_rename_update_newpath (xlator_t *, loc_t *);

int32_t
inspect_file_xattr (xlator_t *this, loc_t *loc, dict_t *dict, struct iatt buf);

int32_t
quota_forget (xlator_t *, quota_inode_ctx_t *);
#endif
