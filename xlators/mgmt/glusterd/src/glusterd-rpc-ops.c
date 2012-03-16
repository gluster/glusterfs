/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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
#include <sys/uio.h>


#define SERVER_PATH_MAX  (16 * 1024)


extern glusterd_op_info_t opinfo;

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

        GF_ASSERT (THIS);

        conf = THIS->private;

        GF_ASSERT (conf);

        ctx = op_ctx;

        switch (op) {
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
        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
        {
                if (ctx) {
                        ret = dict_get_int32 (ctx, "status", &status);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_TRACE,
                                        "failed to get status");
                        }
                }
                break;
        }
        case GD_OP_GSYNC_SET:
        {
               if (ctx) {
                        ret = dict_get_str (ctx, "errstr", &errstr);
                        ret = dict_set_str (ctx, "glusterd_workdir", conf->workdir);
                        /* swallow error here, that will be re-triggered in cli */

               }
               break;

        }
        case GD_OP_QUOTA:
        {
                if (ctx && !op_errstr) {
                        ret = dict_get_str (ctx, "errstr", &errstr);
                }
                break;
        }
        case GD_OP_PROFILE_VOLUME:
        {
                if (ctx && dict_get_int32 (ctx, "count", &count)) {
                        ret = dict_set_int32 (ctx, "count", 0);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "failed to set count in dictionary");
                        }
                }
                break;
        }
        case GD_OP_START_BRICK:
        case GD_OP_STOP_BRICK:
        {
                gf_log ("", GF_LOG_DEBUG, "not supported op %d", op);
                break;
        }
        case GD_OP_NONE:
        case GD_OP_MAX:
        {
                gf_log ("", GF_LOG_ERROR, "invalid operation %d", op);
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
        case GD_OP_LIST_VOLUME:
        case GD_OP_CLEARLOCKS_VOLUME:
        case GD_OP_HEAL_VOLUME:
        {
                /*nothing specific to be done*/
                break;
        }
        }

        rsp.op_ret = op_ret;
        rsp.op_errno = errno;
        if (errstr)
                rsp.op_errstr = errstr;
        else if (op_errstr)
                rsp.op_errstr = op_errstr;

        if (!rsp.op_errstr)
                rsp.op_errstr = "";

        if (ctx) {
                ret = dict_allocate_and_serialize (ctx, &rsp.dict.dict_val,
                                                   (size_t*)&rsp.dict.dict_len);
                if (ret < 0 )
                        gf_log (THIS->name, GF_LOG_ERROR, "failed to "
                                "serialize buffer");
                else
                        free_ptr = rsp.dict.dict_val;
        }

        /* needed by 'rebalance status' */
        if (status)
                rsp.op_errno = status;

        cli_rsp = &rsp;
        xdrproc = (xdrproc_t) xdr_gf_cli_rsp;

        ret = glusterd_submit_reply (req, cli_rsp, NULL, 0, NULL,
                                     xdrproc);

        if (free_ptr)
                GF_FREE (free_ptr);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd3_1_probe_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        gd1_mgmt_probe_rsp    rsp   = {{0},};
        int                   ret   = 0;
        glusterd_peerinfo_t           *peerinfo = NULL;
        glusterd_friend_sm_event_t    *event = NULL;
        glusterd_probe_ctx_t          *ctx = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_probe_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO,
                "Received probe resp from uuid: %s, host: %s",
                uuid_utoa (rsp.uuid), rsp.hostname);
        if (rsp.op_ret != 0) {
                ctx = ((call_frame_t *)myframe)->local;
                ((call_frame_t *)myframe)->local = NULL;

                GF_ASSERT (ctx);

                if (ctx->req) {
                        glusterd_xfer_cli_probe_resp (ctx->req, rsp.op_ret,
                                                      rsp.op_errno,
                                                      ctx->hostname, ctx->port);
                }

                glusterd_destroy_probe_ctx (ctx);
                (void) glusterd_friend_remove (rsp.uuid, rsp.hostname);
                ret = rsp.op_ret;
                goto out;
        }
        ret = glusterd_friend_find (rsp.uuid, rsp.hostname, &peerinfo);
        if (ret) {
                GF_ASSERT (0);
        }

        if (strncasecmp (rsp.hostname, peerinfo->hostname, 1024)) {
                gf_log (THIS->name, GF_LOG_INFO, "Host: %s  with uuid: %s "
                        "already present in cluster with alias hostname: %s",
                        rsp.hostname, uuid_utoa (rsp.uuid), peerinfo->hostname);

                ctx = ((call_frame_t *)myframe)->local;
                ((call_frame_t *)myframe)->local = NULL;

                GF_ASSERT (ctx);

                rsp.op_errno = GF_PROBE_FRIEND;
                if (ctx->req) {
                        glusterd_xfer_cli_probe_resp (ctx->req, rsp.op_ret,
                                                      rsp.op_errno,
                                                      ctx->hostname, ctx->port);
                }

                glusterd_destroy_probe_ctx (ctx);
                (void) glusterd_friend_remove (NULL, rsp.hostname);
                ret = rsp.op_ret;
                goto out;
        }

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
        ((call_frame_t *)myframe)->local = NULL;
        ret = glusterd_friend_sm_inject_event (event);


        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received resp to probe req");

