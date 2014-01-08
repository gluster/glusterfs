/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CLIENT_H
#define _CLIENT_H

#include <pthread.h>
#include <stdint.h>

#include "rpc-clnt.h"
#include "list.h"
#include "inode.h"
#include "client-mem-types.h"
#include "protocol-common.h"
#include "glusterfs3.h"
#include "fd-lk.h"

/* FIXME: Needs to be defined in a common file */
#define CLIENT_CMD_CONNECT    "trusted.glusterfs.client-connect"
#define CLIENT_CMD_DISCONNECT "trusted.glusterfs.client-disconnect"
#define CLIENT_DUMP_LOCKS     "trusted.glusterfs.clientlk-dump"
#define GF_MAX_SOCKET_WINDOW_SIZE  (1 * GF_UNIT_MB)
#define GF_MIN_SOCKET_WINDOW_SIZE  (0)

typedef enum {
        GF_LK_HEAL_IN_PROGRESS,
        GF_LK_HEAL_DONE,
} lk_heal_state_t;

typedef enum {
        DEFAULT_REMOTE_FD = 0,
        FALLBACK_TO_ANON_FD = 1
} clnt_remote_fd_flags_t;

#define CLIENT_GET_REMOTE_FD(xl, fd, flags, remote_fd, op_errno, label) \
        do {                                                            \
                int     _ret    = 0;                                    \
                _ret = client_get_remote_fd (xl, fd, flags, &remote_fd);\
                if (_ret < 0) {                                         \
                        op_errno = errno;                               \
                        goto label;                                     \
                }                                                       \
                if (remote_fd == -1) {                                  \
                        gf_log (xl->name, GF_LOG_WARNING, " (%s) "      \
                                "remote_fd is -1. EBADFD",              \
                                uuid_utoa (fd->inode->gfid));           \
                        op_errno = EBADFD;                              \
                        goto label;                                     \
                }                                                       \
        } while (0)

#define CLIENT_STACK_UNWIND(op, frame, params ...) do {             \
                clnt_local_t *__local = frame->local;               \
                frame->local = NULL;                                \
                STACK_UNWIND_STRICT (op, frame, params);            \
                client_local_wipe (__local);                        \
        } while (0)


struct clnt_options {
        char *remote_subvolume;
        int   ping_timeout;
};

typedef struct clnt_conf {
        struct rpc_clnt       *rpc;
        struct clnt_options    opt;
        struct rpc_clnt_config rpc_conf;
	struct list_head       saved_fds;
        pthread_mutex_t        lock;
        int                    connecting;
        int                    connected;

        rpc_clnt_prog_t       *fops;
        rpc_clnt_prog_t       *mgmt;
        rpc_clnt_prog_t       *handshake;
        rpc_clnt_prog_t       *dump;

        uint64_t               reopen_fd_count; /* Count of fds reopened after a
                                                   connection is established */
        gf_lock_t              rec_lock;
        int                    skip_notify;

        int                    last_sent_event; /* Flag used to make sure we are
                                                   not repeating the same event
                                                   which was sent earlier */
        char                   portmap_err_logged; /* flag used to prevent
                                                      excessive logging */
        char                   disconnect_err_logged; /* flag used to prevent
                                                         excessive disconnect
                                                         logging */

        char                   need_different_port; /* flag used to change the
                                                       portmap path in case of
                                                       'tcp,rdma' on server */
        gf_boolean_t           lk_heal;
        uint16_t               lk_version; /* this variable is used to distinguish
                                              client-server transaction while
                                              performing lock healing */
        struct timespec        grace_ts;
        gf_timer_t            *grace_timer;
        gf_boolean_t           grace_timer_needed; /* The state of this flag will
                                                      be used to decide whether
                                                      a new grace-timer must be
                                                      registered or not. False
                                                      means dont register, true
                                                      means register */
        char                   parent_down;
	gf_boolean_t           quick_reconnect; /* When reconnecting after
						   portmap query, do not let
						   the reconnection happen after
						   the usual 3-second wait
						*/
        gf_boolean_t           filter_o_direct; /* if set, filter O_DIRECT from
                                                   the flags list of open() */
        gf_boolean_t           send_gids; /* let the server resolve gids */
        /* set volume is the op which results in creating/re-using
         * the conn-id and is called once per connection, this remembers
         * how manytimes set_volume is called
         */
        uint64_t               setvol_count;
} clnt_conf_t;

typedef struct _client_fd_ctx {
        struct list_head  sfd_pos;      /*  Stores the reference to this
                                            fd's position in the saved_fds list.
                                        */
        int64_t           remote_fd;
        char              is_dir;
        char              released;
        int32_t           flags;
        fd_lk_ctx_t      *lk_ctx;
        pthread_mutex_t   mutex;
        lk_heal_state_t   lk_heal_state;
        uuid_t            gfid;
        void (*reopen_done) (struct _client_fd_ctx*, xlator_t *);
        struct list_head  lock_list;     /* List of all granted locks on this fd */
        int32_t           reopen_attempts;
} clnt_fd_ctx_t;

