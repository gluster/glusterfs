/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include "compat-uuid.h"

#include "fnmatch.h"
#include "xlator.h"
#include "protocol-common.h"
#include "glusterd.h"
#include "call-stub.h"
#include "defaults.h"
#include "list.h"
#include "glusterd-messages.h"
#include "dict.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "glusterd-svc-helper.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-server-quorum.h"

char local_node_hostname[PATH_MAX] = {0, };

static struct cds_list_head gd_friend_sm_queue;

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
        "GD_FRIEND_EVENT_NEW_NAME",
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

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {
                if (!peerinfo->connected || !peerinfo->peer)
                        continue;

                /* Setting a direct reference to peerinfo in the dict is okay as
                 * it is only going to be used within this read critical section
                 * (in glusterd_rpc_friend_update)
                 */
                ret = dict_set_static_ptr (friends, "peerinfo", peerinfo);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set peerinfo");
                        goto unlock;
                }

                proc = &peerinfo->peer->proctable[GLUSTERD_FRIEND_UPDATE];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, friends);
                }
        }
unlock:
        rcu_read_unlock ();

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);

out:
        if (friends)
                dict_unref (friends);

        return ret;
}


static int
glusterd_ac_none (glusterd_friend_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_error (glusterd_friend_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                GD_MSG_AC_ERROR, "Received event %d ", event->event);

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

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                ret = -1;
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                (GD_FRIEND_EVENT_PROBE, &new_event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL,
                        "Unable to get new new_event");
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

        new_event->peername = gf_strdup (peerinfo->hostname);
        gf_uuid_copy (new_event->peerid, peerinfo->uuid);
        new_event->ctx = new_ev_ctx;

        ret = glusterd_friend_sm_inject_event (new_event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL,
                        "Unable to inject new_event %d, "
                        "ret = %d", new_event->event, ret);
        }

out:
        rcu_read_unlock ();

        if (ret) {
                if (new_event)
                        GF_FREE (new_event->peername);
                GF_FREE (new_event);
                if (new_ev_ctx)
                        GF_FREE (new_ev_ctx->hostname);
                GF_FREE (new_ev_ctx);
        }
        gf_msg_debug ("glusterd", 0, "returning with %d", ret);
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

        this = THIS;
        conf = this->private;

        GF_ASSERT (conf);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND,
                        "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                goto out;
        }

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
        rcu_read_unlock ();

        if (ret && frame)
                STACK_DESTROY (frame->root);

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
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

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (NULL, probe_ctx->hostname);
        if (peerinfo == NULL) {
                //We should not reach this state ideally
                ret = -1;
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

                /* The peerinfo reference being set here is going to be used
                 * only within this critical section, in glusterd_rpc_probe
                 * (ie. proc->fn).
                 */
                ret = dict_set_static_ptr (dict, "peerinfo", peerinfo);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set peerinfo");
                        goto out;
                }

                ret = proc->fn (frame, this, dict);
                if (ret)
                        goto out;

        }

