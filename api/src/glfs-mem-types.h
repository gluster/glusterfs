/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_MEM_TYPES_H
#define _GLFS_MEM_TYPES_H

#include "mem-types.h"

#define GF_MEM_TYPE_START (gf_common_mt_end + 1)

enum glfs_mem_types_ {
        glfs_mt_call_pool_t = GF_MEM_TYPE_START,
        glfs_mt_xlator_t,
	glfs_mt_glfs_fd_t,
	glfs_mt_glfs_io_t,
	glfs_mt_volfile_t,
	glfs_mt_xlator_cmdline_option_t,
        glfs_mt_server_cmdline_t,
	glfs_mt_glfs_object_t,
	glfs_mt_readdirbuf_t,
        glfs_mt_upcall_entry_t,
	glfs_mt_acl_t,
        glfs_mt_upcall_inode_t,
        glfs_mt_realpath_t,
	glfs_mt_end
};
#endif
