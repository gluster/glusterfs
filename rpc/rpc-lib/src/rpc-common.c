
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
