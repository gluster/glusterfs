/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __GLUSTERD_MEM_TYPES_H__
#define __GLUSTERD_MEM_TYPES_H__

#include "mem-types.h"

typedef enum gf_gld_mem_types_ {
        gf_gld_mt_dir_entry_t                   = gf_common_mt_end + 1,
        gf_gld_mt_volfile_ctx                   = gf_common_mt_end + 2,
        gf_gld_mt_glusterd_state_t              = gf_common_mt_end + 3,
        gf_gld_mt_glusterd_conf_t               = gf_common_mt_end + 4,
        gf_gld_mt_locker                        = gf_common_mt_end + 5,
        gf_gld_mt_string                        = gf_common_mt_end + 6,
        gf_gld_mt_lock_table                    = gf_common_mt_end + 7,
        gf_gld_mt_char                          = gf_common_mt_end + 8,
        gf_gld_mt_glusterd_connection_t         = gf_common_mt_end + 9,
        gf_gld_mt_resolve_comp                  = gf_common_mt_end + 10,
        gf_gld_mt_peerinfo_t                    = gf_common_mt_end + 11,
        gf_gld_mt_friend_sm_event_t             = gf_common_mt_end + 12,
        gf_gld_mt_friend_req_ctx_t              = gf_common_mt_end + 13,
        gf_gld_mt_friend_update_ctx_t           = gf_common_mt_end + 14,
        gf_gld_mt_op_sm_event_t                 = gf_common_mt_end + 15,
        gf_gld_mt_op_lock_ctx_t                 = gf_common_mt_end + 16,
        gf_gld_mt_op_stage_ctx_t                = gf_common_mt_end + 17,
        gf_gld_mt_op_commit_ctx_t               = gf_common_mt_end + 18,
        gf_gld_mt_mop_stage_req_t               = gf_common_mt_end + 19,
        gf_gld_mt_probe_ctx_t                   = gf_common_mt_end + 20,
        gf_gld_mt_create_volume_ctx_t           = gf_common_mt_end + 21,
        gf_gld_mt_start_volume_ctx_t            = gf_common_mt_end + 22,
        gf_gld_mt_stop_volume_ctx_t             = gf_common_mt_end + 23,
        gf_gld_mt_delete_volume_ctx_t           = gf_common_mt_end + 24,
        gf_gld_mt_glusterd_volinfo_t            = gf_common_mt_end + 25,
        gf_gld_mt_glusterd_brickinfo_t          = gf_common_mt_end + 26,
        gf_gld_mt_peer_hostname_t               = gf_common_mt_end + 27,
        gf_gld_mt_ifreq                         = gf_common_mt_end + 28,
        gf_gld_mt_store_handle_t                = gf_common_mt_end + 29,
        gf_gld_mt_store_iter_t                  = gf_common_mt_end + 30,
        gf_gld_mt_defrag_info                   = gf_common_mt_end + 31,
        gf_gld_mt_log_filename_ctx_t            = gf_common_mt_end + 32,
        gf_gld_mt_log_locate_ctx_t              = gf_common_mt_end + 33,
        gf_gld_mt_log_rotate_ctx_t              = gf_common_mt_end + 34,
        gf_gld_mt_peerctx_t                     = gf_common_mt_end + 35,
        gf_gld_mt_sm_tr_log_t                   = gf_common_mt_end + 36,
        gf_gld_mt_pending_node_t                = gf_common_mt_end + 37,
        gf_gld_mt_brick_rsp_ctx_t               = gf_common_mt_end + 38,
        gf_gld_mt_mop_brick_req_t               = gf_common_mt_end + 39,
        gf_gld_mt_op_allack_ctx_t               = gf_common_mt_end + 40,
        gf_gld_mt_linearr                       = gf_common_mt_end + 41,
        gf_gld_mt_linebuf                       = gf_common_mt_end + 42,
        gf_gld_mt_mount_pattern                 = gf_common_mt_end + 43,
        gf_gld_mt_mount_comp_container          = gf_common_mt_end + 44,
        gf_gld_mt_mount_component               = gf_common_mt_end + 45,
        gf_gld_mt_mount_spec                    = gf_common_mt_end + 46,
        gf_gld_mt_georep_meet_spec              = gf_common_mt_end + 47,
        gf_gld_mt_nodesrv_t                     = gf_common_mt_end + 48,
        gf_gld_mt_charptr                       = gf_common_mt_end + 49,
        gf_gld_mt_hooks_stub_t                  = gf_common_mt_end + 50,
        gf_gld_mt_hooks_priv_t                  = gf_common_mt_end + 51,
        gf_gld_mt_mop_commit_req_t              = gf_common_mt_end + 52,
        gf_gld_mt_int                           = gf_common_mt_end + 53,
        gf_gld_mt_snap_t                        = gf_common_mt_end + 54,
        gf_gld_mt_missed_snapinfo_t             = gf_common_mt_end + 55,
        gf_gld_mt_snap_create_args_t            = gf_common_mt_end + 56,
        gf_gld_mt_local_peers_t                 = gf_common_mt_end + 57,
        gf_gld_mt_end                           = gf_common_mt_end + 58,
} gf_gld_mem_types_t;
#endif

