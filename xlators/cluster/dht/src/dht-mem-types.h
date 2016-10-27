/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __DHT_MEM_TYPES_H__
#define __DHT_MEM_TYPES_H__

#include "mem-types.h"

enum gf_dht_mem_types_ {
        gf_dht_mt_dht_du_t = gf_common_mt_end + 1,
        gf_dht_mt_dht_conf_t,
        gf_dht_mt_char,
        gf_dht_mt_int32_t,
        gf_dht_mt_xlator_t,
        gf_dht_mt_dht_layout_t,
        gf_switch_mt_dht_conf_t,
        gf_switch_mt_dht_du_t,
        gf_switch_mt_switch_sched_array,
        gf_switch_mt_switch_struct,
        gf_dht_mt_subvol_time,
        gf_dht_mt_loc_t,
        gf_defrag_info_mt,
        gf_dht_mt_inode_ctx_t,
        gf_dht_mt_ctx_stat_time_t,
        gf_dht_mt_dirent_t,
        gf_dht_mt_container_t,
        gf_dht_mt_octx_t,
        gf_dht_mt_miginfo_t,
        gf_tier_mt_bricklist_t,
        gf_tier_mt_ipc_ctr_params_t,
        gf_dht_mt_fd_ctx_t,
        gf_tier_mt_qfile_array_t,
        gf_dht_ret_cache_t,
        gf_dht_mt_end
};
#endif
