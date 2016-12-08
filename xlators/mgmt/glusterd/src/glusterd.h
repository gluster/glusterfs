/*
   Copyright (c) 2006-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_H_
#define _GLUSTERD_H_

#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <libgen.h>

#include "compat-uuid.h"

#include "rpc-clnt.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd-mem-types.h"
#include "rpcsvc.h"
#include "glusterd-sm.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-tierd-svc.h"
#include "glusterd-bitd-svc.h"
#include "glusterd1-xdr.h"
#include "protocol-common.h"
#include "glusterd-pmap.h"
#include "cli1-xdr.h"
#include "syncop.h"
#include "store.h"
#include "glusterd-rcu.h"
#include "events.h"

#define GLUSTERD_TR_LOG_SIZE            50
#define GLUSTERD_SOCKET_LISTEN_BACKLOG  128
#define GLUSTERD_QUORUM_TYPE_KEY        "cluster.server-quorum-type"
#define GLUSTERD_QUORUM_RATIO_KEY       "cluster.server-quorum-ratio"
#define GLUSTERD_GLOBAL_OPT_VERSION     "global-option-version"
#define GLUSTERD_GLOBAL_OP_VERSION_KEY  "cluster.op-version"
#define GLUSTERD_MAX_OP_VERSION_KEY     "cluster.max-op-version"
#define GLUSTERD_COMMON_PEM_PUB_FILE    "/geo-replication/common_secret.pem.pub"
#define GEO_CONF_MAX_OPT_VALS           6
#define GLUSTERD_CREATE_HOOK_SCRIPT     "/hooks/1/gsync-create/post/" \
                                        "S56glusterd-geo-rep-create-post.sh"
#define GLUSTERD_SHRD_STRG_HOOK_SCRIPT  "/hooks/1/set/post/" \
                                        "S32gluster_enable_shared_storage.sh"
#define GLUSTER_SHARED_STORAGE          "gluster_shared_storage"
#define GLUSTERD_SHARED_STORAGE_KEY     "cluster.enable-shared-storage"
#define GLUSTERD_BRICK_MULTIPLEX_KEY    "cluster.brick-multiplex"

#define GANESHA_HA_CONF  CONFDIR "/ganesha-ha.conf"
#define GANESHA_EXPORT_DIRECTORY        CONFDIR"/exports"
#define GLUSTERD_SNAPS_MAX_HARD_LIMIT 256
#define GLUSTERD_SNAPS_DEF_SOFT_LIMIT_PERCENT 90
#define GLUSTERD_SNAPS_MAX_SOFT_LIMIT_PERCENT 100
#define GLUSTERD_SERVER_QUORUM "server"
#define STATUS_STRLEN   128

#define FMTSTR_CHECK_VOL_EXISTS "Volume %s does not exist"
#define FMTSTR_RESOLVE_BRICK "Could not find peer on which brick %s:%s resides"

#define LOGSTR_FOUND_BRICK  "Found brick %s:%s in volume %s"
#define LOGSTR_BUILD_PAYLOAD "Failed to build payload for operation 'Volume %s'"
#define LOGSTR_STAGE_FAIL "Staging of operation 'Volume %s' failed on %s %s %s"
#define LOGSTR_COMMIT_FAIL "Commit of operation 'Volume %s' failed on %s %s %s"

#define OPERRSTR_BUILD_PAYLOAD "Failed to build payload. Please check the log "\
                               "file for more details."
#define OPERRSTR_STAGE_FAIL "Staging failed on %s. Please check the log file " \
                            "for more details."
#define OPERRSTR_COMMIT_FAIL "Commit failed on %s. Please check the log file "\
                             "for more details."
struct glusterd_volinfo_;
typedef struct glusterd_volinfo_ glusterd_volinfo_t;

struct glusterd_snap_;
typedef struct glusterd_snap_ glusterd_snap_t;

/* For every new feature please add respective enum of new feature
 * at the end of latest enum (just before the GD_OP_MAX enum)
 */
typedef enum glusterd_op_ {
        GD_OP_NONE = 0,
        GD_OP_CREATE_VOLUME,
        GD_OP_START_BRICK,
        GD_OP_STOP_BRICK,
        GD_OP_DELETE_VOLUME,
        GD_OP_START_VOLUME,
        GD_OP_STOP_VOLUME,
        GD_OP_DEFRAG_VOLUME,
        GD_OP_ADD_BRICK,
        GD_OP_REMOVE_BRICK,
        GD_OP_REPLACE_BRICK,
        GD_OP_SET_VOLUME,
        GD_OP_RESET_VOLUME,
        GD_OP_SYNC_VOLUME,
        GD_OP_LOG_ROTATE,
        GD_OP_GSYNC_SET,
        GD_OP_PROFILE_VOLUME,
        GD_OP_QUOTA,
        GD_OP_STATUS_VOLUME,
        GD_OP_REBALANCE,
        GD_OP_HEAL_VOLUME,
        GD_OP_STATEDUMP_VOLUME,
        GD_OP_LIST_VOLUME,
        GD_OP_CLEARLOCKS_VOLUME,
        GD_OP_DEFRAG_BRICK_VOLUME,
        GD_OP_COPY_FILE,
        GD_OP_SYS_EXEC,
        GD_OP_GSYNC_CREATE,
        GD_OP_SNAP,
        GD_OP_BARRIER,
        GD_OP_GANESHA,
        GD_OP_BITROT,
        GD_OP_DETACH_TIER,
        GD_OP_TIER_MIGRATE,
        GD_OP_SCRUB_STATUS,
        GD_OP_SCRUB_ONDEMAND,
        GD_OP_RESET_BRICK,
        GD_OP_MAX_OPVERSION,
        GD_OP_TIER_START_STOP,
        GD_OP_TIER_STATUS,
        GD_OP_DETACH_TIER_STATUS,
        GD_OP_DETACH_NOT_STARTED,
        GD_OP_REMOVE_TIER_BRICK,
        GD_OP_MAX,
} glusterd_op_t;

extern const char * gd_op_list[];

