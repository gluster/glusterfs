/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
/* rpc related syncops */
#include "rpc-clnt.h"
#include "protocol-common.h"
#include "xdr-generic.h"
#include "glusterd1-xdr.h"
#include "glusterd-syncop.h"

#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-locks.h"
#include "glusterd-mgmt.h"
#include "glusterd-op-sm.h"
#include "glusterd-messages.h"

static int
glusterd_mgmt_v3_null (rpcsvc_request_t *req)
{
        return 0;
}

static int
glusterd_mgmt_v3_lock_send_resp (rpcsvc_request_t *req, int32_t status,
                                 uint32_t op_errno)
{

        gd1_mgmt_v3_lock_rsp          rsp   = {{0},};
        int                           ret   = -1;
        xlator_t                     *this  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        if (rsp.op_ret)
                rsp.op_errno = op_errno;

        glusterd_get_uuid (&rsp.uuid);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_lock_rsp);

        gf_msg_debug (this->name, 0,
                "Responded to mgmt_v3 lock, ret: %d", ret);

        return ret;
}

static int
glusterd_synctasked_mgmt_v3_lock (rpcsvc_request_t *req,
                                gd1_mgmt_v3_lock_req *lock_req,
                                glusterd_op_lock_ctx_t *ctx)
{
        int32_t                         ret         = -1;
        xlator_t                       *this        = NULL;
        uint32_t                        op_errno    = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (ctx);
        GF_ASSERT (ctx->dict);

        /* Trying to acquire multiple mgmt_v3 locks */
        ret = glusterd_multiple_mgmt_v3_lock (ctx->dict, ctx->uuid, &op_errno);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_LOCK_GET_FAIL,
                        "Failed to acquire mgmt_v3 locks for %s",
                         uuid_utoa (ctx->uuid));

        ret = glusterd_mgmt_v3_lock_send_resp (req, ret, op_errno);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_op_state_machine_mgmt_v3_lock (rpcsvc_request_t *req,
                                       gd1_mgmt_v3_lock_req *lock_req,
                                       glusterd_op_lock_ctx_t *ctx)
{
        int32_t                         ret         = -1;
        xlator_t                       *this        = NULL;
        glusterd_op_info_t              txn_op_info = {{0},};

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        glusterd_txn_opinfo_init (&txn_op_info, NULL, &lock_req->op, ctx->dict,
                                  req);

        ret = glusterd_set_txn_opinfo (&lock_req->txn_id, &txn_op_info);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OPINFO_SET_FAIL,
                        "Unable to set transaction's opinfo");
                goto out;
        }

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK,
                                           &lock_req->txn_id, ctx);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_EVENT_LOCK_FAIL,
                        "Failed to inject event GD_OP_EVENT_LOCK");

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_handle_mgmt_v3_lock_fn (rpcsvc_request_t *req)
{
        gd1_mgmt_v3_lock_req    lock_req        = {{0},};
        int32_t                 ret             = -1;
        glusterd_op_lock_ctx_t *ctx             = NULL;
        xlator_t               *this            = NULL;
        gf_boolean_t            is_synctasked   = _gf_false;
        gf_boolean_t            free_ctx        = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &lock_req,
                              (xdrproc_t)xdr_gd1_mgmt_v3_lock_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode lock "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received mgmt_v3 lock req "
                "from uuid: %s", uuid_utoa (lock_req.uuid));

        if (glusterd_peerinfo_find_by_uuid (lock_req.uuid) == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (lock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);
        if (!ctx) {
                ret = -1;
                goto out;
        }

        gf_uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ctx->dict = dict_new ();
        if (!ctx->dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (lock_req.dict.dict_val,
                                lock_req.dict.dict_len, &ctx->dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        is_synctasked = dict_get_str_boolean (ctx->dict,
                                              "is_synctasked", _gf_false);
        if (is_synctasked) {
                ret = glusterd_synctasked_mgmt_v3_lock (req, &lock_req, ctx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_LOCK_GET_FAIL,
                                "Failed to acquire mgmt_v3_locks");
                        /* Ignore the return code, as it shouldn't be propagated
                         * from the handler function so as to avoid double
                         * deletion of the req
                         */
                        ret = 0;
                }

                /* The above function does not take ownership of ctx.
                 * Therefore we need to free the ctx explicitly. */
                free_ctx = _gf_true;
        }
        else {
                /* Shouldn't ignore the return code here, and it should
                 * be propagated from the handler function as in failure
                 * case it doesn't delete the req object
                 */
                ret = glusterd_op_state_machine_mgmt_v3_lock (req, &lock_req,
                                                              ctx);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_LOCK_GET_FAIL,
                                "Failed to acquire mgmt_v3_locks");
        }

out:

        if (ctx && (ret || free_ctx)) {
                if (ctx->dict)
                        dict_unref (ctx->dict);

                GF_FREE (ctx);
        }

        free (lock_req.dict.dict_val);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_mgmt_v3_pre_validate_send_resp (rpcsvc_request_t   *req,
                                         int32_t op, int32_t status,
                                         char *op_errstr, dict_t *rsp_dict,
                                         uint32_t op_errno)
{
        gd1_mgmt_v3_pre_val_rsp          rsp      = {{0},};
        int                             ret      = -1;
        xlator_t                       *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        rsp.op_errno = op_errno;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_pre_val_rsp);

        GF_FREE (rsp.dict.dict_val);
out:
        gf_msg_debug (this->name, 0,
                "Responded to pre validation, ret: %d", ret);
        return ret;
}

