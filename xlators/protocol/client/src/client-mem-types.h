/*
   Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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
        gf_client_mt_clnt_conf_t = gf_common_mt_end + 1,
        gf_client_mt_clnt_local_t,
        gf_client_mt_clnt_req_buf_t,
        gf_client_mt_clnt_fdctx_t,
        gf_client_mt_clnt_lock_t,
        gf_client_mt_end,
};
#endif /* __CLIENT_MEM_TYPES_H__ */
