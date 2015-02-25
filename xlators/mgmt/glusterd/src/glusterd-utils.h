/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_UTILS_H
#define _GLUSTERD_UTILS_H

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
#include "rpc-clnt.h"
#include "protocol-common.h"

#include "glusterfs3-xdr.h"
#include "glusterd-peer-utils.h"

#define GLUSTERD_SOCK_DIR "/var/run/gluster"
#define GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO(brickinfo, volinfo, brickid) do {\
        sprintf (brickinfo->brick_id, "%s-client-%d",\
                 volinfo->volname, brickid);\
} while (0)

#define glusterd_quorum_count(peerinfo, inquorum_count, active_count, _exit)\
                if (peerinfo->quorum_contrib == QUORUM_WAITING)\
                        goto _exit;\
                if (_is_contributing_to_quorum (peerinfo->quorum_contrib))\
                        inquorum_count = inquorum_count + 1;\
                if (active_count && (peerinfo->quorum_contrib == QUORUM_UP))\
                        *active_count = *active_count + 1;\

struct glusterd_lock_ {
        uuid_t  owner;
        time_t  timestamp;
};

typedef struct glusterd_dict_ctx_ {
        dict_t  *dict;
        int     opt_count;
        char    *key_name;
        char    *val_name;
        char    *prefix;
} glusterd_dict_ctx_t;

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
glusterd_to_cli (rpcsvc_request_t *req, gf_cli_rsp *arg, struct iovec *payload,
                 int payloadcount, struct iobref *iobref, xdrproc_t xdrproc,
                 dict_t *dict);

int
glusterd_submit_request (struct rpc_clnt *rpc, void *req,
                         call_frame_t *frame, rpc_clnt_prog_t *prog,
                         int procnum, struct iobref *iobref,
                         xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);
int32_t
glusterd_volinfo_new (glusterd_volinfo_t **volinfo);

int32_t
glusterd_volinfo_dup (glusterd_volinfo_t *volinfo,
                      glusterd_volinfo_t **dup_volinfo,
                      gf_boolean_t set_userauth);

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
glusterd_brickinfo_new_from_brick (char *brick, glusterd_brickinfo_t **brickinfo);

int32_t
glusterd_volinfo_find (char *volname, glusterd_volinfo_t **volinfo);

int
glusterd_volinfo_find_by_volume_id (uuid_t volume_id, glusterd_volinfo_t **volinfo);

int32_t
glusterd_service_stop(const char *service, char *pidfile, int sig,
                      gf_boolean_t force_kill);

int
glusterd_get_next_available_brickid (glusterd_volinfo_t *volinfo);

int
glusterd_validate_quorum (xlator_t *this, glusterd_op_t op, dict_t *dict,
                          char **op_errstr);

int32_t
glusterd_resolve_brick (glusterd_brickinfo_t *brickinfo);

int32_t
glusterd_volume_start_glusterfs (glusterd_volinfo_t  *volinfo,
                                 glusterd_brickinfo_t   *brickinfo,
                                 gf_boolean_t wait);

int32_t
glusterd_volume_stop_glusterfs (glusterd_volinfo_t  *volinfo,
                                glusterd_brickinfo_t   *brickinfo,
                                gf_boolean_t del_brick);

glusterd_volinfo_t *
glusterd_volinfo_ref (glusterd_volinfo_t *volinfo);

glusterd_volinfo_t *
glusterd_volinfo_unref (glusterd_volinfo_t *volinfo);

int32_t
glusterd_volinfo_delete (glusterd_volinfo_t *volinfo);

int32_t
glusterd_brickinfo_delete (glusterd_brickinfo_t *brickinfo);

gf_boolean_t
glusterd_is_cli_op_req (int32_t op);

int32_t
glusterd_volume_brickinfo_get_by_brick (char *brick,
                                        glusterd_volinfo_t *volinfo,
                                        glusterd_brickinfo_t **brickinfo);

int32_t
glusterd_add_volumes_to_export_dict (dict_t **peer_data);

int32_t
glusterd_compare_friend_data (dict_t *peer_data, int32_t *status,
                              char *hostname);

int
glusterd_compute_cksum (glusterd_volinfo_t  *volinfo,
                        gf_boolean_t is_quota_conf);

void
glusterd_set_socket_filepath (char *sock_filepath, char *sockpath, size_t len);

struct rpc_clnt*
glusterd_pending_node_get_rpc (glusterd_pending_node_t *pending_node);

int
glusterd_remote_hostname_get (rpcsvc_request_t *req,
                              char *remote_host, int len);
