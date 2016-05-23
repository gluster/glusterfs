/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __POSIX2_MEM_TYPES_H__
#define __POSIX2_MEM_TYPES_H__

#include "mem-types.h"

enum gf_posix2_mem_types_ {
        gf_posix2_mt_posix2_conf = gf_common_mt_end + 1,
        gf_posix2_mt_end
};

#endif /* __POSIX2_MEM_TYPES_H__ */
