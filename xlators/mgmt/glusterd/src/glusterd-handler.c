/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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
#include "run.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"

#include "glusterd1-xdr.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "rpc-clnt.h"
#include "glusterd-volgen.h"
#include "glusterd-mountbroker.h"

#include <sys/resource.h>
#include <inttypes.h>

#include "defaults.c"
#include "common-utils.h"

#include "globals.h"
#include "glusterd-syncop.h"

#ifdef HAVE_BD_XLATOR
#include <lvm2app.h>
#endif

extern glusterd_op_info_t opinfo;

int glusterd_big_locked_notify (struct rpc_clnt *rpc, void *mydata,
                                rpc_clnt_event_t event,
                                void *data, rpc_clnt_notify_t notify_fn)
{
        glusterd_conf_t *priv = THIS->private;
        int             ret   = -1;
        synclock_lock (&priv->big_lock);
        ret = notify_fn (rpc, mydata, event, data);
        synclock_unlock (&priv->big_lock);
        return ret;
}

int glusterd_big_locked_handler (rpcsvc_request_t *req, rpcsvc_actor actor_fn)
{
        glusterd_conf_t *priv = THIS->private;
        int             ret   = -1;

        synclock_lock (&priv->big_lock);
        ret = actor_fn (req);
        synclock_unlock (&priv->big_lock);

        return ret;
}

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
                ret = glusterd_xfer_friend_add_resp (req, hostname, rhost, port,
                                                     -1, GF_PROBE_UNKNOWN_PEER);
                if (friend_req->vols.vols_val) {
                        free (friend_req->vols.vols_val);
                        friend_req->vols.vols_val = NULL;
                }
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
                GF_FREE (ctx);
                if (dict) {
                        if ((!dict->extra_stdfree) &&
                            friend_req->vols.vols_val)
                                free (friend_req->vols.vols_val);
                        dict_unref (dict);
                } else {
                    free (friend_req->vols.vols_val);
                }
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
        char           *peer_uuid_str = NULL;

        GF_ASSERT (peerinfo);
        GF_ASSERT (friends);

        snprintf (key, 256, "friend%d.uuid", count);
        peer_uuid_str = gd_peer_uuid_str (peerinfo);
        ret = dict_set_str (friends, key, peer_uuid_str);
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

        snprintf (key, 256, "friend%d.stateId", count);
        ret = dict_set_int32 (friends, key, peerinfo->state.state);
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

struct args_pack {
    dict_t *dict;
    int vol_count;
    int opt_count;
};

static int
_build_option_key (dict_t *d, char *k, data_t *v, void *tmp)
{
        char                    reconfig_key[256] = {0, };
        struct args_pack        *pack             = NULL;
        int                     ret               = -1;
        xlator_t                *this             = NULL;
        glusterd_conf_t         *priv             = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        pack = tmp;
        if (strcmp (k, GLUSTERD_GLOBAL_OPT_VERSION) == 0)
                return 0;

        if (priv->op_version > GD_OP_VERSION_MIN) {
                if ((strcmp (k, "features.limit-usage") == 0) ||
                    (strcmp (k, "features.soft-limit") == 0))
                        return 0;
        }
        snprintf (reconfig_key, 256, "volume%d.option.%s",
                  pack->vol_count, k);
        ret = dict_set_str (pack->dict, reconfig_key, v->data);
        if (0 == ret)
                pack->opt_count++;

        return 0;
}

int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int count)
{

        int                     ret = -1;
        char                    key[256] = {0, };
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *buf = NULL;
        int                     i = 1;
        dict_t                  *dict = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    *volume_id_str  = NULL;
        struct args_pack        pack = {0,};
        xlator_t                *this = NULL;
        GF_UNUSED int           caps = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (volumes);

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv);

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

        snprintf (key, 256, "volume%d.dist_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->dist_leaf_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.stripe_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->stripe_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.replica_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->replica_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.transport", count);
        ret = dict_set_int32 (volumes, key, volinfo->transport_type);
        if (ret)
                goto out;

        volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
        if (!volume_id_str)
                goto out;

        snprintf (key, sizeof (key), "volume%d.volume_id", count);
        ret = dict_set_dynstr (volumes, key, volume_id_str);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.rebalance", count);
        ret = dict_set_int32 (volumes, key, volinfo->rebal.defrag_cmd);
        if (ret)
                goto out;

#ifdef HAVE_BD_XLATOR
        if (volinfo->caps) {
                caps = 0;
                snprintf (key, 256, "volume%d.xlator0", count);
                buf = GF_MALLOC (256, gf_common_mt_char);
                if (!buf) {
                        ret = ENOMEM;
                        goto out;
                }
                if (volinfo->caps & CAPS_BD)
                        snprintf (buf, 256, "BD");
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret) {
                        GF_FREE (buf);
                        goto out;
                }

                if (volinfo->caps & CAPS_THIN) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "thin");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret) {
                                GF_FREE (buf);
                                goto out;
                        }
                }

                if (volinfo->caps & CAPS_OFFLOAD_COPY) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "offload_copy");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret) {
                                GF_FREE (buf);
                                goto out;
                        }
                }

                if (volinfo->caps & CAPS_OFFLOAD_SNAPSHOT) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "offload_snapshot");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret)  {
                                GF_FREE (buf);
                                goto out;
                        }
                }

                if (volinfo->caps & CAPS_OFFLOAD_ZERO) {
                        snprintf (key, 256, "volume%d.xlator0.caps%d", count,
                                  caps++);
                        buf = GF_MALLOC (256, gf_common_mt_char);
                        if (!buf) {
                                ret = ENOMEM;
                                goto out;
                        }
                        snprintf (buf, 256, "offload_zerofill");
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret)  {
                                GF_FREE (buf);
                                goto out;
                        }
                }

        }
#endif

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                char    brick[1024] = {0,};
                char    brick_uuid[64] = {0,};
                snprintf (key, 256, "volume%d.brick%d", count, i);
                snprintf (brick, 1024, "%s:%s", brickinfo->hostname,
                          brickinfo->path);
                buf = gf_strdup (brick);
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;
                snprintf (key, 256, "volume%d.brick%d.uuid", count, i);
                snprintf (brick_uuid, 64, "%s", uuid_utoa (brickinfo->uuid));
                buf = gf_strdup (brick_uuid);
                if (!buf)
                        goto out;
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;

#ifdef HAVE_BD_XLATOR
                if (volinfo->caps & CAPS_BD) {
                        snprintf (key, 256, "volume%d.vg%d", count, i);
                        snprintf (brick, 1024, "%s", brickinfo->vg);
                        buf = gf_strdup (brick);
                        ret = dict_set_dynstr (volumes, key, buf);
                        if (ret)
                                goto out;
                }
#endif
                i++;
        }

        dict = volinfo->dict;
        if (!dict) {
                ret = 0;
                goto out;
        }

        pack.dict = volumes;
        pack.vol_count = count;
        pack.opt_count = 0;
        dict_foreach (dict, _build_option_key, (void *) &pack);
        dict_foreach (priv->opts, _build_option_key, &pack);

        snprintf (key, 256, "volume%d.opt_count", pack.vol_count);
        ret = dict_set_int32 (volumes, key, pack.opt_count);
out:
        return ret;
}

int
glusterd_friend_find (uuid_t uuid, char *hostname,
                      glusterd_peerinfo_t **peerinfo)
{
        int     ret = -1;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (uuid) {
                ret = glusterd_friend_find_by_uuid (uuid, peerinfo);

                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                 "Unable to find peer by uuid: %s",
                                 uuid_utoa (uuid));
                } else {
                        goto out;
                }

        }

        if (hostname) {
                ret = glusterd_friend_find_by_hostname (hostname, peerinfo);

                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Unable to find hostname: %s", hostname);
                } else {
                        goto out;
                }
        }

out:
        return ret;
}

int32_t
glusterd_op_txn_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                       char *err_str, size_t err_len)
{
        int32_t                  ret    = -1;
        xlator_t                *this   = NULL;
        glusterd_conf_t         *priv   = NULL;
        int32_t                  locked = 0;

        GF_ASSERT (req);
        GF_ASSERT ((op > GD_OP_NONE) && (op < GD_OP_MAX));
        GF_ASSERT (NULL != ctx);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_lock (MY_UUID);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to acquire lock on localhost, ret: %d", ret);
                snprintf (err_str, err_len, "Another transaction is in progress. "
                          "Please try again after sometime.");
                goto out;
        }

        locked = 1;
        gf_log (this->name, GF_LOG_DEBUG, "Acquired lock on localhost");

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_START_LOCK, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to acquire cluster"
                        " lock.");
                goto out;
        }

        glusterd_op_set_op (op);
        glusterd_op_set_ctx (ctx);
        glusterd_op_set_req (req);