struct glusterd_volgen {
        dict_t *dict;
};

typedef struct {
        struct _volfile_ctx     *volfile;
        pthread_mutex_t          mutex;
        struct cds_list_head     peers;
        gf_boolean_t             verify_volfile_checksum;
        gf_boolean_t             trace;
        uuid_t                   uuid;
        char                     workdir[PATH_MAX];
        rpcsvc_t                *rpc;
        glusterd_svc_t           shd_svc;
        glusterd_svc_t           nfs_svc;
        glusterd_svc_t           bitd_svc;
        glusterd_svc_t           scrub_svc;
        glusterd_svc_t           quotad_svc;
        struct pmap_registry    *pmap;
        struct cds_list_head     volumes;
        struct cds_list_head     snapshots; /*List of snap volumes */
        pthread_mutex_t          xprt_lock;
        struct list_head         xprt_list;
        gf_store_handle_t       *handle;
        gf_timer_t              *timer;
        glusterd_sm_tr_log_t     op_sm_log;
        struct rpc_clnt_program *gfs_mgmt;
        dict_t                  *mgmt_v3_lock; /* Dict for saving
                                                * mgmt_v3 locks */
        dict_t                  *glusterd_txn_opinfo; /* Dict for saving
                                                       * transaction opinfos */
        uuid_t                   global_txn_id; /* To be used in
                                                 * heterogeneous
                                                 * cluster with no
                                                 * transaction ids */

        struct cds_list_head       mount_specs;
        gf_boolean_t               valgrind;
        pthread_t                  brick_thread;
        void                      *hooks_priv;

        /* need for proper handshake_t */
        int                        op_version; /* Starts with 1 for 3.3.0 */
        xlator_t                  *xl;  /* Should be set to 'THIS' before creating thread */
        gf_boolean_t               pending_quorum_action;
        dict_t                    *opts;
        synclock_t                 big_lock;
        gf_boolean_t               restart_done;
        rpcsvc_t                  *uds_rpc; /* RPCSVC for the unix domain socket */
        uint32_t                   base_port;
        char                      *snap_bricks_directory;
        gf_store_handle_t         *missed_snaps_list_shandle;
        struct cds_list_head       missed_snaps_list;
        int                        ping_timeout;
        uint32_t                   generation;
        int32_t                    workers;
} glusterd_conf_t;


typedef enum gf_brick_status {
        GF_BRICK_STOPPED,
        GF_BRICK_STARTED,
        GF_BRICK_STOPPING,
} gf_brick_status_t;

struct glusterd_brickinfo {
        char               hostname[1024];
        char               path[PATH_MAX];
        char               real_path[PATH_MAX];
        char               device_path[PATH_MAX];
        char               mount_dir[PATH_MAX];
        char               brick_id[1024];/*Client xlator name, AFR changelog name*/
        char               fstype [NAME_MAX]; /* Brick file-system type */
        char               mnt_opts [1024]; /* Brick mount options */
        struct cds_list_head   brick_list;
        uuid_t             uuid;
        int                port;
        int                rdma_port;
        char              *logfile;
        gf_store_handle_t *shandle;
        gf_brick_status_t  status;
        struct rpc_clnt   *rpc;
        int                decommissioned;
        char vg[PATH_MAX]; /* FIXME: Use max size for length of vg */
        int     caps; /* Capability */
        int32_t            snap_status;
        /*
         * The group is used to identify which bricks are part of the same
         * replica set during brick-volfile generation, so that JBR volfiles
         * can "cross-connect" the bricks to one another. It is also used by
         * AFR to load the arbiter xlator in the appropriate brick in case of
         * a replica 3 volume with arbiter enabled.
         */
        uint16_t           group;
        uuid_t             jbr_uuid;
        gf_boolean_t       started_here;
};

typedef struct glusterd_brickinfo glusterd_brickinfo_t;

struct gf_defrag_brickinfo_ {
        char *name;
        int   files;
        int   size;
};

typedef int (*defrag_cbk_fn_t) (glusterd_volinfo_t *volinfo,
                                gf_defrag_status_t status);

struct glusterd_defrag_info_ {
        uint64_t                     total_files;
        uint64_t                     total_data;
        uint64_t                     num_files_lookedup;
        uint64_t                     total_failures;
        gf_lock_t                    lock;
        int                          cmd;
        pthread_t                    th;
        gf_defrag_status_t           defrag_status;
        struct rpc_clnt             *rpc;
        uint32_t                     connected;
        char                         mount[1024];
        char                         databuf[131072];
        struct gf_defrag_brickinfo_ *bricks; /* volinfo->brick_count */

        defrag_cbk_fn_t              cbk_fn;
};


typedef struct glusterd_defrag_info_ glusterd_defrag_info_t;

typedef enum gf_transport_type_ {
        GF_TRANSPORT_TCP,       //DEFAULT
        GF_TRANSPORT_RDMA,
        GF_TRANSPORT_BOTH_TCP_RDMA,
} gf_transport_type;


typedef enum gf_rb_status_ {
        GF_RB_STATUS_NONE,
        GF_RB_STATUS_STARTED,
        GF_RB_STATUS_PAUSED,
} gf_rb_status_t;

struct _auth {
        char       *username;
        char       *password;
};

typedef struct _auth auth_t;

/* Capabilities of xlator */
#define CAPS_BD               0x00000001
#define CAPS_THIN             0x00000002
#define CAPS_OFFLOAD_COPY     0x00000004
#define CAPS_OFFLOAD_SNAPSHOT 0x00000008
#define CAPS_OFFLOAD_ZERO     0x00000020

struct glusterd_bitrot_scrub_ {
        char        *scrub_state;
        char        *scrub_impact;
        char        *scrub_freq;
        uint64_t    scrubbed_files;
        uint64_t    unsigned_files;
        uint64_t    last_scrub_time;
        uint64_t    scrub_duration;
        uint64_t    error_count;
};

typedef struct glusterd_bitrot_scrub_ glusterd_bitrot_scrub_t;

