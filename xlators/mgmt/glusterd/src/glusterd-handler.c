/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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
#include <inttypes.h>


#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"

#include "glusterd1.h"
#include "cli1.h"
#include "rpc-clnt.h"
#include "glusterd1-xdr.h"
#include "glusterd-volgen.h"

#include <sys/resource.h>
#include <inttypes.h>

#include "defaults.c"
#include "common-utils.h"

static int
glusterd_handle_friend_req (rpcsvc_request_t *req, uuid_t  uuid,
                            char *hostname, int port,
                            gd1_mgmt_friend_req *friend_req)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;
        char                            rhost[UNIX_PATH_MAX + 1] = {0};
        uuid_t                          friend_uuid = {0};
        dict_t                          *dict = NULL;

        uuid_parse (uuid_utoa (uuid), friend_uuid);
        if (!port)
                port = GF_DEFAULT_BASE_PORT;

        ret = glusterd_remote_hostname_get (req, rhost, sizeof (rhost));
        ret = glusterd_friend_find (uuid, rhost, &peerinfo);

        if (ret) {
                ret = glusterd_xfer_friend_add_resp (req, rhost, port, -1,
                                                     GF_PROBE_UNKNOWN_PEER);
                if (friend_req->vols.vols_val)
                        free (friend_req->vols.vols_val);
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_FRIEND_REQ, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "event generation failed: %d", ret);
                return ret;
        }

        event->peerinfo = peerinfo;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (friend_req->vols.vols_val,
                                friend_req->vols.vols_len,
                                &dict);

        if (ret)
                goto out;
        else
                dict->extra_stdfree = friend_req->vols.vols_val;

        ctx->vols = dict;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                if (ctx)
                        GF_FREE (ctx);
                if (dict) {
                        if ((!dict->extra_stdfree) &&
                            friend_req->vols.vols_val)
                                free (friend_req->vols.vols_val);
                        dict_unref (dict);
                } else {
                    if (friend_req->vols.vols_val)
                        free (friend_req->vols.vols_val);
                }
                if (event)
                        GF_FREE (event);
        } else {
                if (peerinfo && (0 == peerinfo->connected))
                        ret = GLUSTERD_CONNECTION_AWAITED;
        }
        return ret;
}

static int
glusterd_handle_unfriend_req (rpcsvc_request_t *req, uuid_t  uuid,
                              char *hostname, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = GF_DEFAULT_BASE_PORT;

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Received remove-friend from unknown peer %s",
                        hostname);
                ret = glusterd_xfer_friend_remove_resp (req, hostname,
                                                        port);
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "event generation failed: %d", ret);
                return ret;
        }

        event->peerinfo = peerinfo;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                if (ctx)
                        GF_FREE (ctx);
        }

        return ret;
}

static int
glusterd_add_peer_detail_to_dict (glusterd_peerinfo_t   *peerinfo,
                                  dict_t  *friends, int   count)
{

        int             ret = -1;
        char            key[256] = {0, };

        GF_ASSERT (peerinfo);
        GF_ASSERT (friends);

        snprintf (key, 256, "friend%d.uuid", count);
        uuid_utoa_r (peerinfo->uuid, peerinfo->uuid_str);
        ret = dict_set_str (friends, key, peerinfo->uuid_str);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.hostname", count);
        ret = dict_set_str (friends, key, peerinfo->hostname);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.port", count);
        ret = dict_set_int32 (friends, key, peerinfo->port);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.state", count);
        ret = dict_set_str (friends, key,
                    glusterd_friend_sm_state_name_get(peerinfo->state.state));
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.connected", count);
        ret = dict_set_int32 (friends, key, (int32_t)peerinfo->connected);
        if (ret)
                goto out;

out:
        return ret;
}


int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count)
{

        int                     ret = -1;
        char                    key[256] = {0, };
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *buf = NULL;
        int                     i = 1;
        data_pair_t             *pairs = NULL;
        char                    reconfig_key[256] = {0, };
        dict_t                  *dict = NULL;
        data_t                  *value = NULL;
        int                     opt_count = 0;


        GF_ASSERT (volinfo);
        GF_ASSERT (volumes);

        snprintf (key, 256, "volume%d.name", count);
        ret = dict_set_str (volumes, key, volinfo->volname);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.type", count);
        ret = dict_set_int32 (volumes, key, volinfo->type);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.status", count);
        ret = dict_set_int32 (volumes, key, volinfo->status);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.brick_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->brick_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.sub_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->sub_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.transport", count);
        ret = dict_set_int32 (volumes, key, volinfo->transport_type);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                char    brick[1024] = {0,};
                snprintf (key, 256, "volume%d.brick%d", count, i);
                snprintf (brick, 1024, "%s:%s", brickinfo->hostname,
                          brickinfo->path);
                buf = gf_strdup (brick);
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;
                i++;
        }

        dict = volinfo->dict;
        if (!dict) {
                ret = 0;
                goto out;
        }

        pairs = dict->members_list;

        while (pairs) {
                if (1 == glusterd_check_option_exists (pairs->key, NULL)) {
                        value = pairs->value;
                        if (!value) 
                                continue;

                        snprintf (reconfig_key, 256, "volume%d.option.%s", count, 
                                 pairs->key);
                        ret = dict_set_str  (volumes, reconfig_key, value->data);
                        if (!ret)
                            opt_count++;
                }
                pairs = pairs->next;
        }

        snprintf (key, 256, "volume%d.opt_count", count);
        ret = dict_set_int32 (volumes, key, opt_count);
        if (ret)
            goto out;

out:
        return ret;
}

int
glusterd_friend_find (uuid_t uuid, char *hostname,
                      glusterd_peerinfo_t **peerinfo)
{
        int     ret = -1;

        if (uuid) {
                ret = glusterd_friend_find_by_uuid (uuid, peerinfo);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_NORMAL,
                                 "Unable to find peer by uuid");
                } else {
                        goto out;
                }

        }

        if (hostname) {
                ret = glusterd_friend_find_by_hostname (hostname, peerinfo);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_NORMAL,
                                "Unable to find hostname: %s", hostname);
                } else {
                        goto out;
                }
        }

out:
        return ret;
}

int
glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_lock_req       lock_req = {{0},};
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_cluster_lock_req (req->msg[0], &lock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received LOCK from uuid: %s", uuid_utoa (lock_req.uuid));


        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK, ctx);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gd1_mgmt_stage_op_req           stage_req = {{0,}};
        glusterd_op_stage_ctx_t         *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_stage_op_req (req->msg[0], &stage_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received stage op from uuid: %s", uuid_utoa (stage_req.uuid));

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_stage_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        //CHANGE THIS
        uuid_copy (ctx->stage_req.uuid, stage_req.uuid);
        ctx->stage_req.op = stage_req.op;
        ctx->stage_req.buf.buf_len = stage_req.buf.buf_len;
        ctx->stage_req.buf.buf_val = GF_CALLOC (1, stage_req.buf.buf_len,
                                                gf_gld_mt_string);
        if (!ctx->stage_req.buf.buf_val)
                goto out;

        memcpy (ctx->stage_req.buf.buf_val, stage_req.buf.buf_val,
                stage_req.buf.buf_len);

        ctx->req   = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_OP, ctx);