out:
        if (rsp.hostname)
                free (rsp.hostname);//malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int
glusterd3_1_friend_add_cbk (struct rpc_req * req, struct iovec *iov,
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
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_log ("glusterd", GF_LOG_INFO,
                "Received %s from uuid: %s, host: %s, port: %d",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid), rsp.hostname, rsp.port);

        ret = glusterd_friend_find (rsp.uuid, rsp.hostname, &peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "received friend add response from"
                        " unknown peer uuid: %s", uuid_utoa (rsp.uuid));
                goto out;
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
        ev_ctx = GF_CALLOC (1, sizeof (*ev_ctx),
                                gf_gld_mt_friend_update_ctx_t);
        if (!ev_ctx) {
                ret = -1;
                goto out;
        }

        uuid_copy (ev_ctx->uuid, rsp.uuid);
        ev_ctx->hostname = gf_strdup (rsp.hostname);

        event->ctx = ev_ctx;
        ret = glusterd_friend_sm_inject_event (event);

        if (ret)
                goto out;

out:
        ctx = ((call_frame_t *)myframe)->local;
        ((call_frame_t *)myframe)->local = NULL;

        GF_ASSERT (ctx);

        if (ctx->req)//reverse probe doesn't have req
                ret = glusterd_xfer_cli_probe_resp (ctx->req, op_ret, op_errno,
                                                    ctx->hostname, ctx->port);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }
        if (ctx)
                glusterd_destroy_probe_ctx (ctx);
        if (rsp.hostname)
                free (rsp.hostname);//malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
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
        int32_t                         op_ret = -1;
        int32_t                         op_errno = -1;
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
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto respond;
        }

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_log ("glusterd", GF_LOG_INFO,
                "Received %s from uuid: %s, host: %s, port: %d",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid), rsp.hostname, rsp.port);

