/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#include "glusterd1.h"
#include "cli1.h"
#include "rpc-clnt.h"
#include "glusterd1-xdr.h"

#include <sys/resource.h>
#include <inttypes.h>

/* for default_*_cbk functions */
#include "defaults.c"
#include "common-utils.h"


/*typedef int32_t (*glusterd_mop_t) (call_frame_t *frame,
                            gf_hdr_common_t *hdr, size_t hdrlen);*/

//static glusterd_mop_t glusterd_ops[GF_MOP_MAXVALUE];



static int
glusterd_friend_find_by_hostname (const char *hoststr,
                                  glusterd_peerinfo_t  **peerinfo)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        glusterd_peer_hostname_t *name = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = THIS->private;

        GF_ASSERT (priv);

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                list_for_each_entry (name, &entry->hostnames, hostname_list) {
                        if (!strncmp (name->hostname, hoststr,
                                        1024)) {

                        gf_log ("glusterd", GF_LOG_NORMAL,
                                 "Friend %s found.. state: %d", hoststr,
                                  entry->state.state);
                        *peerinfo = entry;
                        return 0;
                        }
                }
        }

        return ret;
}

static int
glusterd_friend_find_by_uuid (uuid_t uuid,
                              glusterd_peerinfo_t  **peerinfo)
{
        int                     ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;

        GF_ASSERT (peerinfo);

        *peerinfo = NULL;
        priv    = THIS->private;

        GF_ASSERT (priv);

        list_for_each_entry (entry, &priv->peers, uuid_list) {
                if (!uuid_compare (entry->uuid, uuid)) {

                        gf_log ("glusterd", GF_LOG_NORMAL,
                                 "Friend found.. state: %d",
                                  entry->state.state);
                        *peerinfo = entry;
                        return 0;
                }
        }

        return ret;
}

static int
glusterd_handle_friend_req (rpcsvc_request_t *req, uuid_t  uuid, char *hostname, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = 6969; // TODO: use define values.

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL,
                         "Unable to find peer");

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
glusterd_handle_unfriend_req (rpcsvc_request_t *req, uuid_t  uuid,
                              char *hostname, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = 6969; //TODO: use define'd macro

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL,
                         "Unable to find peer");

        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_REMOVE_FRIEND, &event);

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
        uuid_unparse (peerinfo->uuid, peerinfo->uuid_str);
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
        ret = dict_set_int32 (friends, key, (int32_t)peerinfo->state.state);
        if (ret)
                goto out;

out:
        return ret;
}

static int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count)
{

        int             ret = -1;
        char            key[256] = {0, };

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
        char                            str[50] = {0,};
        glusterd_op_lock_ctx_t          *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_cluster_lock_req (req->msg[0], &lock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (lock_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received LOCK from uuid: %s", str);


        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_stage_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK, ctx);

out:
        gf_log ("", GF_LOG_NORMAL, "Returning %d", ret);

        return ret;
}

int
glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        char                            str[50] = {0,};
        gd1_mgmt_stage_op_req           stage_req = {{0,}};
        glusterd_op_stage_ctx_t         *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_stage_op_req (req->msg[0], &stage_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        uuid_unparse (stage_req.uuid, str);
        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received stage op from uuid: %s", str);

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
        return ret;
}

int
glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        char                            str[50] = {0,};
        gd1_mgmt_commit_op_req          commit_req = {{0},};
        glusterd_op_commit_ctx_t        *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_commit_op_req (req->msg[0], &commit_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        uuid_unparse (commit_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received commit op from uuid: %s", str);

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
        return ret;
}

int
glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_probe_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                gf_log ("", 1, "error");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received CLI probe req %s %d",
                cli_req.hostname, cli_req.port);


        ret = glusterd_probe_begin (req, cli_req.hostname, cli_req.port);

out:
        return ret;
}

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_probe_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received CLI deprobe req");


        ret = glusterd_deprobe_begin (req, cli_req.hostname, cli_req.port);

out:
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
                }
        }

        ret = glusterd_list_friends (req, dict, cli_req.flags);

out:
        return ret;
}