out:
        if (locked && ret)
                glusterd_unlock (MY_UUID);

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
__glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_lock_req       lock_req = {{0},};
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &lock_req,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to decode lock "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Received LOCK from uuid: %s",
                uuid_utoa (lock_req.uuid));

        if (glusterd_friend_find_by_uuid (lock_req.uuid, &peerinfo)) {
                gf_log (this->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (lock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK, ctx);

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cluster_lock);
}

int
glusterd_req_ctx_create (rpcsvc_request_t *rpc_req,
                         glusterd_op_t op, uuid_t uuid,
                         char *buf_val, size_t buf_len,
                         gf_gld_mem_types_t mem_type,
                         glusterd_req_ctx_t **req_ctx_out)
{
        int                 ret     = -1;
        char                str[50] = {0,};
        glusterd_req_ctx_t *req_ctx = NULL;
        dict_t             *dict    = NULL;
        xlator_t           *this    = NULL;

        this = THIS;
        GF_ASSERT (this);

        uuid_unparse (uuid, str);
        gf_log (this->name, GF_LOG_DEBUG, "Received op from uuid %s", str);

        dict = dict_new ();
        if (!dict)
                goto out;

        req_ctx = GF_CALLOC (1, sizeof (*req_ctx), mem_type);
        if (!req_ctx) {
                goto out;
        }

        uuid_copy (req_ctx->uuid, uuid);
        req_ctx->op = op;
        ret = dict_unserialize (buf_val, buf_len, &dict);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to unserialize the dictionary");
                goto out;
        }

        req_ctx->dict = dict;
        req_ctx->req = rpc_req;
        *req_ctx_out = req_ctx;
        ret = 0;
out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                GF_FREE (req_ctx);
        }
        return ret;
}

int
__glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        glusterd_req_ctx_t              *req_ctx = NULL;
        gd1_mgmt_stage_op_req           op_req = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_stage_op_req);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to decode stage "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_friend_find_by_uuid (op_req.uuid, &peerinfo)) {
                gf_log (this->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        ret = glusterd_req_ctx_create (req, op_req.op, op_req.uuid,
                                       op_req.buf.buf_val, op_req.buf.buf_len,
                                       gf_gld_mt_op_stage_ctx_t, &req_ctx);
        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_OP, req_ctx);

 out:
        free (op_req.buf.buf_val);//malloced by xdr
        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}

int
glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_stage_op);
}


int
__glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        glusterd_req_ctx_t              *req_ctx = NULL;
        gd1_mgmt_commit_op_req          op_req = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &op_req,
                              (xdrproc_t)xdr_gd1_mgmt_commit_op_req);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to decode commit "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_friend_find_by_uuid (op_req.uuid, &peerinfo)) {
                gf_log (this->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        //the structures should always be equal
        GF_ASSERT (sizeof (gd1_mgmt_commit_op_req) == sizeof (gd1_mgmt_stage_op_req));
        ret = glusterd_req_ctx_create (req, op_req.op, op_req.uuid,
                                       op_req.buf.buf_val, op_req.buf.buf_len,
                                       gf_gld_mt_op_commit_ctx_t, &req_ctx);
        if (ret)
                goto out;

        ret = glusterd_op_init_ctx (op_req.op);
        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_OP, req_ctx);

out:
        free (op_req.buf.buf_val);//malloced by xdr
        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}

int
glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_commit_op);
}

int
__glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                  cli_req = {{0,},};
        glusterd_peerinfo_t       *peerinfo = NULL;
        gf_boolean_t                run_fsm = _gf_true;
        xlator_t                      *this = NULL;
        char                     *bind_name = NULL;
        dict_t                        *dict = NULL;
        char                      *hostname = NULL;
        int                            port = 0;

        GF_ASSERT (req);
        this = THIS;

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0)  {
                //failed to decode msg;
                gf_log ("", GF_LOG_ERROR, "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get hostname");
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &port);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get port");
                goto out;
        }

        if (glusterd_is_any_volume_in_server_quorum (this) &&
            !does_gd_meet_server_quorum (this)) {
                glusterd_xfer_cli_probe_resp (req, -1, GF_PROBE_QUORUM_NOT_MET,
                                              NULL, hostname, port, dict);
                gf_log (this->name, GF_LOG_ERROR, "Quorum does not meet, "
                        "rejecting operation");
                ret = 0;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received CLI probe req %s %d",
                hostname, port);

        if (dict_get_str(this->options,"transport.socket.bind-address",
                         &bind_name) == 0) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "only checking probe address vs. bind address");
                ret = gf_is_same_address (bind_name, hostname);
        }
        else {
                ret = gf_is_local_addr (hostname);
        }
        if (ret) {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_LOCALHOST,
                                              NULL, hostname, port, dict);
                ret = 0;
                goto out;
        }

        if (!(ret = glusterd_friend_find_by_hostname (hostname, &peerinfo))) {
                if (strcmp (peerinfo->hostname, hostname) == 0) {

                        gf_log ("glusterd", GF_LOG_DEBUG, "Probe host %s port "
                                "%d already a peer", hostname, port);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND,
                                                      NULL, hostname, port,
                                                      dict);
                        goto out;
                }
        }
        ret = glusterd_probe_begin (req, hostname, port, dict);

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;
        }

out:
        free (cli_req.dict.dict_val);

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_cli_probe);
}

int
__glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                  cli_req = {{0,},};
        uuid_t                         uuid = {0};
        int                        op_errno = 0;
        xlator_t                      *this = NULL;
        glusterd_conf_t               *priv = NULL;
        dict_t                        *dict = NULL;
        char                      *hostname = NULL;
        int                            port = 0;
        int                           flags = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received CLI deprobe req");

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get hostname");
                goto out;
        }

        ret = dict_get_int32 (dict, "port", &port);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get port");
                goto out;
        }

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get flags");
                goto out;
        }

        ret = glusterd_hostname_to_uuid (hostname, uuid);
        if (ret) {
                op_errno = GF_DEPROBE_NOT_FRIEND;
                goto out;
        }

        if (!uuid_compare (uuid, MY_UUID)) {
                op_errno = GF_DEPROBE_LOCALHOST;
                ret = -1;
                goto out;
        }

        if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                if (!uuid_is_null (uuid)) {
                        /* Check if peers are connected, except peer being detached*/
                        if (!glusterd_chk_peers_connected_befriended (uuid)) {
                                ret = -1;
                                op_errno = GF_DEPROBE_FRIEND_DOWN;
                                goto out;
                        }
                        ret = glusterd_all_volume_cond_check (
                                                 glusterd_friend_brick_belongs,
                                                 -1, &uuid);
                        if (ret) {
                                op_errno = GF_DEPROBE_BRICK_EXIST;
                                goto out;
                        }
                }

                if (glusterd_is_any_volume_in_server_quorum (this) &&
                    !does_gd_meet_server_quorum (this)) {
                        gf_log (this->name, GF_LOG_ERROR, "Quorum does not "
                                "meet, rejecting operation");
                        ret = -1;
                        op_errno = GF_DEPROBE_QUORUM_NOT_MET;
                        goto out;
                }
        }

        if (!uuid_is_null (uuid)) {
                ret = glusterd_deprobe_begin (req, hostname, port, uuid, dict);
        } else {
                ret = glusterd_deprobe_begin (req, hostname, port, NULL, dict);
        }

out:
        free (cli_req.dict.dict_val);

        if (ret) {
                ret = glusterd_xfer_cli_deprobe_resp (req, ret, op_errno, NULL,
                                                      hostname, dict);
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_cli_deprobe);
}

int
__glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_peer_list_req           cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf1_cli_peer_list_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received cli list req");

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
glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_list_friends);
}

int
__glusterd_handle_cli_get_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        int32_t                         flags = 0;

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received get vol req");

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

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get flags");
                goto out;
        }

        ret = glusterd_get_volumes (req, dict, flags);

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
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_get_volume);
}