inject:
        ret = glusterd_friend_find (rsp.uuid, ctx->hostname, &peerinfo);

        if (ret) {
                //can happen as part of rpc clnt connection cleanup
                //when the frame timeout happens after 30 minutes
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

        /*friend_sm would be moved on CLNT_DISCONNECT, consequently
          cleaning up peerinfo. Else, we run the risk of triggering
          a clnt_destroy within saved_frames_unwind.
        */
        op_ret = 0;


respond:
        ret = glusterd_xfer_cli_deprobe_resp (ctx->req, op_ret, op_errno,
                                              ctx->hostname);
        if (!ret && move_sm_now) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (ctx) {
                glusterd_broadcast_friend_delete (ctx->hostname, NULL);
                glusterd_destroy_probe_ctx (ctx);
        }

        if (rsp.hostname)
                free (rsp.hostname);//malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd3_1_friend_update_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        int                           ret    = -1;
        gd1_mgmt_friend_update_rsp    rsp    = {{0}, };
        xlator_t                      *this  = NULL;

        GF_ASSERT (req);
        this = THIS;

        if (-1 == req->rpc_status) {
                gf_log (this->name, GF_LOG_ERROR, "RPC Error");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_friend_update_rsp);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to serialize friend"
                        " update repsonse");
                goto out;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_INFO, "Received %s from uuid: %s",
                (ret)?"RJT":"ACC", uuid_utoa (rsp.uuid));

        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd3_1_cluster_lock_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;

        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_INFO,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid));

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Lock response received from "
                        "unknown peer: %s", uuid_utoa (rsp.uuid));
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

        ret = glusterd_op_sm_inject_event (event_type, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

int32_t
glusterd3_1_cluster_unlock_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = -1;
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;


        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

out:
        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_INFO,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid));

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Unlock response received from "
                        "unknown peer %s", uuid_utoa (rsp.uuid));
        }

        if (op_ret) {
                event_type = GD_OP_EVENT_RCVD_RJT;
                opinfo.op_ret = op_ret;
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

        ret = glusterd_op_sm_inject_event (event_type, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

static int32_t
glusterd_append_gsync_status (dict_t *dst, dict_t *src)
{
        int                ret = 0;
        char               *stop_msg = NULL;

        ret = dict_get_str (src, "gsync-status", &stop_msg);
        if (ret) {
                ret = 0;
                goto out;
        }

        ret = dict_set_dynstr (dst, "gsync-status", gf_strdup (stop_msg));
        if (ret) {
                gf_log ("glusterd", GF_LOG_WARNING, "Unable to set the stop"
                        "message in the ctx dictionary");
                goto out;
        }

        ret = 0;
 out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int32_t
glusterd_append_status_dicts (dict_t *dst, dict_t *src)
{
        int              dst_count = 0;
        int              src_count = 0;
        int              i = 0;
        int              ret = 0;
        char             mst[PATH_MAX] = {0,};
        char             slv[PATH_MAX] = {0, };
        char             sts[PATH_MAX] = {0, };
        char             *mst_val = NULL;
        char             *slv_val = NULL;
        char             *sts_val = NULL;

        GF_ASSERT (dst);

        if (src == NULL)
                goto out;

        ret = dict_get_int32 (dst, "gsync-count", &dst_count);
        if (ret)
                dst_count = 0;

        ret = dict_get_int32 (src, "gsync-count", &src_count);
        if (ret || !src_count) {
                gf_log ("", GF_LOG_DEBUG, "Source brick empty");
                ret = 0;
                goto out;
        }

        for (i = 1; i <= src_count; i++) {
                snprintf (mst, sizeof(mst), "master%d", i);
                snprintf (slv, sizeof(slv), "slave%d", i);
                snprintf (sts, sizeof(sts), "status%d", i);

                ret = dict_get_str (src, mst, &mst_val);
                if (ret)
                        goto out;

                ret = dict_get_str (src, slv, &slv_val);
                if (ret)
                        goto out;

                ret = dict_get_str (src, sts, &sts_val);
                if (ret)
                        goto out;

                snprintf (mst, sizeof(mst), "master%d", i+dst_count);
                snprintf (slv, sizeof(slv), "slave%d", i+dst_count);
                snprintf (sts, sizeof(sts), "status%d", i+dst_count);

                ret = dict_set_dynstr (dst, mst, gf_strdup (mst_val));
                if (ret)
                        goto out;

                ret = dict_set_dynstr (dst, slv, gf_strdup (slv_val));
                if (ret)
                        goto out;

                ret = dict_set_dynstr (dst, sts, gf_strdup (sts_val));
                if (ret)
                        goto out;

        }

        ret = dict_set_int32 (dst, "gsync-count", dst_count+src_count);

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int32_t
glusterd_gsync_use_rsp_dict (dict_t *rsp_dict, char *op_errstr)
{
        dict_t             *ctx = NULL;
        int                ret = 0;

        ctx = glusterd_op_get_ctx ();
        if (!ctx) {
                gf_log ("", GF_LOG_ERROR,
                        "Operation Context is not present");
                GF_ASSERT (0);
        }

        if (rsp_dict) {
                ret = glusterd_append_status_dicts (ctx, rsp_dict);
                if (ret)
                        goto out;

                ret = glusterd_append_gsync_status (ctx, rsp_dict);
                if (ret)
                        goto out;
        }
        if (strcmp ("", op_errstr)) {
                ret = dict_set_dynstr (ctx, "errstr", gf_strdup(op_errstr));
                if (ret)
                        goto out;
        }

        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d ", ret);
        return ret;
}
static int32_t
glusterd_rb_use_rsp_dict (dict_t *rsp_dict)
{
        int32_t  src_port = 0;
        int32_t  dst_port = 0;
        int      ret      = 0;
        dict_t  *ctx      = NULL;


        ctx = glusterd_op_get_ctx ();
        if (!ctx) {
                gf_log ("", GF_LOG_ERROR,
                        "Operation Context is not present");
                GF_ASSERT (0);
        }

        if (rsp_dict) {
                ret = dict_get_int32 (rsp_dict, "src-brick-port", &src_port);
                if (ret == 0) {
                        gf_log ("", GF_LOG_DEBUG,
                                "src-brick-port=%d found", src_port);
                }

                ret = dict_get_int32 (rsp_dict, "dst-brick-port", &dst_port);
                if (ret == 0) {
                        gf_log ("", GF_LOG_DEBUG,
                                "dst-brick-port=%d found", dst_port);
                }

        }

        if (src_port) {
                ret = dict_set_int32 (ctx, "src-brick-port",
                                      src_port);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not set src-brick");
                        goto out;
                }
        }

        if (dst_port) {
                ret = dict_set_int32 (ctx, "dst-brick-port",
                                      dst_port);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "Could not set dst-brick");
                        goto out;
                }

        }

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
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        dict_t                        *dict   = NULL;
        char                          err_str[2048] = {0};
        char                          *peer_str = NULL;

        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
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

        gf_log ("glusterd", GF_LOG_INFO,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid));

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Stage response received from "
                        "unknown peer: %s", uuid_utoa (rsp.uuid));
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
                        snprintf (err_str, sizeof (err_str), "Operation failed "
                                  "on %s", peer_str);
                        opinfo.op_errstr = gf_strdup (err_str);
                }
                if (!opinfo.op_errstr) {
                        gf_log ("", GF_LOG_ERROR, "memory allocation failed");
                        ret = -1;
                        goto out;
                }
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
        }

        switch (rsp.op) {
        case GD_OP_REPLACE_BRICK:
                glusterd_rb_use_rsp_dict (dict);
                break;
        }

        ret = glusterd_op_sm_inject_event (event_type, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (rsp.op_errstr && strcmp (rsp.op_errstr, "error"))
                free (rsp.op_errstr); //malloced by xdr
        if (dict) {
                if (!dict->extra_stdfree && rsp.dict.dict_val)
                        free (rsp.dict.dict_val); //malloced by xdr
                dict_unref (dict);
        } else {
                if (rsp.dict.dict_val)
                        free (rsp.dict.dict_val); //malloced by xdr
        }
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

static int32_t
glusterd_sync_use_rsp_dict (dict_t *rsp_dict)
{
        int      ret      = 0;

        GF_ASSERT (rsp_dict);

        if (!rsp_dict) {
                goto out;
        }

        ret = glusterd_import_friend_volumes (rsp_dict);
out:
        return ret;

}

void
_profile_volume_add_friend_rsp (dict_t *this, char *key, data_t *value,
                               void *data)
{
        char    new_key[256] = {0};
        glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;
        data_t  *new_value = NULL;
        int     brick_count = 0;
        char    brick_key[256];

        if (strcmp (key, "count") == 0)
                return;
        sscanf (key, "%d%s", &brick_count, brick_key);
        rsp_ctx = data;
        new_value = data_copy (value);
        GF_ASSERT (new_value);
        snprintf (new_key, sizeof (new_key), "%d%s",
                  rsp_ctx->count + brick_count, brick_key);
        dict_set (rsp_ctx->dict, new_key, new_value);
}

int
glusterd_profile_volume_use_rsp_dict (dict_t *rsp_dict)
{
        int     ret = 0;
        glusterd_pr_brick_rsp_conv_t rsp_ctx = {0};
        int32_t brick_count = 0;
        int32_t count = 0;
        dict_t  *ctx_dict = NULL;
        glusterd_op_t   op = GD_OP_NONE;

        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (rsp_dict, "count", &brick_count);
        if (ret) {
                ret = 0; //no bricks in the rsp
                goto out;
        }

        op = glusterd_op_get_op ();
        GF_ASSERT (GD_OP_PROFILE_VOLUME == op);
        ctx_dict = glusterd_op_get_ctx ();

        ret = dict_get_int32 (ctx_dict, "count", &count);
        rsp_ctx.count = count;
        rsp_ctx.dict = ctx_dict;
        dict_foreach (rsp_dict, _profile_volume_add_friend_rsp, &rsp_ctx);
        dict_del (ctx_dict, "count");
        ret = dict_set_int32 (ctx_dict, "count", count + brick_count);
out:
        return ret;
}

void
glusterd_volume_status_add_peer_rsp (dict_t *this, char *key, data_t *value,
                                     void *data)
{
        glusterd_status_rsp_conv_t      *rsp_ctx = NULL;
        data_t                          *new_value = NULL;
        char                            brick_key[1024] = {0,};
        char                            new_key[1024] = {0,};
        int32_t                         ret = 0;

        if (!strcmp (key, "count") || !strcmp (key, "cmd"))
                return;

        rsp_ctx = data;
        new_value = data_copy (value);
        GF_ASSERT (new_value);

        if (rsp_ctx->nfs) {
                sscanf (key, "brick%*d.%s", brick_key);
                snprintf (new_key, sizeof (new_key), "brick%d.%s",
                          rsp_ctx->count, brick_key);
        } else
                strncpy (new_key, key, sizeof (new_key));

        ret = dict_set (rsp_ctx->dict, new_key, new_value);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "Unable to set key: %s in dict",
                        key);

        return;
}

