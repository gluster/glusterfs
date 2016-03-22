/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_SM_H_
#define _GLUSTERD_SM_H_

#include <pthread.h>
#include "compat-uuid.h"

#include "rpc-clnt.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
//#include "glusterd.h"
#include "rpcsvc.h"
#include "store.h"

#include "glusterd-rcu.h"

typedef enum gd_quorum_contribution_ {
        QUORUM_NONE,
        QUORUM_WAITING,
        QUORUM_DOWN,
        QUORUM_UP
} gd_quorum_contrib_t;

typedef enum glusterd_friend_sm_state_ {
        GD_FRIEND_STATE_DEFAULT = 0,
        GD_FRIEND_STATE_REQ_SENT,
        GD_FRIEND_STATE_REQ_RCVD,
        GD_FRIEND_STATE_BEFRIENDED,
        GD_FRIEND_STATE_REQ_ACCEPTED,
        GD_FRIEND_STATE_REQ_SENT_RCVD,
        GD_FRIEND_STATE_REJECTED,
        GD_FRIEND_STATE_UNFRIEND_SENT,
        GD_FRIEND_STATE_PROBE_RCVD,
        GD_FRIEND_STATE_CONNECTED_RCVD,
        GD_FRIEND_STATE_CONNECTED_ACCEPTED,
        GD_FRIEND_STATE_MAX
} glusterd_friend_sm_state_t;

typedef struct glusterd_peer_state_info_ {
        glusterd_friend_sm_state_t   state;
        struct timeval          transition_time;
}glusterd_peer_state_info_t;

typedef struct glusterd_peer_hostname_ {
        char                    *hostname;
        struct cds_list_head     hostname_list;
} glusterd_peer_hostname_t;

typedef struct glusterd_sm_transition_ {
        int             old_state;
        int             event;
        int             new_state;
        time_t          time;
} glusterd_sm_transition_t;

typedef struct glusterd_sm_tr_log_ {
        glusterd_sm_transition_t    *transitions;
        size_t                      current;
        size_t                      size;
        size_t                      count;
        char*                       (*state_name_get) (int);
        char*                       (*event_name_get) (int);
} glusterd_sm_tr_log_t;

struct glusterd_peerinfo_ {
        uuid_t                          uuid;
        char                            uuid_str[50]; /* Retrieve this using
                                                       * gd_peer_uuid_str ()
                                                       */
        glusterd_peer_state_info_t      state;
        char                            *hostname;
        struct cds_list_head            hostnames;
        int                             port;
        struct cds_list_head            uuid_list;
        struct cds_list_head            op_peers_list;
        struct rpc_clnt                 *rpc;
        rpc_clnt_prog_t                 *mgmt;
        rpc_clnt_prog_t                 *peer;
        rpc_clnt_prog_t                 *mgmt_v3;
        int                             connected;
        gf_store_handle_t               *shandle;
        glusterd_sm_tr_log_t            sm_log;
        gf_boolean_t                    quorum_action;
        gd_quorum_contrib_t             quorum_contrib;
        gf_boolean_t                    locked;
        gf_boolean_t                    detaching;
        /* Members required for proper cleanup using RCU */
        gd_rcu_head                     rcu_head;
        pthread_mutex_t                 delete_lock;
        uint32_t                        generation;
};

typedef struct glusterd_peerinfo_ glusterd_peerinfo_t;

typedef struct glusterd_local_peers_ {
        glusterd_peerinfo_t   *peerinfo;
        struct cds_list_head  op_peers_list;
} glusterd_local_peers_t;

typedef enum glusterd_ev_gen_mode_ {
        GD_MODE_OFF,
        GD_MODE_ON,
        GD_MODE_SWITCH_ON
} glusterd_ev_gen_mode_t;

typedef struct glusterd_peer_ctx_args_ {
        rpcsvc_request_t        *req;
        glusterd_ev_gen_mode_t  mode;
        dict_t                  *dict;
} glusterd_peerctx_args_t;

typedef struct glusterd_peer_ctx_ {
        glusterd_peerctx_args_t        args;
        uuid_t                         peerid;
        char                           *peername;
        uint32_t                       peerinfo_gen;
        char                           *errstr;
} glusterd_peerctx_t;

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
        GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND,
        GD_FRIEND_EVENT_REMOVE_FRIEND,
        GD_FRIEND_EVENT_CONNECTED,
        GD_FRIEND_EVENT_NEW_NAME,
        GD_FRIEND_EVENT_MAX
} glusterd_friend_sm_event_type_t;


typedef enum glusterd_friend_update_op_ {
        GD_FRIEND_UPDATE_NONE = 0,
        GD_FRIEND_UPDATE_ADD,
        GD_FRIEND_UPDATE_DEL,
} glusterd_friend_update_op_t;


struct glusterd_friend_sm_event_ {
        struct cds_list_head             list;
        uuid_t                           peerid;
        char                            *peername;
        void                            *ctx;
        glusterd_friend_sm_event_type_t  event;
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
        int                      port;
        dict_t                  *vols;
} glusterd_friend_req_ctx_t;

typedef struct glusterd_friend_update_ctx_ {
        uuid_t                  uuid;
        char                    *hostname;
        int                     op;
} glusterd_friend_update_ctx_t;

typedef struct glusterd_probe_ctx_ {
        char                    *hostname;
        rpcsvc_request_t        *req;
        int                      port;
        dict_t                  *dict;
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

void
glusterd_destroy_probe_ctx (glusterd_probe_ctx_t *ctx);

void
glusterd_destroy_friend_req_ctx (glusterd_friend_req_ctx_t *ctx);

char*
glusterd_friend_sm_state_name_get (int state);

char*
glusterd_friend_sm_event_name_get (int event);

int
glusterd_broadcast_friend_delete (char *hostname, uuid_t uuid);
void
glusterd_destroy_friend_update_ctx (glusterd_friend_update_ctx_t *ctx);
#endif
