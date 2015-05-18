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
#include <errno.h>

#include "mem-pool.h"
#include "xdr-rpc.h"
#include "xdr-common.h"
#include "logging.h"
#include "common-utils.h"

/* Decodes the XDR format in msgbuf into rpc_msg.
 * The remaining payload is returned into payload.
 */
int
xdr_to_rpc_reply (char *msgbuf, size_t len, struct rpc_msg *reply,
                  struct iovec *payload, char *verfbytes)
{
        XDR                     xdr;
        int                     ret = -EINVAL;

        GF_VALIDATE_OR_GOTO ("rpc", msgbuf, out);
        GF_VALIDATE_OR_GOTO ("rpc", reply, out);

        memset (reply, 0, sizeof (struct rpc_msg));

        reply->acpted_rply.ar_verf = _null_auth;
        reply->acpted_rply.ar_results.where = NULL;
        reply->acpted_rply.ar_results.proc = (xdrproc_t)(xdr_void);

        xdrmem_create (&xdr, msgbuf, len, XDR_DECODE);
        if (!xdr_replymsg (&xdr, reply)) {
                gf_log ("rpc", GF_LOG_WARNING, "failed to decode reply msg");
                ret = -errno;
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


int
rpc_request_to_xdr (struct rpc_msg *request, char *dest, size_t len,
                    struct iovec *dst)
{
        XDR xdr;
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", dest, out);
        GF_VALIDATE_OR_GOTO ("rpc", request, out);
        GF_VALIDATE_OR_GOTO ("rpc", dst, out);

        xdrmem_create (&xdr, dest, len, XDR_ENCODE);
        if (!xdr_callmsg (&xdr, request)) {
                gf_log ("rpc", GF_LOG_WARNING, "failed to encode call msg");
                goto out;
        }

        dst->iov_base = dest;
        dst->iov_len = xdr_encoded_length (xdr);

        ret = 0;

out:
        return ret;
}


int
auth_unix_cred_to_xdr (struct authunix_parms *au, char *dest, size_t len,
                       struct iovec *iov)
{
        XDR xdr;
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("rpc", au, out);
        GF_VALIDATE_OR_GOTO ("rpc", dest, out);
        GF_VALIDATE_OR_GOTO ("rpc", iov, out);

        xdrmem_create (&xdr, dest, len, XDR_DECODE);

        if (!xdr_authunix_parms (&xdr, au)) {
                gf_log ("rpc", GF_LOG_WARNING, "failed to decode authunix parms");
                goto out;
        }

        iov->iov_base = dest;
        iov->iov_len = xdr_encoded_length (xdr);

        ret = 0;
out:
        return ret;
}
