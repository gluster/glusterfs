/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __SERVER_MEM_TYPES_H__
#define __SERVER_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_server_mem_types_ {
    gf_server_mt_server_conf_t = gf_common_mt_end + 1,
    gf_server_mt_state_t,
    gf_server_mt_dirent_rsp_t,
    gf_server_mt_setvolume_rsp_t,
    gf_server_mt_lock_mig_t,
    gf_server_mt_child_status,
    gf_server_mt_end,
};
#endif /* __SERVER_MEM_TYPES_H__ */
