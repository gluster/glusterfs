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

#include <glusterfs/mem-types.h>

enum cli_mem_types_ {
    cli_mt_xlator_t = GF_MEM_TYPE_START,
    cli_mt_char,
    cli_mt_call_pool_t,
    cli_mt_cli_local_t,
    cli_mt_append_str,
    cli_mt_cli_cmd,
    cli_mt_end

};

#endif
