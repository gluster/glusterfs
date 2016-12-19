/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <inttypes.h>

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif
#include <dlfcn.h>

#include "dict.h"
#include "syscall.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-messages.h"
#include "glusterd-store.h"
#include "glusterd-volgen.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-svc-helper.h"
#include "glusterd-snapd-svc-helper.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-server-quorum.h"
#include "glusterd-messages.h"
#include "glusterd-errno.h"

/*
 *  glusterd_snap_geo_rep_restore:
 *      This function restores the atime and mtime of marker.tstamp
 *      if present from snapped marker.tstamp file.
 */

int32_t
glusterd_snapobject_delete (glusterd_snap_t *snap)
{
        if (snap == NULL) {
                gf_msg(THIS->name, GF_LOG_WARNING, 0,
                       GD_MSG_PARAM_NULL, "snap is NULL");
                return -1;
        }

        cds_list_del_init (&snap->snap_list);
        cds_list_del_init (&snap->volumes);
        if (LOCK_DESTROY(&snap->lock))
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        GD_MSG_LOCK_DESTROY_FAILED,
                        "Failed destroying lock"
                        "of snap %s", snap->snapname);

        GF_FREE (snap->description);
        GF_FREE (snap);

        return 0;
}


/*
 * This function is to be called only from glusterd_peer_detach_cleanup()
 * as this continues to delete snaps inspite of faiure while deleting
 * one, as we don't want to fail peer_detach in such a case.
 */
int
glusterd_cleanup_snaps_for_volume (glusterd_volinfo_t *volinfo)
{
        int32_t                  op_ret         = 0;
        int32_t                  ret            = 0;
        xlator_t                *this           = NULL;
        glusterd_volinfo_t      *snap_vol       = NULL;
        glusterd_volinfo_t      *dummy_snap_vol = NULL;
        glusterd_snap_t         *snap           = NULL;

        this = THIS;
        GF_ASSERT (this);

        cds_list_for_each_entry_safe (snap_vol, dummy_snap_vol,
                                      &volinfo->snap_volumes,
                                      snapvol_list) {
                ret = glusterd_store_delete_volume (snap_vol);
                if (ret) {
                        gf_msg(this->name, GF_LOG_WARNING, 0,
                               GD_MSG_VOL_DELETE_FAIL, "Failed to remove "
                               "volume %s from store", snap_vol->volname);
                        op_ret = ret;
                        continue;
                }

                ret = glusterd_volinfo_delete (snap_vol);
                if (ret) {
                        gf_msg(this->name, GF_LOG_WARNING, 0,
                               GD_MSG_VOL_DELETE_FAIL, "Failed to remove "
                              "volinfo %s ", snap_vol->volname);
                        op_ret = ret;
                        continue;
                }

                snap = snap_vol->snapshot;
                ret = glusterd_store_delete_snap (snap);
                if (ret) {
                        gf_msg(this->name, GF_LOG_WARNING, 0,
                               GD_MSG_VOL_DELETE_FAIL, "Failed to remove "
                               "snap %s from store", snap->snapname);
                        op_ret = ret;
                        continue;
                }

                ret = glusterd_snapobject_delete (snap);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOL_DELETE_FAIL, "Failed to delete "
                                "snap object %s", snap->snapname);
                        op_ret = ret;
                        continue;
                }
        }

        return op_ret;
}



int
glusterd_snap_geo_rep_restore (glusterd_volinfo_t *snap_volinfo,
                               glusterd_volinfo_t *new_volinfo)
{
        char                    vol_tstamp_file[PATH_MAX]  = {0,};
        char                    snap_tstamp_file[PATH_MAX] = {0,};
        glusterd_conf_t         *priv                      = NULL;
        xlator_t                *this                      = NULL;
        int                     geo_rep_indexing_on        = 0;
        int                     ret                        = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (snap_volinfo);
        GF_ASSERT (new_volinfo);

        priv = this->private;
        GF_ASSERT (priv);

        /* Check if geo-rep indexing is enabled, if yes, we need restore
         * back the mtime of 'marker.tstamp' file.
         */
        geo_rep_indexing_on = glusterd_volinfo_get_boolean (new_volinfo,
                                                            VKEY_MARKER_XTIME);
        if (geo_rep_indexing_on == -1) {
                gf_msg_debug (this->name, 0, "Failed"
                        " to check whether geo-rep-indexing enabled or not");
                ret = 0;
                goto out;
        }

        if (geo_rep_indexing_on == 1) {
                GLUSTERD_GET_VOLUME_DIR (vol_tstamp_file, new_volinfo, priv);
                strncat (vol_tstamp_file, "/marker.tstamp",
                         PATH_MAX - strlen(vol_tstamp_file) - 1);
                GLUSTERD_GET_VOLUME_DIR (snap_tstamp_file, snap_volinfo, priv);
                strncat (snap_tstamp_file, "/marker.tstamp",
                         PATH_MAX - strlen(snap_tstamp_file) - 1);
                ret = gf_set_timestamp (snap_tstamp_file, vol_tstamp_file);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_TSTAMP_SET_FAIL,
                                "Unable to set atime and mtime of %s as of %s",
                                vol_tstamp_file, snap_tstamp_file);
                        goto out;
                }
        }

out:
        return ret;
}

/* This function will copy snap volinfo to the new
 * passed volinfo and regenerate backend store files
 * for the restored snap.
 *
 * @param new_volinfo   new volinfo
 * @param snap_volinfo  volinfo of snap volume
 *
 * @return 0 on success and -1 on failure
 *
 * TODO: Duplicate all members of volinfo, e.g. geo-rep sync slaves
 */
int32_t
glusterd_snap_volinfo_restore (dict_t *dict, dict_t *rsp_dict,
                               glusterd_volinfo_t *new_volinfo,
                               glusterd_volinfo_t *snap_volinfo,
                               int32_t volcount)
{
        char                    *value          = NULL;
        char                    key[PATH_MAX]   = "";
        int32_t                 brick_count     = -1;
        int32_t                 ret             = -1;
        xlator_t                *this           = NULL;
        glusterd_brickinfo_t    *brickinfo      = NULL;
        glusterd_brickinfo_t    *new_brickinfo  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        GF_VALIDATE_OR_GOTO (this->name, new_volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, snap_volinfo, out);

        brick_count = 0;
        cds_list_for_each_entry (brickinfo, &snap_volinfo->bricks, brick_list) {
                brick_count++;
                ret = glusterd_brickinfo_new (&new_brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_NEW_INFO_FAIL, "Failed to create "
                                "new brickinfo");
                        goto out;
                }

                /* Duplicate brickinfo */
                ret = glusterd_brickinfo_dup (brickinfo, new_brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_SET_INFO_FAIL, "Failed to dup "
                                "brickinfo");
                        goto out;
                }

                /* Fetch values if present in dict These values won't
                 * be present in case of a missed restore. In that case
                 * it's fine to use the local node's value
                 */
                snprintf (key, sizeof (key), "snap%d.brick%d.path",
                          volcount, brick_count);
                ret = dict_get_str (dict, key, &value);
                if (!ret)
                        strncpy (new_brickinfo->path, value,
                                 sizeof(new_brickinfo->path));

                snprintf (key, sizeof (key), "snap%d.brick%d.snap_status",
                          volcount, brick_count);
                ret = dict_get_int32 (dict, key, &new_brickinfo->snap_status);

                snprintf (key, sizeof (key), "snap%d.brick%d.device_path",
                          volcount, brick_count);
                ret = dict_get_str (dict, key, &value);
                if (!ret)
                        strncpy (new_brickinfo->device_path, value,
                                 sizeof(new_brickinfo->device_path));

                snprintf (key, sizeof (key), "snap%d.brick%d.fs_type",
                          volcount, brick_count);
                ret = dict_get_str (dict, key, &value);
                if (!ret)
                        strncpy (new_brickinfo->fstype, value,
                                 sizeof(new_brickinfo->fstype));

                snprintf (key, sizeof (key), "snap%d.brick%d.mnt_opts",
                          volcount, brick_count);
                ret = dict_get_str (dict, key, &value);
                if (!ret)
                        strncpy (new_brickinfo->mnt_opts, value,
                                 sizeof(new_brickinfo->mnt_opts));

                /* If the brick is not of this peer, or snapshot is missed *
                 * for the brick do not replace the xattr for it */
                if ((!gf_uuid_compare (brickinfo->uuid, MY_UUID)) &&
                    (brickinfo->snap_status != -1)) {
                        /* We need to replace the volume id of all the bricks
                         * to the volume id of the origin volume. new_volinfo
                         * has the origin volume's volume id*/
                        ret = sys_lsetxattr (new_brickinfo->path,
                                             GF_XATTR_VOL_ID_KEY,
                                             new_volinfo->volume_id,
                                             sizeof (new_volinfo->volume_id),
                                             XATTR_REPLACE);
                        if (ret == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SETXATTR_FAIL, "Failed to "
                                        "set extended attribute %s on %s. "
                                        "Reason: %s, snap: %s",
                                        GF_XATTR_VOL_ID_KEY,
                                        new_brickinfo->path, strerror (errno),
                                        new_volinfo->volname);
                                goto out;
                        }
                }

                /* If a snapshot is pending for this brick then
                 * restore should also be pending
                 */
                if (brickinfo->snap_status == -1) {
                        /* Adding missed delete to the dict */
                        ret = glusterd_add_missed_snaps_to_dict
                                                (rsp_dict,
                                                 snap_volinfo,
                                                 brickinfo,
                                                 brick_count,
                                                 GF_SNAP_OPTION_TYPE_RESTORE);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_MISSEDSNAP_INFO_SET_FAIL,
                                        "Failed to add missed snapshot info "
                                        "for %s:%s in the rsp_dict",
                                        brickinfo->hostname,
                                        brickinfo->path);
                                goto out;
                        }
                }

                cds_list_add_tail (&new_brickinfo->brick_list,
                                   &new_volinfo->bricks);
                /* ownership of new_brickinfo is passed to new_volinfo */
                new_brickinfo = NULL;
        }

        /* Regenerate all volfiles */
        ret = glusterd_create_volfiles_and_notify_services (new_volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "Failed to regenerate volfiles");
                goto out;
        }

        /* Restore geo-rep marker.tstamp's timestamp */
        ret = glusterd_snap_geo_rep_restore (snap_volinfo, new_volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TSTAMP_SET_FAIL,
                        "Geo-rep: marker.tstamp's timestamp restoration failed");
                goto out;
        }

out:
        if (ret && (NULL != new_brickinfo)) {
                (void) glusterd_brickinfo_delete (new_brickinfo);
        }

        return ret;
}

int
glusterd_snap_volinfo_find_by_volume_id (uuid_t volume_id,
                                         glusterd_volinfo_t **volinfo)
{
        int32_t                  ret     = -1;
        xlator_t                *this    = NULL;
        glusterd_volinfo_t      *voliter = NULL;
        glusterd_snap_t         *snap    = NULL;
        glusterd_conf_t         *priv    = NULL;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (volinfo);

        if (gf_uuid_is_null(volume_id)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_UUID_NULL, "Volume UUID is NULL");
                goto out;
        }

        cds_list_for_each_entry (snap, &priv->snapshots, snap_list) {
                cds_list_for_each_entry (voliter, &snap->volumes, vol_list) {
                        if (gf_uuid_compare (volume_id, voliter->volume_id))
                                continue;
                        *volinfo = voliter;
                        ret = 0;
                        goto out;
                }
        }

        gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_NOT_FOUND,
                "Snap volume not found");
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_snap_volinfo_find (char *snap_volname, glusterd_snap_t *snap,
                            glusterd_volinfo_t **volinfo)
{
        int32_t                  ret         = -1;
        xlator_t                *this        = NULL;
        glusterd_volinfo_t      *snap_vol    = NULL;
        glusterd_conf_t         *priv        = NULL;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (snap);
        GF_ASSERT (snap_volname);

        cds_list_for_each_entry (snap_vol, &snap->volumes, vol_list) {
                if (!strcmp (snap_vol->volname, snap_volname)) {
                        ret = 0;
                        *volinfo = snap_vol;
                        goto out;
                }
        }

        gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                GD_MSG_SNAP_NOT_FOUND, "Snap volume %s not found",
                snap_volname);
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_snap_volinfo_find_from_parent_volname (char *origin_volname,
                                      glusterd_snap_t *snap,
                                      glusterd_volinfo_t **volinfo)
{
        int32_t                  ret         = -1;
        xlator_t                *this        = NULL;
        glusterd_volinfo_t      *snap_vol    = NULL;
        glusterd_conf_t         *priv        = NULL;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (snap);
        GF_ASSERT (origin_volname);

        cds_list_for_each_entry (snap_vol, &snap->volumes, vol_list) {
                if (!strcmp (snap_vol->parent_volname, origin_volname)) {
                        ret = 0;
                        *volinfo = snap_vol;
                        goto out;
                }
        }

        gf_msg_debug (this->name, 0, "Snap volume not found(snap: %s, "
                "origin-volume: %s", snap->snapname, origin_volname);

out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Exports a bricks snapshot details only if required
 *
 * The details will be exported only if the cluster op-version is greather than
 * 4, ie. snapshot is supported in the cluster
 */
int
gd_add_brick_snap_details_to_dict (dict_t *dict, char *prefix,
                                   glusterd_brickinfo_t *brickinfo)
{
        int ret = -1;
        xlator_t *this = NULL;
        glusterd_conf_t *conf = NULL;
        char key[256] = {0,};

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (brickinfo != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.snap_status", prefix);
        ret = dict_set_int32 (dict, key, brickinfo->snap_status);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_STATUS_FAIL,
                        "Failed to set snap_status for %s:%s",
                        brickinfo->hostname, brickinfo->path);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.device_path", prefix);
        ret = dict_set_str (dict, key, brickinfo->device_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set snap_device for %s:%s",
                         brickinfo->hostname, brickinfo->path);
                goto out;
        }

        snprintf (key, sizeof (key), "%s.fs_type", prefix);
        ret = dict_set_str (dict, key, brickinfo->fstype);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set fstype for %s:%s",
                         brickinfo->hostname, brickinfo->path);
                goto out;
        }

        snprintf (key, sizeof (key), "%s.mnt_opts", prefix);
        ret = dict_set_str (dict, key, brickinfo->mnt_opts);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRK_MOUNTOPTS_FAIL,
                        "Failed to set mnt_opts for %s:%s",
                         brickinfo->hostname, brickinfo->path);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.mount_dir", prefix);
        ret = dict_set_str (dict, key, brickinfo->mount_dir);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to set mount_dir for %s:%s",
                         brickinfo->hostname, brickinfo->path);