int
__glusterd_handle_cli_uuid_reset (rpcsvc_request_t *req)
{
        int                     ret     = -1;
        dict_t                  *dict   = NULL;
        xlator_t                *this   = NULL;
        glusterd_conf_t         *priv   = NULL;
        uuid_t                  uuid    = {0};
        gf_cli_rsp              rsp     = {0,};
        gf_cli_req              cli_req = {{0,}};
        char                    msg_str[2048] = {0,};

        GF_ASSERT (req);

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_DEBUG, "Received uuid reset req");

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
                        snprintf (msg_str, sizeof (msg_str), "Unable to decode "
                                  "the buffer");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        /* In the above section if dict_unserialize is successful, ret is set
         * to zero.
         */
        ret = -1;
        // Do not allow peer reset if there are any volumes in the cluster
        if (!list_empty (&priv->volumes)) {
                snprintf (msg_str, sizeof (msg_str), "volumes are already "
                          "present in the cluster. Resetting uuid is not "
                          "allowed");
                gf_log (this->name, GF_LOG_WARNING, "%s", msg_str);
                goto out;
        }

        // Do not allow peer reset if trusted storage pool is already formed
        if (!list_empty (&priv->peers)) {
                snprintf (msg_str, sizeof (msg_str),"trusted storage pool "
                          "has been already formed. Please detach this peer "
                          "from the pool and reset its uuid.");
                gf_log (this->name, GF_LOG_WARNING, "%s", msg_str);
                goto out;
        }

        uuid_copy (uuid, priv->uuid);
        ret = glusterd_uuid_generate_save ();

        if (!uuid_compare (uuid, MY_UUID)) {
                snprintf (msg_str, sizeof (msg_str), "old uuid and the new uuid"
                          " are same. Try gluster peer reset again");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg_str);
                ret = -1;
                goto out;
        }

out:
        if (ret) {
                rsp.op_ret = -1;
                if (msg_str[0] == '\0')
                        snprintf (msg_str, sizeof (msg_str), "Operation "
                                  "failed");
                rsp.op_errstr = msg_str;
                ret = 0;
        } else {
                rsp.op_errstr = "";
        }

        glusterd_to_cli (req, &rsp, NULL, 0, NULL,
                         (xdrproc_t)xdr_gf_cli_rsp, dict);

        return ret;
}

int
glusterd_handle_cli_uuid_reset (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_uuid_reset);
}

int
__glusterd_handle_cli_uuid_get (rpcsvc_request_t *req)
{
        int                     ret         = -1;
        dict_t                  *dict       = NULL;
        dict_t                  *rsp_dict   = NULL;
        xlator_t                *this       = NULL;
        glusterd_conf_t         *priv       = NULL;
        gf_cli_rsp              rsp         = {0,};
        gf_cli_req              cli_req     = {{0,}};
        char                    msg_str[2048] = {0,};
        char                    uuid_str[64] = {0,};

        GF_ASSERT (req);

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_DEBUG, "Received uuid get req");

        if (cli_req.dict.dict_len) {
                dict  = dict_new ();
                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg_str, sizeof (msg_str), "Unable to decode "
                                  "the buffer");
                        goto out;

                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;

                }
        }

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                ret = -1;
                goto out;
        }

        uuid_utoa_r (MY_UUID, uuid_str);
        ret = dict_set_str (rsp_dict, "uuid", uuid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set uuid in "
                        "dictionary.");
                goto out;
        }

        ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to serialize "
                        "dictionary.");
                goto out;
        }
        ret = 0;
out:
        if (ret) {
                rsp.op_ret = -1;
                if (msg_str[0] == '\0')
                        snprintf (msg_str, sizeof (msg_str), "Operation "
                                  "failed");
                rsp.op_errstr = msg_str;

        } else {
                rsp.op_errstr = "";

        }

        glusterd_to_cli (req, &rsp, NULL, 0, NULL,
                         (xdrproc_t)xdr_gf_cli_rsp, dict);

        return 0;
}
int
glusterd_handle_cli_uuid_get (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_uuid_get);
}

int
__glusterd_handle_cli_list_volume (rpcsvc_request_t *req)
{
        int                     ret = -1;
        dict_t                  *dict = NULL;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int                     count = 0;
        char                    key[1024] = {0,};
        gf_cli_rsp              rsp = {0,};

        GF_ASSERT (req);

        priv = THIS->private;
        GF_ASSERT (priv);

        dict = dict_new ();
        if (!dict)
                goto out;

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d", count);
                ret = dict_set_str (dict, key, volinfo->volname);
                if (ret)
                        goto out;
                count++;
        }

        ret = dict_set_int32 (dict, "count", count);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);
        if (ret)
                goto out;

        ret = 0;

out:
        rsp.op_ret = ret;
        if (ret)
                rsp.op_errstr = "Error listing volumes";
        else
                rsp.op_errstr = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);
        ret = 0;

        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_list_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_list_volume);
}

int32_t
glusterd_op_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                   char *err_str, size_t err_len)
{
        int             ret = -1;

        ret = glusterd_op_txn_begin (req, op, ctx, err_str, err_len);

        return ret;
}

int
__glusterd_handle_reset_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_RESET_VOLUME;
        char                            *volname = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode request "
                          "received from cli");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                    "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }
        gf_log (this->name, GF_LOG_DEBUG, "Received volume reset request for "
                "volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_RESET_VOLUME, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        return ret;
}

int
glusterd_handle_reset_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_reset_volume);
}

int
__glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_SET_VOLUME;
        char                            *key = NULL;
        char                            *value = NULL;
        char                            *volname = NULL;
        char                            *op_errstr = NULL;
        gf_boolean_t                    help = _gf_false;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Failed to decode "
                          "request received from cli");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get volume "
                          "name while handling volume set command");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        if (strcmp (volname, "help") == 0 ||
            strcmp (volname, "help-xml") == 0) {
                ret = glusterd_volset_help (dict, &op_errstr);
                help = _gf_true;
                goto out;
        }

        ret = dict_get_str (dict, "key1", &key);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get key while"
                          " handling volume set for %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = dict_get_str (dict, "value1", &value);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get value while"
                          " handling volume set for %s", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }
        gf_log (this->name, GF_LOG_DEBUG, "Received volume set request for "
                "volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_SET_VOLUME, dict);

out:
        if (help)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req, dict,
                                                     (op_errstr)? op_errstr:"");
        else if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        if (op_errstr)
                GF_FREE (op_errstr);

        return ret;
}

int
glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_set_volume);
}

int
__glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        int32_t                          ret     = -1;
        gf_cli_req                       cli_req = {{0,}};
        dict_t                           *dict = NULL;
        gf_cli_rsp                       cli_rsp = {0.};
        char                             msg[2048] = {0,};
        char                             *volname = NULL;
        gf1_cli_sync_volume              flags = 0;
        char                             *hostname = NULL;
        xlator_t                         *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg, sizeof (msg), "Unable to decode the "
                                  "command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get hostname");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                ret = dict_get_int32 (dict, "flags", (int32_t*)&flags);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Failed to get volume name"
                                  " or flags");
                        gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                        goto out;
                }
        }

        gf_log (this->name, GF_LOG_INFO, "Received volume sync req "
                "for volume %s", (flags & GF_CLI_SYNC_ALL) ? "all" : volname);

        if (gf_is_local_addr (hostname)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "sync from localhost"
                          " not allowed");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_SYNC_VOLUME, dict);

out:
        if (ret) {
                cli_rsp.op_ret = -1;
                cli_rsp.op_errstr = msg;
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                glusterd_to_cli (req, &cli_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gf_cli_rsp, dict);

                ret = 0; //sent error to cli, prevent second reply
        }

        return ret;
}

int
glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_sync_volume);
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
                                                &rsp.fsm_log.fsm_log_len);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
        GF_FREE (rsp.fsm_log.fsm_log_val);

        gf_log ("glusterd", GF_LOG_DEBUG, "Responded, ret: %d", ret);

        return 0;
}

int
__glusterd_handle_fsm_log (rpcsvc_request_t *req)
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

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf1_cli_fsm_log_req);
        if (ret < 0) {
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
        free (cli_req.name);//malloced by xdr
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return 0;//send 0 to avoid double reply
}

int
glusterd_handle_fsm_log (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_fsm_log);
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
                                     (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);

        gf_log (THIS->name, GF_LOG_DEBUG, "Responded to lock, ret: %d", ret);

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
                                     (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);

        gf_log (THIS->name, GF_LOG_DEBUG, "Responded to unlock, ret: %d", ret);

        return ret;
}

int
__glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_unlock_req     unlock_req = {{0}, };
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;
        xlator_t                        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &unlock_req,
                              (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_req);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to decode unlock "
                        "request received from peer");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        gf_log (this->name, GF_LOG_DEBUG,
                "Received UNLOCK from uuid: %s", uuid_utoa (unlock_req.uuid));

        if (glusterd_friend_find_by_uuid (unlock_req.uuid, &peerinfo)) {
                gf_log (this->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (unlock_req.uuid));
                ret = -1;
                goto out;
        }

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
glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cluster_unlock);
}

