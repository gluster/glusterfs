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
#include <glusterfs/list.h>
#include <glusterfs/inode.h>
#include "client-mem-types.h"
#include "protocol-common.h"
#include "glusterfs3.h"
#include "glusterfs3-xdr.h"
#include <glusterfs/fd-lk.h>
#include <glusterfs/defaults.h>
#include <glusterfs/default-args.h>
#include "client-messages.h"

/* FIXME: Needs to be defined in a common file */
#define CLIENT_DUMP_LOCKS "trusted.glusterfs.clientlk-dump"
#define GF_MAX_SOCKET_WINDOW_SIZE (1 * GF_UNIT_MB)
#define GF_MIN_SOCKET_WINDOW_SIZE (0)

typedef enum {
    DEFAULT_REMOTE_FD = 0,
    FALLBACK_TO_ANON_FD = 1
} clnt_remote_fd_flags_t;

#define CLIENT_POST_FOP(fop, this_rsp_u, this_args_cbk, params...)             \
    do {                                                                       \
        gf_common_rsp *_this_rsp = &CPD_RSP_FIELD(this_rsp_u, fop);            \
                                                                               \
        int _op_ret = _this_rsp->op_ret;                                       \
        int _op_errno = gf_error_to_errno(_this_rsp->op_errno);                \
        args_##fop##_cbk_store(this_args_cbk, _op_ret, _op_errno, params);     \
    } while (0)

#define CLIENT_POST_FOP_TYPE(fop, this_rsp_u, this_args_cbk, params...)        \
    do {                                                                       \
        gfs3_##fop##_rsp *_this_rsp = &CPD_RSP_FIELD(this_rsp_u, fop);         \
                                                                               \
        int _op_ret = _this_rsp->op_ret;                                       \
        int _op_errno = gf_error_to_errno(_this_rsp->op_errno);                \
        args_##fop##_cbk_store(this_args_cbk, _op_ret, _op_errno, params);     \
    } while (0)

