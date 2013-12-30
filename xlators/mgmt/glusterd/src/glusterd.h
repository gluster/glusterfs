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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <libgen.h>

#include "uuid.h"

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
#include "glusterd1-xdr.h"
#include "protocol-common.h"
#include "glusterd-pmap.h"
#include "cli1-xdr.h"
#include "syncop.h"
#include "store.h"

#define GLUSTERD_MAX_VOLUME_NAME        1000
#define GLUSTERD_TR_LOG_SIZE            50
#define GLUSTERD_NAME                   "glusterd"
#define GLUSTERD_SOCKET_LISTEN_BACKLOG  128
#define GLUSTERD_QUORUM_TYPE_KEY        "cluster.server-quorum-type"
#define GLUSTERD_QUORUM_RATIO_KEY       "cluster.server-quorum-ratio"
#define GLUSTERD_GLOBAL_OPT_VERSION     "global-option-version"
#define GLUSTERD_COMMON_PEM_PUB_FILE    "/geo-replication/common_secret.pem.pub"
#define GEO_CONF_MAX_OPT_VALS           6
#define GLUSTERD_CREATE_HOOK_SCRIPT     "/hooks/1/gsync-create/post/" \
                                        "S56glusterd-geo-rep-create-post.sh"


#define GLUSTERD_SERVER_QUORUM "server"

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
        GD_OP_MAX,
} glusterd_op_t;

extern const char * gd_op_list[];

struct glusterd_volgen {
        dict_t *dict;
};

typedef struct {
        struct rpc_clnt  *rpc;
        gf_boolean_t      online;
} nodesrv_t;

typedef struct {
        gf_boolean_t    quorum;
        double          quorum_ratio;
        uint64_t        gl_opt_version;
} gd_global_opts_t;

typedef struct {
        struct _volfile_ctx     *volfile;
        pthread_mutex_t          mutex;
        struct list_head         peers;
        struct list_head         xaction_peers;
        gf_boolean_t             verify_volfile_checksum;
        gf_boolean_t             trace;
        uuid_t                   uuid;
        char                     workdir[PATH_MAX];
        rpcsvc_t                *rpc;
        nodesrv_t               *shd;
        nodesrv_t               *nfs;
        nodesrv_t               *quotad;
        struct pmap_registry    *pmap;
        struct list_head         volumes;
        pthread_mutex_t          xprt_lock;
        struct list_head         xprt_list;
        gf_store_handle_t       *handle;
        gf_timer_t              *timer;
        glusterd_sm_tr_log_t     op_sm_log;
        struct rpc_clnt_program *gfs_mgmt;

        struct list_head  mount_specs;
        gf_boolean_t      valgrind;
        pthread_t         brick_thread;
        void             *hooks_priv;

        /* need for proper handshake_t */
        int           op_version; /* Starts with 1 for 3.3.0 */
        xlator_t     *xl;       /* Should be set to 'THIS' before creating thread */
        gf_boolean_t  pending_quorum_action;
        dict_t       *opts;
        synclock_t    big_lock;
        gf_boolean_t  restart_done;
        rpcsvc_t     *uds_rpc;  /* RPCSVC for the unix domain socket */
        uint32_t      base_port;
} glusterd_conf_t;


typedef enum gf_brick_status {
        GF_BRICK_STOPPED,
        GF_BRICK_STARTED,
} gf_brick_status_t;

struct glusterd_brickinfo {
        char               hostname[1024];
        char               path[PATH_MAX];
        struct list_head   brick_list;
        uuid_t             uuid;
        int                port;
        int                rdma_port;
        char              *logfile;
        gf_boolean_t       signed_in;
        gf_store_handle_t *shandle;
        gf_brick_status_t  status;
        struct rpc_clnt   *rpc;
        int                decommissioned;
        char vg[PATH_MAX]; /* FIXME: Use max size for length of vg */
        int     caps; /* Capability */
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

#define GF_DEFAULT_NFS_TRANSPORT  GF_TRANSPORT_RDMA

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
        glusterd_op_t            op;
        dict_t                  *dict; /* Dict to store misc information
                                        * like list of bricks being removed */
};