out:
        if (stage_req.buf.buf_val)
                free (stage_req.buf.buf_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gd1_mgmt_commit_op_req          commit_req = {{0},};
        glusterd_op_commit_ctx_t        *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_commit_op_req (req->msg[0], &commit_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received commit op from uuid: %s", uuid_utoa (commit_req.uuid));

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_commit_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        ctx->req = req;
        //CHANGE THIS
        uuid_copy (ctx->stage_req.uuid, commit_req.uuid);
        ctx->stage_req.op = commit_req.op;
        ctx->stage_req.buf.buf_len = commit_req.buf.buf_len;
        ctx->stage_req.buf.buf_val = GF_CALLOC (1, commit_req.buf.buf_len,
                                                gf_gld_mt_string);
        if (!ctx->stage_req.buf.buf_val)
                goto out;

        memcpy (ctx->stage_req.buf.buf_val, commit_req.buf.buf_val,
                commit_req.buf.buf_len);

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_OP, ctx);

out:
        if (commit_req.buf.buf_val)
                free (commit_req.buf.buf_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};
        glusterd_peerinfo_t             *peerinfo = NULL;
        gf_boolean_t                    run_fsm = _gf_true;
        GF_ASSERT (req);

        if (!gf_xdr_to_cli_probe_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                gf_log ("", GF_LOG_ERROR, "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("peer probe", " on host %s:%d", cli_req.hostname,
                    cli_req.port);
        gf_log ("glusterd", GF_LOG_NORMAL, "Received CLI probe req %s %d",
                cli_req.hostname, cli_req.port);

        if (!(ret = glusterd_is_local_addr(cli_req.hostname))) {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_LOCALHOST,
                                              cli_req.hostname, cli_req.port);
                goto out;
        }

        if (!(ret = glusterd_friend_find_by_hostname(cli_req.hostname,
                                         &peerinfo))) {
                if (strcmp (peerinfo->hostname, cli_req.hostname) == 0) {

                        gf_log ("glusterd", GF_LOG_DEBUG, "Probe host %s port %d"
                               " already a peer", cli_req.hostname, cli_req.port);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND,
                                                      cli_req.hostname, cli_req.port);
                        goto out;
                }
        }
        ret = glusterd_probe_begin (req, cli_req.hostname, cli_req.port);

        gf_cmd_log ("peer probe","on host %s:%d %s",cli_req.hostname, cli_req.port,
                    (ret) ? "FAILED" : "SUCCESS");

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;
        }
out:
        if (cli_req.hostname)
                free (cli_req.hostname);//its malloced by xdr

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};
        uuid_t                          uuid = {0};
        int                             op_errno = 0;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        if (!gf_xdr_to_cli_probe_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received CLI deprobe req");

        ret = glusterd_hostname_to_uuid (cli_req.hostname, uuid);
        if (ret) {
                op_errno = GF_DEPROBE_NOT_FRIEND;
                goto out;
        }

        if (!uuid_compare (uuid, priv->uuid)) {
                op_errno = GF_DEPROBE_LOCALHOST;
                ret = -1;
                goto out;
        }

        if (!uuid_is_null (uuid)) {
                ret = glusterd_all_volume_cond_check (
                                                glusterd_friend_brick_belongs,
                                                -1, &uuid);
                if (ret) {
                        op_errno = GF_DEPROBE_BRICK_EXIST;
                        goto out;
                }
        }

        if (!uuid_is_null (uuid)) {
                ret = glusterd_deprobe_begin (req, cli_req.hostname,
                                              cli_req.port, uuid);
        } else {
                ret = glusterd_deprobe_begin (req, cli_req.hostname,
                                              cli_req.port, NULL);
        }

        gf_cmd_log ("peer deprobe", "on host %s:%d %s", cli_req.hostname,
                    cli_req.port, (ret) ? "FAILED" : "SUCCESS");
out:
        if (ret) {
                ret = glusterd_xfer_cli_deprobe_resp (req, ret, op_errno,
                                                      cli_req.hostname);
        }

        if (cli_req.hostname)
                free (cli_req.hostname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_peer_list_req           cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_peer_list_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received cli list req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_list_friends (req, dict, cli_req.flags);

out:
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_get_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_get_vol_req             cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_get_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received get vol req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_get_volumes (req, dict, cli_req.flags);

out:
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int32_t
glusterd_op_txn_begin ()
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        int32_t                 locked = 0;

        priv = THIS->private;
        GF_ASSERT (priv);

        ret = glusterd_lock (priv->uuid);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Unable to acquire local lock, ret: %d", ret);
                goto out;
        }

        locked = 1;
        gf_log ("glusterd", GF_LOG_NORMAL, "Acquired local lock");

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_START_LOCK, NULL);

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);

out:
        if (locked && ret)
                glusterd_unlock (priv->uuid);
        return ret;
}

int32_t
glusterd_op_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                   gf_boolean_t is_ctx_free)
{
        int             ret = -1;
        GF_ASSERT (req);
        GF_ASSERT ((op > GD_OP_NONE) && (op < GD_OP_MAX));
        GF_ASSERT ((NULL != ctx) || (_gf_false == is_ctx_free));

        glusterd_op_set_op (op);
        glusterd_op_set_ctx (op, ctx);
        glusterd_op_set_ctx_free (op, is_ctx_free);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int
glusterd_handle_create_volume (rpcsvc_request_t *req)
{
        int32_t                 ret         = -1;
        gf1_cli_create_vol_req  cli_req     = {0,};
        dict_t                 *dict        = NULL;
        glusterd_brickinfo_t   *brickinfo   = NULL;
        char                   *brick       = NULL;
        char                   *bricks      = NULL;
        char                   *volname     = NULL;
        int                    brick_count = 0;
        char                   *tmpptr      = NULL;
        int                    i           = 0;
        char                   *brick_list  = NULL;
        void                   *cli_rsp     = NULL;
        char                    err_str[2048] = {0,};
        gf1_cli_create_vol_rsp  rsp         = {0,};
        glusterd_conf_t        *priv        = NULL;
        xlator_t               *this        = NULL;
        char                   *free_ptr    = NULL;
        char                   *trans_type  = NULL;
        uuid_t                  volume_id   = {0,};
        glusterd_brickinfo_t    *tmpbrkinfo = NULL;
        glusterd_volinfo_t      tmpvolinfo = {{0},};
        int                     lock_fail = 0;

        GF_ASSERT (req);

        INIT_LIST_HEAD (&tmpvolinfo.bricks);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                snprintf (err_str, sizeof (err_str), "Another operation is in "
                          "progress, please retry after some time");
                goto out;
        }

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        ret = -1;
        if (!gf_xdr_to_cli_create_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Garbage args received");
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received create volume req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the buffer");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                goto out;
        }
        gf_cmd_log ("Volume create", "on volname: %s attempted", volname);

        if ((ret = glusterd_check_volume_exists (volname))) {
                snprintf(err_str, 2048, "Volume %s already exists", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "brick count");
                goto out;
        }

        ret = dict_get_str (dict, "transport", &trans_type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get transport-type");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "transport-type");
                goto out;
        }
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get bricks");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "bricks");
                goto out;
        }

        uuid_generate (volume_id);
        free_ptr = gf_strdup (uuid_utoa (volume_id));
        ret = dict_set_dynstr (dict, "volume-id", free_ptr);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "unable to set volume-id");
                snprintf (err_str, sizeof (err_str), "Unable to set volume "
                          "id");
                goto out;
        }
        free_ptr = NULL;

        if (bricks) {
                brick_list = gf_strdup (bricks);
                free_ptr = brick_list;
        }

        gf_cmd_log ("Volume create", "on volname: %s type:%s count:%d bricks:%s",
                    cli_req.volname, ((cli_req.type == 0)? "DEFAULT":
                    ((cli_req.type == 1)? "STRIPE":"REPLICATE")), cli_req.count,
                    bricks);


        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get "
                                  "brick info from brick %s", brick);
                        goto out;
                }

                ret = glusterd_new_brick_validate (brick, brickinfo, err_str,
                                                   sizeof (err_str));
                if (ret)
                        goto out;
                ret = glusterd_volume_brickinfo_get (brickinfo->uuid,
                                                     brickinfo->hostname,
                                                     brickinfo->path,
                                                     &tmpvolinfo, &tmpbrkinfo);
                if (!ret) {
                        ret = -1;
                        snprintf (err_str, sizeof (err_str), "Brick: %s:%s, %s"
                                  " in the arguments mean the same",
                                  tmpbrkinfo->hostname, tmpbrkinfo->path,
                                  brick);
                        goto out;
                }
                list_add_tail (&brickinfo->brick_list, &tmpvolinfo.bricks);
                brickinfo = NULL;
        }

        ret = glusterd_op_begin (req, GD_OP_CREATE_VOLUME, dict, _gf_true);
        gf_cmd_log ("Volume create", "on volname: %s %s", volname,
                    (ret != 0) ? "FAILED": "SUCCESS");

