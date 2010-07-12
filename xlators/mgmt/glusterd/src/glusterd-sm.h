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

#ifndef _GLUSTERD_SM_H_
#define _GLUSTERD_SM_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <uuid/uuid.h>

#include "rpc-clnt.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "authenticate.h"
#include "fd.h"
#include "byte-order.h"
//#include "glusterd.h"
#include "rpcsvc.h"

typedef enum glusterd_friend_sm_state_ {
        GD_FRIEND_STATE_DEFAULT = 0,
        GD_FRIEND_STATE_REQ_SENT,
        GD_FRIEND_STATE_REQ_RCVD,
        GD_FRIEND_STATE_BEFRIENDED,
        GD_FRIEND_STATE_REQ_ACCEPTED,
        GD_FRIEND_STATE_REQ_SENT_RCVD,
        GD_FRIEND_STATE_REJECTED,
        GD_FRIEND_STATE_UNFRIEND_SENT,
        GD_FRIEND_STATE_MAX
} glusterd_friend_sm_state_t;

typedef struct glusterd_peer_state_info_ {
        glusterd_friend_sm_state_t   state;
        struct timeval          transition_time;
}glusterd_peer_state_info_t;


struct glusterd_peerinfo_ {
        uuid_t                          uuid;
        char                            uuid_str[50];
        glusterd_peer_state_info_t      state;
        char                            *hostname;
        int                             port;
        struct list_head                uuid_list;
        struct list_head                op_peers_list;
        struct rpc_clnt                 *rpc;
};

typedef struct glusterd_peerinfo_ glusterd_peerinfo_t;



typedef enum glusterd_friend_sm_event_type_ {
        GD_FRIEND_EVENT_NONE = 0,
        GD_FRIEND_EVENT_PROBE,
        GD_FRIEND_EVENT_INIT_FRIEND_REQ,
        GD_FRIEND_EVENT_RCVD_ACC,
        GD_FRIEND_EVENT_LOCAL_ACC,
        GD_FRIEND_EVENT_RCVD_RJT,
        GD_FRIEND_EVENT_LOCAL_RJT,
        GD_FRIEND_EVENT_RCVD_FRIEND_REQ,
        GD_FRIEND_EVENT_INIT_REMOVE_FRIEND,
        GD_FRIEND_EVENT_REMOVE_FRIEND,
        GD_FRIEND_EVENT_MAX
} glusterd_friend_sm_event_type_t;


struct glusterd_friend_sm_event_ {
        struct list_head        list;
        glusterd_peerinfo_t     *peerinfo;
        void                    *ctx;
        glusterd_friend_sm_event_type_t event;
};

typedef struct glusterd_friend_sm_event_ glusterd_friend_sm_event_t;

typedef int (*glusterd_friend_sm_ac_fn) (glusterd_friend_sm_event_t *, void *);

typedef struct glusterd_sm_ {
        glusterd_friend_sm_state_t      next_state;
        glusterd_friend_sm_ac_fn        handler;
} glusterd_sm_t;

typedef struct glusterd_friend_req_ctx_ {
        uuid_t                  uuid;
        char                    *hostname;
        rpcsvc_request_t        *req;
} glusterd_friend_req_ctx_t;

typedef struct glusterd_probe_ctx_ {
        char                    *hostname;
        rpcsvc_request_t        *req;
} glusterd_probe_ctx_t;
int
glusterd_friend_sm_new_event (glusterd_friend_sm_event_type_t event_type,
                              glusterd_friend_sm_event_t **new_event);
int
glusterd_friend_sm_inject_event (glusterd_friend_sm_event_t *event);

int
glusterd_friend_sm_init ();

int
glusterd_friend_sm ();

#endif