static int
glusterd_handle_pre_validate_fn (rpcsvc_request_t *req)
{
        int32_t                         ret       = -1;
        gd1_mgmt_v3_pre_val_req         op_req    = {{0},};
        xlator_t                       *this      = NULL;
        char                           *op_errstr = NULL;
        dict_t                         *dict      = NULL;
        dict_t                         *rsp_dict  = NULL;
        uint32_t                        op_errno  = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_v3_pre_val_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL,
                        "Failed to decode pre validation "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_peerinfo_find_by_uuid (op_req.uuid) == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (op_req.dict.dict_val,
                                op_req.dict.dict_len, &dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to get new dictionary");
                return -1;
        }

        ret = gd_mgmt_v3_pre_validate_fn (op_req.op, dict, &op_errstr,
                                          rsp_dict, &op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL,
                        "Pre Validation failed on operation %s",
                        gd_op_list[op_req.op]);
        }

        ret = glusterd_mgmt_v3_pre_validate_send_resp (req, op_req.op,
                                                       ret, op_errstr,
                                                       rsp_dict, op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_OP_RESP_FAIL,
                        "Failed to send Pre Validation "
                        "response for operation %s",
                        gd_op_list[op_req.op]);
                goto out;
        }

out:
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        free (op_req.dict.dict_val);

        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        /* Return 0 from handler to avoid double deletion of req obj */
        return 0;
}

static int
glusterd_mgmt_v3_brick_op_send_resp (rpcsvc_request_t   *req,
                                    int32_t op, int32_t status,
                                    char *op_errstr, dict_t *rsp_dict)
{
        gd1_mgmt_v3_brick_op_rsp         rsp      = {{0},};
        int                             ret      = -1;
        xlator_t                       *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_brick_op_rsp);

        GF_FREE (rsp.dict.dict_val);
out:
        gf_msg_debug (this->name, 0,
                "Responded to brick op, ret: %d", ret);
        return ret;
}

static int
glusterd_handle_brick_op_fn (rpcsvc_request_t *req)
{
        int32_t                         ret       = -1;
        gd1_mgmt_v3_brick_op_req         op_req    = {{0},};
        xlator_t                       *this      = NULL;
        char                           *op_errstr = NULL;
        dict_t                         *dict      = NULL;
        dict_t                         *rsp_dict  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_v3_brick_op_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode brick op "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_peerinfo_find_by_uuid (op_req.uuid) == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (op_req.dict.dict_val,
                                op_req.dict.dict_len, &dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to get new dictionary");
                return -1;
        }

        ret = gd_mgmt_v3_brick_op_fn (op_req.op, dict, &op_errstr,
                                     rsp_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_OP_FAIL,
                        "Brick Op failed on operation %s",
                        gd_op_list[op_req.op]);
        }

        ret = glusterd_mgmt_v3_brick_op_send_resp (req, op_req.op,
                                                  ret, op_errstr,
                                                  rsp_dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALD_RESP_FAIL,
                        "Failed to send brick op "
                        "response for operation %s",
                        gd_op_list[op_req.op]);
                goto out;
        }

out:
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        free (op_req.dict.dict_val);

        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        /* Return 0 from handler to avoid double deletion of req obj */
        return 0;
}

static int
glusterd_mgmt_v3_commit_send_resp (rpcsvc_request_t   *req,
                                   int32_t op, int32_t status,
                                   char *op_errstr, uint32_t op_errno,
                                   dict_t *rsp_dict)
{
        gd1_mgmt_v3_commit_rsp           rsp      = {{0},};
        int                             ret      = -1;
        xlator_t                       *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        rsp.op_errno = op_errno;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_commit_rsp);

        GF_FREE (rsp.dict.dict_val);
out:
        gf_msg_debug (this->name, 0, "Responded to commit, ret: %d", ret);
        return ret;
}

