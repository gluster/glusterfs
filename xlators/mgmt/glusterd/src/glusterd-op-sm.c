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
//#include "md5.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-ha.h"

static struct list_head gd_op_sm_queue;
glusterd_op_info_t    opinfo;

static int
glusterd_op_get_len (glusterd_op_t op)
{
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        int             ret = -1;

        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        {
                                dict_t *dict = glusterd_op_get_ctx (op);
                                ret = dict_serialized_length (dict);
                                return ret;
                        }
                        break;

                case GD_OP_START_BRICK:
                        break;

                default:
                        GF_ASSERT (op);

        }

        return 0;
}

int 
glusterd_op_build_payload (glusterd_op_t op, gd1_mgmt_stage_op_req **req)
{
        int                     len = 0;
        int                     ret = -1;
        gd1_mgmt_stage_op_req   *stage_req = NULL;
        
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);
        GF_ASSERT (req);

        len = glusterd_op_get_len (op);

        stage_req = GF_CALLOC (1, sizeof (*stage_req), 
                               gf_gld_mt_mop_stage_req_t);

        if (!stage_req) {
                gf_log ("", GF_LOG_ERROR, "Out of Memory");
                goto out;
        }

        stage_req->buf.buf_val = GF_CALLOC (1, len, 
                                            gf_gld_mt_mop_stage_req_t);

        if (!stage_req->buf.buf_val) {
                gf_log ("", GF_LOG_ERROR, "Out of Memory");
                goto out;
        }

        glusterd_get_uuid (&stage_req->uuid);
        stage_req->op = op;
        stage_req->buf.buf_len = len;

        switch (op) {
                case GD_OP_CREATE_VOLUME:
                        {
                                dict_t  *dict = NULL;
                                dict = glusterd_op_get_ctx (op);
                                GF_ASSERT (dict);
                                ret = dict_serialize (dict, 
                                                      stage_req->buf.buf_val);
                                if (ret) {
                                        goto out;
                                }
                        }


                default:
                        break;
        }

        *req = stage_req;
        ret = 0;

out:
        return ret;
}



/*static int
glusterd_xfer_stage_req (xlator_t *this, int32_t *lock_count)
{
        gf_hdr_common_t       *hdr = NULL;
        size_t                hdrlen = -1;
        int                   ret = -1;
        glusterd_conf_t       *priv = NULL;
        call_frame_t          *dummy_frame = NULL;
        glusterd_peerinfo_t   *peerinfo = NULL;
        int                   pending_lock = 0;
        int                   i = 0;

        GF_ASSERT (this);
        GF_ASSERT (lock_count);

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


        ret = glusterd_op_build_payload (i, &hdr, &hdrlen);

        if (ret)
                goto out;

        dummy_frame = create_frame (this, this->ctx->pool);

        if (!dummy_frame)
                goto out;

        list_for_each_entry (peerinfo, &opinfo.op_peers, op_peers_list) {
                GF_ASSERT (peerinfo);

                GF_ASSERT (peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED); 

        
                ret = glusterd_xfer (dummy_frame, this,
                                     peerinfo->trans,
                                     GF_OP_TYPE_MOP_REQUEST, 
                                     GF_MOP_STAGE_OP,
                                     hdr, hdrlen, NULL, 0, NULL);
                if (!ret)
                        pending_lock++;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent op req to %d peers",
                                            pending_lock);
        if (i < GD_OP_MAX) 
                opinfo.pending_op[i] = 0;

        *lock_count = pending_lock;

out:
        if (hdr)
                GF_FREE (hdr);

        return ret;
} */

