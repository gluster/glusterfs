/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "client.h"
#include "rpc-clnt.h"
#include "defaults.h"

int
client_cbk_null (struct rpc_clnt *rpc, void *mydata, void *data)
{
        gf_log (THIS->name, GF_LOG_WARNING,
                "this function should not be called");
        return 0;
}

int
client_cbk_fetchspec (struct rpc_clnt *rpc, void *mydata, void *data)
{
        gf_log (THIS->name, GF_LOG_WARNING,
                "this function should not be called");
        return 0;
}

int
client_cbk_ino_flush (struct rpc_clnt *rpc, void *mydata, void *data)
{
        gf_log (THIS->name, GF_LOG_WARNING,
                "this function should not be called");
        return 0;
}

int
client_cbk_cache_invalidation (struct rpc_clnt *rpc, void *mydata, void *data)
{
        int              ret                        = -1;
        struct iovec     *iov                       = NULL;
        struct gf_upcall upcall_data                = {0,};
        uuid_t           gfid;
        struct gf_upcall_cache_invalidation ca_data = {0,};
        gfs3_cbk_cache_invalidation_req     ca_req  = {0,};

        gf_log (THIS->name, GF_LOG_TRACE, "Upcall callback is called");

        if (!rpc || !mydata || !data)
                goto out;

        iov = (struct iovec *)data;
        ret =  xdr_to_generic (*iov, &ca_req,
                               (xdrproc_t)xdr_gfs3_cbk_cache_invalidation_req);

        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "XDR decode of cache_invalidation failed.");
                goto out;
        }

        upcall_data.data = &ca_data;
        gf_proto_cache_invalidation_to_upcall (&ca_req, &upcall_data);

        gf_log (THIS->name, GF_LOG_TRACE, "Upcall gfid = %s, ret = %d",
                ca_req.gfid, ret);

        default_notify (THIS, GF_EVENT_UPCALL, &upcall_data);

out:
        return 0;
}

rpcclnt_cb_actor_t gluster_cbk_actors[GF_CBK_MAXVALUE] = {
        [GF_CBK_NULL]      = {"NULL",      GF_CBK_NULL,      client_cbk_null },
        [GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, client_cbk_fetchspec },
        [GF_CBK_INO_FLUSH] = {"INO_FLUSH", GF_CBK_INO_FLUSH, client_cbk_ino_flush },
        [GF_CBK_CACHE_INVALIDATION] = {"CACHE_INVALIDATION",
                                       GF_CBK_CACHE_INVALIDATION,
                                       client_cbk_cache_invalidation },
};


struct rpcclnt_cb_program gluster_cbk_prog = {
        .progname  = "GlusterFS Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
        .actors    = gluster_cbk_actors,
        .numactors = GF_CBK_MAXVALUE,
};
