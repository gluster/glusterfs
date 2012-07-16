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

#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"

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
        call_frame_t  *frame    = NULL;

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

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
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

        frame->local = cookie;

        /* Send the msg */
        ret = rpc_clnt_submit (rpc, prog, procnum, cbkfn,
                               &iov, count, NULL, 0, iobref,
                               frame, NULL, 0, NULL, 0, NULL);

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
                         int count, void *myframe)
{
        struct syncargs           *args  = NULL;
        gd1_mgmt_cluster_lock_rsp  rsp   = {{0},};
        int                        ret   = -1;
        call_frame_t              *frame = NULL;

        frame = myframe;
        args = frame->local;
        frame->local = NULL;

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
        STACK_DESTROY (frame->root);

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
                           int count, void *myframe)
{
        struct syncargs             *args  = NULL;
        gd1_mgmt_cluster_unlock_rsp  rsp   = {{0},};
        int                          ret   = -1;
        call_frame_t              *frame = NULL;

        frame = myframe;
        args = frame->local;
        frame->local = NULL;

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
        STACK_DESTROY (frame->root);

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
                        int count, void *myframe)
{
        struct syncargs       *args  = NULL;
        gd1_mgmt_stage_op_rsp  rsp   = {{0},};
        int                    ret   = -1;
        call_frame_t              *frame = NULL;

        frame = myframe;
        args = frame->local;
        frame->local = NULL;

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
        STACK_DESTROY (frame->root);

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

        ret = dict_allocate_and_serialize (dict_out,
                                           &req.buf.buf_val, &req.buf.buf_len);
        if (ret)
                goto out;

        GD_SYNCOP (rpc, (&args), gd_syncop_stage_op_cbk,
                   &req, &gd_mgmt_prog, GLUSTERD_MGMT_STAGE_OP,
                   xdr_gd1_mgmt_stage_op_req);

        if (args.errstr && errstr)
                *errstr = args.errstr;
        else GF_FREE (args.errstr);

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
                         int count, void *myframe)
{
        struct syncargs        *args  = NULL;
        gd1_mgmt_commit_op_rsp  rsp   = {{0},};
        int                     ret   = -1;
        call_frame_t           *frame = NULL;

        frame = myframe;
        args = frame->local;
        frame->local = NULL;

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
        STACK_DESTROY (frame->root);

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

        ret = dict_allocate_and_serialize (dict_out,
                                           &req.buf.buf_val, &req.buf.buf_len);
        if (ret)
                goto out;

        GD_SYNCOP (rpc, (&args), gd_syncop_commit_op_cbk,
                   &req, &gd_mgmt_prog, GLUSTERD_MGMT_COMMIT_OP,
                   xdr_gd1_mgmt_commit_op_req);

        if (args.errstr && errstr)
                *errstr = args.errstr;
        else GF_FREE (args.errstr);

        if (args.dict && dict_in)
                *dict_in = args.dict;
        else if (args.dict)
                dict_unref (args.dict);

        uuid_copy (recv_uuid, args.uuid);

out:
        errno = args.op_errno;
        return args.op_ret;

}


int
gd_sync_task_begin (void *data)
{
        int                  ret      = -1;
        dict_t              *dict     = NULL;
        dict_t              *rsp_dict = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;
        glusterd_conf_t     *conf     = NULL;
        uuid_t               tmp_uuid = {0,};
        char                *errstr   = NULL;
        glusterd_op_t        op       = 0;
        int32_t              tmp_op   = 0;
        gf_boolean_t         local_locked = _gf_false;

        conf = THIS->private;

        dict = data;

        ret = dict_get_int32 (dict, GD_SYNC_OPCODE_KEY, &tmp_op);
        if (ret)
                goto out;

        op = tmp_op;

        ret = -1;
        rsp_dict = dict_new ();
        if (!rsp_dict)
                goto out;

        /*  Lock everything */
        ret = glusterd_lock (conf->uuid);
        if (ret)
                goto out;
        /* successful lock in local node */
        local_locked = _gf_true;

        list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                ret = gd_syncop_mgmt_lock (peerinfo->rpc,
                                           conf->uuid, tmp_uuid);
                if (ret)
                        goto out;
                /* TODO: Only on lock successful nodes it should unlock */
        }

        /* stage op */
        ret = glusterd_op_stage_validate (op, dict, &errstr, rsp_dict);
        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                ret = gd_syncop_mgmt_stage_op (peerinfo->rpc,
                                               conf->uuid, tmp_uuid,
                                               op, dict, &rsp_dict, &errstr);
                if (ret) {
                        if (errstr)
                                ret = dict_set_dynstr (dict, "error", errstr);

                        ret = -1;
                        goto out;
                }
        }

        /* commit op */
        ret = glusterd_op_commit_perform (op, dict, &errstr, rsp_dict);
        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                ret = gd_syncop_mgmt_commit_op (peerinfo->rpc,
                                                conf->uuid, tmp_uuid,
                                                op, dict, &rsp_dict, &errstr);
                if (ret) {
                        if (errstr)
                                ret = dict_set_dynstr (dict, "error", errstr);

                        ret = -1;
                        goto out;
                }
        }

        ret = 0;
out:
        if (local_locked) {
                /* unlock everything as we help successful local lock */
                list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                        /* No need to check the error code, as it is possible
                           that if 'lock' on few nodes failed, it would come
                           here, and unlock would fail on nodes where lock
                           never was sent */
                        gd_syncop_mgmt_unlock (peerinfo->rpc,
                                               conf->uuid, tmp_uuid);
                }

                /* Local node should be the one to be locked first,
                   unlocked last to prevent races */
                glusterd_unlock (conf->uuid);
        }

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

int
gd_sync_task_completion (int op_ret, call_frame_t *sync_frame, void *data)
{
        int               ret    = 0;
        dict_t           *dict   = NULL;
        rpcsvc_request_t *req    = NULL;
        int32_t           tmp_op = 0;
        glusterd_op_t     op     = 0;

        dict = data;

        req = sync_frame->local;
        sync_frame->local = NULL;

        ret = dict_get_int32 (dict, GD_SYNC_OPCODE_KEY, &tmp_op);
        if (ret)
                goto out;
        op = tmp_op;

        ret = glusterd_op_send_cli_response (op, op_ret, 0, req, NULL,
                                             "operation failed");

out:
        if (dict)
                dict_unref (dict);

        STACK_DESTROY (sync_frame->root);

        return ret;
}


int32_t
glusterd_op_begin_synctask (rpcsvc_request_t *req, glusterd_op_t op,
                            void *dict)
{
        int              ret = 0;
        call_frame_t    *dummy_frame = NULL;
        glusterfs_ctx_t *ctx = NULL;

        dummy_frame = create_frame (THIS, THIS->ctx->pool);
        if (!dummy_frame)
                goto out;

        dummy_frame->local = req;

        ret = dict_set_int32 (dict, GD_SYNC_OPCODE_KEY, op);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "dict set failed for setting operations");
                goto out;
        }

        ctx = THIS->ctx;

        ret = synctask_new (ctx->env, gd_sync_task_begin,
                            gd_sync_task_completion,
                            dummy_frame, dict);
out:
        return ret;
}
