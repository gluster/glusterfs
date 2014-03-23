/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __CDC_MEM_TYPES_H
#define __CDC_MEM_TYPES_H

#include "mem-types.h"

enum gf_cdc_mem_types {
        gf_cdc_mt_priv_t         = gf_common_mt_end + 1,
        gf_cdc_mt_vec_t          = gf_common_mt_end + 2,
        gf_cdc_mt_gzip_trailer_t = gf_common_mt_end + 3,
        gf_cdc_mt_end            = gf_common_mt_end + 4,
};

#endif