out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      gf_xdr_serialize_cli_create_vol_rsp);
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();

                ret = 0; //Client response sent, prevent second response
        }

        if (free_ptr)
                GF_FREE(free_ptr);

        glusterd_volume_bricks_delete (&tmpvolinfo);
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (cli_req.volname)
                free (cli_req.volname); // its a malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_start_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_start_vol_req           cli_req = {0,};
        int                             lock_fail = 0;
        char                            *dup_volname = NULL;
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d", ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_start_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received start vol req"
                "for volume %s", cli_req.volname);

        dict = dict_new ();

        if (!dict)
                goto out;

        dup_volname = gf_strdup (cli_req.volname);
        if (!dup_volname)
                goto out;

        ret = dict_set_dynstr (dict, "volname", dup_volname);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "flags", cli_req.flags);
        if (ret)
                goto out;
        ret = glusterd_op_begin (req, GD_OP_START_VOLUME, dict, _gf_true);

        gf_cmd_log ("volume start","on volname: %s %s", cli_req.volname,
                    ((ret == 0) ? "SUCCESS": "FAILED"));

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();

        }

        return ret;
}


int
glusterd_handle_cli_stop_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_stop_vol_req           cli_req = {0,};
        int                             lock_fail = 0;
        char                            *dup_volname = NULL;
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_stop_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received stop vol req"
                "for volume %s", cli_req.volname);

        dict = dict_new ();

        if (!dict)
                goto out;

        dup_volname = gf_strdup (cli_req.volname);
        if (!dup_volname)
                goto out;

        ret = dict_set_dynstr (dict, "volname", dup_volname);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "flags", cli_req.flags);
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, GD_OP_STOP_VOLUME, dict, _gf_true);
        gf_cmd_log ("Volume stop","on volname: %s %s", cli_req.volname,
                    ((ret)?"FAILED":"SUCCESS"));

out:
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();
        }

        return ret;
}

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req)
{
        int                               lock_fail = 0;
        int32_t                           ret = -1;
        gf1_cli_delete_vol_req            cli_req = {0,};
        glusterd_op_delete_volume_ctx_t   *ctx = NULL;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_delete_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        gf_cmd_log ("Volume delete","on volname: %s attempted", cli_req.volname);

        gf_log ("glusterd", GF_LOG_NORMAL, "Received delete vol req"
                "for volume %s", cli_req.volname);


        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_delete_volume_ctx_t);
        if (!ctx)
                goto out;

        strncpy (ctx->volume_name, cli_req.volname, GD_VOLUME_NAME_MAX);

        ret = glusterd_op_begin (req, GD_OP_DELETE_VOLUME, ctx, _gf_true);
        gf_cmd_log ("Volume delete", "on volname: %s %s", cli_req.volname,
                   ((ret) ? "FAILED" : "SUCCESS"));

out:
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();
        }

        return ret;
}

int
glusterd_handle_add_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_add_brick_req          cli_req = {0,};
        dict_t                          *dict = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        char                            *brick = NULL;
        char                            *bricks = NULL;
        char                            *volname = NULL;
        int                             brick_count = 0;
        char                            *tmpptr = NULL;
        int                             i = 0;
        char                            *brick_list = NULL;
        void                            *cli_rsp = NULL;
        char                            err_str[2048] = {0,};
        gf1_cli_add_brick_rsp           rsp = {0,};
        glusterd_volinfo_t              *volinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        xlator_t                        *this = NULL;
        char                            *free_ptr = NULL;
        glusterd_brickinfo_t            *tmpbrkinfo = NULL;
        glusterd_volinfo_t              tmpvolinfo = {{0},};
        int                             lock_fail = 0;

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;

        GF_ASSERT (req);

        INIT_LIST_HEAD (&tmpvolinfo.bricks);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                snprintf (err_str, sizeof (err_str), "Another operation is in "
                          "progress, please retry after some time");
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_add_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (err_str, sizeof (err_str), "Garbage args received");
                goto out;
        }

        gf_cmd_log ("Volume add-brick", "on volname: %s attempted",
                    cli_req.volname);
        gf_log ("glusterd", GF_LOG_NORMAL, "Received add brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the buffer");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                goto out;
        }

        if (!(ret = glusterd_check_volume_exists (volname))) {
                ret = -1;
                snprintf(err_str, 2048, "Volume %s does not exist", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "brick count");
                goto out;
        }

        if (!(ret = glusterd_volinfo_find (volname, &volinfo))) {
                if (volinfo->type == GF_CLUSTER_TYPE_NONE)
                        goto brick_val;
                if (!brick_count || !volinfo->sub_count)
                        goto brick_val;

		/* If the brick count is less than sub_count then, allow add-brick only for
		   plain replicate volume since in plain stripe brick_count becoming less than
		   the sub_count is not allowed */
                if (volinfo->brick_count < volinfo->sub_count && (volinfo->type == GF_CLUSTER_TYPE_REPLICATE) ) {
                        if ((volinfo->sub_count - volinfo->brick_count) == brick_count)
                                goto brick_val;
                }

                if ((brick_count % volinfo->sub_count) != 0) {
                        snprintf(err_str, 2048, "Incorrect number of bricks"
                                " supplied %d for type %s with count %d",
                                brick_count, (volinfo->type == 1)? "STRIPE":
                                "REPLICATE", volinfo->sub_count);
                        gf_log("glusterd", GF_LOG_ERROR, "%s", err_str);
                        ret = -1;
                        goto out;
                }
        } else {
                snprintf (err_str, sizeof (err_str), "Unable to get volinfo "
                          "for volume name %s", volname);
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

brick_val:
        ret = dict_get_str (dict, "bricks", &bricks);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "bricks");
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (bricks)
                brick_list = gf_strdup (bricks);
        if (!brick_list) {
                ret = -1;
                snprintf (err_str, sizeof (err_str), "Out of memory");
                goto out;
        } else {
                free_ptr = brick_list;
        }

        gf_cmd_log ("Volume add-brick", "volname: %s type %s count:%d bricks:%s"
                    ,volname, ((volinfo->type == 0)? "DEFAULT" : ((volinfo->type
                    == 1)? "STRIPE": "REPLICATE")), brick_count, brick_list);


        while ( i < brick_count) {
                i++;
                brick= strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                brickinfo = NULL;
                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get "
                                  "brick info from brick %s", brick);
                        goto out;
                }
                ret = glusterd_new_brick_validate (brick, brickinfo, err_str,
                                                   sizeof (err_str));
                if (ret)
                        goto out;
                ret = glusterd_volume_brickinfo_get (brickinfo->uuid,
                                                     brickinfo->hostname,
                                                     brickinfo->path,
                                                     &tmpvolinfo, &tmpbrkinfo);
                if (!ret) {
                        ret = -1;
                        snprintf (err_str, sizeof (err_str), "Brick: %s:%s, %s"
                                  " in the arguments mean the same",
                                  tmpbrkinfo->hostname, tmpbrkinfo->path,
                                  brick);
                        goto out;
                }
                list_add_tail (&brickinfo->brick_list, &tmpvolinfo.bricks);
                brickinfo = NULL;
        }

        ret = glusterd_op_begin (req, GD_OP_ADD_BRICK, dict, _gf_true);
        gf_cmd_log ("Volume add-brick","on volname: %s %s", volname,
                   (ret != 0)? "FAILED" : "SUCCESS");

