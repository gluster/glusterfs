/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "call-stub.h"
#include "sdfs-messages.h"
#include "atomic.h"

#define SDFS_LOCK_COUNT_MAX    2

typedef struct{
        loc_t           parent_loc;
        char            *basename;
        int             locked[SDFS_LOCK_COUNT_MAX];
} sdfs_entry_lock_t;

typedef struct {
        sdfs_entry_lock_t entrylk[SDFS_LOCK_COUNT_MAX];
        int               lock_count;
} sdfs_lock_t;

struct sdfs_local {
        call_frame_t *main_frame;
        loc_t         loc;
        loc_t         parent_loc;
        call_stub_t  *stub;
        sdfs_lock_t   *lock;
        int           op_ret;
        int           op_errno;
        gf_atomic_t   call_cnt;
};
typedef struct sdfs_local sdfs_local_t;

#define SDFS_STACK_DESTROY(frame) do {                   \
                sdfs_local_t *__local = NULL;            \
                __local               = frame->local;    \
                frame->local          = NULL;            \
                gf_client_unref (frame->root->client);   \
                STACK_DESTROY (frame->root);             \
                sdfs_local_cleanup (__local);            \
        } while (0)