out:
        rcu_read_unlock ();

        if (dict)
                dict_unref (dict);
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);

        if (ret && frame)
                STACK_DESTROY (frame->root);

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

        this = THIS;
        conf = this->private;

        GF_ASSERT (conf);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                goto out;
        }
        ctx = event->ctx;

        if (!peerinfo->connected) {
                event_type = GD_FRIEND_EVENT_REMOVE_FRIEND;

                ret = glusterd_friend_sm_new_event (event_type, &new_event);

                if (!ret) {
                        new_event->peername = peerinfo->hostname;
                        gf_uuid_copy (new_event->peerid, peerinfo->uuid);
                        ret = glusterd_friend_sm_inject_event (new_event);
                } else {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_EVENT_NEW_GET_FAIL,
                                "Unable to get event");
                }

                if (ctx) {
                        ret = glusterd_xfer_cli_deprobe_resp (ctx->req, ret, 0,
                                                              NULL,
                                                              ctx->hostname,
                                                              ctx->dict);
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
        rcu_read_unlock ();

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);

        if (ret && frame)
                STACK_DESTROY (frame->root);

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
        int32_t                       count             = 0;

        GF_ASSERT (event);

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv);

        rcu_read_lock ();

        cur_peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!cur_peerinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                ret = -1;
                goto out;
        }

        ev_ctx.op = GD_FRIEND_UPDATE_ADD;

        friends = dict_new ();
        if (!friends)
                goto out;

        snprintf (key, sizeof (key), "op");
        ret = dict_set_int32 (friends, key, ev_ctx.op);
        if (ret)
                goto out;

        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {
                if (!glusterd_should_update_peer (peerinfo, cur_peerinfo))
                        continue;

                count++;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d", count);
                ret = gd_add_friend_to_dict (peerinfo, friends, key);
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;

        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {
                if (!peerinfo->connected || !peerinfo->peer)
                        continue;

                if (!glusterd_should_update_peer (peerinfo, cur_peerinfo))
                        continue;

                ret = dict_set_static_ptr (friends, "peerinfo", peerinfo);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "failed to set peerinfo");
                        goto out;
                }

                proc = &peerinfo->peer->proctable[GLUSTERD_FRIEND_UPDATE];
                if (proc->fn) {
                        ret = proc->fn (NULL, this, friends);
                }
        }

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);

out:
        rcu_read_unlock ();

        if (friends)
                dict_unref (friends);

        return ret;
}

/* ac_update_friend only sends friend update to the friend that caused this
 * event to happen
 */
static int
glusterd_ac_update_friend (glusterd_friend_sm_event_t *event, void *ctx)
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
        int32_t                       count             = 0;

        GF_ASSERT (event);

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv);

        rcu_read_lock ();

        cur_peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!cur_peerinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                ret = -1;
                goto out;
        }

        /* Bail out early if peer is not connected.
         * We cannot send requests to the peer until we have established our
         * client connection to it.
         */
        if (!cur_peerinfo->connected || !cur_peerinfo->peer) {
                ret = 0;
                goto out;
        }

        ev_ctx.op = GD_FRIEND_UPDATE_ADD;

        friends = dict_new ();
        if (!friends)
                goto out;

        snprintf (key, sizeof (key), "op");
        ret = dict_set_int32 (friends, key, ev_ctx.op);
        if (ret)
                goto out;

        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {
                if (!glusterd_should_update_peer (peerinfo, cur_peerinfo))
                        continue;

                count++;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d", count);
                ret = gd_add_friend_to_dict (peerinfo, friends, key);
                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (friends, "count", count);
        if (ret)
                goto out;

        ret = dict_set_static_ptr (friends, "peerinfo", cur_peerinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "failed to set peerinfo");
                goto out;
        }

        proc = &cur_peerinfo->peer->proctable[GLUSTERD_FRIEND_UPDATE];
        if (proc->fn)
                ret = proc->fn (NULL, this, friends);

        gf_msg_debug (this->name, 0, "Returning with %d", ret);

out:
        rcu_read_unlock ();

        if (friends)
                dict_unref (friends);

        return ret;
}

/* Clean up stale volumes on the peer being detached. The volumes which have
 * bricks on other peers are stale with respect to the detached peer.
 */
