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
#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include "uuid.h"

#include "fnmatch.h"
#include "xlator.h"
#include "protocol-common.h"
#include "glusterd.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"

static struct list_head gd_friend_sm_queue;

static  char *glusterd_friend_sm_state_names[] = {
        "Establishing Connection",
        "Probe Sent to Peer",
        "Probe Received from Peer",
        "Peer in Cluster",
        "Accepted peer request",
        "Sent and Received peer request",
        "Peer Rejected",
        "Peer detach in progress",
        "Probe Received from peer",
        "Connected to Peer",
        "Peer is connected and Accepted",
        "Invalid State"
};

static char *glusterd_friend_sm_event_names[] = {
        "GD_FRIEND_EVENT_NONE",
        "GD_FRIEND_EVENT_PROBE",
        "GD_FRIEND_EVENT_INIT_FRIEND_REQ",
        "GD_FRIEND_EVENT_RCVD_ACC",
        "GD_FRIEND_EVENT_LOCAL_ACC",
        "GD_FRIEND_EVENT_RCVD_RJT",
        "GD_FRIEND_EVENT_LOCAL_RJT",
        "GD_FRIEND_EVENT_RCVD_FRIEND_REQ",
        "GD_FRIEND_EVENT_INIT_REMOVE_FRIEND",
        "GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND",
        "GD_FRIEND_EVENT_REMOVE_FRIEND",
        "GD_FRIEND_EVENT_CONNECTED",
        "GD_FRIEND_EVENT_MAX"
};

char*
glusterd_friend_sm_state_name_get (int state)
{
        if (state < 0 || state >= GD_FRIEND_STATE_MAX)
                return glusterd_friend_sm_state_names[GD_FRIEND_STATE_MAX];
        return glusterd_friend_sm_state_names[state];
}

char*
glusterd_friend_sm_event_name_get (int event)
{
        if (event < 0 || event >= GD_FRIEND_EVENT_MAX)
                return glusterd_friend_sm_event_names[GD_FRIEND_EVENT_MAX];
        return glusterd_friend_sm_event_names[event];
}

void
glusterd_destroy_probe_ctx (glusterd_probe_ctx_t *ctx)
{
        if (!ctx)
                return;

        GF_FREE (ctx->hostname);
        GF_FREE (ctx);
}

void
glusterd_destroy_friend_req_ctx (glusterd_friend_req_ctx_t *ctx)
{
        if (!ctx)
                return;

        if (ctx->vols)
                dict_unref (ctx->vols);
        GF_FREE (ctx->hostname);
        GF_FREE (ctx);
}

void
glusterd_destroy_friend_update_ctx (glusterd_friend_update_ctx_t *ctx)
{
        if (!ctx)
                return;
        GF_FREE (ctx->hostname);
        GF_FREE (ctx);
}

int
glusterd_broadcast_friend_delete (char *hostname, uuid_t uuid)
{
        int                             ret = 0;
        rpc_clnt_procedure_t            *proc = NULL;
        xlator_t                        *this = NULL;
        glusterd_friend_update_ctx_t    ctx = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_conf_t                 *priv = NULL;
        dict_t                          *friends = NULL;
        char                            key[100] = {0,};
        int32_t                         count = 0;

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv);

        ctx.hostname = hostname;
        ctx.op = GD_FRIEND_UPDATE_DEL;

        friends = dict_new ();
        if (!friends)
                goto out;

        snprintf (key, sizeof (key), "op");
        ret = dict_set_int32 (friends, key, ctx.op);
        if (ret)
                goto out;

        snprintf (key, sizeof (key), "hostname");
        ret = dict_set_str (friends, key, hostname);
        if (ret)
                goto out;

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                if (!peerinfo->connected || !peerinfo->peer)
                        continue;

                ret = dict_set_static_ptr (friends, "peerinfo", peerinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "failed to set peerinfo");
                        goto out;
                }

                proc = &peerinfo->peer->proctable[GLUSTERD_FRIEND_UPDATE];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, friends);
                }
        }

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

out:
        if (friends)
                dict_unref (friends);

        return ret;
}


