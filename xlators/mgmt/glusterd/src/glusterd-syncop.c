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

/* rpc related syncops */
#include "syncop.h"
#include "rpc-clnt.h"
#include "protocol-common.h"
#include "xdr-generic.h"
#include "glusterd1-xdr.h"
#include "glusterd-syncop.h"

int
gd_syncop_submit_request (struct rpc_clnt *rpc, void *req,
                          void *cookie, rpc_clnt_prog_t *prog,
                          int procnum, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        int            ret      = -1;
        struct iobuf  *iobuf    = NULL;
        struct iobref *iobref   = NULL;
        int            count    = 0;
        struct iovec   iov      = {0, };
        ssize_t        req_size = 0;

        GF_ASSERT (rpc);
        if (!req)
                goto out;

        req_size = xdr_sizeof (xdrproc, req);
        iobuf = iobuf_get2 (rpc->ctx->iobuf_pool, req_size);
        if (!iobuf)
                goto out;

        iobref = iobref_new ();
        if (!iobref)
                goto out;

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = iobuf_pagesize (iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic (iov, req, xdrproc);
        if (ret == -1)
                goto out;

        iov.iov_len = ret;
        count = 1;

        /* Send the msg */
        ret = rpc_clnt_submit (rpc, prog, procnum, cbkfn,
                               &iov, count, NULL, 0, iobref,
                               (call_frame_t *)cookie, NULL, 0, NULL, 0, NULL);

        /* TODO: do we need to start ping also? */

out:
        iobref_unref (iobref);
        iobuf_unref (iobuf);

        return ret;
}

/* Defined in glusterd-rpc-ops.c */
extern struct rpc_clnt_program gd_mgmt_prog;

int32_t
gd_syncop_mgmt_lock_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *cookie)
{
        struct syncargs           *args  = NULL;
        gd1_mgmt_cluster_lock_rsp  rsp   = {{0},};
        int                        ret   = -1;

        args = cookie;

        /* initialize */
        args->op_ret   = -1;
        args->op_errno = EINVAL;

        if (-1 == req->rpc_status) {
                args->op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);
        if (ret < 0) {
                goto out;
        }

        args->op_ret = rsp.op_ret;
        args->op_errno = rsp.op_errno;

        uuid_copy (args->uuid, rsp.uuid);

out:
        __wake (args);

        return 0;
}


int
gd_syncop_mgmt_lock (struct rpc_clnt *rpc, uuid_t my_uuid, uuid_t recv_uuid)
{
        struct syncargs           args = {0, };
        gd1_mgmt_cluster_lock_req req  = {{0},};

        uuid_copy (req.uuid, my_uuid);

        args.op_ret = -1;
        args.op_errno = ENOTCONN;

        GD_SYNCOP (rpc, (&args), gd_syncop_mgmt_lock_cbk,
                   &req, &gd_mgmt_prog, GLUSTERD_MGMT_CLUSTER_LOCK,
                   xdr_gd1_mgmt_cluster_lock_req);

        if (!args.op_ret)
                uuid_copy (recv_uuid, args.uuid);

        errno = args.op_errno;
        return args.op_ret;

}

int32_t
gd_syncop_mgmt_unlock_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *cookie)
{
        struct syncargs             *args  = NULL;
        gd1_mgmt_cluster_unlock_rsp  rsp   = {{0},};
        int                          ret   = -1;

        args = cookie;

        /* initialize */
        args->op_ret   = -1;
        args->op_errno = EINVAL;

        if (-1 == req->rpc_status) {
                args->op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);
        if (ret < 0) {
                goto out;
        }

        args->op_ret = rsp.op_ret;
        args->op_errno = rsp.op_errno;

        uuid_copy (args->uuid, rsp.uuid);

out:
        __wake (args);

        return 0;
}


int
gd_syncop_mgmt_unlock (struct rpc_clnt *rpc, uuid_t my_uuid, uuid_t recv_uuid)
{
        struct syncargs             args = {0, };
        gd1_mgmt_cluster_unlock_req req  = {{0},};

        uuid_copy (req.uuid, my_uuid);

        args.op_ret = -1;
        args.op_errno = ENOTCONN;

        GD_SYNCOP (rpc, (&args), gd_syncop_mgmt_unlock_cbk,
                   &req, &gd_mgmt_prog, GLUSTERD_MGMT_CLUSTER_UNLOCK,
                   xdr_gd1_mgmt_cluster_unlock_req);

        if (!args.op_ret)
                uuid_copy (recv_uuid, args.uuid);

        errno = args.op_errno;
        return args.op_ret;

}

