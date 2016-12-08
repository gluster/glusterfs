/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"
#include "glusterfs.h"
#include "syscall.h"
#include "compat-errno.h"

#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-snapd-svc-helper.h"
#include "glusterd-tierd-svc-helper.h"
#include "glusterd-volgen.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-messages.h"
#include "glusterfs3.h"
#include "protocol-common.h"
#include "rpcsvc.h"
#include "rpc-common-xdr.h"

extern struct rpc_clnt_program gd_peer_prog;
extern struct rpc_clnt_program gd_mgmt_prog;
extern struct rpc_clnt_program gd_mgmt_v3_prog;


#define TRUSTED_PREFIX         "trusted-"
#define GD_PEER_ID_KEY         "peer-id"

typedef ssize_t (*gfs_serialize_t) (struct iovec outmsg, void *data);

static int
get_snap_volname_and_volinfo (const char *volpath, char **volname,
                              glusterd_volinfo_t **volinfo)
{
        int              ret            = -1;
        char            *save_ptr       = NULL;
        char            *str_token      = NULL;
        char            *snapname       = NULL;
        char            *volname_token  = NULL;
        char            *vol            = NULL;
        glusterd_snap_t *snap           = NULL;
        xlator_t        *this           = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (volpath);
        GF_ASSERT (volinfo);

        str_token = gf_strdup (volpath);
        if (NULL == str_token) {
                goto out;
        }

        /* Input volname will have below formats:
         * /snaps/<snapname>/<volname>.<hostname>
         * or
         * /snaps/<snapname>/<parent-volname>
         * We need to extract snapname and parent_volname */

        /*split string by "/" */
        strtok_r (str_token, "/",  &save_ptr);
        snapname  = strtok_r(NULL, "/", &save_ptr);
        if (!snapname) {
                gf_msg(this->name, GF_LOG_ERROR, EINVAL,
                       GD_MSG_INVALID_ENTRY,
                       "Invalid path: %s", volpath);
                goto out;
        }

        volname_token = strtok_r(NULL, "/", &save_ptr);
        if (!volname_token) {
                gf_msg (this->name, GF_LOG_ERROR,
                        EINVAL, GD_MSG_INVALID_ENTRY,
                        "Invalid path: %s", volpath);
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (!snap) {
                gf_msg(this->name, GF_LOG_ERROR, EINVAL,
                       GD_MSG_SNAP_NOT_FOUND, "Failed to "
                       "fetch snap %s", snapname);
                goto out;
        }

        /* Find if its a parent volume name or snap volume
         * name. This function will succeed if volname_token
         * is a parent volname
         */
        ret = glusterd_volinfo_find (volname_token, volinfo);
        if (ret) {
                *volname = gf_strdup (volname_token);
                if (NULL == *volname) {
                        ret = -1;
                        goto out;
                }

                ret = glusterd_snap_volinfo_find (volname_token, snap,
                                                  volinfo);
                if (ret) {
                        /* Split the volume name */
                        vol = strtok_r (volname_token, ".", &save_ptr);
                        if (!vol) {
                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_INVALID_ENTRY, "Invalid "
                                                "volname (%s)", volname_token);
                                goto out;
                        }

                        ret = glusterd_snap_volinfo_find (vol, snap, volinfo);
                        if (ret) {
                                gf_msg(this->name, GF_LOG_ERROR, 0,
                                       GD_MSG_SNAP_INFO_FAIL, "Failed to "
                                       "fetch snap volume from volname (%s)",
                                       vol);
                                goto out;
                        }
                }
        } else {
                /*volname_token is parent volname*/
                ret = glusterd_snap_volinfo_find_from_parent_volname (
                                volname_token, snap, volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_INFO_FAIL, "Failed to "
                                "fetch snap volume from parent "
                                "volname (%s)", volname_token);
                        goto out;
                }

                /* Since volname_token is a parent volname we should
                 * get the snap volname here*/
                *volname = gf_strdup ((*volinfo)->volname);
                if (NULL == *volname) {
                        ret = -1;
                        goto out;
                }
        }

out:
        if (ret && NULL != *volname) {
                GF_FREE (*volname);
                *volname = NULL;
        }
        return ret;
}

int32_t
glusterd_get_client_per_brick_volfile (glusterd_volinfo_t *volinfo,
                                       char *filename, char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = NULL;
        int32_t                 ret                    = -1;

        GF_VALIDATE_OR_GOTO ("glusterd", THIS, out);
        priv = THIS->private;
        GF_VALIDATE_OR_GOTO (THIS->name, priv, out);

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/%s", workdir, filename);

        ret = 0;
out:
        return ret;
}

size_t
build_volfile_path (char *volume_id, char *path,
                    size_t path_len, char *trusted_str)
{
        struct stat              stbuf                  = {0,};
        int32_t                  ret                    = -1;
        char                    *vol                    = NULL;
        char                    *dup_volname            = NULL;
        char                    *save_ptr               = NULL;
        char                    *free_ptr               = NULL;
        char                    *volname                = NULL;
        char                    *volid_ptr              = NULL;
        char                     dup_volid[PATH_MAX]    = {0,};
        char                     path_prefix[PATH_MAX]  = {0,};
        xlator_t                *this                   = NULL;
        glusterd_volinfo_t      *volinfo                = NULL;
        glusterd_conf_t         *priv                   = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (volume_id);
        GF_ASSERT (path);

        volid_ptr = strstr (volume_id, "snapd/");
        if (volid_ptr) {
                volid_ptr = strchr (volid_ptr, '/');
                if (!volid_ptr) {
                        ret = -1;
                        goto out;
                }
                volid_ptr++;

                ret = glusterd_volinfo_find (volid_ptr, &volinfo);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL,
                                "Couldn't find volinfo");
                        goto out;
                }
                glusterd_svc_build_snapd_volfile (volinfo, path, path_len);
                ret = 0;
                goto out;

        }

        volid_ptr = strstr (volume_id, "tierd/");
        if (volid_ptr) {
                volid_ptr = strchr (volid_ptr, '/');
                if (!volid_ptr) {
                        ret = -1;
                        goto out;
                }
                volid_ptr++;

                ret = glusterd_volinfo_find (volid_ptr, &volinfo);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL,
                                "Couldn't find volinfo");
                        goto out;
                }
                glusterd_svc_build_tierd_volfile_path (volinfo, path, path_len);
                ret = 0;
                goto out;

        }

        volid_ptr = strstr (volume_id, "gluster/");
        if (volid_ptr) {
                volid_ptr = strchr (volid_ptr, '/');
                if (!volid_ptr) {
                        ret = -1;
                        goto out;
                }
                volid_ptr++;

                glusterd_svc_build_volfile_path (volid_ptr,
                                                 priv->workdir,
                                                 path, path_len);
                ret = 0;
                goto out;

        }

        volid_ptr = strstr (volume_id, "/snaps/");
        if (volid_ptr) {
                ret = get_snap_volname_and_volinfo (volid_ptr, &volname,
                                                    &volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_INFO_FAIL, "Failed to get snap"
                                " volinfo from path (%s)", volume_id);
                        ret = -1;
                        goto out;
                }

                snprintf (path_prefix, sizeof (path_prefix), "%s/snaps/%s",
                          priv->workdir, volinfo->snapshot->snapname);

                volid_ptr = volname;
                /* this is to ensure that volname recvd from
                   get_snap_volname_and_volinfo is free'd */
                free_ptr = volname;
                goto gotvolinfo;

        }

        volid_ptr = strstr (volume_id, "rebalance/");
        if (volid_ptr) {
                volid_ptr = strchr (volid_ptr, '/');
                if (!volid_ptr) {
                        ret = -1;
                        goto out;
                }
                volid_ptr++;

                ret = glusterd_volinfo_find (volid_ptr, &volinfo);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL,
                                "Couldn't find volinfo");
                        goto out;
                }
                glusterd_get_rebalance_volfile (volinfo, path, path_len);
                ret = 0;
                goto out;
        }

        volid_ptr = strstr (volume_id, "client_per_brick/");
        if (volid_ptr) {
                volid_ptr = strchr (volid_ptr, '/');
                if (!volid_ptr) {
                        ret = -1;
                        goto out;
                }
                volid_ptr++;

                dup_volname = gf_strdup (volid_ptr);
                if (!dup_volname) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY,
                                "strdup failed");
                        ret = -1;
                        goto out;
                }

                /* Split the volume name */
                vol = strtok_r (dup_volname, ".", &save_ptr);
                if (!vol) {
                        ret = -1;
                        goto out;
                }
                ret = glusterd_volinfo_find (vol, &volinfo);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL,
                                "Couldn't find volinfo");
                        goto out;
                }
                ret = glusterd_get_client_per_brick_volfile (volinfo, volid_ptr,
                                                             path, path_len);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_NO_MEMORY, "failed to get volinfo path");
                        goto out;
                }

                ret = sys_access (path, F_OK);
                goto out;
        }

        if (volume_id[0] == '/') {
                /* Normal behavior */
                volid_ptr = volume_id;
                volid_ptr++;

        } else {
                /* Bringing in NFS like behavior for mount command, */
                /* With this, one can mount a volume with below cmd */
                /* bash# mount -t glusterfs server:/volume /mnt/pnt */
                volid_ptr = volume_id;
        }

        snprintf (path_prefix, sizeof (path_prefix), "%s/vols",
                  priv->workdir);

        ret = glusterd_volinfo_find (volid_ptr, &volinfo);

        if (ret) {
                dup_volname = gf_strdup (volid_ptr);
                if (!dup_volname) {
                        ret = -1;
                        goto out;
                }
                /* Split the volume name */
                vol = strtok_r (dup_volname, ".", &save_ptr);
                if (!vol) {
                        ret = -1;
                        goto out;
                }
                ret = glusterd_volinfo_find (vol, &volinfo);
                if (ret)
                        goto out;
        }

