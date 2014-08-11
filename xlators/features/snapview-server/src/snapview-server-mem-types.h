/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __SNAP_VIEW_MEM_TYPES_H
#define __SNAP_VIEW_MEM_TYPES_H

#include "mem-types.h"

enum snapview_mem_types {
        gf_svs_mt_priv_t = gf_common_mt_end + 1,
        gf_svs_mt_svs_inode_t,
        gf_svs_mt_dirents_t,
        gf_svs_mt_svs_fd_t,
        gf_svs_mt_snaplist_t,
        gf_svs_mt_end
};

#endif