static int
glusterd_handle_commit_fn (rpcsvc_request_t *req)
{
        int32_t                         ret       = -1;
        gd1_mgmt_v3_commit_req           op_req    = {{0},};
        xlator_t                       *this      = NULL;
        char                           *op_errstr = NULL;
        dict_t                         *dict      = NULL;
        dict_t                         *rsp_dict  = NULL;
        uint32_t                        op_errno  = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_v3_commit_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode commit "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_peerinfo_find_by_uuid (op_req.uuid) == NULL) {
                gf_msg  (this->name, GF_LOG_WARNING, 0,
                         GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (op_req.dict.dict_val,
                                op_req.dict.dict_len, &dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to get new dictionary");
                return -1;
        }

        ret = gd_mgmt_v3_commit_fn (op_req.op, dict, &op_errstr,
                                    &op_errno, rsp_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_OP_FAIL,
                        "commit failed on operation %s",
                        gd_op_list[op_req.op]);
        }

        ret = glusterd_mgmt_v3_commit_send_resp (req, op_req.op,
                                                 ret, op_errstr,
                                                 op_errno, rsp_dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_OP_RESP_FAIL,
                        "Failed to send commit "
                        "response for operation %s",
                        gd_op_list[op_req.op]);
                goto out;
        }

out:
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        free (op_req.dict.dict_val);

        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        /* Return 0 from handler to avoid double deletion of req obj */
        return 0;
}

static int
glusterd_mgmt_v3_post_validate_send_resp (rpcsvc_request_t   *req,
                                         int32_t op, int32_t status,
                                         char *op_errstr, dict_t *rsp_dict)
{
        gd1_mgmt_v3_post_val_rsp         rsp      = {{0},};
        int                             ret      = -1;
        xlator_t                       *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_post_val_rsp);

        GF_FREE (rsp.dict.dict_val);
out:
        gf_msg_debug (this->name, 0,
                "Responded to post validation, ret: %d", ret);
        return ret;
}

static int
glusterd_handle_post_validate_fn (rpcsvc_request_t *req)
{
        int32_t                         ret       = -1;
        gd1_mgmt_v3_post_val_req         op_req    = {{0},};
        xlator_t                       *this      = NULL;
        char                           *op_errstr = NULL;
        dict_t                         *dict      = NULL;
        dict_t                         *rsp_dict  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_v3_post_val_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL,
                        "Failed to decode post validation "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_peerinfo_find_by_uuid (op_req.uuid) == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (op_req.dict.dict_val,
                                op_req.dict.dict_len, &dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to get new dictionary");
                return -1;
        }

        ret = gd_mgmt_v3_post_validate_fn (op_req.op, op_req.op_ret, dict,
                                           &op_errstr, rsp_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_POST_VALIDATION_FAIL,
                        "Post Validation failed on operation %s",
                        gd_op_list[op_req.op]);
        }

        ret = glusterd_mgmt_v3_post_validate_send_resp (req, op_req.op,
                                                       ret, op_errstr,
                                                       rsp_dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_OP_RESP_FAIL,
                        "Failed to send Post Validation "
                        "response for operation %s",
                        gd_op_list[op_req.op]);
                goto out;
        }

out:
        if (op_errstr && (strcmp (op_errstr, "")))
                GF_FREE (op_errstr);

        free (op_req.dict.dict_val);

        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        /* Return 0 from handler to avoid double deletion of req obj */
        return 0;
}

static int
glusterd_mgmt_v3_unlock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_v3_unlock_rsp          rsp   = {{0},};
        int                             ret   = -1;
        xlator_t                       *this  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        rsp.op_ret = status;
        if (rsp.op_ret)
                rsp.op_errno = errno;

        glusterd_get_uuid (&rsp.uuid);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_v3_unlock_rsp);

        gf_msg_debug (this->name, 0,
                "Responded to mgmt_v3 unlock, ret: %d", ret);

        return ret;
}

static int
glusterd_syctasked_mgmt_v3_unlock (rpcsvc_request_t *req,
                                  gd1_mgmt_v3_unlock_req *unlock_req,
                                  glusterd_op_lock_ctx_t *ctx)
{
        int32_t                         ret         = -1;
        xlator_t                       *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (ctx);

        /* Trying to release multiple mgmt_v3 locks */
        ret = glusterd_multiple_mgmt_v3_unlock (ctx->dict, ctx->uuid);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_UNLOCK_FAIL,
                        "Failed to release mgmt_v3 locks for %s",
                        uuid_utoa(ctx->uuid));
        }

        ret = glusterd_mgmt_v3_unlock_send_resp (req, ret);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}