gotvolinfo:
        if (!glusterd_auth_get_username (volinfo))
                trusted_str = NULL;

        ret = snprintf (path, path_len, "%s/%s/%s.vol", path_prefix,
                        volinfo->volname, volid_ptr);
        if (ret == -1)
                goto out;

        ret = sys_stat (path, &stbuf);

        if ((ret == -1) && (errno == ENOENT)) {
                strncpy (dup_volid, volid_ptr, (PATH_MAX - 1));
                if (!strchr (dup_volid, '.')) {
                        switch (volinfo->transport_type) {
                        case GF_TRANSPORT_TCP:
                                strcat (dup_volid, ".tcp");
                                break;
                        case GF_TRANSPORT_RDMA:
                                strcat (dup_volid, ".rdma");
                                break;
                        case GF_TRANSPORT_BOTH_TCP_RDMA:
                                strcat (dup_volid, ".tcp");
                                break;
                        default:
                                ret = -1;
                                break;
                        }
                }
                snprintf (path, path_len, "%s/%s/%s%s-fuse.vol",
                          path_prefix, volinfo->volname,
                          (trusted_str ? trusted_str : ""),
                          dup_volid);
                ret = sys_stat (path, &stbuf);
        }
out:
        if (dup_volname)
                GF_FREE (dup_volname);
        if (free_ptr)
                GF_FREE (free_ptr);
        return ret;
}

/* Get and store op-versions of the clients sending the getspec request
 * Clients of versions <= 3.3, don't send op-versions, their op-versions are
 * defaulted to 1. Also fetch brick_name.
 */
int32_t
glusterd_get_args_from_dict (gf_getspec_req *args, peer_info_t *peerinfo,
                             char **brick_name)
{
        dict_t    *dict                  = NULL;
        int        client_max_op_version = 1;
        int        client_min_op_version = 1;
        int32_t    ret                   = -1;
        xlator_t  *this                  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (args);
        GF_ASSERT (peerinfo);

        if (!args->xdata.xdata_len) {
                ret = 0;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (args->xdata.xdata_val,
                                args->xdata.xdata_len, &dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_UNSERIALIZE_FAIL,
                        "Failed to unserialize request dictionary");
                goto out;
        }

        ret = dict_get_int32 (dict, "min-op-version",
                              &client_min_op_version);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get client-min-op-version");
                goto out;
        }

        ret = dict_get_int32 (dict, "max-op-version",
                              &client_max_op_version);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get client-max-op-version");
                goto out;
        }

        ret = dict_get_str (dict, "brick_name",
                            brick_name);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "No brick name present");
                ret = 0;
                goto out;
        }

        gf_msg_debug (this->name, 0, "brick_name = %s", *brick_name);
out:
        peerinfo->max_op_version = client_max_op_version;
        peerinfo->min_op_version = client_min_op_version;

        if (dict)
                dict_unref (dict);


        return ret;
}

/* Given the missed_snapinfo and snap_opinfo take the
 * missed lvm snapshot
 */
