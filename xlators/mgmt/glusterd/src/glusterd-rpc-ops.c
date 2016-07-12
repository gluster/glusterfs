/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "rpc-clnt.h"
#include "glusterd1-xdr.h"
#include "cli1-xdr.h"

#include "xdr-generic.h"

#include "compat-errno.h"
#include "glusterd-op-sm.h"
#include "glusterd-sm.h"
#include "glusterd.h"
#include "protocol-common.h"
#include "glusterd-utils.h"
#include "common-utils.h"
#include "glusterd-messages.h"
#include "glusterd-snapshot-utils.h"
#include <sys/uio.h>


#define SERVER_PATH_MAX  (16 * 1024)


extern glusterd_op_info_t opinfo;
extern uuid_t global_txn_id;

int32_t
glusterd_op_send_cli_response (glusterd_op_t op, int32_t op_ret,
                               int32_t op_errno, rpcsvc_request_t *req,
                               void *op_ctx, char *op_errstr)
{
        int32_t         ret = -1;
        void            *cli_rsp = NULL;
        dict_t          *ctx = NULL;
        char            *free_ptr = NULL;
        glusterd_conf_t *conf = NULL;
        xdrproc_t       xdrproc = NULL;
        char            *errstr = NULL;
        int32_t         status = 0;
        int32_t         count = 0;
        gf_cli_rsp      rsp = {0,};
        xlator_t        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        GF_ASSERT (conf);

        ctx = op_ctx;

        switch (op) {
        case GD_OP_DETACH_TIER:
        case GD_OP_REMOVE_TIER_BRICK:
        case GD_OP_REMOVE_BRICK:
        {
                if (ctx)
                        ret = dict_get_str (ctx, "errstr", &errstr);
                break;
        }
        case GD_OP_RESET_VOLUME:
        {
                if (op_ret && !op_errstr)
                        errstr = "Error while resetting options";
                break;
        }
        case GD_OP_TIER_MIGRATE:
        case GD_OP_TIER_STATUS:
        case GD_OP_DETACH_TIER_STATUS:
        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
        {
                if (ctx) {
                        ret = dict_get_int32 (ctx, "status", &status);
                        if (ret) {
                                gf_msg_trace (this->name, 0,
                                        "failed to get status");
                        }
                }
                break;
        }
        case GD_OP_GSYNC_CREATE:
        case GD_OP_GSYNC_SET:
        {
               if (ctx) {
                        ret = dict_get_str (ctx, "errstr", &errstr);
                        ret = dict_set_str (ctx, "glusterd_workdir", conf->workdir);
                        /* swallow error here, that will be re-triggered in cli */

               }
               break;

        }
        case GD_OP_PROFILE_VOLUME:
        {
                if (ctx && dict_get_int32 (ctx, "count", &count)) {
                        ret = dict_set_int32 (ctx, "count", 0);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "failed to set count in dictionary");
                        }
                }
                break;
        }
        case GD_OP_START_BRICK:
        case GD_OP_STOP_BRICK:
        {
                gf_msg_debug (this->name, 0, "op '%s' not supported",
                        gd_op_list[op]);
                break;
        }
        case GD_OP_NONE:
        case GD_OP_MAX:
        {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_OP_UNSUPPORTED, "invalid operation");
                break;
        }
        case GD_OP_CREATE_VOLUME:
        case GD_OP_START_VOLUME:
        case GD_OP_STOP_VOLUME:
        case GD_OP_DELETE_VOLUME:
        case GD_OP_DEFRAG_VOLUME:
        case GD_OP_ADD_BRICK:
        case GD_OP_LOG_ROTATE:
        case GD_OP_SYNC_VOLUME:
        case GD_OP_STATEDUMP_VOLUME:
        case GD_OP_REPLACE_BRICK:
        case GD_OP_STATUS_VOLUME:
        case GD_OP_SET_VOLUME:
        case GD_OP_GANESHA:
        case GD_OP_LIST_VOLUME:
        case GD_OP_CLEARLOCKS_VOLUME:
        case GD_OP_HEAL_VOLUME:
        case GD_OP_QUOTA:
        case GD_OP_SNAP:
        case GD_OP_BARRIER:
        case GD_OP_BITROT:
        case GD_OP_SCRUB_STATUS:
        case GD_OP_SCRUB_ONDEMAND:
        case GD_OP_RESET_BRICK:
        case GD_OP_MAX_OPVERSION:
        case GD_OP_TIER_START_STOP:
        case GD_OP_DETACH_NOT_STARTED:

        {
                /*nothing specific to be done*/
                break;
        }
        case GD_OP_COPY_FILE:
        {
               if (ctx)
                        ret = dict_get_str (ctx, "errstr", &errstr);
               break;
        }
        case GD_OP_SYS_EXEC:
        {
               if (ctx) {
                        ret = dict_get_str (ctx, "errstr", &errstr);
                        ret = dict_set_str (ctx, "glusterd_workdir",
                                            conf->workdir);
               }
               break;
        }
        }

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;

        if (errstr)
                rsp.op_errstr = errstr;
        else if (op_errstr)
                rsp.op_errstr = op_errstr;

        if (!rsp.op_errstr)
                rsp.op_errstr = "";

        if (ctx) {
                ret = dict_allocate_and_serialize (ctx, &rsp.dict.dict_val,
                                                   &rsp.dict.dict_len);
                if (ret < 0 )
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SERL_LENGTH_GET_FAIL, "failed to "
                                "serialize buffer");
                else
                        free_ptr = rsp.dict.dict_val;
        }

        /* needed by 'rebalance status' */
        if (status)
                rsp.op_errno = status;

        cli_rsp = &rsp;
        xdrproc = (xdrproc_t) xdr_gf_cli_rsp;

        glusterd_to_cli (req, cli_rsp, NULL, 0, NULL,
                         xdrproc, ctx);
        ret = 0;

        GF_FREE (free_ptr);
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_big_locked_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *myframe, fop_cbk_fn_t fn)
{
        glusterd_conf_t *priv = THIS->private;
        int             ret   = -1;

        synclock_lock (&priv->big_lock);
        ret = fn (req, iov, count, myframe);
        synclock_unlock (&priv->big_lock);

        return ret;
}

