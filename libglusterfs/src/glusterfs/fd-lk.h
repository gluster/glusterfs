/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _FD_LK_H
#define _FD_LK_H

#include "glusterfs/locking.h"
#include "glusterfs/list.h"

#define get_lk_type(type)                                                      \
    type == F_UNLCK ? "F_UNLCK" : (type == F_RDLCK ? "F_RDLCK" : "F_WRLCK")

#define get_lk_cmd(cmd)                                                        \
    cmd == F_SETLKW ? "F_SETLKW" : (cmd == F_SETLK ? "F_SETLK" : "F_GETLK")

struct _fd;

struct fd_lk_ctx {
    struct list_head lk_list;
    gf_atomic_t ref;
    gf_lock_t lock;
};
typedef struct fd_lk_ctx fd_lk_ctx_t;

struct fd_lk_ctx_node {
    int32_t cmd;
    short fl_type;
    off_t fl_start;
    off_t fl_end;
    struct list_head next;
    struct gf_flock user_flock;
};
typedef struct fd_lk_ctx_node fd_lk_ctx_node_t;

fd_lk_ctx_t *
fd_lk_ctx_ref(fd_lk_ctx_t *lk_ctx);

fd_lk_ctx_t *
fd_lk_ctx_create(void);

int
fd_lk_insert_and_merge(struct _fd *lk_ctx, int32_t cmd, struct gf_flock *flock);

int
fd_lk_ctx_unref(fd_lk_ctx_t *lk_ctx);

gf_boolean_t
fd_lk_ctx_empty(fd_lk_ctx_t *lk_ctx);

#endif /* _FD_LK_H */
