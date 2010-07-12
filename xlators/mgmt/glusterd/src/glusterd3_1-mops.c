/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpc-clnt.h"
#include "glusterd1.h"
#include "gd-xdr.h"
#include "compat-errno.h"
#include "glusterd-op-sm.h"
#include "glusterd-sm.h"
#include "glusterd.h"
#include "protocol-common.h"
#include "glusterd-utils.h"
#include <sys/uio.h>


#define SERVER_PATH_MAX  (16 * 1024)


extern glusterd_op_info_t    opinfo;

int
glusterd_null (rpcsvc_request_t *req)
{

        return 0;
}

int
glusterd3_1_probe_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        gd1_mgmt_probe_rsp    rsp   = {{0},};
        glusterd_conf_t       *conf = NULL;
        int                   ret   = 0;
        char                  str[50] = {0,};
        glusterd_peerinfo_t           *peerinfo = NULL;
        glusterd_peerinfo_t           *dup_peerinfo = NULL;
        glusterd_friend_sm_event_t    *event = NULL;

        conf  = THIS->private;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gd_xdr_to_mgmt_probe_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe resp from uuid: %s, host: %s",
                str, rsp.hostname);

        ret = glusterd_friend_find (rsp.uuid, rsp.hostname, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
        }

        if (!peerinfo->hostname) {
                glusterd_friend_find (NULL, rsp.hostname, &dup_peerinfo);
                GF_ASSERT (dup_peerinfo);
                GF_ASSERT (dup_peerinfo->hostname);
                peerinfo->hostname = gf_strdup (rsp.hostname);
                peerinfo->rpc = dup_peerinfo->rpc;
                list_del_init (&dup_peerinfo->uuid_list);
                GF_FREE (dup_peerinfo->hostname);
                GF_FREE (dup_peerinfo);
        }
        GF_ASSERT (peerinfo->hostname);
        uuid_copy (peerinfo->uuid, rsp.uuid);

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_INIT_FRIEND_REQ, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto out;
        }

        event->peerinfo = peerinfo;
        event->ctx      = ((call_frame_t *)myframe)->local;
        ret = glusterd_friend_sm_inject_event (event);


        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received resp to probe req");

        return ret;

out:
        return ret;
}

int
glusterd3_1_friend_add_cbk (struct rpc_req * req, struct iovec *iov,
                            int count, void *myframe)
{
        gd1_mgmt_friend_rsp           rsp   = {{0},};
        glusterd_conf_t               *conf = NULL;
        int                           ret   = -1;
        glusterd_friend_sm_event_t        *event = NULL;
        glusterd_friend_sm_event_type_t    event_type = GD_FRIEND_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        char                          str[50] = {0,};
        int32_t                       op_ret = -1;
        int32_t                       op_errno = -1;
        glusterd_probe_ctx_t          *ctx = NULL;

        conf  = THIS->private;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
        }

        ret = gd_xdr_to_mgmt_friend_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s, host: %s",
                (op_ret)?"RJT":"ACC", str, rsp.hostname);

        ret = glusterd_friend_find (rsp.uuid, rsp.hostname, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
        }

        if (op_ret)
                event_type = GD_FRIEND_EVENT_RCVD_RJT;
        else
                event_type = GD_FRIEND_EVENT_RCVD_ACC;

        ret = glusterd_friend_sm_new_event (event_type, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto out;
        }
        event->peerinfo = peerinfo;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret)
                goto out;

        ctx = ((call_frame_t *)myframe)->local;

        GF_ASSERT (ctx);

        ret = glusterd_xfer_cli_probe_resp (ctx->req, op_ret, op_errno,
                                            ctx->hostname);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

out:
        return ret;
}