int
__glusterd_probe_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        gd1_mgmt_probe_rsp          rsp      = {{0},};
        int                         ret      = 0;
        glusterd_peerinfo_t        *peerinfo = NULL;
        glusterd_friend_sm_event_t *event    = NULL;
        glusterd_probe_ctx_t       *ctx      = NULL;
        xlator_t                   *this     = NULL;
        glusterd_conf_t            *conf     = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_probe_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_PROBE_REQ_RESP_RCVD,
                "Received probe resp from uuid: %s, host: %s",
                uuid_utoa (rsp.uuid), rsp.hostname);
        if (rsp.op_ret != 0) {
                ctx = ((call_frame_t *)myframe)->local;
                ((call_frame_t *)myframe)->local = NULL;

                GF_ASSERT (ctx);

                if (ctx->req) {
                        glusterd_xfer_cli_probe_resp (ctx->req, rsp.op_ret,
                                                      rsp.op_errno,
                                                      rsp.op_errstr,
                                                      ctx->hostname, ctx->port,
                                                      ctx->dict);
                }

                glusterd_destroy_probe_ctx (ctx);
                (void) glusterd_friend_remove (rsp.uuid, rsp.hostname);
                ret = rsp.op_ret;
                goto out;
        }

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (rsp.uuid, rsp.hostname);
        if (peerinfo == NULL) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                       GD_MSG_PEER_NOT_FOUND, "Could not find peerd %s(%s)",
                        rsp.hostname, uuid_utoa (rsp.uuid));
                goto unlock;
        }

        /*
         * In the case of a fresh probe rsp.uuid and peerinfo.uuid will not
         * match, as peerinfo->uuid will be NULL.
         *
         * In the case of a peer probe being done to add a new network to a
         * peer, rsp.uuid will match an existing peerinfo.uuid. If we have this
         * stage it means that the current address/hostname being used isn't
         * present in the found peerinfo. If it were, we would have found out
         * earlier in the probe process and wouldn't even reach till here. So,
         * we need to add the new hostname to the peer.
         *
         * This addition should only be done for cluster op-version >=
         * GD_OP_VERSION_3_6_0 as address lists are only supported from then on.
         * Also, this update should only be done when an explicit CLI probe
         * command was used to begin the probe process.
         */
        if ((conf->op_version >= GD_OP_VERSION_3_6_0) &&
            (gf_uuid_compare (rsp.uuid, peerinfo->uuid) == 0)) {
                ctx = ((call_frame_t *)myframe)->local;
                /* Presence of ctx->req implies this probe was started by a cli
                 * probe command
                 */
                if (ctx->req == NULL)
                        goto cont;

                gf_msg_debug (this->name, 0, "Adding address '%s' to "
                        "existing peer %s", rsp.hostname, uuid_utoa (rsp.uuid));

                ret = glusterd_friend_remove (NULL, rsp.hostname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STALE_PEERINFO_REMOVE_FAIL,
                                "Could not remove "
                                "stale peerinfo with name %s", rsp.hostname);
                        goto reply;
                }

                ret = gd_add_address_to_peer (peerinfo, rsp.hostname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_HOSTNAME_ADD_TO_PEERLIST_FAIL,
                                "Couldn't add hostname to peer list");
                        goto reply;
                }

                /* Injecting EVENT_NEW_NAME to send update */
                ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_NEW_NAME,
                                                    &event);
                if (!ret) {
                        event->peername = gf_strdup (peerinfo->hostname);
                        gf_uuid_copy (event->peerid, peerinfo->uuid);

                        ret = glusterd_friend_sm_inject_event (event);
                }
                rsp.op_errno = GF_PROBE_FRIEND;

reply:
                ctx = ((call_frame_t *)myframe)->local;
                ((call_frame_t *)myframe)->local = NULL;

                if (!ctx) {
                        ret = -1;
                        goto unlock;
                }

                if (ctx->req) {
                        glusterd_xfer_cli_probe_resp (ctx->req, ret,
                                                      rsp.op_errno,
                                                      rsp.op_errstr,
                                                      ctx->hostname, ctx->port,
                                                      ctx->dict);
                }

                glusterd_destroy_probe_ctx (ctx);

                goto unlock;

        } else if (strncasecmp (rsp.hostname, peerinfo->hostname, 1024)) {
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        GD_MSG_HOST_PRESENT_ALREADY, "Host: %s  with uuid: %s "
                        "already present in cluster with alias hostname: %s",
                        rsp.hostname, uuid_utoa (rsp.uuid), peerinfo->hostname);

                ctx = ((call_frame_t *)myframe)->local;
                ((call_frame_t *)myframe)->local = NULL;

                if (!ctx) {
                        ret = -1;
                        goto unlock;
                }

                rsp.op_errno = GF_PROBE_FRIEND;
                if (ctx->req) {
                        glusterd_xfer_cli_probe_resp (ctx->req, rsp.op_ret,
                                                      rsp.op_errno,
                                                      rsp.op_errstr,
                                                      ctx->hostname, ctx->port,
                                                      ctx->dict);
                }

                glusterd_destroy_probe_ctx (ctx);
                (void) glusterd_friend_remove (NULL, rsp.hostname);
                ret = rsp.op_ret;

                goto unlock;
        }

cont:
        gf_uuid_copy (peerinfo->uuid, rsp.uuid);

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_INIT_FRIEND_REQ, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_NEW_FRIEND_SM_EVENT_GET_FAIL,
                         "Unable to get event");
                goto unlock;
        }

        event->peername = gf_strdup (peerinfo->hostname);
        gf_uuid_copy (event->peerid, peerinfo->uuid);

        event->ctx      = ((call_frame_t *)myframe)->local;
        ((call_frame_t *)myframe)->local = NULL;
        ret = glusterd_friend_sm_inject_event (event);


        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_PROBE_REQ_RESP_RCVD, "Received resp to probe req");

unlock:
        rcu_read_unlock ();

out:
        free (rsp.hostname);//malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));

        /* Attempt to start the state machine. Needed as no state machine could
         * be running at time this RPC reply was received
         */
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_probe_cbk (struct rpc_req *req, struct iovec *iov,
                    int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_probe_cbk);
}


int
__glusterd_friend_add_cbk (struct rpc_req * req, struct iovec *iov,
                            int count, void *myframe)
{
        gd1_mgmt_friend_rsp           rsp   = {{0},};
        int                           ret   = -1;
        glusterd_friend_sm_event_t        *event = NULL;
        glusterd_friend_sm_event_type_t    event_type = GD_FRIEND_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        int32_t                       op_ret = -1;
        int32_t                       op_errno = -1;
        glusterd_probe_ctx_t          *ctx = NULL;
        glusterd_friend_update_ctx_t  *ev_ctx = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_friend_rsp);
        if (ret < 0) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_RES_DECODE_FAIL, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_RESPONSE_INFO,
                "Received %s from uuid: %s, host: %s, port: %d",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid), rsp.hostname, rsp.port);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (rsp.uuid, rsp.hostname);
        if (peerinfo == NULL) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_RESP_FROM_UNKNOWN_PEER,
                        "received friend add response from"
                        " unknown peer uuid: %s", uuid_utoa (rsp.uuid));
                goto unlock;
        }

        if (op_ret)
                event_type = GD_FRIEND_EVENT_RCVD_RJT;
        else
                event_type = GD_FRIEND_EVENT_RCVD_ACC;

        ret = glusterd_friend_sm_new_event (event_type, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL,
                         "Unable to get event");
                goto unlock;
        }

        ev_ctx = GF_CALLOC (1, sizeof (*ev_ctx),
                                gf_gld_mt_friend_update_ctx_t);
        if (!ev_ctx) {
                ret = -1;
                goto unlock;
        }

        gf_uuid_copy (ev_ctx->uuid, rsp.uuid);
        ev_ctx->hostname = gf_strdup (rsp.hostname);

        event->peername = gf_strdup (peerinfo->hostname);
        gf_uuid_copy (event->peerid, peerinfo->uuid);
        event->ctx = ev_ctx;
        ret = glusterd_friend_sm_inject_event (event);