out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      gf_xdr_serialize_cli_add_brick_rsp);
                if (!lock_fail)
                        glusterd_opinfo_unlock();
                ret = 0; //sent error to cli, prevent second reply
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (free_ptr)
                GF_FREE (free_ptr);
        glusterd_volume_bricks_delete (&tmpvolinfo);
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        return ret;
}

int
glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_replace_brick_req          cli_req = {0,};
        dict_t                          *dict = NULL;
        char                            *src_brick = NULL;
        char                            *dst_brick = NULL;
        int32_t                         op = 0;
        char                            operation[256];
        int                             lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_replace_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received replace brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get src brick");
                goto out;
        }
        gf_log ("", GF_LOG_DEBUG,
                "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get dest brick");
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG,
                "dst brick=%s", dst_brick);

        switch (op) {
                case GF_REPLACE_OP_START: strcpy (operation, "start");
                        break;
                case GF_REPLACE_OP_COMMIT: strcpy (operation, "commit");
                        break;
                case GF_REPLACE_OP_PAUSE:  strcpy (operation, "pause");
                        break;
                case GF_REPLACE_OP_ABORT:  strcpy (operation, "abort");
                        break;
                case GF_REPLACE_OP_STATUS: strcpy (operation, "status");
                        break;
                case GF_REPLACE_OP_COMMIT_FORCE: strcpy (operation, "commit-force");
                        break;
                default:strcpy (operation, "unknown");
                        break;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Recieved replace brick %s request", operation);
        gf_cmd_log ("Volume replace-brick","volname: %s src_brick:%s"
                    " dst_brick:%s op:%s",cli_req.volname, src_brick, dst_brick
                    ,operation);

        ret = glusterd_op_begin (req, GD_OP_REPLACE_BRICK, dict, _gf_true);
        gf_cmd_log ("Volume replace-brick","on volname: %s %s", cli_req.volname,
                   (ret) ? "FAILED" : "SUCCESS");

out:
        if (ret && dict)
                dict_unref (dict);
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();

        }

        return ret;
}




int
glusterd_handle_reset_volume (rpcsvc_request_t *req)
{
        int32_t                           ret = -1;
        gf1_cli_reset_vol_req           cli_req = {0,};
        dict_t                          *dict = NULL;
        int                             lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_set_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR, "failed to "
                                    "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_op_begin (req, GD_OP_RESET_VOLUME, dict, _gf_true);

out:
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();
        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();
        }

        return ret;
}

int
glusterd_handle_gsync_set (rpcsvc_request_t *req)
{
        int32_t                 ret     = 0;
        dict_t                  *dict   = NULL;
        gf1_cli_gsync_set_req   cli_req = {{0},};
        int                     lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_gsync_set_req (req->msg[0], &cli_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();
                if (!dict)
                        goto out;

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR, "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_op_begin (req, GD_OP_GSYNC_SET, dict, _gf_true);

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();
        }
        return ret;
}

int
glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_set_vol_req             cli_req = {0,};
        dict_t                          *dict = NULL;
        int                             lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_set_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_op_begin (req, GD_OP_SET_VOLUME, dict, _gf_true);

out:
        if (cli_req.volname)
                free (cli_req.volname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();
        }
        return ret;
}

int
glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_remove_brick_req        cli_req = {0,};
        dict_t                          *dict = NULL;
        int32_t                         count = 0;
        char                            *brick = NULL;
        char                            key[256] = {0,};
        char                            *brick_list = NULL;
        int                             i = 1;
        glusterd_volinfo_t              *volinfo = NULL;
        glusterd_brickinfo_t            *brickinfo = NULL;
        int32_t                         pos = 0;
        int32_t                         sub_volume = 0;
        int32_t                         sub_volume_start = 0;
        int32_t                         sub_volume_end = 0;
        glusterd_brickinfo_t            *tmp = NULL;
        char                            err_str[2048] = {0};
        gf1_cli_remove_brick_rsp        rsp = {0,};
        void                            *cli_rsp = NULL;
        char                            vol_type[256] = {0,};
        int                             lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_remove_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("Volume remove-brick","on volname: %s attempted",cli_req.volname);
        gf_log ("glusterd", GF_LOG_NORMAL, "Received rem brick req");

        if (cli_req.bricks.bricks_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.bricks.bricks_val,
                                        cli_req.bricks.bricks_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.bricks.bricks_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
        if (ret) {
                 snprintf (err_str, 2048, "Volume %s does not exist",
                          cli_req.volname);
                 gf_log ("", GF_LOG_ERROR, "%s", err_str);
                 goto out;
        }

        if (volinfo->type == GF_CLUSTER_TYPE_REPLICATE)
                strcpy (vol_type, "replica");
        else if (volinfo->type == GF_CLUSTER_TYPE_STRIPE)
                strcpy (vol_type, "stripe");
        else
                strcpy (vol_type, "distribute");

	/* Do not allow remove-brick if the volume is plain stripe */
	if ((volinfo->type == GF_CLUSTER_TYPE_STRIPE) && (volinfo->brick_count == volinfo->sub_count)) {
                snprintf (err_str, 2048, "Removing brick from a plain stripe is not allowed");
                gf_log ("glusterd", GF_LOG_ERROR, "%s", err_str);
                ret = -1;
                goto out;
	}

	/* Do not allow remove-brick if the bricks given is less than the replica count
	   or stripe count */
        if (((volinfo->type == GF_CLUSTER_TYPE_REPLICATE) || (volinfo->type == GF_CLUSTER_TYPE_STRIPE))
	    && !(volinfo->brick_count <= volinfo->sub_count)) {
                if (volinfo->sub_count && (count % volinfo->sub_count != 0)) {
                        snprintf (err_str, 2048, "Remove brick incorrect"
                                  " brick count of %d for %s %d",
                                  count, vol_type, volinfo->sub_count);
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        ret = -1;
                        goto out;
                }
        }

        brick_list = GF_MALLOC (120000 * sizeof(*brick_list),gf_common_mt_char);

        if (!brick_list) {
                ret = -1;
                goto out;
        }

        strcpy (brick_list, " ");
        while ( i <= count) {
                snprintf (key, 256, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get %s", key);
                        goto out;
                }
                gf_log ("", GF_LOG_DEBUG, "Remove brick count %d brick: %s",
                        i, brick);

                ret = glusterd_volume_brickinfo_get_by_brick(brick, volinfo, &brickinfo);
                if (ret) {
                        snprintf(err_str, 2048,"Incorrect brick %s for volume"
                                " %s", brick, cli_req.volname);
                        gf_log ("", GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }
                strcat(brick_list, brick);
                strcat(brick_list, " ");

                i++;
                if ((volinfo->type == GF_CLUSTER_TYPE_NONE) ||
                    (volinfo->brick_count <= volinfo->sub_count))
                        continue;

                pos = 0;
                list_for_each_entry (tmp, &volinfo->bricks, brick_list) {

                        if ((!strcmp (tmp->hostname,brickinfo->hostname)) &&
                            !strcmp (tmp->path, brickinfo->path)) {
                                gf_log ("", GF_LOG_NORMAL, "Found brick");
                                if (!sub_volume && volinfo->sub_count) {
                                        sub_volume = (pos / volinfo->
                                                      sub_count) + 1;
                                        sub_volume_start = volinfo->sub_count *
                                                           (sub_volume - 1);
                                        sub_volume_end = (volinfo->sub_count *
                                                          sub_volume) -1 ;
                                } else {
                                        if (pos < sub_volume_start ||
                                            pos >sub_volume_end) {
                                                ret = -1;
                                                snprintf(err_str, 2048,"Bricks"
                                                         " not from same subvol"
                                                         " for %s", vol_type);
                                                gf_log ("",GF_LOG_ERROR,
                                                        "%s", err_str);
                                                goto out;
                                        }
                                }
                                break;
                        }
                        pos++;
                }
        }
        gf_cmd_log ("Volume remove-brick","volname: %s count:%d bricks:%s",
                    cli_req.volname, count, brick_list);

        ret = glusterd_op_begin (req, GD_OP_REMOVE_BRICK, dict, _gf_true);
        gf_cmd_log ("Volume remove-brick","on volname: %s %s",cli_req.volname,
                    (ret) ? "FAILED" : "SUCCESS");

out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                rsp.op_ret = -1;
                rsp.op_errno = 0;
                rsp.volname = "";
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str), "Operation failed");
                gf_log ("", GF_LOG_ERROR, "%s", err_str);
                rsp.op_errstr = err_str;
                cli_rsp = &rsp;
                glusterd_submit_reply(req, cli_rsp, NULL, 0, NULL,
                                      gf_xdr_serialize_cli_remove_brick_rsp);
                if (!lock_fail)
                        glusterd_opinfo_unlock();

                ret = 0; //sent error to cli, prevent second reply

        }
        if (brick_list)
                GF_FREE (brick_list);
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_log_filename (rpcsvc_request_t *req)
{
        int32_t                   ret     = -1;
        gf1_cli_log_filename_req  cli_req = {0,};
        dict_t                   *dict    = NULL;
        int                       lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_log_filename_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received log filename req "
                "for volume %s", cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;
        ret = dict_set_dynmstr (dict, "brick", cli_req.brick);
        if (ret)
                goto out;
        ret = dict_set_dynmstr (dict, "path", cli_req.path);
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, GD_OP_LOG_FILENAME, dict, _gf_true);

