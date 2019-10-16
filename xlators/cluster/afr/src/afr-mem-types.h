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

#include <glusterfs/mem-types.h>

enum gf_afr_mem_types_ {
    gf_afr_mt_afr_fd_ctx_t = gf_common_mt_end + 1,
    gf_afr_mt_afr_private_t,
    gf_afr_mt_int32_t,
    gf_afr_mt_char,
    gf_afr_mt_xattr_key,
    gf_afr_mt_dict_t,
    gf_afr_mt_xlator_t,
    gf_afr_mt_afr_node_character,
    gf_afr_mt_inode_ctx_t,
    gf_afr_mt_shd_event_t,
    gf_afr_mt_reply_t,
    gf_afr_mt_subvol_healer_t,
    gf_afr_mt_spbc_timeout_t,
    gf_afr_mt_spb_status_t,
    gf_afr_mt_empty_brick_t,
    gf_afr_mt_child_latency_t,
    gf_afr_mt_atomic_t,
    gf_afr_mt_lk_heal_info_t,
    gf_afr_mt_gf_lock,
    gf_afr_mt_end
};
#endif
