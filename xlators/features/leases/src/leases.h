/*
  Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _LEASES_H
#define _LEASES_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "common-utils.h"
#include "glusterfs.h"
#include "xlator.h"
#include "inode.h"
#include "call-stub.h"
#include "logging.h"
#include "client_t.h"
#include "lkowner.h"
#include "locking.h"
#include "upcall-utils.h"
#include "tw.h"
#include "timer-wheel.h"
#include "leases-mem-types.h"
#include "leases-messages.h"

/* The time period for which a client lease lock will be stored after its been
 * recalled for the first time. */
#define RECALL_LEASE_LK_TIMEOUT "60"

#define DATA_MODIFY_FOP 0x0001
#define BLOCKING_FOP 0x0002

#define BLOCK_FOP 0x0001
#define WIND_FOP 0x0002

#define EXIT_IF_LEASES_OFF(this, label) do {                                   \
        if (!is_leases_enabled(this))                                          \
                goto label;                                                    \
} while (0)

#define GET_LEASE_ID(xdata, lease_id, client_uid) do {                         \
        int   ret_val = -1;                                                    \
        ret_val = dict_get_bin (xdata, "lease-id", (void **)&lease_id);        \
        if (ret_val) {                                                         \
                ret_val = 0;                                                   \
                gf_msg_debug ("leases", 0, "Lease id is not set for client:%s", client_uid); \
        }                                                                      \
} while (0)

#define GET_FLAGS(fop, fd_flags)                                               \
do {                                                                           \
        if ((fd_flags & (O_WRONLY | O_RDWR)) && fop == GF_FOP_OPEN)            \
                fop_flags = DATA_MODIFY_FOP;                                   \
                                                                               \
        if (fop == GF_FOP_UNLINK || fop == GF_FOP_RENAME ||                    \
            fop == GF_FOP_TRUNCATE || fop == GF_FOP_FTRUNCATE ||               \
            fop == GF_FOP_FLUSH || fop == GF_FOP_FSYNC ||                      \
            fop == GF_FOP_WRITE || fop == GF_FOP_FALLOCATE ||                  \
            fop == GF_FOP_DISCARD || fop == GF_FOP_ZEROFILL ||                 \
            fop == GF_FOP_SETATTR || fop == GF_FOP_FSETATTR ||                 \
            fop == GF_FOP_LINK)                                                \
                fop_flags = DATA_MODIFY_FOP;                                   \
                                                                               \
        if (!(fd_flags & (O_NONBLOCK | O_NDELAY)))                             \
                fop_flags |= BLOCKING_FOP;                                     \
                                                                               \
} while (0)                                                                    \


#define GET_FLAGS_LK(cmd, l_type, fd_flags)                                    \
do {                                                                           \
        /* TODO: handle F_RESLK_LCK and other glusterfs_lk_recovery_cmds_t */  \
        if ((cmd == F_SETLKW || cmd == F_SETLKW64 ||                           \
             cmd == F_SETLK || cmd == F_SETLK64) &&                            \
            l_type == F_WRLCK)                                                 \
                fop_flags = DATA_MODIFY_FOP;                                   \
                                                                               \
        if (fd_flags & (O_NONBLOCK | O_NDELAY) &&                              \
            (cmd == F_SETLKW || cmd == F_SETLKW64))                            \
                fop_flags |= BLOCKING_FOP;                                     \
                                                                               \
} while (0)                                                                    \

#define LEASE_BLOCK_FOP(inode, fop_name, frame, this, params ...)              \
do {                                                                           \
        call_stub_t             *__stub     = NULL;                            \
        fop_stub_t              *blk_fop    = NULL;                            \
        lease_inode_ctx_t       *lease_ctx  = NULL;                            \
                                                                               \
        __stub = fop_##fop_name##_stub (frame, default_##fop_name##_resume,    \
                                        params);                               \
        if (!__stub) {                                                         \
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,                    \
                        LEASE_MSG_NO_MEM,                                      \
                        "Unable to create stub");                              \
                ret = -ENOMEM;                                                 \
                goto __out;                                                    \
        }                                                                      \
                                                                               \
        blk_fop = GF_CALLOC (1, sizeof (*blk_fop),                             \
                             gf_leases_mt_fop_stub_t);                         \
        if (!blk_fop) {                                                        \
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,                    \
                        LEASE_MSG_NO_MEM,                                      \
                        "Unable to create lease fop stub");                    \
                ret = -ENOMEM;                                                 \
                goto __out;                                                    \
        }                                                                      \
                                                                               \
        lease_ctx = lease_ctx_get (inode, this);                               \
        if (!lease_ctx) {                                                      \
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,                    \
                        LEASE_MSG_NO_MEM,                                      \
                        "Unable to create/get inode ctx");                     \
                op_errno = ENOMEM;                                             \
                goto __out;                                                    \
        }                                                                      \
                                                                               \
        blk_fop->stub = __stub;                                                \
        pthread_mutex_lock (&lease_ctx->lock);                                 \
        {                                                                      \
                /*TODO: If the lease is unlocked btw check lease conflict and  \
                 * by now, then this fop shouldn't be add to the blocked fop   \
                 * list, can use generation number for the same?*/             \
                list_add_tail (&blk_fop->list, &lease_ctx->blocked_list);      \
        }                                                                      \
        pthread_mutex_unlock (&lease_ctx->lock);                               \
                                                                               \
