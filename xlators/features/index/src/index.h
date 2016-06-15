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

typedef enum {
        XATTROP_TYPE_UNSET = -1,
        XATTROP,
        DIRTY,
        ENTRY_CHANGES,
        XATTROP_TYPE_END
} index_xattrop_type_t;

typedef struct index_inode_ctx {
        gf_boolean_t processing;
        struct list_head callstubs;
        int state[XATTROP_TYPE_END];
        uuid_t virtual_pargfid; /* virtual gfid of dir under
                                  .glusterfs/indices/entry-changes. */
} index_inode_ctx_t;

typedef struct index_fd_ctx {
        DIR *dir;
        off_t dir_eof;
} index_fd_ctx_t;

typedef struct index_priv {
        char *index_basepath;
        char *dirty_basepath;
        uuid_t index;
        gf_lock_t lock;
        uuid_t internal_vgfid[XATTROP_TYPE_END];
        struct list_head callstubs;
        pthread_mutex_t mutex;
        pthread_cond_t  cond;
        dict_t  *dirty_watchlist;
        dict_t  *pending_watchlist;
        dict_t  *complete_watchlist;
        int64_t  pending_count;
} index_priv_t;

typedef struct index_local {
        inode_t *inode;
        dict_t *xdata;
} index_local_t;

#define INDEX_STACK_UNWIND(fop, frame, params ...)      \
do {                                                    \
        index_local_t *__local = NULL;                  \
        if (frame) {                                    \
                __local = frame->local;                 \
                frame->local = NULL;                    \
        }                                               \
        STACK_UNWIND_STRICT (fop, frame, params);       \
        if (__local) {                                  \
                inode_unref (__local->inode);           \
                if (__local->xdata)                     \
                        dict_unref (__local->xdata);    \
                mem_put (__local);                      \
        }                                               \
} while (0)

#endif