out:
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();

        }

        return ret;
}

int
glusterd_handle_log_locate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_log_locate_req  cli_req = {0,};
        gf1_cli_log_locate_rsp  rsp     = {0,};
        glusterd_conf_t        *priv = NULL;
        glusterd_volinfo_t     *volinfo = NULL;
        glusterd_brickinfo_t   *brickinfo = NULL;
        char                    tmp_str[PATH_MAX] = {0,};
        char                   *tmp_brick = NULL;
        uint32_t                found = 0;
        glusterd_brickinfo_t   *tmpbrkinfo = NULL;
        int                     lock_fail = 0;

        GF_ASSERT (req);

        priv    = THIS->private;

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_log_locate_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received log locate req "
                "for volume %s", cli_req.volname);

        if (strchr (cli_req.brick, ':')) {
                /* TODO: need to get info of only that brick and then
                   tell what is the exact location */
                tmp_brick = gf_strdup (cli_req.brick);
                if (!tmp_brick)
                        goto out;

                gf_log ("", GF_LOG_DEBUG, "brick : %s", cli_req.brick);
                ret = glusterd_brickinfo_from_brick (tmp_brick, &tmpbrkinfo);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Cannot get brickinfo from the brick");
                        goto out;
                }
        }

        ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
        if (ret) {
                rsp.path = "request sent on non-existent volume";
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (tmpbrkinfo) {
                        ret = glusterd_resolve_brick (tmpbrkinfo);
                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "cannot resolve the brick");
                                goto out;
                        }
                        if (uuid_compare (tmpbrkinfo->uuid, brickinfo->uuid) || strcmp (brickinfo->path, tmpbrkinfo->path))
                                continue;
                }

                if (brickinfo->logfile) {
                        strcpy (tmp_str, brickinfo->logfile);
                        rsp.path = dirname (tmp_str);
                        found = 1;
                } else {
                        snprintf (tmp_str, PATH_MAX, "%s/bricks/",
                                  DEFAULT_LOG_FILE_DIRECTORY);
                        rsp.path = tmp_str;
                        found = 1;
                }
                break;
        }

        if (!found) {
                snprintf (tmp_str, PATH_MAX, "brick %s:%s does not exitst in the volume %s",
                          tmpbrkinfo->hostname, tmpbrkinfo->path, cli_req.volname);
                rsp.path = tmp_str;
        }

        ret = 0;
out:
        if (tmp_brick)
                GF_FREE (tmp_brick);
        if (tmpbrkinfo)
                glusterd_brickinfo_delete (tmpbrkinfo);
        rsp.op_ret = ret;
        if (!rsp.path)
                rsp.path = "Operation failed";

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_log_locate_rsp);

        if (cli_req.brick)
                free (cli_req.brick); //its malloced by xdr
        if (cli_req.volname)
                free (cli_req.volname); //its malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (!lock_fail)
                (void) glusterd_opinfo_unlock ();

        return ret;
}

int
glusterd_handle_log_rotate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf1_cli_log_rotate_req  cli_req = {0,};
        dict_t                 *dict    = NULL;
        int                     lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_log_rotate_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received log rotate req "
                "for volume %s", cli_req.volname);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
        if (ret)
                goto out;

        ret = dict_set_dynmstr (dict, "brick", cli_req.brick);
        if (ret)
                goto out;

        ret = dict_set_uint64 (dict, "rotate-key", (uint64_t)time (NULL));
        if (ret)
                goto out;

        ret = glusterd_op_begin (req, GD_OP_LOG_ROTATE, dict, _gf_true);

out:
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                ret = glusterd_op_send_cli_response (req->procnum, ret, 0, req,
                                                     NULL, "operation failed");
                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();
        }

        return ret;
}

int
glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        int32_t                          ret     = -1;
        gf1_cli_sync_volume_req          cli_req = {0,};
        dict_t                           *dict = NULL;
        gf1_cli_sync_volume_rsp          cli_rsp = {0.};
        char                             msg[2048] = {0,};
        gf_boolean_t                     free_hostname = _gf_true;
        gf_boolean_t                     free_volname = _gf_true;
        glusterd_volinfo_t               *volinfo = NULL;
        int                              lock_fail = 0;

        GF_ASSERT (req);

        ret = glusterd_op_set_cli_op (req->procnum);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set cli op: %d",
                        ret);
                lock_fail = 1;
                goto out;
        }

        ret = -1;
        if (!gf_xdr_to_cli_sync_volume_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        gf_log ("glusterd", GF_LOG_NORMAL, "Received volume sync req "
                "for volume %s",
                (cli_req.flags & GF_CLI_SYNC_ALL) ? "all" : cli_req.volname);

        dict = dict_new ();
        if (!dict) {
                gf_log ("", GF_LOG_ERROR, "Can't allocate sync vol dict");
                goto out;
        }

        if (!glusterd_is_local_addr (cli_req.hostname)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "sync from localhost"
                          " not allowed");
                goto out;
        }

        ret = dict_set_dynmstr (dict, "hostname", cli_req.hostname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "hostname set failed");
                snprintf (msg, sizeof (msg), "hostname set failed");
                goto out;
        } else {
                free_hostname = _gf_false;
        }

        ret = dict_set_int32 (dict, "flags", cli_req.flags);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "volume flags set failed");
                snprintf (msg, sizeof (msg), "volume flags set failed");
                goto out;
        }

        if (!cli_req.flags) {
                ret = glusterd_volinfo_find (cli_req.volname, &volinfo);
                if (!ret) {
                        snprintf (msg, sizeof (msg), "please delete the "
                                 "volume: %s before sync", cli_req.volname);
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynmstr (dict, "volname", cli_req.volname);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "volume name set failed");
                        snprintf (msg, sizeof (msg), "volume name set failed");
                        goto out;
                } else {
                        free_volname = _gf_false;
                }
        } else {
                free_volname = _gf_false;
                if (glusterd_volume_count_get ()) {
                        snprintf (msg, sizeof (msg), "please delete all the "
                                 "volumes before full sync");
                        ret = -1;
                        goto out;
                }
        }

        ret = glusterd_op_begin (req, GD_OP_SYNC_VOLUME, dict, _gf_true);

