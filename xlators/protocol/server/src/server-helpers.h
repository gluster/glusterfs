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

#include <glusterfs/defaults.h>

#define CALL_STATE(frame) ((server_state_t *)frame->root->state)

void
free_state(server_state_t *state);

void
server_print_request(call_frame_t *frame);

call_frame_t *
get_frame_from_request(rpcsvc_request_t *req);

int
server_connection_cleanup(xlator_t *this, struct _client *client, int32_t flags,
                          gf_boolean_t *fd_exist);

int
server_build_config(xlator_t *this, server_conf_t *conf);

int
readdirp_rsp_cleanup_v2(gfx_readdirp_rsp *rsp);
int
readdir_rsp_cleanup_v2(gfx_readdir_rsp *rsp);
int
auth_set_username_passwd(dict_t *input_params, dict_t *config_params,
                         struct _client *client);

server_ctx_t *
server_ctx_get(client_t *client, xlator_t *xlator);
int
server_process_event_upcall(xlator_t *this, void *data);

inode_t *
server_inode_new(inode_table_t *itable, uuid_t gfid);

int
serialize_rsp_locklist_v2(lock_migration_info_t *locklist,
                          gfx_getactivelk_rsp *rsp);

int
getactivelkinfo_rsp_cleanup_v2(gfx_getactivelk_rsp *rsp);

int
unserialize_req_locklist_v2(gfx_setactivelk_req *req,
                            lock_migration_info_t *lmi);

int
serialize_rsp_dirent_v2(gf_dirent_t *entries, gfx_readdir_rsp *rsp);

int
serialize_rsp_direntp_v2(gf_dirent_t *entries, gfx_readdirp_rsp *rsp);

#endif /* !_SERVER_HELPERS_H */
