/*
   Copyright (c) 2006, 2007, 2008 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __POSIX_LOCKS_H__
#define __POSIX_LOCKS_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "compat-errno.h"
#include "transport.h"
#include "stack.h"
#include "call-stub.h"

struct __pl_fd;

struct __posix_lock {
	struct list_head   list;

	short              fl_type;
	off_t              fl_start;
	off_t              fl_end;

	short              blocked;    /* waiting to acquire */
	struct flock       user_flock; /* the flock supplied by the user */
	xlator_t          *this;       /* required for blocked locks */
	fd_t              *fd;

	call_frame_t      *frame;

	/* These two together serve to uniquely identify each process
	   across nodes */

	transport_t       *transport;     /* to identify client node */
	pid_t              client_pid;    /* pid of client process */
};
typedef struct __posix_lock posix_lock_t;

struct __pl_rw_req_t {
	struct list_head      list;
	call_stub_t          *stub;
	posix_lock_t          region;
};
typedef struct __pl_rw_req_t pl_rw_req_t;


struct __entry_lock {
	struct list_head  inode_list;    /* list_head back to pl_inode_t */
	struct list_head  blocked_locks; /* locks blocked due to this lock */

	call_frame_t     *frame;
	xlator_t         *this;
	int               blocked;
	
	const char       *basename;
	entrylk_type      type;
	unsigned int      read_count;    /* number of read locks */
	transport_t      *trans;
};
typedef struct __entry_lock pl_entry_lock_t;


/* The "simulated" inode. This contains a list of all the locks associated 
   with this file */

struct __pl_inode {
	pthread_mutex_t  mutex;

	struct list_head dir_list;       /* list of entry locks */
	struct list_head ext_list;       /* list of fcntl locks */
	struct list_head int_list;       /* list of internal locks */
	struct list_head rw_list;        /* list of waiting r/w requests */
	int              mandatory;      /* if mandatory locking is enabled */
};
typedef struct __pl_inode pl_inode_t;


#define LOCKS_FOR_DOMAIN(inode,domain) (domain == GF_LOCK_POSIX \
					? inode->fcntl_locks	\
					: inode->inodelk_locks)

struct __pl_fd {
	gf_boolean_t nonblocking;       /* whether O_NONBLOCK has been set */
};
typedef struct __pl_fd pl_fd_t;


typedef struct {
	gf_boolean_t    mandatory;      /* if mandatory locking is enabled */
} posix_locks_private_t;


#endif /* __POSIX_LOCKS_H__ */
