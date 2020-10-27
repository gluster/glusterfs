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

#include <glusterfs/mem-types.h>

typedef enum gf_gld_mem_types_ {
    gf_gld_mt_glusterd_conf_t = gf_common_mt_end + 1,
    gf_gld_mt_char,
    gf_gld_mt_peerinfo_t,
    gf_gld_mt_friend_sm_event_t,
    gf_gld_mt_friend_req_ctx_t,
    gf_gld_mt_friend_update_ctx_t,
    gf_gld_mt_op_sm_event_t,
    gf_gld_mt_op_lock_ctx_t,
    gf_gld_mt_op_stage_ctx_t,
    gf_gld_mt_op_commit_ctx_t,
    gf_gld_mt_mop_stage_req_t,
    gf_gld_mt_probe_ctx_t,
    gf_gld_mt_glusterd_volinfo_t,
    gf_gld_mt_volinfo_dict_data_t,
    gf_gld_mt_glusterd_brickinfo_t,
    gf_gld_mt_peer_hostname_t,
    gf_gld_mt_defrag_info,
    gf_gld_mt_peerctx_t,
    gf_gld_mt_sm_tr_log_t,
    gf_gld_mt_pending_node_t,
    gf_gld_mt_brick_rsp_ctx_t,
    gf_gld_mt_mop_brick_req_t,
    gf_gld_mt_op_allack_ctx_t,
    gf_gld_mt_linearr,
    gf_gld_mt_linebuf,
    gf_gld_mt_mount_pattern,
    gf_gld_mt_mount_comp_container,
    gf_gld_mt_mount_spec,
    gf_gld_mt_georep_meet_spec,
    gf_gld_mt_charptr,
    gf_gld_mt_hooks_stub_t,
    gf_gld_mt_hooks_priv_t,
    gf_gld_mt_mop_commit_req_t,
    gf_gld_mt_int,
    gf_gld_mt_snap_t,
    gf_gld_mt_missed_snapinfo_t,
    gf_gld_mt_snap_create_args_t,
    gf_gld_mt_glusterd_brick_proc_t,
    gf_gld_mt_glusterd_svc_proc_t,
    gf_gld_mt_hostname_t,
    gf_gld_mt_end,
} gf_gld_mem_types_t;
#endif