struct glusterd_rebalance_ {
        gf_defrag_status_t       defrag_status;
        uint64_t                 rebalance_files;
        uint64_t                 rebalance_data;
        uint64_t                 lookedup_files;
        uint64_t                 skipped_files;
        glusterd_defrag_info_t  *defrag;
        gf_cli_defrag_type       defrag_cmd;
        uint64_t                 rebalance_failures;
        uuid_t                   rebalance_id;
        double                   rebalance_time;
        uint64_t                 time_left;
        glusterd_op_t            op;
        dict_t                  *dict; /* Dict to store misc information
                                        * like list of bricks being removed */
        uint32_t                 commit_hash;
};

typedef struct glusterd_rebalance_ glusterd_rebalance_t;

struct glusterd_replace_brick_ {
        glusterd_brickinfo_t   *src_brick;
        glusterd_brickinfo_t   *dst_brick;
};

typedef struct glusterd_replace_brick_ glusterd_replace_brick_t;

typedef enum gd_quorum_status_ {
        NOT_APPLICABLE_QUORUM, //Does not follow quorum
        MEETS_QUORUM, //Follows quorum and meets.
        DOESNT_MEET_QUORUM, //Follows quorum and does not meet.
} gd_quorum_status_t;

typedef struct tier_info_ {
        int                       cold_type;
        int                       cold_brick_count;
        int                       cold_replica_count;
        int                       cold_disperse_count;
        int                       cold_dist_leaf_count;
        int                       cold_redundancy_count;
        int                       hot_type;
        int                       hot_brick_count;
        int                       hot_replica_count;
        int                       promoted;
        int                       demoted;
        uint16_t                  cur_tier_hot;
} gd_tier_info_t;

struct glusterd_volinfo_ {
        gf_lock_t                 lock;
        gf_boolean_t              is_snap_volume;
        glusterd_snap_t          *snapshot;
        uuid_t                    restored_from_snap;
        gd_tier_info_t            tier_info;
        gf_boolean_t              is_tier_enabled;
        char                      parent_volname[GD_VOLUME_NAME_MAX];
                                         /* In case of a snap volume
                                            i.e (is_snap_volume == TRUE) this
                                            field will contain the name of
                                            the volume which is snapped. In
                                            case of a non-snap volume, this
                                            field will be initialized as N/A */
        char                      volname[GD_VOLUME_NAME_MAX + 5];
                                        /* An extra 5 bytes are allocated.
                                         * Reason is, As part of the tiering
                                         * volfile generation code, we are
                                         * temporarily appending either hot
                                         * or cold */
        int                       type;
        int                       brick_count;
        uint64_t                  snap_count;
        uint64_t                  snap_max_hard_limit;
        struct cds_list_head      vol_list;
                                      /* In case of a snap volume
                                         i.e (is_snap_volume == TRUE) this
                                         is linked to glusterd_snap_t->volumes.
                                         In case of a non-snap volume, this is
                                         linked to glusterd_conf_t->volumes */
        struct cds_list_head      snapvol_list;
                                      /* This is a current pointer for
                                         glusterd_volinfo_t->snap_volumes */
        struct cds_list_head      bricks;
        struct cds_list_head      snap_volumes;
                                      /* TODO : Need to remove this, as this
                                       * is already part of snapshot object.
                                       */
        glusterd_volume_status    status;
        int                       sub_count;  /* backward compatibility */
        int                       stripe_count;
        int                       replica_count;
        int                       arbiter_count;
        int                       disperse_count;
        int                       redundancy_count;
        int                       subvol_count; /* Number of subvolumes in a
                                                 distribute volume */
        int                       dist_leaf_count; /* Number of bricks in one
                                                    distribute subvolume */
        int                       port;
        gf_store_handle_t        *shandle;
        gf_store_handle_t        *node_state_shandle;
        gf_store_handle_t        *quota_conf_shandle;

        /* Defrag/rebalance related */
        glusterd_rebalance_t      rebal;

        /* Replace brick status */
        glusterd_replace_brick_t  rep_brick;

        /* Bitrot scrub status*/
        glusterd_bitrot_scrub_t   bitrot_scrub;

        glusterd_rebalance_t      tier;

        int                       version;
        uint32_t                  quota_conf_version;
        uint32_t                  cksum;
        uint32_t                  quota_conf_cksum;
        gf_transport_type         transport_type;

        dict_t                   *dict;

        uuid_t                    volume_id;
        auth_t                    auth;
        char                     *logdir;

        dict_t                   *gsync_slaves;
        dict_t                   *gsync_active_slaves;

        int                       decommission_in_progress;
        xlator_t                 *xl;

        gf_boolean_t              memory_accounting;
        int                      caps; /* Capability */

        int                       op_version;
        int                       client_op_version;
        pthread_mutex_t           reflock;
        int                       refcnt;
        gd_quorum_status_t        quorum_status;

        glusterd_snapdsvc_t       snapd;
        glusterd_tierdsvc_t       tierd;
        int32_t                   quota_xattr_version;
};

typedef enum gd_snap_status_ {
        GD_SNAP_STATUS_NONE,
        GD_SNAP_STATUS_INIT,
        GD_SNAP_STATUS_IN_USE,
        GD_SNAP_STATUS_DECOMMISSION,
        GD_SNAP_STATUS_UNDER_RESTORE,
        GD_SNAP_STATUS_RESTORED,
} gd_snap_status_t;

struct glusterd_snap_ {
        gf_lock_t                lock;
        struct  cds_list_head    volumes;
        struct  cds_list_head    snap_list;
        char                     snapname[GLUSTERD_MAX_SNAP_NAME];
        uuid_t                   snap_id;
        char                    *description;
        time_t                   time_stamp;
        gf_boolean_t             snap_restored;
        gd_snap_status_t         snap_status;
        gf_store_handle_t       *shandle;
};

typedef struct glusterd_snap_op_ {
        char                  *snap_vol_id;
        int32_t                brick_num;
        char                  *brick_path;
        int32_t                op;
        int32_t                status;
        struct cds_list_head   snap_ops_list;
} glusterd_snap_op_t;

