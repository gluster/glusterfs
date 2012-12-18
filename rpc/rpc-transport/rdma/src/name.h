/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _IB_VERBS_NAME_H
#define _IB_VERBS_NAME_H

#include <rdma/rdma_cma.h>

#include "compat.h"

int32_t
gf_rdma_client_bind (rpc_transport_t *this, struct sockaddr *sockaddr,
                     socklen_t *sockaddr_len, struct rdma_cm_id *cm_id);

int32_t
gf_rdma_client_get_remote_sockaddr (rpc_transport_t *this,
                                    struct sockaddr *sockaddr,
                                    socklen_t *sockaddr_len,
                                    int16_t remote_port);

int32_t
gf_rdma_server_get_local_sockaddr (rpc_transport_t *this,
                                   struct sockaddr *addr,
                                   socklen_t *addr_len);

int32_t
gf_rdma_get_transport_identifiers (rpc_transport_t *this);

#endif /* _IB_VERBS_NAME_H */
