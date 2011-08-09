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


#ifndef _PORTMAP_H
#define _PORTMAP_H

#include <sys/uio.h>

#include "xdr-generic.h"
#include "portmap-xdr.h"


ssize_t
xdr_to_pmap_port_by_brick_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_pmap_port_by_brick_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_from_pmap_port_by_brick_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_pmap_port_by_brick_rsp (struct iovec outmsg, void *args);


ssize_t
xdr_to_pmap_brick_by_port_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_pmap_brick_by_port_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_from_pmap_brick_by_port_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_pmap_brick_by_port_rsp (struct iovec outmsg, void *args);


ssize_t
xdr_from_pmap_signup_req (struct iovec msg, void *args);

ssize_t
xdr_from_pmap_signup_rsp (struct iovec msg, void *args);

ssize_t
xdr_to_pmap_signup_req (struct iovec msg, void *args);

ssize_t
xdr_to_pmap_signup_rsp (struct iovec msg, void *args);


ssize_t
xdr_from_pmap_signin_req (struct iovec msg, void *args);

ssize_t
xdr_from_pmap_signin_rsp (struct iovec msg, void *args);

ssize_t
xdr_to_pmap_signin_req (struct iovec msg, void *args);

ssize_t
xdr_to_pmap_signin_rsp (struct iovec msg, void *args);


ssize_t
xdr_from_pmap_signout_req (struct iovec msg, void *args);

ssize_t
xdr_from_pmap_signout_rsp (struct iovec msg, void *args);

ssize_t
xdr_to_pmap_signout_req (struct iovec msg, void *args);

ssize_t
xdr_to_pmap_signout_rsp (struct iovec msg, void *args);


#endif /* !_PORTMAP_H */