typedef struct glusterd_missed_snap_ {
        char                   *node_uuid;
        char                   *snap_uuid;
        struct cds_list_head    missed_snaps;
        struct cds_list_head    snap_ops;
} glusterd_missed_snap_info;

typedef enum gd_node_type_ {
        GD_NODE_NONE,
        GD_NODE_BRICK,
        GD_NODE_SHD,
        GD_NODE_REBALANCE,
        GD_NODE_NFS,
        GD_NODE_QUOTAD,
        GD_NODE_SNAPD,
        GD_NODE_BITD,
        GD_NODE_SCRUB,
        GD_NODE_TIERD
} gd_node_type;

typedef enum missed_snap_stat {
        GD_MISSED_SNAP_NONE,
        GD_MISSED_SNAP_PENDING,
        GD_MISSED_SNAP_DONE,
} missed_snap_stat;

typedef struct glusterd_pending_node_ {
        struct cds_list_head list;
        void   *node;
        gd_node_type type;
        int32_t index;
} glusterd_pending_node_t;

struct gsync_config_opt_vals_ {
        char           *op_name;
        int             no_of_pos_vals;
        gf_boolean_t    case_sensitive;
        char           *values[GEO_CONF_MAX_OPT_VALS];
};

enum glusterd_op_ret {
        GLUSTERD_CONNECTION_AWAITED = 100,
};

enum glusterd_vol_comp_status_ {
        GLUSTERD_VOL_COMP_NONE = 0,
        GLUSTERD_VOL_COMP_SCS = 1,
        GLUSTERD_VOL_COMP_UPDATE_REQ,
        GLUSTERD_VOL_COMP_RJT,
};

typedef struct addrinfo_list {
        struct cds_list_head list;
        struct addrinfo *info;
} addrinfo_list_t;

typedef enum {
        GF_AI_COMPARE_NO_MATCH     = 0,
        GF_AI_COMPARE_MATCH        = 1,
        GF_AI_COMPARE_ERROR        = 2
} gf_ai_compare_t;

#define GLUSTERD_DEFAULT_PORT    GF_DEFAULT_BASE_PORT
#define GLUSTERD_INFO_FILE      "glusterd.info"
#define GLUSTERD_VOLUME_QUOTA_CONFIG "quota.conf"
#define GLUSTERD_VOLUME_DIR_PREFIX "vols"
#define GLUSTERD_PEER_DIR_PREFIX "peers"
#define GLUSTERD_VOLUME_INFO_FILE "info"
#define GLUSTERD_VOLUME_SNAPD_INFO_FILE "snapd.info"
#define GLUSTERD_SNAP_INFO_FILE "info"
#define GLUSTERD_VOLUME_RBSTATE_FILE "rbstate"
#define GLUSTERD_BRICK_INFO_DIR "bricks"
#define GLUSTERD_CKSUM_FILE "cksum"
#define GLUSTERD_VOL_QUOTA_CKSUM_FILE "quota.cksum"
#define GLUSTERD_TRASH "trash"
#define GLUSTERD_NODE_STATE_FILE "node_state.info"
#define GLUSTERD_MISSED_SNAPS_LIST_FILE "missed_snaps_list"
#define GLUSTERD_VOL_SNAP_DIR_PREFIX "snaps"

#define GLUSTERD_DEFAULT_SNAPS_BRICK_DIR     "/gluster/snaps"
#define GLUSTER_SHARED_STORAGE_BRICK_DIR     GLUSTERD_DEFAULT_WORKDIR"/ss_brick"
#define GLUSTERD_VAR_RUN_DIR                 "/var/run"
#define GLUSTERD_RUN_DIR                     "/run"

/* definitions related to replace brick */
#define RB_CLIENT_MOUNTPOINT    "rb_mount"
#define RB_CLIENTVOL_FILENAME   "rb_client.vol"
#define RB_DSTBRICK_PIDFILE     "rb_dst_brick.pid"
#define RB_DSTBRICKVOL_FILENAME "rb_dst_brick.vol"
#define RB_PUMP_DEF_ARG         "default"

#define GLUSTERD_UUID_LEN 50

typedef ssize_t (*gd_serialize_t) (struct iovec outmsg, void *args);

#define GLUSTERD_GET_VOLUME_DIR(path, volinfo, priv)                       \
        if (volinfo->is_snap_volume) {                                     \
                snprintf (path, PATH_MAX, "%s/snaps/%s/%s", priv->workdir, \
                          volinfo->snapshot->snapname, volinfo->volname);  \
        } else {                                                           \
                snprintf (path, PATH_MAX, "%s/vols/%s", priv->workdir,     \
                          volinfo->volname);                               \
        }
#define GLUSTERD_GET_TIER_DIR(path, volinfo, priv) do {                 \
                snprintf (path, PATH_MAX, "%s/tier/%s", priv->workdir,  \
                          volinfo->volname);                            \
        } while (0)

#define GLUSTERD_GET_TIER_PID_FILE(path, volinfo, priv) do {            \
                char tier_path[PATH_MAX];                               \
                GLUSTERD_GET_TIER_DIR(tier_path, volinfo, priv);        \
                snprintf (path, PATH_MAX, "%s/run/%s-tierd.pid", tier_path,\
                          volinfo->volname);                            \
        } while (0)

#define GLUSTERD_GET_SNAP_DIR(path, snap, priv)                           \
                snprintf (path, PATH_MAX, "%s/snaps/%s", priv->workdir,   \
                          snap->snapname);

#define GLUSTERD_GET_SNAP_GEO_REP_DIR(path, snap, priv)                      \
                snprintf (path, PATH_MAX, "%s/snaps/%s/%s", priv->workdir,   \
                          snap->snapname, GEOREP);