out:
        return ret;
}

/* Exports a volumes snapshot details only if required.
 *
 * The snapshot details will only be exported if the cluster op-version is
 * greater than 4, ie. snapshot is supported in the cluster
 */
int
gd_add_vol_snap_details_to_dict (dict_t *dict, char *prefix,
                                 glusterd_volinfo_t *volinfo)
{
        int ret = -1;
        xlator_t *this = NULL;
        glusterd_conf_t *conf = NULL;
        char key[256] = {0,};

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (volinfo != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.restored_from_snap", prefix);
        ret = dict_set_dynstr_with_alloc
                                  (dict, key,
                                   uuid_utoa (volinfo->restored_from_snap));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Unable to set %s for volume"
                        "%s", key, volinfo->volname);
                goto out;
        }

        if (strlen (volinfo->parent_volname) > 0) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.parent_volname", prefix);
                ret = dict_set_dynstr_with_alloc (dict, key,
                                                  volinfo->parent_volname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "Unable to set %s "
                                "for volume %s", key, volinfo->volname);
                        goto out;
                }
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.is_snap_volume", prefix);
        ret = dict_set_uint32 (dict, key, volinfo->is_snap_volume);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Unable to set %s for volume"
                        "%s", key, volinfo->volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.snap-max-hard-limit", prefix);
        ret = dict_set_uint64 (dict, key, volinfo->snap_max_hard_limit);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Unable to set %s for volume"
                        "%s", key, volinfo->volname);
        }

out:
        return ret;
}

int32_t
glusterd_add_missed_snaps_to_export_dict (dict_t *peer_data)
{
        char                           name_buf[PATH_MAX]   = "";
        char                           value[PATH_MAX]      = "";
        int32_t                        missed_snap_count    = 0;
        int32_t                        ret                  = -1;
        glusterd_conf_t               *priv                 = NULL;
        glusterd_missed_snap_info     *missed_snapinfo      = NULL;
        glusterd_snap_op_t            *snap_opinfo          = NULL;
        xlator_t                      *this                 = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_data);

        priv = this->private;
        GF_ASSERT (priv);

        /* Add the missed_entries in the dict */
        cds_list_for_each_entry (missed_snapinfo, &priv->missed_snaps_list,
                                 missed_snaps) {
                cds_list_for_each_entry (snap_opinfo,
                                         &missed_snapinfo->snap_ops,
                                         snap_ops_list) {
                        snprintf (name_buf, sizeof(name_buf),
                                  "missed_snaps_%d", missed_snap_count);
                        snprintf (value, sizeof(value), "%s:%s=%s:%d:%s:%d:%d",
                                  missed_snapinfo->node_uuid,
                                  missed_snapinfo->snap_uuid,
                                  snap_opinfo->snap_vol_id,
                                  snap_opinfo->brick_num,
                                  snap_opinfo->brick_path,
                                  snap_opinfo->op,
                                  snap_opinfo->status);

                        ret = dict_set_dynstr_with_alloc (peer_data, name_buf,
                                                          value);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to set %s",
                                        name_buf);
                                goto out;
                        }
                        missed_snap_count++;
                }
        }

        ret = dict_set_int32 (peer_data, "missed_snap_count",
                              missed_snap_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set missed_snap_count");
                goto out;
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_add_snap_to_dict (glusterd_snap_t *snap, dict_t *peer_data,
                           int32_t snap_count)
{
        char                    buf[NAME_MAX]    = "";
        char                    prefix[NAME_MAX] = "";
        int32_t                 ret              = -1;
        int32_t                 volcount         = 0;
        glusterd_volinfo_t     *volinfo          = NULL;
        glusterd_brickinfo_t   *brickinfo        = NULL;
        gf_boolean_t            host_bricks      = _gf_false;
        xlator_t               *this             = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (snap);
        GF_ASSERT (peer_data);

        snprintf (prefix, sizeof(prefix), "snap%d", snap_count);

        cds_list_for_each_entry (volinfo, &snap->volumes, vol_list) {
                volcount++;
                ret = glusterd_add_volume_to_dict (volinfo, peer_data,
                                                   volcount, prefix);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to add snap:%s volume:%s "
                                "to peer_data dict for handshake",
                                snap->snapname, volinfo->volname);
                        goto out;
                }

                if (glusterd_is_volume_quota_enabled (volinfo)) {

                        ret = glusterd_vol_add_quota_conf_to_dict (volinfo,
                                                                   peer_data,
                                                                   volcount,
                                                                   prefix);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Failed to add quota conf for "
                                        "snap:%s volume:%s to peer_data "
                                        "dict for handshake", snap->snapname,
                                        volinfo->volname);
                                goto out;
                        }
                }

                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        if (!gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                                host_bricks = _gf_true;
                                break;
                        }
                }
        }

        snprintf (buf, sizeof(buf), "%s.host_bricks", prefix);
        ret = dict_set_int8 (peer_data, buf, (int8_t) host_bricks);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set host_bricks for snap %s",
                        snap->snapname);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.volcount", prefix);
        ret = dict_set_int32 (peer_data, buf, volcount);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set volcount for snap %s",
                        snap->snapname);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.snapname", prefix);
        ret = dict_set_dynstr_with_alloc (peer_data, buf, snap->snapname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set snapname for snap %s",
                        snap->snapname);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.snap_id", prefix);
        ret = dict_set_dynstr_with_alloc (peer_data, buf,
                                          uuid_utoa (snap->snap_id));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set snap_id for snap %s",
                        snap->snapname);
                goto out;
        }

        if (snap->description) {
                snprintf (buf, sizeof(buf), "%s.snapid", prefix);
                ret = dict_set_dynstr_with_alloc (peer_data, buf,
                                                  snap->description);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set description for snap %s",
                                snap->snapname);
                        goto out;
                }
        }

        snprintf (buf, sizeof(buf), "%s.time_stamp", prefix);
        ret = dict_set_int64 (peer_data, buf, (int64_t)snap->time_stamp);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set time_stamp for snap %s",
                        snap->snapname);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.snap_restored", prefix);
        ret = dict_set_int8 (peer_data, buf, snap->snap_restored);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set snap_restored for snap %s",
                        snap->snapname);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.snap_status", prefix);
        ret = dict_set_int32 (peer_data, buf, snap->snap_status);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set snap_status for snap %s",
                        snap->snapname);
                goto out;
        }
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_add_snapshots_to_export_dict (dict_t *peer_data)
{
        int32_t                 snap_count = 0;
        int32_t                 ret        = -1;
        glusterd_conf_t        *priv       = NULL;
        glusterd_snap_t        *snap       = NULL;
        xlator_t               *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (peer_data);

        cds_list_for_each_entry (snap, &priv->snapshots, snap_list) {
                snap_count++;
                ret = glusterd_add_snap_to_dict (snap, peer_data, snap_count);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to add snap(%s) to the "
                                " peer_data dict for handshake",
                                snap->snapname);
                        goto out;
                }
        }

        ret = dict_set_int32 (peer_data, "snap_count", snap_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set snap_count");
                goto out;
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Imports the snapshot details of a brick if required and available
 *
 * Snapshot details will be imported only if the cluster op-version is >= 4
 */
int
gd_import_new_brick_snap_details (dict_t *dict, char *prefix,
                                  glusterd_brickinfo_t *brickinfo)
{
        int              ret         = -1;
        xlator_t        *this        = NULL;
        glusterd_conf_t *conf        = NULL;
        char             key[512]    = {0,};
        char            *snap_device = NULL;
        char            *fs_type     = NULL;
        char            *mnt_opts    = NULL;
        char            *mount_dir   = NULL;

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (brickinfo != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.snap_status", prefix);
        ret = dict_get_int32 (dict, key, &brickinfo->snap_status);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload", key);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.device_path", prefix);
        ret = dict_get_str (dict, key, &snap_device);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload", key);
                goto out;
        }
        strcpy (brickinfo->device_path, snap_device);

        snprintf (key, sizeof (key), "%s.fs_type", prefix);
        ret = dict_get_str (dict, key, &fs_type);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload", key);
                goto out;
        }
        strcpy (brickinfo->fstype, fs_type);

        snprintf (key, sizeof (key), "%s.mnt_opts", prefix);
        ret = dict_get_str (dict, key, &mnt_opts);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload", key);
                goto out;
        }
        strcpy (brickinfo->mnt_opts, mnt_opts);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.mount_dir", prefix);
        ret = dict_get_str (dict, key, &mount_dir);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "%s missing in payload", key);
                goto out;
        }
        strncpy (brickinfo->mount_dir, mount_dir,
                 (sizeof (brickinfo->mount_dir) - 1));

out:
        return ret;
}

/*
 * Imports the snapshot details of a volume if required and available
 *
 * Snapshot details will be imported only if cluster.op_version is greater than
 * or equal to GD_OP_VERSION_3_6_0, the op-version from which volume snapshot is
 * supported.
 */
int
gd_import_volume_snap_details (dict_t *dict, glusterd_volinfo_t *volinfo,
                               char *prefix, char *volname)
{
        int              ret           = -1;
        xlator_t        *this          = NULL;
        glusterd_conf_t *conf          = NULL;
        char             key[256]      = {0,};
        char            *restored_snap = NULL;

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (dict != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (volinfo != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (prefix != NULL), out);
        GF_VALIDATE_OR_GOTO (this->name, (volname != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.is_snap_volume", prefix);
        ret = dict_get_uint32 (dict, key, &volinfo->is_snap_volume);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload "
                        "for %s", key, volname);
                goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.restored_from_snap", prefix);
        ret = dict_get_str (dict, key, &restored_snap);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload "
                        "for %s", key, volname);
                goto out;
        }

        gf_uuid_parse (restored_snap, volinfo->restored_from_snap);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.snap-max-hard-limit", prefix);
        ret = dict_get_uint64 (dict, key,
                               &volinfo->snap_max_hard_limit);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s missing in payload "
                        "for %s", key, volname);
out:
        return ret;
}

int32_t
glusterd_perform_missed_op (glusterd_snap_t *snap, int32_t op)
{
        dict_t                  *dict           = NULL;
        int32_t                  ret            = -1;
        glusterd_conf_t         *priv           = NULL;
        glusterd_volinfo_t      *snap_volinfo   = NULL;
        glusterd_volinfo_t      *volinfo        = NULL;
        glusterd_volinfo_t      *tmp            = NULL;
        xlator_t                *this           = NULL;
        uuid_t                   null_uuid      = {0};
        char                    *parent_volname = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (snap);

        dict = dict_new();
        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL, "Unable to create dict");
                ret = -1;
                goto out;
        }

        switch (op) {
        case GF_SNAP_OPTION_TYPE_DELETE:
                ret = glusterd_snap_remove (dict, snap, _gf_true, _gf_false,
                                            _gf_false);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_REMOVE_FAIL,
                                "Failed to remove snap");
                        goto out;
                }

                break;
        case GF_SNAP_OPTION_TYPE_RESTORE:
                cds_list_for_each_entry_safe (snap_volinfo, tmp, &snap->volumes,
                                              vol_list) {
                        parent_volname = gf_strdup
                                            (snap_volinfo->parent_volname);
                        if (!parent_volname)
                                goto out;

                        ret = glusterd_volinfo_find (parent_volname, &volinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VOLINFO_GET_FAIL,
                                        "Could not get volinfo of %s",
                                        parent_volname);
                                goto out;
                        }

                        volinfo->version--;
                        gf_uuid_copy (volinfo->restored_from_snap, null_uuid);

                        /* gd_restore_snap_volume() uses the dict and volcount
                         * to fetch snap brick info from other nodes, which were
                         * collected during prevalidation. As this is an ad-hoc
                         * op and only local node's data matter, hence sending
                         * volcount as 0 and re-using the same dict because we
                         * need not record any missed creates in the rsp_dict.
                         */
                        ret = gd_restore_snap_volume (dict, dict, volinfo,
                                                      snap_volinfo, 0);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAP_RESTORE_FAIL,
                                        "Failed to restore snap for %s",
                                        snap->snapname);
                                volinfo->version++;
                                goto out;
                        }

                        /* Restore is successful therefore delete the original
                         * volume's volinfo. If the volinfo is already restored
                         * then we should delete the backend LVMs */
                        if (!gf_uuid_is_null (volinfo->restored_from_snap)) {
                                ret = glusterd_lvm_snapshot_remove (dict,
                                                                    volinfo);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SNAP_REMOVE_FAIL,
                                                "Failed to remove LVM backend");
                                        goto out;
                                }
                        }

                        /* Detach the volinfo from priv->volumes, so that no new
                         * command can ref it any more and then unref it.
                         */
                        cds_list_del_init (&volinfo->vol_list);
                        glusterd_volinfo_unref (volinfo);

                        ret = glusterd_snapshot_restore_cleanup (dict,
                                                                 parent_volname,
                                                                 snap);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAP_CLEANUP_FAIL,
                                        "Failed to perform snapshot restore "
                                        "cleanup for %s volume",
                                        parent_volname);
                                goto out;
                        }

                        GF_FREE (parent_volname);
                        parent_volname = NULL;
                }

                break;
        default:
                /* The entry must be a create, delete, or
                 * restore entry
                 */
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "Invalid missed snap entry");
                ret = -1;
                goto out;
        }

