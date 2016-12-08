/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_OP_SM_H_
#define _GLUSTERD_OP_SM_H_


#include <pthread.h>
#include "compat-uuid.h"

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "protocol-common.h"
#include "glusterd-hooks.h"

#define GD_OP_PROTECTED    (0x02)
#define GD_OP_UNPROTECTED  (0x04)

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
        struct cds_list_head            list;
        void                            *ctx;
        glusterd_op_sm_event_type_t     event;
        uuid_t                          txn_id;
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
        /* op is an enum, glusterd_op_t or glusterd_op_sm_state_info_t */
        int                             op;
        struct cds_list_head            op_peers;
        void                            *op_ctx;
        rpcsvc_request_t                *req;
        int32_t                         op_ret;
        int32_t                         op_errno;
        char                            *op_errstr;
        struct  cds_list_head           pending_bricks;
        uint32_t                        txn_generation;
};

typedef struct glusterd_op_info_ glusterd_op_info_t;

struct glusterd_op_log_filename_ctx_ {
        char                    volume_name[GD_VOLUME_NAME_MAX];
        char                    brick[GD_VOLUME_NAME_MAX];
        char                    path[PATH_MAX];
};
typedef struct glusterd_op_log_filename_ctx_ glusterd_op_log_filename_ctx_t;

struct glusterd_op_lock_ctx_ {
        uuid_t                  uuid;
        dict_t                 *dict;
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
        glusterd_pending_node_t *pending_node;
} glusterd_op_brick_rsp_ctx_t;

typedef struct glusterd_pr_brick_rsp_conv_t {
        int count;
        dict_t *dict;
} glusterd_pr_brick_rsp_conv_t;

typedef struct glusterd_heal_rsp_conv_ {
        dict_t *dict;
        glusterd_volinfo_t *volinfo;
        xlator_t *this;
} glusterd_heal_rsp_conv_t;

typedef struct glusterd_status_rsp_conv_ {
        int count;
        int brick_index_max;
        int other_count;
        dict_t *dict;
} glusterd_status_rsp_conv_t;


typedef struct glusterd_txn_opinfo_object_ {
        glusterd_op_info_t    opinfo;
} glusterd_txn_opinfo_obj;

typedef enum cli_cmd_type_ {
        PER_HEAL_XL,
        ALL_HEAL_XL,
 } cli_cmd_type;

typedef struct glusterd_all_volume_options {
        char    *option;
        char    *dflt_val;
} glusterd_all_vol_opts;

int
glusterd_op_commit_hook (glusterd_op_t op, dict_t *op_ctx,
                         glusterd_commit_hook_type_t type);

int
glusterd_op_sm_new_event (glusterd_op_sm_event_type_t event_type,
                          glusterd_op_sm_event_t **new_event);
int
glusterd_op_sm_inject_event (glusterd_op_sm_event_type_t event_type,
                             uuid_t *txn_id, void *ctx);

int
glusterd_op_sm_init ();

int
glusterd_op_sm ();

int32_t
glusterd_op_set_ctx (void *ctx);

int32_t
glusterd_op_set_op (glusterd_op_t op);

int
glusterd_op_build_payload (dict_t **req, char **op_errstr, dict_t *op_ctx);

int32_t
glusterd_op_stage_validate (glusterd_op_t op, dict_t *req, char **op_errstr,
                            dict_t *rsp_dict);

int32_t
glusterd_op_commit_perform (glusterd_op_t op, dict_t *req, char **op_errstr,
                            dict_t* dict);

int32_t
glusterd_op_txn_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                       char *err_str, size_t err_len);

int32_t
glusterd_op_txn_complete ();

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

char*
glusterd_op_sm_state_name_get (int state);

char*
glusterd_op_sm_event_name_get (int event);
int32_t
glusterd_op_bricks_select (glusterd_op_t op, dict_t *dict, char **op_errstr,
                           struct cds_list_head *selected, dict_t *rsp_dict);
int
glusterd_brick_op_build_payload (glusterd_op_t op, glusterd_brickinfo_t *brickinfo,
                                 gd1_mgmt_brick_op_req **req, dict_t *dict);
int
glusterd_node_op_build_payload (glusterd_op_t op, gd1_mgmt_brick_op_req **req,
                               dict_t *dict);
int32_t
glusterd_handle_brick_rsp (void *pending_entry, glusterd_op_t op,
                           dict_t *rsp_dict, dict_t *ctx_dict, char **op_errstr,
                           gd_node_type type);

dict_t*
glusterd_op_init_commit_rsp_dict (glusterd_op_t op);

void
glusterd_op_modify_op_ctx (glusterd_op_t op, void *op_ctx);

void
glusterd_op_perform_detach_tier (glusterd_volinfo_t *volinfo);

int
glusterd_set_detach_bricks (dict_t *dict, glusterd_volinfo_t *volinfo);

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
glusterd_defrag_volume_node_rsp (dict_t *req_dict, dict_t *rsp_dict,
                                 dict_t *op_ctx);
#ifdef HAVE_BD_XLATOR
int
glusterd_is_valid_vg (glusterd_brickinfo_t *brick, int check_tag, char *msg);
#endif

int32_t
glusterd_get_txn_opinfo (uuid_t *txn_id, glusterd_op_info_t  *opinfo);

int32_t
glusterd_set_txn_opinfo (uuid_t *txn_id, glusterd_op_info_t  *opinfo);

int32_t
glusterd_clear_txn_opinfo (uuid_t *txn_id);

int32_t
glusterd_generate_txn_id (dict_t *dict, uuid_t **txn_id);

void
glusterd_set_opinfo (char *errstr, int32_t op_errno, int32_t op_ret);

int
glusterd_dict_set_volid (dict_t *dict, char *volname, char **op_errstr);

int32_t
glusterd_tier_op (xlator_t *this, void *data);
#endif
