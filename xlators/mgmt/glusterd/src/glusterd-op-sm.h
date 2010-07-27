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

#ifndef _GLUSTERD_OP_SM_H_
#define _GLUSTERD_OP_SM_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include "uuid.h"

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "protocol-common.h"

#define GD_VOLUME_NAME_MAX 256

typedef enum glusterd_op_sm_state_ {
        GD_OP_STATE_DEFAULT = 0,
        GD_OP_STATE_LOCK_SENT,
        GD_OP_STATE_LOCKED,
        GD_OP_STATE_STAGE_OP_SENT,
        GD_OP_STATE_STAGED,
        GD_OP_STATE_COMMIT_OP_SENT,
        GD_OP_STATE_COMMITED,
        GD_OP_STATE_UNLOCK_SENT,
        GD_OP_STATE_MAX,
} glusterd_op_sm_state_t;

typedef enum glusterd_op_sm_event_type_ {
        GD_OP_EVENT_NONE = 0,
        GD_OP_EVENT_START_LOCK,
        GD_OP_EVENT_LOCK,
        GD_OP_EVENT_RCVD_ACC,
        GD_OP_EVENT_ALL_ACC,
        GD_OP_EVENT_STAGE_ACC,
        GD_OP_EVENT_COMMIT_ACC,
        GD_OP_EVENT_RCVD_RJT,
        GD_OP_EVENT_STAGE_OP,
        GD_OP_EVENT_COMMIT_OP,
        GD_OP_EVENT_UNLOCK,
        GD_OP_EVENT_MAX
} glusterd_op_sm_event_type_t;


struct glusterd_op_sm_event_ {
        struct list_head                list;
        void                            *ctx;
        glusterd_op_sm_event_type_t     event;
};

typedef struct glusterd_op_sm_event_ glusterd_op_sm_event_t;

typedef int (*glusterd_op_sm_ac_fn) (glusterd_op_sm_event_t *, void *);

typedef struct glusterd_op_sm_ {
        glusterd_op_sm_state_t      next_state;
        glusterd_op_sm_ac_fn        handler;
} glusterd_op_sm_t;

typedef enum glusterd_op_ {
        GD_OP_NONE = 0,
        GD_OP_CREATE_VOLUME,
        GD_OP_START_BRICK,
        GD_OP_STOP_BRICK,
        GD_OP_DELETE_VOLUME,
        GD_OP_START_VOLUME,
        GD_OP_STOP_VOLUME,
        GD_OP_RENAME_VOLUME,
        GD_OP_DEFRAG_VOLUME,
        GD_OP_ADD_BRICK,
        GD_OP_REMOVE_BRICK,
        GD_OP_REPLACE_BRICK,
        GD_OP_SYNC_VOLUME,
        GD_OP_MAX,
} glusterd_op_t;

typedef struct glusterd_op_sm_state_info_ {
        glusterd_op_sm_state_t          state;
        struct timeval                  time;
} glusterd_op_sm_state_info_t;

struct glusterd_op_info_ {
        glusterd_op_sm_state_info_t     state;
        int32_t                         pending_count;
        int32_t                         op_count;
        glusterd_op_t                   op[GD_OP_MAX];
        glusterd_op_t                   pending_op[GD_OP_MAX];
        glusterd_op_t                   commit_op[GD_OP_MAX];
        struct list_head                op_peers;
        void                            *op_ctx[GD_OP_MAX];
        rpcsvc_request_t                *req;
        int32_t                         op_ret;
        int32_t                         op_errno;
        pthread_mutex_t                 lock;
        int32_t                         cli_op;
};

typedef struct glusterd_op_info_ glusterd_op_info_t;

struct glusterd_op_start_volume_ctx_ {
        char                    volume_name[GD_VOLUME_NAME_MAX];
};

typedef struct glusterd_op_start_volume_ctx_ glusterd_op_start_volume_ctx_t;
typedef struct glusterd_op_start_volume_ctx_ glusterd_op_stop_volume_ctx_t;
typedef struct glusterd_op_start_volume_ctx_ glusterd_op_delete_volume_ctx_t;


struct glusterd_op_lock_ctx_ {
        uuid_t                  uuid;
        rpcsvc_request_t        *req;
};

typedef struct glusterd_op_lock_ctx_ glusterd_op_lock_ctx_t;

struct glusterd_op_stage_ctx_ {
        rpcsvc_request_t        *req;
        gd1_mgmt_stage_op_req   stage_req;
};

typedef struct glusterd_op_stage_ctx_ glusterd_op_stage_ctx_t;

struct glusterd_op_commit_ctx_ {
        rpcsvc_request_t        *req;
        gd1_mgmt_stage_op_req   stage_req;
};

typedef struct glusterd_op_commit_ctx_ glusterd_op_commit_ctx_t;

int
glusterd_op_sm_new_event (glusterd_op_sm_event_type_t event_type,
                          glusterd_op_sm_event_t **new_event);
int
glusterd_op_sm_inject_event (glusterd_op_sm_event_type_t event_type,
                             void *ctx);

int
glusterd_op_sm_init ();

int
glusterd_op_sm ();

int32_t
glusterd_op_set_ctx (glusterd_op_t op, void *ctx);

int32_t
glusterd_op_set_op (glusterd_op_t op);

int32_t
glusterd_op_clear_pending_op (glusterd_op_t op);

int32_t
glusterd_op_clear_commit_op (glusterd_op_t op);

int
glusterd_op_build_payload (glusterd_op_t op, gd1_mgmt_stage_op_req **req);

int32_t
glusterd_op_stage_validate (gd1_mgmt_stage_op_req *req);

int32_t
glusterd_op_commit_perform (gd1_mgmt_stage_op_req *req);

void *
glusterd_op_get_ctx (glusterd_op_t op);

int32_t
glusterd_op_set_req (rpcsvc_request_t *req);

int32_t
glusterd_op_set_cli_op (gf_mgmt_procnum op);

int32_t
glusterd_op_send_cli_response (int32_t op, int32_t op_ret,
                               int32_t op_errno, rpcsvc_request_t *req);
int32_t
glusterd_op_get_op ();

int32_t
glusterd_op_clear_pending_op (glusterd_op_t op);

int32_t
glusterd_op_clear_commit_op (glusterd_op_t op);

int32_t
glusterd_op_clear_op (glusterd_op_t op);

int32_t
glusterd_op_clear_ctx (glusterd_op_t op);
#endif