out:
        dict_unref (dict);
        if (parent_volname) {
                GF_FREE (parent_volname);
                parent_volname = NULL;
        }

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Perform missed deletes and restores on this node */
int32_t
glusterd_perform_missed_snap_ops ()
{
        int32_t                      ret                 = -1;
        int32_t                      op_status           = -1;
        glusterd_conf_t             *priv                = NULL;
        glusterd_missed_snap_info   *missed_snapinfo     = NULL;
        glusterd_snap_op_t          *snap_opinfo         = NULL;
        glusterd_snap_t             *snap                = NULL;
        uuid_t                       snap_uuid           = {0,};
        xlator_t                    *this                = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        cds_list_for_each_entry (missed_snapinfo, &priv->missed_snaps_list,
                                 missed_snaps) {
                /* If the pending snap_op is not for this node then continue */
                if (strcmp (missed_snapinfo->node_uuid, uuid_utoa (MY_UUID)))
                        continue;

                /* Find the snap id */
                gf_uuid_parse (missed_snapinfo->snap_uuid, snap_uuid);
                snap = NULL;
                snap = glusterd_find_snap_by_id (snap_uuid);
                if (!snap) {
                        /* If the snap is not found, then a delete or a
                         * restore can't be pending on that snap_uuid.
                         */
                        gf_msg_debug (this->name, 0,
                                "Not a pending delete or restore op");
                        continue;
                }

                op_status = GD_MISSED_SNAP_PENDING;
                cds_list_for_each_entry (snap_opinfo,
                                         &missed_snapinfo->snap_ops,
                                         snap_ops_list) {
                        /* If the snap_op is create or its status is
                         * GD_MISSED_SNAP_DONE then continue
                         */
                        if ((snap_opinfo->status == GD_MISSED_SNAP_DONE) ||
                            (snap_opinfo->op == GF_SNAP_OPTION_TYPE_CREATE))
                                continue;

                        /* Perform the actual op for the first time for
                         * this snap, and mark the snap_status as
                         * GD_MISSED_SNAP_DONE. For other entries for the same
                         * snap, just mark the entry as done.
                         */
                        if (op_status == GD_MISSED_SNAP_PENDING) {
                                ret = glusterd_perform_missed_op
                                                             (snap,
                                                              snap_opinfo->op);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SNAPSHOT_OP_FAILED,
                                                "Failed to perform missed snap op");
                                        goto out;
                                }
                                op_status = GD_MISSED_SNAP_DONE;
                        }

                        snap_opinfo->status = GD_MISSED_SNAP_DONE;
                }
        }

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Import friend volumes missed_snap_list and update *
 * missed_snap_list if need be */
int32_t
glusterd_import_friend_missed_snap_list (dict_t *peer_data)
{
        int32_t                      missed_snap_count     = -1;
        int32_t                      ret                   = -1;
        glusterd_conf_t             *priv                  = NULL;
        xlator_t                    *this                  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_data);

        priv = this->private;
        GF_ASSERT (priv);

        /* Add the friends missed_snaps entries to the in-memory list */
        ret = dict_get_int32 (peer_data, "missed_snap_count",
                              &missed_snap_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_MISSED_SNAP_GET_FAIL,
                        "No missed snaps");
                ret = 0;
                goto out;
        }

        ret = glusterd_add_missed_snaps_to_list (peer_data,
                                                 missed_snap_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                        "Failed to add missed snaps to list");
                goto out;
        }

        ret = glusterd_perform_missed_snap_ops ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPSHOT_OP_FAILED,
                        "Failed to perform snap operations");
                /* Not going to out at this point coz some *
                 * missed ops might have been performed. We *
                 * need to persist the current list *
                 */
        }

        ret = glusterd_store_update_missed_snaps ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                        "Failed to update missed_snaps_list");
                goto out;
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/*
 * This function will set boolean "conflict" to true if peer snap
 * has a version greater than snap version of local node. Otherwise
 * boolean "conflict" will be set to false.
 */
int
glusterd_check_peer_has_higher_snap_version (dict_t *peer_data,
                               char *peer_snap_name, int volcount,
                               gf_boolean_t *conflict, char *prefix,
                               glusterd_snap_t *snap, char *hostname)
{
        glusterd_volinfo_t      *snap_volinfo = NULL;
        char                     key[256]     = {0};
        int                      version      = 0, i = 0;
        int                      ret          = 0;
        xlator_t                *this         = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (snap);
        GF_ASSERT (peer_data);

        for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "%s%d.version", prefix, i);
                ret = dict_get_int32 (peer_data, key, &version);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "failed to get "
                                "version of snap volume = %s", peer_snap_name);
                        return -1;
                }

               /* TODO : As of now there is only one volume in snapshot.
                * Change this when multiple volume snapshot is introduced
                */
               snap_volinfo = cds_list_entry (snap->volumes.next,
                                          glusterd_volinfo_t, vol_list);
               if (!snap_volinfo) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL, "Failed to get snap "
                                "volinfo %s", snap->snapname);
                   return -1;
               }

               if (version > snap_volinfo->version) {
                        /* Mismatch detected */
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                 GD_MSG_VOL_VERS_MISMATCH,
                                "Version of volume %s differ. "
                                "local version = %d, remote version = %d "
                                "on peer %s", snap_volinfo->volname,
                                snap_volinfo->version, version, hostname);
                        *conflict = _gf_true;
                        break;
                } else {
                        *conflict = _gf_false;
                }
        }
        return 0;
}

/* Check for the peer_snap_name in the list of existing snapshots.
 * If a snap exists with the same name and a different snap_id, then
 * there is a conflict. Set conflict as _gf_true, and snap to the
 * conflicting snap object. If a snap exists with the same name, and the
 * same snap_id, then there is no conflict. Set conflict as _gf_false
 * and snap to the existing snap object. If no snap exists with the
 * peer_snap_name, then there is no conflict. Set conflict as _gf_false
 * and snap to NULL.
 */
void
glusterd_is_peer_snap_conflicting (char *peer_snap_name, char *peer_snap_id,
                                   gf_boolean_t *conflict,
                                   glusterd_snap_t **snap, char *hostname)
{
        uuid_t       peer_snap_uuid = {0,};
        xlator_t    *this           = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_snap_name);
        GF_ASSERT (peer_snap_id);
        GF_ASSERT (conflict);
        GF_ASSERT (snap);
        GF_ASSERT (hostname);

        *snap = glusterd_find_snap_by_name (peer_snap_name);
        if (*snap) {
                gf_uuid_parse (peer_snap_id, peer_snap_uuid);
                if (!gf_uuid_compare (peer_snap_uuid, (*snap)->snap_id)) {
                        /* Current node contains the same snap having
                         * the same snapname and snap_id
                         */
                        gf_msg_debug (this->name, 0,
                                "Snapshot %s from peer %s present in "
                                "localhost", peer_snap_name, hostname);
                        *conflict = _gf_false;
                } else {
                        /* Current node contains the same snap having
                         * the same snapname but different snap_id
                         */
                        gf_msg_debug (this->name, 0,
                                "Snapshot %s from peer %s conflicts with "
                                "snapshot in localhost", peer_snap_name,
                                hostname);
                        *conflict = _gf_true;
                }
        } else {
                /* Peer contains snapshots missing on the current node */
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_MISSED_SNAP_PRESENT,
                        "Snapshot %s from peer %s missing on localhost",
                        peer_snap_name, hostname);
                *conflict = _gf_false;
        }
}

/* Check if the local node is hosting any bricks for the given snapshot */
gf_boolean_t
glusterd_are_snap_bricks_local (glusterd_snap_t *snap)
{
        gf_boolean_t            is_local   = _gf_false;
        glusterd_volinfo_t     *volinfo    = NULL;
        glusterd_brickinfo_t   *brickinfo  = NULL;
        xlator_t               *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (snap);

        cds_list_for_each_entry (volinfo, &snap->volumes, vol_list) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        if (!gf_uuid_compare (brickinfo->uuid, MY_UUID)) {
                                is_local = _gf_true;
                                goto out;
                        }
                }
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", is_local);
        return is_local;
}

/* Check if the peer has missed any snap delete
 * or restore for the given snap_id
 */