/*static int
glusterd_xfer_commit_req (xlator_t *this, int32_t *lock_count)
{
        gf_hdr_common_t       *hdr = NULL;
        size_t                hdrlen = -1;
        int                   ret = -1;
        glusterd_conf_t       *priv = NULL;
        call_frame_t          *dummy_frame = NULL;
        glusterd_peerinfo_t   *peerinfo = NULL;
        int                   pending_lock = 0;
        int                   i = 0;

        GF_ASSERT (this);
        GF_ASSERT (lock_count);

        priv = this->private;
        GF_ASSERT (priv);


        for ( i = GD_OP_NONE; i < GD_OP_MAX; i++) {
                if (opinfo.commit_op[i])
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


        ret = glusterd_op_build_payload (i, &hdr, &hdrlen);

        if (ret)
                goto out;

        dummy_frame = create_frame (this, this->ctx->pool);

        if (!dummy_frame)
                goto out;

        list_for_each_entry (peerinfo, &opinfo.op_peers, op_peers_list) {
                GF_ASSERT (peerinfo);

                GF_ASSERT (peerinfo->state.state == GD_FRIEND_STATE_BEFRIENDED); 

        
                ret = glusterd_xfer (dummy_frame, this,
                                     peerinfo->trans,
                                     GF_OP_TYPE_MOP_REQUEST, 
                                     GF_MOP_STAGE_OP,
                                     hdr, hdrlen, NULL, 0, NULL);
                if (!ret)
                        pending_lock++;
        }

        gf_log ("glusterd", GF_LOG_NORMAL, "Sent op req to %d peers",
                                            pending_lock);
        if (i < GD_OP_MAX) 
                opinfo.pending_op[i] = 0;

        *lock_count = pending_lock;

out:
        if (hdr)
                GF_FREE (hdr);

        return ret;
}*/

static int
glusterd_op_stage_create_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        gf_boolean_t                            exists = _gf_false;

        GF_ASSERT (req);

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname); 

        if (exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s exists", 
                        volname);
                ret = -1;
        } else {
                ret = 0;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
glusterd_op_create_volume (gd1_mgmt_stage_op_req *req)
{
        int                                     ret = 0;
        dict_t                                  *dict = NULL;
        char                                    *volname = NULL;
        glusterd_conf_t                         *priv = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        glusterd_brickinfo_t                    *brickinfo = NULL;
        xlator_t                                *this = NULL;
        char                                    *brick = NULL;
        int32_t                                 count = 0;
        int32_t                                 i = 0;
        char                                    key[50];

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_unserialize (req->buf.buf_val, req->buf.buf_len, &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to unserialize dict");
                goto out;
        }

        ret = glusterd_volinfo_new (&volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        strncpy (volinfo->volname, volname, 1024);

        GF_ASSERT (volinfo->volname);

        ret = dict_get_int32 (dict, "type", &volinfo->type);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get type");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &volinfo->brick_count); 

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get count");
                goto out;
        }

        count = volinfo->brick_count;

        while ( i <= count) {
                snprintf (key, 50, "brick%d", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret)
                        goto out;

                ret = glusterd_brickinfo_from_brick (brick, &brickinfo);
                if (ret)
                        goto out;

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                i++;
        }

        ret = glusterd_ha_create_volume (volinfo);

out:
        return ret;
}

static int
glusterd_op_ac_none (glusterd_op_sm_event_t *event, void *ctx)
{
        int ret = 0;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_lock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        priv = this->private;

        proc = &priv->mgmt->proctable[GD_MGMT_CLUSTER_LOCK];
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
        }
        // TODO: if pending_count = 0, inject ALL_ACC here

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_send_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        priv = this->private;

        proc = &priv->mgmt->proctable[GD_MGMT_CLUSTER_UNLOCK];
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
        }
        // TODO: if pending_count = 0, inject ALL_ACC here

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_lock (glusterd_op_sm_event_t *event, void *ctx)
{
        int                      ret = 0;
        glusterd_op_lock_ctx_t   *lock_ctx = NULL;
        int32_t                  status = 0;


        GF_ASSERT (event);
        GF_ASSERT (ctx);

        lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

        status = glusterd_lock (lock_ctx->uuid);

        gf_log ("", GF_LOG_DEBUG, "Lock Returned %d", status);

        ret = glusterd_op_lock_send_resp (lock_ctx->req, status);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_unlock (glusterd_op_sm_event_t *event, void *ctx)
{
        int ret = 0;
        glusterd_op_lock_ctx_t   *lock_ctx = NULL;

        GF_ASSERT (event);
        GF_ASSERT (ctx);

        lock_ctx = (glusterd_op_lock_ctx_t *)ctx;

        ret = glusterd_unlock (lock_ctx->uuid);

        gf_log ("", GF_LOG_DEBUG, "Unlock Returned %d", ret);

        ret = glusterd_op_unlock_send_resp (lock_ctx->req, ret);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_rcvd_lock_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_ALL_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_op_ac_send_stage_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->mgmt);

        proc = &priv->mgmt->proctable[GD_MGMT_STAGE_OP];
        GF_ASSERT (proc);
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
        }
        // TODO: if pending_count = 0, inject ALL_ACC here

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_send_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        rpc_clnt_procedure_t    *proc = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (priv->mgmt);

        proc = &priv->mgmt->proctable[GD_MGMT_COMMIT_OP];
        GF_ASSERT (proc);
        if (proc->fn) {
                ret = proc->fn (NULL, this, NULL);
        }
        // TODO: if pending_count = 0, inject ALL_ACC here

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;

}

