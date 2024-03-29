/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __SHARD_MEM_TYPES_H__
#define __SHARD_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_shard_mem_types_ {
    gf_shard_mt_priv_t = gf_common_mt_end + 1,
    gf_shard_mt_inode_list,
    gf_shard_mt_inode_ctx_t,
    gf_shard_mt_int64_t,
    gf_shard_mt_uint64_t,
    gf_shard_mt_end
};
#endif
