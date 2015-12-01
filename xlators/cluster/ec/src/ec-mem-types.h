/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
    ec_mt_subvol_healer_t,
    ec_mt_ec_gf_t,
    ec_mt_ec_code_t,
    ec_mt_ec_code_builder_t,
    ec_mt_ec_matrix_t,
    ec_mt_end
};

#endif /* __EC_MEM_TYPES_H__ */
