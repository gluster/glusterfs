/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __READDIR_AHEAD_H
#define __READDIR_AHEAD_H

/* state flags */
#define RDA_FD_NEW (1 << 0)
#define RDA_FD_RUNNING (1 << 1)
#define RDA_FD_EOD (1 << 2)
#define RDA_FD_ERROR (1 << 3)
#define RDA_FD_BYPASS (1 << 4)
#define RDA_FD_PLUGGED (1 << 5)

#define RDA_COMMON_MODIFICATION_FOP(name, frame, this, __inode, __xdata,       \
                                    args...)                                   \
    do {                                                                       \
        struct rda_local *__local = NULL;                                      \
        rda_inode_ctx_t *ctx_p = NULL;                                         \
                                                                               \
        __local = mem_get0(this->local_pool);                                  \
        __local->inode = inode_ref(__inode);                                   \
        LOCK(&__inode->lock);                                                  \
        {                                                                      \
            ctx_p = __rda_inode_ctx_get(__inode, this);                        \
        }                                                                      \
        UNLOCK(&__inode->lock);                                                \
        __local->generation = GF_ATOMIC_GET(ctx_p->generation);                \
                                                                               \
        frame->local = __local;                                                \
        if (__xdata)                                                           \
            __local->xattrs = dict_ref(__xdata);                               \
                                                                               \
        STACK_WIND(frame, rda_##name##_cbk, FIRST_CHILD(this),                 \
                   FIRST_CHILD(this)->fops->name, args, __xdata);              \
    } while (0)

#define RDA_STACK_UNWIND(fop, frame, params...)                                \
    do {                                                                       \
        struct rda_local *__local = NULL;                                      \
        if (frame) {                                                           \
            __local = frame->local;                                            \
            frame->local = NULL;                                               \
        }                                                                      \
        STACK_UNWIND_STRICT(fop, frame, params);                               \
        if (__local) {                                                         \
            rda_local_wipe(__local);                                           \
            mem_put(__local);                                                  \
        }                                                                      \
    } while (0)

struct rda_fd_ctx {
    off_t cur_offset;  /* current head of the ctx */
    size_t cur_size;   /* current size of the preload */
    off_t next_offset; /* tail of the ctx */
    uint32_t state;
    gf_lock_t lock;
    gf_dirent_t entries;
    call_frame_t *fill_frame;
    call_stub_t *stub;
    int op_errno;
    dict_t *xattrs; /* md-cache keys to be sent in readdirp() */
    dict_t *writes_during_prefetch;
    gf_atomic_t prefetching;
};

struct rda_local {
    struct rda_fd_ctx *ctx;
    fd_t *fd;
    dict_t *xattrs; /* md-cache keys to be sent in readdirp() */
    inode_t *inode;
    off_t offset;
    uint64_t generation;
    int32_t skip_dir;
};

struct rda_priv {
    uint64_t rda_req_size;
    uint64_t rda_low_wmark;
    uint64_t rda_high_wmark;
    uint64_t rda_cache_limit;
    gf_atomic_t rda_cache_size;
    gf_boolean_t parallel_readdir;
};

typedef struct rda_inode_ctx {
    struct iatt statbuf;
    gf_atomic_t generation;
} rda_inode_ctx_t;

#endif /* __READDIR_AHEAD_H */