int32_t
glusterd_import_friend_volumes (dict_t *peer_data);
void
glusterd_set_volume_status (glusterd_volinfo_t  *volinfo,
                            glusterd_volume_status status);

int32_t
glusterd_volume_count_get (void);
int32_t
glusterd_add_volume_to_dict (glusterd_volinfo_t *volinfo,
                             dict_t  *dict, int32_t count,
                             char *prefix);
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
glusterd_friend_brick_belongs (glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *brickinfo, void *uuid);
int
glusterd_all_volume_cond_check (glusterd_condition_func func, int status,
                                void *ctx);
int
glusterd_brick_start (glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo,
                      gf_boolean_t wait);
int
glusterd_brick_stop (glusterd_volinfo_t *volinfo,
                     glusterd_brickinfo_t *brickinfo,
                     gf_boolean_t del_brick);

int
glusterd_is_defrag_on (glusterd_volinfo_t *volinfo);

int32_t
glusterd_volinfo_bricks_delete (glusterd_volinfo_t *volinfo);

int
glusterd_new_brick_validate (char *brick, glusterd_brickinfo_t *brickinfo,
                             char *op_errstr, size_t len);
int32_t
glusterd_volume_brickinfos_delete (glusterd_volinfo_t *volinfo);

int32_t
glusterd_volume_brickinfo_get (uuid_t uuid, char *hostname, char *path,
                               glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t **brickinfo);

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
glusterd_check_and_set_brick_xattr (char *host, char *path, uuid_t uuid,
                                    char **op_errstr, gf_boolean_t is_force);

int
glusterd_validate_and_create_brickpath (glusterd_brickinfo_t *brickinfo,
                                        uuid_t volume_id, char **op_errstr,
                                        gf_boolean_t is_force);
int
glusterd_sm_tr_log_transition_add (glusterd_sm_tr_log_t *log,
                                           int old_state, int new_state,
                                           int event);
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
glusterd_remove_pending_entry (struct cds_list_head *list, void *elem);
int
glusterd_clear_pending_nodes (struct cds_list_head *list);
int32_t
glusterd_brick_connect (glusterd_volinfo_t  *volinfo,
                        glusterd_brickinfo_t  *brickinfo, char *socketpath);
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
glusterd_spawn_daemons (void *opaque);

int
glusterd_restart_gsyncds (glusterd_conf_t *conf);

int
glusterd_start_gsync (glusterd_volinfo_t *master_vol, char *slave,
                      char *path_list, char *conf_path,
                      char *glusterd_uuid_str,
                      char **op_errstr, gf_boolean_t is_pause);
int
glusterd_get_local_brickpaths (glusterd_volinfo_t *volinfo,
                               char **pathlist);

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

int
glusterd_quotad_statedump (char *options, int option_cnt, char **op_errstr);

gf_boolean_t
glusterd_is_volume_replicate (glusterd_volinfo_t *volinfo);

gf_boolean_t
glusterd_is_brick_decommissioned (glusterd_volinfo_t *volinfo, char *hostname,
                                  char *path);
int
glusterd_friend_contains_vol_bricks (glusterd_volinfo_t *volinfo,
                                     uuid_t friend_uuid);
int
glusterd_friend_remove_cleanup_vols (uuid_t uuid);

int
glusterd_get_client_filepath (char *filepath,
                              glusterd_volinfo_t *volinfo,
                              gf_transport_type type);
int
glusterd_get_trusted_client_filepath (char *filepath,
                                      glusterd_volinfo_t *volinfo,
                                      gf_transport_type type);
int
glusterd_restart_rebalance (glusterd_conf_t *conf);

int32_t
glusterd_add_bricks_hname_path_to_dict (dict_t *dict,
                                        glusterd_volinfo_t *volinfo);

int
glusterd_add_node_to_dict (char *server, dict_t *dict, int count,
                           dict_t *vol_opts);

int
glusterd_get_dist_leaf_count (glusterd_volinfo_t *volinfo);

glusterd_brickinfo_t*
glusterd_get_brickinfo_by_position (glusterd_volinfo_t *volinfo, uint32_t pos);

gf_boolean_t
glusterd_is_local_brick (xlator_t *this, glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo);
int
glusterd_validate_volume_id (dict_t *op_dict, glusterd_volinfo_t *volinfo);

int
glusterd_defrag_volume_status_update (glusterd_volinfo_t *volinfo,
                                      dict_t *rsp_dict);

