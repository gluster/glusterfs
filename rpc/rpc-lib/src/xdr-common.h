/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
#endif


#if GF_SOLARIS_HOST_OS
#define u_quad_t uint64_t
#define quad_t int64_t
#define xdr_u_quad_t xdr_uint64_t
#define xdr_quad_t   xdr_int64_t
#define xdr_uint32_t xdr_uint32_t
#endif

struct auth_glusterfs_parms {
	uint64_t lk_owner;
	u_int pid;
	u_int uid;
	u_int gid;
	u_int ngrps;
	u_int groups[16];
} __attribute__((packed));
typedef struct auth_glusterfs_parms auth_glusterfs_parms;

struct gf_dump_req {
	uint64_t gfs_id;
} __attribute__((packed));
typedef struct gf_dump_req gf_dump_req;

struct gf_prog_detail {
	char    *progname;
	uint64_t prognum;
	uint64_t progver;
	struct gf_prog_detail *next;
} __attribute__((packed));
typedef struct gf_prog_detail gf_prog_detail;

struct gf_dump_rsp {
	uint64_t gfs_id;
	int op_ret;
	int op_errno;
	struct gf_prog_detail *prog;
}__attribute__((packed));
typedef struct gf_dump_rsp gf_dump_rsp;

extern bool_t
xdr_auth_glusterfs_parms (XDR *xdrs, auth_glusterfs_parms *objp);
extern bool_t xdr_gf_dump_req (XDR *, gf_dump_req*);
extern bool_t xdr_gf_prog_detail (XDR *, gf_prog_detail*);
extern bool_t xdr_gf_dump_rsp (XDR *, gf_dump_rsp*);

ssize_t
xdr_serialize_dump_rsp (struct iovec outmsg, void *rsp);
ssize_t
xdr_to_dump_req (struct iovec inmsg, void *args);
ssize_t
xdr_from_dump_req (struct iovec outmsg, void *rsp);
ssize_t
xdr_to_dump_rsp (struct iovec inmsg, void *args);

#define XDR_BYTES_PER_UNIT      4

/* Returns the address of the byte that follows the
 * last byte used for decoding the previous xdr component.
 * For eg, once the RPC call for NFS has been decoded, thie macro will return
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
