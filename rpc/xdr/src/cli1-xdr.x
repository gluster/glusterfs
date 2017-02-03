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

 enum gf_cli_defrag_type {
	GF_DEFRAG_CMD_NONE = 0,
        GF_DEFRAG_CMD_START,
        GF_DEFRAG_CMD_STOP,
        GF_DEFRAG_CMD_STATUS,
        GF_DEFRAG_CMD_START_LAYOUT_FIX,
        GF_DEFRAG_CMD_START_FORCE, /* used by remove-brick data migration */
        GF_DEFRAG_CMD_START_TIER,
        GF_DEFRAG_CMD_STATUS_TIER,
        GF_DEFRAG_CMD_START_DETACH_TIER,
        GF_DEFRAG_CMD_STOP_DETACH_TIER,
        GF_DEFRAG_CMD_PAUSE_TIER,
        GF_DEFRAG_CMD_RESUME_TIER,
        GF_DEFRAG_CMD_DETACH_STATUS,
        GF_DEFRAG_CMD_STOP_TIER,
        GF_DEFRAG_CMD_DETACH_START,
        GF_DEFRAG_CMD_DETACH_COMMIT,
        GF_DEFRAG_CMD_DETACH_COMMIT_FORCE,
        GF_DEFRAG_CMD_DETACH_STOP,
        GF_DEFRAG_CMD_TYPE_MAX
};

 enum gf_defrag_status_t {
        GF_DEFRAG_STATUS_NOT_STARTED,
        GF_DEFRAG_STATUS_STARTED,
        GF_DEFRAG_STATUS_STOPPED,
        GF_DEFRAG_STATUS_COMPLETE,
        GF_DEFRAG_STATUS_FAILED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_STOPPED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE,
        GF_DEFRAG_STATUS_LAYOUT_FIX_FAILED,
        GF_DEFRAG_STATUS_MAX
};

enum gf1_cluster_type {
        GF_CLUSTER_TYPE_NONE = 0,
        GF_CLUSTER_TYPE_STRIPE,
        GF_CLUSTER_TYPE_REPLICATE,
        GF_CLUSTER_TYPE_STRIPE_REPLICATE,
        GF_CLUSTER_TYPE_DISPERSE,
        GF_CLUSTER_TYPE_TIER,
        GF_CLUSTER_TYPE_MAX
};

enum gf_bitrot_type {
        GF_BITROT_OPTION_TYPE_NONE = 0,
        GF_BITROT_OPTION_TYPE_ENABLE,
        GF_BITROT_OPTION_TYPE_DISABLE,
        GF_BITROT_OPTION_TYPE_SCRUB_THROTTLE,
        GF_BITROT_OPTION_TYPE_SCRUB_FREQ,
        GF_BITROT_OPTION_TYPE_SCRUB,
        GF_BITROT_OPTION_TYPE_EXPIRY_TIME,
        GF_BITROT_CMD_SCRUB_STATUS,
        GF_BITROT_CMD_SCRUB_ONDEMAND,
        GF_BITROT_OPTION_TYPE_MAX
};

 enum gf1_op_commands {
        GF_OP_CMD_NONE = 0,
        GF_OP_CMD_START,
        GF_OP_CMD_COMMIT,
        GF_OP_CMD_STOP,
        GF_OP_CMD_STATUS,
        GF_OP_CMD_COMMIT_FORCE,
        GF_OP_CMD_DETACH_START,
        GF_OP_CMD_DETACH_COMMIT,
        GF_OP_CMD_DETACH_COMMIT_FORCE,
        GF_OP_CMD_STOP_DETACH_TIER
};

