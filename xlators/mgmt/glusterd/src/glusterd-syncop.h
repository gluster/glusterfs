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
#include "glusterd-sm.h"

#define GD_SYNC_OPCODE_KEY "sync-mgmt-operation"

/* gd_syncop_* */
#define GD_SYNCOP(rpc, stb, cookie, cbk, req, prog, procnum, xdrproc) do {     \
                int ret = 0;                                                   \
                struct  synctask        *task = NULL;                          \
                glusterd_conf_t         *conf= THIS->private;                  \
                                                                               \
                task = synctask_get ();                                        \
                stb->task = task;                                              \
                                                                               \
                /*This is to ensure that the brick_op_cbk is able to           \
                 * take the big lock*/                                         \
                synclock_unlock (&conf->big_lock);                             \
                ret = gd_syncop_submit_request (rpc, req, stb, cookie,         \
                                                prog, procnum, cbk,            \
                                                (xdrproc_t)xdrproc);           \
                if (!ret)                                                      \
                        synctask_yield (stb->task);                            \
                synclock_lock (&conf->big_lock);                               \
        } while (0)


int gd_syncop_submit_request (struct rpc_clnt *rpc, void *req, void *local,
                              void *cookie, rpc_clnt_prog_t *prog, int procnum,
                              fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);


int gd_syncop_mgmt_lock (glusterd_peerinfo_t *peerinfo, struct syncargs *arg,
                         uuid_t my_uuid, uuid_t recv_uuid);
int gd_syncop_mgmt_unlock (glusterd_peerinfo_t *peerinfo, struct syncargs *arg,
                           uuid_t my_uuid, uuid_t recv_uuid);
int gd_syncop_mgmt_stage_op (struct rpc_clnt *rpc, struct syncargs *arg,
                             uuid_t my_uuid, uuid_t recv_uuid, int op,
                             dict_t *dict_out, dict_t *op_ctx);
int gd_syncop_mgmt_commit_op (struct rpc_clnt *rpc, struct syncargs *arg,
                              uuid_t my_uuid, uuid_t recv_uuid, int op,
                              dict_t *dict_out, dict_t *op_ctx);
#endif /* __RPC_SYNCOP_H */
