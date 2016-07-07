/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CHANGELOG_MEM_TYPES_H
#define _CHANGELOG_MEM_TYPES_H

#include "mem-types.h"

enum gf_changelog_mem_types {
        gf_changelog_mt_priv_t                     = gf_common_mt_end + 1,
        gf_changelog_mt_str_t                      = gf_common_mt_end + 2,
        gf_changelog_mt_batch_t                    = gf_common_mt_end + 3,
        gf_changelog_mt_rt_t                       = gf_common_mt_end + 4,
        gf_changelog_mt_inode_ctx_t                = gf_common_mt_end + 5,
        gf_changelog_mt_rpc_clnt_t                 = gf_common_mt_end + 6,
        gf_changelog_mt_libgfchangelog_t           = gf_common_mt_end + 7,
        gf_changelog_mt_libgfchangelog_entry_t     = gf_common_mt_end + 8,
        gf_changelog_mt_libgfchangelog_rl_t        = gf_common_mt_end + 9,
        gf_changelog_mt_changelog_buffer_t         = gf_common_mt_end + 10,
        gf_changelog_mt_history_data_t             = gf_common_mt_end + 11,
        gf_changelog_mt_libgfchangelog_call_pool_t = gf_common_mt_end + 12,
        gf_changelog_mt_libgfchangelog_event_t     = gf_common_mt_end + 13,
        gf_changelog_mt_ev_dispatcher_t            = gf_common_mt_end + 14,
        gf_changelog_mt_end
};

#endif