gf_boolean_t
glusterd_peer_has_missed_snap_delete (uuid_t peerid, char *peer_snap_id)
{
        char                        *peer_uuid           = NULL;
        gf_boolean_t                 missed_delete       = _gf_false;
        glusterd_conf_t             *priv                = NULL;
        glusterd_missed_snap_info   *missed_snapinfo     = NULL;
        glusterd_snap_op_t          *snap_opinfo         = NULL;
        xlator_t                    *this                = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (peer_snap_id);

        peer_uuid = uuid_utoa (peerid);

        cds_list_for_each_entry (missed_snapinfo, &priv->missed_snaps_list,
                                 missed_snaps) {
                /* Look for missed snap for the same peer, and
                 * the same snap_id
                 */
                if ((!strcmp (peer_uuid, missed_snapinfo->node_uuid)) &&
                    (!strcmp (peer_snap_id, missed_snapinfo->snap_uuid))) {
                        /* Check if the missed snap's op is delete and the
                         * status is pending
                         */
                        cds_list_for_each_entry (snap_opinfo,
                                                 &missed_snapinfo->snap_ops,
                                                 snap_ops_list) {
                                if (((snap_opinfo->op ==
                                              GF_SNAP_OPTION_TYPE_DELETE) ||
                                     (snap_opinfo->op ==
                                              GF_SNAP_OPTION_TYPE_RESTORE)) &&
                                    (snap_opinfo->status ==
                                             GD_MISSED_SNAP_PENDING)) {
                                        missed_delete = _gf_true;
                                        goto out;
                                }
                        }
                }
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", missed_delete);
        return missed_delete;
}

/* Genrate and store snap volfiles for imported snap object */
int32_t
glusterd_gen_snap_volfiles (glusterd_volinfo_t *snap_vol, char *peer_snap_name)
{
        int32_t                 ret              = -1;
        xlator_t               *this             = NULL;
        glusterd_volinfo_t     *parent_volinfo   = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (snap_vol);
        GF_ASSERT (peer_snap_name);

        ret = glusterd_store_volinfo (snap_vol, GLUSTERD_VOLINFO_VER_AC_NONE);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_SET_FAIL, "Failed to store snapshot "
                        "volinfo (%s) for snap %s", snap_vol->volname,
                        peer_snap_name);
                goto out;
        }

        ret = generate_brick_volfiles (snap_vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "generating the brick volfiles for the "
                        "snap %s failed", peer_snap_name);
                goto out;
        }

        ret = generate_client_volfiles (snap_vol, GF_CLIENT_TRUSTED);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "generating the trusted client volfiles for "
                        "the snap %s failed", peer_snap_name);
                goto out;
        }

        ret = generate_client_volfiles (snap_vol, GF_CLIENT_OTHER);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "generating the client volfiles for the "
                        "snap %s failed", peer_snap_name);
                goto out;
        }

        ret = glusterd_volinfo_find (snap_vol->parent_volname,
                                     &parent_volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "Parent volinfo "
                        "not found for %s volume of snap %s",
                        snap_vol->volname, peer_snap_name);
                goto out;
        }

        glusterd_list_add_snapvol (parent_volinfo, snap_vol);

        ret = glusterd_store_volinfo (snap_vol, GLUSTERD_VOLINFO_VER_AC_NONE);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_SET_FAIL,
                        "Failed to store snap volinfo");
                goto out;
        }
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Import snapshot info from peer_data and add it to priv */
int32_t
glusterd_import_friend_snap (dict_t *peer_data, int32_t snap_count,
                             char *peer_snap_name, char *peer_snap_id)
{
        char                 buf[NAME_MAX]    = "";
        char                 prefix[NAME_MAX] = "";
        dict_t              *dict             = NULL;
        glusterd_snap_t     *snap             = NULL;
        glusterd_volinfo_t  *snap_vol         = NULL;
        glusterd_conf_t     *priv             = NULL;
        int32_t              ret              = -1;
        int32_t              volcount         = -1;
        int32_t              i                = -1;
        xlator_t            *this             = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (peer_data);
        GF_ASSERT (peer_snap_name);
        GF_ASSERT (peer_snap_id);

        snprintf (prefix, sizeof(prefix), "snap%d", snap_count);

        snap = glusterd_new_snap_object ();
        if (!snap) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_CREATION_FAIL, "Could not create "
                        "the snap object for snap %s", peer_snap_name);
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to create dict");
                ret = -1;
                goto out;
        }

        strncpy (snap->snapname, peer_snap_name, sizeof (snap->snapname) - 1);
        gf_uuid_parse (peer_snap_id, snap->snap_id);

        snprintf (buf, sizeof(buf), "%s.snapid", prefix);
        ret = dict_get_str (peer_data, buf, &snap->description);

        snprintf (buf, sizeof(buf), "%s.time_stamp", prefix);
        ret = dict_get_int64 (peer_data, buf, &snap->time_stamp);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get time_stamp for snap %s",
                        peer_snap_name);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.snap_restored", prefix);
        ret = dict_get_int8 (peer_data, buf, (int8_t *) &snap->snap_restored);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get snap_restored for snap %s",
                        peer_snap_name);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.snap_status", prefix);
        ret = dict_get_int32 (peer_data, buf, (int32_t *) &snap->snap_status);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get snap_status for snap %s",
                        peer_snap_name);
                goto out;
        }

        /* If the snap is scheduled to be decommissioned, then
         * don't accept the snap */
        if (snap->snap_status == GD_SNAP_STATUS_DECOMMISSION) {
                gf_msg_debug (this->name, 0,
                        "The snap(%s) is scheduled to be decommissioned "
                        "Not accepting the snap.", peer_snap_name);
                glusterd_snap_remove (dict, snap,
                                      _gf_true, _gf_true, _gf_false);
                ret = 0;
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.volcount", prefix);
        ret = dict_get_int32 (peer_data, buf, &volcount);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volcount for snap %s",
                        peer_snap_name);
                goto out;
        }

        ret = glusterd_store_create_snap_dir (snap);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPDIR_CREATE_FAIL,
                        "Failed to create snap dir");
                goto out;
        }

        glusterd_list_add_order (&snap->snap_list, &priv->snapshots,
                                 glusterd_compare_snap_time);


        for (i = 1; i <= volcount; i++) {
                ret = glusterd_import_volinfo (peer_data, i,
                                               &snap_vol, prefix);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_SET_FAIL,
                                "Failed to import snap volinfo for "
                                "snap %s", peer_snap_name);
                        goto out;
                }

                snap_vol->snapshot = snap;

                ret = glusterd_gen_snap_volfiles (snap_vol, peer_snap_name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL,
                                "Failed to generate snap vol files "
                                "for snap %s", peer_snap_name);
                        goto out;
                }
                if (glusterd_is_volume_started (snap_vol)) {
                        (void) glusterd_start_bricks (snap_vol);
                } else {
                        (void) glusterd_stop_bricks(snap_vol);
                }

                ret = glusterd_import_quota_conf (peer_data, i,
                                                  snap_vol, prefix);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_QUOTA_CONFIG_IMPORT_FAIL,
                                "Failed to import quota conf "
                                "for snap %s", peer_snap_name);
                        goto out;
                }

                snap_vol = NULL;
        }

        ret = glusterd_store_snap (snap);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_SNAP_CREATION_FAIL, "Could not store snap"
                        "object %s", peer_snap_name);
                goto out;
        }

out:
        if (ret)
                glusterd_snap_remove (dict, snap,
                                      _gf_true, _gf_true, _gf_false);

        if (dict)
                dict_unref (dict);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* During a peer-handshake, after the volumes have synced, and the list of
 * missed snapshots have synced, the node will perform the pending deletes
 * and restores on this list. At this point, the current snapshot list in
 * the node will be updated, and hence in case of conflicts arising during
 * snapshot handshake, the peer hosting the bricks will be given precedence
 * Likewise, if there will be a conflict, and both peers will be in the same
 * state, i.e either both would be hosting bricks or both would not be hosting
 * bricks, then a decision can't be taken and a peer-reject will happen.
 *
 * glusterd_compare_and_update_snap() implements the following algorithm to
 * perform the above task:
 * Step  1: Start.
 * Step  2: Check if the peer is missing a delete or restore on the said snap.
 *          If yes, goto step 6.
 * Step  3: Check if there is a conflict between the peer's data and the
 *          local snap. If no, goto step 5.
 * Step  4: As there is a conflict, check if both the peer and the local nodes
 *          are hosting bricks. Based on the results perform the following:
 *          Peer Hosts Bricks    Local Node Hosts Bricks       Action
 *                Yes                     Yes                Goto Step 8
 *                No                      No                 Goto Step 8
 *                Yes                     No                 Goto Step 9
 *                No                      Yes                Goto Step 7
 * Step  5: Check if the local node is missing the peer's data.
 *          If yes, goto step 10.
 * Step  6: Check if the snap volume version is lesser than peer_data
 *          if yes goto step 9
 * Step  7: It's a no-op. Goto step 11
 * Step  8: Peer Reject. Goto step 11
 * Step  9: Delete local node's data.
 * Step 10: Accept Peer Data.
 * Step 11: Stop
 *
 */
int32_t
glusterd_compare_and_update_snap (dict_t *peer_data, int32_t snap_count,
                                  char *peername, uuid_t peerid)
{
        char              buf[NAME_MAX]    = "";
        char              prefix[NAME_MAX] = "";
        char             *peer_snap_name   = NULL;
        char             *peer_snap_id     = NULL;
        dict_t           *dict             = NULL;
        glusterd_snap_t  *snap             = NULL;
        gf_boolean_t      conflict         = _gf_false;
        gf_boolean_t      is_local         = _gf_false;
        gf_boolean_t      is_hosted        = _gf_false;
        gf_boolean_t      missed_delete    = _gf_false;
        gf_boolean_t      remove_lvm       = _gf_true;

        int32_t           ret              = -1;
        int32_t           volcount         = 0;
        xlator_t         *this             = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_data);
        GF_ASSERT (peername);

        snprintf (prefix, sizeof(prefix), "snap%d", snap_count);

        /* Fetch the peer's snapname */
        snprintf (buf, sizeof(buf), "%s.snapname", prefix);
        ret = dict_get_str (peer_data, buf, &peer_snap_name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch snapname from peer: %s",
                        peername);
                goto out;
        }

        /* Fetch the peer's snap_id */
        snprintf (buf, sizeof(buf), "%s.snap_id", prefix);
        ret = dict_get_str (peer_data, buf, &peer_snap_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch snap_id from peer: %s",
                        peername);
                goto out;
        }

        snprintf (buf, sizeof(buf), "%s.volcount", prefix);
        ret = dict_get_int32 (peer_data, buf, &volcount);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get volcount for snap %s",
                        peer_snap_name);
                goto out;
        }

        /* Check if the peer has missed a snap delete or restore
         * resulting in stale data for the snap in question
         */
        missed_delete = glusterd_peer_has_missed_snap_delete (peerid,
                                                              peer_snap_id);
        if (missed_delete == _gf_true) {
                /* Peer has missed delete on the missing/conflicting snap_id */
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_MISSED_SNAP_DELETE,
                        "Peer %s has missed a delete "
                        "on snap %s", peername, peer_snap_name);
                ret = 0;
                goto out;
        }

        /* Check if there is a conflict, and if the
         * peer data is already present
         */
        glusterd_is_peer_snap_conflicting (peer_snap_name, peer_snap_id,
                                           &conflict, &snap, peername);
        if (conflict == _gf_false) {
                if (!snap) {
                        /* Peer has snap with the same snapname
                        * and snap_id, which local node doesn't have.
                        */
                        goto accept_peer_data;
                }
                /* Peer has snap with the same snapname
                 * and snap_id. Now check if peer has a
                 * snap with higher snap version than local
                 * node has.
                 */
                ret = glusterd_check_peer_has_higher_snap_version (peer_data,
                                                    peer_snap_name, volcount,
                                                    &conflict, prefix, snap,
                                                    peername);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOL_VERS_MISMATCH, "Failed "
                                "to check version of snap volume");
                        goto out;
                }
                if (conflict == _gf_true) {
                        /*
                         * Snap version of peer is higher than snap
                         * version of local node.
                         *
                         * Remove data in local node and accept peer data.
                         * We just need to heal snap info of local node, So
                         * When removing data from local node, make sure
                         * we are not removing backend lvm of the snap.
                         */
                        remove_lvm = _gf_false;
                        goto remove_my_data;
                } else {
                        ret = 0;
                        goto out;
                }
        }

        /* There is a conflict. Check if the current node is
         * hosting bricks for the conflicted snap.
         */
        is_local = glusterd_are_snap_bricks_local (snap);

        /* Check if the peer is hosting any bricks for the
         * conflicting snap
         */
        snprintf (buf, sizeof(buf), "%s.host_bricks", prefix);
        ret = dict_get_int8 (peer_data, buf, (int8_t *) &is_hosted);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch host_bricks from peer: %s "
                        "for %s", peername, peer_snap_name);
                goto out;
        }

        /* As there is a conflict at this point of time, the data of the
         * node that hosts a brick takes precedence. If both the local
         * node and the peer are in the same state, i.e if both of them
         * are either hosting or not hosting the bricks, for the snap,
         * then it's a peer reject
         */
        if (is_hosted == is_local) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_CONFLICT,
                        "Conflict in snapshot %s with peer %s",
                        peer_snap_name, peername);
                ret = -1;
                goto out;
        }

        if (is_hosted == _gf_false) {
                /* If there was a conflict, and the peer is not hosting
                 * any brick, then don't accept peer data
                 */
                gf_msg_debug (this->name, 0,
                        "Peer doesn't hosts bricks for conflicting "
                        "snap(%s). Not accepting peer data.",
                        peer_snap_name);
                ret = 0;
                goto out;
        }

        /* The peer is hosting a brick in case of conflict
         * And local node isn't. Hence remove local node's
         * data and accept peer data
         */

        gf_msg_debug (this->name, 0, "Peer hosts bricks for conflicting "
                "snap(%s). Removing local data. Accepting peer data.",
                peer_snap_name);
        remove_lvm = _gf_true;

remove_my_data:

        dict = dict_new();
        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Unable to create dict");
                ret = -1;
                goto out;
        }

        ret = glusterd_snap_remove (dict, snap, remove_lvm, _gf_false,
                                    _gf_false);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_REMOVE_FAIL,
                        "Failed to remove snap %s", snap->snapname);
                goto out;
        }

accept_peer_data:

        /* Accept Peer Data */
        ret = glusterd_import_friend_snap (peer_data, snap_count,
                                           peer_snap_name, peer_snap_id);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_IMPORT_FAIL,
                        "Failed to import snap %s from peer %s",
                        peer_snap_name, peername);
                goto out;
        }

out:
        if (dict)
                dict_unref (dict);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Compare snapshots present in peer_data, with the snapshots in
 * the current node
 */
int32_t
glusterd_compare_friend_snapshots (dict_t *peer_data, char *peername,
                                   uuid_t peerid)
{
        int32_t          ret          = -1;
        int32_t          snap_count   = 0;
        int              i            = 1;
        xlator_t        *this         = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (peer_data);
        GF_ASSERT (peername);

        ret = dict_get_int32 (peer_data, "snap_count", &snap_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to fetch snap_count");
                goto out;
        }

        for (i = 1; i <= snap_count; i++) {
                /* Compare one snapshot from peer_data at a time */
                ret = glusterd_compare_and_update_snap (peer_data, i, peername,
                                                        peerid);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPSHOT_OP_FAILED,
                                "Failed to compare snapshots with peer %s",
                                peername);
                        goto out;
                }
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_add_snapd_to_dict (glusterd_volinfo_t *volinfo,
                            dict_t  *dict, int32_t count)
{

        int             ret                   = -1;
        int32_t         pid                   = -1;
        int32_t         brick_online          = -1;
        char            key[1024]             = {0};
        char            base_key[1024]        = {0};
        char            pidfile[PATH_MAX]     = {0};
        xlator_t        *this                 = NULL;


        GF_ASSERT (volinfo);
        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);

        snprintf (base_key, sizeof (base_key), "brick%d", count);
        snprintf (key, sizeof (key), "%s.hostname", base_key);
        ret = dict_set_str (dict, key, "Snapshot Daemon");
        if (ret)
                goto out;

        snprintf (key, sizeof (key), "%s.path", base_key);
        ret = dict_set_dynstr (dict, key, gf_strdup (uuid_utoa (MY_UUID)));
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.port", base_key);
        ret = dict_set_int32 (dict, key, volinfo->snapd.port);
        if (ret)
                goto out;

        glusterd_svc_build_snapd_pidfile (volinfo, pidfile, sizeof (pidfile));

        brick_online = gf_is_service_running (pidfile, &pid);
        if (brick_online == _gf_false)
                pid = -1;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", base_key);
        ret = dict_set_int32 (dict, key, pid);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.status", base_key);
        ret = dict_set_int32 (dict, key, brick_online);

