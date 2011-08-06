/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#include "portmap.h"


ssize_t
xdr_to_pmap_port_by_brick_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_port_by_brick_req);
}


ssize_t
xdr_to_pmap_port_by_brick_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_port_by_brick_rsp);
}


ssize_t
xdr_from_pmap_port_by_brick_req (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_port_by_brick_req);
}


ssize_t
xdr_from_pmap_port_by_brick_rsp (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_port_by_brick_rsp);
}



ssize_t
xdr_to_pmap_brick_by_port_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_brick_by_port_req);
}


ssize_t
xdr_to_pmap_brick_by_port_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_brick_by_port_rsp);
}


ssize_t
xdr_from_pmap_brick_by_port_req (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_brick_by_port_req);
}


ssize_t
xdr_from_pmap_brick_by_port_rsp (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_brick_by_port_rsp);
}




ssize_t
xdr_to_pmap_signup_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_signup_req);
}


ssize_t
xdr_to_pmap_signup_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_signup_rsp);
}


ssize_t
xdr_from_pmap_signup_req (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_signup_req);
}


ssize_t
xdr_from_pmap_signup_rsp (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_signup_rsp);
}




ssize_t
xdr_to_pmap_signin_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_signin_req);
}


ssize_t
xdr_to_pmap_signin_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_signin_rsp);
}


ssize_t
xdr_from_pmap_signin_req (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_signin_req);
}


ssize_t
xdr_from_pmap_signin_rsp (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_signin_rsp);
}




ssize_t
xdr_to_pmap_signout_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_signout_req);
}


ssize_t
xdr_to_pmap_signout_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_pmap_signout_rsp);
}


ssize_t
xdr_from_pmap_signout_req (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_signout_req);
}


ssize_t
xdr_from_pmap_signout_rsp (struct iovec inmsg, void *args)
{
        return xdr_serialize_generic (inmsg, (void *)args,
                                      (xdrproc_t)xdr_pmap_signout_rsp);
}