static void
glusterd_peer_detach_cleanup (glusterd_conf_t *priv)
{
        int                     ret          = -1;
        glusterd_volinfo_t      *volinfo     = NULL;
        glusterd_volinfo_t      *tmp_volinfo = NULL;
        glusterd_svc_t          *svc         = NULL;

        GF_ASSERT (priv);

        cds_list_for_each_entry_safe (volinfo, tmp_volinfo, &priv->volumes,
                                      vol_list) {
                /* The peer detach checks make sure that, at this point in the
                 * detach process, there are only volumes contained completely
                 * within or completely outside the detached peer.
                 * The only stale volumes at this point are the ones
                 * completely outside the peer and can be safely deleted.
                 */
                if (!glusterd_friend_contains_vol_bricks (volinfo,
                                                          MY_UUID)) {
                        gf_msg (THIS->name, GF_LOG_INFO, 0,
                                GD_MSG_STALE_VOL_DELETE_INFO,
                                "Deleting stale volume %s", volinfo->volname);

                        /*Stop snapd daemon service if snapd daemon is running*/
                        if (!volinfo->is_snap_volume) {
                                svc = &(volinfo->snapd.svc);
                                ret = svc->stop (svc, SIGTERM);
                                if (ret) {
                                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SVC_STOP_FAIL, "Failed "
                                                "to stop snapd daemon service");
                                }
                        }

                        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                                svc = &(volinfo->tierd.svc);
                                ret = svc->stop (svc, SIGTERM);
                                if (ret) {
                                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SVC_STOP_FAIL, "Failed "
                                                "to stop tierd daemon service");
                                }
                        }
                        ret = glusterd_cleanup_snaps_for_volume (volinfo);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VOL_DELETE_FAIL,
                                        "Error deleting snapshots for volume %s",
                                        volinfo->volname);
                        }

                        ret = glusterd_delete_volume (volinfo);
                        if (ret) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        GD_MSG_STALE_VOL_REMOVE_FAIL,
                                        "Error deleting stale volume");
                        }
                }
        }

        /*Reconfigure all daemon services upon peer detach*/
        ret = glusterd_svcs_reconfigure ();
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_SVC_STOP_FAIL,
                        "Failed to reconfigure all daemon services.");
        }
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

        priv = THIS->private;
        GF_ASSERT (priv);

        ret = glusterd_xfer_friend_remove_resp (ev_ctx->req, ev_ctx->hostname,
                                                ev_ctx->port);

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {

                ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_REMOVE_FRIEND,
                                                    &new_event);
                if (ret) {
                        rcu_read_unlock ();
                        goto out;
                }

                new_event->peername = gf_strdup (peerinfo->hostname);
                gf_uuid_copy (new_event->peerid, peerinfo->uuid);

                ret = glusterd_friend_sm_inject_event (new_event);
                if (ret) {
                        rcu_read_unlock ();
                        goto out;
                }

                new_event = NULL;
        }
        rcu_read_unlock ();

        glusterd_peer_detach_cleanup (priv);
out:
        if (new_event)
                GF_FREE (new_event->peername);
        GF_FREE (new_event);

        gf_msg_debug (THIS->name, 0, "Returning with %d", ret);
        return ret;
}

static int
glusterd_ac_friend_remove (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                  ret = -1;
        glusterd_peerinfo_t *peerinfo = NULL;

        GF_ASSERT (event);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND,
                        "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                rcu_read_unlock ();
                goto out;
        }
        ret = glusterd_friend_remove_cleanup_vols (peerinfo->uuid);
        if (ret)
                gf_msg (THIS->name, GF_LOG_WARNING, 0, GD_MSG_VOL_CLEANUP_FAIL,
                        "Volumes cleanup failed");

        rcu_read_unlock ();
        /* Exiting read critical section as glusterd_peerinfo_cleanup calls
         * synchronize_rcu before freeing the peerinfo
         */

        ret = glusterd_peerinfo_cleanup (peerinfo);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_DETACH_CLEANUP_FAIL,
                        "Cleanup returned: %d", ret);
        }
