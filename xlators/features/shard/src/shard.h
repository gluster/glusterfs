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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "compat-errno.h"

#define GF_SHARD_DIR ".shard"
#define SHARD_MIN_BLOCK_SIZE  (4 * GF_UNIT_MB)
#define SHARD_MAX_BLOCK_SIZE  (4 * GF_UNIT_TB)
#define GF_XATTR_SHARD_BLOCK_SIZE "trusted.glusterfs.shard.block-size"
#define GF_XATTR_SHARD_FILE_SIZE  "trusted.glusterfs.shard.file-size"
#define SHARD_ROOT_GFID "be318638-e8a0-4c6d-977d-7a937aa84806"
#define SHARD_INODE_LRU_LIMIT 4096

#define get_lowest_block(off, shard_size) (off / shard_size)
#define get_highest_block(off, len, shard_size) ((off+len-1) / shard_size)

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
        uint64_t      *__size_attr = NULL;                                    \
        shard_priv_t  *__priv      = NULL;                                    \
                                                                              \
        __priv = this->private;                                               \
                                                                              \
        local->block_size = hton64 (__priv->block_size);                      \
        __ret = dict_set_static_bin (xattr_req, GF_XATTR_SHARD_BLOCK_SIZE,    \
                                     &local->block_size,                      \
                                     sizeof (local->block_size));             \
        if (__ret) {                                                          \
                gf_log (this->name, GF_LOG_WARNING, "Failed to set key: %s "  \
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
                gf_log (this->name, GF_LOG_WARNING, "Failed to set key: %s "  \
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
                gf_log (this->name, GF_LOG_WARNING, "Failed to set dict"      \
                        " value: key:%s for %s.", GF_XATTR_SHARD_FILE_SIZE,   \
                        uuid_utoa (gfid));                                    \
                goto label;                                                   \
        }                                                                     \
} while (0)


typedef struct shard_priv {
        uint64_t block_size;
        uuid_t dot_shard_gfid;
        inode_t *dot_shard_inode;
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
        int xflag;
        int count;
        uint32_t flags;
        uint64_t block_size;
        uint64_t dst_block_size;
        off_t offset;
        size_t total_size;
        size_t written_size;
        size_t hole_size;
        size_t req_size;
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
        uint32_t rdev;
        uint64_t block_size; /* The block size with which this inode is
                                sharded */
        mode_t mode;
} shard_inode_ctx_t;

#endif /* __SHARD_H__ */
