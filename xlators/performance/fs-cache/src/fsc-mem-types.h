/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __FSC_MT_H__
#define __FSC_MT_H__

#include <glusterfs/mem-types.h>

enum gf_fsc_mem_types_ {
  gf_fsc_mt_flag = gf_common_mt_end + 1,
  gf_fsc_mt_fsc_inode_t,
  gf_fsc_mt_fsc_path_t,
  gf_fsc_mt_fsc_posix_page_aligned_t,
  gf_fsc_mt_fsc_block_t,
  gf_fsc_mt_fsc_block_dump_t,
  gf_fsc_mt_end
};
#endif
