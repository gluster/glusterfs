/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __POSIX_MEM_TYPES_H__
#define __POSIX_MEM_TYPES_H__

#include "mem-types.h"

enum gf_posix_mem_types_ {
        gf_posix_mt_dir_entry_t = gf_common_mt_end + 1,
        gf_posix_mt_posix_fd,
        gf_posix_mt_char,
        gf_posix_mt_posix_private,
        gf_posix_mt_int32_t,
        gf_posix_mt_posix_dev_t,
        gf_posix_mt_trash_path,
	gf_posix_mt_paiocb,
        gf_posix_mt_inode_ctx_t,
        gf_posix_mt_end
};
#endif