unlock:
        rcu_read_unlock ();
out:
        ctx = ((call_frame_t *)myframe)->local;
        ((call_frame_t *)myframe)->local = NULL;

        GF_ASSERT (ctx);

        if (ctx->req)//reverse probe doesn't have req
                ret = glusterd_xfer_cli_probe_resp (ctx->req, op_ret, op_errno,
                                                    NULL, ctx->hostname,
                                                    ctx->port, ctx->dict);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (ctx)
                glusterd_destroy_probe_ctx (ctx);
        free (rsp.hostname);//malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int
glusterd_friend_add_cbk (struct rpc_req *req, struct iovec *iov,
                    int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_friend_add_cbk);
}

int
__glusterd_friend_remove_cbk (struct rpc_req * req, struct iovec *iov,
                               int count, void *myframe)
{
        gd1_mgmt_friend_rsp             rsp   = {{0},};
        glusterd_conf_t                 *conf = NULL;
        int                             ret   = -1;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_sm_event_type_t event_type = GD_FRIEND_EVENT_NONE;
        glusterd_peerinfo_t             *peerinfo = NULL;
        int32_t                         op_ret = -1;
        int32_t                         op_errno = 0;
        glusterd_probe_ctx_t            *ctx = NULL;
        gf_boolean_t                    move_sm_now = _gf_true;

        conf  = THIS->private;
        GF_ASSERT (conf);

        ctx = ((call_frame_t *)myframe)->local;
        ((call_frame_t *)myframe)->local = NULL;
        GF_ASSERT (ctx);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                move_sm_now = _gf_false;
                goto inject;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_friend_rsp);
        if (ret < 0) {
                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                        GD_MSG_RES_DECODE_FAIL, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto respond;
        }

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_RESPONSE_INFO,
                "Received %s from uuid: %s, host: %s, port: %d",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid), rsp.hostname, rsp.port);

inject:
        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (rsp.uuid, ctx->hostname);
        if (peerinfo == NULL) {
                //can happen as part of rpc clnt connection cleanup
                //when the frame timeout happens after 30 minutes
                ret = -1;
                goto unlock;
        }

        event_type = GD_FRIEND_EVENT_REMOVE_FRIEND;

        ret = glusterd_friend_sm_new_event (event_type, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL,
                         "Unable to get event");
                goto unlock;
        }
        event->peername = gf_strdup (peerinfo->hostname);
        gf_uuid_copy (event->peerid, peerinfo->uuid);

        ret = glusterd_friend_sm_inject_event (event);

        if (ret)
                goto unlock;

        /*friend_sm would be moved on CLNT_DISCONNECT, consequently
          cleaning up peerinfo. Else, we run the risk of triggering
          a clnt_destroy within saved_frames_unwind.
        */
        op_ret = 0;

unlock:
        rcu_read_unlock ();

respond:
        ret = glusterd_xfer_cli_deprobe_resp (ctx->req, op_ret, op_errno, NULL,
                                              ctx->hostname, ctx->dict);
        if (!ret && move_sm_now) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (ctx) {
                glusterd_broadcast_friend_delete (ctx->hostname, NULL);
                glusterd_destroy_probe_ctx (ctx);
        }

        free (rsp.hostname);//malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int
glusterd_friend_remove_cbk (struct rpc_req *req, struct iovec *iov,
                    int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_friend_remove_cbk);
}

int32_t
__glusterd_friend_update_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        int                           ret    = -1;
        gd1_mgmt_friend_update_rsp    rsp    = {{0}, };
        xlator_t                      *this  = NULL;

        GF_ASSERT (req);
        this = THIS;

        if (-1 == req->rpc_status) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_FAILURE, "RPC Error");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_friend_update_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL, "Failed to serialize friend"
                        " update response");
                goto out;
        }

        ret = 0;
out:
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_RESPONSE_INFO, "Received %s from uuid: %s",
                (ret)?"RJT":"ACC", uuid_utoa (rsp.uuid));

        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int
glusterd_friend_update_cbk (struct rpc_req *req, struct iovec *iov,
                    int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_friend_update_cbk);
}

int32_t
__glusterd_cluster_lock_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        xlator_t                      *this = NULL;
        uuid_t                        *txn_id = NULL;
        glusterd_conf_t               *priv = NULL;
        char                          *err_str = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;

        if (-1 == req->rpc_status) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_NO_LOCK_RESP_FROM_PEER,  "Lock response is not "
                        "received from one of the peer");
                err_str = "Lock response is not received from one of the peer";
                glusterd_set_opinfo (err_str, ENETRESET, -1);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL, "Failed to decode "
                        "cluster lock response received from peer");
                err_str = "Failed to decode cluster lock response received from"
                          " peer";
                glusterd_set_opinfo (err_str, EINVAL, -1);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        op_ret = rsp.op_ret;

        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_LOCK_FROM_UUID_REJCT,
                        "Received lock RJT from uuid: %s",
                        uuid_utoa (rsp.uuid));
        } else {
                gf_msg_debug (this->name, 0,
                        "Received lock ACC from uuid: %s",
                        uuid_utoa (rsp.uuid));
        }

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find (rsp.uuid, NULL) == NULL);
        rcu_read_unlock ();

        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_RESP_FROM_UNKNOWN_PEER,
                        "cluster lock response received from unknown peer: %s."
                        "Ignoring response", uuid_utoa (rsp.uuid));
                err_str = "cluster lock response received from unknown peer";
                goto out;

        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
                opinfo.op_errstr = gf_strdup ("Another transaction could be in "
                                              "progress. Please try again after"
                                              " sometime.");
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

out:

        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");


        ret = glusterd_op_sm_inject_event (event_type, txn_id, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd_cluster_lock_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_cluster_lock_cbk);
}

void
glusterd_set_opinfo (char *errstr, int32_t op_errno, int32_t op_ret)
{
        opinfo.op_errstr = gf_strdup (errstr);
        opinfo.op_errno = op_errno;
        opinfo.op_ret = op_ret;
}

static int32_t
glusterd_mgmt_v3_lock_peers_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                    int count, void *myframe)
{
        gd1_mgmt_v3_lock_rsp          rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        xlator_t                      *this = NULL;
        call_frame_t                  *frame  = NULL;
        uuid_t                        *txn_id = NULL;
        char                          *err_str = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        frame = myframe;
        txn_id = frame->cookie;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_NO_LOCK_RESP_FROM_PEER, "Lock response is not "
                        "received from one of the peer");
                err_str = "Lock response is not received from one of the peer";
                glusterd_set_opinfo (err_str, ENETRESET, -1);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_lock_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL, "Failed to decode "
                        "mgmt_v3 lock response received from peer");
                err_str = "Failed to decode mgmt_v3 lock response received from"
                          " peer";
                glusterd_set_opinfo (err_str, EINVAL, -1);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        op_ret = rsp.op_ret;

        txn_id = &rsp.txn_id;

        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_LOCK_FROM_UUID_REJCT,
                        "Received mgmt_v3 lock RJT from uuid: %s",
                        uuid_utoa (rsp.uuid));
        } else {
                gf_msg_debug (this->name, 0,
                        "Received mgmt_v3 lock ACC from uuid: %s",
                        uuid_utoa (rsp.uuid));
        }

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find (rsp.uuid, NULL) == NULL);
        rcu_read_unlock ();

        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_RESP_FROM_UNKNOWN_PEER,
                        "mgmt_v3 lock response received "
                        "from unknown peer: %s. Ignoring response",
                         uuid_utoa (rsp.uuid));
                goto out;
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
                opinfo.op_errstr = gf_strdup ("Another transaction could be in "
                                              "progress. Please try again after"
                                              " sometime.");
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