static int
glusterd_op_ac_rcvd_stage_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_STAGE_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_op_ac_rcvd_commit_op_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_COMMIT_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_op_ac_rcvd_unlock_acc (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;
        glusterd_op_sm_event_t  *new_event = NULL;

        GF_ASSERT (event);

        opinfo.pending_count--;

        if (opinfo.pending_count)
                goto out;

        ret = glusterd_op_sm_new_event (GD_OP_EVENT_ALL_ACC, &new_event);

        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (new_event);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

out:
        return ret;
}

static int
glusterd_op_ac_commit_error (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = 0;

        //Log here with who failed the commit
        //
        return ret;
}

static int
glusterd_op_ac_stage_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                     ret = -1;
        gd1_mgmt_stage_op_req   *req = NULL;
        glusterd_op_stage_ctx_t *stage_ctx = NULL;
        int32_t                 status = 0;

        GF_ASSERT (ctx);

        stage_ctx = ctx;

        req = stage_ctx->stage_req;

        switch (req->op) {
                case GD_OP_CREATE_VOLUME:
                        status = glusterd_op_stage_create_volume (req);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                req->op);
        }

        ret = glusterd_op_stage_send_resp (stage_ctx->req, req->op, status);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

static int
glusterd_op_ac_commit_op (glusterd_op_sm_event_t *event, void *ctx)
{
        int                             ret = 0;
        gd1_mgmt_stage_op_req           *req = NULL;
        glusterd_op_commit_ctx_t        *commit_ctx = NULL;
        int32_t                         status = 0;

        GF_ASSERT (ctx);

        commit_ctx = ctx;

        req = commit_ctx->stage_req;

        switch (req->op) {
                case GD_OP_CREATE_VOLUME:
                        ret = glusterd_op_create_volume (req);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Unknown op %d",
                                req->op);
        }

        ret = glusterd_op_commit_send_resp (commit_ctx->req, req->op, status);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


static int
glusterd_op_sm_transition_state (glusterd_op_info_t *opinfo, 
                                 glusterd_op_sm_t *state, 
                                 glusterd_op_sm_event_type_t event_type)
{

        GF_ASSERT (state);
        GF_ASSERT (opinfo);

        gf_log ("", GF_LOG_NORMAL, "Transitioning from %d to %d",
                     opinfo->state.state, state[event_type].next_state);
        opinfo->state.state = 
                state[event_type].next_state;
        return 0;
}



glusterd_op_sm_t glusterd_op_state_default [] = {
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_send_lock},//EVENT_START_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_lock}, //EVENT_LOCK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_ALL_ACC 
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_lock_sent [] = {
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_rcvd_lock_acc}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_send_stage_op}, //EVENT_ALL_ACC 
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_send_unlock}, //EVENT_RCVD_RJT
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_LOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_locked [] = {
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_ALL_ACC 
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGED, glusterd_op_ac_stage_op}, //EVENT_STAGE_OP
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_LOCKED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_stage_op_sent [] = {
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_rcvd_stage_op_acc}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_send_stage_op}, //EVENT_ALL_ACC 
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_send_commit_op}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_UNLOCK_SENT,   glusterd_op_ac_send_unlock}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGE_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_staged [] = {
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_STAGED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_ALL_ACC 
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMITED, glusterd_op_ac_commit_op}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_STAGED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commit_op_sent [] = {
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_rcvd_commit_op_acc}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_commit_op}, //EVENT_ALL_ACC 
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_UNLOCK_SENT,    glusterd_op_ac_send_unlock}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_commit_error}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_COMMIT_OP_SENT, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_commited [] = {
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_ALL_ACC 
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_COMMITED, glusterd_op_ac_none}, //EVENT_MAX
};

