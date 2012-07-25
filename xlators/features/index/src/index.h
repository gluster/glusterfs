/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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

typedef struct index_priv {
        char *index_basepath;
        uuid_t index;
        gf_lock_t lock;
        uuid_t xattrop_vgfid;//virtual gfid of the xattrop index dir
        struct list_head callstubs;
        pthread_mutex_t mutex;
        pthread_cond_t  cond;
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
