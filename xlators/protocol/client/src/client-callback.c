/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "client.h"
#include "rpc-clnt.h"

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

rpcclnt_cb_actor_t gluster_cbk_actors[GF_CBK_MAXVALUE] = {
        [GF_CBK_NULL]      = {"NULL",      GF_CBK_NULL,      client_cbk_null },
        [GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, client_cbk_fetchspec },
        [GF_CBK_INO_FLUSH] = {"INO_FLUSH", GF_CBK_INO_FLUSH, client_cbk_ino_flush },
};


struct rpcclnt_cb_program gluster_cbk_prog = {
        .progname  = "GlusterFS Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
        .actors    = gluster_cbk_actors,
        .numactors = GF_CBK_MAXVALUE,
};