int
glusterd_op_stage_send_resp (rpcsvc_request_t   *req,
                             int32_t op, int32_t status,
                             char *op_errstr, dict_t *rsp_dict)
{
        gd1_mgmt_stage_op_rsp           rsp      = {{0},};
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
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get serialized length of dict");
                return ret;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);

        gf_log (this->name, GF_LOG_DEBUG, "Responded to stage, ret: %d", ret);
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
        xlator_t                        *this = NULL;

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

        if (rsp_dict) {
                ret = dict_allocate_and_serialize (rsp_dict, &rsp.dict.dict_val,
                                                   &rsp.dict.dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to get serialized length of dict");
                        goto out;
                }
        }


        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);

        gf_log (this->name, GF_LOG_DEBUG, "Responded to commit, ret: %d", ret);

out:
        GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
__glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        gf_boolean_t            run_fsm = _gf_true;

        GF_ASSERT (req);
        ret = xdr_to_generic (req->msg[0], &friend_req,
                              (xdrproc_t)xdr_gd1_mgmt_friend_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO,
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
        free (friend_req.hostname);//malloced by xdr

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_incoming_friend_req);
}

int
__glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);
        ret = xdr_to_generic (req->msg[0], &friend_req,
                              (xdrproc_t)xdr_gd1_mgmt_friend_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO,
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
        free (friend_req.hostname);//malloced by xdr
        free (friend_req.vols.vols_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_incoming_unfriend_req);

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
                ret = glusterd_store_peerinfo (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
__glusterd_handle_friend_update (rpcsvc_request_t *req)
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

        ret = xdr_to_generic (req->msg[0], &friend_req,
                              (xdrproc_t)xdr_gd1_mgmt_friend_update);
        if (ret < 0) {
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
        gf_log ("glusterd", GF_LOG_INFO,
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

                gf_log ("", GF_LOG_INFO, "Received uuid: %s, hostname:%s",
                                uuid_buf, hostname);

                if (uuid_is_null (uuid)) {
                        gf_log (this->name, GF_LOG_WARNING, "Updates mustn't "
                                "contain peer with 'null' uuid");
                        continue;
                }

                if (!uuid_compare (uuid, MY_UUID)) {
                        gf_log ("", GF_LOG_INFO, "Received my uuid as Friend");
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
                                           &uuid, &peerinfo, 0, &args);

                i++;
        }

out:
        uuid_copy (rsp.uuid, MY_UUID);
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_update_rsp);
        if (dict) {
                if (!dict->extra_stdfree && friend_req.friends.friends_val)
                        free (friend_req.friends.friends_val);//malloced by xdr
                dict_unref (dict);
        } else {
                free (friend_req.friends.friends_val);//malloced by xdr
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_friend_update);
}

int
__glusterd_handle_probe_query (rpcsvc_request_t *req)
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

        ret = xdr_to_generic (req->msg[0], &probe_req,
                              (xdrproc_t)xdr_gd1_mgmt_probe_req);
        if (ret < 0) {
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

        gf_log ("glusterd", GF_LOG_INFO,
                "Received probe from uuid: %s", uuid_utoa (probe_req.uuid));

        /* Check for uuid collision and handle it in a user friendly way by
         * sending the error.
         */
        if (!uuid_compare (probe_req.uuid, MY_UUID)) {
                gf_log (THIS->name, GF_LOG_ERROR, "Peer uuid %s is same as "
                        "local uuid. Please check the uuid of both the peers "
                        "from %s/%s", uuid_utoa (probe_req.uuid),
                        GLUSTERD_DEFAULT_WORKDIR, GLUSTERD_INFO_FILE);
                rsp.op_ret = -1;
                rsp.op_errno = GF_PROBE_SAME_UUID;
                rsp.port = port;
                goto respond;
        }

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
                gf_log ("glusterd", GF_LOG_INFO, "Unable to find peerinfo"
                        " for host: %s (%d)", remote_hostname, port);
                args.mode = GD_MODE_ON;
                ret = glusterd_friend_add (remote_hostname, port,
                                           GD_FRIEND_STATE_PROBE_RCVD,
                                           NULL, &peerinfo, 0, &args);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to add peer %s",
                                remote_hostname);
                        rsp.op_errno = GF_PROBE_ADD_FAILED;
                }
        }

respond:
        uuid_copy (rsp.uuid, MY_UUID);

        rsp.hostname = probe_req.hostname;
        rsp.op_errstr = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gd1_mgmt_probe_rsp);
        ret = 0;

        gf_log ("glusterd", GF_LOG_INFO, "Responded to %s, op_ret: %d, "
                "op_errno: %d, ret: %d", remote_hostname,
                rsp.op_ret, rsp.op_errno, ret);

out:
        free (probe_req.hostname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_probe_query);
}

int
__glusterd_handle_cli_profile_volume (rpcsvc_request_t *req)
{
        int32_t                         ret     = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_PROFILE_VOLUME;
        char                            *volname = NULL;
        int32_t                         op = 0;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len > 0) {
                dict = dict_new();
                if (!dict)
                        goto out;
                dict_unserialize (cli_req.dict.dict_val,
                                  cli_req.dict.dict_len, &dict);
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Received volume profile req "
                "for volume %s", volname);
        ret = dict_get_int32 (dict, "op", &op);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get operation");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        ret = glusterd_op_begin (req, cli_op, dict, err_str, sizeof (err_str));

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        free (cli_req.dict.dict_val);

        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_cli_profile_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_profile_volume);
}

int
__glusterd_handle_getwd (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gf1_cli_getwd_rsp     rsp = {0,};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (req);

        priv = THIS->private;
        GF_ASSERT (priv);

        gf_log ("glusterd", GF_LOG_INFO, "Received getwd req");

        rsp.wd = priv->workdir;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_getwd_rsp);
        ret = 0;

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_getwd (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_getwd);
}

int
__glusterd_handle_mount (rpcsvc_request_t *req)
{
        gf1_cli_mount_req mnt_req = {0,};
        gf1_cli_mount_rsp rsp     = {0,};
        dict_t *dict              = NULL;
        int ret                   = 0;
        glusterd_conf_t     *priv   = NULL;

        GF_ASSERT (req);
	priv = THIS->private;

        ret = xdr_to_generic (req->msg[0], &mnt_req,
                              (xdrproc_t)xdr_gf1_cli_mount_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                rsp.op_ret = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received mount req");

        if (mnt_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (mnt_req.dict.dict_val,
                                        mnt_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        rsp.op_ret = -1;
                        rsp.op_errno = -EINVAL;
                        goto out;
                } else {
                        dict->extra_stdfree = mnt_req.dict.dict_val;
                }
        }

	synclock_unlock (&priv->big_lock);
        rsp.op_ret = glusterd_do_mount (mnt_req.label, dict,
                                        &rsp.path, &rsp.op_errno);
	synclock_lock (&priv->big_lock);

 out:
        if (!rsp.path)
                rsp.path = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_mount_rsp);
        ret = 0;

        if (dict)
                dict_unref (dict);
        if (*rsp.path)
                GF_FREE (rsp.path);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_mount (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_mount);
}

int
__glusterd_handle_umount (rpcsvc_request_t *req)
{
        gf1_cli_umount_req umnt_req = {0,};
        gf1_cli_umount_rsp rsp      = {0,};
        char *mountbroker_root      = NULL;
        char mntp[PATH_MAX]         = {0,};
        char *path                  = NULL;
        runner_t runner             = {0,};
        int ret                     = 0;
        xlator_t *this              = THIS;
        gf_boolean_t dir_ok         = _gf_false;
        char *pdir                  = NULL;
        char *t                     = NULL;
        glusterd_conf_t     *priv   = NULL;

        GF_ASSERT (req);
        GF_ASSERT (this);
        priv = this->private;

        ret = xdr_to_generic (req->msg[0], &umnt_req,
                              (xdrproc_t)xdr_gf1_cli_umount_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                rsp.op_ret = -1;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received umount req");

        if (dict_get_str (this->options, "mountbroker-root",
                          &mountbroker_root) != 0) {
                rsp.op_errno = ENOENT;
                goto out;
        }

        /* check if it is allowed to umount path */
        path = gf_strdup (umnt_req.path);
        if (!path) {
                rsp.op_errno = ENOMEM;
                goto out;
        }
        dir_ok = _gf_false;
        pdir = dirname (path);
        t = strtail (pdir, mountbroker_root);
        if (t && *t == '/') {
                t = strtail(++t, MB_HIVE);
                if (t && !*t)
                        dir_ok = _gf_true;
        }
        GF_FREE (path);
        if (!dir_ok) {
                rsp.op_errno = EACCES;
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, "umount", umnt_req.path, NULL);
        if (umnt_req.lazy)
                runner_add_arg (&runner, "-l");
        synclock_unlock (&priv->big_lock);
        rsp.op_ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (rsp.op_ret == 0) {
                if (realpath (umnt_req.path, mntp))
                        rmdir (mntp);
                else {
                        rsp.op_ret = -1;
                        rsp.op_errno = errno;
                }
                if (unlink (umnt_req.path) != 0) {
                        rsp.op_ret = -1;
                        rsp.op_errno = errno;
                }
        }

 out:
        if (rsp.op_errno)
                rsp.op_ret = -1;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_umount_rsp);
        ret = 0;

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_umount (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_umount);
}

