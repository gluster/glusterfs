/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XDR_RPC_H_
#define _XDR_RPC_H_

#ifndef GF_SOLARIS_HOST_OS
#include <rpc/rpc.h>
#endif

#ifdef GF_SOLARIS_HOST_OS
#include <rpc/auth.h>
#include <rpc/auth_sys.h>
#endif

//#include <rpc/pmap_clnt.h>
#include <arpa/inet.h>
#include <rpc/xdr.h>
#include <sys/uio.h>

#include "xdr-common.h"

typedef enum {
        AUTH_GLUSTERFS = 5,
        AUTH_GLUSTERFS_v2 = 390039, /* using a number from  'unused' range,
                                       from the list available in RFC5531 */
} gf_rpc_authtype_t;

/* Converts a given network buffer from its XDR format to a structure
 * that contains everything an RPC call needs to work.
 */
extern int
xdr_to_rpc_call (char *msgbuf, size_t len, struct rpc_msg *call,
                 struct iovec *payload, char *credbytes, char *verfbytes);

extern int
rpc_fill_empty_reply (struct rpc_msg *reply, uint32_t xid);

extern int
rpc_fill_denied_reply (struct rpc_msg *reply, int rjstat, int auth_err);

extern int
rpc_fill_accepted_reply (struct rpc_msg *reply, int arstat, int proglow,
                         int proghigh, int verf, int len, char *vdata);
extern int
rpc_reply_to_xdr (struct rpc_msg *reply, char *dest, size_t len,
                  struct iovec *dst);

extern int
xdr_to_auth_unix_cred (char *msgbuf, int msglen, struct authunix_parms *au,
                       char *machname, gid_t *gids);
/* Macros that simplify accessing the members of an RPC call structure. */
#define rpc_call_xid(call)              ((call)->rm_xid)
#define rpc_call_direction(call)        ((call)->rm_direction)
#define rpc_call_rpcvers(call)          ((call)->ru.RM_cmb.cb_rpcvers)
#define rpc_call_program(call)          ((call)->ru.RM_cmb.cb_prog)
#define rpc_call_progver(call)          ((call)->ru.RM_cmb.cb_vers)
#define rpc_call_progproc(call)         ((call)->ru.RM_cmb.cb_proc)
#define rpc_opaque_auth_flavour(oa)     ((oa)->oa_flavor)
#define rpc_opaque_auth_len(oa)         ((oa)->oa_length)

#define rpc_call_cred_flavour(call)     (rpc_opaque_auth_flavour ((&(call)->ru.RM_cmb.cb_cred)))
#define rpc_call_cred_len(call)         (rpc_opaque_auth_len ((&(call)->ru.RM_cmb.cb_cred)))


#define rpc_call_verf_flavour(call)     (rpc_opaque_auth_flavour ((&(call)->ru.RM_cmb.cb_verf)))
#define rpc_call_verf_len(call)         (rpc_opaque_auth_len ((&(call)->ru.RM_cmb.cb_verf)))


#ifdef GF_DARWIN_HOST_OS
#define GF_PRI_RPC_XID          PRIu32
#define GF_PRI_RPC_VERSION      PRIu32
#define GF_PRI_RPC_PROG_ID      PRIu32
#define GF_PRI_RPC_PROG_VERS    PRIu32
#define GF_PRI_RPC_PROC         PRIu32
#define GF_PRI_RPC_PROC_VERSION PRIu32
#else
#define GF_PRI_RPC_XID          PRIu64
#define GF_PRI_RPC_VERSION      PRIu64
#define GF_PRI_RPC_PROG_ID      PRIu64
#define GF_PRI_RPC_PROG_VERS    PRIu64
#define GF_PRI_RPC_PROC         PRIu64
#define GF_PRI_RPC_PROC_VERSION PRIu64
#endif

#endif