out:
        if (ret)
                gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_snap_config_use_rsp_dict (dict_t *dst, dict_t *src)
{
        char       buf[PATH_MAX]    = "";
        char      *volname          = NULL;
        int        ret              = -1;
        int        config_command   = 0;
        uint64_t   i                = 0;
        uint64_t   hard_limit       = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
        uint64_t   soft_limit       = GLUSTERD_SNAPS_DEF_SOFT_LIMIT_PERCENT;
        uint64_t   value            = 0;
        uint64_t   voldisplaycount  = 0;

        if (!dst || !src) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = dict_get_int32 (dst, "config-command", &config_command);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "failed to get config-command type");
                goto out;
        }

        switch (config_command) {
        case GF_SNAP_CONFIG_DISPLAY:
                ret = dict_get_uint64 (src,
                                       GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                                       &hard_limit);
                if (!ret) {
                        ret = dict_set_uint64 (dst,
                                         GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                                         hard_limit);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to set snap_max_hard_limit");
                                goto out;
                        }
                } else {
                        /* Received dummy response from other nodes */
                        ret = 0;
                        goto out;
                }

                ret = dict_get_uint64 (src,
                                       GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT,
                                       &soft_limit);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get snap_max_soft_limit");
                        goto out;
                }

                ret = dict_set_uint64 (dst,
                                       GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT,
                                       soft_limit);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set snap_max_soft_limit");
                        goto out;
                }

                ret = dict_get_uint64 (src, "voldisplaycount",
                                       &voldisplaycount);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get voldisplaycount");
                        goto out;
                }

                ret = dict_set_uint64 (dst, "voldisplaycount",
                                       voldisplaycount);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set voldisplaycount");
                        goto out;
                }

                for (i = 0; i < voldisplaycount; i++) {
                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-volname", i);
                        ret = dict_get_str (src, buf, &volname);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Unable to get %s", buf);
                                goto out;
                        }
                        ret = dict_set_str (dst, buf, volname);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to set %s", buf);
                                goto out;
                        }

                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-snap-max-hard-limit", i);
                        ret = dict_get_uint64 (src, buf, &value);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Unable to get %s", buf);
                                goto out;
                        }
                        ret = dict_set_uint64 (dst, buf, value);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to set %s", buf);
                                goto out;
                        }

                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-active-hard-limit", i);
                        ret = dict_get_uint64 (src, buf, &value);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Unable to get %s", buf);
                                goto out;
                        }
                        ret = dict_set_uint64 (dst, buf, value);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to set %s", buf);
                                goto out;
                        }

                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-snap-max-soft-limit", i);
                        ret = dict_get_uint64 (src, buf, &value);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Unable to get %s", buf);
                                goto out;
                        }
                        ret = dict_set_uint64 (dst, buf, value);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to set %s", buf);
                                goto out;
                        }
                }

                break;
        default:
                break;
        }

        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_merge_brick_status (dict_t *dst, dict_t *src)
{
        int64_t        volume_count             = 0;
        int64_t        index                    = 0;
        int64_t        j                        = 0;
        int64_t        brick_count              = 0;
        int64_t        brick_order              = 0;
        char           key[PATH_MAX]            = {0, };
        char           key_prefix[PATH_MAX]     = {0, };
        char           snapbrckcnt[PATH_MAX]    = {0, };
        char           snapbrckord[PATH_MAX]    = {0, };
        char          *clonename                = NULL;
        int            ret                      = -1;
        int32_t        brick_online             = 0;
        xlator_t      *this                     = NULL;
        int32_t        snap_command             = 0;

        this = THIS;
        GF_ASSERT (this);

        if (!dst || !src) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = dict_get_int32 (dst, "type", &snap_command);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        if (snap_command == GF_SNAP_OPTION_TYPE_DELETE) {
                gf_msg_debug (this->name, 0, "snapshot delete command."
                        " Need not merge the status of the bricks");
                ret = 0;
                goto out;
        }

        /* Try and fetch clonename. If present set status with clonename *
         * else do so as snap-vol */
        ret = dict_get_str (dst, "clonename", &clonename);
        if (ret) {
                snprintf (key_prefix, sizeof (key_prefix), "snap-vol");
        } else
                snprintf (key_prefix, sizeof (key_prefix), "clone");

        ret = dict_get_int64 (src, "volcount", &volume_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "failed to "
                        "get the volume count");
                goto out;
        }

        for (index = 0; index < volume_count; index++) {
                ret = snprintf (snapbrckcnt, sizeof(snapbrckcnt) - 1,
                                "snap-vol%"PRId64"_brickcount", index+1);
                ret = dict_get_int64 (src, snapbrckcnt, &brick_count);
                if (ret) {
                        gf_msg_trace (this->name, 0,
                                "No bricks for this volume in this dict (%s)",
                                snapbrckcnt);
                        continue;
                }

                for (j = 0; j < brick_count; j++) {
                        /* Fetching data from source dict */
                        snprintf (snapbrckord, sizeof(snapbrckord) - 1,
                                  "snap-vol%"PRId64".brick%"PRId64".order",
                                  index+1, j);

                        ret = dict_get_int64 (src, snapbrckord, &brick_order);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Failed to get brick order (%s)",
                                        snapbrckord);
                                goto out;
                        }

                        snprintf (key, sizeof (key) - 1,
                                  "%s%"PRId64".brick%"PRId64".status",
                                  key_prefix, index+1, brick_order);
                        ret = dict_get_int32 (src, key, &brick_online);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED, "failed to "
                                        "get the brick status (%s)", key);
                                goto out;
                        }

                        ret = dict_set_int32 (dst, key, brick_online);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "failed to "
                                        "set the brick status (%s)", key);
                                goto out;
                        }
                        brick_online = 0;
                }
        }

        ret = 0;

out:
        return ret;
}

/* Aggregate missed_snap_counts from different nodes and save it *
 * in the req_dict of the originator node */
int
glusterd_snap_create_use_rsp_dict (dict_t *dst, dict_t *src)
{
        char          *buf                      = NULL;
        char          *tmp_str                  = NULL;
        char           name_buf[PATH_MAX]       = "";
        int32_t        i                        = -1;
        int32_t        ret                      = -1;
        int32_t        src_missed_snap_count    = -1;
        int32_t        dst_missed_snap_count    = -1;
        xlator_t      *this                     = NULL;
        int8_t         soft_limit_flag          = -1;

        this = THIS;
        GF_ASSERT (this);

        if (!dst || !src) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = glusterd_merge_brick_status (dst, src);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_SET_INFO_FAIL, "failed to merge brick "
                        "status");
                goto out;
        }

        ret = dict_get_str (src, "snapuuid", &buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "failed to get snap UUID");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (dst, "snapuuid", buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set snap uuid in dict");
                goto out;
        }

        /* set in dst dictionary soft-limit-reach only if soft-limit-reach
         * is present src dictionary */
        ret = dict_get_int8 (src, "soft-limit-reach", &soft_limit_flag);
        if (!ret) {
                ret = dict_set_int8 (dst, "soft-limit-reach", soft_limit_flag);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "Failed to set "
                                "soft_limit_flag");
                        goto out;
                }
        }

        ret = dict_get_int32 (src, "missed_snap_count",
                              &src_missed_snap_count);
        if (ret) {
                gf_msg_debug (this->name, 0, "No missed snaps");
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (dst, "missed_snap_count",
                              &dst_missed_snap_count);
        if (ret) {
                /* Initialize dst_missed_count for the first time */
                dst_missed_snap_count = 0;
        }

        for (i = 0; i < src_missed_snap_count; i++) {
                snprintf (name_buf, sizeof(name_buf), "missed_snaps_%d", i);
                ret = dict_get_str (src, name_buf, &buf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to fetch %s", name_buf);
                        goto out;
                }

                snprintf (name_buf, sizeof(name_buf), "missed_snaps_%d",
                          dst_missed_snap_count);

                tmp_str = gf_strdup (buf);
                if (!tmp_str) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (dst, name_buf, tmp_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set %s", name_buf);
                        goto out;
                }

                tmp_str = NULL;
                dst_missed_snap_count++;
        }

        ret = dict_set_int32 (dst, "missed_snap_count", dst_missed_snap_count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set dst_missed_snap_count");
                goto out;
        }

out:
        if (ret && tmp_str)
                GF_FREE(tmp_str);

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_snap_use_rsp_dict (dict_t *dst, dict_t *src)
{
        int            ret            = -1;
        int32_t        snap_command   = 0;

        if (!dst || !src) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = dict_get_int32 (dst, "type", &snap_command);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
        case GF_SNAP_OPTION_TYPE_DELETE:
        case GF_SNAP_OPTION_TYPE_CLONE:
                ret = glusterd_snap_create_use_rsp_dict (dst, src);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_RSP_DICT_USE_FAIL,
                                "Unable to use rsp dict");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_CONFIG:
                ret = glusterd_snap_config_use_rsp_dict (dst, src);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_RSP_DICT_USE_FAIL,
                                "Unable to use rsp dict");
                        goto out;
                }
                break;
        default:
                /* copy the response dictinary's contents to the dict to be
                 * sent back to the cli */
                dict_copy (src, dst);
                break;
        }

        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_compare_snap_time (struct cds_list_head *list1,
                            struct cds_list_head *list2)
{
        glusterd_snap_t *snap1 = NULL;
        glusterd_snap_t *snap2 = NULL;
        double diff_time       = 0;

        GF_ASSERT (list1);
        GF_ASSERT (list2);

        snap1 = cds_list_entry (list1, glusterd_snap_t, snap_list);
        snap2 = cds_list_entry (list2, glusterd_snap_t, snap_list);
        diff_time = difftime(snap1->time_stamp, snap2->time_stamp);

        return (int)diff_time;
}

int
glusterd_compare_snap_vol_time (struct cds_list_head *list1,
                                struct cds_list_head *list2)
{
        glusterd_volinfo_t *snapvol1 = NULL;
        glusterd_volinfo_t *snapvol2 = NULL;
        double diff_time             = 0;

        GF_ASSERT (list1);
        GF_ASSERT (list2);

        snapvol1 = cds_list_entry (list1, glusterd_volinfo_t, snapvol_list);
        snapvol2 = cds_list_entry (list2, glusterd_volinfo_t, snapvol_list);
        diff_time = difftime(snapvol1->snapshot->time_stamp,
                             snapvol2->snapshot->time_stamp);

        return (int)diff_time;
}

int32_t
glusterd_missed_snapinfo_new (glusterd_missed_snap_info **missed_snapinfo)
{
        glusterd_missed_snap_info      *new_missed_snapinfo = NULL;
        int32_t                         ret                 = -1;
        xlator_t                       *this                = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (missed_snapinfo);

        new_missed_snapinfo = GF_CALLOC (1, sizeof(*new_missed_snapinfo),
                                         gf_gld_mt_missed_snapinfo_t);

        if (!new_missed_snapinfo)
                goto out;

        CDS_INIT_LIST_HEAD (&new_missed_snapinfo->missed_snaps);
        CDS_INIT_LIST_HEAD (&new_missed_snapinfo->snap_ops);

        *missed_snapinfo = new_missed_snapinfo;

        ret = 0;

out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_missed_snap_op_new (glusterd_snap_op_t **snap_op)
{
        glusterd_snap_op_t      *new_snap_op = NULL;
        int32_t                  ret         = -1;
        xlator_t                *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (snap_op);

        new_snap_op = GF_CALLOC (1, sizeof(*new_snap_op),
                                 gf_gld_mt_missed_snapinfo_t);

        if (!new_snap_op)
                goto out;

        new_snap_op->brick_num = -1;
        new_snap_op->op = -1;
        new_snap_op->status = -1;
        CDS_INIT_LIST_HEAD (&new_snap_op->snap_ops_list);

        *snap_op = new_snap_op;

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

gf_boolean_t
mntopts_exists (const char *str, const char *opts)
{
        char          *dup_val     = NULL;
        char          *savetok     = NULL;
        char          *token       = NULL;
        gf_boolean_t   exists      = _gf_false;

        GF_ASSERT (opts);

        if (!str || !strlen(str))
                goto out;

        dup_val = gf_strdup (str);
        if (!dup_val)
                goto out;

        token = strtok_r (dup_val, ",", &savetok);
        while (token) {
                if (!strcmp (token, opts)) {
                        exists = _gf_true;
                        goto out;
                }
                token = strtok_r (NULL, ",", &savetok);
        }

out:
        GF_FREE (dup_val);
        return exists;
}

int32_t
glusterd_mount_lvm_snapshot (glusterd_brickinfo_t *brickinfo,
                             char *brick_mount_path)
{
        char               msg[NAME_MAX]  = "";
        char               mnt_opts[1024] = "";
        int32_t            ret            = -1;
        runner_t           runner         = {0, };
        xlator_t          *this           = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brick_mount_path);
        GF_ASSERT (brickinfo);


        runinit (&runner);
        snprintf (msg, sizeof (msg), "mount %s %s",
                  brickinfo->device_path, brick_mount_path);

        strcpy (mnt_opts, brickinfo->mnt_opts);

        /* XFS file-system does not allow to mount file-system with duplicate
         * UUID. File-system UUID of snapshot and its origin volume is same.
         * Therefore to mount such a snapshot in XFS we need to pass nouuid
         * option
         */
        if (!strcmp (brickinfo->fstype, "xfs") &&
            !mntopts_exists (mnt_opts, "nouuid")) {
                if (strlen (mnt_opts) > 0)
                        strcat (mnt_opts, ",");
                strcat (mnt_opts, "nouuid");
        }


        if (strlen (mnt_opts) > 0) {
                runner_add_args (&runner, "mount", "-o", mnt_opts,
                                brickinfo->device_path, brick_mount_path, NULL);
        } else {
                runner_add_args (&runner, "mount", brickinfo->device_path,
                                 brick_mount_path, NULL);
        }

        runner_log (&runner, this->name, GF_LOG_DEBUG, msg);
        ret = runner_run (&runner);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_MOUNT_FAIL, "mounting the snapshot "
                        "logical device %s failed (error: %s)",
                        brickinfo->device_path, strerror (errno));
                goto out;
        } else
                gf_msg_debug (this->name, 0, "mounting the snapshot "
                        "logical device %s successful", brickinfo->device_path);

