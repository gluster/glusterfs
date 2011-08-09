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

#ifndef __QUOTA_MEM_TYPES_H__
#define __QUOTA_MEM_TYPES_H__

#include "mem-types.h"

enum gf_quota_mem_types_ {
        gf_quota_mt_quota_local = gf_common_mt_end + 1,
        gf_quota_mt_quota_priv,
        gf_quota_mt_end
};
#endif