int
glusterd_volume_status_use_rsp_dict (dict_t *rsp_dict)
{
        int                             ret = 0;
        glusterd_status_rsp_conv_t      rsp_ctx = {0};
        int32_t                         brick_count = 0;
        int32_t                         count = 0;
        int32_t                         cmd = 0;
        dict_t                          *ctx_dict = NULL;
        glusterd_op_t                   op = GD_OP_NONE;

        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (rsp_dict, "count", &brick_count);
        if (ret) {
                ret = 0; //no bricks in the rsp
                goto out;
        }

        ret = dict_get_int32 (rsp_dict, "cmd", &cmd);
        if (ret)
                goto out;

        op = glusterd_op_get_op ();
        GF_ASSERT (GD_OP_STATUS_VOLUME == op);
        ctx_dict = glusterd_op_get_ctx (op);

        ret = dict_get_int32 (ctx_dict, "count", &count);
        rsp_ctx.count = count;
        rsp_ctx.dict = ctx_dict;
        if (cmd & GF_CLI_STATUS_NFS)
                rsp_ctx.nfs = _gf_true;
        else
                rsp_ctx.nfs = _gf_false;

        dict_foreach (rsp_dict, glusterd_volume_status_add_peer_rsp, &rsp_ctx);

        ret = dict_set_int32 (ctx_dict, "count", count + brick_count);
out:
        return ret;
}

