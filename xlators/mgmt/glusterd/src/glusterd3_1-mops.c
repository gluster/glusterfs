/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpc-clnt.h"
#include "glusterd1-xdr.h"
#include "glusterd1.h"

#include "compat-errno.h"
#include "glusterd-op-sm.h"
#include "glusterd-sm.h"
#include "glusterd.h"
#include "protocol-common.h"
#include "glusterd-utils.h"
#include "common-utils.h"
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
        glusterd_peerinfo_t           *peerinfo = NULL;
        glusterd_friend_sm_event_t    *event = NULL;
        glusterd_probe_ctx_t          *ctx = NULL;

        conf  = THIS->private;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gd_xdr_to_mgmt_probe_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL,
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

        gf_log ("glusterd", GF_LOG_NORMAL, "Received resp to probe req");

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
        glusterd_conf_t               *conf = NULL;
        int                           ret   = -1;
        glusterd_friend_sm_event_t        *event = NULL;
        glusterd_friend_sm_event_type_t    event_type = GD_FRIEND_EVENT_NONE;
        glusterd_peerinfo_t           *peerinfo = NULL;
        int32_t                       op_ret = -1;
        int32_t                       op_errno = -1;
        glusterd_probe_ctx_t          *ctx = NULL;
        glusterd_friend_update_ctx_t  *ev_ctx = NULL;

        conf  = THIS->private;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = gd_xdr_to_mgmt_friend_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_log ("glusterd", GF_LOG_NORMAL,
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

        if (ctx->req)//reverse probe doesnt have req
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

        conf  = THIS->private;
        GF_ASSERT (conf);

        ctx = ((call_frame_t *)myframe)->local;
        ((call_frame_t *)myframe)->local = NULL;
        GF_ASSERT (ctx);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto inject;
        }

        ret = gd_xdr_to_mgmt_friend_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto respond;
        }

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

        gf_log ("glusterd", GF_LOG_NORMAL,
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

        glusterd_friend_sm ();
        glusterd_op_sm ();

        op_ret = 0;


respond:
        ret = glusterd_xfer_cli_deprobe_resp (ctx->req, op_ret, op_errno,
                                              ctx->hostname);
        if (!ret) {
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
        gd1_mgmt_cluster_lock_rsp     rsp   = {{0},};
        int                           ret   = -1;
        int32_t                       op_ret = 0;
        char                          str[50] = {0,};

        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

/*        ret = gd_xdr_to_mgmt_friend_update_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        uuid_unparse (rsp.uuid, str);

        op_ret = rsp.op_ret;
*/
        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received %s from uuid: %s",
                (op_ret)?"RJT":"ACC", str);

out:
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

        ret = gd_xdr_to_mgmt_cluster_lock_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
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

out:
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

        ret = gd_xdr_to_mgmt_cluster_unlock_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
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

out:
        GLUSTERD_STACK_DESTROY (((call_frame_t *)myframe));
        return ret;
}

