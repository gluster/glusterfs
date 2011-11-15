/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _XDR_RPCCLNT_H
#define _XDR_RPCCLNT_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

//#include <rpc/rpc.h>
//#include <rpc/pmap_clnt.h>
#include <arpa/inet.h>
#include <rpc/xdr.h>
#include <sys/uio.h>
#include <rpc/rpc_msg.h>
#include <rpc/auth_unix.h>

/* Macros that simplify accessing the members of an RPC call structure. */
#define rpc_reply_xid(reply)              ((reply)->rm_xid)
#define rpc_reply_status(reply)           ((reply)->ru.RM_rmb.rp_stat)
#define rpc_accepted_reply_status(reply)  ((reply)->acpted_rply.ar_stat)
#define rpc_reply_verf_flavour(reply)     ((reply)->acpted_rply.ar_verf.oa_flavor)

int xdr_to_rpc_reply (char *msgbuf, size_t len, struct rpc_msg *reply,
                      struct iovec *payload, char *verfbytes);
int
rpc_request_to_xdr (struct rpc_msg *request, char *dest, size_t len,
                    struct iovec *dst);
int
auth_unix_cred_to_xdr (struct authunix_parms *au, char *dest, size_t len,
                       struct iovec *iov);

#endif
