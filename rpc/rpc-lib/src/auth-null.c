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


int
auth_null_request_init (rpcsvc_request_t *req, void *priv)
{
        return 0;
}

int auth_null_authenticate (rpcsvc_request_t *req, void *priv)
{
        /* Always succeed. */
        return RPCSVC_AUTH_ACCEPT;
}

rpcsvc_auth_ops_t auth_null_ops = {
        .transport_init              = NULL,
        .request_init           = auth_null_request_init,
        .authenticate           = auth_null_authenticate
};

rpcsvc_auth_t rpcsvc_auth_null = {
        .authname       = "AUTH_NULL",
        .authnum        = AUTH_NULL,
        .authops        = &auth_null_ops,
        .authprivate    = NULL
};


rpcsvc_auth_t *
rpcsvc_auth_null_init (rpcsvc_t *svc, dict_t *options)
{
        return &rpcsvc_auth_null;
}
