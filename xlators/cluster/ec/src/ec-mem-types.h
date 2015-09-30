/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_MEM_TYPES_H__
#define __EC_MEM_TYPES_H__

#include "mem-types.h"

enum gf_ec_mem_types_
{
    ec_mt_ec_t = gf_common_mt_end + 1,
    ec_mt_xlator_t,
    ec_mt_ec_inode_t,
    ec_mt_ec_fd_t,
    ec_mt_ec_heal_t,
    ec_mt_end
};

#endif /* __EC_MEM_TYPES_H__ */
