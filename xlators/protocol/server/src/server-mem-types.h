/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef __SERVER_MEM_TYPES_H__
#define __SERVER_MEM_TYPES_H__

#include "mem-types.h"

enum gf_server_mem_types_ {
        gf_server_mt_server_conf_t = gf_common_mt_end + 1,
        gf_server_mt_resolv_comp_t,
        gf_server_mt_state_t,
        gf_server_mt_locker_t,
        gf_server_mt_lock_table_t,
        gf_server_mt_conn_t,
        gf_server_mt_dirent_rsp_t,
        gf_server_mt_rsp_buf_t,
        gf_server_mt_volfile_ctx_t,
        gf_server_mt_end,
};
#endif /* __SERVER_MEM_TYPES_H__ */
