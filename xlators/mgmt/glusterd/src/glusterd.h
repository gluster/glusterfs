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


#define GLUSTERD_MAX_VOLUME_NAME        1000
#define DEFAULT_LOG_FILE_DIRECTORY      DATADIR "/log/glusterfs"
#define GLUSTERD_TR_LOG_SIZE            50
#define GLUSTERD_NAME                   "glusterd"


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
        GD_OP_SET_VOLUME,
        GD_OP_RESET_VOLUME,
        GD_OP_SYNC_VOLUME,
        GD_OP_LOG_FILENAME,
        GD_OP_LOG_LOCATE,
        GD_OP_LOG_ROTATE,
        GD_OP_GSYNC_SET,
        GD_OP_MAX,
} glusterd_op_t;


struct glusterd_store_iter_ {
        int     fd;
        FILE    *file;
};

typedef struct glusterd_store_iter_     glusterd_store_iter_t;

typedef struct {
        struct _volfile_ctx *volfile;
	pthread_mutex_t   mutex;
	struct list_head  peers;
//	struct list_head  pending_peers;
        gf_boolean_t      verify_volfile_checksum;
        gf_boolean_t      trace;
        uuid_t            uuid;
        char              workdir[PATH_MAX];
        rpcsvc_t          *rpc;
        rpc_clnt_prog_t   *mgmt;
        struct pmap_registry *pmap;
        struct list_head  volumes;
        struct list_head  xprt_list;
        glusterd_store_handle_t *handle;
        gf_timer_t *timer;
        glusterd_sm_tr_log_t op_sm_log;
} glusterd_conf_t;

typedef enum gf_brick_status {
        GF_BRICK_STOPPED,
        GF_BRICK_STARTED,
} gf_brick_status_t;

struct glusterd_brickinfo {
        char    hostname[1024];
        char    path[PATH_MAX];
        struct list_head  brick_list;
        uuid_t  uuid;
        int     port;
        char   *logfile;
        gf_boolean_t signed_in;
        glusterd_store_handle_t *shandle;
        gf_brick_status_t status; 
};

typedef struct glusterd_brickinfo glusterd_brickinfo_t;

struct gf_defrag_brickinfo_ {
        char *name;
        int   files;
        int   size;
};

typedef enum gf_defrag_status_ {
        GF_DEFRAG_STATUS_NOT_STARTED,
        GF_DEFRAG_STATUS_STARTED,
        GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE,
        GF_DEFRAG_STATUS_STOPED,
        GF_DEFRAG_STATUS_COMPLETE,
        GF_DEFRAG_STATUS_FAILED,
} gf_defrag_status_t;

struct glusterd_defrag_info_ {
        uint64_t                     total_files;
        uint64_t                     total_data;
        uint64_t                     num_files_lookedup;
        gf_lock_t                    lock;
        pthread_t                    th;
        char                         mount[1024];
        char                         databuf[131072];
        struct gf_defrag_brickinfo_ *bricks; /* volinfo->brick_count */
};


typedef struct glusterd_defrag_info_ glusterd_defrag_info_t;

typedef enum gf_transport_type_ {
        GF_TRANSPORT_TCP,       //DEFAULT
        GF_TRANSPORT_RDMA,
} gf_transport_type;


typedef enum gf_rb_status_ {
        GF_RB_STATUS_NONE,
        GF_RB_STATUS_STARTED,
        GF_RB_STATUS_PAUSED,
} gf_rb_status_t;

struct glusterd_volinfo_ {
        char                    volname[GLUSTERD_MAX_VOLUME_NAME];
        int                     type;
        int                     brick_count;
        struct list_head        vol_list;
        struct list_head        bricks;
        glusterd_volume_status  status;
        int                     sub_count;
        int                     port;
        glusterd_store_handle_t *shandle;

        /* Defrag/rebalance related */
        gf_defrag_status_t      defrag_status;
        uint64_t                rebalance_files;
        uint64_t                rebalance_data;
        uint64_t                lookedup_files;
        glusterd_defrag_info_t  *defrag;

        /* Replace brick status */
        gf_rb_status_t          rb_status;
        glusterd_brickinfo_t    *src_brick;
        glusterd_brickinfo_t    *dst_brick;

        int                     version;
        uint32_t                cksum;
        gf_transport_type       transport_type;

        dict_t                  *dict;

        uuid_t                  volume_id;
        char                    *logdir;
};

typedef struct glusterd_volinfo_ glusterd_volinfo_t;

enum glusterd_op_ret {
        GLUSTERD_CONNECTION_AWAITED = 100,
};

enum glusterd_vol_comp_status_ {
        GLUSTERD_VOL_COMP_NONE = 0,
        GLUSTERD_VOL_COMP_SCS = 1,
        GLUSTERD_VOL_COMP_UPDATE_REQ,
        GLUSTERD_VOL_COMP_RJT,
};

#define GLUSTERD_DEFAULT_WORKDIR "/etc/glusterd"
#define GLUSTERD_DEFAULT_PORT    GF_DEFAULT_BASE_PORT
#define GLUSTERD_INFO_FILE      "glusterd.info"
#define GLUSTERD_VOLUME_DIR_PREFIX "vols"
#define GLUSTERD_PEER_DIR_PREFIX "peers"
#define GLUSTERD_VOLUME_INFO_FILE "info"
#define GLUSTERD_BRICK_INFO_DIR "bricks"
#define GLUSTERD_CKSUM_FILE "cksum"