static int
glusterd_op_state_machine_mgmt_v3_unlock (rpcsvc_request_t *req,
                                         gd1_mgmt_v3_unlock_req *lock_req,
                                         glusterd_op_lock_ctx_t *ctx)
{
        int32_t                           ret      = -1;
        xlator_t                         *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_UNLOCK,
                                           &lock_req->txn_id, ctx);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_EVENT_UNLOCK_FAIL,
                        "Failed to inject event GD_OP_EVENT_UNLOCK");

        glusterd_friend_sm ();
        glusterd_op_sm ();

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_handle_mgmt_v3_unlock_fn (rpcsvc_request_t *req)
{
        gd1_mgmt_v3_unlock_req  lock_req        = {{0},};
        int32_t                 ret             = -1;
        glusterd_op_lock_ctx_t *ctx             = NULL;
        xlator_t               *this            = NULL;
        gf_boolean_t            is_synctasked   = _gf_false;
        gf_boolean_t            free_ctx        = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &lock_req,
                              (xdrproc_t)xdr_gd1_mgmt_v3_unlock_req);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode unlock "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg_debug (this->name, 0, "Received volume unlock req "
                "from uuid: %s", uuid_utoa (lock_req.uuid));

        if (glusterd_peerinfo_find_by_uuid (lock_req.uuid) == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_PEER_NOT_FOUND, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (lock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);
        if (!ctx) {
                ret = -1;
                goto out;
        }

        gf_uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ctx->dict = dict_new ();
        if (!ctx->dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (lock_req.dict.dict_val,
                                lock_req.dict.dict_len, &ctx->dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize the dictionary");
                goto out;
        }

        is_synctasked = dict_get_str_boolean (ctx->dict,
                                              "is_synctasked", _gf_false);
        if (is_synctasked) {
                ret = glusterd_syctasked_mgmt_v3_unlock (req, &lock_req, ctx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release mgmt_v3_locks");
                        /* Ignore the return code, as it shouldn't be propagated
                         * from the handler function so as to avoid double
                         * deletion of the req
                         */
                        ret = 0;
                }

                /* The above function does not take ownership of ctx.
                 * Therefore we need to free the ctx explicitly. */
                free_ctx = _gf_true;
        }
        else {
                /* Shouldn't ignore the return code here, and it should
                 * be propagated from the handler function as in failure
                 * case it doesn't delete the req object
                 */
                ret = glusterd_op_state_machine_mgmt_v3_unlock (req, &lock_req,
                                                                ctx);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release mgmt_v3_locks");
        }

out:

        if (ctx && (ret || free_ctx)) {
                if (ctx->dict)
                        dict_unref (ctx->dict);

                GF_FREE (ctx);
        }

        free (lock_req.dict.dict_val);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_mgmt_v3_lock (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            glusterd_handle_mgmt_v3_lock_fn);
}

static int
glusterd_handle_pre_validate (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            glusterd_handle_pre_validate_fn);
}

static int
glusterd_handle_brick_op (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            glusterd_handle_brick_op_fn);
}

static int
glusterd_handle_commit (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            glusterd_handle_commit_fn);
}

static int
glusterd_handle_post_validate (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            glusterd_handle_post_validate_fn);
}

int
glusterd_handle_mgmt_v3_unlock (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            glusterd_handle_mgmt_v3_unlock_fn);
}

rpcsvc_actor_t gd_svc_mgmt_v3_actors[GLUSTERD_MGMT_V3_MAXVALUE] = {
        [GLUSTERD_MGMT_V3_NULL]          = { "NULL",           GLUSTERD_MGMT_V3_NULL,          glusterd_mgmt_v3_null,         NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_V3_LOCK]          = { "MGMT_V3_LOCK",   GLUSTERD_MGMT_V3_LOCK,          glusterd_handle_mgmt_v3_lock,   NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_V3_PRE_VALIDATE]  = { "PRE_VAL",        GLUSTERD_MGMT_V3_PRE_VALIDATE,  glusterd_handle_pre_validate,  NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_V3_BRICK_OP]      = { "BRCK_OP",        GLUSTERD_MGMT_V3_BRICK_OP,      glusterd_handle_brick_op,      NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_V3_COMMIT]        = { "COMMIT",         GLUSTERD_MGMT_V3_COMMIT,        glusterd_handle_commit,        NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_V3_POST_VALIDATE] = { "POST_VAL",       GLUSTERD_MGMT_V3_POST_VALIDATE, glusterd_handle_post_validate, NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_V3_UNLOCK]        = { "MGMT_V3_UNLOCK", GLUSTERD_MGMT_V3_UNLOCK,        glusterd_handle_mgmt_v3_unlock, NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_mgmt_v3_prog = {
        .progname  = "GlusterD svc mgmt v3",
        .prognum   = GD_MGMT_PROGRAM,
        .progver   = GD_MGMT_V3_VERSION,
        .numactors = GLUSTERD_MGMT_V3_MAXVALUE,
        .actors    = gd_svc_mgmt_v3_actors,
        .synctask  = _gf_true,
};
