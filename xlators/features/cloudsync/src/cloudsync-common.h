/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#ifndef _CLOUDSYNC_COMMON_H
#define _CLOUDSYNC_COMMON_H

#include <glusterfs/glusterfs.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/xlator.h>
#include <glusterfs/syncop.h>
#include <glusterfs/compat-errno.h>
#include "cloudsync-mem-types.h"
#include "cloudsync-messages.h"

typedef struct cs_loc_xattr {
    char *file_path;
    uuid_t uuid;
    uuid_t gfid;
    char *volname;
} cs_loc_xattr_t;

typedef struct cs_size_xattr {
    uint64_t size;
    uint64_t blksize;
    uint64_t blocks;
} cs_size_xattr_t;

typedef struct cs_local {
    loc_t loc;
    fd_t *fd;
    call_stub_t *stub;
    call_frame_t *main_frame;
    int op_errno;
    int op_ret;
    fd_t *dlfd;
    off_t dloffset;
    struct iatt stbuf;
    dict_t *xattr_rsp;
    dict_t *xattr_req;
    glusterfs_fop_t fop;
    gf_boolean_t locked;
    int call_cnt;
    inode_t *inode;
    char *remotepath;

    struct {
        /* offset, flags and size are the information needed
         * by read fop for remote read operation. These will be
         * populated in cloudsync read fop, before being passed
         * on to the plugin performing remote read.
         */
        off_t offset;
        uint32_t flags;
        size_t size;
        cs_loc_xattr_t *lxattr;
    } xattrinfo;

} cs_local_t;

typedef int (*fop_download_t)(call_frame_t *frame, void *config);

typedef int (*fop_remote_read_t)(call_frame_t *, void *);

typedef void *(*store_init)(xlator_t *this);

typedef int (*store_reconfigure)(xlator_t *this, dict_t *options);

typedef void (*store_fini)(void *config);

struct cs_remote_stores {
    char *name;                    /* store name */
    void *config;                  /* store related information */
    fop_download_t dlfop;          /* store specific download function */
    fop_remote_read_t rdfop;       /* store specific read function */
    store_init init;               /* store init to initialize store config */
    store_reconfigure reconfigure; /* reconfigure store config */
    store_fini fini;
    void *handle; /* shared library handle*/
};

typedef struct cs_private {
    xlator_t *this;
    struct cs_remote_stores *stores;
    gf_boolean_t abortdl;
    pthread_spinlock_t lock;
    gf_boolean_t remote_read;
} cs_private_t;

void
cs_local_wipe(xlator_t *this, cs_local_t *local);

void
cs_xattrinfo_wipe(cs_local_t *local);

#define CS_STACK_UNWIND(fop, frame, params...)                                 \
    do {                                                                       \
        cs_local_t *__local = NULL;                                            \
        xlator_t *__xl = NULL;                                                 \
        if (frame) {                                                           \
            __xl = frame->this;                                                \
            __local = frame->local;                                            \
            frame->local = NULL;                                               \
        }                                                                      \
        STACK_UNWIND_STRICT(fop, frame, params);                               \
        cs_local_wipe(__xl, __local);                                          \
    } while (0)

#define CS_STACK_DESTROY(frame)                                                \
    do {                                                                       \
        cs_local_t *__local = NULL;                                            \
        xlator_t *__xl = NULL;                                                 \
        __xl = frame->this;                                                    \
        __local = frame->local;                                                \
        frame->local = NULL;                                                   \
        STACK_DESTROY(frame->root);                                            \
        cs_local_wipe(__xl, __local);                                          \
    } while (0)

typedef struct store_methods {
    int (*fop_download)(call_frame_t *frame, void *config);
    int (*fop_remote_read)(call_frame_t *, void *);
    /* return type should be the store config */
    void *(*fop_init)(xlator_t *this);
    int (*fop_reconfigure)(xlator_t *this, dict_t *options);
    void (*fop_fini)(void *config);
} store_methods_t;

#endif /* _CLOUDSYNC_COMMON_H */