#define GLUSTERD_GET_BRICK_DIR(path, volinfo, priv)                           \
        if (volinfo->is_snap_volume) {                                        \
                snprintf (path, PATH_MAX, "%s/snaps/%s/%s/%s", priv->workdir, \
                          volinfo->snapshot->snapname, volinfo->volname,      \
                          GLUSTERD_BRICK_INFO_DIR);                           \
        } else {                                                              \
                snprintf (path, PATH_MAX, "%s/%s/%s/%s", priv->workdir,       \
                          GLUSTERD_VOLUME_DIR_PREFIX, volinfo->volname,       \
                          GLUSTERD_BRICK_INFO_DIR);                           \
        }

#define GLUSTERD_GET_NFS_DIR(path, priv) \
        snprintf (path, PATH_MAX, "%s/nfs", priv->workdir);

#define GLUSTERD_GET_QUOTAD_DIR(path, priv) \
        snprintf (path, PATH_MAX, "%s/quotad", priv->workdir);

#define GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH(abspath, volname, path)      \
        snprintf (abspath, sizeof (abspath)-1,                          \
                  DEFAULT_VAR_RUN_DIRECTORY"/%s%s", volname, path);

#define GLUSTERD_GET_TMP_PATH(abspath, path) do {                       \
        snprintf (abspath, sizeof (abspath)-1,                          \
                  DEFAULT_VAR_RUN_DIRECTORY"/tmp%s", path);             \
        } while (0)

#define GLUSTERD_REMOVE_SLASH_FROM_PATH(path,string) do {                  \
                int i = 0;                                                 \
                for (i = 1; i < strlen (path); i++) {                      \
                        string[i-1] = path[i];                             \
                        if (string[i-1] == '/' && (i != strlen(path) - 1)) \
                                string[i-1] = '-';                         \
                }                                                          \
        } while (0)

#define GLUSTERD_GET_BRICK_PIDFILE(pidfile,volinfo,brickinfo, priv) do {      \
                char exp_path[PATH_MAX] = {0,};                               \
                char volpath[PATH_MAX]  = {0,};                               \
                GLUSTERD_GET_VOLUME_DIR (volpath, volinfo, priv);             \
                GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);  \
                snprintf (pidfile, PATH_MAX, "%s/run/%s-%s.pid",              \
                          volpath, brickinfo->hostname, exp_path);      \
        } while (0)

#define GLUSTERD_GET_NFS_PIDFILE(pidfile,nfspath) {                     \
                snprintf (pidfile, PATH_MAX, "%s/run/nfs.pid",          \
                          nfspath);                                     \
        }

#define GLUSTERD_GET_QUOTAD_PIDFILE(pidfile,quotadpath) {                     \
                snprintf (pidfile, PATH_MAX, "%s/run/quotad.pid",          \
                          quotadpath);                                     \
        }

#define GLUSTERD_GET_QUOTA_CRAWL_PIDDIR(piddir, volinfo, type) do {           \
                char _volpath[PATH_MAX]  = {0,};                              \
                GLUSTERD_GET_VOLUME_DIR (_volpath, volinfo, priv);            \
                if (type == GF_QUOTA_OPTION_TYPE_ENABLE ||                    \
                    type == GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS)              \
                        snprintf (piddir, PATH_MAX, "%s/run/quota/enable",    \
                                  _volpath);                                  \
                else                                                          \
                        snprintf (piddir, PATH_MAX, "%s/run/quota/disable",   \
                                  _volpath);                                  \
        } while (0)

#define GLUSTERD_STACK_DESTROY(frame) do {\
                frame->local = NULL;                                    \
                STACK_DESTROY (frame->root);                            \
        } while (0)

#define GLUSTERD_GET_DEFRAG_PROCESS(path, volinfo) do {                 \
                if (volinfo->rebal.defrag_cmd == GF_DEFRAG_CMD_START_TIER) \
                        snprintf (path, NAME_MAX, "tier");              \
                else                                                    \
                        snprintf (path, NAME_MAX, "rebalance");         \
        } while (0)

#define GLUSTERD_GET_DEFRAG_DIR(path, volinfo, priv) do {               \
                char vol_path[PATH_MAX];                                \
                char operation[NAME_MAX];                               \
                GLUSTERD_GET_VOLUME_DIR(vol_path, volinfo, priv);       \
                GLUSTERD_GET_DEFRAG_PROCESS(operation, volinfo);        \
                snprintf (path, PATH_MAX, "%s/%s", vol_path, operation);\
        } while (0)

#define GLUSTERD_GET_DEFRAG_SOCK_FILE_OLD(path, volinfo, priv) do {     \
                char defrag_path[PATH_MAX];                             \
                GLUSTERD_GET_DEFRAG_DIR(defrag_path, volinfo, priv);    \
                snprintf (path, PATH_MAX, "%s/%s.sock", defrag_path,    \
                           uuid_utoa(MY_UUID));                         \
        } while (0)

#define GLUSTERD_GET_DEFRAG_SOCK_FILE(path, volinfo) do {                   \
                char operation[NAME_MAX];                                   \
                GLUSTERD_GET_DEFRAG_PROCESS(operation, volinfo);            \
                snprintf (path, UNIX_PATH_MAX, DEFAULT_VAR_RUN_DIRECTORY    \
                          "/gluster-%s-%s.sock", operation,                 \
                           uuid_utoa(volinfo->volume_id));                  \
        } while (0)

#define GLUSTERD_GET_DEFRAG_PID_FILE(path, volinfo, priv) do {          \
                char defrag_path[PATH_MAX];                             \
                GLUSTERD_GET_DEFRAG_DIR(defrag_path, volinfo, priv);    \
                snprintf (path, PATH_MAX, "%s/%s.pid", defrag_path,     \
                           uuid_utoa(MY_UUID));                         \
        } while (0)

#define GLUSTERFS_GET_AUX_MOUNT_PIDFILE(pidfile, volname) {               \
                snprintf (pidfile, PATH_MAX-1,                            \
                          DEFAULT_VAR_RUN_DIRECTORY"/%s.pid", volname);   \
        }

