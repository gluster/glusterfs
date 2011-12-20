/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
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

#ifndef __RPC_SYNCOP_H
#define __RPC_SYNCOP_H

#include "syncop.h"


/* gd_syncop_* */
#define GD_SYNCOP(rpc, stb, cbk, req, prog, procnum, xdrproc) do {      \
                int ret = 0;                                            \
                ret = gd_syncop_submit_request (rpc, req, stb,          \
                                                prog, procnum, cbk,     \
                                                (xdrproc_t)xdrproc);    \
                if (!ret)                                               \
                        __yield (stb);                                  \
        } while (0)


int gd_syncop_submit_request (struct rpc_clnt *rpc, void *req,
                               void *cookie, rpc_clnt_prog_t *prog,
                               int procnum, fop_cbk_fn_t cbkfn,
                               xdrproc_t xdrproc);


int gd_syncop_mgmt_lock (struct rpc_clnt *rpc, uuid_t my_uuid,
                          uuid_t recv_uuid);
int gd_syncop_mgmt_unlock (struct rpc_clnt *rpc, uuid_t my_uuid,
                            uuid_t recv_uuid);
int gd_syncop_mgmt_stage_op (struct rpc_clnt *rpc, uuid_t my_uuid,
                              uuid_t recv_uuid, int op, dict_t *dict_out,
                              dict_t **dict_in, char **errstr);
int gd_syncop_mgmt_commit_op (struct rpc_clnt *rpc, uuid_t my_uuid,
                               uuid_t recv_uuid, int op, dict_t *dict_out,
                               dict_t **dict_in, char **errstr);

#endif /* __RPC_SYNCOP_H */
