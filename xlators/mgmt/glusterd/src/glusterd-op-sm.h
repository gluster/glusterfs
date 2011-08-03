/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef GSYNC_CONF
#define GSYNC_CONF GEOREP"/gsyncd.conf"
#endif

#include <pthread.h>
#include <uuid/uuid.h>

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
        GD_OP_STATE_STAGE_OP_FAILED,
        GD_OP_STATE_COMMIT_OP_FAILED,
        GD_OP_STATE_BRICK_OP_SENT,
        GD_OP_STATE_BRICK_OP_FAILED,
        GD_OP_STATE_BRICK_COMMITTED,
        GD_OP_STATE_BRICK_COMMIT_FAILED,
        GD_OP_STATE_ACK_DRAIN,
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
        GD_OP_EVENT_ALL_ACK,
        GD_OP_EVENT_LOCAL_UNLOCK_NO_RESP,
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
        int32_t                         brick_pending_count;
        int32_t                         op_count;
        glusterd_op_t                   op;
        struct list_head                op_peers;
        void                            *op_ctx;
        rpcsvc_request_t                *req;
        int32_t                         op_ret;
        int32_t                         op_errno;
        char                            *op_errstr;
        struct  list_head               pending_bricks;
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

struct glusterd_req_ctx_ {
        rpcsvc_request_t *req;
	u_char            uuid[16];
	int               op;
        dict_t           *dict;
};

typedef struct glusterd_req_ctx_ glusterd_req_ctx_t;

typedef struct glusterd_op_brick_rsp_ctx_ {
        int  op_ret;
        char *op_errstr;
        dict_t *rsp_dict;
        glusterd_req_ctx_t *commit_ctx;
        glusterd_brickinfo_t *brickinfo;
} glusterd_op_brick_rsp_ctx_t;

typedef struct glusterd_pr_brick_rsp_conv_t {
        int count;
        dict_t *dict;
} glusterd_pr_brick_rsp_conv_t;

typedef struct glusterd_status_rsp_conv_ {
        int count;
        dict_t *dict;
} glusterd_status_rsp_conv_t;

typedef struct glusterd_gsync_status_temp {
        dict_t *rsp_dict;
        glusterd_volinfo_t *volinfo;
}glusterd_gsync_status_temp_t;
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
glusterd_op_set_ctx (void *ctx);

int32_t
glusterd_op_set_op (glusterd_op_t op);

int
glusterd_op_build_payload (dict_t **req);

int32_t
glusterd_op_stage_validate (glusterd_op_t op, dict_t *req, char **op_errstr,
                            dict_t *rsp_dict);

int32_t
glusterd_op_commit_perform (glusterd_op_t op, dict_t *req, char **op_errstr,
                            dict_t* dict);

void *
glusterd_op_get_ctx ();

int32_t
glusterd_op_set_req (rpcsvc_request_t *req);

int32_t
glusterd_op_send_cli_response (glusterd_op_t op, int32_t op_ret,
                               int32_t op_errno, rpcsvc_request_t *req,
                               void *ctx, char *op_errstr);
int32_t
glusterd_op_get_op ();

int32_t
glusterd_op_clear_op ();

int32_t
glusterd_op_free_ctx (glusterd_op_t op, void *ctx);

int
glusterd_check_option_exists(char *optstring, char **completion);

int
set_xlator_option (dict_t *dict, char *key, char *value);

char *
glusterd_check_brick_rb_part (char *bricks, int count, glusterd_volinfo_t *volinfo);

void
glusterd_do_replace_brick (void *data);
int
glusterd_options_reset (glusterd_volinfo_t *volinfo, int32_t is_force);

char*
glusterd_op_sm_state_name_get (int state);

char*
glusterd_op_sm_event_name_get (int event);
int32_t
glusterd_op_bricks_select (glusterd_op_t op, dict_t *dict, char **op_errstr);
int
glusterd_brick_op_build_payload (glusterd_op_t op, glusterd_brickinfo_t *brickinfo,
                                 gd1_mgmt_brick_op_req **req, dict_t *dict);
int32_t
glusterd_handle_brick_rsp (glusterd_brickinfo_t *brickinfo,
                           glusterd_op_t op, dict_t *rsp_dict, dict_t *ctx_dict,
                           char **op_errstr);
void glusterd_op_brick_disconnect (void *data);
int32_t
glusterd_op_init_ctx ();
int32_t
glusterd_op_fini_ctx ();
int32_t
glusterd_volume_stats_read_perf (char *brick_path, int32_t blk_size,
                int32_t blk_count, double *throughput, double *time);
int32_t
glusterd_volume_stats_write_perf (char *brick_path, int32_t blk_size,
                int32_t blk_count, double *throughput, double *time);
gf_boolean_t
glusterd_is_volume_started (glusterd_volinfo_t  *volinfo);
int
glusterd_start_bricks (glusterd_volinfo_t *volinfo);
gf_boolean_t
glusterd_are_all_volumes_stopped ();
int
glusterd_stop_bricks (glusterd_volinfo_t *volinfo);
int
glusterd_get_gsync_status_mst_slv( glusterd_volinfo_t *volinfo,
                                   char *slave, dict_t *rsp_dict);
int
gsync_status (char *master, char *slave, int *status);

int
glusterd_gsync_get_param_file (char *prmfile, const char *ext, char *master,
                                char *slave, char *gl_workdir);
int
glusterd_check_gsync_running (glusterd_volinfo_t *volinfo, gf_boolean_t *flag);
#endif