out:

        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");

        ret = glusterd_op_sm_inject_event (event_type, txn_id, NULL);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        GF_FREE (frame->cookie);
        GLUSTERD_STACK_DESTROY (frame);
        return ret;
}

int32_t
glusterd_mgmt_v3_lock_peers_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        glusterd_mgmt_v3_lock_peers_cbk_fn);
}

static int32_t
glusterd_mgmt_v3_unlock_peers_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                      int count, void *myframe)
{
        gd1_mgmt_v3_unlock_rsp        rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        xlator_t                      *this = NULL;
        call_frame_t                  *frame = NULL;
        uuid_t                        *txn_id = NULL;
        char                          *err_str = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        frame = myframe;
        txn_id = frame->cookie;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                err_str = "Unlock response not received from one of the peer.";
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLUSTER_UNLOCK_FAILED,
                        "UnLock response is not received from one of the peer");
                glusterd_set_opinfo (err_str, 0, 0);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_unlock_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLUSTER_UNLOCK_FAILED,
                        "Failed to decode mgmt_v3 unlock response received from"
                        "peer");
                err_str = "Failed to decode mgmt_v3 unlock response received "
                          "from peer";
                glusterd_set_opinfo (err_str, 0, 0);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        op_ret = rsp.op_ret;

        txn_id = &rsp.txn_id;

        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_UNLOCK_FROM_UUID_REJCT,
                        "Received mgmt_v3 unlock RJT from uuid: %s",
                        uuid_utoa (rsp.uuid));
        } else {
                gf_msg_debug (this->name, 0,
                        "Received mgmt_v3 unlock ACC from uuid: %s",
                        uuid_utoa (rsp.uuid));
        }

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find (rsp.uuid, NULL) == NULL);
        rcu_read_unlock ();

        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_CLUSTER_UNLOCK_FAILED,
                        "mgmt_v3 unlock response received "
                        "from unknown peer: %s. Ignoring response",
                        uuid_utoa (rsp.uuid));
                goto out;
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
                opinfo.op_errstr = gf_strdup ("Another transaction could be in "
                                              "progress. Please try again after"
                                              " sometime.");
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

out:

        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");

        ret = glusterd_op_sm_inject_event (event_type, txn_id, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        GF_FREE (frame->cookie);
        GLUSTERD_STACK_DESTROY (frame);
        return ret;
}

int32_t
glusterd_mgmt_v3_unlock_peers_cbk (struct rpc_req *req, struct iovec *iov,
                                   int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        glusterd_mgmt_v3_unlock_peers_cbk_fn);
}

int32_t
__glusterd_cluster_unlock_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        xlator_t                      *this = NULL;
        uuid_t                        *txn_id = NULL;
        glusterd_conf_t               *priv = NULL;
        char                          *err_str = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;

        if (-1 == req->rpc_status) {
                err_str = "Unlock response not received from one of the peer.";
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLUSTER_UNLOCK_FAILED,
                        "UnLock response is not received from one of the peer");
                glusterd_set_opinfo (err_str, 0, 0);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CLUSTER_UNLOCK_FAILED,
                        "Failed to decode unlock response received from peer");
                err_str = "Failed to decode cluster unlock response received "
                          "from peer";
                glusterd_set_opinfo (err_str, 0, 0);
                event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        op_ret = rsp.op_ret;

        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_UNLOCK_FROM_UUID_REJCT,
                        "Received unlock RJT from uuid: %s",
                        uuid_utoa (rsp.uuid));
        } else {
                gf_msg_debug (this->name, 0,
                        "Received unlock ACC from uuid: %s",
                        uuid_utoa (rsp.uuid));
        }

        rcu_read_lock ();
        ret = (glusterd_peerinfo_find (rsp.uuid, NULL) == NULL);
        rcu_read_unlock ();

        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_CLUSTER_UNLOCK_FAILED,
                        "Unlock response received from unknown peer %s",
                        uuid_utoa (rsp.uuid));
                goto out;
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

out:

        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");

        ret = glusterd_op_sm_inject_event (event_type, txn_id, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd_cluster_unlock_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_cluster_unlock_cbk);
}

int32_t
__glusterd_stage_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gd1_mgmt_stage_op_rsp         rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        dict_t                        *dict   = NULL;
        char                          err_str[2048] = {0};
        char                          *peer_str = NULL;
        xlator_t                      *this = NULL;
        glusterd_conf_t               *priv = NULL;
        uuid_t                        *txn_id = NULL;
        call_frame_t                  *frame = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT(myframe);

        frame = myframe;
        txn_id = frame->cookie;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                /* use standard allocation because to keep uniformity
                   in freeing it */
                rsp.op_errstr = strdup ("error");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL, "Failed to decode stage "
                        "response received from peer");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                /* use standard allocation because to keep uniformity
                   in freeing it */
                rsp.op_errstr = strdup ("Failed to decode stage response "
                                        "received from peer.");
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize rsp-buffer to dictionary");
			event_type = GD_OP_EVENT_RCVD_RJT;
                        goto out;
                } else {
                        dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

out:
        op_ret = rsp.op_ret;

        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STAGE_FROM_UUID_REJCT,
                        "Received stage RJT from uuid: %s",
                        uuid_utoa (rsp.uuid));
        } else {
                gf_msg_debug (this->name, 0,
                        "Received stage ACC from uuid: %s",
                        uuid_utoa (rsp.uuid));
        }

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (rsp.uuid, NULL);
        if (peerinfo == NULL) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_RESP_FROM_UNKNOWN_PEER, "Stage response received "
                        "from unknown peer: %s. Ignoring response.",
                        uuid_utoa (rsp.uuid));
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
                if (strcmp ("", rsp.op_errstr)) {
                        opinfo.op_errstr = gf_strdup (rsp.op_errstr);
                } else {
                        if (peerinfo)
                                peer_str = peerinfo->hostname;
                        else
                                peer_str = uuid_utoa (rsp.uuid);
                        snprintf (err_str, sizeof (err_str),
                                  OPERRSTR_STAGE_FAIL, peer_str);
                        opinfo.op_errstr = gf_strdup (err_str);
                }
                if (!opinfo.op_errstr)
                        ret = -1;
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

        rcu_read_unlock ();


        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");

        ret = glusterd_op_sm_inject_event (event_type, txn_id, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        free (rsp.op_errstr); //malloced by xdr
        if (dict) {
                if (!dict->extra_stdfree && rsp.dict.dict_val)
                        free (rsp.dict.dict_val); //malloced by xdr
                dict_unref (dict);
        } else {
                free (rsp.dict.dict_val); //malloced by xdr
        }
        GF_FREE (frame->cookie);
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd_stage_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_stage_op_cbk);
}

