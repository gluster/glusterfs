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


#include "logging.h"
#include "xdr-common.h"

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
                gf_log_callingfn ("xdr", GF_LOG_WARNING,
                                  "XDR encoding failed");
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
                gf_log_callingfn ("xdr", GF_LOG_WARNING,
                                  "XDR decoding failed");
                ret  = -1;
                goto ret;
        }

        ret = xdr_decoded_length (xdr);
ret:
        return ret;
}


bool_t
xdr_gf_dump_req (XDR *xdrs, gf_dump_req *objp)
{
	 if (!xdr_u_quad_t (xdrs, &objp->gfs_id))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gf_prog_detail (XDR *xdrs, gf_prog_detail *objp)
{
	 if (!xdr_string (xdrs, &objp->progname, ~0))
		 return FALSE;
	 if (!xdr_u_quad_t (xdrs, &objp->prognum))
		 return FALSE;
	 if (!xdr_u_quad_t (xdrs, &objp->progver))
		 return FALSE;
	 if (!xdr_pointer (xdrs, (char **)&objp->next, sizeof (gf_prog_detail), (xdrproc_t) xdr_gf_prog_detail))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gf_dump_rsp (XDR *xdrs, gf_dump_rsp *objp)
{
	 if (!xdr_u_quad_t (xdrs, &objp->gfs_id))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	 if (!xdr_pointer (xdrs, (char **)&objp->prog, sizeof (gf_prog_detail), (xdrproc_t) xdr_gf_prog_detail))
		 return FALSE;
	return TRUE;
}


ssize_t
xdr_serialize_dump_rsp (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_dump_rsp);
}

ssize_t
xdr_to_dump_req (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf_dump_req);
}


ssize_t
xdr_from_dump_req (struct iovec outmsg, void *rsp)
{
        return xdr_serialize_generic (outmsg, (void *)rsp,
                                      (xdrproc_t)xdr_gf_dump_req);
}

ssize_t
xdr_to_dump_rsp (struct iovec inmsg, void *args)
{
        return xdr_to_generic (inmsg, (void *)args,
                               (xdrproc_t)xdr_gf_dump_rsp);
}
