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


int
auth_unix_request_init (rpcsvc_request_t *req, void *priv)
{
        return 0;
}

int auth_unix_authenticate (rpcsvc_request_t *req, void *priv)
{
        int                     ret = RPCSVC_AUTH_REJECT;
        struct authunix_parms   aup;
        char                    machname[MAX_MACHINE_NAME];

        if (!req)
                return ret;

	req->auxgids = req->auxgidsmall;
        ret = xdr_to_auth_unix_cred (req->cred.authdata, req->cred.datalen,
                                     &aup, machname, req->auxgids);
        if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, "failed to decode unix credentials");
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        req->uid = aup.aup_uid;
        req->gid = aup.aup_gid;
        req->auxgidcount = aup.aup_len;

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth Info: machine name: %s, uid: %d"
                ", gid: %d", machname, req->uid, req->gid);
        ret = RPCSVC_AUTH_ACCEPT;
err:
        return ret;
}

rpcsvc_auth_ops_t auth_unix_ops = {
        .transport_init              = NULL,
        .request_init           = auth_unix_request_init,
        .authenticate           = auth_unix_authenticate
};

rpcsvc_auth_t rpcsvc_auth_unix = {
        .authname       = "AUTH_UNIX",
        .authnum        = AUTH_UNIX,
        .authops        = &auth_unix_ops,
        .authprivate    = NULL
};


rpcsvc_auth_t *
rpcsvc_auth_unix_init (rpcsvc_t *svc, dict_t *options)
{
        return &rpcsvc_auth_unix;
}