int
glusterd_handle_defrag_volume (rpcsvc_request_t *req)
{
        int32_t                ret           = -1;
        gf1_cli_defrag_vol_req cli_req       = {0,};
        glusterd_conf_t         *priv = NULL;
        char                   cmd_str[4096] = {0,};

        GF_ASSERT (req);

        priv    = THIS->private;
        if (!gf_xdr_to_cli_defrag_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received defrag volume on %s",
                cli_req.volname);

        glusterd_op_set_op (GD_OP_DEFRAG_VOLUME);

        glusterd_op_set_ctx (GD_OP_DEFRAG_VOLUME, cli_req.volname);

        /* TODO: make it more generic.. */
        /* Create a directory, mount glusterfs over it, start glusterfs-defrag */
        snprintf (cmd_str, 4096, "mkdir -p %s/mount/%s",
                  priv->workdir, cli_req.volname);
        ret = system (cmd_str);

        snprintf (cmd_str, 4096, "glusterfs -f %s/vols/%s/%s-tcp.vol "
                  "--xlator-option dht0.unhashed-sticky-bit=yes "
                  "--xlator-option dht0.lookup-unhashed=on %s/mount/%s",
                  priv->workdir, cli_req.volname, cli_req.volname,
                  priv->workdir, cli_req.volname);
        ret = system (cmd_str);

        snprintf (cmd_str, 4096, "glusterfs-defrag %s/mount/%s",
                  priv->workdir, cli_req.volname);
        ret = system (cmd_str);

        ret = 0;
out:
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
                }
        }

        ret = glusterd_get_volumes (req, dict, cli_req.flags);

out:
        return ret;
}

int
glusterd_handle_create_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_create_vol_req          cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_create_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
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
                        goto out;
                }
        }

        ret = glusterd_create_volume (req, dict);

out:
        return ret;
}

int
glusterd_handle_cli_start_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_start_vol_req           cli_req = {0,};
        //dict_t                          *dict = NULL;
        int32_t                         flags = 0;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_start_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received start vol req"
                "for volume %s", cli_req.volname);

        ret = glusterd_start_volume (req, cli_req.volname, flags);

out:
        return ret;
}

int
glusterd_handle_cli_stop_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_stop_vol_req           cli_req = {0,};
        int32_t                         flags = 0;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_stop_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received stop vol req"
                "for volume %s", cli_req.volname);

        ret = glusterd_stop_volume (req, cli_req.volname, flags);

out:
        return ret;
}

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_delete_vol_req          cli_req = {0,};
        int32_t                         flags = 0;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_delete_vol_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Received delete vol req"
                "for volume %s", cli_req.volname);

        ret = glusterd_delete_volume (req, cli_req.volname, flags);

out:
        return ret;
}

int
glusterd_handle_add_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_add_brick_req          cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_add_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

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
                        goto out;
                }
        }

        ret = glusterd_add_brick (req, dict);

out:
        return ret;
}

int
glusterd_handle_remove_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_remove_brick_req        cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!gf_xdr_to_cli_remove_brick_req (req->msg[0], &cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

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
                }
        }

        ret = glusterd_remove_brick (req, dict);