int32_t
glusterd_create_missed_snap (glusterd_missed_snap_info *missed_snapinfo,
                             glusterd_snap_op_t *snap_opinfo)
{
        char                        *device           = NULL;
        glusterd_conf_t             *priv             = NULL;
        glusterd_snap_t             *snap             = NULL;
        glusterd_volinfo_t          *snap_vol         = NULL;
        glusterd_volinfo_t          *volinfo          = NULL;
        glusterd_brickinfo_t        *brickinfo        = NULL;
        int32_t                      ret              = -1;
        int32_t                      i                = 0;
        uuid_t                       snap_uuid        = {0,};
        xlator_t                    *this             = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (missed_snapinfo);
        GF_ASSERT (snap_opinfo);

        gf_uuid_parse (missed_snapinfo->snap_uuid, snap_uuid);

        /* Find the snap-object */
        snap = glusterd_find_snap_by_id (snap_uuid);
        if (!snap) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_NOT_FOUND,
                        "Unable to find the snap with snap_uuid %s",
                        missed_snapinfo->snap_uuid);
                ret = -1;
                goto out;
        }

        /* Find the snap_vol */
        cds_list_for_each_entry (volinfo, &snap->volumes, vol_list) {
                if (!strcmp (volinfo->volname,
                             snap_opinfo->snap_vol_id)) {
                        snap_vol = volinfo;
                        break;
                }
        }

        if (!snap_vol) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND,
                        "Unable to find the snap_vol(%s) "
                        "for snap(%s)", snap_opinfo->snap_vol_id,
                        snap->snapname);
                ret = -1;
                goto out;
        }

        /* Find the missed brick in the snap volume */
        cds_list_for_each_entry (brickinfo, &snap_vol->bricks, brick_list) {
                i++;
                if (i == snap_opinfo->brick_num)
                        break;
        }

        if (brickinfo->snap_status != -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_STATUS_NOT_PENDING,
                        "The snap status of the missed "
                        "brick(%s) is not pending", brickinfo->path);
                goto out;
        }

        /* Fetch the device path */
        device = glusterd_get_brick_mount_device (snap_opinfo->brick_path);
        if (!device) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_GET_INFO_FAIL,
                        "Getting device name for the"
                        "brick %s:%s failed", brickinfo->hostname,
                        snap_opinfo->brick_path);
                ret = -1;
                goto out;
        }

        device = glusterd_build_snap_device_path (device, snap_vol->volname,
                                                  snap_opinfo->brick_num - 1);
        if (!device) {
                gf_msg (this->name, GF_LOG_ERROR, ENXIO,
                        GD_MSG_SNAP_DEVICE_NAME_GET_FAIL,
                        "cannot copy the snapshot "
                        "device name (volname: %s, snapname: %s)",
                         snap_vol->volname, snap->snapname);
                ret = -1;
                goto out;
        }
        strncpy (brickinfo->device_path, device,
                 sizeof(brickinfo->device_path));

        /* Update the backend file-system type of snap brick in
         * snap volinfo. */
        ret = glusterd_update_mntopts (snap_opinfo->brick_path, brickinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRK_MOUNTOPTS_FAIL, "Failed to update "
                        "mount options for %s brick", brickinfo->path);
                /* We should not fail snapshot operation if we fail to get
                 * the file-system type */
        }

        ret = glusterd_take_lvm_snapshot (brickinfo, snap_opinfo->brick_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPSHOT_OP_FAILED,
                        "Failed to take snapshot of %s",
                        snap_opinfo->brick_path);
                goto out;
        }

        /* After the snapshot both the origin brick (LVM brick) and
         * the snapshot brick will have the same file-system label. This
         * will cause lot of problems at mount time. Therefore we must
         * generate a new label for the snapshot brick
         */
        ret = glusterd_update_fs_label (brickinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_SET_INFO_FAIL, "Failed to update "
                        "file-system label for %s brick", brickinfo->path);
                /* Failing to update label should not cause snapshot failure.
                 * Currently label is updated only for XFS and ext2/ext3/ext4
                 * file-system.
                 */
        }

        /* Create and mount the snap brick */
        ret = glusterd_snap_brick_create (snap_vol, brickinfo,
                                          snap_opinfo->brick_num - 1, 0);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_CREATION_FAIL, "Failed to "
                        " create and mount the brick(%s) for the snap %s",
                        snap_opinfo->brick_path,
                        snap_vol->snapshot->snapname);
                goto out;
        }

        brickinfo->snap_status = 0;
        ret = glusterd_store_volinfo (snap_vol,
                                      GLUSTERD_VOLINFO_VER_AC_NONE);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_STORE_FAIL, "Failed to store snapshot "
                        "volinfo (%s) for snap %s", snap_vol->volname,
                        snap->snapname);
                goto out;
        }

        ret = glusterd_brick_start (snap_vol, brickinfo, _gf_false);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_BRICK_DISCONNECTED, "starting the "
                        "brick %s:%s for the snap %s failed",
                        brickinfo->hostname, brickinfo->path,
                        snap->snapname);
                goto out;
        }
out:
        if (device)
                GF_FREE (device);

        return ret;
}

/* Look into missed_snap_list, to see it the given brick_name,
 * has any missed snap creates for the local node */
int32_t
glusterd_take_missing_brick_snapshots (char *brick_name)
{
        char                        *my_node_uuid     = NULL;
        glusterd_conf_t             *priv             = NULL;
        glusterd_missed_snap_info   *missed_snapinfo  = NULL;
        glusterd_snap_op_t          *snap_opinfo      = NULL;
        int32_t                      ret              = -1;
        gf_boolean_t                 update_list      = _gf_false;
        xlator_t                    *this             = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (brick_name);

        my_node_uuid = uuid_utoa (MY_UUID);

        cds_list_for_each_entry (missed_snapinfo, &priv->missed_snaps_list,
                                 missed_snaps) {
                /* If the missed snap op is not for the local node
                 * then continue
                 */
                if (strcmp (my_node_uuid, missed_snapinfo->node_uuid))
                        continue;

                cds_list_for_each_entry (snap_opinfo,
                                         &missed_snapinfo->snap_ops,
                                         snap_ops_list) {
                        /* Check if the missed snap's op is a create for
                         * the brick name in question
                         */
                        if ((snap_opinfo->op == GF_SNAP_OPTION_TYPE_CREATE) &&
                            (!strcmp (brick_name, snap_opinfo->brick_path))) {
                                /* Perform a snap create if the
                                 * op is still pending
                                 */
                                if (snap_opinfo->status ==
                                                 GD_MISSED_SNAP_PENDING) {
                                        ret = glusterd_create_missed_snap
                                                              (missed_snapinfo,
                                                               snap_opinfo);
                                        if (ret) {
                                                gf_msg (this->name,
                                                        GF_LOG_ERROR, 0,
                                                        GD_MSG_MISSED_SNAP_CREATE_FAIL,
                                                        "Failed to create "
                                                        "missed snap for %s",
                                                        brick_name);
                                                /* At this stage, we will mark
                                                 * the entry as done. Because
                                                 * of the failure other
                                                 * snapshots will not be
                                                 * affected, and neither the
                                                 * brick. Only the current snap
                                                 * brick will always remain as
                                                 * pending.
                                                 */
                                        }
                                        snap_opinfo->status =
                                                 GD_MISSED_SNAP_DONE;
                                        update_list = _gf_true;
                                }
                                /* One snap-id won't have more than one missed
                                 * create for the same brick path. Hence
                                 * breaking in search of another missed create
                                 * for the same brick path in the local node
                                 */
                                break;
                        }
                }
        }

        if (update_list == _gf_true) {
                ret = glusterd_store_update_missed_snaps ();
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                                "Failed to update missed_snaps_list");
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

/* Checks if the client supports the volume, ie. client can understand all the
 * options in the volfile
 */
static gf_boolean_t
_client_supports_volume (peer_info_t *peerinfo, int32_t *op_errno)
{
        gf_boolean_t       ret       = _gf_true;
        glusterd_volinfo_t *volinfo  = NULL;

        GF_ASSERT (peerinfo);
        GF_ASSERT (op_errno);


        /* Only check when the volfile being requested is a volume. Not finding
         * a volinfo implies that the volfile requested for is not of a gluster
         * volume. A non volume volfile is requested by the local gluster
         * services like shd and nfs-server. These need not be checked as they
         * will be running at the same op-version as glusterd and will be able
         * to support all the features
         */
        if ((glusterd_volinfo_find (peerinfo->volname, &volinfo) == 0) &&
            ((peerinfo->min_op_version > volinfo->client_op_version) ||
             (peerinfo->max_op_version < volinfo->client_op_version))) {
                ret = _gf_false;
                *op_errno = ENOTSUP;
                gf_msg ("glusterd", GF_LOG_INFO, ENOTSUP,
                        GD_MSG_UNSUPPORTED_VERSION,
                        "Client %s (%d -> %d) doesn't support required "
                        "op-version (%d). Rejecting volfile request.",
                        peerinfo->identifier, peerinfo->min_op_version,
                        peerinfo->max_op_version, volinfo->client_op_version);
        }

        return ret;
}

int
__server_getspec (rpcsvc_request_t *req)
{
        int32_t               ret                    = -1;
        int32_t               op_ret                 = -1;
        int32_t               op_errno               = 0;
        int32_t               spec_fd                = -1;
        size_t                file_len               = 0;
        char                  filename[PATH_MAX]  = {0,};
        struct stat           stbuf                  = {0,};
        char                 *brick_name             = NULL;
        char                 *volume                 = NULL;
        char                 *tmp                    = NULL;
        int                   cookie                 = 0;
        rpc_transport_t      *trans                  = NULL;
        gf_getspec_req        args                   = {0,};
        gf_getspec_rsp        rsp                    = {0,};
        char                  addrstr[RPCSVC_PEER_STRLEN] = {0};
        peer_info_t          *peerinfo               = NULL;
        xlator_t             *this                   = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_getspec_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        peerinfo = &req->trans->peerinfo;

        volume = args.key;

        /* Need to strip leading '/' from volnames. This was introduced to
         * support nfs style mount parameters for native gluster mount
         */
        if (volume[0] == '/')
                strncpy (peerinfo->volname, &volume[1], strlen(&volume[1]));
        else
                strncpy (peerinfo->volname, volume, strlen(volume));

        ret = glusterd_get_args_from_dict (&args, peerinfo, &brick_name);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to get args from dict");
                goto fail;
        }

        if (!_client_supports_volume (peerinfo, &op_errno)) {
                ret = -1;
                goto fail;
        }

        trans = req->trans;
        /* addrstr will be empty for cli socket connections */
        ret = rpcsvc_transport_peername (trans, (char *)&addrstr,
                                         sizeof (addrstr));
        if (ret)
                goto fail;

        tmp  = strrchr (addrstr, ':');
        if (tmp)
                *tmp = '\0';

        /* The trusted volfiles are given to the glusterd owned process like NFS
         * server, self-heal daemon etc., so that they are not inadvertently
         * blocked by a auth.{allow,reject} setting. The trusted volfile is not
         * meant for external users.
         * For unix domain socket, address will be empty.
         */
        if (strlen (addrstr) == 0 || gf_is_local_addr (addrstr)) {

                ret = build_volfile_path (volume, filename,
                                          sizeof (filename),
                                          TRUSTED_PREFIX);
        } else {
                ret = build_volfile_path (volume, filename,
                                          sizeof (filename), NULL);
        }

        if (ret == 0) {
                /* to allocate the proper buffer to hold the file data */
                ret = sys_stat (filename, &stbuf);
                if (ret < 0){
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED,
                                "Unable to stat %s (%s)",
                                filename, strerror (errno));
                        goto fail;
                }

                spec_fd = open (filename, O_RDONLY);
                if (spec_fd < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED,
                                "Unable to open %s (%s)",
                                filename, strerror (errno));
                        goto fail;
                }
                ret = file_len = stbuf.st_size;
        } else {
                op_errno = ENOENT;
                goto fail;
        }

        if (file_len) {
                rsp.spec = CALLOC (file_len+1, sizeof (char));
                if (!rsp.spec) {
                        ret = -1;
                        op_errno = ENOMEM;
                        goto fail;
                }
                ret = sys_read (spec_fd, rsp.spec, file_len);
        }

        if (brick_name) {
                gf_msg_debug (this->name, 0,
                        "Look for missing snap creates for %s", brick_name);
                op_ret = glusterd_take_missing_brick_snapshots (brick_name);
                if (op_ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MISSED_SNAP_CREATE_FAIL,
                                "Failed to take missing brick snapshots");
                        ret = -1;
                        goto fail;
                }
        }

        /* convert to XDR */
