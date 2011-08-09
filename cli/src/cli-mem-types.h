/*
   Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
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
        cli_mt_end

};

#endif