out:
        if (ret) {
                cli_rsp.op_ret = -1;
                cli_rsp.op_errstr = msg;
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                glusterd_submit_reply(req, &cli_rsp, NULL, 0, NULL,
                                      gf_xdr_from_cli_sync_volume_rsp);
                if (free_hostname && cli_req.hostname)
                        free (cli_req.hostname);
                if (free_volname && cli_req.volname)
                        free (cli_req.volname);
                if (dict)
                        dict_unref (dict);

                if (!lock_fail)
                        (void) glusterd_opinfo_unlock ();

                ret = 0; //sent error to cli, prevent second reply
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_fsm_log_send_resp (rpcsvc_request_t *req, int op_ret,
                            char *op_errstr, dict_t *dict)
{

        int                             ret = -1;
        gf1_cli_fsm_log_rsp             rsp = {0};

        GF_ASSERT (req);
        GF_ASSERT (op_errstr);

        rsp.op_ret = op_ret;
        rsp.op_errstr = op_errstr;
        if (rsp.op_ret == 0)
                ret = dict_allocate_and_serialize (dict, &rsp.fsm_log.fsm_log_val,
                                                (size_t *)&rsp.fsm_log.fsm_log_len);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_from_cli_fsm_log_rsp);
        if (rsp.fsm_log.fsm_log_val)
                GF_FREE (rsp.fsm_log.fsm_log_val);

        gf_log ("glusterd", GF_LOG_DEBUG, "Responded, ret: %d", ret);

        return 0;
}

int
glusterd_handle_fsm_log (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_fsm_log_req             cli_req = {0,};
        dict_t                          *dict = NULL;
        glusterd_sm_tr_log_t            *log = NULL;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;
        char                            msg[2048] = {0};
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_fsm_log_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (msg, sizeof (msg), "Garbage request");
                goto out;
        }

        if (strcmp ("", cli_req.name) == 0) {
                this = THIS;
                conf = this->private;
                log = &conf->op_sm_log;
        } else {
                ret = glusterd_friend_find_by_hostname (cli_req.name,
                                                        &peerinfo);
                if (ret) {
                        snprintf (msg, sizeof (msg), "%s is not a peer",
                                  cli_req.name);
                        goto out;
                }
                log = &peerinfo->sm_log;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = glusterd_sm_tr_log_add_to_dict (dict, log);
out:
        (void)glusterd_fsm_log_send_resp (req, ret, msg, dict);
        if (cli_req.name)
                free (cli_req.name);//malloced by xdr
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return 0;//send 0 to avoid double reply
}

int
glusterd_op_lock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_lock_rsp       rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        glusterd_get_uuid (&rsp.uuid);
        rsp.op_ret = status;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_cluster_lock_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded, ret: %d", ret);

        return 0;
}

int
glusterd_op_unlock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_unlock_rsp     rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_cluster_unlock_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to unlock, ret: %d", ret);

        return ret;
}

int
glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_unlock_req     unlock_req = {{0}, };
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_cluster_unlock_req (req->msg[0], &unlock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received UNLOCK from uuid: %s", uuid_utoa (unlock_req.uuid));

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }
        uuid_copy (ctx->uuid, unlock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_UNLOCK, ctx);

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_op_stage_send_resp (rpcsvc_request_t   *req,
                             int32_t op, int32_t status,
                             char *op_errstr, dict_t *rsp_dict)
{

        gd1_mgmt_stage_op_rsp           rsp      = {{0},};
        int                             ret      = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict,
                                           &rsp.dict.dict_val,
                                           (size_t *)&rsp.dict.dict_len);
        if (ret < 0) {
                gf_log ("", GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                return ret;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_stage_op_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to stage, ret: %d", ret);
        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return ret;
}

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                               int32_t op, int32_t status, char *op_errstr,
                               dict_t *rsp_dict)
{
        gd1_mgmt_commit_op_rsp          rsp      = {{0}, };
        int                             ret      = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;

        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict,
                                           &rsp.dict.dict_val,
                                           (size_t *)&rsp.dict.dict_len);
        if (ret < 0) {
                gf_log ("", GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }


        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_commit_op_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to commit, ret: %d", ret);

out:
        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        gf_boolean_t            run_fsm = _gf_true;

        GF_ASSERT (req);
        if (!gd_xdr_to_mgmt_friend_req (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe from uuid: %s", uuid_utoa (friend_req.uuid));
        ret = glusterd_handle_friend_req (req, friend_req.uuid,
                                          friend_req.hostname, friend_req.port,
                                          &friend_req);

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;
        }

out:
        if (friend_req.hostname)
                free (friend_req.hostname);//malloced by xdr

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);
        if (!gd_xdr_to_mgmt_friend_req (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received unfriend from uuid: %s", uuid_utoa (friend_req.uuid));

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get the remote hostname");
                goto out;
        }
        ret = glusterd_handle_unfriend_req (req, friend_req.uuid,
                                            remote_hostname, friend_req.port);

out:
        if (friend_req.hostname)
                free (friend_req.hostname);//malloced by xdr
        if (friend_req.vols.vols_val)
                free (friend_req.vols.vols_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}


int
glusterd_handle_friend_update_delete (dict_t *dict)
{
        char                    *hostname = NULL;
        int32_t                 ret = -1;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret)
                goto out;

        ret = glusterd_friend_remove (NULL, hostname);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_friend_hostname_update (glusterd_peerinfo_t *peerinfo,
                                char *hostname,
                                gf_boolean_t store_update)
{
        char                    *new_hostname = NULL;
        int                     ret = 0;

        GF_ASSERT (peerinfo);
        GF_ASSERT (hostname);

        new_hostname = gf_strdup (hostname);
        if (!new_hostname) {
                ret = -1;
                goto out;
        }

        GF_FREE (peerinfo->hostname);
        peerinfo->hostname = new_hostname;
        if (store_update)
                ret = glusterd_store_update_peerinfo (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_update     friend_req = {{0},};
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        glusterd_peerinfo_t     *tmp = NULL;
        gd1_mgmt_friend_update_rsp rsp = {{0},};
        dict_t                  *dict = NULL;
        char                    key[100] = {0,};
        char                    *uuid_buf = NULL;
        char                    *hostname = NULL;
        int                     i = 1;
        int                     count = 0;
        uuid_t                  uuid = {0,};
        glusterd_peerctx_args_t args = {0};
        int32_t                 op = 0;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!gd_xdr_to_mgmt_friend_update (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        ret = glusterd_friend_find (friend_req.uuid, NULL, &tmp);
        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Received friend update request "
                        "from unknown peer %s", uuid_utoa (friend_req.uuid));
                goto out;
        }
        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received friend update from uuid: %s", uuid_utoa (friend_req.uuid));

        if (friend_req.friends.friends_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (friend_req.friends.friends_val,
                                        friend_req.friends.friends_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = friend_req.friends.friends_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "op", &op);
        if (ret)
                goto out;

        if (GD_FRIEND_UPDATE_DEL == op) {
                ret = glusterd_handle_friend_update_delete (dict);
                goto out;
        }

        args.mode = GD_MODE_ON;
        while ( i <= count) {
                snprintf (key, sizeof (key), "friend%d.uuid", i);
                ret = dict_get_str (dict, key, &uuid_buf);
                if (ret)
                        goto out;
                uuid_parse (uuid_buf, uuid);
                snprintf (key, sizeof (key), "friend%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;

                gf_log ("", GF_LOG_NORMAL, "Received uuid: %s, hostname:%s",
                                uuid_buf, hostname);

                if (!uuid_compare (uuid, priv->uuid)) {
                        gf_log ("", GF_LOG_NORMAL, "Received my uuid as Friend");
                        i++;
                        continue;
                }

                ret = glusterd_friend_find (uuid, hostname, &tmp);

                if (!ret) {
                        if (strcmp (hostname, tmp->hostname) != 0) {
                                glusterd_friend_hostname_update (tmp, hostname,
                                                                 _gf_true);
                        }
                        i++;
                        continue;
                }

                ret = glusterd_friend_add (hostname, friend_req.port,
                                           GD_FRIEND_STATE_BEFRIENDED,
                                           &uuid, NULL, &peerinfo, 0, &args);

                i++;
        }

out:
        uuid_copy (rsp.uuid, priv->uuid);
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_update_rsp);
        if (dict) {
                if (!dict->extra_stdfree && friend_req.friends.friends_val)
                        free (friend_req.friends.friends_val);//malloced by xdr
                dict_unref (dict);
        } else {
                if (friend_req.friends.friends_val)
                        free (friend_req.friends.friends_val);//malloced by xdr
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;
        gd1_mgmt_probe_req              probe_req = {{0},};
        gd1_mgmt_probe_rsp              rsp = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        int                             port = 0;
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_probe_req (req->msg[0], &probe_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        this = THIS;

        conf = this->private;
        if (probe_req.port)
                port = probe_req.port;
        else
                port = GF_DEFAULT_BASE_PORT;

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe from uuid: %s", uuid_utoa (probe_req.uuid));

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get the remote hostname");
                goto out;
        }
        ret = glusterd_friend_find (probe_req.uuid, remote_hostname, &peerinfo);
        if ((ret != 0 ) && (!list_empty (&conf->peers))) {
                rsp.op_ret = -1;
                rsp.op_errno = GF_PROBE_ANOTHER_CLUSTER;
        } else if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Unable to find peerinfo"
                        " for host: %s (%d)", remote_hostname, port);
                args.mode = GD_MODE_SWITCH_ON;
                ret = glusterd_friend_add (remote_hostname, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, NULL, &peerinfo, 0, &args);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to add peer %s",
                                remote_hostname);
                        rsp.op_errno = GF_PROBE_ADD_FAILED;
                }
        }

        uuid_copy (rsp.uuid, conf->uuid);

        rsp.hostname = probe_req.hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_probe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL, "Responded to %s, op_ret: %d, "
                "op_errno: %d, ret: %d", probe_req.hostname,
                rsp.op_ret, rsp.op_errno, ret);