typedef struct glusterd_rebalance_ glusterd_rebalance_t;

struct glusterd_replace_brick_ {
        gf_rb_status_t          rb_status;
        glusterd_brickinfo_t   *src_brick;
        glusterd_brickinfo_t   *dst_brick;
        uuid_t                  rb_id;
};

typedef struct glusterd_replace_brick_ glusterd_replace_brick_t;

struct glusterd_volinfo_ {
        char                      volname[GLUSTERD_MAX_VOLUME_NAME];
        int                       type;
        int                       brick_count;
        struct list_head          vol_list;
        struct list_head          bricks;
        glusterd_volume_status    status;
        int                       sub_count;  /* backward compatibility */
        int                       stripe_count;
        int                       replica_count;
        int                       subvol_count; /* Number of subvolumes in a
                                                 distribute volume */
        int                       dist_leaf_count; /* Number of bricks in one
                                                    distribute subvolume */
        int                     port;
        gf_store_handle_t *shandle;
        gf_store_handle_t *rb_shandle;
        gf_store_handle_t *node_state_shandle;
        gf_store_handle_t *quota_conf_shandle;

        /* Defrag/rebalance related */
        glusterd_rebalance_t      rebal;

        /* Replace brick status */
        glusterd_replace_brick_t  rep_brick;

        int                     version;
        uint32_t                quota_conf_version;
        uint32_t                cksum;
        uint32_t                quota_conf_cksum;
        gf_transport_type       transport_type;
        gf_transport_type   nfs_transport_type;

        dict_t                   *dict;

        uuid_t                    volume_id;
        auth_t                    auth;
        char                     *logdir;

        dict_t                   *gsync_slaves;

        int                       decommission_in_progress;
        xlator_t                 *xl;

        gf_boolean_t              memory_accounting;
        int                      caps; /* Capability */

        int                       op_version;
        int                       client_op_version;
        pthread_mutex_t           reflock;
        int                       refcnt;
};

typedef enum gd_node_type_ {
        GD_NODE_NONE,
        GD_NODE_BRICK,
        GD_NODE_SHD,
        GD_NODE_REBALANCE,
        GD_NODE_NFS,
        GD_NODE_QUOTAD,
} gd_node_type;

typedef struct glusterd_pending_node_ {
        struct list_head list;
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

#define GLUSTERD_DEFAULT_WORKDIR "/var/lib/glusterd"
#define GLUSTERD_DEFAULT_PORT    GF_DEFAULT_BASE_PORT
#define GLUSTERD_INFO_FILE      "glusterd.info"
#define GLUSTERD_VOLUME_QUOTA_CONFIG "quota.conf"
#define GLUSTERD_VOLUME_DIR_PREFIX "vols"
#define GLUSTERD_PEER_DIR_PREFIX "peers"
#define GLUSTERD_VOLUME_INFO_FILE "info"
#define GLUSTERD_VOLUME_RBSTATE_FILE "rbstate"
#define GLUSTERD_BRICK_INFO_DIR "bricks"
#define GLUSTERD_CKSUM_FILE "cksum"
#define GLUSTERD_VOL_QUOTA_CKSUM_FILE "quota.cksum"
#define GLUSTERD_TRASH "trash"
#define GLUSTERD_NODE_STATE_FILE "node_state.info"

/* definitions related to replace brick */
#define RB_CLIENT_MOUNTPOINT    "rb_mount"
#define RB_CLIENTVOL_FILENAME   "rb_client.vol"
#define RB_DSTBRICK_PIDFILE     "rb_dst_brick.pid"
#define RB_DSTBRICKVOL_FILENAME "rb_dst_brick.vol"
#define RB_PUMP_DEF_ARG         "default"

#define GLUSTERD_UUID_LEN 50

typedef ssize_t (*gd_serialize_t) (struct iovec outmsg, void *args);

#define GLUSTERD_GET_VOLUME_DIR(path, volinfo, priv) \
        snprintf (path, PATH_MAX, "%s/vols/%s", priv->workdir,\
                  volinfo->volname);

#define GLUSTERD_GET_BRICK_DIR(path, volinfo, priv) \
        snprintf (path, PATH_MAX, "%s/%s/%s/%s", priv->workdir,\
                  GLUSTERD_VOLUME_DIR_PREFIX, volinfo->volname, \
                  GLUSTERD_BRICK_INFO_DIR);

#define GLUSTERD_GET_NFS_DIR(path, priv) \
        snprintf (path, PATH_MAX, "%s/nfs", priv->workdir);

#define GLUSTERD_GET_QUOTAD_DIR(path, priv) \
        snprintf (path, PATH_MAX, "%s/quotad", priv->workdir);

#define GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH(abspath, volname, path)      \
        snprintf (abspath, sizeof (abspath)-1,                          \
                  DEFAULT_VAR_RUN_DIRECTORY"/%s%s", volname, path);

#define GLUSTERD_REMOVE_SLASH_FROM_PATH(path,string) do {               \
                int i = 0;                                              \
                for (i = 1; i < strlen (path); i++) {                   \
                        string[i-1] = path[i];                          \
                        if (string[i-1] == '/')                         \
                                string[i-1] = '-';                      \
                }                                                       \
        } while (0)

