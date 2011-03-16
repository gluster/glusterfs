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



#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "list.h"
#include "dict.h"
#include "xdr-rpc.h"
#include "xdr-common.h"

bool_t
xdr_auth_glusterfs_parms (XDR *xdrs, auth_glusterfs_parms *objp)
{
	register int32_t *buf;

	int i;

	if (xdrs->x_op == XDR_ENCODE) {
		 if (!xdr_u_quad_t (xdrs, &objp->lk_owner))
			 return FALSE;
		buf = XDR_INLINE (xdrs, (4 +  16 )* BYTES_PER_XDR_UNIT);
		if (buf == NULL) {
			 if (!xdr_u_int (xdrs, &objp->pid))
				 return FALSE;
			 if (!xdr_u_int (xdrs, &objp->uid))
				 return FALSE;
			 if (!xdr_u_int (xdrs, &objp->gid))
				 return FALSE;
			 if (!xdr_u_int (xdrs, &objp->ngrps))
				 return FALSE;
			 if (!xdr_vector (xdrs, (char *)objp->groups, 16,
				sizeof (u_int), (xdrproc_t) xdr_u_int))
				 return FALSE;
		} else {
			IXDR_PUT_U_LONG(buf, objp->pid);
			IXDR_PUT_U_LONG(buf, objp->uid);
			IXDR_PUT_U_LONG(buf, objp->gid);
			IXDR_PUT_U_LONG(buf, objp->ngrps);
			{
				register u_int *genp;

				for (i = 0, genp = objp->groups;
					i < 16; ++i) {
					IXDR_PUT_U_LONG(buf, *genp++);
				}
			}
		}
		return TRUE;
	} else if (xdrs->x_op == XDR_DECODE) {
		 if (!xdr_u_quad_t (xdrs, &objp->lk_owner))
			 return FALSE;
		buf = XDR_INLINE (xdrs, (4 +  16 )* BYTES_PER_XDR_UNIT);
		if (buf == NULL) {
			 if (!xdr_u_int (xdrs, &objp->pid))
				 return FALSE;
			 if (!xdr_u_int (xdrs, &objp->uid))
				 return FALSE;
			 if (!xdr_u_int (xdrs, &objp->gid))
				 return FALSE;
			 if (!xdr_u_int (xdrs, &objp->ngrps))
				 return FALSE;
			 if (!xdr_vector (xdrs, (char *)objp->groups, 16,
				sizeof (u_int), (xdrproc_t) xdr_u_int))
				 return FALSE;
		} else {
			objp->pid = IXDR_GET_U_LONG(buf);
			objp->uid = IXDR_GET_U_LONG(buf);
			objp->gid = IXDR_GET_U_LONG(buf);
			objp->ngrps = IXDR_GET_U_LONG(buf);
			{
				register u_int *genp;

				for (i = 0, genp = objp->groups;
					i < 16; ++i) {
					*genp++ = IXDR_GET_U_LONG(buf);
				}
			}
		}
	 return TRUE;
	}

	 if (!xdr_u_quad_t (xdrs, &objp->lk_owner))
		 return FALSE;
	 if (!xdr_u_int (xdrs, &objp->pid))
		 return FALSE;
	 if (!xdr_u_int (xdrs, &objp->uid))
		 return FALSE;
	 if (!xdr_u_int (xdrs, &objp->gid))
		 return FALSE;
	 if (!xdr_u_int (xdrs, &objp->ngrps))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->groups, 16,
		sizeof (u_int), (xdrproc_t) xdr_u_int))
		 return FALSE;
	return TRUE;
}


ssize_t
xdr_to_glusterfs_auth (char *buf, struct auth_glusterfs_parms *req)
{
        XDR     xdr;
        ssize_t ret = -1;

        if ((!buf) || (!req))
                return -1;

        xdrmem_create (&xdr, buf, sizeof (struct auth_glusterfs_parms),
                       XDR_DECODE);
        if (!xdr_auth_glusterfs_parms (&xdr, req)) {
                gf_log ("", GF_LOG_WARNING,
                        "failed to decode glusterfs parameters");
                ret  = -1;
                goto ret;
        }

        ret = (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base));
ret:
        return ret;

}
int
auth_glusterfs_request_init (rpcsvc_request_t *req, void *priv)
{
        if (!req)
                return -1;
        memset (req->verf.authdata, 0, RPCSVC_MAX_AUTH_BYTES);
        req->verf.datalen = 0;
        req->verf.flavour = AUTH_NULL;

        return 0;
}

int auth_glusterfs_authenticate (rpcsvc_request_t *req, void *priv)
{
        int                          ret = RPCSVC_AUTH_REJECT;
        struct auth_glusterfs_parms  au = {0,};
        int                          gidcount = 0;

        if (!req)
                return ret;

        ret = xdr_to_glusterfs_auth (req->cred.authdata, &au);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING,
                        "failed to decode glusterfs credentials");
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        req->pid = au.pid;
        req->uid = au.uid;
        req->gid = au.gid;
        req->lk_owner = au.lk_owner;
        req->auxgidcount = au.ngrps;

        if (req->auxgidcount > 16) {
                gf_log ("", GF_LOG_WARNING,
                        "more than 16 aux gids found, failing authentication");
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        for (gidcount = 0; gidcount < au.ngrps; ++gidcount)
                req->auxgids[gidcount] = au.groups[gidcount];

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth Info: pid: %u, uid: %d"
                ", gid: %d, owner: %"PRId64,
                req->pid, req->uid, req->gid, req->lk_owner);
        ret = RPCSVC_AUTH_ACCEPT;
err:
        return ret;
}

rpcsvc_auth_ops_t auth_glusterfs_ops = {
        .transport_init         = NULL,
        .request_init           = auth_glusterfs_request_init,
        .authenticate           = auth_glusterfs_authenticate
};

rpcsvc_auth_t rpcsvc_auth_glusterfs = {
        .authname       = "AUTH_GLUSTERFS",
        .authnum        = AUTH_GLUSTERFS,
        .authops        = &auth_glusterfs_ops,
        .authprivate    = NULL
};


rpcsvc_auth_t *
rpcsvc_auth_glusterfs_init (rpcsvc_t *svc, dict_t *options)
{
        return &rpcsvc_auth_glusterfs;
}
