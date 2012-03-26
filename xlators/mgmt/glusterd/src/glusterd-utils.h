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

#ifndef _GLUSTERD_UTILS_H
#define _GLUSTERD_UTILS_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
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
#include "rpc-clnt.h"
#include "protocol-common.h"

/* For brick search using path: partial or complete */
typedef enum {
        GF_PATH_COMPLETE = 0,
        GF_PATH_PARTIAL
} gf_path_match_t;

struct glusterd_lock_ {
        uuid_t  owner;
        time_t  timestamp;
};

typedef struct glusterd_voldict_ctx_ {
        dict_t  *dict;
        int     count;
        int     opt_count;
        char    *key_name;
        char    *val_name;
} glusterd_voldict_ctx_t;

int
glusterd_compare_lines (const void *a, const void *b);

typedef int (*glusterd_condition_func) (glusterd_volinfo_t *volinfo,
                                        glusterd_brickinfo_t *brickinfo,
                                        void *ctx);
typedef struct glusterd_lock_ glusterd_lock_t;

int32_t
glusterd_get_lock_owner (uuid_t *cur_owner);

int32_t
glusterd_lock (uuid_t new_owner);

int32_t
glusterd_unlock (uuid_t owner);

int32_t
glusterd_get_uuid (uuid_t *uuid);

int
glusterd_submit_reply (rpcsvc_request_t *req, void *arg,
                       struct iovec *payload, int payloadcount,
                       struct iobref *iobref, xdrproc_t xdrproc);

int
glusterd_submit_request (struct rpc_clnt *rpc, void *req,
                         call_frame_t *frame, rpc_clnt_prog_t *prog,
                         int procnum, struct iobref *iobref,
                         xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);
int32_t
glusterd_volinfo_new (glusterd_volinfo_t **volinfo);

char *
glusterd_auth_get_username (glusterd_volinfo_t *volinfo);

char *
glusterd_auth_get_password (glusterd_volinfo_t *volinfo);

int32_t
glusterd_auth_set_username (glusterd_volinfo_t *volinfo, char *username);

int32_t
glusterd_auth_set_password (glusterd_volinfo_t *volinfo, char *password);

void
glusterd_auth_cleanup (glusterd_volinfo_t *volinfo);

gf_boolean_t
glusterd_check_volume_exists (char *volname);

int32_t
glusterd_brickinfo_new (glusterd_brickinfo_t **brickinfo);

int32_t
glusterd_brickinfo_from_brick (char *brick, glusterd_brickinfo_t **brickinfo);

