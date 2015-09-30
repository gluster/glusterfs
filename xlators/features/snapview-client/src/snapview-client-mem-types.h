/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _SVC_MEM_TYPES_H
#define _SVC_MEM_TYPES_H

#include "mem-types.h"

enum svc_mem_types {
        gf_svc_mt_svc_private_t = gf_common_mt_end + 1,
        gf_svc_mt_svc_local_t,
        gf_svc_mt_svc_inode_t,
        gf_svc_mt_svc_fd_t,
        gf_svc_mt_end
};

#endif
