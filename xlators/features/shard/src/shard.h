/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __SHARD_H__
#define __SHARD_H__

#include "xlator.h"
#include "compat-errno.h"
#include "shard-messages.h"

#define GF_SHARD_DIR ".shard"
#define SHARD_MIN_BLOCK_SIZE  (4 * GF_UNIT_MB)
#define SHARD_MAX_BLOCK_SIZE  (4 * GF_UNIT_TB)
#define SHARD_XATTR_PREFIX "trusted.glusterfs.shard."
#define GF_XATTR_SHARD_BLOCK_SIZE "trusted.glusterfs.shard.block-size"
#define SHARD_INODE_LRU_LIMIT 4096
#define SHARD_MAX_INODES 16384
/**
 *  Bit masks for the valid flag, which is used while updating ctx
**/
#define SHARD_MASK_BLOCK_SIZE          (1 << 0)
#define SHARD_MASK_PROT                (1 << 1)
#define SHARD_MASK_NLINK               (1 << 2)
#define SHARD_MASK_UID                 (1 << 3)
#define SHARD_MASK_GID                 (1 << 4)
#define SHARD_MASK_SIZE                (1 << 6)
#define SHARD_MASK_BLOCKS              (1 << 7)
#define SHARD_MASK_TIMES               (1 << 8)
#define SHARD_MASK_OTHERS              (1 << 9)
#define SHARD_MASK_REFRESH_RESET       (1 << 10)

#define SHARD_INODE_WRITE_MASK (SHARD_MASK_SIZE | SHARD_MASK_BLOCKS         \
                                                | SHARD_MASK_TIMES)

#define SHARD_LOOKUP_MASK (SHARD_MASK_PROT | SHARD_MASK_NLINK | SHARD_MASK_UID \
                           | SHARD_MASK_GID | SHARD_MASK_TIMES                 \
                           | SHARD_MASK_OTHERS)

#define SHARD_ALL_MASK (SHARD_MASK_BLOCK_SIZE | SHARD_MASK_PROT               \
                        | SHARD_MASK_NLINK | SHARD_MASK_UID | SHARD_MASK_GID  \
                        | SHARD_MASK_SIZE | SHARD_MASK_BLOCKS                 \
                        | SHARD_MASK_TIMES | SHARD_MASK_OTHERS)


#define get_lowest_block(off, shard_size) ((off) / (shard_size))
#define get_highest_block(off, len, shard_size) \
        (((((off)+(len)) == 0)?0:((off)+(len)-1)) / (shard_size))

#define SHARD_ENTRY_FOP_CHECK(loc, op_errno, label) do {               \
        if ((loc->name && !strcmp (GF_SHARD_DIR, loc->name)) &&        \
            (((loc->parent) &&                                          \
            __is_root_gfid (loc->parent->gfid)) ||                     \
            __is_root_gfid (loc->pargfid))) {                           \
                    op_errno = EPERM;                                  \
                    goto label;                                        \
        }                                                              \
                                                                       \
        if ((loc->parent &&                                            \
            __is_shard_dir (loc->parent->gfid)) ||                     \
            __is_shard_dir (loc->pargfid)) {                           \
                    op_errno = EPERM;                                  \
                    goto label;                                        \
        }                                                              \
} while (0)

#define SHARD_INODE_OP_CHECK(gfid, err, label) do {                    \
        if (__is_shard_dir(gfid)) {                                    \
                err = EPERM;                                           \
                goto label;                                            \
        }                                                              \
} while (0)

#define SHARD_STACK_UNWIND(fop, frame, params ...) do {       \
        shard_local_t *__local = NULL;                         \
        if (frame) {                                           \
                __local = frame->local;                        \
                frame->local = NULL;                           \
        }                                                      \
        STACK_UNWIND_STRICT (fop, frame, params);              \
        if (__local) {                                         \
                shard_local_wipe (__local);                    \
                mem_put (__local);                             \
        }                                                      \
} while (0)


#define SHARD_INODE_CREATE_INIT(this, local, xattr_req, loc, label) do {      \
        int            __ret       = -1;                                      \
        int64_t       *__size_attr = NULL;                                    \
        shard_priv_t  *__priv      = NULL;                                    \
                                                                              \
        __priv = this->private;                                               \
                                                                              \
        local->block_size = hton64 (__priv->block_size);                      \
        __ret = dict_set_static_bin (xattr_req, GF_XATTR_SHARD_BLOCK_SIZE,    \
                                     &local->block_size,                      \
                                     sizeof (local->block_size));             \
        if (__ret) {                                                          \
                gf_msg (this->name, GF_LOG_WARNING, 0,                        \
                        SHARD_MSG_DICT_SET_FAILED, "Failed to set key: %s "   \
                        "on path %s", GF_XATTR_SHARD_BLOCK_SIZE, loc->path);  \
                goto label;                                                   \
        }                                                                     \
                                                                              \
        __ret = shard_set_size_attrs (0, 0, &__size_attr);                    \
        if (__ret)                                                            \
                goto label;                                                   \
                                                                              \
        __ret = dict_set_bin (xattr_req, GF_XATTR_SHARD_FILE_SIZE,            \
                              __size_attr, 8 * 4);                            \
        if (__ret) {                                                          \
                gf_msg (this->name, GF_LOG_WARNING, 0,                        \
                        SHARD_MSG_DICT_SET_FAILED, "Failed to set key: %s "   \
                        "on path %s", GF_XATTR_SHARD_FILE_SIZE, loc->path);   \
                GF_FREE (__size_attr);                                        \
                goto label;                                                   \
        }                                                                     \
} while (0)