out:
        if (probe_req.hostname)
                free (probe_req.hostname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_friend_remove (uuid_t uuid, char *hostname)
{
        int                           ret = 0;
        glusterd_peerinfo_t           *peerinfo = NULL;

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);
        if (ret)
                goto out;

        ret = glusterd_friend_cleanup (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_friend_rpc_create (struct rpc_clnt **rpc,
                            const char *hoststr, int port,
                            glusterd_peerctx_t *peerctx)
{
        struct rpc_clnt         *new_rpc = NULL;
        dict_t                  *options = NULL;
        struct rpc_clnt_config  rpc_cfg = {0,};
        int                     ret = -1;
        char                    *hostname = NULL;
        int32_t                 intvl = 0;
        xlator_t                *this = NULL;

        GF_ASSERT (hoststr);
        this = THIS;
        GF_ASSERT (this);

        options = dict_new ();
        if (!options)
                goto out;

        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-interval",
                              &intvl);
        if (!ret) {
                ret = dict_set_int32 (options,
                        "transport.socket.keepalive-interval", intvl);
                if (ret)
                        goto out;
        }

        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-time",
                              &intvl);
        if (!ret) {
                ret = dict_set_int32 (options,
                        "transport.socket.keepalive-time", intvl);
                if (ret)
                        goto out;
        }

        hostname = gf_strdup((char*)hoststr);
        if (!hostname) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (options, "remote-host", hostname);
        if (ret)
                goto out;

        if (!port)
                port = GLUSTERD_DEFAULT_PORT;

        rpc_cfg.remote_host = (char *)hoststr;
        rpc_cfg.remote_port = port;

        ret = dict_set_int32 (options, "remote-port", port);
        if (ret)
                goto out;

        ret = dict_set_str (options, "transport.address-family", "inet");
        if (ret)
                goto out;

        new_rpc = rpc_clnt_new (&rpc_cfg, options, this->ctx, this->name);

        if (!new_rpc) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "new_rpc init failed for peer: %s!", hoststr);
                ret = -1;
                goto out;
        }

        ret = rpc_clnt_register_notify (new_rpc, glusterd_rpc_notify,
                                        peerctx);
        *rpc = new_rpc;

        rpc_clnt_start (new_rpc);

out:
        if (ret) {
                if (new_rpc) {
                        (void) rpc_clnt_unref (new_rpc);
                }
                if (options)
                        dict_unref (options);
                *rpc = NULL;
        }

        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid,
                     struct rpc_clnt    *rpc,
                     glusterd_peerinfo_t **friend,
                     gf_boolean_t restore,
                     glusterd_peerctx_args_t *args)
{
        int                     ret = 0;
        glusterd_conf_t         *conf = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_peerctx_t     *peerctx = NULL;
        gf_boolean_t            is_allocated = _gf_false;

        conf = THIS->private;
        GF_ASSERT (conf)

        peerctx = GF_CALLOC (1, sizeof (*peerctx), gf_gld_mt_peerctx_t);
        if (!peerctx) {
                ret = -1;
                goto out;
        }

        if (args)
                peerctx->args = *args;

        ret = glusterd_peerinfo_new (&peerinfo, state, uuid, hoststr);
        if (ret)
                goto out;
        peerctx->peerinfo = peerinfo;
        if (friend)
                *friend = peerinfo;

        if (hoststr) {
                if (!rpc) {
                        ret = glusterd_friend_rpc_create (&rpc, hoststr, port,
                                                          peerctx);
                        if (ret)
                                goto out;
                        is_allocated = _gf_true;
                }
                peerinfo->rpc = rpc;
        }

        if (!restore)
                ret = glusterd_store_update_peerinfo (peerinfo);

        list_add_tail (&peerinfo->uuid_list, &conf->peers);

out:
        if (ret) {
                if (peerctx)
                        GF_FREE (peerctx);
                if (is_allocated && rpc) {
                        (void) rpc_clnt_unref (rpc);
                }
                if (peerinfo) {
                        peerinfo->rpc = NULL;
                        (void) glusterd_friend_cleanup (peerinfo);
                }
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "connect returned %d", ret);
        return ret;
}

