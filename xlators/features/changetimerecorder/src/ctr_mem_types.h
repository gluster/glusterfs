/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __CTR_MEM_TYPES_H__
#define __CTR_MEM_TYPES_H__

#include "gfdb_mem-types.h"

enum gf_ctr_mem_types_ {
        gf_ctr_mt_private_t = gfdb_mt_end + 1,
        gf_ctr_mt_xlator_ctx,
        gf_ctr_mt_hard_link_t,
        gf_ctr_mt_end
};
#endif

