/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __TRASH_MEM_TYPES_H__
#define __TRASH_MEM_TYPES_H__

#include "mem-types.h"

enum gf_trash_mem_types_ {
        gf_trash_mt_trash_private_t = gf_common_mt_end + 1,
        gf_trash_mt_char,
        gf_trash_mt_uuid,
        gf_trash_mt_trash_elim_path,
        gf_trash_mt_end
};
#endif

