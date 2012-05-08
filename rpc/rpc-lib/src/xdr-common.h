/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XDR_COMMON_H_
#define _XDR_COMMON_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <rpc/types.h>
#include <sys/types.h>
#include <rpc/xdr.h>
#include <sys/uio.h>

#ifdef __NetBSD__
#include <dirent.h>
#endif /* __NetBSD__ */

enum gf_dump_procnum {
        GF_DUMP_NULL,
        GF_DUMP_DUMP,
        GF_DUMP_MAXVALUE,
};

#define GLUSTER_DUMP_PROGRAM 123451501 /* Completely random */
#define GLUSTER_DUMP_VERSION 1

#define GF_MAX_AUTH_BYTES   2048

#if GF_DARWIN_HOST_OS
#define xdr_u_quad_t xdr_u_int64_t
#define xdr_quad_t   xdr_int64_t
#define xdr_uint32_t xdr_u_int32_t
#define uint64_t u_int64_t
#endif

#if defined(__NetBSD__)
#define xdr_u_quad_t xdr_u_int64_t
#define xdr_quad_t   xdr_int64_t
#define xdr_uint32_t xdr_u_int32_t
#define xdr_uint64_t xdr_u_int64_t
#endif


#if GF_SOLARIS_HOST_OS
#define u_quad_t uint64_t
#define quad_t int64_t
#define xdr_u_quad_t xdr_uint64_t
#define xdr_quad_t   xdr_int64_t
#define xdr_uint32_t xdr_uint32_t
#endif

/* Returns the address of the byte that follows the
 * last byte used for decoding the previous xdr component.
 * E.g. once the RPC call for NFS has been decoded, the macro will return
 * the address from which the NFS header starts.
 */
#define xdr_decoded_remaining_addr(xdr)        ((&xdr)->x_private)

/* Returns the length of the remaining record after the previous decode
 * operation completed.
 */
#define xdr_decoded_remaining_len(xdr)         ((&xdr)->x_handy)

/* Returns the number of bytes used by the last encode operation. */
#define xdr_encoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))

#define xdr_decoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))

#endif
