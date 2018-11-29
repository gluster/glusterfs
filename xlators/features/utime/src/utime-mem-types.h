/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __UTIME_MEM_TYPES_H__
#define __UTIME_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_utime_mem_types_ {
    utime_mt_utime_t = gf_common_mt_end + 1,
    utime_mt_end
};

#endif /* __UTIME_MEM_TYPES_H__ */
