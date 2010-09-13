/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is GF_FREE software; you can redistribute it and/or modify
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
#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include <uuid/uuid.h>

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
#include "glusterd-utils.h"
#include "glusterd-store.h"

static struct list_head gd_friend_sm_queue;

void
glusterd_destroy_probe_ctx (glusterd_probe_ctx_t *ctx)
{
        if (!ctx)
                return;

        if (ctx->hostname)
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
        if (ctx->hostname)
                GF_FREE (ctx->hostname);
        GF_FREE (ctx);
}

#define glusterd_destroy_friend_update_ctx(ctx)\
        glusterd_destroy_friend_req_ctx(ctx)

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
        GF_ASSERT (conf->mgmt);

        proc = &conf->mgmt->proctable[GD_MGMT_FRIEND_ADD];
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
        dict_t                  *dict = NULL;

        GF_ASSERT (ctx);

        probe_ctx = ctx;

        this = THIS;

        GF_ASSERT (this);

        conf = this->private;

        GF_ASSERT (conf);
        if (!conf->mgmt)
                goto out;


        proc = &conf->mgmt->proctable[GD_MGMT_PROBE_QUERY];
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
glusterd_ac_send_friend_remove_req (glusterd_friend_sm_event_t *event, void *ctx)
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
        GF_ASSERT (conf->mgmt);

        proc = &conf->mgmt->proctable[GD_MGMT_FRIEND_REMOVE];
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
glusterd_ac_send_friend_update (glusterd_friend_sm_event_t *event, void *ctx)
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
        GF_ASSERT (conf->mgmt);

        proc = &conf->mgmt->proctable[GD_MGMT_FRIEND_UPDATE];
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
glusterd_ac_handle_friend_remove_req (glusterd_friend_sm_event_t *event,
                                      void *ctx)
{
        int                             ret = 0;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_req_ctx_t       *ev_ctx = NULL;
        glusterd_friend_sm_event_t      *new_event = NULL;

        GF_ASSERT (ctx);
        ev_ctx = ctx;
        peerinfo = event->peerinfo;
        GF_ASSERT (peerinfo);

        uuid_clear (peerinfo->uuid);

        ret = glusterd_xfer_friend_remove_resp (ev_ctx->req, ev_ctx->hostname,
                                                ev_ctx->port);

        rpc_clnt_destroy (peerinfo->rpc);
        peerinfo->rpc = NULL;

        ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_REMOVE_FRIEND,
                                            &new_event);

        if (ret)
                goto out;

        new_event->peerinfo = peerinfo;

        ret = glusterd_friend_sm_inject_event (new_event);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_friend_remove (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                     ret = -1;

        ret = glusterd_friend_cleanup (event->peerinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Cleanup returned: %d", ret);
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

        GF_ASSERT (ctx);
        ev_ctx = ctx;
        uuid_copy (uuid, ev_ctx->uuid);
        peerinfo = event->peerinfo;
        GF_ASSERT (peerinfo);
        uuid_copy (peerinfo->uuid, ev_ctx->uuid);

        //Build comparison logic here.
        ret = glusterd_compare_friend_data (ev_ctx->vols, &status);
        if (ret)
                goto out;

        if (GLUSTERD_VOL_COMP_RJT != status) {
                event_type = GD_FRIEND_EVENT_LOCAL_ACC;
                op_ret = 0;
        }
        else {
                event_type = GD_FRIEND_EVENT_LOCAL_RJT;
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

        new_event->ctx = new_ev_ctx;

        glusterd_friend_sm_inject_event (new_event);

        ret = glusterd_xfer_friend_add_resp (ev_ctx->req, ev_ctx->hostname,
                                             ev_ctx->port, op_ret);

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

        //peerinfo->state.state = state;

        gf_log ("", GF_LOG_NORMAL, "Transitioning from %d to %d",
                        peerinfo->state.state, state[event_type].next_state);
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
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_MAX
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
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_rcvd [] = {
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_friend_probe}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_friend_add}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_befriended [] = {
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_UNFRIEND_SENT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND,
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
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
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_rejected [] = {
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_friend_probe}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_friend_add}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_LOCAL_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_send_friend_remove_req}, //EVENT_INIT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_handle_friend_remove_req}, //EVENT_RCVD_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_friend_remove}, //EVENT_REMOVE_FRIEND
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
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_unfriend_sent [] = {
        {GD_FRIEND_STATE_REQ_ACCEPTED, glusterd_ac_none}, //EVENT_NONE,
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
        gf_log ("glusterd", GF_LOG_NORMAL, "Enqueuing event: %d",
                        event->event);
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
                glusterd_destroy_friend_update_ctx(event->ctx);
                break;
        default:
                break;
        }
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
        int                              port       = 6969; //TODO, use standard

        while (!list_empty (&gd_friend_sm_queue)) {
                list_for_each_entry_safe (event, tmp, &gd_friend_sm_queue, list) {

                        list_del_init (&event->list);
                        peerinfo = event->peerinfo;
                        event_type = event->event;

                        if (!peerinfo &&
                           (GD_FRIEND_EVENT_PROBE == event_type ||
                            GD_FRIEND_EVENT_RCVD_FRIEND_REQ == event_type)) {
                                ret = glusterd_friend_add (NULL, port,
                                                          GD_FRIEND_STATE_DEFAULT,
                                                          NULL, NULL, &peerinfo, 0);

                                if (ret) {
                                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to add peer, "
                                                "ret = %d", ret);
                                        continue;
                                }
                                GF_ASSERT (peerinfo);
                                event->peerinfo = peerinfo;
                        }

                        if (!peerinfo)
                                goto out;

                        state = glusterd_friend_state_table[peerinfo->state.state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);

                        ret = handler (event, event->ctx);

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

                        ret = glusterd_friend_sm_transition_state (peerinfo, state, event_type);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, "Unable to transition"
                                        "state from %d to %d", peerinfo->state.state,
                                         state[event_type].next_state);
                                goto out;
                        }

                        ret = glusterd_store_update_peerinfo (peerinfo);

                        glusterd_destroy_friend_event_context (event);
                        GF_FREE (event);
                }
        }


        ret = 0;
out:
        return ret;
}


int
glusterd_friend_sm_init ()
{
        INIT_LIST_HEAD (&gd_friend_sm_queue);
        return 0;
}
