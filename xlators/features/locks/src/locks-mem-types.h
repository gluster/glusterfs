/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __LOCKS_MEM_TYPES_H__
#define __LOCKS_MEM_TYPES_H__

#include "mem-types.h"

enum gf_locks_mem_types_ {
        gf_locks_mt_pl_dom_list_t = gf_common_mt_end + 1,
        gf_locks_mt_pl_inode_t,
        gf_locks_mt_posix_lock_t,
        gf_locks_mt_pl_entry_lock_t,
        gf_locks_mt_pl_inode_lock_t,
        gf_locks_mt_truncate_ops,
        gf_locks_mt_pl_rw_req_t,
        gf_locks_mt_posix_locks_private_t,
        gf_locks_mt_pl_fdctx_t,
        gf_locks_mt_pl_meta_lock_t,
        gf_locks_mt_end
};
#endif