#define GLUSTERD_GET_BRICK_PIDFILE(pidfile,volinfo,brickinfo, priv) do {      \
                char exp_path[PATH_MAX] = {0,};                               \
                char volpath[PATH_MAX]  = {0,};                               \
                GLUSTERD_GET_VOLUME_DIR (volpath, volinfo, priv);             \
                GLUSTERD_REMOVE_SLASH_FROM_PATH (brickinfo->path, exp_path);  \
                snprintf (pidfile, PATH_MAX, "%s/run/%s-%s.pid",              \
                          volpath, brickinfo->hostname, exp_path);            \
        } while (0)

#define GLUSTERD_GET_NFS_PIDFILE(pidfile,nfspath) {                     \
                snprintf (pidfile, PATH_MAX, "%s/run/nfs.pid",          \
                          nfspath);                                     \
        }

#define GLUSTERD_GET_QUOTAD_PIDFILE(pidfile,quotadpath) {                     \
                snprintf (pidfile, PATH_MAX, "%s/run/quotad.pid",          \
                          quotadpath);                                     \
        }

#define GLUSTERD_STACK_DESTROY(frame) do {\
                frame->local = NULL;                                    \
                STACK_DESTROY (frame->root);                            \
        } while (0)

#define GLUSTERD_GET_DEFRAG_DIR(path, volinfo, priv) do {               \
                char vol_path[PATH_MAX];                                \
                GLUSTERD_GET_VOLUME_DIR(vol_path, volinfo, priv);       \
                snprintf (path, PATH_MAX, "%s/rebalance",vol_path);     \
        } while (0)

#define GLUSTERD_GET_DEFRAG_SOCK_FILE_OLD(path, volinfo, priv) do {     \
                char defrag_path[PATH_MAX];                             \
                GLUSTERD_GET_DEFRAG_DIR(defrag_path, volinfo, priv);    \
                snprintf (path, PATH_MAX, "%s/%s.sock", defrag_path,    \
                           uuid_utoa(MY_UUID));                         \
        } while (0)

#define GLUSTERD_GET_DEFRAG_SOCK_FILE(path, volinfo) do {                   \
                snprintf (path, UNIX_PATH_MAX, DEFAULT_VAR_RUN_DIRECTORY    \
                          "/gluster-rebalance-%s.sock",                     \
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

int glusterd_uuid_init();

int glusterd_uuid_generate_save ();

#define MY_UUID (__glusterd_uuid())

static inline unsigned char *
__glusterd_uuid()
{
        glusterd_conf_t *priv = THIS->private;

        if (uuid_is_null (priv->uuid))
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
                      dict_t *dict);

int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *myhostname,
                               char *remote_hostname, int port, int32_t op_ret,
                               int32_t op_errno);

