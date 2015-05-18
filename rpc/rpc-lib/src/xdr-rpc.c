/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

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
#include "common-utils.h"

/* Decodes the XDR format in msgbuf into rpc_msg.
 * The remaining payload is returned into payload.
 */
int
xdr_to_rpc_call (char *msgbuf, size_t len, struct rpc_msg *call,
                 struct iovec *payload, char *credbytes, char *verfbytes)
{
        XDR                     xdr;
        char                    opaquebytes[GF_MAX_AUTH_BYTES];
        struct opaque_auth      *oa = NULL;
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", msgbuf, out);
        GF_VALIDATE_OR_GOTO ("rpc", call, out);

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
        if (!xdr_callmsg (&xdr, call)) {
                gf_log ("rpc", GF_LOG_WARNING, "failed to decode call msg");
                goto out;
        }

        if (payload) {
                payload->iov_base = xdr_decoded_remaining_addr (xdr);
                payload->iov_len = xdr_decoded_remaining_len (xdr);
        }

        ret = 0;
out:
        return ret;
}


bool_t
true_func (XDR *s, caddr_t *a)
{
        return TRUE;
}


int
rpc_fill_empty_reply (struct rpc_msg *reply, uint32_t xid)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", reply, out);

        /* Setting to 0 also results in reply verifier flavor to be
         * set to AUTH_NULL which is what we want right now.
         */
        memset (reply, 0, sizeof (*reply));
        reply->rm_xid = xid;
        reply->rm_direction = REPLY;

        ret = 0;
out:
        return ret;
}

int
rpc_fill_denied_reply (struct rpc_msg *reply, int rjstat, int auth_err)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", reply, out);

        reply->rm_reply.rp_stat = MSG_DENIED;
        reply->rjcted_rply.rj_stat = rjstat;
        if (rjstat == RPC_MISMATCH) {
                /* No problem with hardcoding
                 * RPC version numbers. We only support
                 * v2 anyway.
                 */
                reply->rjcted_rply.rj_vers.low = 2;
                reply->rjcted_rply.rj_vers.high = 2;
        } else if (rjstat == AUTH_ERROR)
                reply->rjcted_rply.rj_why = auth_err;

        ret = 0;
out:
        return ret;
}


int
rpc_fill_accepted_reply (struct rpc_msg *reply, int arstat, int proglow,
                         int proghigh, int verf, int len, char *vdata)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", reply, out);

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
                reply->acpted_rply.ar_results.proc = (xdrproc_t)true_func;
                reply->acpted_rply.ar_results.where = NULL;
        }

        ret = 0;
out:
        return ret;
}

int
rpc_reply_to_xdr (struct rpc_msg *reply, char *dest, size_t len,
                  struct iovec *dst)
{
        XDR xdr;
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", reply, out);
        GF_VALIDATE_OR_GOTO ("rpc", dest, out);
        GF_VALIDATE_OR_GOTO ("rpc", dst, out);

        xdrmem_create (&xdr, dest, len, XDR_ENCODE);
        if (!xdr_replymsg(&xdr, reply)) {
                gf_log ("rpc", GF_LOG_WARNING, "failed to encode reply msg");
                goto out;
        }

        dst->iov_base = dest;
        dst->iov_len = xdr_encoded_length (xdr);

        ret = 0;
out:
        return ret;
}


int
xdr_to_auth_unix_cred (char *msgbuf, int msglen, struct authunix_parms *au,
                       char *machname, gid_t *gids)
{
        XDR             xdr;
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", msgbuf, out);
        GF_VALIDATE_OR_GOTO ("rpc", machname, out);
        GF_VALIDATE_OR_GOTO ("rpc", gids, out);
        GF_VALIDATE_OR_GOTO ("rpc", au, out);

        au->aup_machname = machname;
#if defined(GF_DARWIN_HOST_OS) || defined(__FreeBSD__)
        au->aup_gids = (int *)gids;
#else
        au->aup_gids = gids;
#endif

        xdrmem_create (&xdr, msgbuf, msglen, XDR_DECODE);

        if (!xdr_authunix_parms (&xdr, au)) {
                gf_log ("rpc", GF_LOG_WARNING, "failed to decode auth unix parms");
                goto out;
        }

        ret = 0;
out:
        return ret;
}