static int32_t
glusterd_rb_use_rsp_dict (dict_t *rsp_dict)
{
        int32_t  src_port = 0;
        int32_t  dst_port = 0;
        int      ret      = 0;
        dict_t  *ctx      = NULL;


        ctx = glusterd_op_get_ctx (GD_OP_REPLACE_BRICK);
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

        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
                goto out;
        }

        ret = gd_xdr_to_mgmt_stage_op_rsp (*iov, &rsp);
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

        op_ret = rsp.op_ret;

        gf_log ("glusterd", GF_LOG_NORMAL,
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
                opinfo.op_errstr = gf_strdup(rsp.op_errstr);
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

out:
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


        GF_ASSERT (req);

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                rsp.op_errstr = "error";
		event_type = GD_OP_EVENT_RCVD_RJT;
                goto out;
        }

        ret = gd_xdr_to_mgmt_commit_op_rsp (*iov, &rsp);
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

        gf_log ("glusterd", GF_LOG_NORMAL,
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
                opinfo.op_errstr = gf_strdup(rsp.op_errstr);
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

        ret = glusterd_friend_find (NULL, hostname, &peerinfo);

        if (ret) {
                //We should not reach this state ideally
                GF_ASSERT (0);
                goto out;
        }

        uuid_copy (req.uuid, priv->uuid);
        req.hostname = gf_strdup (hostname);
        req.port = port;

        ret = glusterd_submit_request (peerinfo, &req, frame, priv->mgmt,
                                       GD_MGMT_PROBE_QUERY,
                                       NULL, gd_xdr_from_mgmt_probe_req,
                                       this, glusterd3_1_probe_cbk);

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
        gd1_mgmt_friend_req     req = {{0},};
        int                     ret = 0;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_friend_sm_event_t     *event = NULL;
        glusterd_friend_req_ctx_t *ctx = NULL;
        dict_t                  *vols = NULL;


        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        ctx = event->ctx;

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

        ret = glusterd_submit_request (peerinfo, &req, frame, priv->mgmt,
                                       GD_MGMT_FRIEND_ADD,
                                       NULL, gd_xdr_from_mgmt_friend_req,
                                       this, glusterd3_1_friend_add_cbk);


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
        glusterd_probe_ctx_t            *ctx = NULL;
        glusterd_friend_sm_event_t      *new_event = NULL;
        glusterd_friend_sm_event_type_t event_type = GD_FRIEND_EVENT_NONE;


        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        event = data;
        priv = this->private;

        GF_ASSERT (priv);

        ctx = event->ctx;

        peerinfo = event->peerinfo;

        ret = -1;
        if (peerinfo->connected) {
                uuid_copy (req.uuid, priv->uuid);
                req.hostname = peerinfo->hostname;
                req.port = peerinfo->port;
                ret = glusterd_submit_request (peerinfo, &req, frame, priv->mgmt,
                                               GD_MGMT_FRIEND_REMOVE,
                                               NULL, gd_xdr_from_mgmt_friend_req,
                                               this, glusterd3_1_friend_remove_cbk);
        } else {
                event_type = GD_FRIEND_EVENT_REMOVE_FRIEND;

                ret = glusterd_friend_sm_new_event (event_type, &new_event);

                if (!ret) {
                        new_event->peerinfo = peerinfo;
                        ret = glusterd_friend_sm_inject_event (new_event);
                } else {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                 "Unable to get event");
                }

                if (ctx)
                        ret = glusterd_xfer_cli_deprobe_resp (ctx->req, ret, 0,
                                                              ctx->hostname);
                glusterd_friend_sm ();
                glusterd_op_sm ();

                if (ctx) {
                        glusterd_broadcast_friend_delete (ctx->hostname, NULL);
                        glusterd_destroy_probe_ctx (ctx);
                }
        }

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd3_1_friend_update (call_frame_t *frame, xlator_t *this,
                           void *data)
{
        gd1_mgmt_friend_update          req = {{0},};
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        glusterd_friend_update_ctx_t     *ctx = NULL;
        dict_t                          *friends = NULL;
        char                            key[100] = {0,};
        char                            *dup_buf = NULL;
        int32_t                         count = 0;
        char                            *dict_buf = NULL;
        size_t                         len = -1;
        call_frame_t                    *dummy_frame = NULL;


        if ( !this || !data) {
                ret = -1;
                goto out;
        }

        ctx = data;
        friends = dict_new ();
        if (!friends)
                goto out;

        priv = this->private;

        GF_ASSERT (priv);

        snprintf (key, sizeof (key), "op");
        ret = dict_set_int32 (friends, key, ctx->op);
        if (ret)
                goto out;

        if (GD_FRIEND_UPDATE_ADD == ctx->op) {
                list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                        count++;
                        snprintf (key, sizeof (key), "friend%d.uuid", count);
                        dup_buf = gf_strdup (uuid_utoa (peerinfo->uuid));
                        ret = dict_set_dynstr (friends, key, dup_buf);
                        if (ret)
                                goto out;
                        snprintf (key, sizeof (key), "friend%d.hostname", count);
                        ret = dict_set_str (friends, key, peerinfo->hostname);
                        if (ret)
                                goto out;
                        gf_log ("", GF_LOG_NORMAL, "Added uuid: %s, host: %s",
                                dup_buf, peerinfo->hostname);
                }
        } else {
                snprintf (key, sizeof (key), "hostname");
                ret = dict_set_str (friends, key, ctx->hostname);
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;


        ret = dict_allocate_and_serialize (friends, &dict_buf, (size_t *)&len);

        if (ret)
                goto out;

        req.friends.friends_val = dict_buf;
        req.friends.friends_len = len;

        uuid_copy (req.uuid, priv->uuid);

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                if (!peerinfo->connected)
                        continue;
                dummy_frame = create_frame (this, this->ctx->pool);
                ret = glusterd_submit_request (peerinfo, &req, dummy_frame,
                                               priv->mgmt,
                                               GD_MGMT_FRIEND_UPDATE,
                                               NULL, gd_xdr_from_mgmt_friend_update,
                                               this, glusterd3_1_friend_update_cbk);
        }