out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

gf_boolean_t
glusterd_volume_quorum_calculate (glusterd_volinfo_t *volinfo, dict_t *dict,
                                  int down_count, gf_boolean_t first_brick_on,
                                  int8_t snap_force, int quorum_count,
                                  char *quorum_type, char **op_errstr,
                                  uint32_t *op_errno)
{
        gf_boolean_t  quorum_met        = _gf_false;
        char          err_str[PATH_MAX] = {0, };
        xlator_t     *this              = NULL;
        int           up_count          = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        if (!volinfo || !dict) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_INVALID_ENTRY, "input parameters NULL");
                goto out;
        }

        /* In a n-way replication where n >= 3 we should not take a snapshot
         * if even one brick is down, irrespective of the quorum being met.
         * TODO: Remove this restriction once n-way replication is
         * supported with snapshot.
         */
        if (down_count) {
                snprintf (err_str, sizeof (err_str), "One or more bricks may "
                          "be down.");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_DISCONNECTED, "%s", err_str);
                *op_errstr = gf_strdup (err_str);
                *op_errno = EG_BRCKDWN;
                goto out;
        } else {
                quorum_met = _gf_true;
                goto out;
        }

        up_count = volinfo->dist_leaf_count - down_count;

        if (quorum_type && !strcmp (quorum_type, "fixed")) {
                if (up_count >= quorum_count) {
                        quorum_met = _gf_true;
                        goto out;
                }
        } else {
                if ((GF_CLUSTER_TYPE_DISPERSE != volinfo->type) &&
                    (volinfo->dist_leaf_count % 2 == 0)) {
                        if ((up_count > quorum_count) ||
                            ((up_count == quorum_count) && first_brick_on)) {
                                quorum_met = _gf_true;
                                goto out;
                        }
                } else {
                        if (up_count >= quorum_count) {
                                quorum_met = _gf_true;
                                goto out;
                        }
                }
        }

        if (!quorum_met) {
                snprintf (err_str, sizeof (err_str), "quorum is not met");
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_SERVER_QUORUM_NOT_MET, "%s", err_str);
                *op_errstr = gf_strdup (err_str);
                *op_errno = EG_BRCKDWN;
        }

out:
        return quorum_met;
}

int32_t
glusterd_volume_quorum_check (glusterd_volinfo_t *volinfo, int64_t index,
                              dict_t *dict, char *key_prefix,
                              int8_t snap_force, int quorum_count,
                              char *quorum_type, char **op_errstr,
                              uint32_t *op_errno)
{
        int                      ret                = 0;
        xlator_t                *this               = NULL;
        int64_t                  i                  = 0;
        int64_t                  j                  = 0;
        char                     key[1024]          = {0, };
        int                      down_count         = 0;
        gf_boolean_t             first_brick_on     = _gf_true;
        glusterd_conf_t         *priv               = NULL;
        gf_boolean_t             quorum_met         = _gf_false;
        int                      distribute_subvols = 0;
        int32_t                  brick_online       = 0;
        char                     err_str[PATH_MAX]  = {0, };

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        if (!volinfo || !dict) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_INVALID_ENTRY, "input parameters NULL");
                goto out;
        }

        if ((!glusterd_is_volume_replicate (volinfo) ||
             volinfo->replica_count < 3) &&
            (GF_CLUSTER_TYPE_DISPERSE != volinfo->type)) {
                for (i = 0; i < volinfo->brick_count ; i++) {
                        /* for a pure distribute volume, and replica volume
                           with replica count 2, quorum is not met if even
                           one of its subvolumes is down
                        */
                        snprintf (key, sizeof (key),
                                  "%s%"PRId64".brick%"PRId64".status",
                                  key_prefix, index, i);
                        ret = dict_get_int32 (dict, key, &brick_online);
                        if (ret || !brick_online) {
                                ret = 1;
                                snprintf (err_str, sizeof (err_str), "quorum "
                                          "is not met");
                                gf_msg (this->name, GF_LOG_ERROR,
                                        0, GD_MSG_SERVER_QUORUM_NOT_MET, "%s",
                                        err_str);
                                *op_errstr = gf_strdup (err_str);
                                *op_errno = EG_BRCKDWN;
                                goto out;
                        }
                }
                ret = 0;
                quorum_met = _gf_true;
        } else {
             distribute_subvols = volinfo->brick_count /
                                  volinfo->dist_leaf_count;
             for (j = 0; j < distribute_subvols; j++) {
                        /* by default assume quorum is not met
                           TODO: Handle distributed striped replicate volumes
                           Currently only distributed replicate volumes are
                           handled.
                        */
                        ret = 1;
                        quorum_met = _gf_false;
                        for (i = 0; i < volinfo->dist_leaf_count; i++) {
                                snprintf (key, sizeof (key),
                                          "%s%"PRId64".brick%"PRId64".status",
                                          key_prefix, index,
                                          (j * volinfo->dist_leaf_count) + i);
                                ret = dict_get_int32 (dict, key, &brick_online);
                                if (ret || !brick_online) {
                                        if (i == 0)
                                                first_brick_on = _gf_false;
                                        down_count++;
                                }
                        }

                        quorum_met = glusterd_volume_quorum_calculate (volinfo,
                                                                       dict,
                                                                    down_count,
                                                                first_brick_on,
                                                                    snap_force,
                                                                  quorum_count,
                                                                   quorum_type,
                                                                   op_errstr,
                                                                   op_errno);
                        /* goto out if quorum is not met */
                        if (!quorum_met) {
                                ret = -1;
                                goto out;
                        }

                        down_count = 0;
                        first_brick_on = _gf_true;
                }
        }

        if (quorum_met) {
                gf_msg_debug (this->name, 0, "volume %s is in quorum",
                        volinfo->volname);
                ret = 0;
        }

out:
        return ret;
}

int32_t
glusterd_snap_common_quorum_calculate (glusterd_volinfo_t *volinfo,
                                       dict_t *dict, int64_t index,
                                       char *key_prefix,
                                       int8_t snap_force,
                                       gf_boolean_t snap_volume,
                                       char **op_errstr,
                                       uint32_t *op_errno)
{
        int                 quorum_count      = 0;
        char               *quorum_type       = NULL;
        int32_t             tmp               = 0;
        int32_t             ret               = -1;
        xlator_t           *this              = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        /* for replicate volumes with replica count equal to or
           greater than 3, do quorum check by getting what type
           of quorum rule has been set by getting the volume
           option set. If getting the option fails, then assume
           default.
           AFR does this:
           if quorum type is "auto":
           - for odd numner of bricks (n), n/2 + 1
           bricks should be present
           - for even number of bricks n, n/2 bricks
           should be present along with the 1st
           subvolume
           if quorum type is not "auto":
           - get the quorum count from dict with the
           help of the option "cluster.quorum-count"
           if the option is not there in the dict,
           then assume quorum type is auto and follow
           the above method.
           For non replicate volumes quorum is met only if all
           the bricks of the volume are online
         */

        if (GF_CLUSTER_TYPE_REPLICATE == volinfo->type) {
                if (volinfo->replica_count % 2 == 0)
                        quorum_count = volinfo->replica_count/2;
                else
                        quorum_count =
                                       volinfo->replica_count/2 + 1;
        } else if (GF_CLUSTER_TYPE_DISPERSE == volinfo->type) {
                quorum_count = volinfo->disperse_count -
                               volinfo->redundancy_count;
        } else {
                quorum_count = volinfo->brick_count;
        }

        ret = dict_get_str (volinfo->dict, "cluster.quorum-type",
                            &quorum_type);
        if (!ret && !strcmp (quorum_type, "fixed")) {
                ret = dict_get_int32 (volinfo->dict,
                                      "cluster.quorum-count", &tmp);
                /* if quorum-type option is not found in the
                   dict assume auto quorum type. i.e n/2 + 1.
                   The same assumption is made when quorum-count
                   option cannot be obtained from the dict (even
                   if the quorum-type option is not set to auto,
                   the behavior is set to the default behavior)
                 */
                if (!ret) {
                        /* for dispersed volumes, only allow quorums
                           equal or larger than minimum functional
                           value.
                         */
                        if ((GF_CLUSTER_TYPE_DISPERSE != volinfo->type) ||
                            (tmp >= quorum_count)) {
                                quorum_count = tmp;
                        } else {
                                gf_msg(this->name, GF_LOG_INFO, 0,
                                       GD_MSG_QUORUM_COUNT_IGNORED,
                                       "Ignoring small quorum-count "
                                       "(%d) on dispersed volume", tmp);
                                quorum_type = NULL;
                        }
                } else
                        quorum_type = NULL;
        }

        ret = glusterd_volume_quorum_check (volinfo, index, dict,
                                            key_prefix,
                                            snap_force,
                                            quorum_count,
                                            quorum_type,
                                            op_errstr,
                                            op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOL_NOT_FOUND, "volume %s "
                        "is not in quorum", volinfo->volname);
                goto out;
        }

out:
        return ret;
}

int32_t
glusterd_snap_quorum_check_for_clone (dict_t *dict, gf_boolean_t snap_volume,
                                      char **op_errstr, uint32_t *op_errno)
{
        char                err_str[PATH_MAX] = {0, };
        char                key_prefix[PATH_MAX] = {0, };
        char               *snapname          = NULL;
        glusterd_snap_t    *snap              = NULL;
        glusterd_volinfo_t *volinfo           = NULL;
        glusterd_volinfo_t *tmp_volinfo       = NULL;
        char               *volname           = NULL;
        int64_t             volcount          = 0;
        char                key[PATH_MAX]     = {0, };
        int64_t             i                 = 0;
        int32_t             ret               = -1;
        xlator_t           *this              = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "dict is NULL");
                goto out;
        }

        if (snap_volume) {
                ret = dict_get_str (dict, "snapname", &snapname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "failed to "
                                "get snapname");
                        goto out;
                }

                snap = glusterd_find_snap_by_name (snapname);
                if (!snap) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_NOT_FOUND, "failed to "
                                "get the snapshot %s", snapname);
                        ret = -1;
                        goto out;
                }
        }

        /* Do a quorum check of glusterds also. Because, the missed snapshot
         * information will be saved by glusterd and if glusterds are not in
         * quorum, then better fail the snapshot
         */
        if (!does_gd_meet_server_quorum (this)) {
                snprintf (err_str, sizeof (err_str),
                          "glusterds are not in quorum");
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_SERVER_QUORUM_NOT_MET, "%s", err_str);
                *op_errstr = gf_strdup (err_str);
                *op_errno = EG_NODEDWN;
                ret = -1;
                goto out;
        } else
                gf_msg_debug (this->name, 0, "glusterds are in quorum");

        ret = dict_get_int64 (dict, "volcount", &volcount);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "failed to get "
                        "volcount");
                goto out;
        }

        for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "%s%"PRId64,
                          snap_volume?"snap-volname":"volname", i);
                ret = dict_get_str (dict, "clonename", &volname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "failed to "
                                "get clonename");
                        goto out;
                }

                if (snap_volume && snap) {
                        cds_list_for_each_entry (tmp_volinfo, &snap->volumes,
                                                 vol_list) {
                                if (!tmp_volinfo) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SNAP_NOT_FOUND,
                                                "failed to get snap volume "
                                                "for snap %s", snapname);
                                        ret = -1;
                                        goto out;
                                }
                                volinfo = tmp_volinfo;
                        }
                } else {
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VOL_NOT_FOUND,
                                        "failed to find the volume %s",
                                        volname);
                                goto out;
                        }
                }

                snprintf (key_prefix, sizeof (key_prefix),
                          "%s", snap_volume?"vol":"clone");

                ret = glusterd_snap_common_quorum_calculate (volinfo,
                                                             dict, i,
                                                             key_prefix,
                                                             0,
                                                             snap_volume,
                                                             op_errstr,
                                                             op_errno);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOL_NOT_FOUND, "volume %s "
                                "is not in quorum", volinfo->volname);
                        goto out;
                }
        }