int
glusterd3_1_friend_remove_cbk (struct rpc_req * req, struct iovec *iov,
                               int count, void *myframe)
{
        gd1_mgmt_friend_rsp             rsp   = {{0},};
        glusterd_conf_t                 *conf = NULL;
        int                             ret   = -1;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_sm_event_type_t event_type = GD_FRIEND_EVENT_NONE;
        glusterd_peerinfo_t             *peerinfo = NULL;
        char                            str[50] = {0,};
        int32_t                         op_ret = -1;
        int32_t                         op_errno = -1;
        glusterd_probe_ctx_t            *ctx = NULL;

        conf  = THIS->private;
        GF_ASSERT (conf);

        ctx = ((call_frame_t *)myframe)->local;
        GF_ASSERT (ctx);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto respond;
        }

        ret = gd_xdr_to_mgmt_friend_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto respond;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s, host: %s",
                (op_ret)?"RJT":"ACC", str, rsp.hostname);

        if (op_ret)
                goto respond;

        ret = glusterd_friend_find (rsp.uuid, rsp.hostname, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
                goto respond;
        }

        event_type = GD_FRIEND_EVENT_REMOVE_FRIEND;

        ret = glusterd_friend_sm_new_event (event_type, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto respond;
        }
        event->peerinfo = peerinfo;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret)
                goto respond;

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;

respond:
        ret = glusterd_xfer_cli_probe_resp (ctx->req, op_ret, op_errno,
                                            ctx->hostname);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int32_t
glusterd3_1_cluster_lock_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_t        *event = NULL;
        glusterd_op_sm_event_type_t    event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        char                          str[50] = {0,};

        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
        }

        ret = gd_xdr_to_mgmt_cluster_lock_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", str);

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
        }

        if (op_ret)
                event_type = GD_OP_EVENT_RCVD_RJT;
        else
                event_type = GD_OP_EVENT_RCVD_ACC;

        ret = glusterd_op_sm_new_event (event_type, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto out;
        }

        ret = glusterd_op_sm_inject_event (event);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;


out:
        return ret;
}

int32_t
glusterd3_1_cluster_unlock_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_t        *event = NULL;
        glusterd_op_sm_event_type_t    event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        char                          str[50] = {0,};


        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
        }

        ret = gd_xdr_to_mgmt_cluster_unlock_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", str);

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
        }

        if (op_ret)
                event_type = GD_OP_EVENT_RCVD_RJT;
        else
                event_type = GD_OP_EVENT_RCVD_ACC;

        ret = glusterd_op_sm_new_event (event_type, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto out;
        }

        ret = glusterd_op_sm_inject_event (event);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;


out:
        return ret;
}

int32_t
glusterd3_1_stage_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gd1_mgmt_stage_op_rsp         rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_t        *event = NULL;
        glusterd_op_sm_event_type_t    event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        char                          str[50] = {0,};


        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
        }

        ret = gd_xdr_to_mgmt_stage_op_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", str);

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
        }

        if (op_ret)
                event_type = GD_OP_EVENT_RCVD_RJT;
        else
                event_type = GD_OP_EVENT_RCVD_ACC;

        ret = glusterd_op_sm_new_event (event_type, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto out;
        }

        ret = glusterd_op_sm_inject_event (event);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;


out:
        return ret;
}

int32_t
glusterd3_1_commit_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gd1_mgmt_commit_op_rsp         rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_t        *event = NULL;
        glusterd_op_sm_event_type_t    event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        char                          str[50] = {0,};


        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
        }

        ret = gd_xdr_to_mgmt_commit_op_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", str);

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                GF_ASSERT (0);
        }

        if (op_ret)
                event_type = GD_OP_EVENT_RCVD_RJT;
        else
                event_type = GD_OP_EVENT_RCVD_ACC;

        ret = glusterd_op_sm_new_event (event_type, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                         "Unable to get event");
                goto out;
        }

        ret = glusterd_op_sm_inject_event (event);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;


out:
        return ret;
}



