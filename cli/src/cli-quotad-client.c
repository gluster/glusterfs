/*
   Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "cli-quotad-client.h"

int
cli_quotad_notify(struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                  void *data)
{
    xlator_t *this = NULL;
    int ret = 0;

    this = mydata;

    switch (event) {
        case RPC_CLNT_CONNECT: {
            gf_log(this->name, GF_LOG_TRACE, "got RPC_CLNT_CONNECT");
            break;
        }

        case RPC_CLNT_DISCONNECT: {
            gf_log(this->name, GF_LOG_TRACE, "got RPC_CLNT_DISCONNECT");
            break;
        }

        default:
            gf_log(this->name, GF_LOG_TRACE, "got some other RPC event %d",
                   event);
            ret = 0;
            break;
    }

    return ret;
}

struct rpc_clnt *
cli_quotad_clnt_init(xlator_t *this, dict_t *options)
{
    struct rpc_clnt *rpc = NULL;
    int ret = -1;

    ret = dict_set_nstrn(options, "transport.address-family",
                         SLEN("transport.address-family"), "unix",
                         SLEN("unix"));
    if (ret)
        goto out;

    ret = dict_set_nstrn(options, "transport-type", SLEN("transport-type"),
                         "socket", SLEN("socket"));
    if (ret)
        goto out;

    ret = dict_set_nstrn(options, "transport.socket.connect-path",
                         SLEN("transport.socket.connect-path"),
                         "/var/run/gluster/quotad.socket",
                         SLEN("/var/run/gluster/quotad.socket"));
    if (ret)
        goto out;

    rpc = rpc_clnt_new(options, this, this->name, 16);
    if (!rpc)
        goto out;

    ret = rpc_clnt_register_notify(rpc, cli_quotad_notify, this);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "failed to register notify");
        goto out;
    }

    rpc_clnt_start(rpc);
out:
    if (ret) {
        if (rpc)
            rpc_clnt_unref(rpc);
        rpc = NULL;
    }

    return rpc;
}