int
glusterd_friend_remove (uuid_t uuid, char *hostname)
{
        int                           ret = 0;
        glusterd_peerinfo_t           *peerinfo = NULL;

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);
        if (ret)
                goto out;

        ret = glusterd_friend_remove_cleanup_vols (peerinfo->uuid);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING, "Volumes cleanup failed");
        ret = glusterd_friend_cleanup (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_rpc_create (struct rpc_clnt **rpc,
                     dict_t *options,
                     rpc_clnt_notify_t notify_fn,
                     void *notify_data)
{
        struct rpc_clnt         *new_rpc = NULL;
        int                     ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (options);

        /* TODO: is 32 enough? or more ? */
        new_rpc = rpc_clnt_new (options, this->ctx, this->name, 16);
        if (!new_rpc)
                goto out;

        ret = rpc_clnt_register_notify (new_rpc, notify_fn, notify_data);
        *rpc = new_rpc;
        if (ret)
                goto out;
        ret = rpc_clnt_start (new_rpc);
out:
        if (ret) {
                if (new_rpc) {
                        (void) rpc_clnt_unref (new_rpc);
                }
        }

        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_transport_keepalive_options_get (int *interval, int *time)
{
        int     ret = 0;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-interval",
                              interval);
        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-time",
                              time);
        return 0;
}

int
glusterd_transport_inet_options_build (dict_t **options, const char *hostname,
                                       int port)
{
        dict_t  *dict = NULL;
        int32_t interval = -1;
        int32_t time     = -1;
        int     ret = 0;

        GF_ASSERT (options);
        GF_ASSERT (hostname);

        if (!port)
                port = GLUSTERD_DEFAULT_PORT;

        /* Build default transport options */
        ret = rpc_transport_inet_options_build (&dict, hostname, port);
        if (ret)
                goto out;

        /* Set frame-timeout to 10mins. Default timeout of 30 mins is too long
         * when compared to 2 mins for cli timeout. This ensures users don't
         * wait too long after cli timesout before being able to resume normal
         * operations
         */
        ret = dict_set_int32 (dict, "frame-timeout", 600);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Failed to set frame-timeout");
                goto out;
        }

        /* Set keepalive options */
        glusterd_transport_keepalive_options_get (&interval, &time);

        if ((interval > 0) || (time > 0))
                ret = rpc_transport_keepalive_options_set (dict, interval, time);
        *options = dict;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_friend_rpc_create (xlator_t *this, glusterd_peerinfo_t *peerinfo,
                            glusterd_peerctx_args_t *args)
{
        dict_t                 *options = NULL;
        int                    ret = -1;
        glusterd_peerctx_t     *peerctx = NULL;
        data_t                 *data = NULL;

        peerctx = GF_CALLOC (1, sizeof (*peerctx), gf_gld_mt_peerctx_t);
        if (!peerctx)
                goto out;

        if (args)
                peerctx->args = *args;

        peerctx->peerinfo = peerinfo;

        ret = glusterd_transport_inet_options_build (&options,
                                                     peerinfo->hostname,
                                                     peerinfo->port);
        if (ret)
                goto out;

        /*
         * For simulated multi-node testing, we need to make sure that we
         * create our RPC endpoint with the same address that the peer would
         * use to reach us.
         */
        if (this->options) {
                data = dict_get(this->options,"transport.socket.bind-address");
                if (data) {
                        ret = dict_set(options,
                                       "transport.socket.source-addr",data);
                }
        }

        ret = glusterd_rpc_create (&peerinfo->rpc, options,
                                   glusterd_peer_rpc_notify, peerctx);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to create rpc for"
                        " peer %s", peerinfo->hostname);
                goto out;
        }
        peerctx = NULL;
        ret = 0;
out:
        GF_FREE (peerctx);
        return ret;
}

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid,
                     glusterd_peerinfo_t **friend,
                     gf_boolean_t restore,
                     glusterd_peerctx_args_t *args)
{
        int                     ret = 0;
        xlator_t               *this = NULL;
        glusterd_conf_t        *conf = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);
        GF_ASSERT (hoststr);

        ret = glusterd_peerinfo_new (friend, state, uuid, hoststr, port);
        if (ret) {
                goto out;
        }

        /*
         * We can't add to the list after calling glusterd_friend_rpc_create,
         * even if it succeeds, because by then the callback to take it back
         * off and free might have happened already (notably in the case of an
         * invalid peer name).  That would mean we're adding something that had
         * just been free, and we're likely to crash later.
         */
        list_add_tail (&(*friend)->uuid_list, &conf->peers);

        //restore needs to first create the list of peers, then create rpcs
        //to keep track of quorum in race-free manner. In restore for each peer
        //rpc-create calls rpc_notify when the friend-list is partially
        //constructed, leading to wrong quorum calculations.
        if (!restore) {
                ret = glusterd_store_peerinfo (*friend);
                if (ret == 0) {
                        synclock_unlock (&conf->big_lock);
                        ret = glusterd_friend_rpc_create (this, *friend, args);
                        synclock_lock (&conf->big_lock);
                }
                else {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to store peerinfo");
                }
        }

        if (ret) {
                (void) glusterd_friend_cleanup (*friend);
                *friend = NULL;
        }

out:
        gf_log (this->name, GF_LOG_INFO, "connect returned %d", ret);
        return ret;
}

int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                      dict_t *dict)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        glusterd_friend_sm_event_t      *event = NULL;

        GF_ASSERT (hoststr);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_INFO, "Unable to find peerinfo"
                        " for host: %s (%d)", hoststr, port);
                args.mode = GD_MODE_ON;
                args.req  = req;
                args.dict = dict;
                ret = glusterd_friend_add ((char *)hoststr, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, &peerinfo, 0, &args);
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
                                                      NULL, (char*)hoststr,
                                                      port, dict);
                }
        } else {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND, NULL,
                                              (char*)hoststr, port, dict);
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                        uuid_t uuid, dict_t *dict)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        ret = glusterd_friend_find (uuid, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_INFO, "Unable to find peerinfo"
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
        ctx->dict = dict;

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

        uuid_copy (rsp.uuid, MY_UUID);
        rsp.hostname = hostname;
        rsp.port = port;
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        return ret;
}


int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *myhostname,
                               char *remote_hostname, int port, int32_t op_ret,
                               int32_t op_errno)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (myhostname);

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, MY_UUID);
        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = gf_strdup (myhostname);
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to %s (%d), ret: %d", remote_hostname, port, ret);
        GF_FREE (rsp.hostname);
        return ret;
}

static void
set_probe_error_str (int op_ret, int op_errno, char *op_errstr, char *errstr,
                     size_t len, char *hostname, int port)
{
        if ((op_errstr) && (strcmp (op_errstr, ""))) {
                snprintf (errstr, len, "%s", op_errstr);
                return;
        }

        if (!op_ret) {
                switch (op_errno) {
                        case GF_PROBE_LOCALHOST:
                                snprintf (errstr, len, "Probe on localhost not "
                                          "needed");
                                break;

                        case GF_PROBE_FRIEND:
                                snprintf (errstr, len, "Host %s port %d already"
                                          " in peer list", hostname, port);
                                break;

                        default:
                                if (op_errno != 0)
                                        snprintf (errstr, len, "Probe returned "
                                                  "with unknown errno %d",
                                                  op_errno);
                                break;
                }
        } else {
                switch (op_errno) {
                        case GF_PROBE_ANOTHER_CLUSTER:
                                snprintf (errstr, len, "%s is already part of "
                                          "another cluster", hostname);
                                break;

                        case GF_PROBE_VOLUME_CONFLICT:
                                snprintf (errstr, len, "Atleast one volume on "
                                          "%s conflicts with existing volumes "
                                          "in the cluster", hostname);
                                break;

                        case GF_PROBE_UNKNOWN_PEER:
                                snprintf (errstr, len, "%s responded with "
                                          "'unknown peer' error, this could "
                                          "happen if %s doesn't have localhost "
                                          "in its peer database", hostname,
                                          hostname);
                                break;

                        case GF_PROBE_ADD_FAILED:
                                snprintf (errstr, len, "Failed to add peer "
                                          "information on %s", hostname);
                                break;

                        case GF_PROBE_SAME_UUID:
                                snprintf (errstr, len, "Peer uuid (host %s) is "
                                          "same as local uuid", hostname);
                                break;

                        case GF_PROBE_QUORUM_NOT_MET:
                                snprintf (errstr, len, "Cluster quorum is not "
                                          "met. Changing peers is not allowed "
                                          "in this state");
                                break;

                        default:
                                snprintf (errstr, len, "Probe returned with "
                                          "unknown errno %d", op_errno);
                                break;
                }
        }
}

