/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __MDC_MEM_TYPES_H__
#define __MDC_MEM_TYPES_H__

#include "mem-types.h"

enum gf_mdc_mem_types_ {
        gf_mdc_mt_mdc_local_t   = gf_common_mt_end + 1,
	gf_mdc_mt_md_cache_t,
	gf_mdc_mt_mdc_conf_t,
        gf_mdc_mt_mdc_ipc,
        gf_mdc_mt_end
};
#endif

