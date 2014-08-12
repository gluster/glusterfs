/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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

        if (!PROC(&xdr, res)) {
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

        if (!PROC (&xdr, args)) {
                ret = -1;
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

        if (!PROC (&xdr, args)) {
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

ssize_t
xdr_length_round_up (size_t len, size_t bufsize)
{
        int     roundup = 0;

        roundup = len % XDR_BYTES_PER_UNIT;
        if (roundup > 0)
                roundup = XDR_BYTES_PER_UNIT - roundup;

        if ((roundup > 0) && ((roundup + len) <= bufsize))
                len += roundup;

        return len;
}

int
xdr_bytes_round_up (struct iovec *vec, size_t bufsize)
{
        vec->iov_len = xdr_length_round_up (vec->iov_len, bufsize);
        return 0;
}


void
xdr_vector_round_up (struct iovec *vec, int vcount, uint32_t count)
{
        uint32_t round_count = 0;

        round_count = xdr_length_round_up (count, 1048576);
        round_count -= count;
        if (round_count == 0 || vcount <= 0)
                return;

        vec[vcount-1].iov_len += round_count;
}
