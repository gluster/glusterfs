/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __TRASH_H__
#define __TRASH_H__

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "inode.c"
#include "fnmatch.h"

#include <libgen.h>

#ifndef GF_BLOCK_READV_SIZE
#define GF_BLOCK_READV_SIZE      (128 * GF_UNIT_KB)
#endif

#ifndef GF_DEFAULT_MAX_FILE_SIZE
#define GF_DEFAULT_MAX_FILE_SIZE (200 * GF_UNIT_MB)
#endif

struct trash_struct {
        fd_t    *fd;         /* for the fd of existing file */
        fd_t    *newfd;      /* for the newly created file */
        loc_t    loc;        /* to store the location of the existing file */
        loc_t    newloc;     /* to store the location for the new file */
        size_t   fsize;      /* for keeping the size of existing file */
        off_t    cur_offset; /* current offset for read and write ops */
        off_t    fop_offset; /* original offset received with the fop */
        pid_t    pid;
        char     origpath[PATH_MAX];
        char     newpath[PATH_MAX];
        int32_t  loop_count;
        gf_boolean_t is_set_pid;
        struct iatt preparent;
        struct iatt postparent;
        gf_boolean_t ctr_link_count_req;
};
typedef struct trash_struct trash_local_t;

struct _trash_elim_path {
        struct _trash_elim_path    *next;
        char                       *path;
};
typedef struct _trash_elim_path trash_elim_path;

struct trash_priv {
        char                 *oldtrash_dir;
        char                 *newtrash_dir;
        char                 *brick_path;
        trash_elim_path      *eliminate;
        size_t                max_trash_file_size;
        gf_boolean_t          state;
        gf_boolean_t          internal;
        inode_t              *trash_inode;
        inode_table_t        *trash_itable;
};
typedef struct trash_priv trash_private_t;

#define TRASH_SET_PID(frame, local) do {                        \
                GF_ASSERT (!local->is_set_pid);                 \
                if (!local->is_set_pid) {                       \
                        local->pid = frame->root->pid;          \
                        frame->root->pid = GF_SERVER_PID_TRASH; \
                        local->is_set_pid = _gf_true;           \
                }                                               \
} while (0)

#define TRASH_UNSET_PID(frame, local) do {              \
                GF_ASSERT (local->is_set_pid);          \
                if (local->is_set_pid) {                \
                        frame->root->pid = local->pid;  \
                        local->is_set_pid = _gf_false;  \
                }                                       \
} while (0)

#define TRASH_STACK_UNWIND(op, frame, params ...) do {    \
                trash_local_t *__local = NULL;            \
                __local = frame->local;                   \
                frame->local = NULL;                      \
                STACK_UNWIND_STRICT (op, frame, params);  \
                trash_local_wipe (__local);               \
        } while (0)

#endif /* __TRASH_H__ */