int
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *op_errstr, char *hostname,
                              int port, dict_t *dict)
{
        gf_cli_rsp           rsp = {0,};
        int32_t              ret = -1;
        char        errstr[2048] = {0,};
        char            *cmd_str = NULL;
        xlator_t           *this = THIS;

        GF_ASSERT (req);
        GF_ASSERT (this);

        (void) set_probe_error_str (op_ret, op_errno, op_errstr, errstr,
                                    sizeof (errstr), hostname, port);

        if (dict) {
                ret = dict_get_str (dict, "cmd-str", &cmd_str);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "command string");
        }

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.op_errstr = (errstr[0] != '\0') ? errstr : "";

        gf_cmd_log ("", "%s : %s %s %s", cmd_str,
                    (op_ret) ? "FAILED" : "SUCCESS",
                    (errstr[0] != '\0') ? ":" : " ",
                    (errstr[0] != '\0') ? errstr : " ");

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);

        if (dict)
                dict_unref (dict);
        gf_log (this->name, GF_LOG_DEBUG, "Responded to CLI, ret: %d",ret);

        return ret;
}

static void
set_deprobe_error_str (int op_ret, int op_errno, char *op_errstr, char *errstr,
                       size_t len, char *hostname)
{
        if ((op_errstr) && (strcmp (op_errstr, ""))) {
                snprintf (errstr, len, "%s", op_errstr);
                return;
        }

        if (op_ret) {
                switch (op_errno) {
                        case GF_DEPROBE_LOCALHOST:
                                snprintf (errstr, len, "%s is localhost",
                                hostname);
                                break;

                        case GF_DEPROBE_NOT_FRIEND:
                                snprintf (errstr, len, "%s is not part of "
                                          "cluster", hostname);
                                break;

                        case GF_DEPROBE_BRICK_EXIST:
                                snprintf (errstr, len, "Brick(s) with the peer "
                                          "%s exist in cluster", hostname);
                                break;

                        case GF_DEPROBE_FRIEND_DOWN:
                                snprintf (errstr, len, "One of the peers is "
                                          "probably down. Check with "
                                          "'peer status'");
                                break;

                        case GF_DEPROBE_QUORUM_NOT_MET:
                                snprintf (errstr, len, "Cluster quorum is not "
                                          "met. Changing peers is not allowed "
                                          "in this state");
                                break;

                        default:
                                snprintf (errstr, len, "Detach returned with "
                                          "unknown errno %d", op_errno);
                                break;

                }
        }
}


int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *op_errstr,
                                char *hostname, dict_t *dict)
{
        gf_cli_rsp             rsp = {0,};
        int32_t                ret = -1;
        char              *cmd_str = NULL;
        char          errstr[2048] = {0,};

        GF_ASSERT (req);

        (void) set_deprobe_error_str (op_ret, op_errno, op_errstr, errstr,
                                      sizeof (errstr), hostname);

        if (dict) {
                ret = dict_get_str (dict, "cmd-str", &cmd_str);
                if (ret)
                        gf_log (THIS->name, GF_LOG_ERROR, "Failed to get "
                                "command string");
        }

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.op_errstr = (errstr[0] != '\0') ? errstr : "";

        gf_cmd_log ("", "%s : %s %s %s", cmd_str,
                    (op_ret) ? "FAILED" : "SUCCESS",
                    (errstr[0] != '\0') ? ":" : " ",
                    (errstr[0] != '\0') ? errstr : " ");

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);

        gf_log (THIS->name, GF_LOG_DEBUG, "Responded to CLI, ret: %d",ret);

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
        char                    my_uuid_str[64] = {0,};
        char                    key[256] = {0,};

        priv = THIS->private;
        GF_ASSERT (priv);

        friends = dict_new ();
        if (!friends) {
                gf_log ("", GF_LOG_WARNING, "Out of Memory");
                goto out;
        }
        if (!list_empty (&priv->peers)) {
                list_for_each_entry (entry, &priv->peers, uuid_list) {
                        count++;
                        ret = glusterd_add_peer_detail_to_dict (entry,
                                                                friends, count);
                        if (ret)
                                goto out;
                }
        }

        if (flags == GF_CLI_LIST_POOL_NODES) {
                count++;
                snprintf (key, 256, "friend%d.uuid", count);
                uuid_utoa_r (MY_UUID, my_uuid_str);
                ret = dict_set_str (friends, key, my_uuid_str);
                if (ret)
                        goto out;

                snprintf (key, 256, "friend%d.hostname", count);
                ret = dict_set_str (friends, key, "localhost");
                if (ret)
                        goto out;

                snprintf (key, 256, "friend%d.connected", count);
                ret = dict_set_int32 (friends, key, 1);
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (friends, &rsp.friends.friends_val,
                                           &rsp.friends.friends_len);

        if (ret)
                goto out;

        ret = 0;
out:

        if (friends)
                dict_unref (friends);

        rsp.op_ret = ret;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
        ret = 0;
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
        gf_cli_rsp              rsp = {0,};
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
        ret = dict_allocate_and_serialize (volumes, &rsp.dict.dict_val,
                                           &rsp.dict.dict_len);

        if (ret)
                goto out;

        ret = 0;
out:
        rsp.op_ret = ret;

        rsp.op_errstr = "";
        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_cli_rsp);
        ret = 0;

        if (volumes)
                dict_unref (volumes);

        GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
__glusterd_handle_status_volume (rpcsvc_request_t *req)
{
        int32_t                         ret     = -1;
        uint32_t                        cmd     = 0;
        dict_t                         *dict    = NULL;
        char                           *volname = 0;
        gf_cli_req                      cli_req = {{0,}};
        glusterd_op_t                   cli_op  = GD_OP_STATUS_VOLUME;
        char                            err_str[2048] = {0,};
        xlator_t                       *this = NULL;
        glusterd_conf_t                *conf = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len > 0) {
                dict = dict_new();
                if (!dict)
                        goto out;
                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "unserialize buffer");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }

        }

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (!(cmd & GF_CLI_STATUS_ALL)) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        snprintf (err_str, sizeof (err_str), "Unable to get "
                                  "volume name");
                        gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                        goto out;
                }
                gf_log (this->name, GF_LOG_INFO,
                        "Received status volume req for volume %s", volname);

        }
        if ((cmd & GF_CLI_STATUS_QUOTAD) &&
            (conf->op_version == GD_OP_VERSION_MIN)) {
                snprintf (err_str, sizeof (err_str), "The cluster is operating "
                          "at version 1. Getting the status of quotad is not "
                          "allowed in this state.");
                ret = -1;
                goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_STATUS_VOLUME, dict);

out:

        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        free (cli_req.dict.dict_val);

        return ret;
}

int
glusterd_handle_status_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_status_volume);
}

int
__glusterd_handle_cli_clearlocks_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        glusterd_op_t                   cli_op = GD_OP_CLEARLOCKS_VOLUME;
        char                            *volname = NULL;
        dict_t                          *dict = NULL;
        char                            err_str[2048] = {0,};
        xlator_t                        *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = -1;
        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to unserialize req-buffer to"
                                " dictionary");
                        snprintf (err_str, sizeof (err_str), "unable to decode "
                                  "the command");
                        goto out;
                }

        } else {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Empty cli request.");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Unable to get volume "
                          "name");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        gf_log (this->name, GF_LOG_INFO, "Received clear-locks volume req "
                "for volume %s", volname);

        ret = glusterd_op_begin_synctask (req, GD_OP_CLEARLOCKS_VOLUME, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        free (cli_req.dict.dict_val);

        return ret;
}