#define GLUSTERD_GET_UUID_NOHYPHEN(ret_string, uuid) do {               \
                char *snap_volname_ptr = ret_string;                    \
                char  tmp_uuid[64];                                     \
                char *snap_volid_ptr = uuid_utoa_r(uuid, tmp_uuid);     \
                while (*snap_volid_ptr) {                               \
                        if (*snap_volid_ptr == '-') {                   \
                                snap_volid_ptr++;                       \
                        } else {                                        \
                                (*snap_volname_ptr++) =                 \
                                (*snap_volid_ptr++);                    \
                        }                                               \
                }                                                       \
                *snap_volname_ptr = '\0';                               \
        } while (0)

#define GLUSTERD_DUMP_PEERS(head, member, xpeers) do {                       \
                glusterd_peerinfo_t  *_peerinfo                = NULL;       \
                int                   index                    = 1;          \
                char                  key[GF_DUMP_MAX_BUF_LEN] = {0,};       \
                                                                             \
                if (!xpeers)                                                 \
                        snprintf (key, sizeof (key), "glusterd.peer");       \
                else                                                         \
                        snprintf (key, sizeof (key),                         \
                                  "glusterd.xaction_peer");                  \
                                                                             \
                rcu_read_lock ();                                            \
                cds_list_for_each_entry_rcu (_peerinfo, head, member) {      \
                        glusterd_dump_peer (_peerinfo, key, index, xpeers);  \
                        if (!xpeers)                                         \
                                glusterd_dump_peer_rpcstat (_peerinfo, key,  \
                                                            index);          \
                        index++;                                             \
                }                                                            \
                rcu_read_unlock ();                                          \
                                                                             \
        } while (0)

int glusterd_uuid_init();

int glusterd_uuid_generate_save ();

#define MY_UUID (__glusterd_uuid())

static inline unsigned char *
__glusterd_uuid()
{
        glusterd_conf_t *priv = THIS->private;

        if (gf_uuid_is_null (priv->uuid))
                glusterd_uuid_init();
        return &priv->uuid[0];
}

int glusterd_big_locked_notify (struct rpc_clnt *rpc, void *mydata,
                                rpc_clnt_event_t event,
                                void *data, rpc_clnt_notify_t notify_fn);

int
glusterd_big_locked_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *myframe, fop_cbk_fn_t fn);

int glusterd_big_locked_handler (rpcsvc_request_t *req, rpcsvc_actor actor_fn);

int32_t
glusterd_brick_from_brickinfo (glusterd_brickinfo_t *brickinfo,
                               char **new_brick);
int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                      dict_t *dict, int *op_errno);

int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *myhostname,
                               char *remote_hostname, int port, int32_t op_ret,
                               int32_t op_errno);

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid, glusterd_peerinfo_t **friend,
                     gf_boolean_t restore, glusterd_peerctx_args_t *args);

int
glusterd_friend_add_from_peerinfo (glusterd_peerinfo_t *friend,
                                   gf_boolean_t restore,
                                   glusterd_peerctx_args_t *args);
int
glusterd_friend_rpc_create (xlator_t *this, glusterd_peerinfo_t *peerinfo,
                            glusterd_peerctx_args_t *args);
int
glusterd_friend_remove (uuid_t uuid, char *hostname);

int
glusterd_op_lock_send_resp (rpcsvc_request_t *req, int32_t status);

int
glusterd_op_unlock_send_resp (rpcsvc_request_t *req, int32_t status);

int
glusterd_op_mgmt_v3_lock_send_resp (rpcsvc_request_t *req,
                                    uuid_t *txn_id, int32_t status);

int
glusterd_op_mgmt_v3_unlock_send_resp (rpcsvc_request_t *req,
                                      uuid_t *txn_id, int32_t status);

int
glusterd_op_stage_send_resp (rpcsvc_request_t *req,
                             int32_t op, int32_t status,
                             char *op_errstr, dict_t *rsp_dict);

int
glusterd_op_commmit_send_resp (rpcsvc_request_t *req,
                               int32_t op, int32_t status);

int32_t
glusterd_create_volume (rpcsvc_request_t *req, dict_t *dict);

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req);

int
glusterd_handle_probe_query (rpcsvc_request_t *req);

int
glusterd_handle_cluster_lock (rpcsvc_request_t *req);

int
glusterd_handle_cluster_unlock (rpcsvc_request_t *req);

int
glusterd_handle_stage_op (rpcsvc_request_t *req);

int
glusterd_handle_commit_op (rpcsvc_request_t *req);

int
glusterd_handle_cli_probe (rpcsvc_request_t *req);

int
glusterd_handle_create_volume (rpcsvc_request_t *req);

int
glusterd_handle_defrag_volume (rpcsvc_request_t *req);

int
glusterd_handle_defrag_volume_v2 (rpcsvc_request_t *req);

int
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *op_errstr, char *hostname,
                              int port, dict_t *dict);

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                              int32_t op, int32_t status, char *op_errstr,
                              dict_t *rsp_dict);

int
glusterd_xfer_friend_remove_resp (rpcsvc_request_t *req, char *hostname, int port);

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                        uuid_t uuid, dict_t *dict, int *op_errno);

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req);

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req);

int32_t
glusterd_list_friends (rpcsvc_request_t *req, dict_t *dict, int32_t flags);

int
glusterd_handle_cli_list_friends (rpcsvc_request_t *req);

int
glusterd_handle_cli_start_volume (rpcsvc_request_t *req);

int
glusterd_handle_friend_update (rpcsvc_request_t *req);

int
glusterd_handle_cli_stop_volume (rpcsvc_request_t *req);

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req);

int
glusterd_handle_cli_get_volume (rpcsvc_request_t *req);

int32_t
glusterd_get_volumes (rpcsvc_request_t *req, dict_t *dict, int32_t flags);

int
glusterd_handle_add_brick (rpcsvc_request_t *req);

int
glusterd_handle_tier (rpcsvc_request_t *req);

int
glusterd_handle_attach_tier (rpcsvc_request_t *req);

int
glusterd_handle_detach_tier (rpcsvc_request_t *req);

int
glusterd_handle_replace_brick (rpcsvc_request_t *req);

