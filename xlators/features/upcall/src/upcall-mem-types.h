/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __UPCALL_MEM_TYPES_H__
#define __UPCALL_MEM_TYPES_H__

#include "mem-types.h"

enum gf_upcall_mem_types_ {
        gf_upcall_mt_conf_t = gf_common_mt_end + 1,
        gf_upcall_mt_private_t,
        gf_upcall_mt_upcall_inode_ctx_t,
        gf_upcall_mt_upcall_client_entry_t,
        gf_upcall_mt_end
};
#endif