int
glusterd_handle_cli_clearlocks_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_cli_clearlocks_volume);
}

static int
get_brickinfo_from_brickid (char *brickid, glusterd_brickinfo_t **brickinfo)
{
        glusterd_volinfo_t      *volinfo    = NULL;
        char                    *volid_str  = NULL;
        char                    *brick      = NULL;
        char                    *brickid_dup = NULL;
        uuid_t                  volid       = {0};
        int                     ret         = -1;

        brickid_dup = gf_strdup (brickid);
        if (!brickid_dup)
                goto out;

        volid_str = brickid_dup;
        brick = strchr (brickid_dup, ':');
        *brick = '\0';
        brick++;
        if (!volid_str || !brick)
                goto out;

        uuid_parse (volid_str, volid);
        ret = glusterd_volinfo_find_by_volume_id (volid, &volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo,
                                                      brickinfo);
        if (ret)
                goto out;

        ret = 0;
out:
        GF_FREE (brickid_dup);
        return ret;
}

int
__glusterd_brick_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event, void *data)
{
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        int                     ret = 0;
        char                    *brickid = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;

        brickid = mydata;
        if (!brickid)
                return 0;

        ret = get_brickinfo_from_brickid (brickid, &brickinfo);
        if (ret)
                return 0;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        switch (event) {
        case RPC_CLNT_CONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "Connected to %s:%s",
                        brickinfo->hostname, brickinfo->path);
                glusterd_set_brick_status (brickinfo, GF_BRICK_STARTED);
                ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

                break;

        case RPC_CLNT_DISCONNECT:
                if (GF_BRICK_STARTED == brickinfo->status)
                        gf_log (this->name, GF_LOG_INFO, "Disconnected from "
                                "%s:%s", brickinfo->hostname, brickinfo->path);

                glusterd_set_brick_status (brickinfo, GF_BRICK_STOPPED);
                break;

        case RPC_CLNT_DESTROY:
                GF_FREE (mydata);
                mydata = NULL;
                break;
        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                break;
        }

        return ret;
}

int
glusterd_brick_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                           rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify (rpc, mydata, event, data,
                                           __glusterd_brick_rpc_notify);
}

int
__glusterd_nodesvc_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                               rpc_clnt_event_t event, void *data)
{
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        char                    *server = NULL;
        int                     ret = 0;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        server = mydata;
        if (!server)
                return 0;

        switch (event) {
        case RPC_CLNT_CONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");
                (void) glusterd_nodesvc_set_online_status (server, _gf_true);
                ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

                break;

        case RPC_CLNT_DISCONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT");
                (void) glusterd_nodesvc_set_online_status (server, _gf_false);
                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                break;
        }

        return ret;
}

int
glusterd_nodesvc_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                             rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify (rpc, mydata, event, data,
                                           __glusterd_nodesvc_rpc_notify);
}

int
glusterd_friend_remove_notify (glusterd_peerctx_t *peerctx)
{
        int                             ret = -1;
        glusterd_friend_sm_event_t      *new_event = NULL;
        glusterd_peerinfo_t             *peerinfo = peerctx->peerinfo;
        rpcsvc_request_t                *req = peerctx->args.req;
        char                            *errstr = peerctx->errstr;
        dict_t                          *dict = NULL;

        GF_ASSERT (peerctx);

        peerinfo = peerctx->peerinfo;
        req = peerctx->args.req;
        dict = peerctx->args.dict;
        errstr = peerctx->errstr;

        ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_REMOVE_FRIEND,
                                            &new_event);
        if (!ret) {
                if (!req) {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "Unable to find the request for responding "
                                "to User (%s)", peerinfo->hostname);
                        goto out;
                }

                glusterd_xfer_cli_probe_resp (req, -1, ENOTCONN, errstr,
                                              peerinfo->hostname,
                                              peerinfo->port, dict);

                new_event->peerinfo = peerinfo;
                ret = glusterd_friend_sm_inject_event (new_event);

        } else {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Unable to create event for removing peer %s",
                        peerinfo->hostname);
        }

out:
        return ret;
}

int
__glusterd_peer_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                            rpc_clnt_event_t event, void *data)
{
        xlator_t             *this        = NULL;
        glusterd_conf_t      *conf        = NULL;
        int                   ret         = 0;
        glusterd_peerinfo_t  *peerinfo    = NULL;
        glusterd_peerctx_t   *peerctx     = NULL;
        gf_boolean_t         quorum_action = _gf_false;
        uuid_t               uuid;

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
                peerinfo->quorum_action = _gf_true;

                ret = glusterd_peer_dump_version (this, rpc, peerctx);
                if (ret)
                        gf_log ("", GF_LOG_ERROR, "glusterd handshake failed");
                break;
        }

        case RPC_CLNT_DISCONNECT:
        {
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT %d",
                        peerinfo->state.state);

                if ((peerinfo->quorum_contrib != QUORUM_DOWN) &&
                    (peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED)) {
                        peerinfo->quorum_contrib = QUORUM_DOWN;
                        quorum_action = _gf_true;
                        peerinfo->quorum_action = _gf_false;
                }

                /* Remove peer if it is not a friend and connection/handshake
                *  fails, and notify cli. Happens only during probe.
                */
                if (peerinfo->state.state == GD_FRIEND_STATE_DEFAULT) {
                        glusterd_friend_remove_notify (peerctx);
                        goto out;
                }
                glusterd_get_lock_owner (&uuid);
                if (!uuid_is_null (uuid) &&
                    !uuid_compare (peerinfo->uuid, uuid)) {
                        glusterd_unlock (peerinfo->uuid);
                        if (opinfo.state.state != GD_OP_STATE_DEFAULT)
                                opinfo.state.state = GD_OP_STATE_DEFAULT;
                }

                peerinfo->connected = 0;
                break;
        }
        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();
        if (quorum_action)
                glusterd_do_quorum_action ();
        return ret;
}

int
glusterd_peer_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify (rpc, mydata, event, data,
                                           __glusterd_peer_rpc_notify);
}

int
glusterd_null (rpcsvc_request_t *req)
{

        return 0;
}

