/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XDR_RPCCLNT_H
#define _XDR_RPCCLNT_H

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