static int
glusterd_ac_none (glusterd_friend_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_error (glusterd_friend_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_log ("", GF_LOG_ERROR, "Received event %d ", event->event);

        return ret;
}

static int
glusterd_ac_reverse_probe_begin (glusterd_friend_sm_event_t *event, void *ctx)
{
        int ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *new_event = NULL;
        glusterd_probe_ctx_t            *new_ev_ctx = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);

        peerinfo = event->peerinfo;
        ret = glusterd_friend_sm_new_event
                (GD_FRIEND_EVENT_PROBE, &new_event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to get new new_event");
                ret = -1;
                goto out;
        }

        new_ev_ctx = GF_CALLOC (1, sizeof(*new_ev_ctx), gf_gld_mt_probe_ctx_t);

        if (!new_ev_ctx) {
                ret = -1;
                goto out;
        }

        new_ev_ctx->hostname = gf_strdup (peerinfo->hostname);
        new_ev_ctx->port = peerinfo->port;
        new_ev_ctx->req = NULL;

        new_event->peerinfo = peerinfo;
        new_event->ctx = new_ev_ctx;

        ret = glusterd_friend_sm_inject_event (new_event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject new_event %d, "
                                "ret = %d", new_event->event, ret);
        }

out:
        if (ret) {
                GF_FREE (new_event);
                GF_FREE (new_ev_ctx->hostname);
                GF_FREE (new_ev_ctx);
        }
        gf_log ("", GF_LOG_DEBUG, "returning with %d", ret);
        return ret;
}

static int
glusterd_ac_friend_add (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_peerinfo_t     *peerinfo = NULL;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        glusterd_conf_t         *conf = NULL;
        xlator_t                *this = NULL;

        GF_ASSERT (event);
        peerinfo = event->peerinfo;

        this = THIS;
        conf = this->private;

        GF_ASSERT (conf);

        if (!peerinfo->peer)
                goto out;
        proc = &peerinfo->peer->proctable[GLUSTERD_FRIEND_ADD];
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                frame->local = ctx;
                ret = proc->fn (frame, this, event);
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_friend_probe (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        glusterd_conf_t         *conf = NULL;
        xlator_t                *this = NULL;
        glusterd_probe_ctx_t    *probe_ctx = NULL;
        glusterd_peerinfo_t     *peerinfo = NULL;
        dict_t                  *dict = NULL;

        GF_ASSERT (ctx);

        probe_ctx = ctx;

        this = THIS;

        GF_ASSERT (this);

        conf = this->private;

        GF_ASSERT (conf);

        ret = glusterd_friend_find (NULL, probe_ctx->hostname, &peerinfo);
        if (ret) {
                //We should not reach this state ideally
                GF_ASSERT (0);
                goto out;
        }

        if (!peerinfo->peer)
                goto out;
        proc = &peerinfo->peer->proctable[GLUSTERD_PROBE_QUERY];
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                frame->local = ctx;
                dict = dict_new ();
                if (!dict)
                        goto out;
                ret = dict_set_str (dict, "hostname", probe_ctx->hostname);
                if (ret)
                        goto out;

                ret = dict_set_int32 (dict, "port", probe_ctx->port);
                if (ret)
                        goto out;

                ret = dict_set_static_ptr (dict, "peerinfo", peerinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "failed to set peerinfo");
                        goto out;
                }

                ret = proc->fn (frame, this, dict);
                if (ret)
                        goto out;

        }


