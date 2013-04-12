/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _SERVER_HELPERS_H
#define _SERVER_HELPERS_H

#include "server.h"

#define CALL_STATE(frame)   ((server_state_t *)frame->root->state)

#define BOUND_XL(frame)     ((xlator_t *) CALL_STATE(frame)->conn->bound_xl)

#define XPRT_FROM_FRAME(frame) ((rpc_transport_t *) CALL_STATE(frame)->xprt)

#define SERVER_CONNECTION(frame)                                \
        ((server_connection_t *) CALL_STATE(frame)->conn)

#define SERVER_CONF(frame)                                              \
        ((server_conf_t *)XPRT_FROM_FRAME(frame)->this->private)

#define XPRT_FROM_XLATOR(this) ((((server_conf_t *)this->private))->listen)

#define INODE_LRU_LIMIT(this)                                           \
        (((server_conf_t *)(this->private))->config.inode_lru_limit)

#define IS_ROOT_INODE(inode) (inode == inode->table->root)

#define IS_NOT_ROOT(pathlen) ((pathlen > 2)? 1 : 0)

void free_state (server_state_t *state);

void server_loc_wipe (loc_t *loc);

int32_t
gf_add_locker (server_connection_t *conn, const char *volume,
               loc_t *loc,
               fd_t *fd,
               pid_t pid,
               gf_lkowner_t *owner,
               glusterfs_fop_t type);

int32_t
gf_del_locker (server_connection_t *conn, const char *volume,
               loc_t *loc,
               fd_t *fd,
               gf_lkowner_t *owner,
               glusterfs_fop_t type);

void
server_print_request (call_frame_t *frame);

call_frame_t *
get_frame_from_request (rpcsvc_request_t *req);

gf_boolean_t
server_cancel_conn_timer (xlator_t *this, server_connection_t *conn);

void
put_server_conn_state (xlator_t *this, rpc_transport_t *xprt);

server_connection_t *
get_server_conn_state (xlator_t *this, rpc_transport_t *xptr);

server_connection_t *
create_server_conn_state (xlator_t *this, rpc_transport_t *xptr);

void
destroy_server_conn_state (server_connection_t *conn);

int
server_build_config (xlator_t *this, server_conf_t *conf);

int serialize_rsp_dirent (gf_dirent_t *entries, gfs3_readdir_rsp *rsp);
int serialize_rsp_direntp (gf_dirent_t *entries, gfs3_readdirp_rsp *rsp);
int readdirp_rsp_cleanup (gfs3_readdirp_rsp *rsp);
int readdir_rsp_cleanup (gfs3_readdir_rsp *rsp);

#endif /* !_SERVER_HELPERS_H */
