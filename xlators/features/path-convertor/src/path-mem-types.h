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

#ifndef __PATH_MEM_TYPES_H__
#define __PATH_MEM_TYPES_H__

#include "mem-types.h"

enum gf_path_mem_types_ {
        gf_path_mt_path_private_t = gf_common_mt_end + 1,
        gf_path_mt_char,
        gf_path_mt_regex_t,
        gf_path_mt_end
};
#endif

