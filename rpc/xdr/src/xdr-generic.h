/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XDR_GENERIC_H
#define _XDR_GENERIC_H

#include <sys/uio.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

#include "compat.h"

#define xdr_decoded_remaining_addr(xdr)        ((&xdr)->x_private)
#define xdr_decoded_remaining_len(xdr)         ((&xdr)->x_handy)
#define xdr_encoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))
#define xdr_decoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))

#define XDR_BYTES_PER_UNIT      4

/*
  On OSX > 10.9
  -------------
  typedef bool_t (*xdrproc_t)(XDR *, void *, unsigned int);

  On OSX < 10.9
  ------------
  typedef bool_t (*xdrproc_t)(XDR *, ...);

  FreeBSD all versions
  ------------
  typedef bool_t (*xdrproc_t)(XDR *, ...);

  NetBSD 6.1.4
  -----------
  typedef bool_t (*xdrproc_t)(XDR *, const void *);

  Linux all versions
  -----------
  typedef bool_t (*xdrproc_t)(XDR *, void *,...);
*/

#if defined(__NetBSD__)
#define  PROC(xdr, res)  proc(xdr, res)
#else
#define  PROC(xdr, res)  proc(xdr, res, 0)
#endif

ssize_t
xdr_serialize_generic (struct iovec outmsg, void *res, xdrproc_t proc);

ssize_t
xdr_to_generic (struct iovec inmsg, void *args, xdrproc_t proc);

ssize_t
xdr_to_generic_payload (struct iovec inmsg, void *args, xdrproc_t proc,
                        struct iovec *pendingpayload);


extern int
xdr_bytes_round_up (struct iovec *vec, size_t bufsize);

extern ssize_t
xdr_length_round_up (size_t len, size_t bufsize);

void
xdr_vector_round_up (struct iovec *vec, int vcount, uint32_t count);

#endif /* !_XDR_GENERIC_H */
