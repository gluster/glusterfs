/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _PROTOCOL_COMMON_H
#define _PROTOCOL_COMMON_H

enum gf_fop_procnum {
        GFS3_OP_NULL,    /* 0 */
        GFS3_OP_STAT,
        GFS3_OP_READLINK,
        GFS3_OP_MKNOD,
        GFS3_OP_MKDIR,
        GFS3_OP_UNLINK,
        GFS3_OP_RMDIR,
        GFS3_OP_SYMLINK,
        GFS3_OP_RENAME,
        GFS3_OP_LINK,
        GFS3_OP_TRUNCATE,
        GFS3_OP_OPEN,
        GFS3_OP_READ,
        GFS3_OP_WRITE,
        GFS3_OP_STATFS,
        GFS3_OP_FLUSH,
        GFS3_OP_FSYNC,
        GFS3_OP_SETXATTR,
        GFS3_OP_GETXATTR,
        GFS3_OP_REMOVEXATTR,
        GFS3_OP_OPENDIR,
        GFS3_OP_FSYNCDIR,
        GFS3_OP_ACCESS,
        GFS3_OP_CREATE,
        GFS3_OP_FTRUNCATE,
        GFS3_OP_FSTAT,
        GFS3_OP_LK,
        GFS3_OP_LOOKUP,
        GFS3_OP_READDIR,
        GFS3_OP_INODELK,
        GFS3_OP_FINODELK,
	GFS3_OP_ENTRYLK,
	GFS3_OP_FENTRYLK,
        GFS3_OP_XATTROP,
        GFS3_OP_FXATTROP,
        GFS3_OP_FGETXATTR,
        GFS3_OP_FSETXATTR,
        GFS3_OP_RCHECKSUM,
        GFS3_OP_SETATTR,
        GFS3_OP_FSETATTR,
        GFS3_OP_READDIRP,
        GFS3_OP_RELEASE,
        GFS3_OP_RELEASEDIR,
        GFS3_OP_FREMOVEXATTR,
	GFS3_OP_FALLOCATE,
	GFS3_OP_DISCARD,
        GFS3_OP_ZEROFILL,
        GFS3_OP_IPC,
        GFS3_OP_SEEK,
        GFS3_OP_COMPOUND,
        GFS3_OP_LEASE,
        GFS3_OP_GETACTIVELK,
        GFS3_OP_SETACTIVELK,
        GFS3_OP_MAXVALUE,
};

enum gf_handshake_procnum {
        GF_HNDSK_NULL,
        GF_HNDSK_SETVOLUME,
        GF_HNDSK_GETSPEC,
        GF_HNDSK_PING,
        GF_HNDSK_SET_LK_VER,
        GF_HNDSK_EVENT_NOTIFY,
        GF_HNDSK_GET_VOLUME_INFO,
        GF_HNDSK_GET_SNAPSHOT_INFO,
        GF_HNDSK_MAXVALUE,
};

enum gf_pmap_procnum {
        GF_PMAP_NULL = 0,
        GF_PMAP_PORTBYBRICK,
        GF_PMAP_BRICKBYPORT,
        GF_PMAP_SIGNUP,
        GF_PMAP_SIGNIN,
        GF_PMAP_SIGNOUT,
        GF_PMAP_MAXVALUE,
};

enum gf_aggregator_procnum {
        GF_AGGREGATOR_NULL = 0,
        GF_AGGREGATOR_LOOKUP,
        GF_AGGREGATOR_GETLIMIT,
        GF_AGGREGATOR_MAXVALUE,
};

enum gf_pmap_port_type {
        GF_PMAP_PORT_FREE = 0,
        GF_PMAP_PORT_FOREIGN,     /* it actually means, not sure who is using it, but it is in-use */
        GF_PMAP_PORT_LEASED,
        GF_PMAP_PORT_NONE,
        GF_PMAP_PORT_BRICKSERVER, /* port used by brick process */
};
typedef enum gf_pmap_port_type gf_pmap_port_type_t;

enum gf_probe_resp {
	GF_PROBE_SUCCESS,
	GF_PROBE_LOCALHOST,
	GF_PROBE_FRIEND,
        GF_PROBE_ANOTHER_CLUSTER,
        GF_PROBE_VOLUME_CONFLICT,
        GF_PROBE_SAME_UUID,
        GF_PROBE_UNKNOWN_PEER,
        GF_PROBE_ADD_FAILED,
        GF_PROBE_QUORUM_NOT_MET,
        GF_PROBE_MISSED_SNAP_CONFLICT,
        GF_PROBE_SNAP_CONFLICT,
        GF_PROBE_FRIEND_DETACHING,
};

enum gf_deprobe_resp {
        GF_DEPROBE_SUCCESS,
        GF_DEPROBE_LOCALHOST,
        GF_DEPROBE_NOT_FRIEND,
        GF_DEPROBE_BRICK_EXIST,
        GF_DEPROBE_FRIEND_DOWN,
        GF_DEPROBE_QUORUM_NOT_MET,
        GF_DEPROBE_FRIEND_DETACHING,
};

