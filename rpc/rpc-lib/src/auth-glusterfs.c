/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/



#include "rpcsvc.h"
#include "list.h"
#include "dict.h"
#include "xdr-rpc.h"
#include "xdr-common.h"
#include "rpc-common-xdr.h"

/* V1 */

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
        return 0;
}

int auth_glusterfs_authenticate (rpcsvc_request_t *req, void *priv)
{
        struct auth_glusterfs_parms  au = {0,};

        int ret      = RPCSVC_AUTH_REJECT;
        int j        = 0;
        int i        = 0;
        int gidcount = 0;

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
        req->lk_owner.len = 8;
        {
                for (i = 0; i < req->lk_owner.len; i++, j += 8)
                        req->lk_owner.data[i] = (char)((au.lk_owner >> j) & 0xff);
        }
        req->auxgidcount = au.ngrps;

        if (req->auxgidcount > 16) {
                gf_log ("", GF_LOG_WARNING,
                        "more than 16 aux gids found, failing authentication");
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

	if (req->auxgidcount > SMALL_GROUP_COUNT) {
		req->auxgidlarge = GF_CALLOC(req->auxgidcount,
					     sizeof(req->auxgids[0]),
					     gf_common_mt_auxgids);
		req->auxgids = req->auxgidlarge;
	} else {
		req->auxgids = req->auxgidsmall;
	}

	if (!req->auxgids) {
		gf_log ("auth-glusterfs", GF_LOG_WARNING,
			"cannot allocate gid list");
		ret = RPCSVC_AUTH_REJECT;
		goto err;
	}

        for (gidcount = 0; gidcount < au.ngrps; ++gidcount)
                req->auxgids[gidcount] = au.groups[gidcount];


        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth Info: pid: %u, uid: %d"
                ", gid: %d, owner: %s",
                req->pid, req->uid, req->gid, lkowner_utoa (&req->lk_owner));
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

/* V2 */

ssize_t
xdr_to_glusterfs_auth_v2 (char *buf, struct auth_glusterfs_parms_v2 *req)
{
        XDR     xdr;
        ssize_t ret = -1;

        if ((!buf) || (!req))
                return -1;

        xdrmem_create (&xdr, buf, GF_MAX_AUTH_BYTES, XDR_DECODE);
        if (!xdr_auth_glusterfs_parms_v2 (&xdr, req)) {
                gf_log ("", GF_LOG_WARNING,
                        "failed to decode glusterfs v2 parameters");
                ret  = -1;
                goto ret;
        }

        ret = (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base));
ret:
        return ret;

}
int
auth_glusterfs_v2_request_init (rpcsvc_request_t *req, void *priv)
{
        return 0;
}

int auth_glusterfs_v2_authenticate (rpcsvc_request_t *req, void *priv)
{
        struct auth_glusterfs_parms_v2  au = {0,};
        int ret                            = RPCSVC_AUTH_REJECT;
        int i                              = 0;
        int max_groups                     = 0;
        int max_lk_owner_len               = 0;

        if (!req)
                return ret;

        ret = xdr_to_glusterfs_auth_v2 (req->cred.authdata, &au);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING,
                        "failed to decode glusterfs credentials");
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        req->pid = au.pid;
        req->uid = au.uid;
        req->gid = au.gid;
        req->lk_owner.len = au.lk_owner.lk_owner_len;
        req->auxgidcount = au.groups.groups_len;

        /* the number of groups and size of lk_owner depend on each other */
        max_groups = GF_AUTH_GLUSTERFS_MAX_GROUPS (req->lk_owner.len);
        max_lk_owner_len = GF_AUTH_GLUSTERFS_MAX_LKOWNER (req->auxgidcount);

        if (req->auxgidcount > max_groups) {
                gf_log ("", GF_LOG_WARNING,
                        "more than max aux gids found (%d) , truncating it "
                        "to %d and continuing", au.groups.groups_len,
                        max_groups);
                req->auxgidcount = max_groups;
        }

        if (req->lk_owner.len > max_lk_owner_len) {
                gf_log ("", GF_LOG_WARNING,
                        "lkowner field to big (%d), depends on the number of "
                        "groups (%d), failing authentication",
                        req->lk_owner.len, req->auxgidcount);
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

	if (req->auxgidcount > SMALL_GROUP_COUNT) {
		req->auxgidlarge = GF_CALLOC(req->auxgidcount,
					     sizeof(req->auxgids[0]),
					     gf_common_mt_auxgids);
		req->auxgids = req->auxgidlarge;
	} else {
		req->auxgids = req->auxgidsmall;
	}

	if (!req->auxgids) {
		gf_log ("auth-glusterfs-v2", GF_LOG_WARNING,
			"cannot allocate gid list");
		ret = RPCSVC_AUTH_REJECT;
		goto err;
	}

        for (i = 0; i < req->auxgidcount; ++i)
                req->auxgids[i] = au.groups.groups_val[i];

        for (i = 0; i < au.lk_owner.lk_owner_len; ++i)
                req->lk_owner.data[i] = au.lk_owner.lk_owner_val[i];


        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth Info: pid: %u, uid: %d"
                ", gid: %d, owner: %s",
                req->pid, req->uid, req->gid, lkowner_utoa (&req->lk_owner));
        ret = RPCSVC_AUTH_ACCEPT;
err:
        /* TODO: instead use alloca() for these variables */
        free (au.groups.groups_val);
        free (au.lk_owner.lk_owner_val);

        return ret;
}

rpcsvc_auth_ops_t auth_glusterfs_ops_v2 = {
        .transport_init         = NULL,
        .request_init           = auth_glusterfs_v2_request_init,
        .authenticate           = auth_glusterfs_v2_authenticate
};

rpcsvc_auth_t rpcsvc_auth_glusterfs_v2 = {
        .authname       = "AUTH_GLUSTERFS-v2",
        .authnum        = AUTH_GLUSTERFS_v2,
        .authops        = &auth_glusterfs_ops_v2,
        .authprivate    = NULL
};


rpcsvc_auth_t *
rpcsvc_auth_glusterfs_v2_init (rpcsvc_t *svc, dict_t *options)
{
        return &rpcsvc_auth_glusterfs_v2;
}
