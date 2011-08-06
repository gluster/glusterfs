
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


#ifndef __CLIENT_MEM_TYPES_H__
#define __CLIENT_MEM_TYPES_H__

#include "mem-types.h"

enum gf_client_mem_types_ {
        gf_client_mt_dir_entry_t = gf_common_mt_end + 1,
        gf_client_mt_volfile_ctx,
        gf_client_mt_client_state_t,
        gf_client_mt_client_conf_t,
        gf_client_mt_locker,
        gf_client_mt_lock_table,
        gf_client_mt_char,
        gf_client_mt_client_connection_t,
        gf_client_mt_client_fd_ctx_t,
        gf_client_mt_client_local_t,
        gf_client_mt_saved_frames,
        gf_client_mt_saved_frame,
        gf_client_mt_end
};
#endif

