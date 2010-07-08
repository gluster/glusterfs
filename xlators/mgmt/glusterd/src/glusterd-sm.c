/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is GF_FREE software; you can redistribute it and/or modify
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
#include <time.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include <uuid/uuid.h>

//#include "transport.h"
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

static struct list_head gd_friend_sm_queue;

static int
glusterd_ac_none (glusterd_friend_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

/*        ret = glusterd_xfer_friend_req_msg (peerinfo, THIS);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to probe: %s", hostname);
        }
*/

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_ac_friend_probe (glusterd_friend_sm_event_t *event, void *ctx)
{
        int                     ret = -1;
        char                    *hostname = NULL;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        glusterd_conf_t         *conf = NULL;
        xlator_t                *this = NULL;
        glusterd_probe_ctx_t    *probe_ctx = NULL;

        GF_ASSERT (ctx);

        probe_ctx = ctx;
        hostname = probe_ctx->hostname;

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
                ret = proc->fn (frame, this, hostname);
        }


/*        ret = glusterd_friend_probe (hostname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to probe: %s", hostname);
        }
*/

out:        
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
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

        GF_ASSERT (ctx);
        ev_ctx = ctx;
        uuid_copy (uuid, ev_ctx->uuid);
        peerinfo = event->peerinfo;
        GF_ASSERT (peerinfo);
        uuid_copy (peerinfo->uuid, ev_ctx->uuid);

        ret = glusterd_xfer_friend_add_resp (ev_ctx->req, ev_ctx->hostname);

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
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_MAX
};


glusterd_sm_t  glusterd_state_req_sent [] = {
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_rcvd [] = {
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_friend_probe}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_friend_add}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_befriended [] = {
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_req_sent_rcvd [] = {
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_BEFRIENDED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_SENT_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t  glusterd_state_rejected [] = {
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_NONE,
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_friend_probe}, //EVENT_PROBE,
        {GD_FRIEND_STATE_REQ_SENT, glusterd_ac_friend_add}, //EVENT_INIT_FRIEND_REQ,
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_ACC
        {GD_FRIEND_STATE_REJECTED, glusterd_ac_none}, //EVENT_RCVD_RJT
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_handle_friend_add_req}, //EVENT_RCV_FRIEND_REQ
        {GD_FRIEND_STATE_DEFAULT, glusterd_ac_none}, //EVENT_REMOVE_FRIEND
        {GD_FRIEND_STATE_REQ_RCVD, glusterd_ac_none},//EVENT_MAX
};

glusterd_sm_t *glusterd_friend_state_table [] = {
        glusterd_state_default,
        glusterd_state_req_sent,
        glusterd_state_req_rcvd,
        glusterd_state_befriended,
        glusterd_state_req_sent_rcvd,
        glusterd_state_rejected,
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


int
glusterd_friend_sm ()
{
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_sm_event_t      *tmp = NULL;
        int                             ret = -1;
        glusterd_friend_sm_ac_fn        handler = NULL;
        glusterd_sm_t      *state = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_type_t event_type = 0;

	list_for_each_entry_safe (event, tmp, &gd_friend_sm_queue, list) {
		
                list_del_init (&event->list);
                peerinfo = event->peerinfo;
                event_type = event->event;

                if (!peerinfo && 
                   (GD_FRIEND_EVENT_PROBE == event_type ||
                    GD_FRIEND_EVENT_RCVD_FRIEND_REQ == event_type)) {
                        ret = glusterd_friend_add (NULL, GD_PEER_STATE_NONE, NULL, NULL,
                                                   &peerinfo);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, "Unable to add peer, "
                                        "ret = %d", ret);
                                continue; 
                        }
                        GF_ASSERT (peerinfo);
                        event->peerinfo = peerinfo;
                }


                state = glusterd_friend_state_table[peerinfo->state.state];

                GF_ASSERT (state);

                handler = state[event_type].handler;
                GF_ASSERT (handler);
        
                ret = handler (event, event->ctx);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "handler returned: "
                                        "%d", ret);
                        return ret;
                }

                ret = glusterd_friend_sm_transition_state (peerinfo, state, event_type);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to transition"
                                "state from %d to %d", peerinfo->state.state,
                                 state[event_type].next_state);
                        return ret;
                }

                GF_FREE (event);
        }
        

        ret = 0;

        return ret;
}


int
glusterd_friend_sm_init ()
{
        INIT_LIST_HEAD (&gd_friend_sm_queue);
        return 0;
}
