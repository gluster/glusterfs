/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __AFR_MEM_TYPES_H__
#define __AFR_MEM_TYPES_H__

#include "mem-types.h"

enum gf_afr_mem_types_ {
        gf_afr_mt_iovec  = gf_common_mt_end + 1,
        gf_afr_mt_afr_fd_ctx_t,
        gf_afr_mt_afr_private_t,
        gf_afr_mt_int32_t,
        gf_afr_mt_char,
        gf_afr_mt_xattr_key,
        gf_afr_mt_dict_t,
        gf_afr_mt_xlator_t,
        gf_afr_mt_iatt,
        gf_afr_mt_int,
        gf_afr_mt_afr_node_character,
        gf_afr_mt_sh_diff_loop_state,
        gf_afr_mt_uint8_t,
        gf_afr_mt_loc_t,
        gf_afr_mt_entry_name,
        gf_afr_mt_pump_priv,
        gf_afr_mt_locked_fd,
        gf_afr_mt_inode_ctx_t,
        gf_afr_fd_paused_call_t,
        gf_afr_mt_crawl_data_t,
        gf_afr_mt_brick_pos_t,
        gf_afr_mt_shd_bool_t,
        gf_afr_mt_shd_timer_t,
        gf_afr_mt_shd_event_t,
        gf_afr_mt_time_t,
        gf_afr_mt_pos_data_t,
	gf_afr_mt_reply_t,
	gf_afr_mt_subvol_healer_t,
	gf_afr_mt_spbc_timeout_t,
        gf_afr_mt_spb_status_t,
        gf_afr_mt_empty_brick_t,
        gf_afr_mt_end
};
#endif