int
glusterd_friend_find (uuid_t uuid, char *hostname,
                      glusterd_peerinfo_t **peerinfo);

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid, glusterd_peerinfo_t **friend,
                     gf_boolean_t restore, glusterd_peerctx_args_t *args);

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
                        uuid_t uuid, dict_t *dict);

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
glusterd_handle_replace_brick (rpcsvc_request_t *req);

int
glusterd_handle_remove_brick (rpcsvc_request_t *req);

int
glusterd_handle_log_rotate (rpcsvc_request_t *req);

int
glusterd_handle_sync_volume (rpcsvc_request_t *req);

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
glusterd_handle_fsm_log (rpcsvc_request_t *req);

int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *op_errstr,
                                char *hostname, dict_t *dict);

int
glusterd_fetchspec_notify (xlator_t *this);

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
glusterd_nodesvc_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                             rpc_clnt_event_t event, void *data);

int
glusterd_rpc_create (struct rpc_clnt **rpc, dict_t *options,
                     rpc_clnt_notify_t notify_fn, void *notify_data);


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
glusterd_rebalance_rpc_create (glusterd_volinfo_t *volinfo,
                               gf_boolean_t reconnect);

int glusterd_handle_cli_heal_volume (rpcsvc_request_t *req);

int glusterd_handle_cli_list_volume (rpcsvc_request_t *req);

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
int glusterd_op_stage_quota (dict_t *dict, char **op_errstr, dict_t *rsp_dict);
int glusterd_op_stage_replace_brick (dict_t *dict, char **op_errstr,
                                     dict_t *rsp_dict);
int glusterd_op_replace_brick (dict_t *dict, dict_t *rsp_dict);
int glusterd_op_log_rotate (dict_t *dict);
int glusterd_op_stage_log_rotate (dict_t *dict, char **op_errstr);
int glusterd_op_stage_create_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stage_start_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stage_stop_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stage_delete_volume (dict_t *dict, char **op_errstr);
int glusterd_op_create_volume (dict_t *dict, char **op_errstr);
int glusterd_op_start_volume (dict_t *dict, char **op_errstr);
int glusterd_op_stop_volume (dict_t *dict);
int glusterd_op_delete_volume (dict_t *dict);

int glusterd_op_add_brick (dict_t *dict, char **op_errstr);
int glusterd_op_remove_brick (dict_t *dict, char **op_errstr);
int glusterd_op_stage_add_brick (dict_t *dict, char **op_errstr);
int glusterd_op_stage_remove_brick (dict_t *dict, char **op_errstr);

int glusterd_op_stage_rebalance (dict_t *dict, char **op_errstr);
int glusterd_op_rebalance (dict_t *dict, char **op_errstr, dict_t *rsp_dict);

int glusterd_op_stage_statedump_volume (dict_t *dict, char **op_errstr);
int glusterd_op_statedump_volume (dict_t *dict, char **op_errstr);

int glusterd_op_stage_clearlocks_volume (dict_t *dict, char **op_errstr);
int glusterd_op_clearlocks_volume (dict_t *dict, char **op_errstr,
                                   dict_t *rsp_dict);

/* misc */
void glusterd_do_replace_brick (void *data);
int glusterd_op_perform_remove_brick (glusterd_volinfo_t  *volinfo, char *brick,
                                      int force, int *need_migrate);
int glusterd_op_stop_volume_args_get (dict_t *dict, char** volname, int *flags);
int glusterd_op_statedump_volume_args_get (dict_t *dict, char **volname,
                                           char **options, int *option_cnt);

int glusterd_op_gsync_args_get (dict_t *dict, char **op_errstr,
                                char **master, char **slave, char **host_uuid);
/* Synctask part */
int32_t glusterd_op_begin_synctask (rpcsvc_request_t *req, glusterd_op_t op,
                                    void *dict);
int32_t
glusterd_defrag_event_notify_handle (dict_t *dict);
#endif