int32_t
__glusterd_commit_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gd1_mgmt_commit_op_rsp        rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        dict_t                        *dict = NULL;
        char                          err_str[2048] = {0};
        char                          *peer_str = NULL;
        xlator_t                      *this = NULL;
        glusterd_conf_t               *priv = NULL;
        uuid_t                        *txn_id = NULL;
        glusterd_op_info_t            txn_op_info = {{0},};
        call_frame_t                  *frame  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT(myframe);

        frame = myframe;
        txn_id = frame->cookie;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                /* use standard allocation because to keep uniformity
                   in freeing it */
                rsp.op_errstr = strdup ("error");
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL, "Failed to decode commit "
                        "response received from peer");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                /* use standard allocation because to keep uniformity
                   in freeing it */
                rsp.op_errstr = strdup ("Failed to decode commit response "
                                        "received from peer.");
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize rsp-buffer to dictionary");
			event_type = GD_OP_EVENT_RCVD_RJT;
                        goto out;
                } else {
                        dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

        op_ret = rsp.op_ret;

        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_FROM_UUID_REJCT,
                        "Received commit RJT from uuid: %s",
                        uuid_utoa (rsp.uuid));
        } else {
                gf_msg_debug (this->name, 0,
                        "Received commit ACC from uuid: %s",
                        uuid_utoa (rsp.uuid));
        }

        ret = glusterd_get_txn_opinfo (txn_id, &txn_op_info);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_OPINFO_GET_FAIL,
                        "Failed to get txn_op_info "
                        "for txn_id = %s", uuid_utoa (*txn_id));
        }

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (rsp.uuid, NULL);
        if (peerinfo == NULL) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_RESP_FROM_UNKNOWN_PEER, "Commit response for "
                        "'Volume %s' received from unknown peer: %s",
                        gd_op_list[opinfo.op], uuid_utoa (rsp.uuid));
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
                if (strcmp ("", rsp.op_errstr)) {
                        opinfo.op_errstr = gf_strdup(rsp.op_errstr);
                } else {
                        if (peerinfo)
                                peer_str = peerinfo->hostname;
                        else
                                peer_str = uuid_utoa (rsp.uuid);
                        snprintf (err_str, sizeof (err_str),
                                  OPERRSTR_COMMIT_FAIL, peer_str);
                        opinfo.op_errstr = gf_strdup (err_str);
                }
                if (!opinfo.op_errstr) {
                        ret = -1;
                        goto unlock;
                }
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
                GF_ASSERT (rsp.op == txn_op_info.op);

                switch (rsp.op) {

                case GD_OP_PROFILE_VOLUME:
                        ret = glusterd_profile_volume_use_rsp_dict (txn_op_info.op_ctx, dict);
                        if (ret)
                                goto unlock;
                break;

                case GD_OP_REBALANCE:
                case GD_OP_DEFRAG_BRICK_VOLUME:
                        ret = glusterd_volume_rebalance_use_rsp_dict (txn_op_info.op_ctx, dict);
                        if (ret)
                                goto unlock;
                break;

                default:
                break;
                }
        }
unlock:
        rcu_read_unlock ();

out:

        ret = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");

        ret = glusterd_op_sm_inject_event (event_type, txn_id, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (dict)
                dict_unref (dict);
        free (rsp.op_errstr); //malloced by xdr
        GF_FREE (frame->cookie);
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd_commit_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_commit_op_cbk);
}

int32_t
glusterd_rpc_probe (call_frame_t *frame, xlator_t *this,
                    void *data)
{
        gd1_mgmt_probe_req      req = {{0},};
        int                     ret = 0;
        int                     port = 0;
        char                    *hostname = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        priv = this->private;

        GF_ASSERT (priv);
        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "port", &port);
        if (ret)
                port = GF_DEFAULT_BASE_PORT;

        ret = dict_get_ptr (dict, "peerinfo", VOID (&peerinfo));
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, MY_UUID);
        req.hostname = gf_strdup (hostname);
        req.port = port;

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame, peerinfo->peer,
                                       GLUSTERD_PROBE_QUERY,
                                       NULL, this, glusterd_probe_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_probe_req);

out:
        GF_FREE (req.hostname);
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}


int32_t
glusterd_rpc_friend_add (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gd1_mgmt_friend_req         req       = {{0},};
        int                         ret       = 0;
        glusterd_peerinfo_t        *peerinfo  = NULL;
        glusterd_conf_t            *priv      = NULL;
        glusterd_friend_sm_event_t *event     = NULL;
        dict_t                     *peer_data = NULL;


        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                rcu_read_unlock ();
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                goto out;
        }

        gf_uuid_copy (req.uuid, MY_UUID);
        req.hostname = gf_strdup (peerinfo->hostname);
        req.port = peerinfo->port;

        rcu_read_unlock ();

        ret = glusterd_add_volumes_to_export_dict (&peer_data);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to add list of volumes "
                        "in the peer_data dict for handshake");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (peer_data,
                                          "hostname_in_cluster",
                                          peerinfo->hostname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to add hostname of the peer");
                goto out;
        }

        if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                ret = glusterd_add_missed_snaps_to_export_dict (peer_data);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                                "Unable to add list of missed snapshots "
                                "in the peer_data dict for handshake");
                        goto out;
                }

                ret = glusterd_add_snapshots_to_export_dict (peer_data);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_LIST_SET_FAIL,
                                "Unable to add list of snapshots "
                                "in the peer_data dict for handshake");
                        goto out;
                }
        }

        ret = dict_allocate_and_serialize (peer_data, &req.vols.vols_val,
                                           &req.vols.vols_len);
        if (ret)
                goto out;

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame, peerinfo->peer,
                                       GLUSTERD_FRIEND_ADD,
                                       NULL, this, glusterd_friend_add_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_friend_req);


out:
        GF_FREE (req.vols.vols_val);
        GF_FREE (req.hostname);

        if (peer_data)
                dict_unref (peer_data);

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_rpc_friend_remove (call_frame_t *frame, xlator_t *this,
                            void *data)
{
        gd1_mgmt_friend_req             req = {{0},};
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        glusterd_friend_sm_event_t      *event = NULL;

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                rcu_read_unlock ();
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                goto out;
        }

        gf_uuid_copy (req.uuid, MY_UUID);
        req.hostname = gf_strdup (peerinfo->hostname);
        req.port = peerinfo->port;

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame, peerinfo->peer,
                                       GLUSTERD_FRIEND_REMOVE, NULL,
                                       this, glusterd_friend_remove_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_friend_req);

        rcu_read_unlock ();
out:
        GF_FREE (req.hostname);

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}


