/*
   Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __AFR_MEM_TYPES_H__
#define __AFR_MEM_TYPES_H__

#include "mem-types.h"

enum gf_afr_mem_types_ {
        gf_afr_mt_iovec  = gf_common_mt_end + 1,
        gf_afr_mt_afr_fd_ctx_t,
        gf_afr_mt_afr_local_t,
        gf_afr_mt_afr_private_t,
        gf_afr_mt_int32_t,
        gf_afr_mt_char,
        gf_afr_mt_xattr_key,
        gf_afr_mt_dict_t,
        gf_afr_mt_xlator_t,
        gf_afr_mt_iatt,
        gf_afr_mt_int,
        gf_afr_mt_afr_node_character,
        gf_afr_mt_sh_diff_loop_state,
        gf_afr_mt_uint8_t,
        gf_afr_mt_loc_t,
        gf_afr_mt_entry_name,
        gf_afr_mt_pump_priv,
        gf_afr_mt_locked_fd,
        gf_afr_mt_end
};
#endif

