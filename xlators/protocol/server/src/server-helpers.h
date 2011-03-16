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
gf_add_locker (struct _lock_table *table, const char *volume,
               loc_t *loc,
               fd_t *fd,
               pid_t pid,
               uint64_t owner,
               glusterfs_fop_t type);

int32_t
gf_del_locker (struct _lock_table *table, const char *volume,
               loc_t *loc,
               fd_t *fd,
               uint64_t owner,
               glusterfs_fop_t type);

void
server_print_request (call_frame_t *frame);

call_frame_t *
get_frame_from_request (rpcsvc_request_t *req);

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
