/*
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#ifndef __GLUSTERFSD_MEM_TYPES_H__
#define __GLUSTERFSD_MEM_TYPES_H__

#include "mem-types.h"

#define GF_MEM_TYPE_START (gf_common_mt_end + 1)

enum gfd_mem_types_ {
        gfd_mt_xlator_list_t = GF_MEM_TYPE_START,
        gfd_mt_xlator_t,
        gfd_mt_server_cmdline_t,
        gfd_mt_xlator_cmdline_option_t,
        gfd_mt_char,
        gfd_mt_call_pool_t,
        gfd_mt_end

};
#endif
