/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __SP_MEM_TYPES_H__
#define __SP_MEM_TYPES_H__

#include "mem-types.h"

enum gf_sp_mem_types_ {
        gf_sp_mt_sp_cache_t   = gf_common_mt_end + 1,
        gf_sp_mt_sp_fd_ctx_t,
        gf_sp_mt_stat,
        gf_sp_mt_sp_local_t,
        gf_sp_mt_sp_inode_ctx_t,
        gf_sp_mt_sp_private_t,
        gf_sp_mt_fd_wrapper_t,
        gf_sp_mt_end
};
#endif
