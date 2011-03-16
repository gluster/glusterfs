/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __IOC_MT_H__
#define __IOC_MT_H__

#include "mem-types.h"

enum gf_ioc_mem_types_ {
        gf_ioc_mt_iovec  = gf_common_mt_end + 1,
        gf_ioc_mt_ioc_table_t,
        gf_ioc_mt_char,
        gf_ioc_mt_ioc_local_t,
        gf_ioc_mt_ioc_waitq_t,
        gf_ioc_mt_ioc_priority,
        gf_ioc_mt_list_head,
        gf_ioc_mt_call_pool_t,
        gf_ioc_mt_ioc_inode_t,
        gf_ioc_mt_ioc_fill_t,
        gf_ioc_mt_ioc_newpage_t,
        gf_ioc_mt_end
};
#endif
