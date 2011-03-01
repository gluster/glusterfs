/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
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
        GD_OP_EVENT_START_UNLOCK,
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
        gf_boolean_t                    ctx_free[GD_OP_MAX];
        char                            *op_errstr;
};

typedef struct glusterd_op_info_ glusterd_op_info_t;

struct glusterd_op_delete_volume_ctx_ {
        char                    volume_name[GD_VOLUME_NAME_MAX];
};

typedef struct glusterd_op_delete_volume_ctx_ glusterd_op_delete_volume_ctx_t;

struct glusterd_op_log_filename_ctx_ {
        char                    volume_name[GD_VOLUME_NAME_MAX];
        char                    brick[GD_VOLUME_NAME_MAX];
        char                    path[PATH_MAX];
};
typedef struct glusterd_op_log_filename_ctx_ glusterd_op_log_filename_ctx_t;

struct glusterd_op_lock_ctx_ {
        uuid_t                  uuid;
        rpcsvc_request_t        *req;
};

typedef struct glusterd_op_lock_ctx_ glusterd_op_lock_ctx_t;

struct glusterd_op_stage_ctx_ {
        rpcsvc_request_t *req;
	u_char            uuid[16];
	int               op;
        dict_t           *dict;
};

typedef struct glusterd_op_stage_ctx_ glusterd_op_stage_ctx_t;

struct glusterd_op_commit_ctx_ {
        rpcsvc_request_t *req;
	u_char            uuid[16];
	int               op;
        dict_t           *dict;
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
glusterd_op_build_payload (glusterd_op_t op, dict_t **req);

int32_t
glusterd_op_stage_validate (dict_t *req, char **op_errstr,
                            dict_t *rsp_dict);

int32_t
glusterd_op_commit_perform (dict_t *req, char **op_errstr,
                            dict_t* dict);

void *
glusterd_op_get_ctx (glusterd_op_t op);

int32_t
glusterd_op_set_req (rpcsvc_request_t *req);

int32_t
glusterd_op_set_cli_op (glusterd_op_t op);

int32_t
glusterd_op_send_cli_response (glusterd_op_t op, int32_t op_ret,
                               int32_t op_errno, rpcsvc_request_t *req,
                               void *ctx, char *op_errstr);
int32_t
glusterd_op_get_op ();

int32_t
glusterd_op_clear_pending_op (glusterd_op_t op);

int32_t
glusterd_op_clear_commit_op (glusterd_op_t op);

int32_t
glusterd_op_clear_op (glusterd_op_t op);

int32_t
glusterd_op_free_ctx (glusterd_op_t op, void *ctx, gf_boolean_t ctx_free);

int32_t
glusterd_opinfo_unlock();

int32_t
glusterd_op_set_ctx_free (glusterd_op_t op, gf_boolean_t ctx_free);

int32_t
glusterd_op_clear_ctx_free (glusterd_op_t op);

gf_boolean_t
glusterd_op_get_ctx_free (glusterd_op_t op);

int
glusterd_check_option_exists(char *optstring, char **completion);

int
set_xlator_option (dict_t *dict, char *key, char *value);

char *
glusterd_check_brick_rb_part (char *bricks, int count, glusterd_volinfo_t *volinfo);

void
glusterd_do_replace_brick (void *data);
int
glusterd_options_reset (glusterd_volinfo_t *volinfo);

char*
glusterd_op_sm_state_name_get (int state);

char*
glusterd_op_sm_event_name_get (int event);
#endif
