/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __META_MEM_TYPES_H__
#define __META_MEM_TYPES_H__

#include "mem-types.h"

enum gf_meta_mem_types_ {
        gf_meta_mt__open_local = gf_common_mt_end + 1,
        gf_meta_mt_dir_entry_t,
        gf_meta_mt_meta_dirent_t,
        gf_meta_mt_meta_private_t,
        gf_meta_mt_stat,
        gf_meta_mt_end
};
#endif

