/*
   Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
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

/* FIXME: Needs to be defined in a common file */
#define CLIENT_CMD_CONNECT    "trusted.glusterfs.client-connect"
#define CLIENT_CMD_DISCONNECT "trusted.glusterfs.client-disconnect"
#define CLIENT_DUMP_LOCKS     "trusted.glusterfs.clientlk-dump"

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
	struct timeval         last_sent;
	struct timeval         last_received;

        rpc_clnt_prog_t       *fops;
        rpc_clnt_prog_t       *mgmt;
        rpc_clnt_prog_t       *handshake;
        rpc_clnt_prog_t       *dump;

        uint64_t               reopen_fd_count; /* Count of fds reopened after a
                                                   connection is established */
        gf_lock_t              rec_lock;
        int                    skip_notify;
} clnt_conf_t;

typedef struct _client_fd_ctx {
        struct list_head  sfd_pos;      /*  Stores the reference to this
                                            fd's position in the saved_fds list.
                                        */
        int64_t           remote_fd;
        inode_t          *inode;
        uint64_t          ino;
        uint64_t          gen;
        char              is_dir;
        char              released;
        int32_t           flags;
        int32_t           wbflags;

        pthread_mutex_t   mutex;
        struct list_head  lock_list;     /* List of all granted locks on this fd */
} clnt_fd_ctx_t;

typedef struct _client_posix_lock {
        fd_t              *fd;            /* The fd on which the lk operation was made */

        struct gf_flock    user_flock;    /* the flock supplied by the user */
        off_t              fl_start;
        off_t              fl_end;
        short              fl_type;
        int32_t            cmd;           /* the cmd for the lock call */
        uint64_t           owner;         /* lock owner from fuse */

        struct list_head   list;          /* reference used to add to the fdctx list of locks */
} client_posix_lock_t;

typedef struct client_local {
        loc_t                loc;
        loc_t                loc2;
        fd_t                *fd;
        clnt_fd_ctx_t       *fdctx;
        uint32_t             flags;
        uint32_t             wbflags;
        struct iobref       *iobref;

        client_posix_lock_t *client_lock;
        uint64_t             owner;
        int32_t              cmd;
        struct list_head     lock_list;
        pthread_mutex_t      mutex;
} clnt_local_t;

typedef struct client_args {
        loc_t              *loc;
        fd_t               *fd;
        dict_t             *xattr_req;
        const char         *linkname;
        struct iobref      *iobref;
        struct iovec       *vector;
        dict_t             *xattr;
        struct iatt        *stbuf;
        dict_t             *dict;
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
        int32_t             wbflags;
        int32_t             count;
        int32_t             datasync;
        entrylk_cmd         cmd_entrylk;
        entrylk_type        type;
        gf_xattrop_flags_t  optype;
        int32_t             valid;
        int32_t             len;
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
                           struct iobref *iobref, gfs_serialize_t sfunc,
                           struct iovec *rsphdr, int rsphdr_count,
                           struct iovec *rsp_payload, int rsp_count,
                           struct iobref *rsp_iobref);

int protocol_client_reopendir (xlator_t *this, clnt_fd_ctx_t *fdctx);
int protocol_client_reopen (xlator_t *this, clnt_fd_ctx_t *fdctx);

int unserialize_rsp_dirent (struct gfs3_readdir_rsp *rsp, gf_dirent_t *entries);
int unserialize_rsp_direntp (struct gfs3_readdirp_rsp *rsp, gf_dirent_t *entries);

int clnt_readdir_rsp_cleanup (gfs3_readdir_rsp *rsp);
int clnt_readdirp_rsp_cleanup (gfs3_readdirp_rsp *rsp);
int client_attempt_lock_recovery (xlator_t *this, clnt_fd_ctx_t *fdctx);
int32_t delete_granted_locks_owner (fd_t *fd, uint64_t owner);
int client_add_lock_for_recovery (fd_t *fd, struct gf_flock *flock, uint64_t owner,
                                  int32_t cmd);
uint64_t decrement_reopen_fd_count (xlator_t *this, clnt_conf_t *conf);
int32_t delete_granted_locks_fd (clnt_fd_ctx_t *fdctx);
int32_t client_cmd_to_gf_cmd (int32_t cmd, int32_t *gf_cmd);
void client_save_number_fds (clnt_conf_t *conf, int count);
int dump_client_locks (inode_t *inode);
int client_notify_parents_child_up (xlator_t *this);
int32_t is_client_dump_locks_cmd (char *name);
int32_t client_dump_locks (char *name, inode_t *inode,
                           dict_t *dict);
int client_fdctx_destroy (xlator_t *this, clnt_fd_ctx_t *fdctx);

#endif /* !_CLIENT_H */