int
glusterd_volume_rebalance_use_rsp_dict (dict_t *rsp_dict)
{
        int            ret      = 0;
        dict_t        *ctx_dict = NULL;
        glusterd_op_t  op       = GD_OP_NONE;
        uint64_t       value    = 0;
        int32_t        value32  = 0;
        char          *volname  = NULL;
        glusterd_volinfo_t *volinfo = NULL;
        char           key[256] = {0,};
        int32_t        index    = 0;
        int32_t        i        = 0;
        char          *node_uuid = NULL;
        char          *node_uuid_str = NULL;

        GF_ASSERT (rsp_dict);

        op = glusterd_op_get_op ();
        GF_ASSERT ((GD_OP_REBALANCE == op) ||
                   (GD_OP_DEFRAG_BRICK_VOLUME == op));

        ctx_dict = glusterd_op_get_ctx (op);

        if (!ctx_dict)
                goto out;

        ret = dict_get_int32 (ctx_dict, "count", &i);
        i++;
        ret = dict_set_int32 (ctx_dict, "count", i);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "Failed to set index");

        ret = dict_get_str (ctx_dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret  = glusterd_volinfo_find (volname, &volinfo);

        if (ret)
                goto out;

        ret = dict_get_int32 (rsp_dict, "count", &index);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "failed to get index");

        snprintf (key, 256, "files-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "files-%d", i);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set the file count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "size-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "size-%d", i);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set the size of migration");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "lookups-%d", index);
        ret = dict_get_uint64 (rsp_dict, key, &value);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "lookups-%d", i);
                ret = dict_set_uint64 (ctx_dict, key, value);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set lookuped file count");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "status-%d", index);
        ret = dict_get_int32 (rsp_dict, key, &value32);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "status-%d", i);
                ret = dict_set_int32 (ctx_dict, key, value32);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set status");
                }
        }

        memset (key, 0, 256);
        snprintf (key, 256, "node-uuid-%d", index);
        ret = dict_get_str (rsp_dict, key, &node_uuid);
        if (!ret) {
                memset (key, 0, 256);
                snprintf (key, 256, "node-uuid-%d", i);
                node_uuid_str = gf_strdup (node_uuid);
                ret = dict_set_dynstr (ctx_dict, key, node_uuid_str);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "failed to set node-uuid");
                }
        }
        ret = 0;

out:
        return ret;
}

