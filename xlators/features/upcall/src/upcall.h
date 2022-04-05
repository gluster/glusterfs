/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __UPCALL_H__
#define __UPCALL_H__

#include <glusterfs/compat-errno.h>
#include "upcall-mem-types.h"
#include <glusterfs/client_t.h>
#include "upcall-messages.h"
#include <glusterfs/upcall-utils.h>

#define EXIT_IF_UPCALL_OFF(this, label)                                        \
    do {                                                                       \
        if (!is_upcall_enabled(this))                                          \
            goto label;                                                        \
    } while (0)

#define UPCALL_STACK_UNWIND(fop, frame, params...)                             \
    do {                                                                       \
        upcall_local_t *__local = NULL;                                        \
        if (frame) {                                                           \
            __local = frame->local;                                            \
            frame->local = NULL;                                               \
        }                                                                      \
        STACK_UNWIND_STRICT(fop, frame, params);                               \
        upcall_local_wipe(__local);                                            \
    } while (0)

#define UPCALL_STACK_DESTROY(frame)                                            \
    do {                                                                       \
        upcall_local_t *__local = NULL;                                        \
        __local = frame->local;                                                \
        frame->local = NULL;                                                   \
        STACK_DESTROY(frame->root);                                            \
        upcall_local_wipe(__local);                                            \
    } while (0)

struct _upcall_private {
    time_t cache_invalidation_timeout;
    struct list_head inode_ctx_list;
    gf_lock_t inode_ctx_lk;
    pthread_t reaper_thr;
    dict_t *xattrs; /* list of xattrs registered by clients
                       for receiving invalidation */
    int32_t fini;
    gf_boolean_t cache_invalidation_enabled;
    gf_boolean_t reaper_init_done;
};
typedef struct _upcall_private upcall_private_t;

struct _upcall_client {
    struct list_head client_list;
    /* strdup to store client_uid, strdup. Free it explicitly */
    char *client_uid;
    time_t access_time; /* time last accessed */
    /* the amount of time which client can cache this entry */
    uint32_t expire_time_attr;
};
typedef struct _upcall_client upcall_client_t;

/* Upcall entries are maintained in inode_ctx */
struct _upcall_inode_ctx {
    struct list_head inode_ctx_list;
    struct list_head client_list;
    pthread_mutex_t client_list_lock; /* mutex for clients list
                                         of this upcall entry */
    uuid_t gfid;                      /* gfid of the entry */
    int destroy;
};
typedef struct _upcall_inode_ctx upcall_inode_ctx_t;

struct upcall_local {
    /* XXX: need to check if we can store
     * pointers in 'local' which may get freed
     * in future by other thread
     */
    inode_t *inode;
    loc_t rename_oldloc;
    loc_t loc; /* required for stat in *xattr_cbk */
    fd_t *fd;  /* required for fstat in *xattr_cbk */
    dict_t *xattr;
};
typedef struct upcall_local upcall_local_t;

void
upcall_local_wipe(upcall_local_t *local);

int
upcall_cleanup_inode_ctx(xlator_t *this, inode_t *inode);

void *
upcall_reaper_thread(void *data);
int
upcall_reaper_thread_init(xlator_t *this);

/* Xlator options */
gf_boolean_t
is_upcall_enabled(xlator_t *this);

/* Cache invalidation specific */
void
upcall_cache_invalidate(call_frame_t *frame, xlator_t *this, client_t *client,
                        inode_t *inode, uint32_t flags, struct iatt *stbuf,
                        struct iatt *p_stbuf, struct iatt *oldp_stbuf,
                        dict_t *xattr);
int
up_filter_xattr(dict_t *xattr, dict_t *regd_xattrs);

int
up_compare_afr_xattr(dict_t *d, char *k, data_t *v, void *tmp);

gf_boolean_t
up_invalidate_needed(dict_t *xattrs);
#endif /* __UPCALL_H__ */