out:
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
        glusterd_conf_t                *conf       = NULL;
        int                             status = 0;
        int32_t                         op_ret = -1;
        int32_t                         op_errno = 0;
        xlator_t                       *this       = NULL;
        char                           *hostname   = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (ctx);
        ev_ctx = ctx;
        gf_uuid_copy (uuid, ev_ctx->uuid);

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (event->peerid, event->peername);
        if (!peerinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        event->peername, uuid_utoa (event->peerid));
                ret = -1;
                rcu_read_unlock ();
                goto out;
        }

        /* TODO: How do you do an atomic copy of uuid_t */
        /* TODO: Updating within a read-critical section is also invalid
         *       Update properly with updater synchronization
         */
        gf_uuid_copy (peerinfo->uuid, ev_ctx->uuid);

        rcu_read_unlock ();

        conf = this->private;
        GF_ASSERT (conf);

        /* Passing the peername from the event. glusterd_compare_friend_data
         * updates volumes and will use synchronize_rcu. If we were to pass
         * peerinfo->hostname, we would have to do it under a read critical
         * section which would lead to a deadlock
         */

        //Build comparison logic here.
        ret = glusterd_compare_friend_data (ev_ctx->vols, &status,
                                            event->peername);
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

        /* Compare missed_snapshot list with the peer *
         * if volume comparison is successful */
        if ((op_ret == 0) &&
            (conf->op_version >= GD_OP_VERSION_3_6_0)) {
                ret = glusterd_import_friend_missed_snap_list (ev_ctx->vols);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                                "Failed to import peer's "
                                "missed_snaps_list.");
                        event_type = GD_FRIEND_EVENT_LOCAL_RJT;
                        op_errno = GF_PROBE_MISSED_SNAP_CONFLICT;
                        op_ret = -1;
                }

                /* glusterd_compare_friend_snapshots and functions only require
                 * a peers hostname and uuid. It also does updates, which
                 * require use of synchronize_rcu. So we pass the hostname and
                 * id from the event instead of the peerinfo object to prevent
                 * deadlocks as above.
                 */
                ret = glusterd_compare_friend_snapshots (ev_ctx->vols,
                                                         event->peername,
                                                         event->peerid);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_COMPARE_CONFLICT,
                                "Conflict in comparing peer's snapshots");
                        event_type = GD_FRIEND_EVENT_LOCAL_RJT;
                        op_errno = GF_PROBE_SNAP_CONFLICT;
                        op_ret = -1;
                }
        }

        ret = glusterd_friend_sm_new_event (event_type, &new_event);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Out of Memory");
        }

        new_event->peername = gf_strdup (event->peername);
        gf_uuid_copy (new_event->peerid, event->peerid);

        new_ev_ctx = GF_CALLOC (1, sizeof (*new_ev_ctx),
                                gf_gld_mt_friend_update_ctx_t);
        if (!new_ev_ctx) {
                ret = -1;
                goto out;
        }

        gf_uuid_copy (new_ev_ctx->uuid, ev_ctx->uuid);
        new_ev_ctx->hostname = gf_strdup (ev_ctx->hostname);
        new_ev_ctx->op = GD_FRIEND_UPDATE_ADD;

        new_event->ctx = new_ev_ctx;

        ret = dict_get_str (ev_ctx->vols, "hostname_in_cluster",
                            &hostname);
        if (ret || !hostname) {
                gf_msg_debug (this->name, 0,
                        "Unable to fetch local hostname from peer");
        } else
                strncpy (local_node_hostname, hostname,
                         sizeof(local_node_hostname));

        glusterd_friend_sm_inject_event (new_event);
        new_event = NULL;

        ret = glusterd_xfer_friend_add_resp (ev_ctx->req, ev_ctx->hostname,
                                             event->peername, ev_ctx->port,
                                             op_ret, op_errno);

out:
        if (new_event)
                GF_FREE (new_event->peername);
        GF_FREE (new_event);

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

