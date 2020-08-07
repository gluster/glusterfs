/*
   Copyright (c) 2006-2012, 2015-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __POSIX_LOCKS_H__
#define __POSIX_LOCKS_H__

#include <glusterfs/compat-errno.h>
#include <glusterfs/stack.h>
#include <glusterfs/call-stub.h>
#include "locks-mem-types.h"
#include <glusterfs/client_t.h>

#include <glusterfs/lkowner.h>

typedef enum {
    MLK_NONE,
    MLK_FILE_BASED,
    MLK_FORCED,
    MLK_OPTIMAL
} mlk_mode_t; /* defines different mandatory locking modes*/

struct __pl_fd;

struct __posix_lock {
    struct list_head list;

    off_t fl_start;
    off_t fl_end;
    uint32_t lk_flags;

    short fl_type;
    short blocked;              /* waiting to acquire */
    struct gf_flock user_flock; /* the flock supplied by the user */
    xlator_t *this;             /* required for blocked locks */
    unsigned long fd_num;

    fd_t *fd;
    call_frame_t *frame;

    time_t blkd_time;    /* time at which lock was queued into blkd list */
    time_t granted_time; /* time at which lock was queued into active list */

    /* These two together serve to uniquely identify each process
       across nodes */

    void *client; /* to identify client node */

    /* This field uniquely identifies the client the lock belongs to.  As
     * lock migration is handled by rebalance, the client_t object will be
     * overwritten by rebalance and can't be deemed as the owner of the
     * lock on destination. Hence, the below field is migrated from
     * source to destination by lock_migration_info_t and updated on the
     * destination. So that on client-server disconnection, server can
     * cleanup the locks proper;y.  */

    char *client_uid;
    gf_lkowner_t owner;
    pid_t client_pid; /* pid of client process */

    int blocking;
};
typedef struct __posix_lock posix_lock_t;

struct __pl_inode_lock {
    struct list_head list;
    struct list_head blocked_locks; /* list_head pointing to blocked_inodelks */
    struct list_head contend;       /* list of contending locks */
    int ref;

    off_t fl_start;
    off_t fl_end;

    const char *volume;

    struct gf_flock user_flock; /* the flock supplied by the user */
    xlator_t *this;             /* required for blocked locks */
    struct __pl_inode *pl_inode;

    call_frame_t *frame;

    time_t blkd_time;    /* time at which lock was queued into blkd list */
    time_t granted_time; /* time at which lock was queued into active list */

    /*last time at which lock contention was detected and notified*/
    struct timespec contention_time;

    /* These two together serve to uniquely identify each process
       across nodes */

    void *client; /* to identify client node */
    gf_lkowner_t owner;
    pid_t client_pid; /* pid of client process */

    char *connection_id; /* stores the client connection id */

    struct list_head client_list; /* list of all locks from a client */
    short fl_type;

    int32_t status; /* Error code when we try to grant a lock in blocked
                       state */
};
typedef struct __pl_inode_lock pl_inode_lock_t;

struct _pl_rw_req {
    struct list_head list;
    call_stub_t *stub;
    posix_lock_t region;
};
typedef struct _pl_rw_req pl_rw_req_t;

struct _pl_dom_list {
    struct list_head inode_list; /* list_head back to pl_inode_t */
    const char *domain;
    struct list_head entrylk_list;     /* List of entry locks */
    struct list_head blocked_entrylks; /* List of all blocked entrylks */
    struct list_head inodelk_list;     /* List of inode locks */
    struct list_head blocked_inodelks; /* List of all blocked inodelks */
};
typedef struct _pl_dom_list pl_dom_list_t;

struct __entry_lock {
    struct list_head domain_list;   /* list_head back to pl_dom_list_t */
    struct list_head blocked_locks; /* list_head back to blocked_entrylks */
    struct list_head contend;       /* list of contending locks */
    int ref;

    call_frame_t *frame;
    xlator_t *this;
    struct __pl_inode *pinode;

    const char *volume;

    const char *basename;

    time_t blkd_time;    /* time at which lock was queued into blkd list */
    time_t granted_time; /* time at which lock was queued into active list */

    /*last time at which lock contention was detected and notified*/
    struct timespec contention_time;

    void *client;
    gf_lkowner_t owner;
    pid_t client_pid; /* pid of client process */

    char *connection_id; /* stores the client connection id */

    struct list_head client_list; /* list of all locks from a client */
    entrylk_type type;
};
typedef struct __entry_lock pl_entry_lock_t;

/* The "simulated" inode. This contains a list of all the locks associated
   with this file */

