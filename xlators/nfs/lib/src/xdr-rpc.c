/*
  Copyright (c) 2010-2011-2011-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <arpa/inet.h>
#include <rpc/xdr.h>
#include <sys/uio.h>
#include <rpc/auth_unix.h>

#include "mem-pool.h"
#include "xdr-rpc.h"
#include "xdr-common.h"
#include "logging.h"

/* Decodes the XDR format in msgbuf into rpc_msg.
 * The remaining payload is returned into payload.
 */
int
nfs_xdr_to_rpc_call (char *msgbuf, size_t len, struct rpc_msg *call,
                     struct iovec *payload, char *credbytes, char *verfbytes)
{
        XDR                     xdr;
        char                    opaquebytes[MAX_AUTH_BYTES];
        struct opaque_auth      *oa = NULL;

        if ((!msgbuf) || (!call))
                return -1;

        memset (call, 0, sizeof (*call));

        oa = &call->rm_call.cb_cred;
        if (!credbytes)
                oa->oa_base = opaquebytes;
        else
                oa->oa_base = credbytes;

        oa = &call->rm_call.cb_verf;
        if (!verfbytes)
                oa->oa_base = opaquebytes;
        else
                oa->oa_base = verfbytes;

        xdrmem_create (&xdr, msgbuf, len, XDR_DECODE);
        if (!xdr_callmsg (&xdr, call))
                return -1;

        if (payload) {
                payload->iov_base = nfs_xdr_decoded_remaining_addr (xdr);
                payload->iov_len = nfs_xdr_decoded_remaining_len (xdr);
        }

        return 0;
}


bool_t
nfs_true_func (XDR *s, caddr_t *a)
{
        return TRUE;
}


int
nfs_rpc_fill_empty_reply (struct rpc_msg *reply, uint32_t xid)
{
        if (!reply)
                return -1;

        /* Setting to 0 also results in reply verifier flavor to be
         * set to AUTH_NULL which is what we want right now.
         */
        memset (reply, 0, sizeof (*reply));
        reply->rm_xid = xid;
        reply->rm_direction = REPLY;

        return 0;
}

int
nfs_rpc_fill_denied_reply (struct rpc_msg *reply, int rjstat, int auth_err)
{
        if (!reply)
                return -1;

        reply->rm_reply.rp_stat = MSG_DENIED;
        reply->rjcted_rply.rj_stat = rjstat;
        if (rjstat == RPC_MISMATCH) {
                /* No problem with hardocoding
                 * RPC version numbers. We only support
                 * v2 anyway.
                 */
                reply->rjcted_rply.rj_vers.low = 2;
                reply->rjcted_rply.rj_vers.high = 2;
        } else if (rjstat == AUTH_ERROR)
                reply->rjcted_rply.rj_why = auth_err;

        return 0;
}


int
nfs_rpc_fill_accepted_reply (struct rpc_msg *reply, int arstat, int proglow,
                             int proghigh, int verf, int len, char *vdata)
{
        if (!reply)
                return -1;

        reply->rm_reply.rp_stat = MSG_ACCEPTED;
        reply->acpted_rply.ar_stat = arstat;

        reply->acpted_rply.ar_verf.oa_flavor = verf;
        reply->acpted_rply.ar_verf.oa_length = len;
        reply->acpted_rply.ar_verf.oa_base = vdata;
        if (arstat == PROG_MISMATCH) {
                reply->acpted_rply.ar_vers.low = proglow;
                reply->acpted_rply.ar_vers.high = proghigh;
        } else if (arstat == SUCCESS) {

                /* This is a hack. I'd really like to build a custom
                 * XDR library because Sun RPC interface is not very flexible.
                 */
                reply->acpted_rply.ar_results.proc = (xdrproc_t)nfs_true_func;
                reply->acpted_rply.ar_results.where = NULL;
        }

        return 0;
}

int
nfs_rpc_reply_to_xdr (struct rpc_msg *reply, char *dest, size_t len,
                      struct iovec *dst)
{
        XDR             xdr;

        if ((!dest) || (!reply) || (!dst))
                return -1;

        xdrmem_create (&xdr, dest, len, XDR_ENCODE);
        if (!xdr_replymsg(&xdr, reply))
                return -1;

        dst->iov_base = dest;
        dst->iov_len = nfs_xdr_encoded_length (xdr);

        return 0;
}


int
nfs_xdr_to_auth_unix_cred (char *msgbuf, int msglen, struct authunix_parms *au,
                           char *machname, gid_t *gids)
{
        XDR             xdr;

        if ((!msgbuf) || (!machname) || (!gids) || (!au))
                return -1;

        au->aup_machname = machname;
#ifdef GF_DARWIN_HOST_OS
        au->aup_gids = (int *)gids;
#else
        au->aup_gids = gids;
#endif

        xdrmem_create (&xdr, msgbuf, msglen, XDR_DECODE);

        if (!xdr_authunix_parms (&xdr, au))
                return -1;

        return 0;
}

ssize_t
nfs_xdr_length_round_up (size_t len, size_t bufsize)
{
        int     roundup = 0;

        roundup = len % NFS_XDR_BYTES_PER_UNIT;
        if (roundup > 0)
                roundup = NFS_XDR_BYTES_PER_UNIT - roundup;

        if ((roundup > 0) && ((roundup + len) <= bufsize))
                len += roundup;

        return len;
}

int
nfs_xdr_bytes_round_up (struct iovec *vec, size_t bufsize)
{
        vec->iov_len = nfs_xdr_length_round_up (vec->iov_len, bufsize);
        return 0;
}

void
nfs_xdr_vector_round_up (struct iovec *vec, int vcount, uint32_t count)
{
        uint32_t round_count = 0;

        round_count = nfs_xdr_length_round_up (count, 1048576);
        round_count -= count;
        if (round_count == 0)
                return;

        vec[vcount-1].iov_len += round_count;
}