out:
        if (friends)
                dict_unref (friends);
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

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
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

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
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
        char                            *op_errstr = NULL;

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

                ret = glusterd_op_sm_inject_event
                        (GD_OP_EVENT_STAGE_ACC, NULL);

                return ret;
        }

        glusterd_op_clear_pending_op (i);


        ret = glusterd_op_build_payload (i, &req);

        if (ret)
                goto out;

        /* rsp_dict NULL from source */
        ret = glusterd_op_stage_validate (req, &op_errstr, NULL);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Staging failed");
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
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
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }
        if (req) {
                GF_FREE (req->buf.buf_val);
                GF_FREE (req);
        }
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
        char                            *op_errstr = NULL;

        if (!this) {
                ret = -1;
                goto out;
        }

        priv = this->private;

        GF_ASSERT (priv);

        for ( i = GD_OP_NONE; i < GD_OP_MAX; i++) {
                if (opinfo.commit_op[i])
                        break;
        }

        if (GD_OP_MAX == i) {
                //No pending ops, return
                return ret;
        }

        glusterd_op_clear_commit_op (i);

        ret = glusterd_op_build_payload (i, (gd1_mgmt_stage_op_req **)&req);

        if (ret)
                goto out;

        ret = glusterd_op_commit_perform ((gd1_mgmt_stage_op_req *)req, &op_errstr,
                                          NULL);//rsp_dict invalid for source

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Commit failed");
                opinfo.op_errstr = op_errstr;
                goto out;
        }

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                GF_ASSERT (peerinfo);

                if (!peerinfo->connected)
                        continue;
                if ((peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED) &&
                    (glusterd_op_get_op() != GD_OP_SYNC_VOLUME))
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
        if (ret) {
                glusterd_op_sm_inject_event (GD_OP_EVENT_RCVD_RJT, NULL);
                opinfo.op_ret = ret;
        }
        if (req) {
                GF_FREE (req->buf.buf_val);
                GF_FREE (req);
        }
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

