/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "gf-changelog-rpc.h"
#include "changelog-misc.h"
#include "changelog-mem-types.h"

struct rpc_clnt_program gf_changelog_clnt;

/* TODO: piggyback reconnect to called (upcall) */
int
gf_changelog_rpc_notify (struct rpc_clnt *rpc,
                         void *mydata, rpc_clnt_event_t event, void *data)
{
        switch (event) {
        case RPC_CLNT_CONNECT:
                rpc_clnt_set_connected (&rpc->conn);
                break;
        case RPC_CLNT_DISCONNECT:
        case RPC_CLNT_MSG:
        case RPC_CLNT_DESTROY:
                break;
        }

        return 0;
}

struct rpc_clnt *
gf_changelog_rpc_init (xlator_t *this, gf_changelog_t *entry)
{
        char sockfile[UNIX_PATH_MAX] = {0,};

        CHANGELOG_MAKE_SOCKET_PATH (entry->brick,
                                    sockfile, UNIX_PATH_MAX);
        return changelog_rpc_client_init (this, entry,
                                          sockfile, gf_changelog_rpc_notify);
}

/**
 * remote procedure calls declarations.
 */

int
gf_probe_changelog_cbk (struct rpc_req *req,
                        struct iovec *iovec, int count, void *myframe)
{
        return 0;
}

int
gf_probe_changelog_filter (call_frame_t *frame, xlator_t *this, void *data)
{
        char *sock = NULL;
        gf_changelog_t *entry = NULL;
        changelog_probe_req req = {0,};

        entry = data;
        sock = RPC_SOCK (entry);

        (void) memcpy (&req.sock, sock, strlen (sock));
        req.filter = entry->notify;

        /* invoke RPC */
        return changelog_rpc_sumbit_req (RPC_PROBER (entry), (void *) &req,
                                         frame, &gf_changelog_clnt,
                                         CHANGELOG_RPC_PROBE_FILTER, NULL, 0,
                                         NULL, this, gf_probe_changelog_cbk,
                                         (xdrproc_t) xdr_changelog_probe_req);
}

int
gf_changelog_invoke_rpc (xlator_t *this, gf_changelog_t *entry, int procidx)
{
        return changelog_invoke_rpc (this, RPC_PROBER (entry),
                                     &gf_changelog_clnt, procidx, entry);
}

struct rpc_clnt_procedure gf_changelog_procs[CHANGELOG_RPC_PROC_MAX] = {
        [CHANGELOG_RPC_PROC_NULL] = {"NULL", NULL},
        [CHANGELOG_RPC_PROBE_FILTER] = {
                "PROBE FILTER", gf_probe_changelog_filter
        },
};

struct rpc_clnt_program gf_changelog_clnt = {
        .progname  = "LIBGFCHANGELOG",
        .prognum   = CHANGELOG_RPC_PROGNUM,
        .progver   = CHANGELOG_RPC_PROGVER,
        .numproc   = CHANGELOG_RPC_PROC_MAX,
        .proctable = gf_changelog_procs,
};
