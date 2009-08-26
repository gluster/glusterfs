/*
   Copyright (c) 2009-2010 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _STAT_PREFETCH_H
#define _STAT_PREFETCH_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "dict.h"
#include "xlator.h"

struct sp_cache {
        gf_dirent_t entries;            /* Head of list of cached dirents */
        uint64_t    expected_offset;    /* Offset where the next read will
                                         * happen.
                                         */
        gf_lock_t   lock;
};
typedef struct sp_cache sp_cache_t;

struct sp_fd_ctx {
        sp_cache_t *cache;
        inode_t    *parent_inode;       /* 
                                         * inode corresponding to dirname (path)
                                         */
        char       *name;               /*
                                         * basename of path on which this fd is 
                                         * opened
                                         */
        gf_lock_t    lock;
};
typedef struct sp_fd_ctx sp_fd_ctx_t;

struct sp_local {
        loc_t  loc;
        fd_t  *fd;
};
typedef struct sp_local sp_local_t;


void sp_local_free (sp_local_t *local);

#define SP_STACK_UNWIND(frame, params ...) do {    \
        sp_local_t *__local = frame->local;        \
        frame->local = NULL;                       \
        STACK_UNWIND (frame, params);              \
        sp_local_free (__local);                   \
} while (0)

#define SP_STACK_DESTROY(frame) do {         \
        sp_local_t *__local = frame->local;  \
        frame->local = NULL;                 \
        STACK_DESTROY (frame->root);         \
        sp_local_free (__local);             \
} while (0)

#endif  /* #ifndef _STAT_PREFETCH_H */