int32_t
glusterd_rpc_friend_update (call_frame_t *frame, xlator_t *this,
                            void *data)
{
        gd1_mgmt_friend_update  req         = {{0},};
        int                     ret         = 0;
        glusterd_conf_t        *priv        = NULL;
        dict_t                 *friends     = NULL;
        call_frame_t           *dummy_frame = NULL;
        glusterd_peerinfo_t    *peerinfo    = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        friends = data;
        if (!friends)
                goto out;

        ret = dict_get_ptr (friends, "peerinfo", VOID(&peerinfo));
        if (ret)
                goto out;
        /* Don't want to send the pointer over */
        dict_del (friends, "peerinfo");

        ret = dict_allocate_and_serialize (friends, &req.friends.friends_val,
                                           &req.friends.friends_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, MY_UUID);

        dummy_frame = create_frame (this, this->ctx->pool);
        ret = glusterd_submit_request (peerinfo->rpc, &req, dummy_frame,
                                       peerinfo->peer,
                                       GLUSTERD_FRIEND_UPDATE, NULL,
                                       this, glusterd_friend_update_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_friend_update);

out:
        GF_FREE (req.friends.friends_val);

        if (ret && dummy_frame)
                STACK_DESTROY (dummy_frame->root);

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_cluster_lock (call_frame_t *frame, xlator_t *this,
                          void *data)
{
        gd1_mgmt_cluster_lock_req       req = {{0},};
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        call_frame_t                    *dummy_frame = NULL;

        if (!this)
                goto out;

        peerinfo = data;

        priv = this->private;
        GF_ASSERT (priv);

        glusterd_get_uuid (&req.uuid);

        dummy_frame = create_frame (this, this->ctx->pool);
        if (!dummy_frame)
                goto out;

        ret = glusterd_submit_request (peerinfo->rpc, &req, dummy_frame,
                                       peerinfo->mgmt, GLUSTERD_MGMT_CLUSTER_LOCK,
                                       NULL,
                                       this, glusterd_cluster_lock_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req);
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

        if (ret && dummy_frame)
                STACK_DESTROY (dummy_frame->root);
        return ret;
}

int32_t
glusterd_mgmt_v3_lock_peers (call_frame_t *frame, xlator_t *this,
                             void *data)
{
        gd1_mgmt_v3_lock_req             req         = {{0},};
        int                              ret         = -1;
        glusterd_peerinfo_t             *peerinfo    = NULL;
        glusterd_conf_t                 *priv        = NULL;
        dict_t                          *dict        = NULL;
        uuid_t                          *txn_id      = NULL;

        if (!this)
                goto out;

        dict = data;

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_ptr (dict, "peerinfo", VOID (&peerinfo));
        if (ret)
                goto out;

        //peerinfo should not be in payload
        dict_del (dict, "peerinfo");

        glusterd_get_uuid (&req.uuid);

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL, "Failed to serialize dict "
                        "to request buffer");
                goto out;
        }

        /* Sending valid transaction ID to peers */
        ret = dict_get_bin (dict, "transaction_id",
                            (void **)&txn_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_ID_GET_FAIL,
                       "Failed to get transaction id.");
                goto out;
        } else {
                gf_msg_debug (this->name, 0,
                        "Transaction_id = %s", uuid_utoa (*txn_id));
                gf_uuid_copy (req.txn_id, *txn_id);
        }

        if (!frame)
                frame = create_frame (this, this->ctx->pool);

        if (!frame) {
                ret = -1;
                goto out;
        }
        frame->cookie = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!frame->cookie) {
                ret = -1;
                goto out;
        }
        gf_uuid_copy (frame->cookie, req.txn_id);

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame,
                                       peerinfo->mgmt_v3,
                                       GLUSTERD_MGMT_V3_LOCK, NULL,
                                       this, glusterd_mgmt_v3_lock_peers_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_v3_lock_req);
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        if (dict)
                dict_unref (dict);
        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
glusterd_mgmt_v3_unlock_peers (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        gd1_mgmt_v3_unlock_req           req         = {{0},};
        int                              ret         = -1;
        glusterd_peerinfo_t             *peerinfo    = NULL;
        glusterd_conf_t                 *priv        = NULL;
        dict_t                          *dict        = NULL;
        uuid_t                          *txn_id      = NULL;

        if (!this)
                goto out;

        dict = data;

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_ptr (dict, "peerinfo", VOID (&peerinfo));
        if (ret)
                goto out;

        //peerinfo should not be in payload
        dict_del (dict, "peerinfo");

        glusterd_get_uuid (&req.uuid);

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "Failed to serialize dict "
                        "to request buffer");
                goto out;
        }

        /* Sending valid transaction ID to peers */
        ret = dict_get_bin (dict, "transaction_id",
                            (void **)&txn_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_ID_GET_FAIL,
                       "Failed to get transaction id.");
                goto out;
        } else {
                gf_msg_debug (this->name, 0,
                        "Transaction_id = %s", uuid_utoa (*txn_id));
                gf_uuid_copy (req.txn_id, *txn_id);
        }

        if (!frame)
                frame = create_frame (this, this->ctx->pool);

        if (!frame) {
                ret = -1;
                goto out;
        }
        frame->cookie = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!frame->cookie) {
                ret = -1;
                goto out;
        }
        gf_uuid_copy (frame->cookie, req.txn_id);

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame,
                                       peerinfo->mgmt_v3,
                                       GLUSTERD_MGMT_V3_UNLOCK, NULL,
                                       this, glusterd_mgmt_v3_unlock_peers_cbk,
                                       (xdrproc_t)
                                       xdr_gd1_mgmt_v3_unlock_req);
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        if (dict)
                dict_unref(dict);

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
glusterd_cluster_unlock (call_frame_t *frame, xlator_t *this,
                            void *data)
{
        gd1_mgmt_cluster_lock_req       req = {{0},};
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        call_frame_t                    *dummy_frame = NULL;

        if (!this ) {
                ret = -1;
                goto out;
        }
        peerinfo = data;
        priv = this->private;
        GF_ASSERT (priv);

        glusterd_get_uuid (&req.uuid);

        dummy_frame = create_frame (this, this->ctx->pool);
        if (!dummy_frame)
                goto out;

        ret = glusterd_submit_request (peerinfo->rpc, &req, dummy_frame,
                                       peerinfo->mgmt, GLUSTERD_MGMT_CLUSTER_UNLOCK,
                                       NULL,
                                       this, glusterd_cluster_unlock_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_req);
out:
        gf_msg_debug (this ? this->name : "glusterd", 0, "Returning %d", ret);

        if (ret && dummy_frame)
                STACK_DESTROY (dummy_frame->root);

        return ret;
}