int
glusterd_check_files_identical (char *filename1, char *filename2,
                                gf_boolean_t *identical);

int
glusterd_check_topology_identical (const char *filename1,
                                   const char *filename2,
                                   gf_boolean_t *identical);

void
glusterd_volinfo_reset_defrag_stats (glusterd_volinfo_t *volinfo);
int
glusterd_volset_help (dict_t *dict, char **op_errstr);

int32_t
glusterd_sync_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int32_t
glusterd_gsync_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict, char *op_errstr);
int32_t
glusterd_rb_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_profile_volume_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_volume_status_copy_to_op_ctx_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_volume_rebalance_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_volume_heal_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_sys_exec_output_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int32_t
glusterd_handle_node_rsp (dict_t *req_ctx, void *pending_entry,
                          glusterd_op_t op, dict_t *rsp_dict, dict_t *op_ctx,
                          char **op_errstr, gd_node_type type);
int
glusterd_volume_rebalance_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);
int
glusterd_volume_heal_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);

int32_t
glusterd_check_if_quota_trans_enabled (glusterd_volinfo_t *volinfo);
int
glusterd_volume_quota_copy_to_op_ctx_dict (dict_t *aggr, dict_t *rsp);
int
_profile_volume_add_brick_rsp (dict_t *this, char *key, data_t *value,
                             void *data);
int
glusterd_profile_volume_brick_rsp (void *pending_entry,
                                   dict_t *rsp_dict, dict_t *op_ctx,
                                   char **op_errstr, gd_node_type type);

int32_t
glusterd_set_originator_uuid (dict_t *dict);

/* Should be used only when an operation is in progress, as that is the only
 * time a lock_owner is set
 */
gf_boolean_t
is_origin_glusterd (dict_t *dict);

gf_boolean_t
glusterd_is_quorum_changed (dict_t *options, char *option, char *value);

int
glusterd_do_quorum_action ();

int
glusterd_get_quorum_cluster_counts (xlator_t *this, int *active_count,
                                    int *quorum_count,
                                    struct cds_list_head *peer_list,
                                    gf_boolean_t _local__xaction_peers);

int
glusterd_get_next_global_opt_version_str (dict_t *opts, char **version_str);
gf_boolean_t
glusterd_is_quorum_option (char *option);
gf_boolean_t
glusterd_is_volume_in_server_quorum (glusterd_volinfo_t *volinfo);
gf_boolean_t
glusterd_is_any_volume_in_server_quorum (xlator_t *this);
gf_boolean_t
does_gd_meet_server_quorum (xlator_t *this, struct cds_list_head *peers_list,
                            gf_boolean_t _local__xaction_peers);

int
glusterd_generate_and_set_task_id (dict_t *dict, char *key);

int
glusterd_validate_and_set_gfid (dict_t *op_ctx, dict_t *req_dict,
                                char **op_errstr);

int
glusterd_copy_uuid_to_dict (uuid_t uuid, dict_t *dict, char *key);

gf_boolean_t
glusterd_is_same_address (char *name1, char *name2);

void
gd_update_volume_op_versions (glusterd_volinfo_t *volinfo);

int
op_version_check (xlator_t *this, int min_op_version, char *msg, int msglen);

gf_boolean_t
gd_is_remove_brick_committed (glusterd_volinfo_t *volinfo);

int
glusterd_get_slave_details_confpath (glusterd_volinfo_t *volinfo,
                                     dict_t *dict, char **slave_url,
                                     char **slave_host, char **slave_vol,
                                     char **conf_path, char **op_errstr);

int
glusterd_get_slave_info (char *slave,
                         char **slave_url, char **hostname,
                         char **slave_vol, char **op_errstr);

int
glusterd_get_statefile_name (glusterd_volinfo_t *volinfo, char *slave,
                             char *conf_path, char **statefile,
                             gf_boolean_t *is_template_in_use);

int
glusterd_gsync_read_frm_status (char *path, char *buf, size_t blen);

int
glusterd_create_status_file (char *master, char *slave, char *slave_url,
                             char *slave_vol, char *status);

int
glusterd_check_restart_gsync_session (glusterd_volinfo_t *volinfo, char *slave,
                                      dict_t *resp_dict, char *path_list,
                                      char *conf_path, gf_boolean_t is_force);

int
glusterd_check_gsync_running_local (char *master, char *slave,
                                    char *conf_path,
                                    gf_boolean_t *is_run);

gf_boolean_t
glusterd_is_status_tasks_op (glusterd_op_t op, dict_t *dict);

