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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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

/* Decodes the XDR format in msgbuf into rpc_msg.
 * The remaining payload is returned into payload.
 */
int
xdr_to_rpc_reply (char *msgbuf, size_t len, struct rpc_msg *reply,
                  struct iovec *payload, char *verfbytes)
{
        XDR                     xdr;
        int                     ret = -1;

        if ((!msgbuf) || (!reply)) {
                ret = -EINVAL;
                goto out;
        }

        memset (reply, 0, sizeof (struct rpc_msg));

        reply->acpted_rply.ar_verf = _null_auth;
        reply->acpted_rply.ar_results.where = NULL;
        reply->acpted_rply.ar_results.proc = (xdrproc_t)(xdr_void);

        xdrmem_create (&xdr, msgbuf, len, XDR_DECODE);
        if (!xdr_replymsg (&xdr, reply)) {
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

#if 0
bool_t
true_func (XDR *s, caddr_t *a)
{
        return TRUE;
}
#endif

int
rpc_request_to_xdr (struct rpc_msg *request, char *dest, size_t len,
                    struct iovec *dst)
{
        XDR xdr;
        int ret = -1;

        if ((!dest) || (!request) || (!dst)) {
                goto out;
        }

        xdrmem_create (&xdr, dest, len, XDR_ENCODE);
        if (!xdr_callmsg (&xdr, request)) {
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

        if (!au || !dest || !iov) {
                goto out;
        }

        xdrmem_create (&xdr, dest, len, XDR_DECODE);

        if (!xdr_authunix_parms (&xdr, au)) {
                goto out;
        }

        iov->iov_base = dest;
        iov->iov_len = xdr_encoded_length (xdr);

        ret = 0;
out:
        return ret;
}
