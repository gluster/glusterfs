/*
 *   Copyright (c) 2021 Pavilion Data Systems, Inc. <http://www.pavilion.io>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __TIER_H__
#define __TIER_H__

#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>
#include <glusterfs/syncop.h>
#include <glusterfs/call-stub.h>
#include "tier-mem-types.h"

#define ALIGN_SIZE 4096

#define MARK_REMOTE_KEY "tier.mark-file-as-remote"
#define TIER_PROMOTE_FILE "tier.promote-file-as-hot"
#define TIER_GET_READ_CNT "tier.remote-read-count"
#define TIER_MIGRATED_BLOCK_CNT "tier.migrated-block-count"

typedef enum {
    GF_TIER_LOCAL = 1,
    GF_TIER_REMOTE = 2,
    GF_TIER_ERROR = 4,
} gf_tier_obj_state;

typedef struct tier_dlstore {
    struct iovec *vector;
    struct iobref *iobref;
    off_t off;
    int32_t count;
    uint32_t flags;
} tier_dlstore;

typedef struct tier_inode_ctx {
    char *remote_path;
    gf_atomic_t readcnt;
    uint64_t ia_size;
    gf_tier_obj_state state;
} tier_inode_ctx_t;

struct tier_plugin {
    char *name;        /* store name */
    char *library;     /* library to load for the given store */
    char *description; /* description about the store */
};

typedef struct tier_local {
    loc_t loc;
    fd_t *fd;
    inode_t *inode;
    call_stub_t *stub;
    call_frame_t *main_frame;
    tier_inode_ctx_t *ctx;
    glusterfs_fop_t fop;

    char *remotepath;
    uint64_t ia_size;
    gf_tier_obj_state state;

    /* offset, flags and size are the information needed
     * by read fop for remote read operation. These will be
     * populated in tier read fop, before being passed
     * on to the plugin performing remote read.
     */
    off_t offset;
    uint32_t flags;
    size_t size;

    int op_errno;
    int op_ret;
    int call_cnt;
    gf_boolean_t locked;
} tier_local_t;

#define TIER_STACK_UNWIND(fop, frame, params...)                               \
    do {                                                                       \
        tier_local_t *__local = NULL;                                          \
        xlator_t *__xl = NULL;                                                 \
        if (frame) {                                                           \
            __xl = frame->this;                                                \
            __local = frame->local;                                            \
            frame->local = NULL;                                               \
        }                                                                      \
        STACK_UNWIND_STRICT(fop, frame, params);                               \
        tier_local_wipe(__xl, __local);                                        \
    } while (0)

#define TIER_STACK_DESTROY(frame)                                              \
    do {                                                                       \
        tier_local_t *__local = NULL;                                          \
        xlator_t *__xl = NULL;                                                 \
        __xl = frame->this;                                                    \
        __local = frame->local;                                                \
        frame->local = NULL;                                                   \
        STACK_DESTROY(frame->root);                                            \
        tier_local_wipe(__xl, __local);                                        \
    } while (0)

tier_local_t *
tier_local_init(xlator_t *this, call_frame_t *frame, loc_t *loc, fd_t *fd,
                glusterfs_fop_t fop);

int
locate_and_execute(call_frame_t *frame);

void
tier_common_cbk(call_frame_t *frame);

void
tier_local_wipe(xlator_t *this, tier_local_t *local);

int
tier_resume_postprocess(xlator_t *this, call_frame_t *frame, inode_t *inode);

typedef enum {
    GF_TIER_WRITE_COUNT = 1,
} tier_getvalue_keys_t;

typedef int (*fop_download_t)(call_frame_t *frame, void *config);
typedef int (*fop_triggerdl_t)(call_frame_t *frame, char *bm_file_path);

typedef gf_tier_obj_state (*fop_remote_read_t)(call_frame_t *, char *, size_t *,
                                               void *);

typedef int (*fop_remote_delete_t)(call_frame_t *, int flags, void *);
typedef int (*fop_remote_truncate_t)(call_frame_t *, off_t offset, void *);
typedef gf_tier_obj_state (*fop_remote_readblk_t)(call_frame_t *, void *);

typedef void *(*store_init)(xlator_t *this, dict_t *options);

typedef int (*store_reconfigure)(xlator_t *this, dict_t *options);

typedef void (*store_fini)(void *config);

typedef int (*fop_remote_sync_t)(call_frame_t *frame, void *config, fd_t *fd);
typedef int (*fop_remote_rmdir_t)(call_frame_t *frame, void *config,
                                  const char *path);
typedef int (*fop_remote_getvalue_t)(inode_t *inode, void *config,
                                     tier_getvalue_keys_t key, dict_t *dict);
typedef int (*fop_remote_forget_t)(void *ctx, void *config);
typedef int (*fop_remote_open_t)(inode_t *inode, fd_t *fd, char *path,
                                 void *config);
typedef int (*fop_remote_release_t)(inode_t *inode, fd_t *fd, void *config);

typedef struct store_methods {
    int (*fop_download)(call_frame_t *frame, void *config);
    gf_tier_obj_state (*fop_remote_read)(call_frame_t *frame, char *data,
                                         size_t *size, void *config);
    int (*fop_remote_delete)(call_frame_t *frame, int flags, void *config);
    gf_tier_obj_state (*fop_remote_readblk)(call_frame_t *frame, void *config);

    /* return type should be the store config */
    void *(*fop_init)(xlator_t *this, dict_t *options);
    int (*fop_reconfigure)(xlator_t *this, dict_t *options);
    void (*fop_fini)(void *config);
    int (*fop_triggerdl)(call_frame_t *frame, char *bm_file_path);
    int (*fop_sync)(call_frame_t *frame, void *config, fd_t *fd);
    int (*fop_rmdir)(call_frame_t *frame, void *config, const char *path);
    int (*get_value)(inode_t *inode, void *config, tier_getvalue_keys_t key,
                     dict_t *dict);
    int (*forget)(void *ctx, void *config);
    int (*open)(inode_t *inode, fd_t *fd, char *path, void *config);
    int (*release)(inode_t *inode, fd_t *fd, void *config);
} store_methods_t;

struct tier_remote_stores {
    char *name;                /* store name */
    void *config;              /* store related information */
    fop_download_t dlfop;      /* store specific download function */
    fop_triggerdl_t triggerdl; /* store specific trigger download function */
    fop_remote_read_t rdfop;   /* store specific read function */
    fop_remote_delete_t deletefop; /* store specific delete/unlink function */
    store_init init;               /* store init to initialize store config */
    store_reconfigure reconfigure; /* reconfigure store config */
    store_fini fini;
    void *handle; /* shared library handle*/
    fop_remote_readblk_t
        readblkfop;         /* store specific read-and-update block function */
    fop_remote_sync_t sync; /* store specific fsync function */
    fop_remote_rmdir_t rmdir;        /* store specific rmdir function */
    fop_remote_getvalue_t get_value; /* store specific get_value function */
    fop_remote_forget_t forget;      /* store specific forget function */
    fop_remote_open_t open;          /* store specific forget function */
    fop_remote_release_t release;    /* store specific forget function */
};

typedef struct tier_private {
    struct tier_remote_stores *stores;
    gf_boolean_t abortdl;
    gf_boolean_t remote_read;
    size_t stub_size;
    char *tierdir;
} tier_private_t;

#endif /* __TIER_H__ */