out:
        return ret;
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
        char                            str[50] = {0, };
        glusterd_op_lock_ctx_t          *ctx = NULL;

        GF_ASSERT (req);

        if (!gd_xdr_to_mgmt_cluster_unlock_req (req->msg[0], &unlock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        uuid_unparse (unlock_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received UNLOCK from uuid: %s", str);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }
        uuid_copy (ctx->uuid, unlock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_UNLOCK, ctx);

out:
        return ret;
}

int
glusterd_op_stage_send_resp (rpcsvc_request_t   *req,
                             int32_t op, int32_t status)
{

        gd1_mgmt_stage_op_rsp           rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_stage_op_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to stage, ret: %d", ret);

        return ret;
}

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                               int32_t op, int32_t status)
{
        gd1_mgmt_commit_op_rsp          rsp = {{0}, };
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_commit_op_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to commit, ret: %d", ret);

        return ret;
}

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char                    str[50];

        GF_ASSERT (req);
        if (!gd_xdr_to_mgmt_friend_req (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (friend_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe from uuid: %s", str);

        ret = glusterd_handle_friend_req (req, friend_req.uuid,
                                          friend_req.hostname, friend_req.port);

out:

        return ret;
}

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char                    str[50];

        GF_ASSERT (req);
        if (!gd_xdr_to_mgmt_friend_req (req->msg[0], &friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        uuid_unparse (friend_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received unfriend from uuid: %s", str);

        ret = glusterd_handle_unfriend_req (req, friend_req.uuid,
                                            friend_req.hostname, friend_req.port);

out:

        return ret;
}

int
glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_update     friend_req = {{0},};
        char                    str[50] = {0,};
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
        uuid_unparse (friend_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received friend update from uuid: %s", str);

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
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

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
                        i++;
                        continue;
                }

                ret = glusterd_friend_add (hostname, friend_req.port,
                                           GD_FRIEND_STATE_BEFRIENDED,
                                           &uuid, NULL, &peerinfo);

                i++;
        }

out:
        uuid_copy (rsp.uuid, priv->uuid);
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_update_rsp);
        return ret;
}

int
glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        int32_t             ret = -1;
        char                str[50];
        xlator_t            *this = NULL;
        glusterd_conf_t     *conf = NULL;
        gd1_mgmt_probe_req  probe_req = {{0},};
        gd1_mgmt_probe_rsp  rsp = {{0},};
        char                hostname[1024] = {0};
        glusterd_peer_hostname_t        *name = NULL;

        GF_ASSERT (req);

        probe_req.hostname = hostname;

        if (!gd_xdr_to_mgmt_probe_req (req->msg[0], &probe_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        this = THIS;

        conf = this->private;
        uuid_unparse (probe_req.uuid, str);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Received probe from uuid: %s", str);

        ret = glusterd_peer_hostname_new (probe_req.hostname, &name);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get new peer_hostname");
        } else {
                list_add_tail (&name->hostname_list, &conf->hostnames);
        }


        uuid_copy (rsp.uuid, conf->uuid);
        rsp.hostname = probe_req.hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_probe_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to %s, ret: %d", probe_req.hostname, ret);

out:
        return ret;
}

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid,
                     struct rpc_clnt    *rpc,
                     glusterd_peerinfo_t **friend)
{
        int                     ret = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        dict_t                  *options = NULL;
        struct rpc_clnt_config  rpc_cfg = {0,};
        glusterd_peer_hostname_t *name = NULL;

        priv = THIS->private;

        peerinfo = GF_CALLOC (1, sizeof(*peerinfo), gf_gld_mt_peerinfo_t);

        if (!peerinfo)
                return -1;

        if (friend)
                *friend = peerinfo;

        INIT_LIST_HEAD (&peerinfo->hostnames);
        peerinfo->state.state = state;
        if (hoststr) {
                ret =  glusterd_peer_hostname_new ((char *)hoststr, &name);
                if (ret)
                        goto out;
                list_add_tail (&peerinfo->hostnames, &name->hostname_list);
                rpc_cfg.remote_host = gf_strdup (hoststr);
		peerinfo->hostname = gf_strdup (hoststr);
        }
        INIT_LIST_HEAD (&peerinfo->uuid_list);

        list_add_tail (&peerinfo->uuid_list, &priv->peers);

        if (uuid) {
                uuid_copy (peerinfo->uuid, *uuid);
        }


        if (hoststr) {
                options = dict_new ();
                if (!options)
                        return -1;

                ret = dict_set_str (options, "remote-host", (char *)hoststr);
                if (ret)
                        goto out;


                if (!port)
                        port = GLUSTERD_DEFAULT_PORT;

                rpc_cfg.remote_port = port;

                ret = dict_set_int32 (options, "remote-port", port);
                if (ret)
                        goto out;

                ret = dict_set_str (options, "transport.address-family", "inet");
                if (ret)
                        goto out;

                rpc = rpc_clnt_init (&rpc_cfg, options, THIS->ctx, THIS->name);

                if (!rpc) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "rpc init failed for peer: %s!", hoststr);
                        ret = -1;
                        goto out;
                }

                ret = rpc_clnt_register_notify (rpc, glusterd_rpc_notify,
                                                peerinfo);

                peerinfo->rpc = rpc;

        }

        gf_log ("glusterd", GF_LOG_NORMAL, "connect returned %d", ret);

out:
        return ret;

}



int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_NORMAL, "Unable to find peerinfo"
                        " for host: %s (%d)", hoststr, port);
                ret = glusterd_friend_add ((char *)hoststr, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, NULL, &peerinfo);
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_PROBE, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get new event");
                return ret;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                return ret;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;

        event->peerinfo = peerinfo;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                return ret;
        }

        if (!peerinfo->connected) {
                return  GLUSTERD_CONNECTION_AWAITED;
        }


        return ret;
}

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

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
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get new event");
                return ret;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                goto out;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;

        event->peerinfo = peerinfo;
        event->ctx = ctx;

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
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *hostname, int port)
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
        rsp.hostname = gf_strdup (hostname);
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gd_xdr_serialize_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_NORMAL,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
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

        gf_log ("glusterd", GF_LOG_NORMAL, "Returning %d", ret);

out:
        if (locked && ret)
                glusterd_unlock (priv->uuid);
        return ret;
}