int32_t
glusterd_friend_cleanup (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_peer_destroy (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_peer_hostname_new (char *hostname, glusterd_peer_hostname_t **name);

int32_t
glusterd_volinfo_find (char *volname, glusterd_volinfo_t **volinfo);

int32_t
glusterd_service_stop(const char *service, char *pidfile, int sig,
                      gf_boolean_t force_kill);

int32_t
glusterd_resolve_brick (glusterd_brickinfo_t *brickinfo);

int32_t
glusterd_volume_start_glusterfs (glusterd_volinfo_t  *volinfo,
                                 glusterd_brickinfo_t   *brickinfo);

int32_t
glusterd_volume_stop_glusterfs (glusterd_volinfo_t  *volinfo,
                                glusterd_brickinfo_t   *brickinfo);

int32_t
glusterd_volinfo_delete (glusterd_volinfo_t *volinfo);

int32_t
glusterd_brickinfo_delete (glusterd_brickinfo_t *brickinfo);

gf_boolean_t
glusterd_is_cli_op_req (int32_t op);

int32_t
glusterd_volume_brickinfo_get_by_brick (char *brick,
                                        glusterd_volinfo_t *volinfo,
                                        glusterd_brickinfo_t **brickinfo,
                                        gf_path_match_t path_match);
int32_t
glusterd_is_local_addr (char *hostname);

int32_t
glusterd_build_volume_dict (dict_t **vols);

int32_t
glusterd_compare_friend_data (dict_t  *vols, int32_t *status);

int
glusterd_volume_compute_cksum (glusterd_volinfo_t  *volinfo);

void
glusterd_get_nodesvc_volfile (char *server, char *workdir,
                                    char *volfile, size_t len);

gf_boolean_t
glusterd_is_service_running (char *pidfile, int *pid);

gf_boolean_t
glusterd_is_nodesvc_running ();

gf_boolean_t
glusterd_is_nodesvc_running ();

void
glusterd_get_nodesvc_dir (char *server, char *workdir,
                                char *path, size_t len);
int32_t
glusterd_nfs_server_start ();

int32_t
glusterd_nfs_server_stop ();

int32_t
glusterd_shd_start ();

int32_t
glusterd_shd_stop ();

int32_t
glusterd_nodesvc_set_socket_filepath (char *rundir, uuid_t uuid,
                                      char *socketpath, int len);

struct rpc_clnt*
glusterd_pending_node_get_rpc (glusterd_pending_node_t *pending_node);

struct rpc_clnt*
glusterd_nodesvc_get_rpc (char *server);

int32_t
glusterd_nodesvc_set_rpc (char *server, struct rpc_clnt *rpc);

int32_t
glusterd_nodesvc_connect (char *server, char *socketpath);

void
glusterd_nodesvc_set_running (char *server, gf_boolean_t status);

gf_boolean_t
glusterd_nodesvc_is_running (char *server);

int
glusterd_remote_hostname_get (rpcsvc_request_t *req,
                              char *remote_host, int len);
int32_t
glusterd_import_friend_volumes (dict_t  *vols);
void
glusterd_set_volume_status (glusterd_volinfo_t  *volinfo,
                            glusterd_volume_status status);
int
glusterd_check_generate_start_nfs (void);

int
glusterd_check_generate_start_shd (void);

int
glusterd_nodesvcs_handle_graph_change (glusterd_volinfo_t *volinfo);

int
glusterd_nodesvcs_handle_reconfigure (glusterd_volinfo_t *volinfo);

int
glusterd_nodesvcs_start (glusterd_volinfo_t *volinfo);

int
glusterd_nodesvcs_stop (glusterd_volinfo_t *volinfo);

int32_t
glusterd_volume_count_get (void);
int32_t
glusterd_add_volume_to_dict (glusterd_volinfo_t *volinfo,
                             dict_t  *dict, int32_t count);
int
glusterd_get_brickinfo (xlator_t *this, const char *brickname, 
                        int port, gf_boolean_t localhost, 
                        glusterd_brickinfo_t **brickinfo);

void
glusterd_set_brick_status (glusterd_brickinfo_t  *brickinfo,
                            gf_brick_status_t status);

gf_boolean_t
glusterd_is_brick_started (glusterd_brickinfo_t  *brickinfo);

int
glusterd_friend_find_by_hostname (const char *hoststr,
                                  glusterd_peerinfo_t  **peerinfo);
int
glusterd_hostname_to_uuid (char *hostname, uuid_t uuid);

int
glusterd_friend_brick_belongs (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *brickinfo, void *uuid);
int
glusterd_all_volume_cond_check (glusterd_condition_func func, int status,
                                void *ctx);
int
glusterd_brick_start (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo);
int
glusterd_brick_stop (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo);

int
glusterd_is_defrag_on (glusterd_volinfo_t *volinfo);

int32_t
glusterd_volinfo_bricks_delete (glusterd_volinfo_t *volinfo);
int
glusterd_friend_find_by_uuid (uuid_t uuid,
                              glusterd_peerinfo_t  **peerinfo);
int
glusterd_new_brick_validate (char *brick, glusterd_brickinfo_t *brickinfo,
                             char *op_errstr, size_t len);
int32_t
glusterd_volume_brickinfos_delete (glusterd_volinfo_t *volinfo);

int32_t
glusterd_volume_brickinfo_get (uuid_t uuid, char *hostname, char *path,
                               glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t **brickinfo,
                               gf_path_match_t path_match);
int
glusterd_brickinfo_get (uuid_t uuid, char *hostname, char *path,
                       glusterd_brickinfo_t **brickinfo);
int
glusterd_is_rb_started (glusterd_volinfo_t *volinfo);

int
glusterd_is_rb_paused (glusterd_volinfo_t *volinfo);

int
glusterd_set_rb_status (glusterd_volinfo_t *volinfo, gf_rb_status_t status);

gf_boolean_t
glusterd_is_rb_ongoing (glusterd_volinfo_t *volinfo);

int
glusterd_rb_check_bricks (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *src_brick,
                          glusterd_brickinfo_t *dst_brick);

int
glusterd_brick_create_path (char *host, char *path, uuid_t uuid, mode_t mode,
                            char **op_errstr);
int
glusterd_sm_tr_log_transition_add (glusterd_sm_tr_log_t *log,
                                           int old_state, int new_state,
                                           int event);
int
glusterd_peerinfo_new (glusterd_peerinfo_t **peerinfo,
                       glusterd_friend_sm_state_t state,
                       uuid_t *uuid, const char *hostname);
int
glusterd_sm_tr_log_init (glusterd_sm_tr_log_t *log,
                         char * (*state_name_get) (int),
                         char * (*event_name_get) (int),
                         size_t  size);
void
glusterd_sm_tr_log_delete (glusterd_sm_tr_log_t *log);

int
glusterd_sm_tr_log_add_to_dict (dict_t *dict,
                                glusterd_sm_tr_log_t *circular_log);
int
glusterd_remove_pending_entry (struct list_head *list, void *elem);
int
glusterd_clear_pending_nodes (struct list_head *list);
gf_boolean_t
glusterd_peerinfo_is_uuid_unknown (glusterd_peerinfo_t *peerinfo);
int32_t
glusterd_brick_connect (glusterd_volinfo_t  *volinfo,
                        glusterd_brickinfo_t  *brickinfo);
int32_t
glusterd_brick_disconnect (glusterd_brickinfo_t *brickinfo);
int32_t
glusterd_delete_volume (glusterd_volinfo_t *volinfo);
int32_t
glusterd_delete_brick (glusterd_volinfo_t* volinfo,
                       glusterd_brickinfo_t *brickinfo);
int32_t
glusterd_delete_all_bricks (glusterd_volinfo_t* volinfo);
int
glusterd_restart_gsyncds (glusterd_conf_t *conf);
int
glusterd_start_gsync (glusterd_volinfo_t *master_vol, char *slave,
                      char *glusterd_uuid_str, char **op_errstr);
int32_t
glusterd_recreate_bricks (glusterd_conf_t *conf);
int32_t
glusterd_handle_upgrade_downgrade (dict_t *options, glusterd_conf_t *conf);

int
glusterd_add_brick_detail_to_dict (glusterd_volinfo_t *volinfo,
                                   glusterd_brickinfo_t *brickinfo,
                                   dict_t  *dict, int32_t count);

int32_t
glusterd_add_brick_to_dict (glusterd_volinfo_t *volinfo,
                            glusterd_brickinfo_t *brickinfo,
                            dict_t  *dict, int32_t count);

int32_t
glusterd_get_all_volnames (dict_t *dict);

gf_boolean_t
glusterd_is_fuse_available ();

int
glusterd_brick_statedump (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *brickinfo,
                          char *options, int option_cnt, char **op_errstr);
int
glusterd_nfs_statedump (char *options, int option_cnt, char **op_errstr);
gf_boolean_t
glusterd_is_volume_replicate (glusterd_volinfo_t *volinfo);
gf_boolean_t
glusterd_is_brick_decommissioned (glusterd_volinfo_t *volinfo, char *hostname,
                                  char *path);
gf_boolean_t
glusterd_friend_contains_vol_bricks (glusterd_volinfo_t *volinfo,
                                     uuid_t friend_uuid);
int
glusterd_friend_remove_cleanup_vols (uuid_t uuid);

gf_boolean_t
glusterd_chk_peers_connected_befriended (uuid_t skip_uuid);

void
glusterd_get_client_filepath (char *filepath,
                              glusterd_volinfo_t *volinfo,
                              gf_transport_type type);
void
glusterd_get_trusted_client_filepath (char *filepath,
                                      glusterd_volinfo_t *volinfo,
                                      gf_transport_type type);
int
glusterd_restart_rebalance (glusterd_conf_t *conf);

int32_t
glusterd_add_bricks_hname_path_to_dict (dict_t *dict);

int
glusterd_add_node_to_dict (char *server, dict_t *dict, int count);

char *
glusterd_uuid_to_hostname (uuid_t uuid);

glusterd_brickinfo_t*
glusterd_get_brickinfo_by_position (glusterd_volinfo_t *volinfo, uint32_t pos);

gf_boolean_t
glusterd_is_local_brick (xlator_t *this, glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo);
#endif
