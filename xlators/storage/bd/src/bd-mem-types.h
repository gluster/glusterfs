/*
  Copyright (c) 2008-2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __BD_MEM_TYPES_H__
#define __BD_MEM_TYPES_H__

#include "mem-types.h"

enum gf_bd_mem_types_ {
        gf_bd_private  = gf_common_mt_end + 1,
        gf_bd_attr,
        gf_bd_fd,
        gf_bd_loc_t,
        gf_bd_int32_t,
        gf_bd_aio_cb,
        gf_bd_mt_end
};

#endif
