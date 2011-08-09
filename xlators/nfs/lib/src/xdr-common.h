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

#ifndef _NFS_XDR_COMMON_H_
#define _NFS_XDR_COMMON_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <rpc/rpc.h>
#define NFS_XDR_BYTES_PER_UNIT      4

/* Returns the address of the byte that follows the
 * last byte used for decoding the previous xdr component.
 * For eg, once the RPC call for NFS has been decoded, thie macro will return
 * the address from which the NFS header starts.
 */
#define nfs_xdr_decoded_remaining_addr(xdr)        ((&xdr)->x_private)

/* Returns the length of the remaining record after the previous decode
 * operation completed.
 */
#define nfs_xdr_decoded_remaining_len(xdr)         ((&xdr)->x_handy)

/* Returns the number of bytes used by the last encode operation. */
#define nfs_xdr_encoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))

#define nfs_xdr_decoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))

#endif