rpcsvc_actor_t glusterd1_mgmt_actors[] = {
        [GD_MGMT_NULL]        = { "NULL",       GD_MGMT_NULL, glusterd_null, NULL, NULL},
        [GD_MGMT_PROBE_QUERY] = { "PROBE_QUERY", GD_MGMT_PROBE_QUERY, glusterd_handle_probe_query, NULL, NULL},
        [GD_MGMT_FRIEND_ADD] = { "FRIEND_ADD", GD_MGMT_FRIEND_ADD, glusterd_handle_incoming_friend_req, NULL, NULL},
        [GD_MGMT_FRIEND_REMOVE] = { "FRIEND_REMOVE", GD_MGMT_FRIEND_REMOVE, glusterd_handle_incoming_unfriend_req, NULL, NULL},
        [GD_MGMT_FRIEND_UPDATE] = { "FRIEND_UPDATE", GD_MGMT_FRIEND_UPDATE, glusterd_handle_friend_update, NULL, NULL},
        [GD_MGMT_CLUSTER_LOCK] = { "CLUSTER_LOCK", GD_MGMT_CLUSTER_LOCK, glusterd_handle_cluster_lock, NULL, NULL},
        [GD_MGMT_CLUSTER_UNLOCK] = { "CLUSTER_UNLOCK", GD_MGMT_CLUSTER_UNLOCK, glusterd_handle_cluster_unlock, NULL, NULL},
        [GD_MGMT_STAGE_OP] = { "STAGE_OP", GD_MGMT_STAGE_OP, glusterd_handle_stage_op, NULL, NULL},
        [GD_MGMT_COMMIT_OP] = { "COMMIT_OP", GD_MGMT_COMMIT_OP, glusterd_handle_commit_op, NULL, NULL},
        [GD_MGMT_CLI_PROBE] = { "CLI_PROBE", GD_MGMT_CLI_PROBE, glusterd_handle_cli_probe, NULL, NULL},
        [GD_MGMT_CLI_CREATE_VOLUME] = { "CLI_CREATE_VOLUME", GD_MGMT_CLI_CREATE_VOLUME, glusterd_handle_create_volume, NULL,NULL},
        [GD_MGMT_CLI_DEFRAG_VOLUME] = { "CLI_DEFRAG_VOLUME", GD_MGMT_CLI_DEFRAG_VOLUME, glusterd_handle_defrag_volume, NULL,NULL},
        [GD_MGMT_CLI_DEPROBE] = { "FRIEND_REMOVE", GD_MGMT_CLI_DEPROBE, glusterd_handle_cli_deprobe, NULL, NULL},
        [GD_MGMT_CLI_LIST_FRIENDS] = { "LIST_FRIENDS", GD_MGMT_CLI_LIST_FRIENDS, glusterd_handle_cli_list_friends, NULL, NULL},
        [GD_MGMT_CLI_START_VOLUME] = { "START_VOLUME", GD_MGMT_CLI_START_VOLUME, glusterd_handle_cli_start_volume, NULL, NULL},
        [GD_MGMT_CLI_STOP_VOLUME] = { "STOP_VOLUME", GD_MGMT_CLI_STOP_VOLUME, glusterd_handle_cli_stop_volume, NULL, NULL},
        [GD_MGMT_CLI_DELETE_VOLUME] = { "DELETE_VOLUME", GD_MGMT_CLI_DELETE_VOLUME, glusterd_handle_cli_delete_volume, NULL, NULL},
        [GD_MGMT_CLI_GET_VOLUME] = { "GET_VOLUME", GD_MGMT_CLI_GET_VOLUME, glusterd_handle_cli_get_volume, NULL, NULL},
        [GD_MGMT_CLI_ADD_BRICK] = { "ADD_BRICK", GD_MGMT_CLI_ADD_BRICK, glusterd_handle_add_brick, NULL, NULL},
        [GD_MGMT_CLI_REPLACE_BRICK] = { "REPLACE_BRICK", GD_MGMT_CLI_REPLACE_BRICK, glusterd_handle_replace_brick, NULL, NULL},
        [GD_MGMT_CLI_REMOVE_BRICK] = { "REMOVE_BRICK", GD_MGMT_CLI_REMOVE_BRICK, glusterd_handle_remove_brick, NULL, NULL},
        [GD_MGMT_CLI_LOG_FILENAME] = { "LOG FILENAME", GD_MGMT_CLI_LOG_FILENAME, glusterd_handle_log_filename, NULL, NULL},
        [GD_MGMT_CLI_LOG_LOCATE] = { "LOG LOCATE", GD_MGMT_CLI_LOG_LOCATE, glusterd_handle_log_locate, NULL, NULL},
        [GD_MGMT_CLI_LOG_ROTATE] = { "LOG FILENAME", GD_MGMT_CLI_LOG_ROTATE, glusterd_handle_log_rotate, NULL, NULL},
        [GD_MGMT_CLI_SET_VOLUME] = { "SET_VOLUME", GD_MGMT_CLI_SET_VOLUME, glusterd_handle_set_volume, NULL, NULL},
        [GD_MGMT_CLI_SYNC_VOLUME] = { "SYNC_VOLUME", GD_MGMT_CLI_SYNC_VOLUME, glusterd_handle_sync_volume, NULL, NULL},
        [GD_MGMT_CLI_RESET_VOLUME] = { "RESET_VOLUME", GD_MGMT_CLI_RESET_VOLUME, glusterd_handle_reset_volume, NULL, NULL},
        [GD_MGMT_CLI_FSM_LOG] = { "FSM_LOG", GD_MGMT_CLI_FSM_LOG, glusterd_handle_fsm_log, NULL, NULL},
        [GD_MGMT_CLI_GSYNC_SET] = {"GSYNC_SET", GD_MGMT_CLI_GSYNC_SET, glusterd_handle_gsync_set, NULL, NULL},
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
        [GD_MGMT_FRIEND_UPDATE]  = { "FRIEND_UPDATE",  glusterd3_1_friend_update},
};



struct rpc_clnt_program glusterd3_1_mgmt_prog = {
        .progname = "Mgmt 3.1",
        .prognum  = GLUSTERD1_MGMT_PROGRAM,
        .progver  = GLUSTERD1_MGMT_VERSION,
        .proctable    = glusterd3_1_clnt_mgmt_actors,
        .numproc  = GLUSTERD1_MGMT_PROCCNT,
};