int32_t
glusterd_create_volume (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t  ret       = -1;
        data_t  *data = NULL;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_CREATE_VOLUME);

        glusterd_op_set_ctx (GD_OP_CREATE_VOLUME, dict);

        glusterd_op_set_req (req);

        data = dict_get (dict, "volname");
        if (!data)
                goto out;

        data = dict_get (dict, "type");
        if (!data)
                goto out;

        data = dict_get (dict, "count");
        if (!data)
                goto out;

        data = dict_get (dict, "bricks");
        if (!data)
                goto out;

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_start_volume (rpcsvc_request_t *req, char *volname, int flags)
{
        int32_t      ret       = -1;
        glusterd_op_start_volume_ctx_t  *ctx = NULL;

        GF_ASSERT (req);
        GF_ASSERT (volname);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_start_volume_ctx_t);

        if (!ctx)
                goto out;

        strncpy (ctx->volume_name, volname, GD_VOLUME_NAME_MAX);

        glusterd_op_set_op (GD_OP_START_VOLUME);

        glusterd_op_set_ctx (GD_OP_START_VOLUME, ctx);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_stop_volume (rpcsvc_request_t *req, char *volname, int flags)
{
        int32_t      ret       = -1;
        glusterd_op_stop_volume_ctx_t  *ctx = NULL;

        GF_ASSERT (req);
        GF_ASSERT (volname);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_stop_volume_ctx_t);

        if (!ctx)
                goto out;

        strncpy (ctx->volume_name, volname, GD_VOLUME_NAME_MAX);

        glusterd_op_set_op (GD_OP_STOP_VOLUME);

        glusterd_op_set_ctx (GD_OP_STOP_VOLUME, ctx);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_delete_volume (rpcsvc_request_t *req, char *volname, int flags)
{
        int32_t      ret       = -1;
        glusterd_op_delete_volume_ctx_t  *ctx = NULL;

        GF_ASSERT (req);
        GF_ASSERT (volname);

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_delete_volume_ctx_t);

        if (!ctx)
                goto out;

        strncpy (ctx->volume_name, volname, GD_VOLUME_NAME_MAX);

        glusterd_op_set_op (GD_OP_DELETE_VOLUME);

        glusterd_op_set_ctx (GD_OP_DELETE_VOLUME, ctx);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

out:
        return ret;
}

int32_t
glusterd_add_brick (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_ADD_BRICK);

        glusterd_op_set_ctx (GD_OP_ADD_BRICK, dict);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

        return ret;
}

int32_t
glusterd_remove_brick (rpcsvc_request_t *req, dict_t *dict)
{
        int32_t      ret       = -1;

        GF_ASSERT (req);
        GF_ASSERT (dict);

        glusterd_op_set_op (GD_OP_REMOVE_BRICK);

        glusterd_op_set_ctx (GD_OP_REMOVE_BRICK, dict);
        glusterd_op_set_req (req);

        ret = glusterd_op_txn_begin ();

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

        if (ret) {
                if (friends)
                        dict_destroy (friends);
        }

        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_peer_list_rsp);

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

        priv = THIS->private;
        GF_ASSERT (priv);

        if (!list_empty (&priv->volumes)) {
                volumes = dict_new ();
                if (!volumes) {
                        gf_log ("", GF_LOG_WARNING, "Out of Memory");
                        goto out;
                }
        } else {
                ret = 0;
                goto out;
        }

        if (flags == GF_CLI_GET_VOLUME_ALL) {
                        list_for_each_entry (entry, &priv->volumes, vol_list) {
                                count++;
                                ret = glusterd_add_volume_detail_to_dict (entry,
                                                                volumes, count);
                                if (ret)
                                        goto out;

                        }

                        ret = dict_set_int32 (volumes, "count", count);

                        if (ret)
                                goto out;
        }

        ret = dict_allocate_and_serialize (volumes, &rsp.volumes.volumes_val,
                                           (size_t *)&rsp.volumes.volumes_len);

        if (ret)
                goto out;

        ret = 0;
out:

        if (ret) {
                if (volumes)
                        dict_destroy (volumes);
        }

        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     gf_xdr_serialize_cli_peer_list_rsp);

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

        peerinfo = mydata;
        this = THIS;
        conf = this->private;


        switch (event) {
        case RPC_CLNT_CONNECT:
        {

                gf_log (this->name, GF_LOG_TRACE, "got RPC_CLNT_CONNECT");
                peerinfo->connected = 1;
                glusterd_friend_sm ();
                glusterd_op_sm ();

                if ((ret < 0) || (strcasecmp (handshake, "on"))) {
                        //ret = glusterd_handshake (this, peerinfo->rpc);

                } else {
                        //conf->rpc->connected = 1;
                        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);
                }
                break;
        }

        case RPC_CLNT_DISCONNECT:

                //Inject friend disconnected here

                gf_log (this->name, GF_LOG_TRACE, "got RPC_CLNT_DISCONNECT");
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
