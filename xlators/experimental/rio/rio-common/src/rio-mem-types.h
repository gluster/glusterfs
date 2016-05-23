/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __RIO_MEM_TYPES_H__
#define __RIO_MEM_TYPES_H__

#include "mem-types.h"

enum gf_rio_mem_types_ {
        gf_rio_mt_rio_conf = gf_common_mt_end + 1,
        gf_rio_mt_rio_subvol,
        gf_rio_mt_layout,
        gf_rio_mt_layout_static_bucket,
        gf_rio_mt_layout_inodehash_bucket,
        gf_rio_mt_end
};

#endif /* __RIO_MEM_TYPES_H__ */