enum gf_cbk_procnum {
        GF_CBK_NULL = 0,
        GF_CBK_FETCHSPEC,
        GF_CBK_INO_FLUSH,
        GF_CBK_EVENT_NOTIFY,
        GF_CBK_GET_SNAPS,
        GF_CBK_CACHE_INVALIDATION,
        GF_CBK_CHILD_UP,
        GF_CBK_CHILD_DOWN,
        GF_CBK_RECALL_LEASE,
        GF_CBK_MAXVALUE,
};

enum gluster_cli_procnum {
        GLUSTER_CLI_NULL,    /* 0 */
        GLUSTER_CLI_PROBE,
        GLUSTER_CLI_DEPROBE,
        GLUSTER_CLI_LIST_FRIENDS,
        GLUSTER_CLI_CREATE_VOLUME,
        GLUSTER_CLI_GET_VOLUME,
        GLUSTER_CLI_GET_NEXT_VOLUME,
        GLUSTER_CLI_DELETE_VOLUME,
        GLUSTER_CLI_START_VOLUME,
        GLUSTER_CLI_STOP_VOLUME,
        GLUSTER_CLI_RENAME_VOLUME,
        GLUSTER_CLI_DEFRAG_VOLUME,
        GLUSTER_CLI_SET_VOLUME,
        GLUSTER_CLI_ADD_BRICK,
        GLUSTER_CLI_REMOVE_BRICK,
        GLUSTER_CLI_REPLACE_BRICK,
        GLUSTER_CLI_LOG_ROTATE,
        GLUSTER_CLI_GETSPEC,
        GLUSTER_CLI_PMAP_PORTBYBRICK,
        GLUSTER_CLI_SYNC_VOLUME,
        GLUSTER_CLI_RESET_VOLUME,
        GLUSTER_CLI_FSM_LOG,
        GLUSTER_CLI_GSYNC_SET,
        GLUSTER_CLI_PROFILE_VOLUME,
        GLUSTER_CLI_QUOTA,
        GLUSTER_CLI_TOP_VOLUME,
        GLUSTER_CLI_GETWD,
        GLUSTER_CLI_STATUS_VOLUME,
        GLUSTER_CLI_STATUS_ALL,
        GLUSTER_CLI_MOUNT,
        GLUSTER_CLI_UMOUNT,
        GLUSTER_CLI_HEAL_VOLUME,
        GLUSTER_CLI_STATEDUMP_VOLUME,
        GLUSTER_CLI_LIST_VOLUME,
        GLUSTER_CLI_CLRLOCKS_VOLUME,
        GLUSTER_CLI_UUID_RESET,
        GLUSTER_CLI_UUID_GET,
        GLUSTER_CLI_COPY_FILE,
        GLUSTER_CLI_SYS_EXEC,
        GLUSTER_CLI_SNAP,
        GLUSTER_CLI_BARRIER_VOLUME,
        GLUSTER_CLI_GET_VOL_OPT,
        GLUSTER_CLI_GANESHA,
        GLUSTER_CLI_BITROT,
        GLUSTER_CLI_ATTACH_TIER,
        GLUSTER_CLI_DETACH_TIER,
        GLUSTER_CLI_TIER,
        GLUSTER_CLI_MAXVALUE,
};

enum glusterd_mgmt_procnum {
        GLUSTERD_MGMT_NULL,    /* 0 */
        GLUSTERD_MGMT_CLUSTER_LOCK,
        GLUSTERD_MGMT_CLUSTER_UNLOCK,
        GLUSTERD_MGMT_STAGE_OP,
        GLUSTERD_MGMT_COMMIT_OP,
        GLUSTERD_MGMT_MAXVALUE,
};

enum glusterd_friend_procnum {
        GLUSTERD_FRIEND_NULL,    /* 0 */
        GLUSTERD_PROBE_QUERY,
        GLUSTERD_FRIEND_ADD,
        GLUSTERD_FRIEND_REMOVE,
        GLUSTERD_FRIEND_UPDATE,
        GLUSTERD_FRIEND_MAXVALUE,
};

enum glusterd_brick_procnum {
        GLUSTERD_BRICK_NULL,    /* 0 */
        GLUSTERD_BRICK_TERMINATE,
        GLUSTERD_BRICK_XLATOR_INFO,
        GLUSTERD_BRICK_XLATOR_OP,
        GLUSTERD_BRICK_STATUS,
        GLUSTERD_BRICK_OP,
        GLUSTERD_BRICK_XLATOR_DEFRAG,
        GLUSTERD_NODE_PROFILE,
        GLUSTERD_NODE_STATUS,
        GLUSTERD_VOLUME_BARRIER_OP,
        GLUSTERD_BRICK_BARRIER,
        GLUSTERD_NODE_BITROT,
        GLUSTERD_BRICK_MAXVALUE,
};

