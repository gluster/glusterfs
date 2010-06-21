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



#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "list.h"
#include "dict.h"
#include "xdr-rpc.h"
#include "glusterfs-xdr.h"

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

        if (!req)
                return ret;

        ret = xdr_to_glusterfs_auth (req->cred.authdata, &au);
        if (ret == -1) {
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        req->pid = au.pid;
        req->uid = au.uid;
        req->gid = au.gid;
        req->lk_owner = au.lk_owner;
        req->auxgidcount = au.ngrps;

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth Info: pid: %u, uid: %d"
                ", gid: %d, owner: %"PRId64,
                req->pid, req->uid, req->gid, req->lk_owner);
        ret = RPCSVC_AUTH_ACCEPT;
err:
        return ret;
}

rpcsvc_auth_ops_t auth_glusterfs_ops = {
        .conn_init              = NULL,
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
