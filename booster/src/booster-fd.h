/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _BOOSTER_FD_H
#define _BOOSTER_FD_H

#include <libglusterfsclient.h>
#include <locking.h>
#include <list.h>

/* This struct must be updated if the fd_t in fd.h changes.
 * We cannot include those headers here because unistd.h, included
 * by glusterfs headers, conflicts with the syscall prototypes we
 * define for booster.
 */
struct _fd {
        pid_t             pid;
	int32_t           flags;
        int32_t           refcount;
        uint64_t          flush_unique;
        struct list_head  inode_list;
        struct _inode    *inode;
        struct _dict     *ctx;
        gf_lock_t         lock; /* used ONLY for manipulating
                                   'struct _fd_ctx' array (_ctx).*/
	struct _fd_ctx   *_ctx;
};
typedef struct _fd fd_t;


struct _booster_fdtable {
        int             refcount;
        unsigned int    max_fds;
        gf_lock_t       lock;
        fd_t            **fds;
};
typedef struct _booster_fdtable booster_fdtable_t;

extern int
booster_fd_unused_get (booster_fdtable_t *fdtable, fd_t *fdptr, int fd);

extern void
booster_fd_put (booster_fdtable_t *fdtable, int fd);

extern fd_t *
booster_fdptr_get (booster_fdtable_t *fdtable, int fd);

extern void
booster_fdptr_put (fd_t *fdptr);

extern void
booster_fdtable_destroy (booster_fdtable_t *fdtable);

booster_fdtable_t *
booster_fdtable_alloc (void);

#endif /* #ifndef _BOOSTER_FD_H */