typedef struct _client_posix_lock {
        fd_t              *fd;            /* The fd on which the lk operation was made */

        struct gf_flock    user_flock;    /* the flock supplied by the user */
        off_t              fl_start;
        off_t              fl_end;
        short              fl_type;
        int32_t            cmd;           /* the cmd for the lock call */
        gf_lkowner_t       owner; /* lock owner from fuse */
        struct list_head   list;          /* reference used to add to the fdctx list of locks */
} client_posix_lock_t;

typedef struct client_local {
        loc_t                loc;
        loc_t                loc2;
        fd_t                *fd;
        clnt_fd_ctx_t       *fdctx;
        uint32_t             flags;
        struct iobref       *iobref;

        client_posix_lock_t *client_lock;
        gf_lkowner_t         owner;
        int32_t              cmd;
        struct list_head     lock_list;
        pthread_mutex_t      mutex;
        char                *name;
        gf_boolean_t         attempt_reopen;
} clnt_local_t;

typedef struct client_args {
        loc_t              *loc;
        fd_t               *fd;
        const char         *linkname;
        struct iobref      *iobref;
        struct iovec       *vector;
        dict_t             *xattr;
        struct iatt        *stbuf;
        loc_t              *oldloc;
        loc_t              *newloc;
        const char         *name;
        struct gf_flock    *flock;
        const char         *volume;
        const char         *basename;
        off_t               offset;
        int32_t             mask;
        int32_t             cmd;
        size_t              size;
        mode_t              mode;
        dev_t               rdev;
        int32_t             flags;
        int32_t             count;
        int32_t             datasync;
        entrylk_cmd         cmd_entrylk;
        entrylk_type        type;
        gf_xattrop_flags_t  optype;
        int32_t             valid;
        int32_t             len;

        mode_t              umask;
        dict_t             *xdata;
} clnt_args_t;

typedef ssize_t (*gfs_serialize_t) (struct iovec outmsg, void *args);

clnt_fd_ctx_t *this_fd_get_ctx (fd_t *file, xlator_t *this);
clnt_fd_ctx_t *this_fd_del_ctx (fd_t *file, xlator_t *this);
void this_fd_set_ctx (fd_t *file, xlator_t *this, loc_t *loc,
                      clnt_fd_ctx_t *ctx);

int client_local_wipe (clnt_local_t *local);
int client_submit_request (xlator_t *this, void *req,
                           call_frame_t *frame, rpc_clnt_prog_t *prog,
                           int procnum, fop_cbk_fn_t cbk,
                           struct iobref *iobref,
                           struct iovec *rsphdr, int rsphdr_count,
                           struct iovec *rsp_payload, int rsp_count,
                           struct iobref *rsp_iobref, xdrproc_t xdrproc);

int unserialize_rsp_dirent (struct gfs3_readdir_rsp *rsp, gf_dirent_t *entries);
int unserialize_rsp_direntp (xlator_t *this, fd_t *fd,
                             struct gfs3_readdirp_rsp *rsp, gf_dirent_t *entries);

int clnt_readdir_rsp_cleanup (gfs3_readdir_rsp *rsp);
int clnt_readdirp_rsp_cleanup (gfs3_readdirp_rsp *rsp);
int client_attempt_lock_recovery (xlator_t *this, clnt_fd_ctx_t *fdctx);
int32_t delete_granted_locks_owner (fd_t *fd, gf_lkowner_t *owner);
int client_add_lock_for_recovery (fd_t *fd, struct gf_flock *flock,
                                  gf_lkowner_t *owner, int32_t cmd);
int32_t delete_granted_locks_fd (clnt_fd_ctx_t *fdctx);
int32_t client_cmd_to_gf_cmd (int32_t cmd, int32_t *gf_cmd);
void client_save_number_fds (clnt_conf_t *conf, int count);
int dump_client_locks (inode_t *inode);
int client_notify_parents_child_up (xlator_t *this);
int32_t is_client_dump_locks_cmd (char *name);
int32_t client_dump_locks (char *name, inode_t *inode,
                           dict_t *dict);
int client_fdctx_destroy (xlator_t *this, clnt_fd_ctx_t *fdctx);

uint32_t client_get_lk_ver (clnt_conf_t *conf);

int32_t client_type_to_gf_type (short l_type);

int client_mark_fd_bad (xlator_t *this);

int client_set_lk_version (xlator_t *this);

int client_fd_lk_list_empty (fd_lk_ctx_t *lk_ctx, gf_boolean_t use_try_lock);
void client_default_reopen_done (clnt_fd_ctx_t *fdctx, xlator_t *this);
void client_attempt_reopen (fd_t *fd, xlator_t *this);
int client_get_remote_fd (xlator_t *this, fd_t *fd, int flags,
                          int64_t *remote_fd);
int client_fd_fop_prepare_local (call_frame_t *frame, fd_t *fd,
                                 int64_t remote_fd);
gf_boolean_t
__is_fd_reopen_in_progress (clnt_fd_ctx_t *fdctx);
#endif /* !_CLIENT_H */