#define CLIENT_GET_REMOTE_FD(xl, fd, flags, remote_fd, op_errno, label)        \
    do {                                                                       \
        int _ret = 0;                                                          \
        _ret = client_get_remote_fd(xl, fd, flags, &remote_fd);                \
        if (_ret < 0) {                                                        \
            op_errno = errno;                                                  \
            goto label;                                                        \
        }                                                                      \
        if (remote_fd == -1) {                                                 \
            gf_smsg(xl->name, GF_LOG_WARNING, EBADFD, PC_MSG_BAD_FD,           \
                    "gfid=%s", uuid_utoa(fd->inode->gfid), NULL);              \
            op_errno = EBADFD;                                                 \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define CLIENT_STACK_UNWIND(op, frame, params...)                              \
    do {                                                                       \
        if (!frame)                                                            \
            break;                                                             \
        clnt_local_t *__local = frame->local;                                  \
        frame->local = NULL;                                                   \
        STACK_UNWIND_STRICT(op, frame, params);                                \
        client_local_wipe(__local);                                            \
    } while (0)

struct clnt_options {
    char *remote_subvolume;
    int ping_timeout;
};

typedef struct clnt_conf {
    struct rpc_clnt *rpc;
    struct clnt_options opt;
    struct rpc_clnt_config rpc_conf;
    struct list_head saved_fds;
    pthread_spinlock_t fd_lock; /* protects saved_fds list
                                 * and all fdctx */
    pthread_mutex_t lock;
    int connected;

    rpc_clnt_prog_t *fops;
    rpc_clnt_prog_t *mgmt;
    rpc_clnt_prog_t *handshake;
    rpc_clnt_prog_t *dump;

    int client_id;
    uint64_t reopen_fd_count; /* Count of fds reopened after a
                                 connection is established */
    gf_lock_t rec_lock;
    int skip_notify;

    int last_sent_event;        /* Flag used to make sure we are
                                   not repeating the same event
                                   which was sent earlier */
    char portmap_err_logged;    /* flag used to prevent
                                   excessive logging */
    char disconnect_err_logged; /* flag used to prevent
                                   excessive disconnect
                                   logging */
    char parent_down;
    gf_boolean_t quick_reconnect; /* When reconnecting after
                                     portmap query, do not let
                                     the reconnection happen after
                                     the usual 3-second wait
                                  */
    gf_boolean_t filter_o_direct; /* if set, filter O_DIRECT from
                                     the flags list of open() */
    /* set volume is the op which results in creating/re-using
     * the conn-id and is called once per connection, this remembers
     * how manytimes set_volume is called
     */
    uint64_t setvol_count;

    gf_boolean_t send_gids; /* let the server resolve gids */

    int event_threads; /* # of event threads
                        * configured */

    gf_boolean_t destroy; /* if enabled implies fini was called
                           * on @this xlator instance */

    gf_boolean_t child_up; /* Set to true, when child is up, and
                            * false, when child is down */

    gf_boolean_t can_log_disconnect; /* socket level connection is
                                      * up, disconnects can be
                                      * logged
                                      */

    gf_boolean_t old_protocol;         /* used only for old-protocol testing */
    pthread_cond_t fini_complete_cond; /* Used to wait till we finsh the fini
                                          compltely, ie client_fini_complete
                                          to return*/
    gf_boolean_t fini_completed;
    gf_boolean_t strict_locks; /* When set, doesn't reopen saved fds after
                                  reconnect if POSIX locks are held on them.
                                  Hence subsequent operations on these fds will
                                  fail. This is necessary for stricter lock
                                  complaince as bricks cleanup any granted
                                  locks when a client disconnects.
                               */

    gf_boolean_t connection_to_brick; /*True from attempt to connect to brick
                                        till disconnection to brick*/
} clnt_conf_t;

typedef struct _client_fd_ctx {
    struct list_head sfd_pos; /*  Stores the reference to this
                                  fd's position in the saved_fds list.
                              */
    int64_t remote_fd;
    char is_dir;
    char released;
    int32_t flags;
    fd_lk_ctx_t *lk_ctx;
    uuid_t gfid;
    void (*reopen_done)(struct _client_fd_ctx *, int64_t rfd, xlator_t *);
    struct list_head lock_list; /* List of all granted locks on this fd */
    int32_t reopen_attempts;
} clnt_fd_ctx_t;

typedef struct _client_posix_lock {
    fd_t *fd; /* The fd on which the lk operation was made */

    struct gf_flock user_flock; /* the flock supplied by the user */
    off_t fl_start;
    off_t fl_end;
    short fl_type;
    int32_t cmd;        /* the cmd for the lock call */
    gf_lkowner_t owner; /* lock owner from fuse */
    struct list_head
        list; /* reference used to add to the fdctx list of locks */
} client_posix_lock_t;

typedef struct client_local {
    loc_t loc;
    loc_t loc2;
    fd_t *fd;
    fd_t *fd_out; /* used in copy_file_range */
    clnt_fd_ctx_t *fdctx;
    uint32_t flags;
    struct iobref *iobref;

    client_posix_lock_t *client_lock;
    gf_lkowner_t owner;
    int32_t cmd;
    struct list_head lock_list;
    pthread_mutex_t mutex;
    char *name;
    gf_boolean_t attempt_reopen;
    /*
     * The below boolean variable is used
     * only for copy_file_range fop
     */
    gf_boolean_t attempt_reopen_out;
} clnt_local_t;

typedef struct client_args {
    loc_t *loc;
    /*
     * This is the source fd for copy_file_range and
     * the default fd for any other fd based fop which
     * requires only one fd (i.e. opetates on one fd)
     */
    fd_t *fd;
    fd_t *fd_out; /* this is the destination fd for copy_file_range */
    const char *linkname;
    struct iobref *iobref;
    struct iovec *vector;
    dict_t *xattr;
    struct iatt *stbuf;
    loc_t *oldloc;
    loc_t *newloc;
    const char *name;
    struct gf_flock *flock;
    const char *volume;
    const char *basename;

    off_t offset;
    /*
     * According to the man page of copy_file_range,
     * the offsets for source and destination file
     * are of type loff_t. But the type loff_t is
     * linux specific and is actual a typedef of
     * off64_t.
     */
    off64_t off_in;  /* used in copy_file_range for source fd */
    off64_t off_out; /* used in copy_file_range for dst fd */
    int32_t mask;
    int32_t cmd;
    size_t size;
    mode_t mode;
    dev_t rdev;
    int32_t flags;
    int32_t count;
    int32_t datasync;
    entrylk_cmd cmd_entrylk;
    entrylk_type type;
    gf_xattrop_flags_t optype;
    int32_t valid;
    int32_t len;
    gf_seek_what_t what;
    struct gf_lease *lease;

    mode_t umask;
    dict_t *xdata;
    lock_migration_info_t *locklist;
} clnt_args_t;

typedef struct client_payload {
    struct iobref *iobref;
    struct iovec *payload;
    struct iovec *rsphdr;
    struct iovec *rsp_payload;
    struct iobref *rsp_iobref;
    int payload_cnt;
    int rsphdr_cnt;
    int rsp_payload_cnt;
} client_payload_t;

typedef ssize_t (*gfs_serialize_t)(struct iovec outmsg, void *args);

clnt_fd_ctx_t *
this_fd_get_ctx(fd_t *file, xlator_t *this);
clnt_fd_ctx_t *
this_fd_del_ctx(fd_t *file, xlator_t *this);
void
this_fd_set_ctx(fd_t *file, xlator_t *this, loc_t *loc, clnt_fd_ctx_t *ctx);

int
client_local_wipe(clnt_local_t *local);
int
client_submit_request(xlator_t *this, void *req, call_frame_t *frame,
                      rpc_clnt_prog_t *prog, int procnum, fop_cbk_fn_t cbk,
                      client_payload_t *cp, xdrproc_t xdrproc);

int
unserialize_rsp_dirent(xlator_t *this, struct gfs3_readdir_rsp *rsp,
                       gf_dirent_t *entries);
int
unserialize_rsp_direntp(xlator_t *this, fd_t *fd, struct gfs3_readdirp_rsp *rsp,
                        gf_dirent_t *entries);

int
clnt_readdir_rsp_cleanup(gfs3_readdir_rsp *rsp);
int
clnt_readdirp_rsp_cleanup(gfs3_readdirp_rsp *rsp);
int
client_attempt_lock_recovery(xlator_t *this, clnt_fd_ctx_t *fdctx);
int32_t
delete_granted_locks_owner(fd_t *fd, gf_lkowner_t *owner);
void
__delete_granted_locks_owner_from_fdctx(clnt_fd_ctx_t *fdctx,
                                        gf_lkowner_t *owner,
                                        struct list_head *deleted);
void
destroy_client_locks_from_list(struct list_head *deleted);
int32_t
client_cmd_to_gf_cmd(int32_t cmd, int32_t *gf_cmd);
void
client_save_number_fds(clnt_conf_t *conf, int count);
int
dump_client_locks(inode_t *inode);
int32_t
is_client_dump_locks_cmd(char *name);
int32_t
client_dump_locks(char *name, inode_t *inode, dict_t *dict);
int
client_fdctx_destroy(xlator_t *this, clnt_fd_ctx_t *fdctx);

int
client_fd_lk_list_empty(fd_lk_ctx_t *lk_ctx, gf_boolean_t use_try_lock);
void
client_default_reopen_done(clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this);
void
client_attempt_reopen(fd_t *fd, xlator_t *this);
int
client_get_remote_fd(xlator_t *this, fd_t *fd, int flags, int64_t *remote_fd);
int
client_fd_fop_prepare_local(call_frame_t *frame, fd_t *fd, int64_t remote_fd);
gf_boolean_t
__is_fd_reopen_in_progress(clnt_fd_ctx_t *fdctx);
int
client_notify_dispatch(xlator_t *this, int32_t event, void *data, ...);
int
client_notify_dispatch_uniq(xlator_t *this, int32_t event, void *data, ...);

gf_boolean_t
client_is_reopen_needed(fd_t *fd, xlator_t *this, int64_t remote_fd);

int
client_add_fd_to_saved_fds(xlator_t *this, fd_t *fd, loc_t *loc, int32_t flags,
                           int64_t remote_fd, int is_dir);
int
clnt_unserialize_rsp_locklist(xlator_t *this, struct gfs3_getactivelk_rsp *rsp,
                              lock_migration_info_t *lmi);
void
clnt_getactivelk_rsp_cleanup(gfs3_getactivelk_rsp *rsp);

void
clnt_setactivelk_req_cleanup(gfs3_setactivelk_req *req);

int
serialize_req_locklist(lock_migration_info_t *locklist,
                       gfs3_setactivelk_req *req);

void
clnt_getactivelk_rsp_cleanup_v2(gfx_getactivelk_rsp *rsp);

void
clnt_setactivelk_req_cleanup_v2(gfx_setactivelk_req *req);

int
serialize_req_locklist_v2(lock_migration_info_t *locklist,
                          gfx_setactivelk_req *req);

int
clnt_unserialize_rsp_locklist_v2(xlator_t *this,
                                 struct gfx_getactivelk_rsp *rsp,
                                 lock_migration_info_t *lmi);

int
unserialize_rsp_dirent_v2(xlator_t *this, struct gfx_readdir_rsp *rsp,
                          gf_dirent_t *entries);
int
unserialize_rsp_direntp_v2(xlator_t *this, fd_t *fd,
                           struct gfx_readdirp_rsp *rsp, gf_dirent_t *entries);

int
clnt_readdir_rsp_cleanup_v2(gfx_readdir_rsp *rsp);
int
clnt_readdirp_rsp_cleanup_v2(gfx_readdirp_rsp *rsp);

int
client_add_lock_for_recovery(fd_t *fd, struct gf_flock *flock,
                             gf_lkowner_t *owner, int32_t cmd);

int
client_is_setlk(int32_t cmd);

#endif /* !_CLIENT_H */
