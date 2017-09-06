/*
 *   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */


#ifndef __NL_CACHE_MEM_TYPES_H__
#define __NL_CACHE_MEM_TYPES_H__

#include "mem-types.h"

enum gf_nlc_mem_types_ {
        gf_nlc_mt_conf_t = gf_common_mt_end + 1,
        gf_nlc_mt_nlc_conf_t,
        gf_nlc_mt_nlc_ctx_t,
        gf_nlc_mt_nlc_local_t,
        gf_nlc_mt_nlc_pe_t,
        gf_nlc_mt_nlc_ne_t,
        gf_nlc_mt_nlc_timer_data_t,
        gf_nlc_mt_nlc_lru_node,
        gf_nlc_mt_end
};

#endif /* __NL_CACHE_MEM_TYPES_H__ */