enum glusterd_mgmt_hndsk_procnum {
        GD_MGMT_HNDSK_NULL,
        GD_MGMT_HNDSK_VERSIONS,
        GD_MGMT_HNDSK_VERSIONS_ACK,
        GD_MGMT_HNDSK_MAXVALUE,
};

typedef enum {
        GF_SHD_OP_INVALID,
        GF_SHD_OP_HEAL_INDEX,
        GF_SHD_OP_HEAL_FULL,
        GF_SHD_OP_INDEX_SUMMARY,
        GF_SHD_OP_HEALED_FILES,
        GF_SHD_OP_HEAL_FAILED_FILES,
        GF_SHD_OP_SPLIT_BRAIN_FILES,
        GF_SHD_OP_STATISTICS,
        GF_SHD_OP_STATISTICS_HEAL_COUNT,
        GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA,
        GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE,
        GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK,
        GF_SHD_OP_HEAL_ENABLE,
        GF_SHD_OP_HEAL_DISABLE,
        GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME,
        GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE,
        GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE,
} gf_xl_afr_op_t ;

struct gf_gsync_detailed_status_ {
        char node[NAME_MAX];
        char master[NAME_MAX];
        char brick[NAME_MAX];
        char slave_user[NAME_MAX];
        char slave[NAME_MAX];
        char slave_node[NAME_MAX];
        char worker_status[NAME_MAX];
        char crawl_status[NAME_MAX];
        char last_synced[NAME_MAX];
        char last_synced_utc[NAME_MAX];
        char entry[NAME_MAX];
        char data[NAME_MAX];
        char meta[NAME_MAX];
        char failures[NAME_MAX];
        char checkpoint_time[NAME_MAX];
        char checkpoint_time_utc[NAME_MAX];
        char checkpoint_completed[NAME_MAX];
        char checkpoint_completion_time[NAME_MAX];
        char checkpoint_completion_time_utc[NAME_MAX];
        char brick_host_uuid[NAME_MAX];
        char slavekey[NAME_MAX];
        char session_slave[NAME_MAX];
};

enum glusterd_mgmt_v3_procnum {
        GLUSTERD_MGMT_V3_NULL,    /* 0 */
        GLUSTERD_MGMT_V3_LOCK,
        GLUSTERD_MGMT_V3_PRE_VALIDATE,
        GLUSTERD_MGMT_V3_BRICK_OP,
        GLUSTERD_MGMT_V3_COMMIT,
        GLUSTERD_MGMT_V3_POST_VALIDATE,
        GLUSTERD_MGMT_V3_UNLOCK,
        GLUSTERD_MGMT_V3_MAXVALUE,
};

typedef struct gf_gsync_detailed_status_ gf_gsync_status_t;

enum gf_get_volume_info_type {
        GF_GET_VOLUME_NONE,    /* 0 */
        GF_GET_VOLUME_UUID
};

typedef enum gf_get_volume_info_type gf_get_volume_info_type;


enum gf_get_snapshot_info_type {
        GF_GET_SNAPSHOT_LIST,
};
typedef enum gf_get_snapshot_info_type gf_get_snapshot_info_type;

#define GLUSTER_HNDSK_PROGRAM    14398633 /* Completely random */
#define GLUSTER_HNDSK_VERSION    2   /* 0.0.2 */

#define GLUSTER_PMAP_PROGRAM     34123456
#define GLUSTER_PMAP_VERSION     1

#define GLUSTER_CBK_PROGRAM      52743234 /* Completely random */
#define GLUSTER_CBK_VERSION      1   /* 0.0.1 */

#define GLUSTER_FOP_PROGRAM   1298437 /* Completely random */
#define GLUSTER_FOP_VERSION   330 /* 3.3.0 */
#define GLUSTER_FOP_PROCCNT   GFS3_OP_MAXVALUE

/* Aggregator */
#define GLUSTER_AGGREGATOR_PROGRAM 29852134 /* Completely random */
#define GLUSTER_AGGREGATOR_VERSION 1

/* Second version */
#define GD_MGMT_PROGRAM          1238433 /* Completely random */
#define GD_MGMT_VERSION          2   /* 0.0.2 */

#define GD_FRIEND_PROGRAM        1238437 /* Completely random */
#define GD_FRIEND_VERSION        2  /* 0.0.2 */

#define GLUSTER_CLI_PROGRAM      1238463 /* Completely random */
#define GLUSTER_CLI_VERSION      2   /* 0.0.2 */

#define GD_BRICK_PROGRAM         4867634 /*Completely random*/
#define GD_BRICK_VERSION         2

/* Third version */
#define GD_MGMT_V3_VERSION       3

/* OP-VERSION handshake */
#define GD_MGMT_HNDSK_PROGRAM    1239873 /* Completely random */
#define GD_MGMT_HNDSK_VERSION    1

#define GD_VOLUME_NAME_MAX 256 /* Maximum size of volume name */

#endif /* !_PROTOCOL_COMMON_H */
