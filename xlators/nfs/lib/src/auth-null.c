/*
  Copyright (c) 2010-2011-2011-2011 Gluster, Inc. <http://www.gluster.com>
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


int
nfs_auth_null_request_init (rpcsvc_request_t *req, void *priv)
{
        if (!req)
                return -1;

        memset (req->cred.authdata, 0, RPCSVC_MAX_AUTH_BYTES);
        req->cred.datalen = 0;

        memset (req->verf.authdata, 0, RPCSVC_MAX_AUTH_BYTES);
        req->verf.datalen = 0;

        return 0;
}

int
nfs_auth_null_authenticate (rpcsvc_request_t *req, void *priv)
{
        /* Always succeed. */
        return RPCSVC_AUTH_ACCEPT;
}

rpcsvc_auth_ops_t nfs_auth_null_ops = {
        .conn_init              = NULL,
        .request_init           = nfs_auth_null_request_init,
        .authenticate           = nfs_auth_null_authenticate
};

rpcsvc_auth_t nfs_rpcsvc_auth_null = {
        .authname       = "AUTH_NULL",
        .authnum        = AUTH_NULL,
        .authops        = &nfs_auth_null_ops,
        .authprivate    = NULL
};


rpcsvc_auth_t *
nfs_rpcsvc_auth_null_init (rpcsvc_t *svc, dict_t *options)
{
        return &nfs_rpcsvc_auth_null;
}

