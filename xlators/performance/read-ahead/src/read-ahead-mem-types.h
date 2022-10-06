/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __RA_MEM_TYPES_H__
#define __RA_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_ra_mem_types_ {
    gf_ra_mt_ra_file_t = GF_MEM_TYPE_START,
    gf_ra_mt_ra_conf_t,
    gf_ra_mt_ra_page_t,
    gf_ra_mt_ra_waitq_t,
    gf_ra_mt_ra_fill_t,
    gf_ra_mt_iovec,
    gf_ra_mt_end
};
#endif