int32_t
gd_syncop_stage_op_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *cookie)
{
        struct syncargs       *args  = NULL;
        gd1_mgmt_stage_op_rsp  rsp   = {{0},};
        int                    ret   = -1;

        args = cookie;

        /* initialize */
        args->op_ret   = -1;
        args->op_errno = EINVAL;

        if (-1 == req->rpc_status) {
                args->op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);
        if (ret < 0) {
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                args->dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &args->dict);
                if (ret < 0) {
                        GF_FREE (rsp.dict.dict_val);
                        goto out;
                } else {
                        args->dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

        args->op_ret = rsp.op_ret;
        args->op_errno = rsp.op_errno;

        uuid_copy (args->uuid, rsp.uuid);

        args->errstr = gf_strdup (rsp.op_errstr);

out:
        __wake (args);

        return 0;
}


int
gd_syncop_mgmt_stage_op (struct rpc_clnt *rpc, uuid_t my_uuid, uuid_t recv_uuid,
                         int op, dict_t *dict_out, dict_t **dict_in,
                         char **errstr)
{
        struct syncargs       args = {0, };
        gd1_mgmt_stage_op_req req  = {{0},};
        int                   ret  = 0;

        uuid_copy (req.uuid, my_uuid);
        req.op = op;

        args.op_ret = -1;
        args.op_errno = ENOTCONN;

        ret = dict_allocate_and_serialize (dict_out, &req.buf.buf_val,
                                           (size_t *)&req.buf.buf_len);
        if (ret)
                goto out;

        GD_SYNCOP (rpc, (&args), gd_syncop_stage_op_cbk,
                   &req, &gd_mgmt_prog, GLUSTERD_MGMT_STAGE_OP,
                   xdr_gd1_mgmt_stage_op_req);

        if (args.errstr && errstr)
                *errstr = args.errstr;
        else if (args.errstr)
                GF_FREE (args.errstr);

        if (args.dict && dict_in)
                *dict_in = args.dict;
        else if (args.dict)
                dict_unref (args.dict);

        uuid_copy (recv_uuid, args.uuid);
out:
        errno = args.op_errno;
        return args.op_ret;

}

int32_t
gd_syncop_commit_op_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *cookie)
{
        struct syncargs        *args  = NULL;
        gd1_mgmt_commit_op_rsp  rsp   = {{0},};
        int                     ret   = -1;

        args = cookie;

        /* initialize */
        args->op_ret   = -1;
        args->op_errno = EINVAL;

        if (-1 == req->rpc_status) {
                args->op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);
        if (ret < 0) {
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                args->dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &args->dict);
                if (ret < 0) {
                        GF_FREE (rsp.dict.dict_val);
                        goto out;
                } else {
                        args->dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

        args->op_ret = rsp.op_ret;
        args->op_errno = rsp.op_errno;

        uuid_copy (args->uuid, rsp.uuid);

        args->errstr = gf_strdup (rsp.op_errstr);

out:
        __wake (args);

        return 0;
}


int
gd_syncop_mgmt_commit_op (struct rpc_clnt *rpc, uuid_t my_uuid, uuid_t recv_uuid,
                          int op, dict_t *dict_out, dict_t **dict_in,
                          char **errstr)
{
        struct syncargs        args = {0, };
        gd1_mgmt_commit_op_req req  = {{0},};
        int                    ret  = 0;

        uuid_copy (req.uuid, my_uuid);
        req.op = op;

        args.op_ret = -1;
        args.op_errno = ENOTCONN;

        ret = dict_allocate_and_serialize (dict_out, &req.buf.buf_val,
                                           (size_t *)&req.buf.buf_len);
        if (ret)
                goto out;

        GD_SYNCOP (rpc, (&args), gd_syncop_commit_op_cbk,
                   &req, &gd_mgmt_prog, GLUSTERD_MGMT_COMMIT_OP,
                   xdr_gd1_mgmt_commit_op_req);

        if (args.errstr && errstr)
                *errstr = args.errstr;
        else if (args.errstr)
                GF_FREE (args.errstr);

        if (args.dict && dict_in)
                *dict_in = args.dict;
        else if (args.dict)
                dict_unref (args.dict);

        uuid_copy (recv_uuid, args.uuid);

out:
        errno = args.op_errno;
        return args.op_ret;

}
