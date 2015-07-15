/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __CLI_MEM_TYPES_H__
#define __CLI_MEM_TYPES_H__

#include "mem-types.h"

#define CLI_MEM_TYPE_START (gf_common_mt_end + 1)

enum cli_mem_types_ {
        cli_mt_xlator_list_t = CLI_MEM_TYPE_START,
        cli_mt_xlator_t,
        cli_mt_xlator_cmdline_option_t,
        cli_mt_char,
        cli_mt_call_pool_t,
        cli_mt_cli_local_t,
        cli_mt_cli_get_vol_ctx_t,
        cli_mt_append_str,
        cli_mt_cli_cmd,
        cli_mt_end

};

#endif