gf_boolean_t
gd_should_i_start_rebalance  (glusterd_volinfo_t *volinfo);

int
glusterd_is_volume_quota_enabled (glusterd_volinfo_t *volinfo);

gf_boolean_t
glusterd_all_volumes_with_quota_stopped ();

void
glusterd_clean_up_quota_store (glusterd_volinfo_t *volinfo);

int
glusterd_store_quota_conf_skip_header (xlator_t *this, int fd);

int
glusterd_store_quota_conf_stamp_header (xlator_t *this, int fd);

int
glusterd_remove_auxiliary_mount (char *volname);

gf_boolean_t
glusterd_status_has_tasks (int cmd);

int
gd_stop_rebalance_process (glusterd_volinfo_t *volinfo);

rpc_clnt_t *
glusterd_rpc_clnt_unref (glusterd_conf_t *conf, rpc_clnt_t *rpc);

int32_t
glusterd_compare_volume_name(struct cds_list_head *, struct cds_list_head *);

char*
glusterd_get_brick_mount_device (char *brick_path);

struct mntent *
glusterd_get_mnt_entry_info (char *mnt_pt, char *buff, int buflen,
                             struct mntent *entry_ptr);

int
glusterd_get_brick_root (char *path, char **mount_point);

int32_t
glusterd_lvm_snapshot_remove (dict_t *rsp_dict, glusterd_volinfo_t *snap_vol);

gf_boolean_t
gd_vol_is_geo_rep_active (glusterd_volinfo_t *volinfo);

int32_t
glusterd_get_brick_mount_dir (char *brickpath, char *hostname, char *mount_dir);

int32_t
glusterd_aggr_brick_mount_dirs (dict_t *aggr, dict_t *rsp_dict);

int32_t
glusterd_take_lvm_snapshot (glusterd_brickinfo_t *brickinfo,
                            char *origin_brick_path);

void
glusterd_launch_synctask (synctask_fn_t fn, void *opaque);

int
glusterd_enable_default_options (glusterd_volinfo_t *volinfo, char *option);

int
glusterd_unlink_file (char *sock_file_path);

int32_t
glusterd_find_brick_mount_path (char *brick_path, int32_t brick_count,
                                char **brick_mount_path);
/*
 * Function to retrieve list of snap volnames and their uuids
 */
int glusterd_snapshot_get_volnames_uuids (dict_t *dict,
           char *volname, gf_getsnap_name_uuid_rsp *snap_info_rsp);

int
glusterd_update_mntopts (char *brick_path, glusterd_brickinfo_t *brickinfo);

int
glusterd_update_fs_label (glusterd_brickinfo_t *brickinfo);

int
glusterd_get_volopt_content (dict_t *dict, gf_boolean_t xml_out);

int
glusterd_get_default_val_for_volopt (dict_t *dict, gf_boolean_t all_opts,
                                     char *key, char *orig_key,
                                     dict_t *vol_dict, char **err_str);

int
glusterd_check_client_op_version_support (char *volname, uint32_t op_version,
                                          char **op_errstr);

gf_boolean_t
glusterd_have_peers ();

void
glusterd_op_clear_xaction_peers ();

void
glusterd_get_rebalance_volfile (glusterd_volinfo_t *volinfo,
                                char *path, int path_len);

int32_t
glusterd_brickinfo_dup (glusterd_brickinfo_t *brickinfo,
                        glusterd_brickinfo_t *dup_brickinfo);

int
glusterd_vol_add_quota_conf_to_dict (glusterd_volinfo_t *volinfo, dict_t *load,
                                     int vol_idx, char *prefix);

int32_t
glusterd_import_volinfo (dict_t *peer_data, int count,
                         glusterd_volinfo_t **volinfo,
                         char *prefix);

int
glusterd_import_quota_conf (dict_t *peer_data, int vol_idx,
                            glusterd_volinfo_t *new_volinfo,
                            char *prefix);

gf_boolean_t
glusterd_is_shd_compatible_volume (glusterd_volinfo_t *volinfo);


gf_boolean_t
glusterd_are_all_volumes_stopped ();

gf_boolean_t
glusterd_all_shd_compatible_volumes_stopped ();

void
glusterd_nfs_pmap_deregister ();

gf_boolean_t
glusterd_is_volume_started (glusterd_volinfo_t  *volinfo);

void
glusterd_list_add_order (struct cds_list_head *new, struct cds_list_head *head,
                        int (*compare)(struct cds_list_head *,
                                       struct cds_list_head *));

#endif
