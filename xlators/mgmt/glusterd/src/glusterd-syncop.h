/*
   Copyright (c) 2012-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __RPC_SYNCOP_H
#define __RPC_SYNCOP_H

#include "syncop.h"

#define GD_SYNC_OPCODE_KEY "sync-mgmt-operation"

/* gd_syncop_* */
#define GD_SYNCOP(rpc, stb, cbk, req, prog, procnum, xdrproc) do {      \
                int ret = 0;                                            \
                struct  synctask        *task = NULL;                   \
                gf_boolean_t            cbk_lost = _gf_true;            \
                task = synctask_get ();                                 \
                stb->task = task;                                       \
                                                                        \
                ret = gd_syncop_submit_request (rpc, req, stb,          \
                                                prog, procnum, cbk,     \
                                                (xdrproc_t)xdrproc,     \
                                                &cbk_lost);             \
                if (!cbk_lost)                                          \
                        synctask_yield (stb->task);                     \
        } while (0)


int gd_syncop_submit_request (struct rpc_clnt *rpc, void *req,
                               void *cookie, rpc_clnt_prog_t *prog,
                               int procnum, fop_cbk_fn_t cbkfn,
                               xdrproc_t xdrproc, gf_boolean_t *cbk_lost);


int gd_syncop_mgmt_lock (struct rpc_clnt *rpc, struct syncargs *arg,
                         uuid_t my_uuid, uuid_t recv_uuid);
int gd_syncop_mgmt_unlock (struct rpc_clnt *rpc, struct syncargs *arg,
                           uuid_t my_uuid, uuid_t recv_uuid);
int gd_syncop_mgmt_stage_op (struct rpc_clnt *rpc, struct syncargs *arg,
                             uuid_t my_uuid, uuid_t recv_uuid, int op,
                             dict_t *dict_out, dict_t *op_ctx);
int gd_syncop_mgmt_commit_op (struct rpc_clnt *rpc, struct syncargs *arg,
                              uuid_t my_uuid, uuid_t recv_uuid, int op,
                              dict_t *dict_out, dict_t *op_ctx);
#endif /* __RPC_SYNCOP_H */