int
glusterd_volume_heal_use_rsp_dict (dict_t *rsp_dict)
{
        int            ret      = 0;
        dict_t        *ctx_dict = NULL;
        glusterd_op_t  op       = GD_OP_NONE;

        GF_ASSERT (rsp_dict);

        op = glusterd_op_get_op ();
        GF_ASSERT (GD_OP_HEAL_VOLUME == op);

        ctx_dict = glusterd_op_get_ctx (op);

        if (!ctx_dict)
                goto out;
        dict_copy (rsp_dict, ctx_dict);
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
        glusterd_op_sm_event_type_t   event_type = GD_OP_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        dict_t                        *dict = NULL;
        char                          err_str[2048] = {0};
        char                          *peer_str = NULL;


        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
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
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize rsp-buffer to dictionary");
			event_type = GD_OP_EVENT_RCVD_RJT;
                        goto out;
                } else {
                        dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_INFO,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", uuid_utoa (rsp.uuid));

        ret = glusterd_friend_find (rsp.uuid, NULL, &peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Commit response received from "
                        "unknown peer: %s", uuid_utoa (rsp.uuid));
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
                        snprintf (err_str, sizeof (err_str), "Operation failed "
                                  "on %s", peer_str);
                        opinfo.op_errstr = gf_strdup (err_str);
                }
                if (!opinfo.op_errstr) {
                        gf_log ("", GF_LOG_ERROR, "memory allocation failed");
                        ret = -1;
                        goto out;
                }
        } else {
                event_type = GD_OP_EVENT_RCVD_ACC;
                switch (rsp.op) {
                case GD_OP_REPLACE_BRICK:
                        ret = glusterd_rb_use_rsp_dict (dict);
                        if (ret)
                                goto out;
                break;

                case GD_OP_SYNC_VOLUME:
                        ret = glusterd_sync_use_rsp_dict (dict);
                        if (ret)
                                goto out;
                break;

                case GD_OP_PROFILE_VOLUME:
                        ret = glusterd_profile_volume_use_rsp_dict (dict);
                        if (ret)
                                goto out;
                break;

                case GD_OP_GSYNC_SET:
                        ret = glusterd_gsync_use_rsp_dict (dict, rsp.op_errstr);
                        if (ret)
                                goto out;
                break;

                case GD_OP_STATUS_VOLUME:
                        ret = glusterd_volume_status_use_rsp_dict (dict);
                        if (ret)
                                goto out;
                break;

                case GD_OP_REBALANCE:
                case GD_OP_DEFRAG_BRICK_VOLUME:
                        ret = glusterd_volume_rebalance_use_rsp_dict (dict);
                        if (ret)
                                goto out;
                break;

                case GD_OP_HEAL_VOLUME:
                        ret = glusterd_volume_heal_use_rsp_dict (dict);
                        if (ret)
                                goto out;

                break;

                default:
                break;
                }
        }

out:
        ret = glusterd_op_sm_inject_event (event_type, NULL);

        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (dict)
                dict_unref (dict);
        if (rsp.op_errstr && strcmp (rsp.op_errstr, "error"))
                free (rsp.op_errstr); //malloced by xdr
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}



int32_t
glusterd3_1_probe (call_frame_t *frame, xlator_t *this,
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

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = gf_strdup (hostname);
        req.port = port;

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame, peerinfo->peer,
                                       GLUSTERD_PROBE_QUERY,
                                       NULL, this, glusterd3_1_probe_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_probe_req);

out:
        if (req.hostname)
                GF_FREE (req.hostname);
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd3_1_friend_add (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        gd1_mgmt_friend_req         req      = {{0},};
        int                         ret      = 0;
        glusterd_peerinfo_t        *peerinfo = NULL;
        glusterd_conf_t            *priv     = NULL;
        glusterd_friend_sm_event_t *event    = NULL;
        dict_t                     *vols     = NULL;


        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        peerinfo = event->peerinfo;

        ret = glusterd_build_volume_dict (&vols);
        if (ret)
                goto out;

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = peerinfo->hostname;
        req.port = peerinfo->port;

        ret = dict_allocate_and_serialize (vols, &req.vols.vols_val,
                                           (size_t *)&req.vols.vols_len);
        if (ret)
                goto out;

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame, peerinfo->peer,
                                       GLUSTERD_FRIEND_ADD,
                                       NULL, this, glusterd3_1_friend_add_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_friend_req);