out:
        if (dict)
                dict_unref (dict);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_send_friend_remove_req (glusterd_friend_sm_event_t *event,
                                    void *data)
{
        int                     ret = 0;
        glusterd_peerinfo_t     *peerinfo = NULL;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        glusterd_conf_t         *conf = NULL;
        xlator_t                *this = NULL;
        glusterd_friend_sm_event_type_t event_type = GD_FRIEND_EVENT_NONE;
        glusterd_probe_ctx_t            *ctx = NULL;
        glusterd_friend_sm_event_t      *new_event = NULL;

        GF_ASSERT (event);
        peerinfo = event->peerinfo;

        this = THIS;
        conf = this->private;

        GF_ASSERT (conf);

        ctx = event->ctx;

        if (!peerinfo->connected) {
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
                                                              NULL,
                                                              ctx->hostname,
                                                              ctx->dict);
                glusterd_friend_sm ();
                glusterd_op_sm ();

                if (ctx) {
                        glusterd_broadcast_friend_delete (ctx->hostname, NULL);
                        glusterd_destroy_probe_ctx (ctx);
                }
                goto out;
        }

        if (!peerinfo->peer)
                goto out;
        proc = &peerinfo->peer->proctable[GLUSTERD_FRIEND_REMOVE];
        if (proc->fn) {
                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        goto out;
                }
                frame->local = data;
                ret = proc->fn (frame, this, event);
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static gf_boolean_t
glusterd_should_update_peer (glusterd_peerinfo_t *peerinfo,
                             glusterd_peerinfo_t *cur_peerinfo)
{
        gf_boolean_t is_valid = _gf_false;

        if ((peerinfo == cur_peerinfo) ||
            (peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED))
                is_valid = _gf_true;

        return is_valid;
}

static int
glusterd_ac_send_friend_update (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                           ret               = 0;
        glusterd_peerinfo_t          *cur_peerinfo      = NULL;
        glusterd_peerinfo_t          *peerinfo          = NULL;
        rpc_clnt_procedure_t         *proc              = NULL;
        xlator_t                     *this              = NULL;
        glusterd_friend_update_ctx_t  ev_ctx            = {{0}};
        glusterd_conf_t              *priv              = NULL;
        dict_t                       *friends           = NULL;
        char                          key[100]          = {0,};
        char                         *dup_buf           = NULL;
        int32_t                       count             = 0;

        GF_ASSERT (event);
        cur_peerinfo = event->peerinfo;

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv);

        ev_ctx.op = GD_FRIEND_UPDATE_ADD;

        friends = dict_new ();
        if (!friends)
                goto out;

        snprintf (key, sizeof (key), "op");
        ret = dict_set_int32 (friends, key, ev_ctx.op);
        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                if (!glusterd_should_update_peer (peerinfo, cur_peerinfo))
                        continue;

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
                gf_log ("", GF_LOG_INFO, "Added uuid: %s, host: %s",
                        dup_buf, peerinfo->hostname);
        }

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                if (!peerinfo->connected || !peerinfo->peer)
                        continue;

                if (!glusterd_should_update_peer (peerinfo, cur_peerinfo))
                        continue;

                ret = dict_set_static_ptr (friends, "peerinfo", peerinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "failed to set peerinfo");
                        goto out;
                }

                proc = &peerinfo->peer->proctable[GLUSTERD_FRIEND_UPDATE];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, friends);
                }
        }

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

out:
        if (friends)
                dict_unref (friends);

        return ret;
}

static int
glusterd_peer_detach_cleanup (glusterd_conf_t *priv)
{
        int                     ret = -1;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_volinfo_t      *tmp_volinfo = NULL;

        GF_ASSERT (priv);

        list_for_each_entry_safe (volinfo,tmp_volinfo,
                                  &priv->volumes, vol_list) {
                if (!glusterd_friend_contains_vol_bricks (volinfo,
                                                          MY_UUID)) {
                        gf_log (THIS->name, GF_LOG_INFO,
                                "Deleting stale volume %s", volinfo->volname);
                        ret = glusterd_delete_volume (volinfo);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Error deleting stale volume");
                                goto out;
                        }
                }
        }
        ret = 0;
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_ac_handle_friend_remove_req (glusterd_friend_sm_event_t *event,
                                      void *ctx)
{
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_req_ctx_t       *ev_ctx = NULL;
        glusterd_friend_sm_event_t      *new_event = NULL;
        glusterd_conf_t                 *priv = NULL;

        GF_ASSERT (ctx);
        ev_ctx = ctx;
        peerinfo = event->peerinfo;
        GF_ASSERT (peerinfo);

        priv = THIS->private;
        GF_ASSERT (priv);

        ret = glusterd_xfer_friend_remove_resp (ev_ctx->req, ev_ctx->hostname,
                                                ev_ctx->port);

        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {

                ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_REMOVE_FRIEND,
                                                    &new_event);
                if (ret)
                        goto out;

                new_event->peerinfo = peerinfo;

                ret = glusterd_friend_sm_inject_event (new_event);
                if (ret)
                        goto out;
        }
        ret = glusterd_peer_detach_cleanup (priv);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "Peer detach cleanup was not successful");
                ret = 0;
        }
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_friend_remove (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                     ret = -1;

        ret = glusterd_friend_remove_cleanup_vols (event->peerinfo->uuid);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING, "Volumes cleanup failed");

        ret = glusterd_friend_cleanup (event->peerinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Cleanup returned: %d", ret);
        }

        return 0;
}

