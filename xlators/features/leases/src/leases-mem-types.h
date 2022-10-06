/*
   Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __LEASES_MEM_TYPES_H__
#define __LEASES_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_leases_mem_types_ {
    gf_leases_mt_private_t = GF_MEM_TYPE_START,
    gf_leases_mt_lease_client_t,
    gf_leases_mt_lease_inode_t,
    gf_leases_mt_fd_ctx_t,
    gf_leases_mt_lease_inode_ctx_t,
    gf_leases_mt_lease_id_entry_t,
    gf_leases_mt_fop_stub_t,
    gf_leases_mt_timer_data_t,
    gf_leases_mt_end
};
#endif
