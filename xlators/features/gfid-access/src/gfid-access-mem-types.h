/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GFID_ACCESS_MEM_TYPES_H
#define _GFID_ACCESS_MEM_TYPES_H

#include "mem-types.h"

enum gf_changelog_mem_types {
        gf_gfid_access_mt_priv_t = gf_common_mt_end + 1,
        gf_gfid_access_mt_gfid_t,
        gf_gfid_access_mt_end
};

#endif

