/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.
  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __ARBITER_MEM_TYPES_H__
#define __ARBITER_MEM_TYPES_H__
#include "mem-types.h"

typedef enum gf_arbiter_mem_types_ {
        gf_arbiter_mt_inode_ctx_t =  gf_common_mt_end + 1,
        gf_arbiter_mt_end
} gf_arbiter_mem_types_t;
#endif