/*static int
glusterd_ac_none (void *ctx)
{
        int ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}*/

static int
glusterd_ac_handle_friend_add_req (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                             ret = 0;
        uuid_t                          uuid;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_req_ctx_t       *ev_ctx = NULL;
        glusterd_friend_update_ctx_t    *new_ev_ctx = NULL;
        glusterd_friend_sm_event_t      *new_event = NULL;
        glusterd_friend_sm_event_type_t event_type = GD_FRIEND_EVENT_NONE;
        int                             status = 0;
        int32_t                         op_ret = -1;
        int32_t                         op_errno = 0;

        GF_ASSERT (ctx);
        ev_ctx = ctx;
        uuid_copy (uuid, ev_ctx->uuid);
        peerinfo = event->peerinfo;
        GF_ASSERT (peerinfo);
        uuid_copy (peerinfo->uuid, ev_ctx->uuid);

        //Build comparison logic here.
        ret = glusterd_compare_friend_data (ev_ctx->vols, &status,
                                            peerinfo->hostname);
        if (ret)
                goto out;

        if (GLUSTERD_VOL_COMP_RJT != status) {
                event_type = GD_FRIEND_EVENT_LOCAL_ACC;
                op_ret = 0;
        } else {
                event_type = GD_FRIEND_EVENT_LOCAL_RJT;
                op_errno = GF_PROBE_VOLUME_CONFLICT;
                op_ret = -1;
        }

        ret = glusterd_friend_sm_new_event (event_type, &new_event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Out of Memory");
        }

        new_event->peerinfo = peerinfo;

        new_ev_ctx = GF_CALLOC (1, sizeof (*new_ev_ctx),
                                gf_gld_mt_friend_update_ctx_t);
        if (!new_ev_ctx) {
                ret = -1;
                goto out;
        }

        uuid_copy (new_ev_ctx->uuid, ev_ctx->uuid);
        new_ev_ctx->hostname = gf_strdup (ev_ctx->hostname);
        new_ev_ctx->op = GD_FRIEND_UPDATE_ADD;

        new_event->ctx = new_ev_ctx;

        glusterd_friend_sm_inject_event (new_event);

        ret = glusterd_xfer_friend_add_resp (ev_ctx->req, ev_ctx->hostname,
                                             peerinfo->hostname, ev_ctx->port,
                                             op_ret, op_errno);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_friend_sm_transition_state (glusterd_peerinfo_t *peerinfo,
                                     glusterd_sm_t *state,
                                     glusterd_friend_sm_event_type_t event_type)
{

        GF_ASSERT (state);
        GF_ASSERT (peerinfo);

        (void) glusterd_sm_tr_log_transition_add (&peerinfo->sm_log,
                                           peerinfo->state.state,
                                           state[event_type].next_state,
                                           event_type);

        peerinfo->state.state = state[event_type].next_state;
        return 0;
}


glusterd_sm_t glusterd_state_default [] = {
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none},
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_probe},//EV_PROBE
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_friend_add}, //EV_INIT_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EV_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_probe}, //EVENT_CONNECTED
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_MAX
};

glusterd_sm_t glusterd_state_probe_rcvd [] = {
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none},
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none}, //EV_PROBE
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none}, //EV_INIT_FRIEND_REQ
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_PROBE_RCVD, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EV_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EVENT_CONNECTED
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_MAX
};

