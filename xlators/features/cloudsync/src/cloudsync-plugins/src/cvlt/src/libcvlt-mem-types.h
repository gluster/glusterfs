/*
 *   Copyright (c) 2018 Commvault Systems, Inc. <http://www.commvault.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#ifndef __LIBCVLT_MEM_TYPES_H__
#define __LIBCVLT_MEM_TYPES_H__

#include <glusterfs/mem-types.h>
enum libcvlt_mem_types_ {
    gf_libcvlt_mt_cvlt_private_t = gf_common_mt_end + 1,
    gf_libcvlt_mt_end
};
#endif /* __LIBCVLT_MEM_TYPES_H__ */
