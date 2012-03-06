/*
  Copyright (c) 2011-2012 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _FD_LK_H
#define _FD_LK_H

#include "fd.h"
#include "locking.h"
#include "list.h"
#include "logging.h"
#include "mem-pool.h"
#include "mem-types.h"
#include "glusterfs.h"

#define get_lk_type(type)                                               \
        type == F_UNLCK ? "F_UNLCK" : (type == F_RDLCK ? "F_RDLCK" : "F_WRLCK")

#define get_lk_cmd(cmd)                                                 \
        cmd == F_SETLKW ? "F_SETLKW" : (cmd == F_SETLK ? "F_SETLK" : "F_GETLK")

struct _fd;

struct fd_lk_ctx {
        struct list_head lk_list;
        int   ref;
        gf_lock_t lock;
};
typedef struct fd_lk_ctx fd_lk_ctx_t;

struct fd_lk_ctx_node {
        int32_t            cmd;
        struct gf_flock    user_flock;
        off_t              fl_start;
        off_t              fl_end;
        short              fl_type;
        struct list_head   next;
};
typedef struct fd_lk_ctx_node fd_lk_ctx_node_t;

fd_lk_ctx_t *
_fd_lk_ctx_ref (fd_lk_ctx_t *lk_ctx);

fd_lk_ctx_t *
fd_lk_ctx_ref (fd_lk_ctx_t *lk_ctx);

fd_lk_ctx_t *
fd_lk_ctx_try_ref (fd_lk_ctx_t *lk_ctx);

fd_lk_ctx_t *
fd_lk_ctx_create ();

int
fd_lk_insert_and_merge (struct _fd *lk_ctx, int32_t cmd,
                        struct gf_flock *flock);

int
fd_lk_ctx_unref (fd_lk_ctx_t *lk_ctx);

#endif /* _FD_LK_H */
