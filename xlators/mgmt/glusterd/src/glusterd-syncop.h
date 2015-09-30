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
#include "glusterd.h"

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
                else                                                           \
                        gf_asprintf (&stb->errstr, "%s failed. Check log file" \
                                     " for more details", (prog)->progname);   \
                synclock_lock (&conf->big_lock);                               \
        } while (0)

#define GD_ALLOC_COPY_UUID(dst_ptr, uuid, ret) do {                            \
        dst_ptr = GF_CALLOC (1, sizeof (*dst_ptr), gf_common_mt_uuid_t);       \
        if (dst_ptr) {                                                         \
                gf_uuid_copy (*dst_ptr, uuid);                                 \
                ret = 0;                                                       \
        } else {                                                               \
                ret = -1;                                                      \
        }                                                                      \
} while (0)

int gd_syncop_submit_request (struct rpc_clnt *rpc, void *req, void *local,
                              void *cookie, rpc_clnt_prog_t *prog, int procnum,
                              fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);
int gd_syncop_mgmt_lock (glusterd_peerinfo_t *peerinfo, struct syncargs *arg,
                         uuid_t my_uuid, uuid_t recv_uuid);

int gd_syncop_mgmt_unlock (glusterd_peerinfo_t *peerinfo, struct syncargs *arg,
                           uuid_t my_uuid, uuid_t recv_uuid);

int gd_syncop_mgmt_stage_op (glusterd_peerinfo_t *peerinfo,
                             struct syncargs *arg, uuid_t my_uuid,
                             uuid_t recv_uuid, int op, dict_t *dict_out,
                             dict_t *op_ctx);

int gd_syncop_mgmt_commit_op (glusterd_peerinfo_t *peerinfo,
                              struct syncargs *arg, uuid_t my_uuid,
                              uuid_t recv_uuid, int op, dict_t *dict_out,
                              dict_t *op_ctx);

void
gd_synctask_barrier_wait (struct syncargs *args, int count);

int
gd_brick_op_phase (glusterd_op_t op, dict_t *op_ctx, dict_t *req_dict,
                   char **op_errstr);

int
glusterd_syncop_aggr_rsp_dict (glusterd_op_t op, dict_t *aggr, dict_t *rsp);

void
gd_syncargs_init (struct syncargs *args, dict_t *op_ctx);
#endif /* __RPC_SYNCOP_H */