glusterd_sm_t glusterd_state_connected_rcvd [] = {
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none},
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EV_PROBE
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EV_INIT_FRIEND_REQ
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_reverse_probe_begin}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EV_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EVENT_CONNECTED
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EVENT_MAX
};

glusterd_sm_t glusterd_state_connected_accepted [] = {
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none},
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_friend_probe}, //EV_PROBE
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_friend_add}, //EV_INIT_FRIEND_REQ
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EV_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_CONNECTED
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_sent [] = {
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_CONNECTED
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_rcvd [] = {
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none},//EVENT_CONNECTED
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_befriended [] = {
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_send_friend_update}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_friend_add},//EVENT_CONNECTED
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_sent_rcvd [] = {
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_send_friend_update}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none},//EVENT_CONNECTED
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_rejected [] = {
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_friend_probe}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_friend_add}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_friend_add},//EVENT_CONNECTED
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_accepted [] = {
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_send_friend_update}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_send_friend_update}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_reverse_probe_begin},//EVENT_CONNECTED
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_unfriend_sent [] = {
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_error}, //EVENT_PROBE,
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_error}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_error}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_error}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none}, //EVENT_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none},//EVENT_CONNECTED
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t *glusterd_friend_state_table [] = {
        glusterd_state_default,
        glusterd_state_req_sent,
        glusterd_state_req_rcvd,
        glusterd_state_befriended,
        glusterd_state_req_accepted,
        glusterd_state_req_sent_rcvd,
        glusterd_state_rejected,
        glusterd_state_unfriend_sent,
        glusterd_state_probe_rcvd,
        glusterd_state_connected_rcvd,
        glusterd_state_connected_accepted
};

int
glusterd_friend_sm_new_event (glusterd_friend_sm_event_type_t event_type,
                              glusterd_friend_sm_event_t **new_event)
{
        glusterd_friend_sm_event_t      *event = NULL;

        GF_ASSERT (new_event);
        GF_ASSERT (GD_FRIEND_EVENT_NONE <= event_type &&
                        GD_FRIEND_EVENT_MAX > event_type);

        event = GF_CALLOC (1, sizeof (*event), gf_gld_mt_friend_sm_event_t);

        if (!event)
                return -1;

        *new_event = event;
        event->event = event_type;
        INIT_LIST_HEAD (&event->list);

        return 0;
}

int
glusterd_friend_sm_inject_event (glusterd_friend_sm_event_t *event)
{
        GF_ASSERT (event);
        gf_log ("glusterd", GF_LOG_DEBUG, "Enqueue event: '%s'",
                glusterd_friend_sm_event_name_get (event->event));
        list_add_tail (&event->list, &gd_friend_sm_queue);

        return 0;
}

void
glusterd_destroy_friend_event_context (glusterd_friend_sm_event_t *event)
{
        if (!event)
                return;

        switch (event->event) {
        case GD_FRIEND_EVENT_RCVD_FRIEND_REQ:
        case GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND:
                glusterd_destroy_friend_req_ctx (event->ctx);
                break;
        case GD_FRIEND_EVENT_LOCAL_ACC:
        case GD_FRIEND_EVENT_LOCAL_RJT:
        case GD_FRIEND_EVENT_RCVD_ACC:
        case GD_FRIEND_EVENT_RCVD_RJT:
                glusterd_destroy_friend_update_ctx (event->ctx);
                break;
        default:
                break;
        }
}

gf_boolean_t
gd_does_peer_affect_quorum (glusterd_friend_sm_state_t old_state,
                            glusterd_friend_sm_event_type_t event_type,
                            glusterd_peerinfo_t *peerinfo)
{
        gf_boolean_t    affects = _gf_false;

        //When glusterd comes up with friends in BEFRIENDED state in store,
        //wait until compare-data happens.
        if ((old_state == GD_FRIEND_STATE_BEFRIENDED) &&
            (event_type != GD_FRIEND_EVENT_RCVD_ACC) &&
            (event_type != GD_FRIEND_EVENT_LOCAL_ACC))
                goto out;
        if ((peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED)
            && peerinfo->connected) {
                affects = _gf_true;
        }
out:
        return affects;
}