out:
        return ret;
}


int32_t
glusterd_snap_quorum_check_for_create (dict_t *dict, gf_boolean_t snap_volume,
                                       char **op_errstr, uint32_t *op_errno)
{
        int8_t              snap_force        = 0;
        int32_t             force             = 0;
        char                err_str[PATH_MAX] = {0, };
        char                key_prefix[PATH_MAX] = {0, };
        char               *snapname          = NULL;
        glusterd_snap_t    *snap              = NULL;
        glusterd_volinfo_t *volinfo           = NULL;
        char               *volname           = NULL;
        int64_t             volcount          = 0;
        char                key[PATH_MAX]     = {0, };
        int64_t             i                 = 0;
        int32_t             ret               = -1;
        xlator_t           *this              = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "dict is NULL");
                goto out;
        }

        if (snap_volume) {
                ret = dict_get_str (dict, "snapname", &snapname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "failed to "
                                "get snapname");
                        goto out;
                }

                snap = glusterd_find_snap_by_name (snapname);
                if (!snap) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_NOT_FOUND, "failed to "
                                "get the snapshot %s", snapname);
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_get_int32 (dict, "flags", &force);
        if (!ret && (force & GF_CLI_FLAG_OP_FORCE))
                snap_force = 1;

        /* Do a quorum check of glusterds also. Because, the missed snapshot
         * information will be saved by glusterd and if glusterds are not in
         * quorum, then better fail the snapshot
         */
        if (!does_gd_meet_server_quorum (this)) {
                snprintf (err_str, sizeof (err_str),
                          "glusterds are not in quorum");
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_SERVER_QUORUM_NOT_MET, "%s", err_str);
                *op_errstr = gf_strdup (err_str);
                *op_errno = EG_NODEDWN;
                ret = -1;
                goto out;
        } else
                gf_msg_debug (this->name, 0, "glusterds are in quorum");

        ret = dict_get_int64 (dict, "volcount", &volcount);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "failed to get "
                        "volcount");
                goto out;
        }

        for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "%s%"PRId64,
                          snap_volume?"snap-volname":"volname", i);
                ret = dict_get_str (dict, key, &volname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "failed to "
                                "get volname");
                        goto out;
                }

                if (snap_volume) {
                        ret = glusterd_snap_volinfo_find (volname, snap,
                                                          &volinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAP_NOT_FOUND,
                                        "failed to get snap volume %s "
                                        "for snap %s", volname,
                                        snapname);
                                goto out;
                        }
                } else {
                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VOL_NOT_FOUND,
                                        "failed to find the volume %s",
                                        volname);
                                goto out;
                        }
                }

                snprintf (key_prefix, sizeof (key_prefix),
                          "%s", snap_volume?"snap-vol":"vol");

                ret = glusterd_snap_common_quorum_calculate (volinfo,
                                                             dict, i,
                                                             key_prefix,
                                                             snap_force,
                                                             snap_volume,
                                                             op_errstr,
                                                             op_errno);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOL_NOT_FOUND, "volume %s "
                                "is not in quorum", volinfo->volname);
                        goto out;
                }
        }
out:
        return ret;
}

int32_t
glusterd_snap_quorum_check (dict_t *dict, gf_boolean_t snap_volume,
                            char **op_errstr, uint32_t *op_errno)
{
        int32_t             ret               = -1;
        xlator_t           *this              = NULL;
        int32_t             snap_command      = 0;
        char                err_str[PATH_MAX] = {0, };

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "dict is NULL");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &snap_command);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                ret = glusterd_snap_quorum_check_for_create (dict, snap_volume,
                                                             op_errstr,
                                                             op_errno);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_QUORUM_CHECK_FAIL, "Quorum check"
                                "failed during snapshot create command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_CLONE:
                ret = glusterd_snap_quorum_check_for_clone (dict, !snap_volume,
                                                            op_errstr,
                                                            op_errno);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_QUORUM_CHECK_FAIL, "Quorum check"
                                "failed during snapshot clone command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_DELETE:
        case GF_SNAP_OPTION_TYPE_RESTORE:
                if (!does_gd_meet_server_quorum (this)) {
                        ret = -1;
                        snprintf (err_str, sizeof (err_str),
                                  "glusterds are not in quorum");
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_SERVER_QUORUM_NOT_MET, "%s",
                                err_str);
                        *op_errstr = gf_strdup (err_str);
                        *op_errno = EG_NODEDWN;
                        goto out;
                }

                gf_msg_debug (this->name, 0, "glusterds are in "
                        "quorum");
                break;
        default:
                break;
        }

        ret = 0;

out:
        return ret;
}

int32_t
glusterd_umount (const char *path)
{
        char               msg[NAME_MAX] = "";
        int32_t            ret           = -1;
        runner_t           runner        = {0, };
        xlator_t          *this          = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (path);

        runinit (&runner);
        snprintf (msg, sizeof (msg), "umount path %s", path);
        runner_add_args (&runner, _PATH_UMOUNT, "-f", path, NULL);
        runner_log (&runner, this->name, GF_LOG_DEBUG, msg);
        ret = runner_run (&runner);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_GLUSTERD_UMOUNT_FAIL, "umounting %s failed (%s)",
                        path, strerror (errno));

        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_copy_file (const char *source, const char *destination)
{
        int32_t         ret             =       -1;
        xlator_t        *this           =       NULL;
        char            buffer[1024]    =       "";
        int             src_fd          =       -1;
        int             dest_fd         =       -1;
        int             read_len        =       -1;
        struct  stat    stbuf           =       {0,};
        mode_t          dest_mode       =       0;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (source);
        GF_ASSERT (destination);

        /* Here is stat is made to get the file permission of source file*/
        ret = sys_lstat (source, &stbuf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "%s not found", source);
                goto out;
        }

        dest_mode = stbuf.st_mode & 0777;

        src_fd = open (source, O_RDONLY);
        if (src_fd < 0) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to open file %s",
                        source);
                goto out;
        }

        dest_fd = sys_creat (destination, dest_mode);
        if (dest_fd < 0) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_FILE_OP_FAILED,
                        "Unble to open a file %s", destination);
                goto out;
        }

        do {
                ret = sys_read (src_fd, buffer, sizeof (buffer));
                if (ret ==  -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "Error reading file "
                                "%s", source);
                        goto out;
                }
                read_len = ret;
                if (read_len == 0)
                        break;

                ret = sys_write (dest_fd, buffer, read_len);
                if (ret != read_len) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FILE_OP_FAILED, "Error writing in "
                                "file %s", destination);
                        goto out;
                }
        } while (ret > 0);
out:
        if (src_fd > 0)
                sys_close (src_fd);

        if (dest_fd > 0)
                sys_close (dest_fd);
        return ret;
}

int32_t
glusterd_copy_folder (const char *source, const char *destination)
{
        int32_t         ret                 = -1;
        xlator_t       *this                = NULL;
        DIR            *dir_ptr             = NULL;
        struct dirent  *entry               = NULL;
        struct dirent   scratch[2]          = {{0,},};
        char            src_path[PATH_MAX]  = {0,};
        char            dest_path[PATH_MAX] = {0,};

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (source);
        GF_ASSERT (destination);

        dir_ptr = sys_opendir (source);
        if (!dir_ptr) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED,  "Unable to open %s", source);
                goto out;
        }

        for (;;) {
                errno = 0;
                entry = sys_readdir (dir_ptr, scratch);
                if (!entry || errno != 0)
                        break;

                if (strcmp (entry->d_name, ".") == 0 ||
                    strcmp (entry->d_name, "..") == 0)
                        continue;
                ret = snprintf (src_path, sizeof (src_path), "%s/%s",
                                source, entry->d_name);
                if (ret < 0)
                        goto out;

                ret = snprintf (dest_path, sizeof (dest_path), "%s/%s",
                                destination, entry->d_name);
                if (ret < 0)
                        goto out;

                ret = glusterd_copy_file (src_path, dest_path);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY, "Could not copy "
                                "%s to %s", src_path, dest_path);
                        goto out;
                }
        }
out:
        if (dir_ptr)
                (void) sys_closedir (dir_ptr);

        return ret;
}

int32_t
glusterd_get_geo_rep_session (char *slave_key, char *origin_volname,
                              dict_t *gsync_slaves_dict, char *session,
                              char *slave)
{
        int32_t         ret             =       -1;
        char            *token          =       NULL;
        char            *tok            =       NULL;
        char            *temp           =       NULL;
        char            *ip             =       NULL;
        char            *ip_i           =       NULL;
        char            *ip_temp        =       NULL;
        char            *buffer         =       NULL;
        xlator_t        *this           =       NULL;
        char            *slave_temp     =       NULL;
        char            *save_ptr       =       NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (slave_key);
        GF_ASSERT (origin_volname);
        GF_ASSERT (gsync_slaves_dict);

        ret = dict_get_str (gsync_slaves_dict, slave_key, &buffer);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to "
                        "get value for key %s", slave_key);
                goto out;
        }

        temp = gf_strdup (buffer);
        if (!temp) {
                ret = -1;
                goto out;
        }

        /* geo-rep session string format being parsed:
         * "master_node_uuid:ssh://slave_host::slave_vol:slave_voluuid"
         */
        token = strtok_r (temp, "/", &save_ptr);

        token = strtok_r (NULL, ":", &save_ptr);
        if (!token) {
                ret = -1;
                goto out;
        }
        token++;

        ip = gf_strdup (token);
        if (!ip) {
                ret = -1;
                goto out;
        }
        ip_i = ip;

        token = strtok_r (NULL, ":", &save_ptr);
        if (!token) {
                ret = -1;
                goto out;
        }

        slave_temp = gf_strdup (token);
        if (!slave) {
                ret = -1;
                goto out;
        }

        /* If 'ip' has 'root@slavehost', point to 'slavehost' as
         * working directory for root users are created without
         * 'root@' */
        ip_temp = gf_strdup (ip);
        tok = strtok_r (ip_temp, "@", &save_ptr);
        if (tok && !strcmp (tok, "root"))
                ip_i = ip + 5;

        ret = snprintf (session, PATH_MAX, "%s_%s_%s",
                        origin_volname, ip_i, slave_temp);
        if (ret < 0) /* Negative value is an error */
                goto out;

        ret = snprintf  (slave, PATH_MAX, "%s::%s", ip, slave_temp);
        if (ret < 0) {
                goto out;
        }

        ret = 0; /* Success */

out:
        if (temp)
                GF_FREE (temp);

        if (ip)
                GF_FREE (ip);

        if (ip_temp)
                GF_FREE (ip_temp);

        if (slave_temp)
                GF_FREE (slave_temp);

        return ret;
}

int32_t
glusterd_copy_quota_files (glusterd_volinfo_t *src_vol,
                           glusterd_volinfo_t *dest_vol,
                           gf_boolean_t *conf_present) {

        int32_t         ret                     = -1;
        char            src_dir[PATH_MAX]       = "";
        char            dest_dir[PATH_MAX]      = "";
        char            src_path[PATH_MAX]      = "";
        char            dest_path[PATH_MAX]     = "";
        xlator_t        *this                   = NULL;
        glusterd_conf_t *priv                   = NULL;
        struct  stat    stbuf                   = {0,};

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (src_vol);
        GF_ASSERT (dest_vol);

        GLUSTERD_GET_VOLUME_DIR (src_dir, src_vol, priv);

        GLUSTERD_GET_VOLUME_DIR (dest_dir, dest_vol, priv);

        ret = snprintf (src_path, sizeof (src_path), "%s/quota.conf",
                        src_dir);
        if (ret < 0)
                goto out;

        /* quota.conf is not present if quota is not enabled, Hence ignoring
         * the absence of this file
         */
        ret = sys_lstat (src_path, &stbuf);
        if (ret) {
                ret = 0;
                gf_msg_debug (this->name, 0, "%s not found", src_path);
                goto out;
        }

        ret = snprintf (dest_path, sizeof (dest_path), "%s/quota.conf",
                       dest_dir);
        if (ret < 0)
                goto out;

        ret = glusterd_copy_file (src_path, dest_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Failed to copy %s in %s",
                        src_path, dest_path);
                goto out;
        }

        ret = snprintf (src_path, sizeof (src_path), "%s/quota.cksum",
                        src_dir);
        if (ret < 0)
                goto out;

        /* if quota.conf is present, quota.cksum has to be present. *
         * Fail snapshot operation if file is absent                *
         */
        ret = sys_lstat (src_path, &stbuf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_FILE_NOT_FOUND, "%s not found", src_path);
                goto out;
        }

        ret = snprintf (dest_path, sizeof (dest_path), "%s/quota.cksum",
                       dest_dir);
        if (ret < 0)
                goto out;

        ret = glusterd_copy_file (src_path, dest_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Failed to copy %s in %s",
                        src_path, dest_path);
                goto out;
        }

        *conf_present = _gf_true;
out:
        return ret;

}

/* *
 * Here there are two possibilities, either destination is snaphot or
 * clone. In the case of snapshot nfs_ganesha export file will be copied
 * to snapdir. If it is clone , then new export file will be created for
 * the clone in the GANESHA_EXPORT_DIRECTORY, replacing occurences of
 * volname with clonename
 */
