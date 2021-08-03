/*
 *   Copyright (c) 2021 Pavilion Data Systems, Inc. <http://www.pavilion.io>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __TIER_MEM_TYPES_H__
#define __TIER_MEM_TYPES_H__

#include <glusterfs/mem-types.h>
enum tier_mem_types_ {
    gf_tier_mt_private_t = gf_common_mt_end + 1,
    gf_tier_mt_remote_stores_t,
    gf_tier_mt_inode_ctx_t,
    gf_tier_mt_lxattr_t,
    gf_filesystem_mt_private_t,
    gf_tier_mt_end
};
#endif /* __TIER_MEM_TYPES_H__ */