enum gf_quota_type {
        GF_QUOTA_OPTION_TYPE_NONE = 0,
        GF_QUOTA_OPTION_TYPE_ENABLE,
        GF_QUOTA_OPTION_TYPE_DISABLE,
        GF_QUOTA_OPTION_TYPE_LIMIT_USAGE,
        GF_QUOTA_OPTION_TYPE_REMOVE,
        GF_QUOTA_OPTION_TYPE_LIST,
        GF_QUOTA_OPTION_TYPE_VERSION,
        GF_QUOTA_OPTION_TYPE_ALERT_TIME,
        GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT,
        GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT,
        GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT,
        GF_QUOTA_OPTION_TYPE_VERSION_OBJECTS,
        GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS,
        GF_QUOTA_OPTION_TYPE_LIST_OBJECTS,
        GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS,
        GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS,
        GF_QUOTA_OPTION_TYPE_UPGRADE,
        GF_QUOTA_OPTION_TYPE_MAX
};

enum gf1_cli_friends_list {
        GF_CLI_LIST_PEERS = 1,
        GF_CLI_LIST_POOL_NODES = 2
};

enum gf1_cli_get_volume {
        GF_CLI_GET_VOLUME_ALL = 1,
        GF_CLI_GET_VOLUME,
        GF_CLI_GET_NEXT_VOLUME
};

enum gf1_cli_sync_volume {
        GF_CLI_SYNC_ALL = 1
};

enum gf1_cli_op_flags {
        GF_CLI_FLAG_OP_FORCE = 1
};

enum gf1_cli_gsync_set {
        GF_GSYNC_OPTION_TYPE_NONE,
        GF_GSYNC_OPTION_TYPE_START,
        GF_GSYNC_OPTION_TYPE_STOP,
        GF_GSYNC_OPTION_TYPE_CONFIG,
        GF_GSYNC_OPTION_TYPE_STATUS,
        GF_GSYNC_OPTION_TYPE_ROTATE,
        GF_GSYNC_OPTION_TYPE_CREATE,
        GF_GSYNC_OPTION_TYPE_DELETE,
        GF_GSYNC_OPTION_TYPE_PAUSE,
        GF_GSYNC_OPTION_TYPE_RESUME
};

enum gf1_cli_stats_op {
        GF_CLI_STATS_NONE  = 0,
        GF_CLI_STATS_START = 1,
        GF_CLI_STATS_STOP  = 2,
        GF_CLI_STATS_INFO  = 3,
        GF_CLI_STATS_TOP = 4
};

enum gf1_cli_info_op {
        GF_CLI_INFO_NONE = 0,
        GF_CLI_INFO_ALL = 1,
        GF_CLI_INFO_INCREMENTAL = 2,
        GF_CLI_INFO_CUMULATIVE = 3,
        GF_CLI_INFO_CLEAR = 4
};

enum gf1_cli_top_op {
        GF_CLI_TOP_NONE = 0,
        GF_CLI_TOP_OPEN,
        GF_CLI_TOP_READ,
        GF_CLI_TOP_WRITE,
        GF_CLI_TOP_OPENDIR,
        GF_CLI_TOP_READDIR,
        GF_CLI_TOP_READ_PERF,
        GF_CLI_TOP_WRITE_PERF
};

/* The unconventional hex numbers help us perform
   bit-wise operations which reduces complexity */
enum gf_cli_status_type {
        GF_CLI_STATUS_NONE         = 0x000000,
        GF_CLI_STATUS_MEM          = 0x000001,    /*000000000000001*/
        GF_CLI_STATUS_CLIENTS      = 0x000002,    /*000000000000010*/
        GF_CLI_STATUS_INODE        = 0x000004,    /*000000000000100*/
        GF_CLI_STATUS_FD           = 0x000008,    /*000000000001000*/
        GF_CLI_STATUS_CALLPOOL     = 0x000010,    /*000000000010000*/
        GF_CLI_STATUS_DETAIL       = 0x000020,    /*000000000100000*/
        GF_CLI_STATUS_TASKS        = 0x000040,    /*00000001000000*/
        GF_CLI_STATUS_MASK         = 0x0000FF,    /*000000011111111 Used to get the op*/
        GF_CLI_STATUS_VOL          = 0x000100,    /*00000000100000000*/
        GF_CLI_STATUS_ALL          = 0x000200,    /*00000001000000000*/
        GF_CLI_STATUS_BRICK        = 0x000400,    /*00000010000000000*/
        GF_CLI_STATUS_NFS          = 0x000800,    /*00000100000000000*/
        GF_CLI_STATUS_SHD          = 0x001000,    /*00001000000000000*/
        GF_CLI_STATUS_QUOTAD       = 0x002000,    /*00010000000000000*/
        GF_CLI_STATUS_SNAPD        = 0x004000,    /*00100000000000000*/
        GF_CLI_STATUS_BITD         = 0x008000,    /*01000000000000000*/
        GF_CLI_STATUS_SCRUB        = 0x010000,    /*10000000000000000*/
        GF_CLI_STATUS_TIERD        = 0x020000     /*100000000000000000*/
};