static int
glusterd_friend_sm_transition_state (uuid_t peerid, char *peername,
                                     glusterd_sm_t *state,
                                     glusterd_friend_sm_event_type_t event_type)
{
        int ret = -1;
        glusterd_peerinfo_t *peerinfo = NULL;

        GF_ASSERT (state);
        GF_ASSERT (peername);

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find (peerid, peername);
        if (!peerinfo) {
                goto out;
        }

        (void) glusterd_sm_tr_log_transition_add (&peerinfo->sm_log,
                                           peerinfo->state.state,
                                           state[event_type].next_state,
                                           event_type);

        uatomic_set (&peerinfo->state.state, state[event_type].next_state);

        ret = 0;
out:
        rcu_read_unlock ();
        return ret;
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
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none}, //EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_CONNECTED_ACCEPTED, glusterd_ac_none}, //EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_CONNECTED_RCVD, glusterd_ac_none},//EVENT_NEW_NAME
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_befriended [] = {
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_update_friend}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_update_friend}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_friend_add},//EVENT_CONNECTED
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_send_friend_update},//EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none},//EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none},//EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none},//EVENT_NEW_NAME
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
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_none},//EVENT_NEW_NAME
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
        CDS_INIT_LIST_HEAD (&event->list);

        return 0;
}

int
glusterd_friend_sm_inject_event (glusterd_friend_sm_event_t *event)
{
        GF_ASSERT (event);
        gf_msg_debug ("glusterd", 0, "Enqueue event: '%s'",
                glusterd_friend_sm_event_name_get (event->event));
        cds_list_add_tail (&event->list, &gd_friend_sm_queue);

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

        while (!cds_list_empty (&gd_friend_sm_queue)) {
                cds_list_for_each_entry_safe (event, tmp, &gd_friend_sm_queue,
                                              list) {

                        cds_list_del_init (&event->list);
                        event_type = event->event;

                        rcu_read_lock ();

                        peerinfo = glusterd_peerinfo_find (event->peerid,
                                                           event->peername);
                        if (!peerinfo) {
                                gf_msg ("glusterd", GF_LOG_CRITICAL, 0,
                                        GD_MSG_PEER_NOT_FOUND, "Received"
                                        " event %s with empty peer info",
                                glusterd_friend_sm_event_name_get (event_type));

                                GF_FREE (event);
                                rcu_read_unlock ();
                                continue;
                        }
                        gf_msg_debug ("glusterd", 0, "Dequeued event of type: '%s'",
                                glusterd_friend_sm_event_name_get (event_type));


                        old_state = peerinfo->state.state;

                        rcu_read_unlock ();
                        /* Giving up read-critical section here as we only need
                         * the current state to call the handler.
                         *
                         * We cannot continue into the handler in a read
                         * critical section as there are handlers who do
                         * updates, and could cause deadlocks.
                         */

                        state = glusterd_friend_state_table[old_state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);

                        ret = handler (event, event->ctx);
                        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                                is_await_conn = _gf_true;
                                ret = 0;
                        }

                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_HANDLER_RETURNED,
                                        "handler returned: "
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

                        ret = glusterd_friend_sm_transition_state
                                (event->peerid, event->peername, state,
                                 event_type);

                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_EVENT_STATE_TRANSITION_FAIL,
                                        "Unable to transition"
                                        " state from '%s' to '%s' for event '%s'",
                        glusterd_friend_sm_state_name_get(old_state),
                        glusterd_friend_sm_state_name_get(state[event_type].next_state),
                                glusterd_friend_sm_event_name_get(event_type));
                                goto out;
                        }

                        peerinfo = NULL;
                        /* We need to obtain peerinfo reference once again as we
                         * had exited the read critical section above.
                         */
                        rcu_read_lock ();
                        peerinfo = glusterd_peerinfo_find (event->peerid,
                                        event->peername);
                        if (!peerinfo) {
                                rcu_read_unlock ();
                                /* A peer can only be deleted as a effect of
                                 * this state machine, and two such state
                                 * machines can never run at the same time.
                                 * So if we cannot find the peerinfo here,
                                 * something has gone terribly wrong.
                                 */
                                ret = -1;
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_PEER_NOT_FOUND,
                                        "Cannot find peer %s(%s)",
                                        event->peername, uuid_utoa (event->peerid));
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
                        rcu_read_unlock ();

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
        CDS_INIT_LIST_HEAD (&gd_friend_sm_queue);
        return 0;
}
