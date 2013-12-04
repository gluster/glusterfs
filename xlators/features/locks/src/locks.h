/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __POSIX_LOCKS_H__
#define __POSIX_LOCKS_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "compat-errno.h"
#include "stack.h"
#include "call-stub.h"
#include "locks-mem-types.h"
#include "client_t.h"

#include "lkowner.h"

struct __pl_fd;

struct __posix_lock {
        struct list_head   list;

        short              fl_type;
        off_t              fl_start;
        off_t              fl_end;

        short              blocked;    /* waiting to acquire */
        struct gf_flock    user_flock; /* the flock supplied by the user */
        xlator_t          *this;       /* required for blocked locks */
        unsigned long      fd_num;

        fd_t              *fd;
        call_frame_t      *frame;

        struct timeval     blkd_time;   /*time at which lock was queued into blkd list*/
        struct timeval     granted_time; /*time at which lock was queued into active list*/

        /* These two together serve to uniquely identify each process
           across nodes */

        void              *client;     /* to identify client node */
        gf_lkowner_t       owner;
        pid_t              client_pid;    /* pid of client process */
};
typedef struct __posix_lock posix_lock_t;

struct __pl_inode_lock {
        struct list_head   list;
        struct list_head   blocked_locks; /* list_head pointing to blocked_inodelks */
        int                ref;

        short              fl_type;
        off_t              fl_start;
        off_t              fl_end;

        const char        *volume;

        struct gf_flock    user_flock; /* the flock supplied by the user */
        xlator_t          *this;       /* required for blocked locks */
	struct __pl_inode *pl_inode;

        call_frame_t      *frame;

        struct timeval     blkd_time;   /*time at which lock was queued into blkd list*/
        struct timeval     granted_time; /*time at which lock was queued into active list*/

        /* These two together serve to uniquely identify each process
           across nodes */

        void              *client;     /* to identify client node */
        gf_lkowner_t       owner;
        pid_t              client_pid;    /* pid of client process */

        char              *connection_id; /* stores the client connection id */

	struct list_head   client_list; /* list of all locks from a client */
};
typedef struct __pl_inode_lock pl_inode_lock_t;

struct __pl_rw_req_t {
        struct list_head      list;
        call_stub_t          *stub;
        posix_lock_t          region;
};
typedef struct __pl_rw_req_t pl_rw_req_t;

struct __pl_dom_list_t {
        struct list_head   inode_list;       /* list_head back to pl_inode_t */
        const char        *domain;
        struct list_head   entrylk_list;     /* List of entry locks */
        struct list_head   blocked_entrylks; /* List of all blocked entrylks */
        struct list_head   inodelk_list;     /* List of inode locks */
        struct list_head   blocked_inodelks; /* List of all blocked inodelks */
};
typedef struct __pl_dom_list_t pl_dom_list_t;

struct __entry_lock {
        struct list_head  domain_list;    /* list_head back to pl_dom_list_t */
        struct list_head  blocked_locks; /* list_head back to blocked_entrylks */
	int ref;

        call_frame_t     *frame;
        xlator_t         *this;
	struct __pl_inode *pinode;

        const char       *volume;

        const char       *basename;
        entrylk_type      type;

        struct timeval     blkd_time;   /*time at which lock was queued into blkd list*/
        struct timeval     granted_time; /*time at which lock was queued into active list*/

        void             *client;
        gf_lkowner_t      owner;
	pid_t             client_pid;    /* pid of client process */

        char             *connection_id; /* stores the client connection id */

	struct list_head   client_list; /* list of all locks from a client */
};
typedef struct __entry_lock pl_entry_lock_t;


/* The "simulated" inode. This contains a list of all the locks associated
   with this file */

struct __pl_inode {
        pthread_mutex_t  mutex;

        struct list_head dom_list;       /* list of domains */
        struct list_head ext_list;       /* list of fcntl locks */
        struct list_head rw_list;        /* list of waiting r/w requests */
        struct list_head reservelk_list;        /* list of reservelks */
        struct list_head blocked_reservelks;        /* list of blocked reservelks */
        struct list_head blocked_calls;  /* List of blocked lock calls while a reserve is held*/
        int              mandatory;      /* if mandatory locking is enabled */

        inode_t          *refkeeper;     /* hold refs on an inode while locks are
                                            held to prevent pruning */
};
typedef struct __pl_inode pl_inode_t;


typedef struct {
        gf_boolean_t    mandatory;      /* if mandatory locking is enabled */
        gf_boolean_t    trace;          /* trace lock requests in and out */
        char           *brickname;
} posix_locks_private_t;


typedef struct {
        gf_boolean_t   entrylk_count_req;
        gf_boolean_t   inodelk_count_req;
        gf_boolean_t   inodelk_dom_count_req;
        gf_boolean_t   posixlk_count_req;
        gf_boolean_t   parent_entrylk_req;

        /* used by {f,}truncate */
        loc_t  loc;
        fd_t  *fd;
        off_t  offset;
        dict_t *xdata;
        enum {TRUNCATE, FTRUNCATE} op;
} pl_local_t;


typedef struct {
        struct list_head locks_list;
} pl_fdctx_t;


struct _locker {
        struct list_head  lockers;
        char             *volume;
        inode_t          *inode;
        gf_lkowner_t      owner;
};

typedef struct _locks_ctx {
        pthread_mutex_t      lock;
        struct list_head     inodelk_lockers;
        struct list_head     entrylk_lockers;
} pl_ctx_t;


pl_ctx_t *
pl_ctx_get (client_t *client, xlator_t *xlator);

int
pl_inodelk_client_cleanup (xlator_t *this, pl_ctx_t *ctx);

int
pl_entrylk_client_cleanup (xlator_t *this, pl_ctx_t *ctx);

#endif /* __POSIX_LOCKS_H__ */