int32_t
glusterd_stage_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_stage_op_req           req = {{0,},};
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        dict_t                          *dict = NULL;
        gf_boolean_t                    is_alloc = _gf_true;
        uuid_t                          *txn_id      = NULL;

        if (!this) {
                goto out;
        }

        dict = data;

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_ptr (dict, "peerinfo", VOID (&peerinfo));
        if (ret)
                goto out;

        //peerinfo should not be in payload
        dict_del (dict, "peerinfo");

        glusterd_get_uuid (&req.uuid);
        req.op = glusterd_op_get_op ();

        ret = dict_allocate_and_serialize (dict, &req.buf.buf_val,
                                           &req.buf.buf_len);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "Failed to serialize dict "
                        "to request buffer");
                goto out;
        }
        /* Sending valid transaction ID to peers */
        ret = dict_get_bin (dict, "transaction_id",
                            (void **)&txn_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_ID_GET_FAIL,
                       "Failed to get transaction id.");
                goto out;
        } else {
                gf_msg_debug (this->name, 0,
                        "Transaction_id = %s", uuid_utoa (*txn_id));
        }

        if (!frame)
                frame = create_frame (this, this->ctx->pool);

        if (!frame) {
                ret = -1;
                goto out;
        }
        frame->cookie = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!frame->cookie) {
                ret = -1;
                goto out;
        }
        gf_uuid_copy (frame->cookie, *txn_id);

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame,
                                       peerinfo->mgmt, GLUSTERD_MGMT_STAGE_OP,
                                       NULL,
                                       this, glusterd_stage_op_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_stage_op_req);

out:
        if ((_gf_true == is_alloc) && req.buf.buf_val)
                GF_FREE (req.buf.buf_val);

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_commit_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_commit_op_req  req         = {{0,},};
        int                     ret         = -1;
        glusterd_peerinfo_t    *peerinfo    = NULL;
        glusterd_conf_t        *priv        = NULL;
        dict_t                 *dict        = NULL;
        gf_boolean_t            is_alloc    = _gf_true;
        uuid_t                 *txn_id      = NULL;

        if (!this) {
                goto out;
        }

        dict = data;
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_ptr (dict, "peerinfo", VOID (&peerinfo));
        if (ret)
                goto out;

        //peerinfo should not be in payload
        dict_del (dict, "peerinfo");

        glusterd_get_uuid (&req.uuid);
        req.op = glusterd_op_get_op ();

        ret = dict_allocate_and_serialize (dict, &req.buf.buf_val,
                                           &req.buf.buf_len);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SERL_LENGTH_GET_FAIL,
                        "Failed to serialize dict to "
                        "request buffer");
                goto out;
        }
        /* Sending valid transaction ID to peers */
        ret = dict_get_bin (dict, "transaction_id",
                            (void **)&txn_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TRANS_ID_GET_FAIL,
                       "Failed to get transaction id.");
                goto out;
        } else {
                gf_msg_debug (this->name, 0,
                        "Transaction_id = %s", uuid_utoa (*txn_id));
        }

        if (!frame)
                frame = create_frame (this, this->ctx->pool);

        if (!frame) {
                ret = -1;
                goto out;
        }
        frame->cookie = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!frame->cookie) {
                ret = -1;
                goto out;
        }
        gf_uuid_copy (frame->cookie, *txn_id);

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame,
                                       peerinfo->mgmt, GLUSTERD_MGMT_COMMIT_OP,
                                       NULL,
                                       this, glusterd_commit_op_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_commit_op_req);

out:
        if ((_gf_true == is_alloc) && req.buf.buf_val)
                GF_FREE (req.buf.buf_val);

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
__glusterd_brick_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gd1_mgmt_brick_op_rsp         rsp   = {0};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        call_frame_t                  *frame = NULL;
        glusterd_op_brick_rsp_ctx_t   *ev_ctx = NULL;
        dict_t                        *dict = NULL;
        int                           index = 0;
        glusterd_req_ctx_t            *req_ctx = NULL;
        glusterd_pending_node_t       *node = NULL;
        xlator_t                      *this = NULL;
        uuid_t                        *txn_id = NULL;
        glusterd_conf_t               *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        txn_id = &priv->global_txn_id;
        frame = myframe;
        req_ctx = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                /* use standard allocation because to keep uniformity
                   in freeing it */
                rsp.op_errstr = strdup ("error");
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret =  xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RES_DECODE_FAIL,
                        "Failed to decode brick op "
                        "response received");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = strdup ("Unable to decode brick op response");
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        if (rsp.output.output_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.output.output_val,
                                        rsp.output.output_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "Failed to "
                                "unserialize rsp-buffer to dictionary");
			event_type = GD_OP_EVENT_RCVD_RJT;
                        goto out;
                } else {
                        dict->extra_stdfree = rsp.output.output_val;
                }
        }

        op_ret = rsp.op_ret;

        /* Add index to rsp_dict for GD_OP_STATUS_VOLUME */
        if (GD_OP_STATUS_VOLUME == req_ctx->op) {
                node = frame->cookie;
                index = node->index;
                ret = dict_set_int32 (dict, "index", index);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Error setting index on brick status rsp dict");
                        rsp.op_ret = -1;
                        event_type = GD_OP_EVENT_RCVD_RJT;
                        goto out;
                }
        }
out:

        if (req_ctx && req_ctx->dict) {
                ret = dict_get_bin (req_ctx->dict, "transaction_id",
                                    (void **)&txn_id);
                gf_msg_debug (this->name, 0,
                        "transaction ID = %s", uuid_utoa (*txn_id));
        }

        ev_ctx = GF_CALLOC (1, sizeof (*ev_ctx), gf_gld_mt_brick_rsp_ctx_t);
        GF_ASSERT (ev_ctx);
        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                ev_ctx->op_ret = op_ret;
                ev_ctx->op_errstr = gf_strdup(rsp.op_errstr);
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }
        ev_ctx->pending_node = frame->cookie;
        ev_ctx->rsp_dict  = dict;
        ev_ctx->commit_ctx = frame->local;
        ret = glusterd_op_sm_inject_event (event_type, txn_id, ev_ctx);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (ret && dict)
                dict_unref (dict);
        free (rsp.op_errstr); //malloced by xdr
        GLUSTERD_STACK_DESTROY (frame);
        return ret;
}

int32_t
glusterd_brick_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_brick_op_cbk);
}