/* Identifiers for snapshot clis */
enum gf1_cli_snapshot {
        GF_SNAP_OPTION_TYPE_NONE = 0,
        GF_SNAP_OPTION_TYPE_CREATE,
        GF_SNAP_OPTION_TYPE_DELETE,
        GF_SNAP_OPTION_TYPE_RESTORE,
        GF_SNAP_OPTION_TYPE_ACTIVATE,
        GF_SNAP_OPTION_TYPE_DEACTIVATE,
        GF_SNAP_OPTION_TYPE_LIST,
        GF_SNAP_OPTION_TYPE_STATUS,
        GF_SNAP_OPTION_TYPE_CONFIG,
        GF_SNAP_OPTION_TYPE_CLONE,
        GF_SNAP_OPTION_TYPE_INFO
};

enum gf1_cli_snapshot_info {
        GF_SNAP_INFO_TYPE_ALL = 0,
        GF_SNAP_INFO_TYPE_SNAP,
        GF_SNAP_INFO_TYPE_VOL
};

enum gf1_cli_snapshot_config {
        GF_SNAP_CONFIG_TYPE_NONE = 0,
        GF_SNAP_CONFIG_TYPE_SET,
        GF_SNAP_CONFIG_DISPLAY
};

enum  gf1_cli_snapshot_status {
        GF_SNAP_STATUS_TYPE_ALL = 0,
        GF_SNAP_STATUS_TYPE_SNAP,
        GF_SNAP_STATUS_TYPE_VOL,
        GF_SNAP_STATUS_TYPE_ITER
};

/* Changing order of GF_SNAP_DELETE_TYPE_VOL           *
 * and GF_SNAP_DELETE_TYPE_SNAP so that they don't     *
 * overlap with the enums of GF_SNAP_STATUS_TYPE_SNAP, *
 * and GF_SNAP_STATUS_TYPE_VOL                         *
 */
enum gf1_cli_snapshot_delete {
        GF_SNAP_DELETE_TYPE_ALL  = 0,
        GF_SNAP_DELETE_TYPE_VOL  = 1,
        GF_SNAP_DELETE_TYPE_SNAP = 2,
        GF_SNAP_DELETE_TYPE_ITER = 3
};

struct gf_cli_req {
        opaque  dict<>;
};

 struct gf_cli_rsp {
        int     op_ret;
        int     op_errno;
        string  op_errstr<>;
        opaque  dict<>;
};

struct gf1_cli_peer_list_req {
        int     flags;
        opaque  dict<>;
};

struct gf1_cli_peer_list_rsp {
        int     op_ret;
        int     op_errno;
        opaque  friends<>;
};

struct gf1_cli_fsm_log_req {
        string name<>;
};

struct gf1_cli_fsm_log_rsp {
        int op_ret;
        int op_errno;
        string op_errstr<>;
        opaque fsm_log<>;
};

struct gf1_cli_getwd_req {
        int     unused;
};

struct gf1_cli_getwd_rsp {
        int     op_ret;
        int     op_errno;
        string  wd<>;
};

struct gf1_cli_mount_req {
        string label<>;
        opaque dict<>;
};

struct gf1_cli_mount_rsp {
       int op_ret;
       int op_errno;
       string path<>;
};

struct gf1_cli_umount_req {
        int lazy;
        string path<>;
};

struct gf1_cli_umount_rsp {
       int op_ret;
       int op_errno;
};