fail:
        if (spec_fd > 0)
                sys_close (spec_fd);

        rsp.op_ret   = ret;

        if (op_errno)
                rsp.op_errno = gf_errno_to_error (op_errno);
        if (cookie)
                rsp.op_errno = cookie;

        if (!rsp.spec)
                rsp.spec = strdup ("");

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_getspec_rsp);
        free (args.key);//malloced by xdr
        free (rsp.spec);
        if (args.xdata.xdata_val)
                free (args.xdata.xdata_val);

        return 0;
}

int
server_getspec (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __server_getspec);
}

int32_t
__server_event_notify (rpcsvc_request_t *req)
{
        int32_t                 ret             = -1;
        int32_t                 op_errno        =  0;
        gf_event_notify_req     args            = {0,};
        gf_event_notify_rsp     rsp             = {0,};
        dict_t                 *dict            = NULL;
        gf_boolean_t            need_rsp        = _gf_true;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_event_notify_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        if (args.dict.dict_len) {
                dict = dict_new ();
                if (!dict)
                        return ret;
                ret = dict_unserialize (args.dict.dict_val,
                                        args.dict.dict_len, &dict);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "Failed to unserialize req");
                        goto fail;
                }
        }

        switch (args.op) {
        case GF_EN_DEFRAG_STATUS:
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_DEFRAG_STATUS_UPDATED,
                        "received defrag status updated");
                if (dict) {
                        glusterd_defrag_event_notify_handle (dict);
                        need_rsp = _gf_false;
                }
                break;
        default:
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_OP_UNSUPPORTED, "Unknown op received in event "
                        "notify");
                gf_event (EVENT_NOTIFY_UNKNOWN_OP, "op=%d", args.op);
                ret = -1;
                break;
        }

fail:
        rsp.op_ret   = ret;

        if (op_errno)
                rsp.op_errno = gf_errno_to_error (op_errno);

        if (need_rsp)
                glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                       (xdrproc_t)xdr_gf_event_notify_rsp);
        if (dict)
                dict_unref (dict);
        free (args.dict.dict_val);//malloced by xdr

        return 0;
}

int32_t
server_event_notify (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __server_event_notify);
}

int
gd_validate_cluster_op_version (xlator_t *this, int cluster_op_version,
                                char *peerid)
{
        int              ret  = -1;
        glusterd_conf_t *conf = NULL;

        conf = this->private;

        if (cluster_op_version > GD_OP_VERSION_MAX) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_VERSION_MISMATCH,
                        "operating version %d is more than the maximum "
                        "supported (%d) on the machine (as per peer request "
                        "from %s)", cluster_op_version, GD_OP_VERSION_MAX,
                        peerid);
                goto out;
        }

        /* The peer can only reduce its op-version when it doesn't have any
         * volumes. Reducing op-version when it already contains volumes can
         * lead to inconsistencies in the cluster
         */
        if ((cluster_op_version < conf->op_version) &&
            !cds_list_empty (&conf->volumes)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_VERS_ADJUST_FAIL,
                        "cannot reduce operating version to %d from current "
                        "version %d as volumes exist (as per peer request from "
                        "%s)", cluster_op_version, conf->op_version, peerid);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

/* Validate if glusterd can serve the management handshake request
 *
 * Requests are allowed if,
 *  - glusterd has no peers & no volumes, or
 *  - the request came from a known peer
 * A known peer is identified using the following steps
 *  - the dict is checked for a peer uuid, which if present is matched with the
 *  peer list, else
 *  - the incoming request address is matched with the peer list
 */
gf_boolean_t
gd_validate_mgmt_hndsk_req (rpcsvc_request_t *req, dict_t *dict)
{
        int                  ret                         = -1;
        char                 hostname[UNIX_PATH_MAX + 1] = {0,};
        glusterd_peerinfo_t *peer                        = NULL;
        xlator_t            *this                        = NULL;
        char                *uuid_str                    = NULL;
        uuid_t               peer_uuid                   = {0,};

        this = THIS;
        GF_ASSERT (this);

        if (!glusterd_have_peers () && !glusterd_have_volumes ())
                return _gf_true;

        ret = dict_get_str (dict, GD_PEER_ID_KEY, &uuid_str);
        /* Try to match uuid only if available, don't fail as older peers will
         * not send a uuid
         */
        if (!ret) {
                gf_uuid_parse (uuid_str, peer_uuid);
                rcu_read_lock ();
                ret = (glusterd_peerinfo_find (peer_uuid, NULL) != NULL);
                rcu_read_unlock ();
                if (ret)
                        return _gf_true;
        }

        /* If you cannot get the hostname, you cannot authenticate */
        ret = glusterd_remote_hostname_get (req, hostname, sizeof (hostname));
        if (ret)
                return _gf_false;

        /* If peer object is not found it indicates that request is from an
         * unknown peer, if its found, validate whether its uuid is also
         * available in the peerinfo list. There could be a case where hostname
         * is available in the peerinfo list but the uuid has changed of the
         * node due to a reinstall, in that case the validation should fail!
         */
        rcu_read_lock ();
        peer = glusterd_peerinfo_find (NULL, hostname);
        if (!peer) {
                ret = -1;
        } else if (peer && glusterd_peerinfo_find (peer_uuid, NULL) != NULL) {
                ret = 0;
        } else {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HANDSHAKE_REQ_REJECTED, "Request from peer %s "
                        "has an entry in peerinfo, but uuid does not match",
                        req->trans->peerinfo.identifier);
                ret = -1;
        }
        rcu_read_unlock ();

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HANDSHAKE_REQ_REJECTED, "Rejecting management "
                        "handshake request from unknown peer %s",
                        req->trans->peerinfo.identifier);
                gf_event (EVENT_PEER_REJECT, "peer=%s",
                          req->trans->peerinfo.identifier);
                return _gf_false;
        }

        return _gf_true;
}