int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        glusterd_friend_sm_event_t      *event = NULL;

        GF_ASSERT (hoststr);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Unable to find peerinfo"
                        " for host: %s (%d)", hoststr, port);
                args.mode = GD_MODE_ON;
                args.req  = req;
                ret = glusterd_friend_add ((char *)hoststr, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, NULL, &peerinfo, 0, &args);
                if ((!ret) && (!peerinfo->connected)) {
                        ret = GLUSTERD_CONNECTION_AWAITED;
                }

        } else if (peerinfo->connected &&
                   (GD_FRIEND_STATE_BEFRIENDED == peerinfo->state.state)) {
                ret = glusterd_friend_hostname_update (peerinfo, (char*)hoststr,
                                                       _gf_false);
                if (ret)
                        goto out;
                //this is just to rename so inject local acc for cluster update
                ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_LOCAL_ACC,
                                                    &event);
                if (!ret) {
                        event->peerinfo = peerinfo;
                        ret = glusterd_friend_sm_inject_event (event);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_SUCCESS,
                                                      (char*)hoststr, port);
                }
        } else {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND,
                                              (char*)hoststr, port);
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                        uuid_t uuid)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        ret = glusterd_friend_find (uuid, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Unable to find peerinfo"
                        " for host: %s %d", hoststr, port);
                goto out;
        }

        if (!peerinfo->rpc) {
                //handle this case
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                (GD_FRIEND_EVENT_INIT_REMOVE_FRIEND, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to get new event");
                return ret;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                goto out;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;

        event->ctx = ctx;

        event->peerinfo = peerinfo;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

out:
        return ret;
}


int
glusterd_xfer_friend_remove_resp (rpcsvc_request_t *req, char *hostname, int port)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (hostname);

        rsp.op_ret = 0;
        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, conf->uuid);
        rsp.hostname = hostname;
        rsp.port = port;
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_rsp);


        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        return ret;
}

int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *hostname, int port,
                               int32_t op_ret, int32_t op_errno)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (hostname);

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, conf->uuid);
        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = gf_strdup (hostname);
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        if (rsp.hostname)
                GF_FREE (rsp.hostname)
        return ret;
}

int
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *hostname, int port)
{
        gf1_cli_probe_rsp    rsp = {0, };
        int32_t              ret = -1;

        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = hostname;
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_probe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL, "Responded to CLI, ret: %d",ret);

        return ret;
}

int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *hostname)
{
        gf1_cli_deprobe_rsp    rsp = {0, };
        int32_t                ret = -1;

        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_deprobe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL, "Responded to CLI, ret: %d",ret);

        return ret;
}

int32_t
glusterd_list_friends (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *friends = NULL;
        gf1_cli_peer_list_rsp   rsp = {0,};

        priv = THIS->private;
        GF_ASSERT (priv);

        if (!list_empty (&priv->peers)) {
                friends = dict_new ();
                if (!friends) {
                        gf_log ("", GF_LOG_WARNING, "Out of Memory");
                        goto out;
                }
        } else {
                ret = 0;
                goto out;
        }

        if (flags == GF_CLI_LIST_ALL) {
                        list_for_each_entry (entry, &priv->peers, uuid_list) {
                                count++;
                                ret = glusterd_add_peer_detail_to_dict (entry,
                                                                friends, count);
                                if (ret)
                                        goto out;

                        }

                        ret = dict_set_int32 (friends, "count", count);

                        if (ret)
                                goto out;
        }

        ret = dict_allocate_and_serialize (friends, &rsp.friends.friends_val,
                                           (size_t *)&rsp.friends.friends_len);

        if (ret)
                goto out;

        ret = 0;
out:

        if (friends)
                dict_unref (friends);

        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_peer_list_rsp);
        if (rsp.friends.friends_val)
                GF_FREE (rsp.friends.friends_val);

        return ret;
}

int32_t
glusterd_get_volumes (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *volumes = NULL;
        gf1_cli_get_vol_rsp     rsp = {0,};
        char                    *volname = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        volumes = dict_new ();
        if (!volumes) {
                gf_log ("", GF_LOG_WARNING, "Out of Memory");
                goto out;
        }

        if (list_empty (&priv->volumes)) {
                ret = 0;
                goto respond;
        }

        if (flags == GF_CLI_GET_VOLUME_ALL) {
                list_for_each_entry (entry, &priv->volumes, vol_list) {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                        volumes, count);
                        if (ret)
                                goto respond;

                        count++;

                }

        } else if (flags == GF_CLI_GET_NEXT_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);

                if (ret) {
                        if (priv->volumes.next) {
                                entry = list_entry (priv->volumes.next,
                                                    typeof (*entry),
                                                    vol_list);
                        }
                } else {
                        ret = glusterd_volinfo_find (volname, &entry);
                        if (ret)
                                goto respond;
                        entry = list_entry (entry->vol_list.next,
                                            typeof (*entry),
                                            vol_list);
                }

                if (&entry->vol_list == &priv->volumes) {
                       goto respond;
                } else {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                         volumes, count);
                        if (ret)
                                goto respond;

                        count++;
                }
        } else if (flags == GF_CLI_GET_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret)
                        goto respond;

                ret = glusterd_volinfo_find (volname, &entry);
                if (ret)
                        goto respond;

                ret = glusterd_add_volume_detail_to_dict (entry,
                                                 volumes, count);
                if (ret)
                        goto respond;

                count++;
        }

respond:
        ret = dict_set_int32 (volumes, "count", count);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (volumes, &rsp.volumes.volumes_val,
                                           (size_t *)&rsp.volumes.volumes_len);

        if (ret)
                goto out;

        ret = 0;
out:
        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_peer_list_rsp);

        if (volumes)
                dict_unref (volumes);

        if (rsp.volumes.volumes_val)
                GF_FREE (rsp.volumes.volumes_val);
        return ret;
}

static int
glusterd_event_connected_inject (glusterd_peerctx_t *peerctx)
{
        GF_ASSERT (peerctx);

        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;


        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_CONNECTED, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get new event");
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "Memory not available");
                goto out;
        }

        peerinfo = peerctx->peerinfo;
        ctx->hostname = gf_strdup (peerinfo->hostname);
        ctx->port = peerinfo->port;
        ctx->req = peerctx->args.req;

        event->peerinfo = peerinfo;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject "
                        "EVENT_CONNECTED ret = %d", ret);
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                     void *data)
{
        xlator_t                *this = NULL;
        char                    *handshake = "on";
        glusterd_conf_t         *conf = NULL;
        int                     ret = 0;
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_peerctx_t     *peerctx = NULL;

        peerctx = mydata;
        if (!peerctx)
                return 0;

        peerinfo = peerctx->peerinfo;
        this = THIS;
        conf = this->private;


        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");
                peerinfo->connected = 1;

                if ((ret < 0) || (strcasecmp (handshake, "on"))) {
                        //ret = glusterd_handshake (this, peerinfo->rpc);

                } else {
                        //conf->rpc->connected = 1;
                        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);
                }

                if (GD_MODE_ON == peerctx->args.mode) {
                        ret = glusterd_event_connected_inject (peerctx);
                        peerctx->args.req = NULL;
                } else if (GD_MODE_SWITCH_ON == peerctx->args.mode) {
                        peerctx->args.mode = GD_MODE_ON;
                }

                glusterd_friend_sm ();
                glusterd_op_sm ();
                break;
        }

        case RPC_CLNT_DISCONNECT:

                //Inject friend disconnected here

                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT");
                peerinfo->connected = 0;

                //default_notify (this, GF_EVENT_CHILD_DOWN, NULL);
                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        return ret;
}