int
glusterd_handle_remove_brick (rpcsvc_request_t *req);

int
glusterd_handle_log_rotate (rpcsvc_request_t *req);

int
glusterd_handle_sync_volume (rpcsvc_request_t *req);

int
glusterd_defrag_start_validate (glusterd_volinfo_t *volinfo, char *op_errstr,
                                size_t len, glusterd_op_t op);

int
glusterd_rebalance_cmd_validate (int cmd, char *volname,
                                 glusterd_volinfo_t **volinfo,
                                 char *op_errstr, size_t len);

int32_t
glusterd_log_filename (rpcsvc_request_t *req, dict_t *dict);

int32_t
glusterd_log_rotate (rpcsvc_request_t *req, dict_t *dict);

int32_t
glusterd_remove_brick (rpcsvc_request_t *req, dict_t *dict);

int32_t
glusterd_set_volume (rpcsvc_request_t *req, dict_t *dict);

int32_t
glusterd_reset_volume (rpcsvc_request_t *req, dict_t *dict);

int32_t
glusterd_gsync_set (rpcsvc_request_t *req, dict_t *dict);

int32_t
glusterd_quota (rpcsvc_request_t *req, dict_t *dict);

int
glusterd_handle_set_volume (rpcsvc_request_t *req);

int
glusterd_handle_reset_volume (rpcsvc_request_t *req);

int
glusterd_handle_copy_file (rpcsvc_request_t *req);

int
glusterd_handle_sys_exec (rpcsvc_request_t *req);

int
glusterd_handle_gsync_set (rpcsvc_request_t *req);

int
glusterd_handle_quota (rpcsvc_request_t *req);

int
glusterd_handle_bitrot (rpcsvc_request_t *req);

int
glusterd_handle_fsm_log (rpcsvc_request_t *req);

int
glusterd_handle_reset_brick (rpcsvc_request_t *req);

int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *op_errstr,
                                char *hostname, dict_t *dict);

int
glusterd_client_statedump_submit_req (char *volname, char *target_ip,
                                      char *pid);

int
glusterd_fetchspec_notify (xlator_t *this);

int
glusterd_fetchsnap_notify (xlator_t *this);

int
glusterd_add_tier_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count);

int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count);

int
glusterd_restart_bricks (glusterd_conf_t *conf);

int32_t
glusterd_volume_txn (rpcsvc_request_t *req, char *volname, int flags,
                     glusterd_op_t op);

int
glusterd_peer_dump_version (xlator_t *this, struct rpc_clnt *rpc,
                            glusterd_peerctx_t *peerctx);

int
glusterd_validate_reconfopts (glusterd_volinfo_t *volinfo, dict_t *val_dict, char **op_errstr);
int
glusterd_handle_cli_profile_volume (rpcsvc_request_t *req);

int
glusterd_handle_getwd (rpcsvc_request_t *req);

int32_t
glusterd_set_volume (rpcsvc_request_t *req, dict_t *dict);
int
glusterd_peer_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event,
                          void *data);
int
glusterd_brick_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event, void *data);

int
glusterd_rpc_create (struct rpc_clnt **rpc, dict_t *options,
                     rpc_clnt_notify_t notify_fn, void *notify_data,
                     gf_boolean_t force);


/* handler functions */
int32_t glusterd_op_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx,
                           char *err_str, size_t size);

/* removed other definitions as they have been defined elsewhere in this file*/

int glusterd_handle_cli_statedump_volume (rpcsvc_request_t *req);
int glusterd_handle_cli_clearlocks_volume (rpcsvc_request_t *req);

int glusterd_handle_defrag_start (glusterd_volinfo_t *volinfo, char *op_errstr,
                                  size_t len, int cmd, defrag_cbk_fn_t cbk,
                                  glusterd_op_t op);
int
glusterd_rebalance_rpc_create (glusterd_volinfo_t *volinfo);

int glusterd_rebalance_defrag_init (glusterd_volinfo_t *volinfo,
                                    defrag_cbk_fn_t cbk);

int glusterd_handle_cli_heal_volume (rpcsvc_request_t *req);

int glusterd_handle_cli_list_volume (rpcsvc_request_t *req);

int
glusterd_handle_snapshot (rpcsvc_request_t *req);

