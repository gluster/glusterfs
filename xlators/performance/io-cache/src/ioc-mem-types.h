/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __IOC_MT_H__
#define __IOC_MT_H__

#include "mem-types.h"

enum gf_ioc_mem_types_ {
        gf_ioc_mt_iovec  = gf_common_mt_end + 1,
        gf_ioc_mt_ioc_table_t,
        gf_ioc_mt_char,
        gf_ioc_mt_ioc_waitq_t,
        gf_ioc_mt_ioc_priority,
        gf_ioc_mt_list_head,
        gf_ioc_mt_call_pool_t,
        gf_ioc_mt_ioc_inode_t,
        gf_ioc_mt_ioc_fill_t,
        gf_ioc_mt_ioc_newpage_t,
        gf_ioc_mt_end
};
#endif