int
__glusterd_mgmt_hndsk_versions (rpcsvc_request_t *req)
{
        dict_t            *dict            = NULL;
        xlator_t          *this            = NULL;
        glusterd_conf_t   *conf            = NULL;
        int                ret             = -1;
        int                op_errno        = EINVAL;
        gf_mgmt_hndsk_req  args            = {{0,},};
        gf_mgmt_hndsk_rsp  rsp             = {0,};
        dict_t            *args_dict       = NULL;

        this = THIS;
        conf = this->private;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_mgmt_hndsk_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, args_dict, args.hndsk.hndsk_val,
                                      (args.hndsk.hndsk_len), ret, op_errno,
                                      out);

        /* Check if we can service the request */
        if (!gd_validate_mgmt_hndsk_req (req, args_dict)) {
                ret = -1;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_int32 (dict, GD_OP_VERSION_KEY, conf->op_version);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set operating version");
                rsp.op_ret = ret;
                goto out;
        }

        ret = dict_set_int32 (dict, GD_MIN_OP_VERSION_KEY, GD_OP_VERSION_MIN);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set %s", GD_MIN_OP_VERSION_KEY);
                rsp.op_ret = ret;
                goto out;
        }

        ret = dict_set_int32 (dict, GD_MAX_OP_VERSION_KEY, GD_OP_VERSION_MAX);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "failed to set %s", GD_MAX_OP_VERSION_KEY);
                rsp.op_ret = ret;
                goto out;
        }

        ret = 0;

        GF_PROTOCOL_DICT_SERIALIZE (this, dict, (&rsp.hndsk.hndsk_val),
                                    rsp.hndsk.hndsk_len, op_errno, out);
out:

        rsp.op_ret = ret;
        rsp.op_errno = op_errno;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);

        ret = 0;

        if (dict)
                dict_unref (dict);

        if (args.hndsk.hndsk_val)
                free (args.hndsk.hndsk_val);

        if (rsp.hndsk.hndsk_val)
                GF_FREE (rsp.hndsk.hndsk_val);

        return ret;
}

int
glusterd_mgmt_hndsk_versions (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_mgmt_hndsk_versions);
}

int
__glusterd_mgmt_hndsk_versions_ack (rpcsvc_request_t *req)
{
        dict_t            *clnt_dict       = NULL;
        xlator_t          *this            = NULL;
        glusterd_conf_t   *conf            = NULL;
        int                ret             = -1;
        int                op_errno        = EINVAL;
        int                peer_op_version = 0;
        gf_mgmt_hndsk_req  args            = {{0,},};
        gf_mgmt_hndsk_rsp  rsp             = {0,};

        this = THIS;
        conf = this->private;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gf_mgmt_hndsk_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, clnt_dict, args.hndsk.hndsk_val,
                                      (args.hndsk.hndsk_len), ret, op_errno,
                                      out);

        ret = dict_get_int32 (clnt_dict, GD_OP_VERSION_KEY, &peer_op_version);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_DICT_GET_FAILED,
                        "failed to get the op-version key peer=%s",
                        req->trans->peerinfo.identifier);
                goto out;
        }

        ret = gd_validate_cluster_op_version (this, peer_op_version,
                                              req->trans->peerinfo.identifier);
        if (ret)
                goto out;


        /* As this is ACK from the Cluster for the versions supported,
           can set the op-version of 'this' glusterd to the one
           received. */
        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_VERS_INFO, "using the op-version %d",
                peer_op_version);
        conf->op_version = peer_op_version;
        ret = glusterd_store_global_info (this);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLOBAL_OP_VERSION_SET_FAIL,
                        "Failed to store op-version");

out:
        rsp.op_ret = ret;
        rsp.op_errno = op_errno;

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);

        ret = 0;

        if (clnt_dict)
                dict_unref (clnt_dict);

        if (args.hndsk.hndsk_val)
                free (args.hndsk.hndsk_val);

        return ret;
}

int
glusterd_mgmt_hndsk_versions_ack (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_mgmt_hndsk_versions_ack);
}

int
__server_get_volume_info (rpcsvc_request_t *req)
{
        int                     ret             = -1;
        int32_t                 op_errno        = ENOENT;
        gf_get_volume_info_req  vol_info_req    = {{0,}};
        gf_get_volume_info_rsp  vol_info_rsp    = {0,};
        char                    *volname        = NULL;
        glusterd_volinfo_t      *volinfo        = NULL;
        dict_t                  *dict           = NULL;
        dict_t                  *dict_rsp       = NULL;
        char                    *volume_id_str  = NULL;
        int32_t                 flags           = 0;

        ret = xdr_to_generic (req->msg[0], &vol_info_req,
                             (xdrproc_t)xdr_gf_get_volume_info_req);
        if (ret < 0) {
                /* failed to decode msg */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        gf_msg ("glusterd", GF_LOG_INFO, 0,
                GD_MSG_VOL_INFO_REQ_RECVD, "Received get volume info req");

        if (vol_info_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();
                if (!dict) {
                        gf_msg ("glusterd", GF_LOG_WARNING, ENOMEM,
                                GD_MSG_NO_MEMORY, "Out of Memory");
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (vol_info_req.dict.dict_val,
                                        vol_info_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        op_errno = -ret;
                        ret = -1;
                        goto out;
                } else {
                        dict->extra_stdfree = vol_info_req.dict.dict_val;
                }
        }

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, -ret,
                        GD_MSG_DICT_GET_FAILED, "failed to get flags");
                op_errno = -ret;
                ret = -1;
                goto out;
        }

        if (!flags) {
                /* Nothing to query about. Just return success */
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_NO_FLAG_SET, "No flags set");
                ret = 0;
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                op_errno = EINVAL;
                ret = -1;
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                op_errno = EINVAL;
                ret = -1;
                goto out;
        }

        if (flags | (int32_t)GF_GET_VOLUME_UUID) {
                volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
                if (!volume_id_str) {
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }

                dict_rsp = dict_new ();
                if (!dict_rsp) {
                        gf_msg ("glusterd", GF_LOG_WARNING, ENOMEM,
                                GD_MSG_NO_MEMORY, "Out of Memory");
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dict_rsp, "volume_id", volume_id_str);
                if (ret) {
                        op_errno = -ret;
                        ret = -1;
                        goto out;
                }
        }
        ret = dict_allocate_and_serialize (dict_rsp, &vol_info_rsp.dict.dict_val,
                                           &vol_info_rsp.dict.dict_len);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }

out:
        vol_info_rsp.op_ret = ret;
        vol_info_rsp.op_errno = op_errno;
        vol_info_rsp.op_errstr = "";
        glusterd_submit_reply (req, &vol_info_rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_get_volume_info_rsp);
        ret = 0;

        if (dict) {
                dict_unref (dict);
        }

        if (dict_rsp) {
                dict_unref (dict_rsp);
        }

        if (vol_info_rsp.dict.dict_val) {
                GF_FREE (vol_info_rsp.dict.dict_val);
        }
        return ret;
}

