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
#include "defaults.h"

#define CALL_STATE(frame)   ((server_state_t *)frame->root->state)

#define XPRT_FROM_FRAME(frame) ((rpc_transport_t *) CALL_STATE(frame)->xprt)

#define SERVER_CONF(frame)                                              \
        ((server_conf_t *)XPRT_FROM_FRAME(frame)->this->private)

#define XPRT_FROM_XLATOR(this) ((((server_conf_t *)this->private))->listen)

#define INODE_LRU_LIMIT(this)                                           \
        (((server_conf_t *)(this->private))->config.inode_lru_limit)

#define IS_ROOT_INODE(inode) (inode == inode->table->root)

#define IS_NOT_ROOT(pathlen) ((pathlen > 2)? 1 : 0)

void free_state (server_state_t *state);

void server_loc_wipe (loc_t *loc);

void
server_print_request (call_frame_t *frame);

call_frame_t *
get_frame_from_request (rpcsvc_request_t *req);

int
server_connection_cleanup (xlator_t *this, struct _client_t *client,
                           int32_t flags);

gf_boolean_t
server_cancel_grace_timer (xlator_t *this, struct _client_t *client);

int
server_build_config (xlator_t *this, server_conf_t *conf);

int serialize_rsp_dirent (gf_dirent_t *entries, gfs3_readdir_rsp *rsp);
int serialize_rsp_direntp (gf_dirent_t *entries, gfs3_readdirp_rsp *rsp);
int readdirp_rsp_cleanup (gfs3_readdirp_rsp *rsp);
int readdir_rsp_cleanup (gfs3_readdir_rsp *rsp);
int auth_set_username_passwd (dict_t *input_params, dict_t *config_params,
                              struct _client_t *client);

server_ctx_t *server_ctx_get (client_t *client, xlator_t *xlator);
int server_process_event_upcall (xlator_t *this, void *data);

inode_t *
server_inode_new (inode_table_t *itable, uuid_t gfid);

int
serialize_rsp_locklist (lock_migration_info_t *locklist,
                                                 gfs3_getactivelk_rsp *rsp);

int
getactivelkinfo_rsp_cleanup (gfs3_getactivelk_rsp  *rsp);

int
server_populate_compound_response (xlator_t *this, gfs3_compound_rsp *rsp,
                                   call_frame_t *frame,
                                   compound_args_cbk_t *args_cbk, int index);
int
server_get_compound_resolve (server_state_t *state, gfs3_compound_req *req);

int
server_populate_compound_request (gfs3_compound_req *req, call_frame_t *frame,
                                  default_args_t *this_args,
                                  int index);
#endif /* !_SERVER_HELPERS_H */
