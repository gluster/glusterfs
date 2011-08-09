/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __POSIX_MEM_TYPES_H__
#define __POSIX_MEM_TYPES_H__

#include "mem-types.h"

enum gf_bdb_mem_types_ {
        gf_bdb_mt_bctx_t = gf_common_mt_end + 1,
        gf_bdb_mt_bdb_fd,
        gf_bdb_mt_dir_entry_t,
        gf_bdb_mt_char,
        gf_bdb_mt_dir_entry_t,
        gf_bdb_mt_char,
        gf_bdb_mt_bdb_private,
        gf_bdb_mt_uint32_t,
        gf_bdb_mt_char,
        gf_bdb_mt_bdb_cache_t,
        gf_bdb_mt_char,
        gf_bdb_mt_bctx_table_t,
        gf_bdb_mt_list_head,
        gf_bdb_mt_end,
};
#endif