#define SHARD_MD_READ_FOP_INIT_REQ_DICT(this, dict, gfid, local, label)  do { \
        int __ret = -1;                                                       \
                                                                              \
        __ret = dict_set_uint64 (dict, GF_XATTR_SHARD_FILE_SIZE, 8 * 4);      \
        if (__ret) {                                                          \
                local->op_ret = -1;                                           \
                local->op_errno = ENOMEM;                                     \
                gf_msg (this->name, GF_LOG_WARNING, 0,                        \
                        SHARD_MSG_DICT_SET_FAILED, "Failed to set dict value:"\
                        " key:%s for %s.", GF_XATTR_SHARD_FILE_SIZE,          \
                        uuid_utoa (gfid));                                    \
                goto label;                                                   \
        }                                                                     \
} while (0)

#define SHARD_SET_ROOT_FS_ID(frame, local) do {                               \
                if (!local->is_set_fsid) {                                    \
                        local->uid = frame->root->uid;                        \
                        local->gid = frame->root->gid;                        \
                        frame->root->uid = 0;                                 \
                        frame->root->gid = 0;                                 \
                        local->is_set_fsid = _gf_true;                        \
                }                                                             \
} while (0)

#define SHARD_UNSET_ROOT_FS_ID(frame, local) do {                             \
                if (local->is_set_fsid) {                                     \
                        frame->root->uid = local->uid;                        \
                        frame->root->gid = local->gid;                        \
                        local->is_set_fsid = _gf_false;                       \
                }                                                             \
} while (0)

#define SHARD_TIME_UPDATE(ctx_sec, ctx_nsec, new_sec, new_nsec) do {          \
                if (ctx_sec == new_sec)                                       \
                        ctx_nsec = new_nsec = max (new_nsec, ctx_nsec);       \
                else if (ctx_sec > new_sec) {                                 \
                        new_sec = ctx_sec;                                    \
                        new_nsec = ctx_nsec;                                  \
                } else {                                                      \
                        ctx_sec = new_sec;                                    \
                        ctx_nsec = new_nsec;                                  \
                }                                                             \
        } while (0)


typedef struct shard_priv {
        uint64_t block_size;
        uuid_t dot_shard_gfid;
        inode_t *dot_shard_inode;
        gf_lock_t lock;
        int inode_count;
        struct list_head ilist_head;
} shard_priv_t;

typedef struct {
        loc_t *loc;
        short type;
        char *domain;
} shard_lock_t;

typedef int32_t (*shard_post_fop_handler_t) (call_frame_t *frame,
                                             xlator_t *this);
typedef int32_t (*shard_post_resolve_fop_handler_t) (call_frame_t *frame,
                                                     xlator_t *this);
typedef int32_t (*shard_post_lookup_shards_fop_handler_t) (call_frame_t *frame,
                                                           xlator_t *this);

typedef int32_t (*shard_post_mknod_fop_handler_t) (call_frame_t *frame,
                                                   xlator_t *this);

typedef int32_t (*shard_post_update_size_fop_handler_t) (call_frame_t *frame,
                                                         xlator_t *this);
typedef struct shard_local {
        int op_ret;
        int op_errno;
        int first_block;
        int last_block;
        int num_blocks;
        int call_count;
        int eexist_count;
        int create_count;
        int xflag;
        int count;
        uint32_t flags;
        uint32_t uid;
        uint32_t gid;
        uint64_t block_size;
        uint64_t dst_block_size;
        off_t offset;
        size_t total_size;
        size_t written_size;
        size_t hole_size;
        size_t req_size;
        size_t readdir_size;
        int64_t delta_size;
        int delta_blocks;
        loc_t loc;
        loc_t dot_shard_loc;
        loc_t loc2;
        loc_t tmp_loc;
        fd_t *fd;
        dict_t *xattr_req;
        dict_t *xattr_rsp;
        inode_t **inode_list;
        glusterfs_fop_t fop;
        struct iatt prebuf;
        struct iatt postbuf;
        struct iatt preoldparent;
        struct iatt postoldparent;
        struct iatt prenewparent;
        struct iatt postnewparent;
        struct iovec *vector;
        struct iobref *iobref;
        struct iobuf *iobuf;
        gf_dirent_t entries_head;
        gf_boolean_t is_set_fsid;
        gf_boolean_t list_inited;
        shard_post_fop_handler_t handler;
        shard_post_lookup_shards_fop_handler_t pls_fop_handler;
        shard_post_resolve_fop_handler_t post_res_handler;
        shard_post_mknod_fop_handler_t post_mknod_handler;
        shard_post_update_size_fop_handler_t post_update_size_handler;
        struct {
                int lock_count;
                fop_inodelk_cbk_t inodelk_cbk;
                shard_lock_t *shard_lock;
        } lock;
} shard_local_t;

typedef struct shard_inode_ctx {
        uint64_t block_size; /* The block size with which this inode is
                                sharded */
        struct iatt stat;
        gf_boolean_t refresh;
        /* The following members of inode ctx will be applicable only to the
         * individual shards' ctx and never the base file ctx.
         */
        struct list_head ilist;
        uuid_t base_gfid;
        int block_num;
} shard_inode_ctx_t;

#endif /* __SHARD_H__ */
