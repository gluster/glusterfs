/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CLI1_H
#define _CLI1_H

#include <sys/uio.h>

#include "cli1-xdr.h"

enum gf_cli_defrag_type {
        GF_DEFRAG_CMD_START = 1,
        GF_DEFRAG_CMD_STOP,
        GF_DEFRAG_CMD_STATUS,
        GF_DEFRAG_CMD_START_LAYOUT_FIX,
        GF_DEFRAG_CMD_START_MIGRATE_DATA,
};

ssize_t
gf_xdr_serialize_cli_probe_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_probe_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_probe_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_probe_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_deprobe_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_deprobe_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_deprobe_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_deprobe_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_peer_list_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_peer_list_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_peer_list_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_peer_list_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_create_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_create_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_create_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_create_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_delete_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_delete_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_delete_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_delete_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_start_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_start_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_start_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_start_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_stop_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_stop_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_stop_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_stop_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_rename_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_rename_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_rename_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_rename_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_defrag_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_defrag_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_defrag_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_to_cli_defrag_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_serialize_cli_defrag_vol_rsp_v2 (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_defrag_vol_rsp_v2 (struct iovec inmsg, void *args);

ssize_t
gf_xdr_serialize_cli_add_brick_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_add_brick_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_add_brick_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_add_brick_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_remove_brick_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_remove_brick_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_remove_brick_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_remove_brick_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_replace_brick_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_replace_brick_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_replace_brick_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_replace_brick_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_reset_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_reset_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_reset_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_reset_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_gsync_set_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_gsync_set_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_gsync_set_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_gsync_set_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_set_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_set_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_set_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_set_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_get_vol_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_get_vol_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_get_vol_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_get_vol_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_log_filename_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_log_filename_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_log_filename_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_log_filename_req (struct iovec outmsg, void *req);


ssize_t
gf_xdr_serialize_cli_log_locate_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_log_locate_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_log_locate_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_log_locate_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_serialize_cli_log_rotate_rsp (struct iovec outmsg, void *rsp);

ssize_t
gf_xdr_to_cli_log_rotate_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_to_cli_log_rotate_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_log_rotate_req (struct iovec outmsg, void *req);

ssize_t
gf_xdr_to_cli_sync_volume_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_sync_volume_req (struct iovec outmsg, void *args);

ssize_t
gf_xdr_to_cli_sync_volume_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_sync_volume_rsp (struct iovec outmsg, void *args);

ssize_t
gf_xdr_to_cli_fsm_log_req (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_fsm_log_req (struct iovec outmsg, void *args);

ssize_t
gf_xdr_to_cli_fsm_log_rsp (struct iovec inmsg, void *args);

ssize_t
gf_xdr_from_cli_fsm_log_rsp (struct iovec outmsg, void *args);
#endif /* !_CLI1_H */