int
glusterd_copy_nfs_ganesha_file (glusterd_volinfo_t *src_vol,
                                glusterd_volinfo_t *dest_vol)
{

        int32_t         ret                     = -1;
        char            snap_dir[PATH_MAX]      = {0,};
        char            src_path[PATH_MAX]      = {0,};
        char            dest_path[PATH_MAX]     = {0,};
        char            buffer[BUFSIZ]          = {0,};
        char            *find_ptr               = NULL;
        char            *buff_ptr               = NULL;
        char            *tmp_ptr                = NULL;
        xlator_t        *this                   = NULL;
        glusterd_conf_t *priv                   = NULL;
        struct  stat    stbuf                   = {0,};
        FILE            *src                    = NULL;
        FILE            *dest                   = NULL;


        this = THIS;
        GF_VALIDATE_OR_GOTO ("snapshot", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        GF_VALIDATE_OR_GOTO (this->name, src_vol, out);
        GF_VALIDATE_OR_GOTO (this->name, dest_vol, out);

        if (glusterd_check_ganesha_export(src_vol) == _gf_false) {
                gf_msg_debug (this->name, 0, "%s is not exported via "
                              "NFS-Ganesha. Skipping copy of export conf.",
                              src_vol->volname);
                ret = 0;
                goto out;
        }

        if (src_vol->is_snap_volume) {
                GLUSTERD_GET_SNAP_DIR (snap_dir, src_vol->snapshot, priv);
                ret = snprintf (src_path, PATH_MAX, "%s/export.%s.conf",
                                snap_dir, src_vol->snapshot->snapname);
        } else {
                ret = snprintf (src_path, PATH_MAX, "%s/export.%s.conf",
                                GANESHA_EXPORT_DIRECTORY, src_vol->volname);
        }
        if (ret < 0 || ret >= PATH_MAX)
                goto out;

        ret = sys_lstat (src_path, &stbuf);
        if (ret) {
                /*
                 * This code path is hit, only when the src_vol is being *
                 * exported via NFS-Ganesha. So if the conf file is not  *
                 * available, we fail the snapshot operation.            *
                 */
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "Stat on %s failed with %s",
                        src_path, strerror (errno));
                goto out;
        }

        if (dest_vol->is_snap_volume) {
                memset (snap_dir, 0 , PATH_MAX);
                GLUSTERD_GET_SNAP_DIR (snap_dir, dest_vol->snapshot, priv);
                ret = snprintf (dest_path, sizeof (dest_path),
                                "%s/export.%s.conf", snap_dir,
                                dest_vol->snapshot->snapname);
                if (ret < 0)
                        goto out;

                ret = glusterd_copy_file (src_path, dest_path);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY, "Failed to copy %s in %s",
                                src_path, dest_path);
                        goto out;
                }

        } else {
                ret = snprintf (dest_path, sizeof (dest_path),
                                "%s/export.%s.conf", GANESHA_EXPORT_DIRECTORY,
                                dest_vol->volname);
                if (ret < 0)
                        goto out;

                src = fopen (src_path, "r");
                dest = fopen (dest_path, "w");

                if (!src || !dest) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FILE_OP_FAILED,
                                "Failed to open %s",
                                dest ? src_path : dest_path);
                        ret = -1;
                        goto out;
                }

                /* *
                 * if the source volume is snapshot, the export conf file
                 * consists of orginal volname
                 */
                if (src_vol->is_snap_volume)
                        find_ptr = gf_strdup (src_vol->parent_volname);
                else
                        find_ptr = gf_strdup (src_vol->volname);

                if (!find_ptr)
                        goto out;

                /* Replacing volname with clonename */
                while (fgets(buffer, BUFSIZ, src)) {
                        buff_ptr = buffer;
                        while ((tmp_ptr = strstr(buff_ptr, find_ptr))) {
                                while (buff_ptr < tmp_ptr)
                                        fputc((int)*buff_ptr++, dest);
                                fputs(dest_vol->volname, dest);
                                buff_ptr += strlen(find_ptr);
                        }
                        fputs(buff_ptr, dest);
                        memset (buffer, 0, BUFSIZ);
                }
        }
out:
        if (src)
                fclose (src);
        if (dest)
                fclose (dest);
        if (find_ptr)
                GF_FREE(find_ptr);

        return ret;
}

int32_t
glusterd_restore_geo_rep_files (glusterd_volinfo_t *snap_vol)
{
        int32_t                 ret                     =       -1;
        char                    src_path[PATH_MAX]      =       "";
        char                    dest_path[PATH_MAX]     =       "";
        xlator_t                *this                   =       NULL;
        char                    *origin_volname         =       NULL;
        glusterd_volinfo_t      *origin_vol             =       NULL;
        int                     i                       =       0;
        char                    key[PATH_MAX]           =       "";
        char                    session[PATH_MAX]       =       "";
        char                    slave[PATH_MAX]         =       "";
        char                    snapgeo_dir[PATH_MAX]   =       "";
        glusterd_conf_t         *priv                   =       NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (snap_vol);

        origin_volname = gf_strdup (snap_vol->parent_volname);
        if (!origin_volname) {
                ret = -1;
                goto out;
        }

        ret = glusterd_volinfo_find (origin_volname, &origin_vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "Unable to fetch "
                        "volinfo for volname %s", origin_volname);
                goto out;
        }

        for (i = 1 ; i <= snap_vol->gsync_slaves->count; i++) {
                ret = snprintf (key, sizeof (key), "slave%d", i);
                if (ret < 0) {
                        goto out;
                }

                /* "origin_vol" is used here because geo-replication saves
                 * the session in the form of master_ip_slave.
                 * As we need the master volume to be same even after
                 * restore, we are passing the origin volume name.
                 *
                 * "snap_vol->gsync_slaves" contain the slave information
                 * when the snapshot was taken, hence we have to restore all
                 * those slaves information when we do snapshot restore.
                 */
                ret = glusterd_get_geo_rep_session (key, origin_vol->volname,
                                                    snap_vol->gsync_slaves,
                                                    session, slave);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GEOREP_GET_FAILED,
                                "Failed to get geo-rep session");
                        goto out;
                }

                GLUSTERD_GET_SNAP_GEO_REP_DIR(snapgeo_dir, snap_vol->snapshot,
                                              priv);
                ret = snprintf (src_path, sizeof (src_path),
                                "%s/%s", snapgeo_dir, session);
                if (ret < 0)
                        goto out;

                ret = snprintf (dest_path, sizeof (dest_path),
                                "%s/%s/%s", priv->workdir, GEOREP,
                                session);
                if (ret < 0)
                        goto out;

                ret = glusterd_copy_folder (src_path, dest_path);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DIR_OP_FAILED, "Could not copy "
                                "%s to %s", src_path, dest_path);
                        goto out;
                }
        }
out:
        if (origin_volname)
                GF_ASSERT (origin_volname);

        return ret;
}

int
glusterd_restore_nfs_ganesha_file (glusterd_volinfo_t *src_vol,
                                   glusterd_snap_t *snap)
{

        int32_t         ret                     = -1;
        char            snap_dir[PATH_MAX]      = "";
        char            src_path[PATH_MAX]      = "";
        char            dest_path[PATH_MAX]     = "";
        xlator_t        *this                   = NULL;
        glusterd_conf_t *priv                   = NULL;
        struct  stat    stbuf                   = {0,};

        this = THIS;
        GF_VALIDATE_OR_GOTO ("snapshot", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        GF_VALIDATE_OR_GOTO (this->name, src_vol, out);
        GF_VALIDATE_OR_GOTO (this->name, snap, out);

        GLUSTERD_GET_SNAP_DIR (snap_dir, snap, priv);

        ret = snprintf (src_path, sizeof (src_path), "%s/export.%s.conf",
                       snap_dir, snap->snapname);
        if (ret < 0)
                goto out;

        ret = sys_lstat (src_path, &stbuf);
        if (ret) {
                if (errno == ENOENT) {
                        ret = 0;
                        gf_msg_debug (this->name, 0, "%s not found", src_path);
                } else
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                GD_MSG_FILE_OP_FAILED,
                                "Stat on %s failed with %s",
                                src_path, strerror (errno));
                goto out;
        }

        ret = snprintf (dest_path, sizeof (dest_path), "%s/export.%s.conf",
                        GANESHA_EXPORT_DIRECTORY, src_vol->volname);
        if (ret < 0)
                goto out;

        ret = glusterd_copy_file (src_path, dest_path);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Failed to copy %s in %s",
                        src_path, dest_path);

out:
        return ret;

}
/* Snapd functions */
int
glusterd_is_snapd_enabled (glusterd_volinfo_t *volinfo)
{
        int              ret    = 0;
        xlator_t        *this   = THIS;

        ret = dict_get_str_boolean (volinfo->dict, "features.uss", -2);
        if (ret == -2) {
                gf_msg_debug (this->name, 0, "Key features.uss not "
                        "present in the dict for volume %s", volinfo->volname);
                ret = 0;

        } else if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get 'features.uss'"
                        " from dict for volume %s", volinfo->volname);
        }

        return ret;
}


int32_t
glusterd_is_snap_soft_limit_reached (glusterd_volinfo_t *volinfo, dict_t *dict)
{
        int32_t         ret            = -1;
        uint64_t        opt_max_hard   = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
        uint64_t        opt_max_soft   = GLUSTERD_SNAPS_DEF_SOFT_LIMIT_PERCENT;
        uint64_t        limit          = 0;
        int             auto_delete    = 0;
        uint64_t        effective_max_limit = 0;
        xlator_t        *this          = NULL;
        glusterd_conf_t *priv          = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        /* config values snap-max-hard-limit and snap-max-soft-limit are
         * optional and hence we are not erroring out if values are not
         * present
         */
        gd_get_snap_conf_values_if_present (priv->opts, &opt_max_hard,
                                            &opt_max_soft);

        /* "auto-delete" might not be set by user explicitly,
         * in that case it's better to consider the default value.
         * Hence not erroring out if Key is not found.
         */
        auto_delete = dict_get_str_boolean (priv->opts,
                                GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE,
                                _gf_false);

        if (volinfo->snap_max_hard_limit < opt_max_hard)
                effective_max_limit = volinfo->snap_max_hard_limit;
        else
                effective_max_limit = opt_max_hard;

        limit = (opt_max_soft * effective_max_limit)/100;

        if (volinfo->snap_count >= limit && auto_delete != _gf_true) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_SOFT_LIMIT_REACHED, "Soft-limit "
                        "(value = %"PRIu64") of volume %s is reached. "
                        "Snapshot creation is not possible once effective "
                        "hard-limit (value = %"PRIu64") is reached.",
                        limit, volinfo->volname, effective_max_limit);

                ret = dict_set_int8 (dict, "soft-limit-reach",
                                     _gf_true);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "Failed to "
                                "set soft limit exceed flag in "
                                "response dictionary");
                }

                goto out;
        }
        ret = 0;
out:
        return ret;
}

/* This function initializes the parameter sys_hard_limit,
 * sys_soft_limit and auto_delete value to the value set
 * in dictionary, If value is not present then it is
 * initialized to default values. Hence this function does not
 * return any values.
 */
void
gd_get_snap_conf_values_if_present (dict_t *dict, uint64_t *sys_hard_limit,
                                    uint64_t *sys_soft_limit)
{
        xlator_t        *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (dict);

        /* "snap-max-hard-limit" might not be set by user explicitly,
         * in that case it's better to consider the default value.
         * Hence not erroring out if Key is not found.
         */
        if (dict_get_uint64 (dict, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                               sys_hard_limit)) {
                gf_msg_debug (this->name, 0, "%s is not present in"
                        "dictionary",
                        GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
        }

        /* "snap-max-soft-limit" might not be set by user explicitly,
         * in that case it's better to consider the default value.
         * Hence not erroring out if Key is not found.
         */
        if (dict_get_uint64 (dict, GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT,
                              sys_soft_limit)) {
                gf_msg_debug (this->name, 0, "%s is not present in"
                        "dictionary",
                        GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT);
        }
}

int
glusterd_get_snap_status_str (glusterd_snap_t *snapinfo, char *snap_status_str)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO (THIS->name, snapinfo, out);
        GF_VALIDATE_OR_GOTO (THIS->name, snap_status_str, out);

        switch (snapinfo->snap_status) {
        case GD_SNAP_STATUS_NONE:
                sprintf (snap_status_str, "%s", "none");
                break;
        case GD_SNAP_STATUS_INIT:
                sprintf (snap_status_str, "%s", "init");
                break;
        case GD_SNAP_STATUS_IN_USE:
                sprintf (snap_status_str, "%s", "in_use");
                break;
        case GD_SNAP_STATUS_DECOMMISSION:
                sprintf (snap_status_str, "%s", "decommissioned");
                break;
        case GD_SNAP_STATUS_UNDER_RESTORE:
                sprintf (snap_status_str, "%s", "under_restore");
                break;
        case GD_SNAP_STATUS_RESTORED:
                sprintf (snap_status_str, "%s", "restored");
                break;
        default:
                goto out;
        }
        ret = 0;
out:
        return ret;
}