rpcsvc_actor_t gd_svc_mgmt_actors[GLUSTERD_MGMT_MAXVALUE] = {
        [GLUSTERD_MGMT_NULL]           = { "NULL",           GLUSTERD_MGMT_NULL,           glusterd_null,                  NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_CLUSTER_LOCK]   = { "CLUSTER_LOCK",   GLUSTERD_MGMT_CLUSTER_LOCK,   glusterd_handle_cluster_lock,   NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_CLUSTER_UNLOCK] = { "CLUSTER_UNLOCK", GLUSTERD_MGMT_CLUSTER_UNLOCK, glusterd_handle_cluster_unlock, NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_STAGE_OP]       = { "STAGE_OP",       GLUSTERD_MGMT_STAGE_OP,       glusterd_handle_stage_op,       NULL, 0, DRC_NA},
        [GLUSTERD_MGMT_COMMIT_OP]      = { "COMMIT_OP",      GLUSTERD_MGMT_COMMIT_OP,      glusterd_handle_commit_op,      NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_mgmt_prog = {
        .progname  = "GlusterD svc mgmt",
        .prognum   = GD_MGMT_PROGRAM,
        .progver   = GD_MGMT_VERSION,
        .numactors = GLUSTERD_MGMT_MAXVALUE,
        .actors    = gd_svc_mgmt_actors,
	.synctask  = _gf_true,
};

rpcsvc_actor_t gd_svc_peer_actors[GLUSTERD_FRIEND_MAXVALUE] = {
        [GLUSTERD_FRIEND_NULL]    = { "NULL",          GLUSTERD_MGMT_NULL,     glusterd_null,                         NULL, 0, DRC_NA},
        [GLUSTERD_PROBE_QUERY]    = { "PROBE_QUERY",   GLUSTERD_PROBE_QUERY,   glusterd_handle_probe_query,           NULL, 0, DRC_NA},
        [GLUSTERD_FRIEND_ADD]     = { "FRIEND_ADD",    GLUSTERD_FRIEND_ADD,    glusterd_handle_incoming_friend_req,   NULL, 0, DRC_NA},
        [GLUSTERD_FRIEND_REMOVE]  = { "FRIEND_REMOVE", GLUSTERD_FRIEND_REMOVE, glusterd_handle_incoming_unfriend_req, NULL, 0, DRC_NA},
        [GLUSTERD_FRIEND_UPDATE]  = { "FRIEND_UPDATE", GLUSTERD_FRIEND_UPDATE, glusterd_handle_friend_update,         NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_peer_prog = {
        .progname  = "GlusterD svc peer",
        .prognum   = GD_FRIEND_PROGRAM,
        .progver   = GD_FRIEND_VERSION,
        .numactors = GLUSTERD_FRIEND_MAXVALUE,
        .actors    = gd_svc_peer_actors,
	.synctask  = _gf_false,
};



rpcsvc_actor_t gd_svc_cli_actors[GLUSTER_CLI_MAXVALUE] = {
        [GLUSTER_CLI_PROBE]              = { "CLI_PROBE",         GLUSTER_CLI_PROBE,            glusterd_handle_cli_probe,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_CREATE_VOLUME]      = { "CLI_CREATE_VOLUME", GLUSTER_CLI_CREATE_VOLUME,    glusterd_handle_create_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_DEFRAG_VOLUME]      = { "CLI_DEFRAG_VOLUME", GLUSTER_CLI_DEFRAG_VOLUME,    glusterd_handle_defrag_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_DEPROBE]            = { "FRIEND_REMOVE",     GLUSTER_CLI_DEPROBE,          glusterd_handle_cli_deprobe,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_LIST_FRIENDS]       = { "LIST_FRIENDS",      GLUSTER_CLI_LIST_FRIENDS,     glusterd_handle_cli_list_friends,      NULL, 0, DRC_NA},
        [GLUSTER_CLI_UUID_RESET]         = { "UUID_RESET",        GLUSTER_CLI_UUID_RESET,       glusterd_handle_cli_uuid_reset,        NULL, 0, DRC_NA},
        [GLUSTER_CLI_UUID_GET]           = { "UUID_GET",          GLUSTER_CLI_UUID_GET,         glusterd_handle_cli_uuid_get,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_START_VOLUME]       = { "START_VOLUME",      GLUSTER_CLI_START_VOLUME,     glusterd_handle_cli_start_volume,      NULL, 0, DRC_NA},
        [GLUSTER_CLI_STOP_VOLUME]        = { "STOP_VOLUME",       GLUSTER_CLI_STOP_VOLUME,      glusterd_handle_cli_stop_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_DELETE_VOLUME]      = { "DELETE_VOLUME",     GLUSTER_CLI_DELETE_VOLUME,    glusterd_handle_cli_delete_volume,     NULL, 0, DRC_NA},
        [GLUSTER_CLI_GET_VOLUME]         = { "GET_VOLUME",        GLUSTER_CLI_GET_VOLUME,       glusterd_handle_cli_get_volume,        NULL, 0, DRC_NA},
        [GLUSTER_CLI_ADD_BRICK]          = { "ADD_BRICK",         GLUSTER_CLI_ADD_BRICK,        glusterd_handle_add_brick,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_REPLACE_BRICK]      = { "REPLACE_BRICK",     GLUSTER_CLI_REPLACE_BRICK,    glusterd_handle_replace_brick,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_REMOVE_BRICK]       = { "REMOVE_BRICK",      GLUSTER_CLI_REMOVE_BRICK,     glusterd_handle_remove_brick,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_LOG_ROTATE]         = { "LOG FILENAME",      GLUSTER_CLI_LOG_ROTATE,       glusterd_handle_log_rotate,            NULL, 0, DRC_NA},
        [GLUSTER_CLI_SET_VOLUME]         = { "SET_VOLUME",        GLUSTER_CLI_SET_VOLUME,       glusterd_handle_set_volume,            NULL, 0, DRC_NA},
        [GLUSTER_CLI_SYNC_VOLUME]        = { "SYNC_VOLUME",       GLUSTER_CLI_SYNC_VOLUME,      glusterd_handle_sync_volume,           NULL, 0, DRC_NA},
        [GLUSTER_CLI_RESET_VOLUME]       = { "RESET_VOLUME",      GLUSTER_CLI_RESET_VOLUME,     glusterd_handle_reset_volume,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_FSM_LOG]            = { "FSM_LOG",           GLUSTER_CLI_FSM_LOG,          glusterd_handle_fsm_log,               NULL, 0, DRC_NA},
        [GLUSTER_CLI_GSYNC_SET]          = { "GSYNC_SET",         GLUSTER_CLI_GSYNC_SET,        glusterd_handle_gsync_set,             NULL, 0, DRC_NA},
        [GLUSTER_CLI_PROFILE_VOLUME]     = { "STATS_VOLUME",      GLUSTER_CLI_PROFILE_VOLUME,   glusterd_handle_cli_profile_volume,    NULL, 0, DRC_NA},
        [GLUSTER_CLI_QUOTA]              = { "QUOTA",             GLUSTER_CLI_QUOTA,            glusterd_handle_quota,                 NULL, 0, DRC_NA},
        [GLUSTER_CLI_GETWD]              = { "GETWD",             GLUSTER_CLI_GETWD,            glusterd_handle_getwd,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_STATUS_VOLUME]      = {"STATUS_VOLUME",      GLUSTER_CLI_STATUS_VOLUME,    glusterd_handle_status_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_MOUNT]              = { "MOUNT",             GLUSTER_CLI_MOUNT,            glusterd_handle_mount,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_UMOUNT]             = { "UMOUNT",            GLUSTER_CLI_UMOUNT,           glusterd_handle_umount,                NULL, 1, DRC_NA},
        [GLUSTER_CLI_HEAL_VOLUME]        = { "HEAL_VOLUME",       GLUSTER_CLI_HEAL_VOLUME,      glusterd_handle_cli_heal_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_STATEDUMP_VOLUME]   = {"STATEDUMP_VOLUME",   GLUSTER_CLI_STATEDUMP_VOLUME, glusterd_handle_cli_statedump_volume,  NULL, 0, DRC_NA},
        [GLUSTER_CLI_LIST_VOLUME]        = {"LIST_VOLUME",        GLUSTER_CLI_LIST_VOLUME,      glusterd_handle_cli_list_volume,       NULL, 0, DRC_NA},
        [GLUSTER_CLI_CLRLOCKS_VOLUME]    = {"CLEARLOCKS_VOLUME",  GLUSTER_CLI_CLRLOCKS_VOLUME,  glusterd_handle_cli_clearlocks_volume, NULL, 0, DRC_NA},
        [GLUSTER_CLI_COPY_FILE]     = {"COPY_FILE", GLUSTER_CLI_COPY_FILE, glusterd_handle_copy_file, NULL, 0, DRC_NA},
        [GLUSTER_CLI_SYS_EXEC]      = {"SYS_EXEC", GLUSTER_CLI_SYS_EXEC, glusterd_handle_sys_exec, NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_cli_prog = {
        .progname  = "GlusterD svc cli",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numactors = GLUSTER_CLI_MAXVALUE,
        .actors    = gd_svc_cli_actors,
	.synctask  = _gf_true,
};

/* This is a minimal RPC prog, which contains only the readonly RPC procs from
 * the cli rpcsvc
 */
rpcsvc_actor_t gd_svc_cli_actors_ro[GLUSTER_CLI_MAXVALUE] = {
        [GLUSTER_CLI_LIST_FRIENDS]       = { "LIST_FRIENDS",      GLUSTER_CLI_LIST_FRIENDS,     glusterd_handle_cli_list_friends,      NULL, 0, DRC_NA},
        [GLUSTER_CLI_UUID_GET]           = { "UUID_GET",          GLUSTER_CLI_UUID_GET,         glusterd_handle_cli_uuid_get,          NULL, 0, DRC_NA},
        [GLUSTER_CLI_GET_VOLUME]         = { "GET_VOLUME",        GLUSTER_CLI_GET_VOLUME,       glusterd_handle_cli_get_volume,        NULL, 0, DRC_NA},
        [GLUSTER_CLI_GETWD]              = { "GETWD",             GLUSTER_CLI_GETWD,            glusterd_handle_getwd,                 NULL, 1, DRC_NA},
        [GLUSTER_CLI_STATUS_VOLUME]      = {"STATUS_VOLUME",      GLUSTER_CLI_STATUS_VOLUME,    glusterd_handle_status_volume,         NULL, 0, DRC_NA},
        [GLUSTER_CLI_LIST_VOLUME]        = {"LIST_VOLUME",        GLUSTER_CLI_LIST_VOLUME,      glusterd_handle_cli_list_volume,       NULL, 0, DRC_NA},
};

struct rpcsvc_program gd_svc_cli_prog_ro = {
        .progname  = "GlusterD svc cli read-only",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numactors = GLUSTER_CLI_MAXVALUE,
        .actors    = gd_svc_cli_actors_ro,
	.synctask  = _gf_true,
};
