/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _BR_MEM_TYPES_H
#define _BR_MEM_TYPES_H

#include "mem-types.h"

enum br_mem_types {
        gf_br_stub_mt_private_t   = gf_common_mt_end + 1,
        gf_br_stub_mt_version_t   = gf_common_mt_end + 2,
        gf_br_stub_mt_inode_ctx_t = gf_common_mt_end + 3,
        gf_br_stub_mt_signature_t = gf_common_mt_end + 4,
        gf_br_stub_mt_end
};

#endif