int
glusterd_friend_sm ()
{
        glusterd_friend_sm_event_t      *event      = NULL;
        glusterd_friend_sm_event_t      *tmp        = NULL;
        int                              ret        = -1;
        glusterd_friend_sm_ac_fn         handler    = NULL;
        glusterd_sm_t                   *state      = NULL;
        glusterd_peerinfo_t             *peerinfo   = NULL;
        glusterd_friend_sm_event_type_t  event_type = 0;
        gf_boolean_t                     is_await_conn = _gf_false;
        gf_boolean_t                     quorum_action = _gf_false;
        glusterd_friend_sm_state_t       old_state = GD_FRIEND_STATE_DEFAULT;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        while (!list_empty (&gd_friend_sm_queue)) {
                list_for_each_entry_safe (event, tmp, &gd_friend_sm_queue, list) {

                        list_del_init (&event->list);
                        event_type = event->event;
                        peerinfo = event->peerinfo;
                        if (!peerinfo) {
                                gf_log ("glusterd", GF_LOG_CRITICAL, "Received"
                                        " event %s with empty peer info",
                                glusterd_friend_sm_event_name_get (event_type));

                                GF_FREE (event);
                                continue;
                        }
                        gf_log ("", GF_LOG_DEBUG, "Dequeued event of type: '%s'",
                                glusterd_friend_sm_event_name_get (event_type));


                        old_state = peerinfo->state.state;
                        state = glusterd_friend_state_table[peerinfo->state.state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);

                        ret = handler (event, event->ctx);
                        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                                is_await_conn = _gf_true;
                                ret = 0;
                        }

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, "handler returned: "
                                                "%d", ret);
                                glusterd_destroy_friend_event_context (event);
                                GF_FREE (event);
                                continue;
                        }

                        if ((GD_FRIEND_EVENT_REMOVE_FRIEND == event_type) ||
                           (GD_FRIEND_EVENT_INIT_REMOVE_FRIEND == event_type)){
                                glusterd_destroy_friend_event_context (event);
                                GF_FREE (event);
                                continue;
                        }

                        ret = glusterd_friend_sm_transition_state (peerinfo,
                                                            state, event_type);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, "Unable to transition"
                                        " state from '%s' to '%s' for event '%s'",
                        glusterd_friend_sm_state_name_get(peerinfo->state.state),
                        glusterd_friend_sm_state_name_get(state[event_type].next_state),
                                glusterd_friend_sm_event_name_get(event_type));
                                goto out;
                        }

                        if (gd_does_peer_affect_quorum (old_state, event_type,
                                                        peerinfo)) {
                                peerinfo->quorum_contrib = QUORUM_UP;
                                if (peerinfo->quorum_action) {
                                        peerinfo->quorum_action = _gf_false;
                                        quorum_action = _gf_true;
                                }
                        }

                        ret = glusterd_store_peerinfo (peerinfo);

                        glusterd_destroy_friend_event_context (event);
                        GF_FREE (event);
                        if (is_await_conn)
                                break;
                }
                if (is_await_conn)
                        break;
        }

        ret = 0;
out:
        if (quorum_action) {
            /* When glusterd is restarted, it needs to wait until the 'friends' view
             * of the volumes settle, before it starts any of the internal daemons.
             *
             * Every friend that was part of the cluster, would send its
             * cluster-view, 'our' way. For every friend, who belongs to
             * a partition which has a different cluster-view from our
             * partition, we may update our cluster-view. For subsequent
             * friends from that partition would agree with us, if the first
             * friend wasn't rejected. For every first friend, whom we agreed with,
             * we would need to start internal daemons/bricks belonging to the
             * new volumes.
             * glusterd_spawn_daemons calls functions that are idempotent. ie,
             * the functions spawn process(es) only if they are not started yet.
             *
             * */
                synclock_unlock (&priv->big_lock);
                glusterd_launch_synctask (glusterd_spawn_daemons, NULL);
                synclock_lock (&priv->big_lock);
                glusterd_do_quorum_action ();
        }
        return ret;
}


int
glusterd_friend_sm_init ()
{
        INIT_LIST_HEAD (&gd_friend_sm_queue);
        return 0;
}