int32_t
glusterd_brick_op (call_frame_t *frame, xlator_t *this,
                   void *data)
{

        gd1_mgmt_brick_op_req           *req = NULL;
        int                             ret = 0;
        int                             ret1 = 0;
        glusterd_conf_t                 *priv = NULL;
        call_frame_t                    *dummy_frame = NULL;
        char                            *op_errstr = NULL;
        int                             pending_bricks = 0;
        glusterd_pending_node_t         *pending_node;
        glusterd_req_ctx_t              *req_ctx = NULL;
        struct rpc_clnt                 *rpc = NULL;
        dict_t                          *op_ctx = NULL;
        uuid_t                          *txn_id = NULL;

        if (!this) {
                ret = -1;
                goto out;
        }
        priv = this->private;
        GF_ASSERT (priv);

        txn_id = &priv->global_txn_id;

        req_ctx = data;
        GF_ASSERT (req_ctx);
        CDS_INIT_LIST_HEAD (&opinfo.pending_bricks);

        ret = dict_get_bin (req_ctx->dict, "transaction_id", (void **)&txn_id);
        gf_msg_debug (this->name, 0, "transaction ID = %s",
                uuid_utoa (*txn_id));

        ret = glusterd_op_bricks_select (req_ctx->op, req_ctx->dict, &op_errstr,
                                         &opinfo.pending_bricks, NULL);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_SELECT_FAIL, "Failed to select bricks "
                        "while performing brick op during 'Volume %s'",
                        gd_op_list[opinfo.op]);
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        cds_list_for_each_entry (pending_node, &opinfo.pending_bricks, list) {
                dummy_frame = create_frame (this, this->ctx->pool);
                if (!dummy_frame)
                        continue;

                if ((pending_node->type == GD_NODE_NFS) ||
                    (pending_node->type == GD_NODE_QUOTAD) ||
                    (pending_node->type == GD_NODE_SNAPD) ||
                    (pending_node->type == GD_NODE_SCRUB) ||
                    ((pending_node->type == GD_NODE_SHD) &&
                     (req_ctx->op == GD_OP_STATUS_VOLUME)))
                        ret = glusterd_node_op_build_payload
                                (req_ctx->op,
                                 (gd1_mgmt_brick_op_req **)&req,
                                 req_ctx->dict);
                else {
                        ret = glusterd_brick_op_build_payload
                                (req_ctx->op, pending_node->node,
                                 (gd1_mgmt_brick_op_req **)&req,
                                 req_ctx->dict);

                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL,
                                        "Failed to "
                                        "build brick op payload during "
                                        "'Volume %s'", gd_op_list[req_ctx->op]);
                                goto out;
                        }
                }

                dummy_frame->local = data;
                dummy_frame->cookie = pending_node;

                rpc = glusterd_pending_node_get_rpc (pending_node);
                if (!rpc) {
                        if (pending_node->type == GD_NODE_REBALANCE ||
                            pending_node->type == GD_NODE_TIERD) {
                                opinfo.brick_pending_count = 0;
                                ret = 0;
                                if (req) {
                                        GF_FREE (req->input.input_val);
                                        GF_FREE (req);
                                        req = NULL;
                                }
                                GLUSTERD_STACK_DESTROY (dummy_frame);

                                op_ctx = glusterd_op_get_ctx ();
                                if (!op_ctx)
                                        goto out;
                                glusterd_defrag_volume_node_rsp (req_ctx->dict,
                                                                 NULL, op_ctx);

                                goto out;
                        }

                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RPC_FAILURE, "Brick Op failed "
                                "due to rpc failure.");
                        goto out;
                }

                ret = glusterd_submit_request (rpc, req, dummy_frame,
                                               priv->gfs_mgmt,
                                               req->op, NULL,
                                               this, glusterd_brick_op_cbk,
                                               (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
                if (req) {
                        GF_FREE (req->input.input_val);
                        GF_FREE (req);
                        req = NULL;
                }
                if (!ret)
                        pending_bricks++;

                glusterd_pending_node_put_rpc (pending_node);
        }

        gf_msg_trace (this->name, 0, "Sent brick op req for operation "
                "'Volume %s' to %d bricks", gd_op_list[req_ctx->op],
                pending_bricks);
        opinfo.brick_pending_count = pending_bricks;

out:

        if (ret)
                opinfo.op_ret = ret;

        ret1 = glusterd_set_txn_opinfo (txn_id, &opinfo);
        if (ret1)
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TRANS_OPINFO_SET_FAIL,
                                "Unable to set "
                                "transaction's opinfo");

        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT,
                                             txn_id, data);
                opinfo.op_ret = ret;
        }

        gf_msg_debug (this ? this->name : "glusterd", 0, "Returning %d", ret);
        return ret;
}

struct rpc_clnt_procedure gd_brick_actors[GLUSTERD_BRICK_MAXVALUE] = {
        [GLUSTERD_BRICK_NULL] = {"NULL", NULL },
        [GLUSTERD_BRICK_OP]   = {"BRICK_OP", glusterd_brick_op},
};

struct rpc_clnt_procedure gd_peer_actors[GLUSTERD_FRIEND_MAXVALUE] = {
        [GLUSTERD_FRIEND_NULL]   = {"NULL", NULL },
        [GLUSTERD_PROBE_QUERY]   = {"PROBE_QUERY", glusterd_rpc_probe},
        [GLUSTERD_FRIEND_ADD]    = {"FRIEND_ADD", glusterd_rpc_friend_add},
        [GLUSTERD_FRIEND_REMOVE] = {"FRIEND_REMOVE", glusterd_rpc_friend_remove},
        [GLUSTERD_FRIEND_UPDATE] = {"FRIEND_UPDATE", glusterd_rpc_friend_update},
};

struct rpc_clnt_procedure gd_mgmt_actors[GLUSTERD_MGMT_MAXVALUE] = {
        [GLUSTERD_MGMT_NULL]           = {"NULL", NULL },
        [GLUSTERD_MGMT_CLUSTER_LOCK]   = {"CLUSTER_LOCK", glusterd_cluster_lock},
        [GLUSTERD_MGMT_CLUSTER_UNLOCK] = {"CLUSTER_UNLOCK", glusterd_cluster_unlock},
        [GLUSTERD_MGMT_STAGE_OP]       = {"STAGE_OP", glusterd_stage_op},
        [GLUSTERD_MGMT_COMMIT_OP]      = {"COMMIT_OP", glusterd_commit_op},
};

struct rpc_clnt_procedure gd_mgmt_v3_actors[GLUSTERD_MGMT_V3_MAXVALUE] = {
        [GLUSTERD_MGMT_V3_NULL]    = {"NULL", NULL },
        [GLUSTERD_MGMT_V3_LOCK]    = {"MGMT_V3_LOCK", glusterd_mgmt_v3_lock_peers},
        [GLUSTERD_MGMT_V3_UNLOCK]  = {"MGMT_V3_UNLOCK", glusterd_mgmt_v3_unlock_peers},
};

struct rpc_clnt_program gd_mgmt_prog = {
        .progname  = "glusterd mgmt",
        .prognum   = GD_MGMT_PROGRAM,
        .progver   = GD_MGMT_VERSION,
        .proctable = gd_mgmt_actors,
        .numproc   = GLUSTERD_MGMT_MAXVALUE,
};

struct rpc_clnt_program gd_brick_prog = {
        .progname  = "brick operations",
        .prognum   = GD_BRICK_PROGRAM,
        .progver   = GD_BRICK_VERSION,
        .proctable = gd_brick_actors,
        .numproc   = GLUSTERD_BRICK_MAXVALUE,
};

struct rpc_clnt_program gd_peer_prog = {
        .progname  = "Peer mgmt",
        .prognum   = GD_FRIEND_PROGRAM,
        .progver   = GD_FRIEND_VERSION,
        .proctable = gd_peer_actors,
        .numproc   = GLUSTERD_FRIEND_MAXVALUE,
};

struct rpc_clnt_program gd_mgmt_v3_prog = {
        .progname  = "glusterd mgmt v3",
        .prognum   = GD_MGMT_PROGRAM,
        .progver   = GD_MGMT_V3_VERSION,
        .proctable = gd_mgmt_v3_actors,
        .numproc   = GLUSTERD_MGMT_V3_MAXVALUE,
};
