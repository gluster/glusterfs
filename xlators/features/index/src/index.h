/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __INDEX_H__
#define __INDEX_H__

#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "byte-order.h"
#include "common-utils.h"
#include "index-mem-types.h"

#define INDEX_THREAD_STACK_SIZE   ((size_t)(1024*1024))

typedef enum {
        UNKNOWN,
        IN,
        NOTIN
} index_state_t;

typedef struct index_inode_ctx {
        gf_boolean_t processing;
        struct list_head callstubs;
        index_state_t state;
} index_inode_ctx_t;

typedef struct index_fd_ctx {
        DIR *dir;
} index_fd_ctx_t;

typedef enum {
        sync_not_started,
        sync_started,
        synced_state,
} to_be_healed_states_t;

typedef enum {
        INDEX_XATTROP,
        BASE_INDICES_HOLDER,
} readdir_directory;

typedef struct index_priv {
        char *index_basepath;
        uuid_t index;
        gf_lock_t lock;
        uuid_t xattrop_vgfid;//virtual gfid of the xattrop index dir
        uuid_t base_indices_holder_vgfid; //virtual gfid of the
                                          //to_be_healed_xattrop directory
        struct list_head callstubs;
        pthread_mutex_t mutex;
        pthread_cond_t  cond;
        to_be_healed_states_t to_be_healed_states;
} index_priv_t;

#define INDEX_STACK_UNWIND(fop, frame, params ...)      \
do {                                                    \
        if (frame) {                                    \
                inode_t *_inode = frame->local;         \
                frame->local = NULL;                    \
                inode_unref (_inode);                   \
        }                                               \
        STACK_UNWIND_STRICT (fop, frame, params);       \
} while (0)

#endif
