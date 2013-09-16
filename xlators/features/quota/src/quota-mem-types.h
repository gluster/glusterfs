/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __QUOTA_MEM_TYPES_H__
#define __QUOTA_MEM_TYPES_H__

#include "mem-types.h"

enum gf_quota_mem_types_ {
        gf_quota_mt_quota_priv_t = gf_common_mt_end + 1,
        gf_quota_mt_quota_inode_ctx_t,
        gf_quota_mt_loc_t,
        gf_quota_mt_char,
        gf_quota_mt_int64_t,
        gf_quota_mt_int32_t,
        gf_quota_mt_limits_t,
        gf_quota_mt_quota_dentry_t,
        gf_quota_mt_quota_limits_level_t,
        gf_quota_mt_qd_vols_conf_t,
        gf_quota_mt_aggregator_state_t,
        gf_quota_mt_end
};
#endif

