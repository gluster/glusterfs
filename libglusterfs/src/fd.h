/*
  Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _FD_H
#define _FD_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "list.h"
#include "dict.h"

struct _inode;
struct _fd {
	pid_t pid;
	struct list_head inode_list;
	struct _inode *inode;
	dict_t *ctx;
	int32_t refcount;
};
typedef struct _fd fd_t;

struct _fdtable {
	uint32_t max_fds;
	fd_t **fds;
	pthread_mutex_t lock;
};
typedef struct _fdtable fdtable_t;

#include "inode.h"

inline void 
gf_fd_put (fdtable_t *fdtable, int32_t fd);

fd_t *
gf_fd_fdptr_get (fdtable_t *fdtable, int32_t fd);

fdtable_t *
gf_fd_fdtable_alloc (void);

int32_t 
gf_fd_unused_get (fdtable_t *fdtable, fd_t *fdptr);

void 
gf_fd_fdtable_destroy (fdtable_t *fdtable);

fd_t *
fd_ref (fd_t *fd);

fd_t *
fd_unref (fd_t *fd);

fd_t *
fd_create (inode_t *inode, pid_t pid);

fd_t *
fd_lookup (inode_t *inode, pid_t pid);

uint8_t
fd_list_empty (inode_t *inode);

fd_t *
fd_bind (fd_t *fd);

#endif
