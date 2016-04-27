/*
  Copyright (c) 2008-2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __SOCKET_MEM_TYPES_H__
#define __SOCKET_MEM_TYPES_H__

#include "mem-types.h"

typedef enum gf_sock_mem_types_ {
        gf_sock_connect_error_state_t     = gf_common_mt_end + 1,
        gf_sock_mt_lock_array,
        gf_sock_mt_tid_wrap,
        gf_sock_mt_end
} gf_sock_mem_types_t;

#endif