int
server_get_volume_info (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __server_get_volume_info);
}


/*
 * glusterd function to get the list of snapshot names and uuids
 */
int
__server_get_snap_info (rpcsvc_request_t *req)
{
        int                             ret             = -1;
        int                             op_errno        = ENOENT;
        gf_getsnap_name_uuid_req        snap_info_req   = {{0,}};
        gf_getsnap_name_uuid_rsp        snap_info_rsp   = {0,};
        dict_t                          *dict           = NULL;
        dict_t                          *dict_rsp       = NULL;
        char                            *volname        = NULL;

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &snap_info_req,
                              (xdrproc_t)xdr_gf_getsnap_name_uuid_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL,
                        "Failed to decode management handshake response");
                goto out;
        }

        if (snap_info_req.dict.dict_len) {
                dict = dict_new ();
                if (!dict) {
                        op_errno = ENOMEM;
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (snap_info_req.dict.dict_val,
                                        snap_info_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "Failed to unserialize dictionary");
                        op_errno = EINVAL;
                        ret = -1;
                        goto out;
                } else {
                        dict->extra_stdfree = snap_info_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                op_errno = EINVAL;
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_DICT_GET_FAILED,
                        "Failed to retrieve volname");
                ret = -1;
                goto out;
        }

        dict_rsp = dict_new ();
        if (!dict_rsp) {
                op_errno = ENOMEM;
                ret = -1;
                goto out;
        }

        ret = glusterd_snapshot_get_volnames_uuids (dict_rsp, volname,
                                                    &snap_info_rsp);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND,
                        "Error getting snapshot volume names and uuids : %s",
                        volname);
                op_errno = EINVAL;
        }

out:
        snap_info_rsp.op_ret = ret;
        snap_info_rsp.op_errno = op_errno;
        snap_info_rsp.op_errstr = "";
        glusterd_submit_reply (req, &snap_info_rsp, NULL, 0, NULL,
                               (xdrproc_t)xdr_gf_getsnap_name_uuid_rsp);

        if (dict) {
                dict_unref (dict);
        }

        if (dict_rsp) {
                dict_unref (dict_rsp);
        }

        if (snap_info_rsp.dict.dict_val) {
                GF_FREE (snap_info_rsp.dict.dict_val);
        }

        return 0;
}

int
server_get_snap_info (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __server_get_snap_info);
}

rpcsvc_actor_t gluster_handshake_actors[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = {"NULL",        GF_HNDSK_NULL,         NULL,                NULL, 0, DRC_NA},
        [GF_HNDSK_GETSPEC]      = {"GETSPEC",     GF_HNDSK_GETSPEC,      server_getspec,      NULL, 0, DRC_NA},
        [GF_HNDSK_EVENT_NOTIFY] = {"EVENTNOTIFY", GF_HNDSK_EVENT_NOTIFY, server_event_notify, NULL, 0, DRC_NA},
        [GF_HNDSK_GET_VOLUME_INFO] = {"GETVOLUMEINFO", GF_HNDSK_GET_VOLUME_INFO, server_get_volume_info, NULL, 0, DRC_NA},
        [GF_HNDSK_GET_SNAPSHOT_INFO] = {"GETSNAPINFO", GF_HNDSK_GET_SNAPSHOT_INFO, server_get_snap_info, NULL, 0, DRC_NA},
};


struct rpcsvc_program gluster_handshake_prog = {
        .progname  = "Gluster Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_handshake_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};

/* A minimal RPC program just for the cli getspec command */
rpcsvc_actor_t gluster_cli_getspec_actors[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_GETSPEC]      = {"GETSPEC",     GF_HNDSK_GETSPEC,      server_getspec,      NULL, 0, DRC_NA},
};

struct rpcsvc_program gluster_cli_getspec_prog = {
        .progname  = "Gluster Handshake (CLI Getspec)",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_cli_getspec_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};


char *glusterd_dump_proc[GF_DUMP_MAXVALUE] = {
        [GF_DUMP_NULL] = "NULL",
        [GF_DUMP_DUMP] = "DUMP",
        [GF_DUMP_PING] = "PING",
};

rpc_clnt_prog_t glusterd_dump_prog = {
        .progname       = "GLUSTERD-DUMP",
        .prognum        = GLUSTER_DUMP_PROGRAM,
        .progver        = GLUSTER_DUMP_VERSION,
        .procnames      = glusterd_dump_proc,
};


rpcsvc_actor_t glusterd_mgmt_hndsk_actors[GD_MGMT_HNDSK_MAXVALUE] = {
        [GD_MGMT_HNDSK_NULL]            = {"NULL", GD_MGMT_HNDSK_NULL, NULL,
                                           NULL, 0, DRC_NA},
        [GD_MGMT_HNDSK_VERSIONS]        = {"MGMT-VERS", GD_MGMT_HNDSK_VERSIONS,
                                           glusterd_mgmt_hndsk_versions, NULL,
                                           0, DRC_NA},
        [GD_MGMT_HNDSK_VERSIONS_ACK]    = {"MGMT-VERS-ACK",
                                           GD_MGMT_HNDSK_VERSIONS_ACK,
                                           glusterd_mgmt_hndsk_versions_ack,
                                           NULL, 0, DRC_NA},
};

struct rpcsvc_program glusterd_mgmt_hndsk_prog = {
        .progname       = "Gluster MGMT Handshake",
        .prognum        = GD_MGMT_HNDSK_PROGRAM,
        .progver        = GD_MGMT_HNDSK_VERSION,
        .actors         = glusterd_mgmt_hndsk_actors,
        .numactors      = GD_MGMT_HNDSK_MAXVALUE,
};

char *glusterd_mgmt_hndsk_proc[GD_MGMT_HNDSK_MAXVALUE] = {
        [GD_MGMT_HNDSK_NULL]          = "NULL",
        [GD_MGMT_HNDSK_VERSIONS] = "MGMT-VERS",
        [GD_MGMT_HNDSK_VERSIONS_ACK] = "MGMT-VERS-ACK",
};

rpc_clnt_prog_t gd_clnt_mgmt_hndsk_prog = {
        .progname  = "Gluster MGMT Handshake",
        .prognum   = GD_MGMT_HNDSK_PROGRAM,
        .progver   = GD_MGMT_HNDSK_VERSION,
        .procnames = glusterd_mgmt_hndsk_proc,
};


static int
glusterd_event_connected_inject (glusterd_peerctx_t *peerctx)
{
        GF_ASSERT (peerctx);

        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;


        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_CONNECTED, &event);

        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_NEW_GET_FAIL, "Unable to get new event");
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Memory not available");
                goto out;
        }

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                ret = -1;
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_PEER_NOT_FOUND, "Could not find peer %s(%s)",
                        peerctx->peername, uuid_utoa (peerctx->peerid));
                goto unlock;
        }
        ctx->hostname = gf_strdup (peerinfo->hostname);
        ctx->port = peerinfo->port;
        ctx->req = peerctx->args.req;
        ctx->dict = peerctx->args.dict;

        event->peername = gf_strdup (peerinfo->hostname);
        gf_uuid_copy (event->peerid, peerinfo->uuid);
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret)
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_EVENT_INJECT_FAIL, "Unable to inject "
                        "EVENT_CONNECTED ret = %d", ret);
unlock:
        rcu_read_unlock ();

out:
        gf_msg_debug ("glusterd", 0, "returning %d", ret);
        return ret;
}