__out:                                                                         \
        if (ret < 0) {                                                         \
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM, LEASE_MSG_NO_MEM,  \
                        "Unable to create stub for blocking the fop:%s (%s)",  \
                         gf_fop_list[frame->root->op], strerror(ENOMEM));      \
                if (__stub != NULL) {                                          \
                        call_stub_destroy (__stub);                            \
                }                                                              \
                GF_FREE (blk_fop);                                             \
                goto err;                                                      \
        }                                                                      \
} while (0)                                                                    \

struct _leases_private {
        gf_boolean_t      leases_enabled;
        int32_t           recall_lease_timeout;
        struct list_head  client_list;
        struct list_head  recall_list;
        struct tvec_base *timer_wheel;    /* timer wheel where the recall request
                                             is qued and waits for unlock/expiry */
        gf_boolean_t      fini;
        pthread_t         recall_thr;
        gf_boolean_t      inited_recall_thr;
        pthread_mutex_t   mutex;
        pthread_cond_t    cond;
};
typedef struct _leases_private leases_private_t;

struct _lease_client {
        char             *client_uid;
        struct list_head  client_list;
        struct list_head  inode_list;
};
typedef struct _lease_client lease_client_t;

struct _lease_inode {
        inode_t          *inode;
        struct list_head  list;  /* This can be part of both inode_list and recall_list */
};
typedef struct _lease_inode lease_inode_t;

struct _lease_fd_ctx {
        char             *client_uid;
        char              lease_id[LEASE_ID_SIZE];
};
typedef struct _lease_fd_ctx lease_fd_ctx_t;

struct _lease_inode_ctx {
        struct list_head  lease_id_list;  /* clients that have taken leases */
        int               lease_type_cnt[GF_LEASE_MAX_TYPE+1];
        int               lease_type;   /* Types of leases acquired */
        uint64_t          lease_cnt;    /* Total number of leases on this inode */
        uint64_t          openfd_cnt;   /* number of fds open */
        gf_boolean_t      recall_in_progress;  /* if lease recall is sent on this inode */
        struct list_head  blocked_list; /* List of fops blocked until the
                                           lease recall is complete */
        inode_t          *inode;        /* this represents the inode on which the
                                           lock was taken, required mainly during
                                           disconnect cleanup */
        struct gf_tw_timer_list *timer;
        pthread_mutex_t   lock;
};
typedef struct _lease_inode_ctx lease_inode_ctx_t;

struct _lease_id_entry {
        struct list_head    lease_id_list;
        char                lease_id[LEASE_ID_SIZE];
        char               *client_uid;  /* uid of the client that has
                                            taken the lease */
        int                 lease_type_cnt[GF_LEASE_MAX_TYPE+1]; /* count of each lease type */
        int                 lease_type;  /* Union of all the leases taken
                                            under the given lease id */
        uint64_t            lease_cnt;   /* Number of leases taken under the
                                            given lease id */
        time_t              recall_time; /* time @ which recall was sent */
};
typedef struct _lease_id_entry lease_id_entry_t;

/* Required? as stub itself will have list */
struct __fop_stub {
        struct list_head      list;
        call_stub_t          *stub;
};
typedef struct __fop_stub fop_stub_t;

struct __lease_timer_data {
        inode_t *inode;
        xlator_t *this;
};
typedef struct __lease_timer_data lease_timer_data_t;

gf_boolean_t
is_leases_enabled (xlator_t *this);

int32_t
get_recall_lease_timeout (xlator_t *this);

lease_inode_ctx_t *
lease_ctx_get (inode_t *inode, xlator_t *this);

int
process_lease_req (call_frame_t *frame, xlator_t *this,
                   inode_t *inode, struct gf_lease *lease);

int
check_lease_conflict (call_frame_t *frame, inode_t *inode,
                      const char *lease_id, uint32_t fop_flags);

int
cleanup_client_leases (xlator_t *this, const char *client_uid);

void *
expired_recall_cleanup (void *data);

#endif /* _LEASES_H */
