/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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


#include "xdr-generic.h"


ssize_t
xdr_serialize_generic (struct iovec outmsg, void *res, xdrproc_t proc)
{
        ssize_t ret = -1;
        XDR     xdr;

        if ((!outmsg.iov_base) || (!res) || (!proc))
                return -1;

        xdrmem_create (&xdr, outmsg.iov_base, (unsigned int)outmsg.iov_len,
                       XDR_ENCODE);

        if (!proc (&xdr, res)) {
                ret = -1;
                goto ret;
        }

        ret = xdr_encoded_length (xdr);

ret:
        return ret;
}


ssize_t
xdr_to_generic (struct iovec inmsg, void *args, xdrproc_t proc)
{
        XDR     xdr;
        ssize_t ret = -1;

        if ((!inmsg.iov_base) || (!args) || (!proc))
                return -1;

        xdrmem_create (&xdr, inmsg.iov_base, (unsigned int)inmsg.iov_len,
                       XDR_DECODE);

        if (!proc (&xdr, args)) {
                ret  = -1;
                goto ret;
        }

        ret = xdr_decoded_length (xdr);
ret:
        return ret;
}


ssize_t
xdr_to_generic_payload (struct iovec inmsg, void *args, xdrproc_t proc,
                        struct iovec *pendingpayload)
{
        XDR     xdr;
        ssize_t ret = -1;

        if ((!inmsg.iov_base) || (!args) || (!proc))
                return -1;

        xdrmem_create (&xdr, inmsg.iov_base, (unsigned int)inmsg.iov_len,
                       XDR_DECODE);

        if (!proc (&xdr, args)) {
                ret  = -1;
                goto ret;
        }

        ret = xdr_decoded_length (xdr);

        if (pendingpayload) {
                pendingpayload->iov_base = xdr_decoded_remaining_addr (xdr);
                pendingpayload->iov_len = xdr_decoded_remaining_len (xdr);
        }

ret:
        return ret;
}