int
gd_validate_peer_op_version (xlator_t *this, glusterd_peerinfo_t *peerinfo,
                             dict_t *dict, char **errstr)
{
        int              ret  = -1;
        glusterd_conf_t *conf = NULL;
        int32_t          peer_op_version = 0;
        int32_t          peer_min_op_version = 0;
        int32_t          peer_max_op_version = 0;

        if (!dict && !this && !peerinfo)
                goto out;

        conf = this->private;

        ret = dict_get_int32 (dict, GD_OP_VERSION_KEY, &peer_op_version);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, GD_MAX_OP_VERSION_KEY,
                              &peer_max_op_version);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, GD_MIN_OP_VERSION_KEY,
                              &peer_min_op_version);
        if (ret)
                goto out;

        ret = -1;
        /* Check if peer can support our op_version */
        if ((peer_max_op_version < conf->op_version) ||
            (peer_min_op_version > conf->op_version)) {
                ret = gf_asprintf (errstr, "Peer %s does not support required "
                                   "op-version", peerinfo->hostname);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        gf_msg_debug (this->name , 0, "Peer %s %s", peerinfo->hostname,
                ((ret < 0) ? "rejected" : "accepted"));
        return ret;
}

int
__glusterd_mgmt_hndsk_version_ack_cbk (struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
        int                  ret      = -1;
        gf_mgmt_hndsk_rsp    rsp      = {0,};
        xlator_t            *this     = NULL;
        call_frame_t        *frame    = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;
        glusterd_peerctx_t  *peerctx  = NULL;
        char                msg[1024] = {0,};

        this = THIS;
        frame = myframe;
        peerctx = frame->local;

        rcu_read_lock ();
        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                gf_msg_debug (this->name, 0, "Could not find peer %s(%s)",
                        peerctx->peername, uuid_utoa (peerctx->peerid));
                ret = -1;
                goto out;
        }

        if (-1 == req->rpc_status) {
                snprintf (msg, sizeof (msg),
                          "Error through RPC layer, retry again later");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_LAYER_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);
        if (ret < 0) {
                snprintf (msg, sizeof (msg), "Failed to decode XDR");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        if (-1 == rsp.op_ret) {
                ret = -1;
                snprintf (msg, sizeof (msg),
                          "Failed to get handshake ack from remote server");
                gf_msg (frame->this->name, GF_LOG_ERROR, 0,
                        GD_MSG_NO_HANDSHAKE_ACK, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        /* TODO: this is hardcoded as of now, but I don't forsee any problems
         * with this as long as we are properly handshaking operating versions
         */
        peerinfo->mgmt = &gd_mgmt_prog;
        peerinfo->peer = &gd_peer_prog;
        peerinfo->mgmt_v3 = &gd_mgmt_v3_prog;

        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

        if (GD_MODE_ON == peerctx->args.mode) {
                ret = glusterd_event_connected_inject (peerctx);
                peerctx->args.req = NULL;
        } else if (GD_MODE_SWITCH_ON == peerctx->args.mode) {
                peerctx->args.mode = GD_MODE_ON;
        } else {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_UNKNOWN_MODE, "unknown mode %d",
                        peerctx->args.mode);
        }

        ret = 0;
out:

        if (ret != 0 && peerinfo)
                rpc_transport_disconnect (peerinfo->rpc->conn.trans, _gf_false);

        rcu_read_unlock ();

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        if (rsp.hndsk.hndsk_val)
                free (rsp.hndsk.hndsk_val);

        glusterd_friend_sm ();

        return 0;
}

int
glusterd_mgmt_hndsk_version_ack_cbk (struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_mgmt_hndsk_version_ack_cbk);
}

int
__glusterd_mgmt_hndsk_version_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        int                  ret       = -1;
        int                  op_errno  = EINVAL;
        gf_mgmt_hndsk_rsp    rsp       = {0,};
        gf_mgmt_hndsk_req    arg       = {{0,}};
        xlator_t            *this      = NULL;
        call_frame_t        *frame     = NULL;
        glusterd_peerinfo_t *peerinfo  = NULL;
        glusterd_peerctx_t  *peerctx   = NULL;
        dict_t              *dict      = NULL;
        dict_t              *rsp_dict  = NULL;
        glusterd_conf_t     *conf      = NULL;
        char                 msg[1024] = {0,};

        this = THIS;
        conf = this->private;
        frame = myframe;
        peerctx = frame->local;

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                ret = -1;
                gf_msg_debug (this->name, 0, "Could not find peer %s(%s)",
                        peerctx->peername, uuid_utoa (peerctx->peerid));
                goto out;
        }

        if (-1 == req->rpc_status) {
                ret = -1;
                snprintf (msg, sizeof (msg),
                          "Error through RPC layer, retry again later");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_LAYER_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_mgmt_hndsk_rsp);
        if (ret < 0) {
                snprintf (msg, sizeof (msg), "Failed to decode management "
                          "handshake response");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        GF_PROTOCOL_DICT_UNSERIALIZE (this, dict, rsp.hndsk.hndsk_val,
                                      rsp.hndsk.hndsk_len, ret, op_errno,
                                      out);

        op_errno = rsp.op_errno;
        if (-1 == rsp.op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        GD_MSG_VERS_GET_FAIL,
                        "failed to get the 'versions' from peer (%s)",
                        req->conn->trans->peerinfo.identifier);
                goto out;
        }

        /* Check if peer can be part of cluster */
        ret = gd_validate_peer_op_version (this, peerinfo, dict,
                                           &peerctx->errstr);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_VERSION_MISMATCH,
                        "failed to validate the operating version of peer (%s)",
                        peerinfo->hostname);
                goto out;
        }

        rsp_dict = dict_new ();
        if (!rsp_dict)
                goto out;

        ret = dict_set_int32 (rsp_dict, GD_OP_VERSION_KEY, conf->op_version);
        if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_DICT_SET_FAILED,
                       "failed to set operating version in dict");
                goto out;
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, rsp_dict, (&arg.hndsk.hndsk_val),
                                    arg.hndsk.hndsk_len, op_errno, out);

        ret = glusterd_submit_request (peerinfo->rpc, &arg, frame,
                                       &gd_clnt_mgmt_hndsk_prog,
                                       GD_MGMT_HNDSK_VERSIONS_ACK, NULL, this,
                                       glusterd_mgmt_hndsk_version_ack_cbk,
                                       (xdrproc_t)xdr_gf_mgmt_hndsk_req);

out:
        if (ret) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
                if (peerinfo)
                        rpc_transport_disconnect (peerinfo->rpc->conn.trans,
                                                  _gf_false);
        }

        rcu_read_unlock ();

        if (rsp.hndsk.hndsk_val)
                free (rsp.hndsk.hndsk_val);

        if (arg.hndsk.hndsk_val)
                GF_FREE (arg.hndsk.hndsk_val);

        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return 0;
}

int
glusterd_mgmt_hndsk_version_cbk (struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_mgmt_hndsk_version_cbk);
}

int
glusterd_mgmt_handshake (xlator_t *this, glusterd_peerctx_t *peerctx)
{
        call_frame_t        *frame    = NULL;
        gf_mgmt_hndsk_req    req      = {{0,},};
        glusterd_peerinfo_t *peerinfo = NULL;
        dict_t              *req_dict = NULL;
        int                  ret      = -1;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        frame->local = peerctx;

        req_dict = dict_new ();
        if (!req_dict)
                goto out;

        ret = dict_set_dynstr (req_dict, GD_PEER_ID_KEY,
                               gf_strdup (uuid_utoa (MY_UUID)));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                       "failed to set peer ID in dict");
                goto out;
        }

        GF_PROTOCOL_DICT_SERIALIZE (this, req_dict, (&req.hndsk.hndsk_val),
                                    req.hndsk.hndsk_len, ret, out);

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                gf_msg_debug (THIS->name, 0, "Could not find peer %s(%s)",
                        peerctx->peername, uuid_utoa (peerctx->peerid));
                goto unlock;
        }

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame,
                                       &gd_clnt_mgmt_hndsk_prog,
                                       GD_MGMT_HNDSK_VERSIONS, NULL, this,
                                       glusterd_mgmt_hndsk_version_cbk,
                                       (xdrproc_t)xdr_gf_mgmt_hndsk_req);
        ret = 0;