/* op-sm functions */
int glusterd_op_stage_heal_volume (dict_t *dict, char **op_errstr);
int glusterd_op_heal_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stage_gsync_set (dict_t *dict, char **op_errstr);
int glusterd_op_gsync_set (dict_t *dict, char **op_errstr, dict_t *rsp_dict);
int glusterd_op_stage_copy_file (dict_t *dict, char **op_errstr);
int glusterd_op_copy_file (dict_t *dict, char **op_errstr);
int glusterd_op_stage_sys_exec (dict_t *dict, char **op_errstr);
int glusterd_op_sys_exec (dict_t *dict, char **op_errstr, dict_t *rsp_dict);
int glusterd_op_stage_gsync_create (dict_t *dict, char **op_errstr);
int glusterd_op_gsync_create (dict_t *dict, char **op_errstr, dict_t *rsp_dict);
int glusterd_op_quota (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int glusterd_op_bitrot (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int glusterd_op_stage_quota (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int glusterd_op_stage_bitrot (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int glusterd_op_stage_replace_brick (dict_t *dict, char **op_errstr,
                                     dict_t *rsp_dict);
int glusterd_op_replace_brick (dict_t *dict, dict_t *rsp_dict);
int glusterd_op_log_rotate (dict_t *dict);
int glusterd_op_stage_log_rotate (dict_t *dict, char **op_errstr);
int glusterd_op_stage_create_volume (dict_t *dict, char **op_errstr,
                                     dict_t *rsp_dict);
int glusterd_op_stage_start_volume (dict_t *dict, char **op_errstr,
                                    dict_t *rsp_dict);
int glusterd_op_stage_stop_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stage_delete_volume (dict_t *dict, char **op_errstr);
int glusterd_op_create_volume (dict_t *dict, char **op_errstr);
int glusterd_op_start_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stop_volume (dict_t *dict);
int glusterd_op_delete_volume (dict_t *dict);
int glusterd_handle_ganesha_op (dict_t *dict, char **op_errstr,
                               char *key, char *value);
int glusterd_check_ganesha_cmd (char *key, char *value,
                                char **errstr, dict_t *dict);
int glusterd_op_stage_set_ganesha (dict_t *dict, char **op_errstr);
int glusterd_op_set_ganesha (dict_t *dict, char **errstr);
int ganesha_manage_export (dict_t *dict, char *value, char **op_errstr);
int manage_export_config (char *volname, char *value, char **op_errstr);

gf_boolean_t
glusterd_is_ganesha_cluster ();
gf_boolean_t glusterd_check_ganesha_export (glusterd_volinfo_t *volinfo);
int stop_ganesha (char **op_errstr);
int tear_down_cluster (gf_boolean_t run_teardown);
int glusterd_op_add_brick (dict_t *dict, char **op_errstr);
int glusterd_op_remove_brick (dict_t *dict, char **op_errstr);
int glusterd_op_stage_add_brick (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict);
int glusterd_op_stage_remove_brick (dict_t *dict, char **op_errstr);

int glusterd_op_stage_rebalance (dict_t *dict, char **op_errstr);
int glusterd_op_rebalance (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int glusterd_op_stage_statedump_volume (dict_t *dict, char **op_errstr);
int glusterd_op_statedump_volume (dict_t *dict, char **op_errstr);

int glusterd_op_stage_clearlocks_volume (dict_t *dict, char **op_errstr);
int glusterd_op_clearlocks_volume (dict_t *dict, char **op_errstr,
                                   dict_t *rsp_dict);


int glusterd_op_stage_barrier (dict_t *dict, char **op_errstr);
int glusterd_op_barrier (dict_t *dict, char **op_errstr);

/* misc */
int glusterd_op_perform_remove_brick (glusterd_volinfo_t  *volinfo, char *brick,
                                      int force, int *need_migrate);
int glusterd_op_stop_volume_args_get (dict_t *dict, char** volname, int *flags);
int glusterd_op_statedump_volume_args_get (dict_t *dict, char **volname,
                                           char **options, int *option_cnt);

int glusterd_op_gsync_args_get (dict_t *dict, char **op_errstr,
                                char **master, char **slave, char **host_uuid);

int glusterd_op_get_max_opversion (char **op_errstr, dict_t *rsp_dict);

int glusterd_start_volume (glusterd_volinfo_t *volinfo, int flags,
                           gf_boolean_t wait);

int glusterd_stop_volume (glusterd_volinfo_t *volinfo);

/* Synctask part */
int32_t glusterd_op_begin_synctask (rpcsvc_request_t *req, glusterd_op_t op,
                                    void *dict);
int32_t
glusterd_defrag_event_notify_handle (dict_t *dict);

int32_t
glusterd_txn_opinfo_dict_init ();

void
glusterd_txn_opinfo_dict_fini ();

void
glusterd_txn_opinfo_init ();

/* snapshot */
glusterd_snap_t*
glusterd_new_snap_object();

int32_t
glusterd_list_add_snapvol (glusterd_volinfo_t *origin_vol,
                           glusterd_volinfo_t *snap_vol);

glusterd_snap_t*
glusterd_remove_snap_by_id (uuid_t snap_id);

glusterd_snap_t*
glusterd_remove_snap_by_name (char *snap_name);

glusterd_snap_t*
glusterd_find_snap_by_name (char *snap_name);

glusterd_snap_t*
glusterd_find_snap_by_id (uuid_t snap_id);

int
glusterd_snapshot_prevalidate (dict_t *dict, char **op_errstr,
                               dict_t *rsp_dict, uint32_t *op_errno);
int
glusterd_snapshot_brickop (dict_t *dict, char **op_errstr, dict_t *rsp_dict);
int
glusterd_snapshot (dict_t *dict, char **op_errstr,
                   uint32_t *op_errno, dict_t *rsp_dict);
int
glusterd_snapshot_postvalidate (dict_t *dict, int32_t op_ret, char **op_errstr,
                                dict_t *rsp_dict);
char *
glusterd_build_snap_device_path (char *device, char *snapname,
                                 int32_t brick_count);

int32_t
glusterd_snap_remove (dict_t *rsp_dict, glusterd_snap_t *snap,
                      gf_boolean_t remove_lvm, gf_boolean_t force,
                      gf_boolean_t is_clone);
int32_t
glusterd_snapshot_cleanup (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int32_t
glusterd_add_missed_snaps_to_list (dict_t *dict, int32_t missed_snap_count);

int32_t
glusterd_add_new_entry_to_list (char *missed_info, char *snap_vol_id,
                                int32_t brick_num, char *brick_path,
                                int32_t snap_op, int32_t snap_status);

int
glusterd_snapshot_revert_restore_from_snap (glusterd_snap_t *snap);


int
glusterd_add_brick_status_to_dict (dict_t *dict, glusterd_volinfo_t *volinfo,
                                   glusterd_brickinfo_t *brickinfo,
                                   char *key_prefix);

int32_t
glusterd_handle_snap_limit (dict_t *dict, dict_t *rsp_dict);

gf_boolean_t
glusterd_should_i_stop_bitd ();

int
glusterd_remove_brick_migrate_cbk (glusterd_volinfo_t *volinfo,
                                   gf_defrag_status_t status);
/* tier */

int
__glusterd_handle_reset_brick (rpcsvc_request_t *req);
int glusterd_op_stage_tier (dict_t *dict, char **op_errstr, dict_t *rsp_dict);
int glusterd_op_tier_start_stop (dict_t *dict, char **op_errstr,
                dict_t *rsp_dict);
int glusterd_op_remove_tier_brick (dict_t *dict, char **op_errstr,
                                   dict_t *rsp_dict);
int
glusterd_tier_prevalidate (dict_t *dict, char **op_errstr,
                               dict_t *rsp_dict, uint32_t *op_errno);
#endif