out:
        if (req.vols.vols_val)
                GF_FREE (req.vols.vols_val);

        if (vols)
                dict_unref (vols);

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

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        peerinfo = event->peerinfo;

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = peerinfo->hostname;
        req.port = peerinfo->port;
        ret = glusterd_submit_request (peerinfo->rpc, &req, frame, peerinfo->peer,
                                       GLUSTERD_FRIEND_REMOVE, NULL,
                                       this, glusterd3_1_friend_remove_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_friend_req);

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd3_1_friend_update (call_frame_t *frame, xlator_t *this,
                           void *data)
{
        gd1_mgmt_friend_update  req         = {{0},};
        int                     ret         = 0;
        glusterd_conf_t        *priv        = NULL;
        dict_t                 *friends     = NULL;
        char                   *dict_buf    = NULL;
        size_t                  len         = -1;
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

        ret = dict_allocate_and_serialize (friends, &dict_buf, (size_t *)&len);
        if (ret)
                goto out;

        req.friends.friends_val = dict_buf;
        req.friends.friends_len = len;

        uuid_copy (req.uuid, priv->uuid);

        dummy_frame = create_frame (this, this->ctx->pool);
        ret = glusterd_submit_request (peerinfo->rpc, &req, dummy_frame,
                                       peerinfo->peer,
                                       GLUSTERD_FRIEND_UPDATE, NULL,
                                       this, glusterd3_1_friend_update_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_friend_update);

out:
        if (req.friends.friends_val)
                GF_FREE (req.friends.friends_val);

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_cluster_lock (call_frame_t *frame, xlator_t *this,
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
                                       this, glusterd3_1_cluster_lock_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req);
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_cluster_unlock (call_frame_t *frame, xlator_t *this,
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
                                       this, glusterd3_1_cluster_unlock_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_req);
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_stage_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_stage_op_req           req = {{0,},};
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        call_frame_t                    *dummy_frame = NULL;
        dict_t                          *dict = NULL;
        gf_boolean_t                    is_alloc = _gf_true;

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
                                           (size_t *)&req.buf.buf_len);
        if (ret)
                goto out;


        dummy_frame = create_frame (this, this->ctx->pool);
        if (!dummy_frame)
                goto out;

        ret = glusterd_submit_request (peerinfo->rpc, &req, dummy_frame,
                                       peerinfo->mgmt, GLUSTERD_MGMT_STAGE_OP,
                                       NULL,
                                       this, glusterd3_1_stage_op_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_stage_op_req);

out:
        if ((_gf_true == is_alloc) && req.buf.buf_val)
                GF_FREE (req.buf.buf_val);

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_commit_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_commit_op_req  req         = {{0,},};
        int                     ret         = -1;
        glusterd_peerinfo_t    *peerinfo    = NULL;
        glusterd_conf_t        *priv        = NULL;
        call_frame_t           *dummy_frame = NULL;
        dict_t                 *dict        = NULL;
        gf_boolean_t            is_alloc    = _gf_true;

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
                                           (size_t *)&req.buf.buf_len);
        if (ret)
                goto out;

        dummy_frame = create_frame (this, this->ctx->pool);
        if (!dummy_frame)
                goto out;

        ret = glusterd_submit_request (peerinfo->rpc, &req, dummy_frame,
                                       peerinfo->mgmt, GLUSTERD_MGMT_COMMIT_OP,
                                       NULL,
                                       this, glusterd3_1_commit_op_cbk,
                                       (xdrproc_t)xdr_gd1_mgmt_commit_op_req);

out:
        if ((_gf_true == is_alloc) && req.buf.buf_val)
                GF_FREE (req.buf.buf_val);

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd3_1_brick_op_cbk (struct rpc_req *req, struct iovec *iov,
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

        GF_ASSERT (req);
        frame = myframe;
        req_ctx = frame->local;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret =  xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
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
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
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
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Error setting index on brick status rsp dict");
                        rsp.op_ret = -1;
                        event_type = GD_OP_EVENT_RCVD_RJT;
                        goto out;
                }
        }
out:
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
        ret = glusterd_op_sm_inject_event (event_type, ev_ctx);
        if (!ret) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        if (ret && dict)
                dict_unref (dict);
        if (rsp.op_errstr && strcmp (rsp.op_errstr, "error"))
                free (rsp.op_errstr); //malloced by xdr
        GLUSTERD_STACK_DESTROY (frame);
        return ret;
}