unlock:
        rcu_read_unlock ();
out:
        if (ret && frame)
                STACK_DESTROY (frame->root);

        return ret;
}

int
glusterd_set_clnt_mgmt_program (glusterd_peerinfo_t *peerinfo,
                                gf_prog_detail *prog)
{
        gf_prog_detail  *trav   = NULL;
        int             ret     = -1;

        if (!peerinfo || !prog)
                goto out;

        trav = prog;

        while (trav) {
                ret = -1;
                if ((gd_mgmt_prog.prognum == trav->prognum) &&
                    (gd_mgmt_prog.progver == trav->progver)) {
                        peerinfo->mgmt = &gd_mgmt_prog;
                        ret = 0;
                }

                if ((gd_peer_prog.prognum == trav->prognum) &&
                    (gd_peer_prog.progver == trav->progver)) {
                        peerinfo->peer = &gd_peer_prog;
                        ret = 0;
                }

                if (ret) {
                        gf_msg_debug ("glusterd", 0,
                                "%s (%"PRId64":%"PRId64") not supported",
                                trav->progname, trav->prognum,
                                trav->progver);
                }

                trav = trav->next;
        }

        if (peerinfo->mgmt) {
                 gf_msg ("glusterd", GF_LOG_INFO, 0,
                         GD_MSG_VERS_INFO,
                         "Using Program %s, Num (%d), Version (%d)",
                         peerinfo->mgmt->progname, peerinfo->mgmt->prognum,
                         peerinfo->mgmt->progver);
        }

        if (peerinfo->peer) {
                 gf_msg ("glusterd", GF_LOG_INFO, 0,
                         GD_MSG_VERS_INFO,
                         "Using Program %s, Num (%d), Version (%d)",
                         peerinfo->peer->progname, peerinfo->peer->prognum,
                         peerinfo->peer->progver);
        }

        if (peerinfo->mgmt_v3) {
                 gf_msg ("glusterd", GF_LOG_INFO, 0,
                         GD_MSG_VERS_INFO,
                         "Using Program %s, Num (%d), Version (%d)",
                         peerinfo->mgmt_v3->progname,
                         peerinfo->mgmt_v3->prognum,
                         peerinfo->mgmt_v3->progver);
        }

        ret = 0;
out:
        return ret;

}

static gf_boolean_t
_mgmt_hndsk_prog_present (gf_prog_detail *prog) {
        gf_boolean_t    ret = _gf_false;
        gf_prog_detail  *trav = NULL;

        GF_ASSERT (prog);

        trav = prog;

        while (trav) {
                if ((trav->prognum == GD_MGMT_HNDSK_PROGRAM) &&
                    (trav->progver == GD_MGMT_HNDSK_VERSION)) {
                        ret = _gf_true;
                        goto out;
                }
                trav = trav->next;
        }
out:
        return ret;
}

int
__glusterd_peer_dump_version_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        int                  ret      = -1;
        gf_dump_rsp          rsp      = {0,};
        xlator_t            *this     = NULL;
        gf_prog_detail      *trav     = NULL;
        gf_prog_detail      *next     = NULL;
        call_frame_t        *frame    = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;
        glusterd_peerctx_t  *peerctx  = NULL;
        glusterd_conf_t     *conf     = NULL;
        char                msg[1024] = {0,};

        this = THIS;
        conf = this->private;
        frame = myframe;
        peerctx = frame->local;

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                gf_msg_debug (this->name, 0, "Couldn't find peer %s(%s)",
                        peerctx->peername, uuid_utoa (peerctx->peerid));
                goto out;
        }

        if (-1 == req->rpc_status) {
                snprintf (msg, sizeof (msg),
                          "Error through RPC layer, retry again later");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RPC_LAYER_ERROR, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_dump_rsp);
        if (ret < 0) {
                snprintf (msg, sizeof (msg), "Failed to decode XDR");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }
        if (-1 == rsp.op_ret) {
                snprintf (msg, sizeof (msg),
                          "Failed to get the 'versions' from remote server");
                gf_msg (frame->this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VERS_GET_FAIL, "%s", msg);
                peerctx->errstr = gf_strdup (msg);
                goto out;
        }

        if (_mgmt_hndsk_prog_present (rsp.prog)) {
                gf_msg_debug (this->name, 0,
                        "Proceeding to op-version handshake with peer %s",
                        peerinfo->hostname);
                ret = glusterd_mgmt_handshake (this, peerctx);
                goto out;
        } else if (conf->op_version > 1) {
                ret = -1;
                snprintf (msg, sizeof (msg),
                          "Peer %s does not support required op-version",
                          peerinfo->hostname);
                peerctx->errstr = gf_strdup (msg);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VERSION_UNSUPPORTED, "%s", msg);
                goto out;
        }

        /* Make sure we assign the proper program to peer */
        ret = glusterd_set_clnt_mgmt_program (peerinfo, rsp.prog);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_MGMT_PGM_SET_FAIL,
                        "failed to set the mgmt program");
                goto out;
        }

        ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

        if (GD_MODE_ON == peerctx->args.mode) {
                ret = glusterd_event_connected_inject (peerctx);
                peerctx->args.req = NULL;
        } else if (GD_MODE_SWITCH_ON == peerctx->args.mode) {
                peerctx->args.mode = GD_MODE_ON;
        } else {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_UNKNOWN_MODE, "unknown mode %d",
                        peerctx->args.mode);
        }

        ret = 0;

out:
        if (ret != 0 && peerinfo)
                rpc_transport_disconnect (peerinfo->rpc->conn.trans, _gf_false);

        rcu_read_unlock ();

        glusterd_friend_sm ();
        glusterd_op_sm ();

        /* don't use GF_FREE, buffer was allocated by libc */
        if (rsp.prog) {
                trav = rsp.prog;
                while (trav) {
                        next = trav->next;
                        free (trav->progname);
                        free (trav);
                        trav = next;
                }
        }

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        return 0;
}


int
glusterd_peer_dump_version_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        __glusterd_peer_dump_version_cbk);
}

int
glusterd_peer_dump_version (xlator_t *this, struct rpc_clnt *rpc,
                            glusterd_peerctx_t *peerctx)
{
        call_frame_t        *frame    = NULL;
        gf_dump_req          req      = {0,};
        glusterd_peerinfo_t *peerinfo = NULL;
        int                  ret      = -1;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        frame->local = peerctx;
        if (!peerctx)
                goto out;

        rcu_read_lock ();

        peerinfo = glusterd_peerinfo_find_by_generation (peerctx->peerinfo_gen);
        if (!peerinfo) {
                gf_msg_debug (this->name, 0, "Couldn't find peer %s(%s)",
                        peerctx->peername, uuid_utoa (peerctx->peerid));
                goto unlock;
        }

        req.gfs_id = 0xcafe;

        ret = glusterd_submit_request (peerinfo->rpc, &req, frame,
                                       &glusterd_dump_prog, GF_DUMP_DUMP,
                                       NULL, this,
                                       glusterd_peer_dump_version_cbk,
                                       (xdrproc_t)xdr_gf_dump_req);
unlock:
        rcu_read_unlock ();
out:
        if (ret && frame)
                STACK_DESTROY (frame->root);

        return ret;
}
