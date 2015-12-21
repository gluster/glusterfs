/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __FUSE_MEM_TYPES_H__
#define __FUSE_MEM_TYPES_H__

#include "mem-types.h"

enum gf_fuse_mem_types_ {
        gf_fuse_mt_iovec  = gf_common_mt_end + 1,
        gf_fuse_mt_fuse_private_t,
        gf_fuse_mt_char,
        gf_fuse_mt_iov_base,
        gf_fuse_mt_fuse_state_t,
        gf_fuse_mt_fd_ctx_t,
        gf_fuse_mt_graph_switch_args_t,
	gf_fuse_mt_gids_t,
        gf_fuse_mt_invalidate_node_t,
        gf_fuse_mt_end
};
#endif

