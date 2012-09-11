/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __MAP_MEM_TYPES_H__
#define __MAP_MEM_TYPES_H__

#include "mem-types.h"

enum gf_map_mem_types_ {
        gf_map_mt_map_private_t = gf_common_mt_end + 1,
        gf_map_mt_map_local_t,
        gf_map_mt_map_xlator_array,
        gf_map_mt_map_pattern,
        gf_map_mt_end
};
#endif

