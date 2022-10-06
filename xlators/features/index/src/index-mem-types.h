/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __INDEX_MEM_TYPES_H__
#define __INDEX_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_index_mem_types_ {
    gf_index_mt_priv_t = gf_common_mt_end + 1,
    gf_index_inode_ctx_t,
    gf_index_fd_ctx_t,
    gf_index_mt_end
};
#endif