/*All definitions related to replace brick */
#define RB_PUMP_START_CMD       "trusted.glusterfs.pump.start"
#define RB_PUMP_PAUSE_CMD       "trusted.glusterfs.pump.pause"
#define RB_PUMP_ABORT_CMD       "trusted.glusterfs.pump.abort"
#define RB_PUMP_STATUS_CMD      "trusted.glusterfs.pump.status"
#define RB_CLIENT_MOUNTPOINT    "rb_mount"
#define RB_CLIENTVOL_FILENAME   "rb_client.vol"
#define RB_DSTBRICK_PIDFILE     "rb_dst_brick.pid"
#define RB_DSTBRICKVOL_FILENAME "rb_dst_brick.vol"

#define GLUSTERD_UUID_LEN 50

typedef ssize_t (*gd_serialize_t) (struct iovec outmsg, void *args);

#define GLUSTERD_GET_NFS_DIR(path, priv)                                \
        do {                                                            \
                snprintf (path, PATH_MAX, "%s/nfs", priv->workdir);\
        } while (0);                                                    \

#define GLUSTERD_GET_VOLUME_DIR(path, volinfo, priv) \
        snprintf (path, PATH_MAX, "%s/vols/%s", priv->workdir,\
                  volinfo->volname);

#define GLUSTERD_GET_BRICK_DIR(path, volinfo, priv) \
        snprintf (path, PATH_MAX, "%s/%s/%s/%s", priv->workdir,\
                  GLUSTERD_VOLUME_DIR_PREFIX, volinfo->volname, \
                  GLUSTERD_BRICK_INFO_DIR);

#define GLUSTERD_GET_NFS_PIDFILE(pidfile)                               \
                snprintf (pidfile, PATH_MAX, "%s/nfs/run/nfs.pid", \
                          priv->workdir);                               \

#define GLUSTERD_REMOVE_SLASH_FROM_PATH(path,string) do {               \
                int i = 0;                                              \
                for (i = 1; i < strlen (path); i++) {                   \
                        string[i-1] = path[i];                          \
                        if (string[i-1] == '/')                         \
                                string[i-1] = '-';                      \
                }                                                       \
        } while (0)

#define GLUSTERD_GET_BRICK_PIDFILE(pidfile,volpath,hostname,brickpath) { \
                char exp_path[PATH_MAX] = {0,};                         \
                GLUSTERD_REMOVE_SLASH_FROM_PATH (brickpath, exp_path);  \
                snprintf (pidfile, PATH_MAX, "%s/run/%s-%s.pid",        \
                          volpath, hostname, exp_path);                 \
        }

#define GLUSTERD_STACK_DESTROY(frame) do {\
		void *__local = NULL;     \
                xlator_t *__xl = NULL;    \
                __xl = frame->this;       \
		__local = frame->local;   \
		frame->local = NULL;	  \
		STACK_DESTROY (frame->root);\
	} while (0)

int32_t
glusterd_brick_from_brickinfo (glusterd_brickinfo_t *brickinfo,
                               char **new_brick);
int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port);

int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *hostname,
                               int port, int32_t op_ret, int32_t op_errno);

int
glusterd_friend_find (uuid_t uuid, char *hostname,
                      glusterd_peerinfo_t **peerinfo);

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid, struct rpc_clnt    *rpc,
                     glusterd_peerinfo_t **friend,
                     gf_boolean_t restore,
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
glusterd_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                     rpc_clnt_event_t event,
                     void *data);
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
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *hostname, int port);

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                              int32_t op, int32_t status, char *op_errstr,
                              dict_t *rsp_dict);

int
glusterd_xfer_friend_remove_resp (rpcsvc_request_t *req, char *hostname, int port);

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                        uuid_t uuid);

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

int32_t
glusterd_delete_volume (rpcsvc_request_t *req, char *volname, int flags);

int
glusterd_handle_cli_delete_volume (rpcsvc_request_t *req);

int
glusterd_handle_cli_get_volume (rpcsvc_request_t *req);

int32_t
glusterd_get_volumes (rpcsvc_request_t *req, dict_t *dict, int32_t flags);

int32_t
glusterd_add_brick (rpcsvc_request_t *req, dict_t *dict);

int
glusterd_handle_add_brick (rpcsvc_request_t *req);

int32_t
glusterd_replace_brick (rpcsvc_request_t *req, dict_t *dict);

int
glusterd_handle_replace_brick (rpcsvc_request_t *req);

int
glusterd_handle_remove_brick (rpcsvc_request_t *req);

int
glusterd_handle_log_filename (rpcsvc_request_t *req);
int
glusterd_handle_log_locate (rpcsvc_request_t *req);
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

int
glusterd_handle_set_volume (rpcsvc_request_t *req);

int
glusterd_handle_reset_volume (rpcsvc_request_t *req);

int
glusterd_handle_gsync_set (rpcsvc_request_t *req);

int
glusterd_handle_fsm_log (rpcsvc_request_t *req);

int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *hostname);

int
glusterd_fetchspec_notify (xlator_t *this);

int32_t
glusterd_sync_volume (rpcsvc_request_t *req, dict_t *ctx);

int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count);

int
glusterd_restart_bricks(glusterd_conf_t *conf);

int32_t
glusterd_volume_txn (rpcsvc_request_t *req, char *volname, int flags,
                     glusterd_op_t op);

int
glusterd_validate_reconfopts (glusterd_volinfo_t *volinfo, dict_t *val_dict, char **op_errstr);
#endif
