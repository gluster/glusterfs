/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

 enum glusterd_volume_status {
        GLUSTERD_STATUS_NONE = 0,
        GLUSTERD_STATUS_STARTED,
        GLUSTERD_STATUS_STOPPED
} ;

 struct gd1_mgmt_probe_req {
        unsigned char  uuid[16];
        string  hostname<>;
        int     port;
}  ;

 struct gd1_mgmt_probe_rsp {
        unsigned char  uuid[16];
        string  hostname<>;
        int     port;
        int     op_ret;
        int     op_errno;
        string op_errstr<>;
}  ;

struct gd1_mgmt_friend_req {
        unsigned char  uuid[16];
        string  hostname<>;
        int     port;
        opaque  vols<>;
}  ;

struct gd1_mgmt_friend_rsp {
        unsigned char  uuid[16];
        string  hostname<>;
        int     op_ret;
        int     op_errno;
        int     port;
}  ;

struct gd1_mgmt_unfriend_req {
        unsigned char  uuid[16];
        string  hostname<>;
        int     port;
}  ;

struct gd1_mgmt_unfriend_rsp {
        unsigned char  uuid[16];
        string  hostname<>;
        int     op_ret;
        int     op_errno;
        int     port;
}  ;

struct gd1_mgmt_cluster_lock_req {
        unsigned char  uuid[16];
}  ;

struct gd1_mgmt_cluster_lock_rsp {
        unsigned char  uuid[16];
        int     op_ret;
        int     op_errno;
}  ;

struct gd1_mgmt_cluster_unlock_req {
        unsigned char  uuid[16];
}  ;

struct gd1_mgmt_cluster_unlock_rsp {
        unsigned char  uuid[16];
        int     op_ret;
        int     op_errno;
}  ;

struct gd1_mgmt_stage_op_req {
        unsigned char  uuid[16];
        int     op;
        opaque  buf<>;
}  ;


struct gd1_mgmt_stage_op_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        opaque  dict<>;
}  ;

struct gd1_mgmt_commit_op_req {
        unsigned char  uuid[16];
        int     op;
        opaque  buf<>;
}  ;


struct gd1_mgmt_commit_op_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
        opaque  dict<>;
        string  op_errstr<>;
}  ;

struct gd1_mgmt_friend_update {
        unsigned char uuid[16];
        opaque  friends<>;
        int     port;
} ;

struct gd1_mgmt_friend_update_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
}  ;

struct gd1_mgmt_brick_op_req {
        string  name<>;
        int     op;
        opaque  input<>;
} ;

struct gd1_mgmt_brick_op_rsp {
        int     op_ret;
        int     op_errno;
        opaque  output<>;
        string  op_errstr<>;
} ;

struct gd1_mgmt_v3_lock_req {
        unsigned char  uuid[16];
        unsigned char  txn_id[16];
        int            op;
        opaque         dict<>;
}  ;

struct gd1_mgmt_v3_lock_rsp {
        unsigned char  uuid[16];
        unsigned char  txn_id[16];
        opaque         dict<>;
        int            op_ret;
        int            op_errno;
}  ;

struct gd1_mgmt_v3_pre_val_req {
        unsigned char  uuid[16];
        int     op;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_pre_val_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_brick_op_req {
        unsigned char  uuid[16];
        int     op;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_brick_op_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_commit_req {
        unsigned char  uuid[16];
        int     op;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_commit_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
        opaque  dict<>;
        string  op_errstr<>;
}  ;

struct gd1_mgmt_v3_post_val_req {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_post_val_rsp {
        unsigned char  uuid[16];
        int     op;
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        opaque  dict<>;
}  ;

struct gd1_mgmt_v3_unlock_req {
        unsigned char  uuid[16];
        unsigned char  txn_id[16];
        int            op;
        opaque         dict<>;
}  ;

struct gd1_mgmt_v3_unlock_rsp {
        unsigned char  uuid[16];
        unsigned char  txn_id[16];
        opaque         dict<>;
        int            op_ret;
        int            op_errno;
}  ;
