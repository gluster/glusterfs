/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __GLUSTERD_MEM_TYPES_H__
#define __GLUSTERD_MEM_TYPES_H__

#include "mem-types.h"

enum gf_gld_mem_types_ {
        gf_gld_mt_dir_entry_t = gf_common_mt_end + 1,
        gf_gld_mt_volfile_ctx,
        gf_gld_mt_glusterd_state_t,
        gf_gld_mt_glusterd_conf_t,
        gf_gld_mt_locker,
        gf_gld_mt_lock_table,
        gf_gld_mt_char,
        gf_gld_mt_glusterd_connection_t,
        gf_gld_mt_resolve_comp,
        gf_gld_mt_peerinfo_t,
        gf_gld_mt_friend_sm_event_t,
        gf_gld_mt_friend_req_ctx_t,
        gf_gld_mt_op_sm_event_t,
        gf_gld_mt_op_lock_ctx_t,
        gf_gld_mt_op_stage_ctx_t,
        gf_gld_mt_op_commit_ctx_t,
        gf_gld_mt_mop_stage_req_t,
        gf_gld_mt_probe_ctx_t,
        gf_gld_mt_create_volume_ctx_t,
        gf_gld_mt_glusterd_volinfo_t,
        gf_gld_mt_glusterd_brickinfo_t,
        gf_gld_mt_end
};
#endif

