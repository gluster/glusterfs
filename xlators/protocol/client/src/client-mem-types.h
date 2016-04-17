/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __CLIENT_MEM_TYPES_H__
#define __CLIENT_MEM_TYPES_H__

#include "mem-types.h"

enum gf_client_mem_types_ {
        gf_client_mt_clnt_conf_t = gf_common_mt_end + 1,
        gf_client_mt_clnt_req_buf_t,
        gf_client_mt_clnt_fdctx_t,
        gf_client_mt_clnt_lock_t,
        gf_client_mt_clnt_fd_lk_local_t,
        gf_client_mt_clnt_args_t,
        gf_client_mt_compound_req_t,
        gf_client_mt_clnt_lock_request_t,
        gf_client_mt_end,
};
#endif /* __CLIENT_MEM_TYPES_H__ */
