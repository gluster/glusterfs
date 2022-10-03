/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __SELINUX_MEM_TYPES_H__
#define __SELINUX_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_selinux_mem_types_ {
    gf_selinux_mt_selinux_priv_t = GF_MEM_TYPE_START,
    gf_selinux_mt_end
};
#endif