glusterd_op_sm_t glusterd_op_state_unlock_sent [] = {
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_NONE
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none},//EVENT_START_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_LOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_rcvd_unlock_acc}, //EVENT_RCVD_ACC 
        {GD_OP_STATE_DEFAULT, glusterd_op_ac_none}, //EVENT_ALL_ACC 
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_ACC 
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_ACC 
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_RCVD_RJT
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_STAGE_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_COMMIT_OP
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_unlock}, //EVENT_UNLOCK
        {GD_OP_STATE_UNLOCK_SENT, glusterd_op_ac_none}, //EVENT_MAX
};


glusterd_op_sm_t *glusterd_op_state_table [] = {
        glusterd_op_state_default,
        glusterd_op_state_lock_sent,
        glusterd_op_state_locked,
        glusterd_op_state_stage_op_sent,
        glusterd_op_state_staged,
        glusterd_op_state_commit_op_sent,
        glusterd_op_state_commited,
        glusterd_op_state_unlock_sent
};

int
glusterd_op_sm_new_event (glusterd_op_sm_event_type_t event_type,
                          glusterd_op_sm_event_t **new_event)
{
        glusterd_op_sm_event_t      *event = NULL;

        GF_ASSERT (new_event);
        GF_ASSERT (GD_OP_EVENT_NONE <= event_type &&
                        GD_OP_EVENT_MAX > event_type);

        event = GF_CALLOC (1, sizeof (*event), gf_gld_mt_op_sm_event_t); 

        if (!event)
                return -1;

        *new_event = event;
        event->event = event_type;
        INIT_LIST_HEAD (&event->list);

        return 0;
}

int
glusterd_op_sm_inject_event (glusterd_op_sm_event_t *event)
{
        GF_ASSERT (event);
        gf_log ("glusterd", GF_LOG_NORMAL, "Enqueuing event: %d",
                        event->event);
        list_add_tail (&event->list, &gd_op_sm_queue);

        return 0;
}


int
glusterd_op_sm ()
{
        glusterd_op_sm_event_t          *event = NULL;
        glusterd_op_sm_event_t          *tmp = NULL;
        int                             ret = -1;
        glusterd_op_sm_ac_fn            handler = NULL;
        glusterd_op_sm_t                *state = NULL;
        glusterd_op_sm_event_type_t     event_type = 0;

        
        while (!list_empty (&gd_op_sm_queue)) {
	
                list_for_each_entry_safe (event, tmp, &gd_op_sm_queue, list) {
		
                        list_del_init (&event->list);
                        event_type = event->event;

                        state = glusterd_op_state_table[opinfo.state.state];

                        GF_ASSERT (state);

                        handler = state[event_type].handler;
                        GF_ASSERT (handler);
                
                        ret = handler (event, event->ctx);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, 
                                        "handler returned: %d", ret);
                                return ret;
                        }

                        ret = glusterd_op_sm_transition_state (&opinfo, state, 
                                                                event_type);

                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR, 
                                        "Unable to transition"
                                        "state from %d to %d", 
                                         opinfo.state.state,
                                         state[event_type].next_state);
                                return ret;
                        }

                        GF_FREE (event);
                }
        }
        

        ret = 0;

        return ret;
}

int32_t
glusterd_op_set_op (glusterd_op_t op)
{
 
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op[op] = 1;
        opinfo.pending_op[op] = 1;
        opinfo.commit_op[op] = 1;

        return 0;

}

int32_t
glusterd_op_set_ctx (glusterd_op_t op, void *ctx)
{
 
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        opinfo.op_ctx[op] = ctx;

        return 0;

}


void *
glusterd_op_get_ctx (glusterd_op_t op)
{
        GF_ASSERT (op < GD_OP_MAX);
        GF_ASSERT (op > GD_OP_NONE);

        return opinfo.op_ctx[op];

}

int
glusterd_op_sm_init ()
{
        INIT_LIST_HEAD (&gd_op_sm_queue);
        return 0;
}
