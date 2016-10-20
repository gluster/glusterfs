/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_SNAP_UTILS_H
#define _GLUSTERD_SNAP_UTILS_H

int32_t
glusterd_snap_volinfo_find (char *volname, glusterd_snap_t *snap,
                            glusterd_volinfo_t **volinfo);

int32_t
glusterd_snap_volinfo_find_from_parent_volname (char *origin_volname,
                                      glusterd_snap_t *snap,
                                      glusterd_volinfo_t **volinfo);

int
glusterd_snap_volinfo_find_by_volume_id (uuid_t volume_id,
                                         glusterd_volinfo_t **volinfo);

int32_t
glusterd_add_snapd_to_dict (glusterd_volinfo_t *volinfo,
                            dict_t  *dict, int32_t count);

int
glusterd_compare_snap_time (struct cds_list_head *, struct cds_list_head *);

int
glusterd_compare_snap_vol_time (struct cds_list_head *, struct cds_list_head *);

int32_t
glusterd_snap_volinfo_restore (dict_t *dict, dict_t *rsp_dict,
                               glusterd_volinfo_t *new_volinfo,
                               glusterd_volinfo_t *snap_volinfo,
                               int32_t volcount);
int32_t
glusterd_snapobject_delete (glusterd_snap_t *snap);

int32_t
glusterd_cleanup_snaps_for_volume (glusterd_volinfo_t *volinfo);

int32_t
glusterd_missed_snapinfo_new (glusterd_missed_snap_info **missed_snapinfo);

int32_t
glusterd_missed_snap_op_new (glusterd_snap_op_t **snap_op);

int32_t
glusterd_add_missed_snaps_to_dict (dict_t *rsp_dict,
                                   glusterd_volinfo_t *snap_vol,
                                   glusterd_brickinfo_t *brickinfo,
                                   int32_t brick_number, int32_t op);

int32_t
glusterd_add_missed_snaps_to_export_dict (dict_t *peer_data);

int32_t
glusterd_import_friend_missed_snap_list (dict_t *peer_data);

int
gd_restore_snap_volume (dict_t *dict, dict_t *rsp_dict,
                        glusterd_volinfo_t *orig_vol,
                        glusterd_volinfo_t *snap_vol,
                        int32_t volcount);

int32_t
glusterd_mount_lvm_snapshot (glusterd_brickinfo_t *brickinfo,
                             char *brick_mount_path);

int32_t
glusterd_umount (const char *path);

int32_t
glusterd_add_snapshots_to_export_dict (dict_t *peer_data);

int32_t
glusterd_compare_friend_snapshots (dict_t *peer_data, char *peername,
                                   uuid_t peerid);

int32_t
glusterd_store_create_snap_dir (glusterd_snap_t *snap);

int32_t
glusterd_copy_file (const char *source, const char *destination);

int32_t
glusterd_copy_folder (const char *source, const char *destination);

int32_t
glusterd_get_geo_rep_session (char *slave_key, char *origin_volname,
                              dict_t *gsync_slaves_dict, char *session,
                              char *slave);

int32_t
glusterd_restore_geo_rep_files (glusterd_volinfo_t *snap_vol);

int
glusterd_restore_nfs_ganesha_file (glusterd_volinfo_t *src_vol,
                                   glusterd_snap_t *snap);
int32_t
glusterd_copy_quota_files (glusterd_volinfo_t *src_vol,
                           glusterd_volinfo_t *dest_vol,
                           gf_boolean_t *conf_present);

int
glusterd_copy_nfs_ganesha_file (glusterd_volinfo_t *src_vol,
                                glusterd_volinfo_t *dest_vol);

int
glusterd_snap_use_rsp_dict (dict_t *aggr, dict_t *rsp_dict);

int
gd_add_vol_snap_details_to_dict (dict_t *dict, char *prefix,
                                 glusterd_volinfo_t *volinfo);

int
gd_add_brick_snap_details_to_dict (dict_t *dict, char *prefix,
                                   glusterd_brickinfo_t *brickinfo);

int
gd_import_new_brick_snap_details (dict_t *dict, char *prefix,
                                  glusterd_brickinfo_t *brickinfo);

int
gd_import_volume_snap_details (dict_t *dict, glusterd_volinfo_t *volinfo,
                               char *prefix, char *volname);

int32_t
glusterd_snap_quorum_check (dict_t *dict, gf_boolean_t snap_volume,
                            char **op_errstr, uint32_t *op_errno);

int32_t
glusterd_snap_brick_create (glusterd_volinfo_t *snap_volinfo,
                            glusterd_brickinfo_t *brickinfo,
                            int32_t brick_count, int32_t clone);

int
glusterd_snapshot_restore_cleanup (dict_t *rsp_dict,
                                   char *volname,
                                   glusterd_snap_t *snap);

void
glusterd_get_snapd_dir (glusterd_volinfo_t *volinfo,
                        char *path, int path_len);

int
glusterd_is_snapd_enabled (glusterd_volinfo_t *volinfo);

int32_t
glusterd_check_and_set_config_limit (glusterd_conf_t *priv);

int32_t
glusterd_is_snap_soft_limit_reached (glusterd_volinfo_t *volinfo,
                                     dict_t *dict);

void
gd_get_snap_conf_values_if_present (dict_t *opts, uint64_t *sys_hard_limit,
                                    uint64_t *sys_soft_limit);
int
glusterd_get_snap_status_str (glusterd_snap_t *snapinfo, char *snap_status_str);

#endif

