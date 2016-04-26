/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

struct pmap_port_by_brick_req {
       string   brick<>;
};

struct pmap_port_by_brick_rsp {
       int      op_ret;
       int      op_errno;
       int      status;
       int      port;
};


struct pmap_brick_by_port_req {
       int      port;
};

struct pmap_brick_by_port_rsp {
       int      op_ret;
       int      op_errno;
       int      status;
       string   brick<>;
};


struct pmap_signin_req {
       string brick<>;
       int port;
};

struct pmap_signin_rsp {
       int      op_ret;
       int      op_errno;
};

struct pmap_signout_req {
       string brick<>;
       int port;
       int rdma_port;
};

struct pmap_signout_rsp {
       int      op_ret;
       int      op_errno;
};
