/*
   Copyright (c) 2012-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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

/*TODO: Need to add syncop for brick ops*/
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


static int
glusterd_syncop_aggr_rsp_dict (glusterd_op_t op, dict_t *aggr, dict_t *rsp,
                               char *op_errstr)
{
        int ret = -1;

        switch (op) {
        case GD_OP_REPLACE_BRICK:
                ret = glusterd_rb_use_rsp_dict (aggr, rsp);
                if (ret)
                        goto out;
        break;

        case GD_OP_SYNC_VOLUME:
                ret = glusterd_sync_use_rsp_dict (aggr, rsp);
                if (ret)
                        goto out;
        break;

        case GD_OP_PROFILE_VOLUME:
                ret = glusterd_profile_volume_use_rsp_dict (aggr, rsp);
                if (ret)
                        goto out;
        break;

        case GD_OP_GSYNC_SET:
                ret = glusterd_gsync_use_rsp_dict (aggr, rsp, op_errstr);
                if (ret)
                        goto out;
        break;

        case GD_OP_STATUS_VOLUME:
                ret = glusterd_volume_status_copy_to_op_ctx_dict (aggr, rsp);
                if (ret)
                        goto out;
        break;

        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
                ret = glusterd_volume_rebalance_use_rsp_dict (aggr, rsp);
                if (ret)
                        goto out;
        break;

        case GD_OP_HEAL_VOLUME:
                ret = glusterd_volume_heal_use_rsp_dict (aggr, rsp);
                if (ret)
                        goto out;

        break;

        default:
        break;
        }
out:
        return ret;
}

void
gd_sync_task_begin (dict_t *op_ctx, rpcsvc_request_t * req)
{
        int                  ret      = -1;
        dict_t              *req_dict     = NULL;
        dict_t              *rsp_dict = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;
        glusterd_peerinfo_t *tmp = NULL;
        glusterd_conf_t     *conf     = NULL;
        uuid_t               tmp_uuid = {0,};
        glusterd_op_t        op       = 0;
        int32_t              tmp_op   = 0;
        gf_boolean_t         local_locked = _gf_false;
        char                 *op_errstr = NULL;
        xlator_t             *this = NULL;
        char                 *hostname = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = dict_get_int32 (op_ctx, GD_SYNC_OPCODE_KEY, &tmp_op);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get volume "
                        "operation");
                goto out;
        }

        op = tmp_op;

        ret = -1;
        rsp_dict = dict_new ();
        if (!rsp_dict)
                goto out;

        /*  Lock everything */
        ret = glusterd_lock (MY_UUID);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to acquire lock");
                gf_asprintf (&op_errstr, "Another transaction is in progress. "
                             "Please try again after sometime.");
                goto out;
        }
        /* successful lock in local node */
        local_locked = _gf_true;

        /* storing op globally to access in synctask code paths
         * This is still acceptable, as we are performing this under
         * the 'cluster' lock*/

        glusterd_op_set_op  (op);
        INIT_LIST_HEAD (&conf->xaction_peers);
        list_for_each_entry (peerinfo, &conf->peers, uuid_list) {
                if (!peerinfo->connected)
                        continue;
                if (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                ret = gd_syncop_mgmt_lock (peerinfo->rpc,
                                           MY_UUID, tmp_uuid);
                if (ret) {
                        gf_asprintf (&op_errstr, "Another transaction could be "
                                     "in progress. Please try again after "
                                     "sometime.");
                        gf_log (this->name, GF_LOG_ERROR, "Failed to acquire "
                                "lock on peer %s", peerinfo->hostname);
                        goto out;
                } else {
                        list_add_tail (&peerinfo->op_peers_list,
                                       &conf->xaction_peers);
                }
        }

        ret = glusterd_op_build_payload (&req_dict, &op_errstr, op_ctx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                goto out;
        }

        /* stage op */
        ret = glusterd_op_stage_validate (op, req_dict, &op_errstr, rsp_dict);
        if (ret) {
                hostname = "localhost";
                goto stage_done;
        }

        list_for_each_entry (peerinfo, &conf->xaction_peers, op_peers_list) {
                ret = gd_syncop_mgmt_stage_op (peerinfo->rpc,
                                               MY_UUID, tmp_uuid,
                                               op, req_dict, &rsp_dict,
                                               &op_errstr);
                if (ret) {
                        hostname = peerinfo->hostname;
                        goto stage_done;
                }

                if (op == GD_OP_REPLACE_BRICK)
                        (void) glusterd_syncop_aggr_rsp_dict (op, op_ctx,
                                                              rsp_dict,
                                                              op_errstr);

                if (rsp_dict)
                        dict_unref (rsp_dict);
        }

stage_done:
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_STAGE_FAIL,
                        gd_op_list[op], hostname, (op_errstr) ? ":" : " ",
                        (op_errstr) ? op_errstr : " ");
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_STAGE_FAIL, hostname);
                goto out;
        }

        /* commit op */
        ret = glusterd_op_commit_perform (op, req_dict, &op_errstr, rsp_dict);
        if (ret) {
                hostname = "localhost";
                goto commit_done;
        }

        list_for_each_entry (peerinfo, &conf->xaction_peers, op_peers_list) {
                ret = gd_syncop_mgmt_commit_op (peerinfo->rpc,
                                                MY_UUID, tmp_uuid,
                                                op, req_dict, &rsp_dict,
                                                &op_errstr);
                if (ret) {
                        hostname = peerinfo->hostname;
                        goto commit_done;
                }
                (void) glusterd_syncop_aggr_rsp_dict (op, op_ctx, rsp_dict,
                                                      op_errstr);
                if (rsp_dict)
                        dict_unref (rsp_dict);
        }
commit_done:
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_COMMIT_FAIL,
                        gd_op_list[op], hostname, (op_errstr) ? ":" : " ",
                        (op_errstr) ? op_errstr : " ");
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_COMMIT_FAIL,
                                     hostname);
                goto out;
         }

        ret = 0;
out:
        if (local_locked) {
                list_for_each_entry_safe (peerinfo, tmp, &conf->xaction_peers,
                                          op_peers_list) {
                        gd_syncop_mgmt_unlock (peerinfo->rpc,
                                               MY_UUID, tmp_uuid);
                        list_del_init (&peerinfo->op_peers_list);
                }

                /* Local node should be the one to be locked first,
                   unlocked last to prevent races */
                glusterd_op_clear_op (op);
                glusterd_unlock (MY_UUID);
        }

        glusterd_op_send_cli_response (op, ret, 0, req, op_ctx, op_errstr);

        if (req_dict)
                dict_unref (req_dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        if (op_errstr)
                GF_FREE (op_errstr);

        return;
}

int32_t
glusterd_op_begin_synctask (rpcsvc_request_t *req, glusterd_op_t op,
                            void *dict)
{
        int              ret = 0;

        ret = dict_set_int32 (dict, GD_SYNC_OPCODE_KEY, op);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "dict set failed for setting operations");
                goto out;
        }

        gd_sync_task_begin (dict, req);
out:
        if (dict)
                dict_unref (dict);

        return ret;
}