struct __pl_inode {
    pthread_mutex_t mutex;

    struct list_head dom_list;           /* list of domains */
    struct list_head ext_list;           /* list of fcntl locks */
    struct list_head rw_list;            /* list of waiting r/w requests */
    struct list_head reservelk_list;     /* list of reservelks */
    struct list_head blocked_reservelks; /* list of blocked reservelks */
    struct list_head blocked_calls;      /* List of blocked lock calls while a
                                            reserve is held*/
    struct list_head metalk_list;        /* Meta lock list */
    struct list_head queued_locks;       /* This is to store the incoming lock
                                            requests while meta lock is enabled */
    struct list_head waiting; /* List of pending fops waiting to unlink/rmdir
                                 the inode. */
    int mandatory;            /* if mandatory locking is enabled */

    inode_t *refkeeper; /* hold refs on an inode while locks are
                           held to prevent pruning */
    uuid_t gfid;        /* placeholder for gfid of the inode */
    inode_t *inode;     /* pointer to be used for ref and unref
                           of inode_t as long as there are
                           locks on it */
    gf_boolean_t migrated;

    /* Flag to indicate whether to read mlock-enforce xattr from disk */
    gf_boolean_t check_mlock_info;

    /* Mandatory_lock enforce: IO will be allowed if and only if the lkowner has
       held the lock.

       Note: An xattr is set on the file to recover this information post
       reboot. If client does not want mandatory lock to be enforced, then it
       should remove this xattr explicitly
    */
    gf_boolean_t mlock_enforced;
    /* There are scenarios where mandatory lock is granted but there are IOs
       pending at posix level. To avoid this before preempting the previous lock
       owner, we wait for all the fops to be unwound.
    */
    int fop_wind_count;
    pthread_cond_t check_fop_wind_count;

    gf_boolean_t track_fop_wind_count;

    int32_t links;           /* Number of hard links the inode has. */
    uint32_t remove_running; /* Number of remove operations running. */
    gf_boolean_t is_locked;  /* Regular locks will be blocked. */
    gf_boolean_t removed;    /* The inode has been deleted. */
};
typedef struct __pl_inode pl_inode_t;

struct __pl_metalk {
    pthread_mutex_t mutex;
    /* For pl_inode meta lock list */
    struct list_head list;
    /* For pl_ctx_t list */
    struct list_head client_list;
    char *client_uid;

    pl_inode_t *pl_inode;
    int ref;
};
typedef struct __pl_metalk pl_meta_lock_t;

typedef struct {
    char *brickname;
    uint32_t revocation_secs;
    uint32_t revocation_max_blocked;
    uint32_t notify_contention_delay;
    mlk_mode_t mandatory_mode; /* holds current mandatory locking mode */
    gf_boolean_t trace;        /* trace lock requests in and out */
    gf_boolean_t monkey_unlocking;
    gf_boolean_t revocation_clear_all;
    gf_boolean_t notify_contention;
    gf_boolean_t mlock_enforced;
} posix_locks_private_t;

typedef struct {
    data_t *inodelk_dom_count_req;

    dict_t *xdata;
    loc_t loc[2];
    fd_t *fd;
    inode_t *inode;
    off_t offset;
    glusterfs_fop_t op;
    gf_boolean_t entrylk_count_req;
    gf_boolean_t inodelk_count_req;
    gf_boolean_t posixlk_count_req;
    gf_boolean_t parent_entrylk_req;
    gf_boolean_t multiple_dom_lk_requests;
    int update_mlock_enforced_flag;
} pl_local_t;

typedef struct {
    struct list_head locks_list;
} pl_fdctx_t;

struct _locker {
    struct list_head lockers;
    char *volume;
    inode_t *inode;
    gf_lkowner_t owner;
};

typedef struct _locks_ctx {
    pthread_mutex_t lock;
    struct list_head inodelk_lockers;
    struct list_head entrylk_lockers;
    struct list_head metalk_list;
} pl_ctx_t;

typedef struct _multi_dom_lk_data {
    xlator_t *this;
    inode_t *inode;
    dict_t *xdata_rsp;
    gf_boolean_t keep_max;
} multi_dom_lk_data;

typedef enum { DECREMENT, INCREMENT } pl_count_op_t;

pl_ctx_t *
pl_ctx_get(client_t *client, xlator_t *xlator);

int
pl_inodelk_client_cleanup(xlator_t *this, pl_ctx_t *ctx);

int
pl_entrylk_client_cleanup(xlator_t *this, pl_ctx_t *ctx);

#endif /* __POSIX_LOCKS_H__ */