int32_t
glusterd3_1_probe (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        gd1_mgmt_probe_req      req = {{0},};
        int                     ret = 0;
        char                    *hostname = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        hostname = data;
        priv = this->private;

        GF_ASSERT (priv);

        ret = glusterd_friend_find (NULL, hostname, &peerinfo);

        if (ret) {
                //We should not reach this state ideally
                GF_ASSERT (0);
                goto out;
        }

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = gf_strdup (hostname);

        ret = glusterd_submit_request (peerinfo, &req, frame, priv->mgmt,
                                       GD_MGMT_PROBE_QUERY,
                                       NULL, gd_xdr_from_mgmt_probe_req,
                                       this, glusterd3_1_probe_cbk);

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd3_1_friend_add (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        gd1_mgmt_friend_req     req = {{0},};
        int                     ret = 0;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_friend_sm_event_t     *event = NULL;
        glusterd_friend_req_ctx_t *ctx = NULL;


        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        ctx = event->ctx;

        peerinfo = event->peerinfo;

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = gf_strdup (peerinfo->hostname);

        ret = glusterd_submit_request (peerinfo, &req, frame, priv->mgmt,
                                       GD_MGMT_FRIEND_ADD,
                                       NULL, gd_xdr_from_mgmt_friend_req,
                                       this, glusterd3_1_friend_add_cbk);

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_friend_remove (call_frame_t *frame, xlator_t *this,
                           void *data)
{
        gd1_mgmt_friend_req             req = {{0},};
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;


        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        ctx = event->ctx;

        peerinfo = event->peerinfo;

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = peerinfo->hostname;

        ret = glusterd_submit_request (peerinfo, &req, frame, priv->mgmt,
                                       GD_MGMT_FRIEND_REMOVE,
                                       NULL, gd_xdr_from_mgmt_friend_req,
                                       this, glusterd3_1_friend_remove_cbk);

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd3_1_cluster_lock (call_frame_t *frame, xlator_t *this,
                           void *data)
{
        gd1_mgmt_cluster_lock_req       req = {{0},};
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        call_frame_t                    *dummy_frame = NULL;
        int32_t                         pending_lock = 0;

        if (!this) {
                ret = -1;
                goto out;
        }

        priv = this->private;
        glusterd_get_uuid (&req.uuid);

        GF_ASSERT (priv);
        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                dummy_frame = create_frame (this, this->ctx->pool);

                if (!dummy_frame)
                        continue;

                ret = glusterd_submit_request (peerinfo, &req, dummy_frame,
                                        priv->mgmt, GD_MGMT_CLUSTER_LOCK,
                                        NULL,
                                        gd_xdr_from_mgmt_cluster_lock_req,
                                        this, glusterd3_1_cluster_lock_cbk);
                if (!ret)
                        pending_lock++;
                //TODO: Instead of keeping count, maintain a list of locked
                //UUIDs.
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent lock req to %d peers",
                                            pending_lock);
        opinfo.pending_count = pending_lock;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_cluster_unlock (call_frame_t *frame, xlator_t *this,
                            void *data)
{
        gd1_mgmt_cluster_lock_req       req = {{0},};
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        int32_t                         pending_unlock = 0;
        call_frame_t                    *dummy_frame = NULL;

        if (!this ) {
                ret = -1;
                goto out;
        }

        priv = this->private;

        glusterd_get_uuid (&req.uuid);

        GF_ASSERT (priv);
        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                dummy_frame = create_frame (this, this->ctx->pool);

                if (!dummy_frame)
                        continue;

                ret = glusterd_submit_request (peerinfo, &req, dummy_frame,
                                        priv->mgmt, GD_MGMT_CLUSTER_UNLOCK,
                                        NULL,
                                        gd_xdr_from_mgmt_cluster_unlock_req,
                                        this, glusterd3_1_cluster_unlock_cbk);
                if (!ret)
                        pending_unlock++;
                //TODO: Instead of keeping count, maintain a list of locked
                //UUIDs.
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent unlock req to %d peers",
                                            pending_unlock);
        opinfo.pending_count = pending_unlock;

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_stage_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_stage_op_req           *req = NULL;
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        int32_t                         pending_peer = 0;
        int                             i = 0;
        call_frame_t                    *dummy_frame = NULL;

        if (!this) {
                ret = -1;
                goto out;
        }

        priv = this->private;

        GF_ASSERT (priv);

        for ( i = GD_OP_NONE; i < GD_OP_MAX; i++) {
                if (opinfo.pending_op[i])
                        break;
        }

        if (GD_OP_MAX == i) {

                //No pending ops, inject stage_acc

                glusterd_op_sm_event_t  *event = NULL;

                ret = glusterd_op_sm_new_event (GD_OP_EVENT_STAGE_ACC,
                                                &event);

                if (ret)
                        goto out;

                ret = glusterd_op_sm_inject_event (event);

                return ret;
        }


        ret = glusterd_op_build_payload (i, &req);

        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                dummy_frame = create_frame (this, this->ctx->pool);

                if (!dummy_frame)
                        continue;

                ret = glusterd_submit_request (peerinfo, req, dummy_frame,
                                                priv->mgmt, GD_MGMT_STAGE_OP,
                                                NULL,
                                                gd_xdr_from_mgmt_stage_op_req,
                                                this, glusterd3_1_stage_op_cbk);
                if (!ret)
                        pending_peer++;
                //TODO: Instead of keeping count, maintain a list of pending
                //UUIDs.
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent op req to %d peers",
                                            pending_peer);
        opinfo.pending_count = pending_peer;

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_commit_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_commit_op_req          *req = NULL;
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        int32_t                         pending_peer = 0;
        int                             i = 0;
        call_frame_t                    *dummy_frame = NULL;

        if (!this) {
                ret = -1;
                goto out;
        }

        priv = this->private;

        GF_ASSERT (priv);

        for ( i = GD_OP_NONE; i < GD_OP_MAX; i++) {
                if (opinfo.pending_op[i])
                        break;
        }

        if (GD_OP_MAX == i) {

                //No pending ops, inject stage_acc

                glusterd_op_sm_event_t  *event = NULL;

                ret = glusterd_op_sm_new_event (GD_OP_EVENT_COMMIT_ACC,
                                                &event);

                if (ret)
                        goto out;

                ret = glusterd_op_sm_inject_event (event);

                return ret;
        }


        ret = glusterd_op_build_payload (i, (gd1_mgmt_stage_op_req **)&req);

        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                dummy_frame = create_frame (this, this->ctx->pool);

                if (!dummy_frame)
                        continue;

                ret = glusterd_submit_request (peerinfo, req, dummy_frame,
                                                priv->mgmt, GD_MGMT_COMMIT_OP,
                                                NULL,
                                                gd_xdr_from_mgmt_commit_op_req,
                                                this, glusterd3_1_commit_op_cbk);
                if (!ret)
                        pending_peer++;
                //TODO: Instead of keeping count, maintain a list of pending
                //UUIDs.
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent op req to %d peers",
                                            pending_peer);
        opinfo.pending_count = pending_peer;

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int
glusterd_handle_rpc_msg (rpcsvc_request_t *req)
{
        int     ret = -1;
        GF_ASSERT (req);

        //ret = glusterd1_mgmt_actors[req->procnum].actor (req);
        //

        switch (req->procnum) {
                case GD_MGMT_PROBE_QUERY:
                        ret = glusterd_handle_probe_query (req);
                        break;

                case GD_MGMT_FRIEND_ADD:
                        ret = glusterd_handle_incoming_friend_req (req);
                        break;

                case GD_MGMT_CLUSTER_LOCK:
                        ret = glusterd_handle_cluster_lock (req);
                        break;

                case GD_MGMT_CLUSTER_UNLOCK:
                        ret = glusterd_handle_cluster_unlock (req);
                        break;

                case GD_MGMT_STAGE_OP:
                        ret = glusterd_handle_stage_op (req);
                        break;

                case GD_MGMT_COMMIT_OP:
                        ret = glusterd_handle_commit_op (req);
                        break;

                case GD_MGMT_CLI_PROBE:
                        ret = glusterd_handle_cli_probe (req);
                        break;

                case GD_MGMT_CLI_CREATE_VOLUME:
                        ret = glusterd_handle_create_volume (req);
                        break;

                case GD_MGMT_CLI_DEPROBE:
                        ret = glusterd_handle_cli_deprobe (req);
                        break;

                case GD_MGMT_FRIEND_REMOVE:
                        ret = glusterd_handle_incoming_unfriend_req (req);
                        break;

                case GD_MGMT_CLI_LIST_FRIENDS:
                        ret = glusterd_handle_cli_list_friends (req);
                        break;

                default:
                        GF_ASSERT (0);
        }

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}


rpcsvc_actor_t glusterd1_mgmt_actors[] = {
        [GD_MGMT_NULL]        = { "NULL",       GD_MGMT_NULL, glusterd_null, NULL, NULL},
        [GD_MGMT_PROBE_QUERY] = { "PROBE_QUERY", GD_MGMT_PROBE_QUERY, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_FRIEND_ADD] = { "FRIEND_ADD", GD_MGMT_FRIEND_ADD, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_FRIEND_REMOVE] = { "FRIEND_REMOVE", GD_MGMT_FRIEND_REMOVE, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_CLUSTER_LOCK] = { "CLUSTER_LOCK", GD_MGMT_CLUSTER_LOCK, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_CLUSTER_UNLOCK] = { "CLUSTER_UNLOCK", GD_MGMT_CLUSTER_UNLOCK, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_STAGE_OP] = { "STAGE_OP", GD_MGMT_STAGE_OP, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_COMMIT_OP] = { "COMMIT_OP", GD_MGMT_COMMIT_OP, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_CLI_PROBE] = { "CLI_PROBE", GD_MGMT_CLI_PROBE, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_CLI_CREATE_VOLUME] = { "CLI_CREATE_VOLUME", GD_MGMT_CLI_CREATE_VOLUME, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_CLI_DEPROBE] = { "FRIEND_REMOVE", GD_MGMT_CLI_DEPROBE, glusterd_handle_rpc_msg, NULL, NULL},
        [GD_MGMT_CLI_LIST_FRIENDS] = { "LIST_FRIENDS", GD_MGMT_CLI_LIST_FRIENDS, glusterd_handle_rpc_msg, NULL, NULL},
};

/*rpcsvc_actor_t glusterd1_mgmt_actors[] = {
        [GD_MGMT_NULL]        = { "NULL",       GD_MGMT_NULL, glusterd_null, NULL, NULL},
        [GD_MGMT_PROBE_QUERY] = { "PROBE_QUERY", GD_MGMT_PROBE_QUERY, glusterd_handle_probe_query, NULL, NULL},
        [GD_MGMT_FRIEND_ADD] = { "FRIEND_ADD", GD_MGMT_FRIEND_ADD, glusterd_handle_incoming_friend_req, NULL, NULL},
        [GD_MGMT_CLUSTER_LOCK] = { "CLUSTER_LOCK", GD_MGMT_CLUSTER_LOCK, glusterd_handle_cluster_lock, NULL, NULL},
        [GD_MGMT_CLUSTER_UNLOCK] = { "CLUSTER_UNLOCK", GD_MGMT_CLUSTER_UNLOCK, glusterd_handle_cluster_unlock, NULL, NULL},
        [GD_MGMT_STAGE_OP] = { "STAGE_OP", GD_MGMT_STAGE_OP, glusterd_handle_stage_op, NULL, NULL},
        [GD_MGMT_COMMIT_OP] = { "COMMIT_OP", GD_MGMT_COMMIT_OP, glusterd_handle_commit_op, NULL, NULL},
        [GD_MGMT_CLI_PROBE] = { "CLI_PROBE", GD_MGMT_CLI_PROBE, glusterd_handle_cli_probe, NULL, NULL},
};*/


struct rpcsvc_program glusterd1_mop_prog = {
        .progname  = "GlusterD0.0.1",
        .prognum   = GLUSTERD1_MGMT_PROGRAM,
        .progver   = GLUSTERD1_MGMT_VERSION,
        .numactors = GLUSTERD1_MGMT_PROCCNT,
        .actors    = glusterd1_mgmt_actors,
        .progport  = 4284,
};


struct rpc_clnt_procedure glusterd3_1_clnt_mgmt_actors[GD_MGMT_MAXVALUE] = {
        [GD_MGMT_NULL]        = {"NULL", NULL },
        [GD_MGMT_PROBE_QUERY]  = { "PROBE_QUERY",  glusterd3_1_probe},
        [GD_MGMT_FRIEND_ADD]  = { "FRIEND_ADD",  glusterd3_1_friend_add },
        [GD_MGMT_CLUSTER_LOCK] = {"CLUSTER_LOCK", glusterd3_1_cluster_lock},
        [GD_MGMT_CLUSTER_UNLOCK] = {"CLUSTER_UNLOCK", glusterd3_1_cluster_unlock},
        [GD_MGMT_STAGE_OP] = {"STAGE_OP", glusterd3_1_stage_op},
        [GD_MGMT_COMMIT_OP] = {"COMMIT_OP", glusterd3_1_commit_op},
        [GD_MGMT_FRIEND_REMOVE]  = { "FRIEND_REMOVE",  glusterd3_1_friend_remove},
//        [GF_FOP_GETSPEC]     = { "GETSPEC",   client_getspec, client_getspec_cbk },
};



struct rpc_clnt_program glusterd3_1_mgmt_prog = {
        .progname = "Mgmt 3.1",
        .prognum  = GLUSTERD1_MGMT_PROGRAM,
        .progver  = GLUSTERD1_MGMT_VERSION,
        .proctable    = glusterd3_1_clnt_mgmt_actors,
        .numproc  = GLUSTERD1_MGMT_PROCCNT,
};