int32_t
glusterd3_1_brick_op (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gd1_mgmt_brick_op_req           *req = NULL;
        int                             ret = 0;
        glusterd_conf_t                 *priv = NULL;
        call_frame_t                    *dummy_frame = NULL;
        char                            *op_errstr = NULL;
        int                             pending_bricks = 0;
        glusterd_pending_node_t         *pending_node;
        glusterd_req_ctx_t              *req_ctx = NULL;
        struct rpc_clnt                 *rpc = NULL;
        dict_t                          *op_ctx = NULL;

        if (!this) {
                ret = -1;
                goto out;
        }
        priv = this->private;
        GF_ASSERT (priv);

        req_ctx = data;
        GF_ASSERT (req_ctx);
        INIT_LIST_HEAD (&opinfo.pending_bricks);
        ret = glusterd_op_bricks_select (req_ctx->op, req_ctx->dict, &op_errstr);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Brick Op failed");
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        list_for_each_entry (pending_node, &opinfo.pending_bricks, list) {
                dummy_frame = create_frame (this, this->ctx->pool);
                if (!dummy_frame)
                        continue;

                if (pending_node->type == GD_NODE_NFS)
                        ret = glusterd_nfs_op_build_payload
                                (req_ctx->op,
                                 (gd1_mgmt_brick_op_req **)&req,
                                 req_ctx->dict);
                else
                        ret = glusterd_brick_op_build_payload
                                (req_ctx->op, pending_node->node,
                                 (gd1_mgmt_brick_op_req **)&req,
                                 req_ctx->dict);

                if (ret)
                        goto out;

                dummy_frame->local = data;
                dummy_frame->cookie = pending_node;

                rpc = glusterd_pending_node_get_rpc (pending_node);
                if (!rpc) {
                        if (pending_node->type == GD_NODE_REBALANCE) {
                                opinfo.brick_pending_count = 0;
                                ret = 0;
                                if (req) {
                                        if (req->input.input_val)
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
                        gf_log (this->name, GF_LOG_ERROR, "Brick Op failed "
                                "due to rpc failure.");
                        goto out;
                }

                ret = glusterd_submit_request (rpc, req, dummy_frame,
                                               priv->gfs_mgmt,
                                               req->op, NULL,
                                               this, glusterd3_1_brick_op_cbk,
                                               (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
                if (req) {
                        if (req->input.input_val)
                                GF_FREE (req->input.input_val);
                        GF_FREE (req);
                        req = NULL;
                }
                if (!ret)
                        pending_bricks++;
        }

        gf_log ("glusterd", GF_LOG_DEBUG, "Sent op req to %d bricks",
                pending_bricks);
        opinfo.brick_pending_count = pending_bricks;

out:
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, data);
                opinfo.op_ret = ret;
        }
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

struct rpc_clnt_procedure gd_brick_actors[GLUSTERD_BRICK_MAXVALUE] = {
        [GLUSTERD_BRICK_NULL] = {"NULL", NULL },
        [GLUSTERD_BRICK_OP]   = {"BRICK_OP", glusterd3_1_brick_op},
};

struct rpc_clnt_procedure gd_peer_actors[GLUSTERD_FRIEND_MAXVALUE] = {
        [GLUSTERD_FRIEND_NULL]   = {"NULL", NULL },
        [GLUSTERD_PROBE_QUERY]   = {"PROBE_QUERY", glusterd3_1_probe},
        [GLUSTERD_FRIEND_ADD]    = {"FRIEND_ADD", glusterd3_1_friend_add},
        [GLUSTERD_FRIEND_REMOVE] = {"FRIEND_REMOVE", glusterd3_1_friend_remove},
        [GLUSTERD_FRIEND_UPDATE] = {"FRIEND_UPDATE", glusterd3_1_friend_update},
};

struct rpc_clnt_procedure gd_mgmt_actors[GLUSTERD_MGMT_MAXVALUE] = {
        [GLUSTERD_MGMT_NULL]           = {"NULL", NULL },
        [GLUSTERD_MGMT_CLUSTER_LOCK]   = {"CLUSTER_LOCK", glusterd3_1_cluster_lock},
        [GLUSTERD_MGMT_CLUSTER_UNLOCK] = {"CLUSTER_UNLOCK", glusterd3_1_cluster_unlock},
        [GLUSTERD_MGMT_STAGE_OP]       = {"STAGE_OP", glusterd3_1_stage_op},
        [GLUSTERD_MGMT_COMMIT_OP]      = {"COMMIT_OP", glusterd3_1_commit_op},
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

