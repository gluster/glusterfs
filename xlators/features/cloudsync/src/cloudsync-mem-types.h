/*
 *   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __CLOUDSYNC_MEM_TYPES_H__
#define __CLOUDSYNC_MEM_TYPES_H__

#include <glusterfs/mem-types.h>
enum cs_mem_types_ {
    gf_cs_mt_cs_private_t = GF_MEM_TYPE_START,
    gf_cs_mt_cs_remote_stores_t,
    gf_cs_mt_cs_inode_ctx_t,
    gf_cs_mt_cs_lxattr_t,
    gf_cs_mt_end
};
#endif /* __CLOUDSYNC_MEM_TYPES_H__ */
