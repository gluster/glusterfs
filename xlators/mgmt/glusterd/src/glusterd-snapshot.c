/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <signal.h>


#if !defined(__NetBSD__) && !defined(GF_DARWIN_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif

#include "globals.h"
#include "compat.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "run.h"
#include "glusterd-volgen.h"
#include "glusterd-mgmt.h"
#include "glusterd-syncop.h"

#include "syscall.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"

#include "lvm-defaults.h"

char snap_mount_folder[PATH_MAX];

static int32_t
glusterd_find_missed_snap (dict_t *rsp_dict, glusterd_volinfo_t *vol,
                           char *snap_uuid, struct list_head *peers,
                           int32_t op);

/* This function will restore a snapshot volumes
 *
 * @param dict          dictionary containing snapshot restore request
 * @param op_errstr     In case of any failure error message will be returned
 *                      in this variable
 * @return              Negative value on Failure and 0 in success
 */
int
glusterd_snapshot_restore (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int                     ret             = -1;
        char                    *volname        = NULL;
        char                    *snapname       = NULL;
        xlator_t                *this           = NULL;
        glusterd_volinfo_t      *snap_volinfo   = NULL;
        glusterd_volinfo_t      *volinfo        = NULL;
        glusterd_snap_t         *snap           = NULL;
        glusterd_conf_t         *priv           = NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                        "snap name");
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (NULL == snap) {
                ret = gf_asprintf (op_errstr, "Snap (%s) not found",
                                        snapname);
                if (ret < 0) {
                        goto out;
                }
                gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                ret = -1;
                goto out;
        }

        /* TODO : As of now there is only volume in snapshot.
         * Change this when multiple volume snapshot is introduced
         */
        snap_volinfo = list_entry (snap->volumes.next, glusterd_volinfo_t,
                                   vol_list);

        ret = glusterd_volinfo_find (snap_volinfo->parent_volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not get volinfo of "
                        "%s", snap_volinfo->parent_volname);
                goto out;
        }

        if (is_origin_glusterd (dict) == _gf_true) {
                /* From origin glusterd check if      *
                 * any peers with snap bricks is down */
                ret = glusterd_find_missed_snap (rsp_dict, snap_volinfo,
                                                 snap_volinfo->volname,
                                                 &priv->peers,
                                                 GF_SNAP_OPTION_TYPE_RESTORE);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to find missed snap restores");
                        goto out;
                }
        }

        ret = gd_restore_snap_volume (rsp_dict, volinfo, snap_volinfo);
        if (ret) {
                /* No need to update op_errstr because it is assumed
                 * that the called function will do that in case of
                 * failure.
                 */
                gf_log (this->name, GF_LOG_ERROR, "Failed to restore "
                        "snap for %s volume", volname);
                goto out;
        }

        ret = 0;

        /* TODO: Need to check if we need to delete the snap after the
         * operation is successful or not. Also need to persist the state
         * of restore operation in the store.
         */
out:
        return ret;
}

/* This function is called before actual restore is taken place. This function
 * will validate whether the snapshot volumes are ready to be restored or not.
 *
 * @param dict          dictionary containing snapshot restore request
 * @param op_errstr     In case of any failure error message will be returned
 *                      in this variable
 * @param rsp_dict      response dictionary
 * @return              Negative value on Failure and 0 in success
 */
int
glusterd_snapshot_restore_prevalidate (dict_t *dict, char **op_errstr,
                                       dict_t *rsp_dict)
{
        int                     ret             = -1;
        int32_t                 i               = 0;
        int32_t                 volcount        = 0;
        gf_boolean_t            snap_restored   = _gf_false;
        char                    key[PATH_MAX]   = {0, };
        char                    *volname        = NULL;
        char                    *snapname       = NULL;
        glusterd_volinfo_t      *volinfo        = NULL;
        glusterd_snap_t         *snap           = NULL;
        xlator_t                *this           = NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                        "snap name");
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (NULL == snap) {
                ret = gf_asprintf (op_errstr, "Snap (%s) not found",
                                snapname);
                if (ret < 0) {
                        goto out;
                }
                gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                ret = -1;
                goto out;
        }

        snap_restored = snap->snap_restored;

        if (snap_restored) {
                ret = gf_asprintf (op_errstr, "Snap (%s) is already "
                                  "restored", snapname);
                if (ret < 0) {
                        goto out;
                }
                gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                ret = -1;
                goto out;
        }

        ret = dict_set_str (rsp_dict, "snapname", snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "snap name(%s)", snapname);
                goto out;
        }

        ret = dict_get_int32 (dict, "volcount", &volcount);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get volume count");
                goto out;
        }

        /* Snapshot restore will only work if all the volumes,
           that are part of the snapshot, are stopped. */
        for (i = 1; i <= volcount; ++i) {
                snprintf (key, sizeof (key), "volname%d", i);
                ret = dict_get_str (dict, key, &volname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "get volume name");
                        goto out;
                }

                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        ret = gf_asprintf (op_errstr, "Volume (%s) not found",
                                           volname);
                        if (ret < 0) {
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                        ret = -1;
                        goto out;
                }

                if (glusterd_is_volume_started (volinfo)) {
                        ret = gf_asprintf (op_errstr, "Volume (%s) has been "
                        "started. Volume needs to be stopped before restoring "
                        "a snapshot.", volname);
                        if (ret < 0) {
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int
snap_max_hard_limits_validate (dict_t *dict, char *volname,
                               uint64_t value, char **op_errstr)
{
        char                err_str[PATH_MAX] = "";
        glusterd_conf_t    *conf              = NULL;
        glusterd_volinfo_t *volinfo           = NULL;
        int                 ret               = -1;
        uint64_t            max_limit         = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
        xlator_t           *this              = NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        conf = this->private;

        GF_ASSERT (conf);

        if (volname) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (!ret) {
                        if (volinfo->is_snap_volume) {
                                ret = -1;
                                snprintf (err_str, PATH_MAX,
                                         "%s is a snap volume. Configuring "
                                         "snap-max-hard-limit for a snap "
                                         "volume is prohibited.", volname);
                                goto out;
                        }
                }
        }

        if (value) {
                /* Max limit for the system is GLUSTERD_SNAPS_MAX_HARD_LIMIT
                 * but max limit for a volume is conf->snap_max_hard_limit.
                 */
                if (volname) {
                        max_limit = conf->snap_max_hard_limit;
                } else {
                        max_limit = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
                }
        }

        if ((value < 0) || (value > max_limit)) {
                ret = -1;
                snprintf (err_str, PATH_MAX, "Invalid snap-max-hard-limit"
                          "%"PRIu64 ". Expected range 0 - %"PRIu64,
                          value, max_limit);
                goto out;
        }

        ret = 0;
out:
        if (ret) {
                *op_errstr = gf_strdup (err_str);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
        }
        return ret;
}

int
glusterd_snapshot_config_prevalidate (dict_t *dict, char **op_errstr)
{
        char               *volname             = NULL;
        glusterd_volinfo_t *volinfo             = NULL;
        xlator_t           *this                = NULL;
        int                 ret                 = -1;
        int                 config_command      = 0;
        char                err_str[PATH_MAX]   = {0,};
        glusterd_conf_t    *conf                = NULL;
        uint64_t            value               = 0;
        uint64_t            hard_limit          = 0;
        uint64_t            soft_limit          = 0;
        gf_loglevel_t       loglevel            = GF_LOG_ERROR;
        uint64_t            max_limit           = GLUSTERD_SNAPS_MAX_HARD_LIMIT;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        conf = this->private;

        GF_ASSERT (conf);

        ret = dict_get_int32 (dict, "config-command", &config_command);
        if (ret) {
                snprintf (err_str, sizeof (err_str),
                          "failed to get config-command type");
                goto out;
        }

        ret = dict_get_uint64 (dict, "snap-max-hard-limit", &hard_limit);

        ret = dict_get_uint64 (dict, "snap-max-soft-limit", &soft_limit);

        ret = dict_get_str (dict, "volname", &volname);

        if (volname) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        snprintf (err_str, sizeof (err_str),
                                  "Volume %s does not exist.", volname);
                        goto out;
                }
        }

        switch (config_command) {
        case GF_SNAP_CONFIG_TYPE_SET:
                if (hard_limit) {
                        /* Validations for snap-max-hard-limits */
                        ret = snap_max_hard_limits_validate (dict, volname,
                                                         hard_limit, op_errstr);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "snap-max-hard-limit validation "
                                        "failed.");
                                goto out;
                        }
                }

                if (soft_limit) {
                        max_limit = GLUSTERD_SNAPS_MAX_SOFT_LIMIT_PERCENT;
                        if ((soft_limit < 0) || (soft_limit > max_limit)) {
                                ret = -1;
                                snprintf (err_str, PATH_MAX, "Invalid "
                                         "snap-max-soft-limit ""%"
                                         PRIu64 ". Expected range 0 - %"PRIu64,
                                         value, max_limit);
                                goto out;
                        }
                        break;
                }
        default:
                break;
        }

        ret = 0;
out:

        if (ret && err_str[0] != '\0') {
                gf_log (this->name, loglevel, "%s", err_str);
                *op_errstr = gf_strdup (err_str);
        }

        return ret;
}

int
glusterd_snap_create_pre_val_use_rsp_dict (dict_t *dst, dict_t *src)
{
        char                  *snap_brick_dir        = NULL;
        char                  *snap_device           = NULL;
        char                  *tmpstr                = NULL;
        char                   key[PATH_MAX]         = "";
        char                   snapbrckcnt[PATH_MAX] = "";
        char                   snapbrckord[PATH_MAX] = "";
        int                    ret                   = -1;
        int64_t                i                     = -1;
        int64_t                j                     = -1;
        int64_t                volume_count          = 0;
        int64_t                brick_count           = 0;
        int64_t                brick_order           = 0;
        xlator_t              *this                  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dst);
        GF_ASSERT (src);

        ret = dict_get_int64 (src, "volcount", &volume_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "get the volume count");
                goto out;
        }

        for (i = 0; i < volume_count; i++) {
                memset (snapbrckcnt, '\0', sizeof(snapbrckcnt));
                ret = snprintf (snapbrckcnt, sizeof(snapbrckcnt) - 1,
                                "vol%"PRId64"_brickcount", i+1);
                ret = dict_get_int64 (src, snapbrckcnt, &brick_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "No bricks for this volume in this dict");
                        continue;
                }

                for (j = 0; j < brick_count; j++) {
                        /* Fetching data from source dict */
                        snprintf (key, sizeof(key) - 1,
                                  "vol%"PRId64".brickdir%"PRId64, i+1, j);

                        ret = dict_get_ptr (src, key,
                                            (void **)&snap_brick_dir);
                        if (ret) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Unable to fetch %s", key);
                                continue;
                        }

                        snprintf (key, sizeof(key) - 1,
                                  "vol%"PRId64".brick_snapdevice%"PRId64, i+1, j);

                        ret = dict_get_ptr (src, key,
                                            (void **)&snap_device);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unable to fetch snap_device");
                                goto out;
                        }

                        snprintf (snapbrckord, sizeof(snapbrckord) - 1,
                                  "vol%"PRId64".brick%"PRId64".order", i+1, j);

                        ret = dict_get_int64 (src, snapbrckord, &brick_order);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get brick order");
                                goto out;
                        }

                        /* Adding the data in the dst dict */
                        snprintf (key, sizeof(key) - 1,
                                  "vol%"PRId64".brickdir%"PRId64, i+1, brick_order);

                        tmpstr = gf_strdup (snap_brick_dir);
                        if (!tmpstr) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out Of Memory");
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_dynstr (dst, key, tmpstr);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set %s", key);
                                GF_FREE (tmpstr);
                                goto out;
                        }

                        snprintf (key, sizeof(key) - 1,
                                  "vol%"PRId64".brick_snapdevice%"PRId64,
                                  i+1, brick_order);

                        tmpstr = gf_strdup (snap_device);
                        if (!tmpstr) {
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_dynstr (dst, key, tmpstr);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set %s", key);
                                GF_FREE (tmpstr);
                                goto out;
                        }
                }
        }

        ret = 0;
out:

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_snap_pre_validate_use_rsp_dict (dict_t *dst, dict_t *src)
{
        int             ret             = -1;
        int32_t         snap_command    = 0;
        xlator_t       *this            = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (!dst || !src) {
                gf_log (this->name, GF_LOG_ERROR, "Source or Destination "
                        "dict is empty.");
                goto out;
        }

        ret = dict_get_int32 (dst, "type", &snap_command);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                ret = glusterd_snap_create_pre_val_use_rsp_dict (dst, src);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to use "
                                "rsp dict");
                        goto out;
                }
                break;
        default:
                break;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_snapshot_create_prevalidate (dict_t *dict, char **op_errstr,
                                      dict_t *rsp_dict)
{
        char                  *volname                   = NULL;
        char                  *snapname                  = NULL;
        char                  *device                    = NULL;
        char                  *tmpstr                    = NULL;
        char                  *brick_dir                 = NULL;
        char                   snap_brick_dir[PATH_MAX]  = "";
        char                  *mnt_pt                    = NULL;
        char                   key[PATH_MAX]             = "";
        char                   snap_mount[PATH_MAX]      = "";
        char                   snap_volname[64]          = "";
        char                   err_str[PATH_MAX]         = "";
        int                    ret                       = -1;
        int64_t                i                         = 0;
        int64_t                volcount                  = 0;
        int64_t                brick_count               = 0;
        int64_t                brick_order               = 0;
        glusterd_brickinfo_t  *brickinfo                 = NULL;
        glusterd_volinfo_t    *volinfo                   = NULL;
        xlator_t              *this                      = NULL;
        uuid_t                *snap_volid                = NULL;
        gf_loglevel_t          loglevel                  = GF_LOG_ERROR;
        glusterd_conf_t       *conf                      = NULL;
        int64_t                effective_max_limit       = 0;

        this = THIS;
        GF_ASSERT (op_errstr);
        conf = this->private;
        GF_ASSERT (conf);

        ret = dict_get_int64 (dict, "volcount", &volcount);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to "
                          "get the volume count");
                goto out;
        }
        if (volcount <= 0) {
                snprintf (err_str, sizeof (err_str), "Invalid volume count %"PRId64
                          " supplied", volcount);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                snprintf (err_str, sizeof (err_str), "Failed to get snapname");
                goto out;
        }

        if (glusterd_find_snap_by_name (snapname)) {
                ret = -1;
                snprintf (err_str, sizeof (err_str), "Snap %s already exists",
                          snapname);
                goto out;
        }

        for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "volname%"PRId64, i);
                ret = dict_get_str (dict, key, &volname);
                if (ret) {
                        snprintf (err_str, sizeof (err_str),
                                  "failed to get volume name");
                        goto out;
                }
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        snprintf (err_str, sizeof (err_str),
                                  "Volume (%s) does not exist ", volname);
                        goto out;
                }

                ret = -1;
                if (!glusterd_is_volume_started (volinfo)) {
                        snprintf (err_str, sizeof (err_str), "volume %s is "
                                  "not started", volinfo->volname);
                        loglevel = GF_LOG_WARNING;
                        goto out;
                }
                if (glusterd_is_defrag_on (volinfo)) {
                        snprintf (err_str, sizeof (err_str),
                                  "rebalance process is running for the "
                                  "volume %s", volname);
                        loglevel = GF_LOG_WARNING;
                        goto out;
                }
                /* TODO: Also check whether geo replication is running */

                if (volinfo->is_snap_volume == _gf_true) {
                        snprintf (err_str, sizeof (err_str),
                                  "Volume %s is a snap volume", volname);
                        loglevel = GF_LOG_WARNING;
                        goto out;
                }

                if (volinfo->snap_max_hard_limit < conf->snap_max_hard_limit)
                        effective_max_limit = volinfo->snap_max_hard_limit;
                else
                        effective_max_limit = conf->snap_max_hard_limit;

                if (volinfo->snap_count >= effective_max_limit) {
                        snprintf (err_str, sizeof (err_str),
                                  "The number of existing snaps has reached "
                                  "the effective maximum limit of %"PRIu64" ,"
                                  "for the volume %s", effective_max_limit,
                                  volname);
                        loglevel = GF_LOG_WARNING;
                        goto out;
                }

                snprintf (key, sizeof(key) - 1, "vol%"PRId64"_volid", i);
                ret = dict_get_bin (dict, key, (void **)&snap_volid);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to fetch snap_volid");
                        goto out;
                }

                /* snap volume uuid is used as lvm snapshot name.
                   This will avoid restrictions on snapshot names
                   provided by user */
                GLUSTERD_GET_UUID_NOHYPHEN (snap_volname, *snap_volid);

                brick_count = 0;
                brick_order = 0;
                /* Adding snap bricks mount paths to the dict */
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        if (uuid_compare (brickinfo->uuid, MY_UUID)) {
                                brick_order++;
                                continue;
                        }

                        if (!glusterd_is_brick_started (brickinfo)) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "brick %s:%s is not started",
                                        brickinfo->hostname,
                                        brickinfo->path);
                                brick_order++;
                                brick_count++;
                                continue;
                        }

                        device = glusterd_get_brick_mount_details (brickinfo);
                        if (!device) {
                                snprintf (err_str, sizeof (err_str),
                                          "getting device name for the brick "
                                          "%s:%s failed", brickinfo->hostname,
                                          brickinfo->path);
                                ret = -1;
                                goto out;
                        }

                        device = glusterd_build_snap_device_path (device,
                                                                  snap_volname);
                        if (!device) {
                                snprintf (err_str, sizeof (err_str),
                                          "cannot copy the snapshot device "
                                          "name (volname: %s, snapname: %s)",
                                          volinfo->volname, snapname);
                                loglevel = GF_LOG_WARNING;
                                ret = -1;
                                goto out;
                        }

                        snprintf (key, sizeof(key),
                                  "vol%"PRId64".brick_snapdevice%"PRId64, i,
                                  brick_count);
                        ret = dict_set_dynstr (rsp_dict, key, device);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set %s", key);
                                GF_FREE (device);
                                goto out;
                        }

                        ret = glusterd_get_brick_root (brickinfo->path,
                                                       &mnt_pt);
                        if (ret) {
                                snprintf (err_str, sizeof (err_str),
                                "could not get the root of the brick path %s",
                                brickinfo->path);
                                loglevel = GF_LOG_WARNING;
                                goto out;
                        }
                        if (strncmp (brickinfo->path, mnt_pt, strlen(mnt_pt))) {
                                snprintf (err_str, sizeof (err_str),
                                "brick: %s brick mount: %s",
                                 brickinfo->path, mnt_pt);
                                loglevel = GF_LOG_WARNING;
                                goto out;
                        }

                        brick_dir = &brickinfo->path[strlen (mnt_pt)];
                        brick_dir++;

                        snprintf (snap_brick_dir, sizeof (snap_brick_dir),
                                  "/%s", brick_dir);

                        tmpstr = gf_strdup (snap_brick_dir);
                        if (!tmpstr) {
                                ret = -1;
                                goto out;
                        }
                        snprintf (key, sizeof(key), "vol%"PRId64".brickdir%"PRId64, i,
                                  brick_count);
                        ret = dict_set_dynstr (rsp_dict, key, tmpstr);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set %s", snap_mount);
                                goto out;
                        }
                        tmpstr = NULL;

                        snprintf (key, sizeof(key) - 1, "vol%"PRId64".brick%"PRId64".order",
                                  i, brick_count);
                        ret = dict_set_int64 (rsp_dict, key, brick_order);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set %s", key);
                                goto out;
                        }

                        brick_count++;
                        brick_order++;
                }
                snprintf (key, sizeof(key) - 1, "vol%"PRId64"_brickcount", i);
                ret = dict_set_int64 (rsp_dict, key, brick_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set %s",
                                key);
                        goto out;
                }
        }

        ret = dict_set_int64 (rsp_dict, "volcount", volcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set volcount");
                goto out;
        }

        ret = 0;
out:
        if (ret)
                GF_FREE (tmpstr);

        if (ret && err_str[0] != '\0') {
                gf_log (this->name, loglevel, "%s", err_str);
                *op_errstr = gf_strdup (err_str);
        }

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

glusterd_snap_t*
glusterd_new_snap_object()
{
        glusterd_snap_t        *snap    = NULL;

        snap = GF_CALLOC (1, sizeof (*snap), gf_gld_mt_snap_t);

        if (snap) {
                if (LOCK_INIT (&snap->lock)) {
                        gf_log (THIS->name, GF_LOG_ERROR, "Failed initiating"
                                " snap lock");
                        GF_FREE (snap);
                        return NULL;
                }

                INIT_LIST_HEAD (&snap->snap_list);
                INIT_LIST_HEAD (&snap->volumes);
                snap->snapname[0] = 0;
                snap->snap_status = GD_SNAP_STATUS_INIT;
        }

        return snap;

};

/* Function glusterd_list_add_snapvol adds the volinfo object (snapshot volume)
   to the snapshot object list and to the parent volume list */
int32_t
glusterd_list_add_snapvol (glusterd_volinfo_t *origin_vol,
                           glusterd_volinfo_t *snap_vol)
{
        int ret               = -1;
        glusterd_snap_t *snap = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", origin_vol, out);
        GF_VALIDATE_OR_GOTO ("glusterd", snap_vol, out);

        snap = snap_vol->snapshot;
        GF_ASSERT (snap);

        list_add_tail (&snap_vol->vol_list, &snap->volumes);
        LOCK (&origin_vol->lock);
        {
                list_add_order (&snap_vol->snapvol_list,
                               &origin_vol->snap_volumes,
                               glusterd_compare_snap_vol_time);
                origin_vol->snap_count++;
        }
        UNLOCK (&origin_vol->lock);

        gf_log (THIS->name, GF_LOG_DEBUG, "Snap %s added to the list",
                snap->snapname);
        ret = 0;
 out:
        return ret;
}

glusterd_snap_t*
glusterd_find_snap_by_name (char *snapname)
{
        glusterd_snap_t *snap  = NULL;
        glusterd_conf_t *priv  = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (snapname);

        list_for_each_entry (snap, &priv->snapshots, snap_list) {
                if (!strcmp (snap->snapname, snapname)) {
                        gf_log (THIS->name, GF_LOG_DEBUG, "Found "
                                "snap %s (%s)", snap->snapname,
                                uuid_utoa (snap->snap_id));
                        goto out;
                }
        }
        snap = NULL;
out:
        return snap;
}

glusterd_snap_t*
glusterd_find_snap_by_id (uuid_t snap_id)
{
        glusterd_snap_t *snap  = NULL;
        glusterd_conf_t *priv  = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        if (uuid_is_null(snap_id))
                goto out;

        list_for_each_entry (snap, &priv->snapshots, snap_list) {
                if (!uuid_compare (snap->snap_id, snap_id)) {
                        gf_log (THIS->name, GF_LOG_DEBUG, "Found "
                                "snap %s (%s)", snap->snapname,
                                uuid_utoa (snap->snap_id));
                        goto out;
                }
        }
        snap = NULL;
out:
        return snap;
}

int
glusterd_do_lvm_snapshot_remove (glusterd_volinfo_t *snap_vol,
                                 glusterd_brickinfo_t *brickinfo,
                                 const char *mount_pt, const char *snap_device)
{
        int                     ret               = -1;
        xlator_t               *this              = NULL;
        glusterd_conf_t        *priv              = NULL;
        runner_t                runner            = {0,};
        char                    msg[1024]         = {0, };
        char                    pidfile[PATH_MAX] = {0, };
        pid_t                   pid               = -1;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!brickinfo) {
                gf_log (this->name, GF_LOG_ERROR, "brickinfo NULL");
                goto out;
        }
        GF_ASSERT (snap_vol);
        GF_ASSERT (mount_pt);
        GF_ASSERT (snap_device);

        GLUSTERD_GET_BRICK_PIDFILE (pidfile, snap_vol, brickinfo, priv);
        if (gf_is_service_running (pidfile, &pid)) {
                ret = kill (pid, SIGKILL);
                if (ret && errno != ESRCH) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to kill pid "
                                "%d reason : %s", pid, strerror(errno));
                        goto out;
                }
        }

        runinit (&runner);
        snprintf (msg, sizeof (msg), "umount the snapshot mounted path %s",
                  mount_pt);
        runner_add_args (&runner, "umount", mount_pt, NULL);
        runner_log (&runner, "", GF_LOG_DEBUG, msg);

        /* We need not do synclock_unlock => runner_run => synclock_lock here.
           Because it is needed if we are running a glusterfs process in
           runner_run, so that when the glusterfs process started wants to
           communicate to glusterd, glusterd wont be able to respond if it
           has held the big lock. So we do unlock, run glusterfs process
           (thus communicate to glusterd), lock. But since this is not a
           glusterfs command that is being run, unlocking and then relocking
           is not needed.
        */
        ret = runner_run (&runner);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "unmounting the "
                        "path %s (brick: %s) failed (%s)", mount_pt,
                        brickinfo->path, strerror (errno));
                goto out;
        }

        runinit (&runner);
        snprintf (msg, sizeof(msg), "remove snapshot of the brick %s:%s, "
                  "device: %s", brickinfo->hostname, brickinfo->path,
                  snap_device);
        runner_add_args (&runner, LVM_REMOVE, "-f", snap_device, NULL);
        runner_log (&runner, "", GF_LOG_DEBUG, msg);

        ret = runner_run (&runner);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "removing snapshot of the "
                        "brick (%s:%s) of device %s failed",
                        brickinfo->hostname, brickinfo->path, snap_device);
                goto out;
        }

out:
        return ret;
}

int32_t
glusterd_lvm_snapshot_remove (dict_t *rsp_dict, glusterd_volinfo_t *snap_vol)
{
        char                 *mnt_pt        = NULL;
        struct mntent        *entry         = NULL;
        int32_t               brick_count   = -1;
        int32_t               ret           = -1;
        glusterd_brickinfo_t *brickinfo     = NULL;
        xlator_t             *this          = NULL;
        FILE                 *mtab          = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (snap_vol);

        if (!snap_vol) {
                gf_log (this->name, GF_LOG_ERROR, "snap volinfo is NULL");
                goto out;
        }

        brick_count = -1;
        list_for_each_entry (brickinfo, &snap_vol->bricks, brick_list) {
                brick_count++;
                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                if (brickinfo->snap_status == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "snapshot was pending. lvm not present "
                                "for brick %s:%s of the snap %s.",
                                brickinfo->hostname, brickinfo->path,
                                snap_vol->snapshot->snapname);

                        /* Adding missed delete to the dict */
                        ret = glusterd_add_missed_snaps_to_dict
                                                   (rsp_dict,
                                                    snap_vol->volname,
                                                    brickinfo,
                                                    brick_count + 1,
                                                    GF_SNAP_OPTION_TYPE_DELETE);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to add missed snapshot info "
                                        "for %s:%s in the rsp_dict",
                                        brickinfo->hostname,
                                        brickinfo->path);
                                goto out;
                        }

                        continue;
                }

                ret = glusterd_get_brick_root (brickinfo->path, &mnt_pt);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "getting the root "
                               "of the brick for volume %s (snap %s) failed ",
                               snap_vol->volname, snap_vol->snapshot->snapname);
                        goto out;
                }

                entry = glusterd_get_mnt_entry_info (mnt_pt, mtab);
                if (!entry) {
                        gf_log (this->name, GF_LOG_WARNING, "getting the mount"
                                " entry for the brick %s:%s of the snap %s "
                                "(volume: %s) failed", brickinfo->hostname,
                                brickinfo->path, snap_vol->snapshot->snapname,
                                snap_vol->volname);
                        ret = -1;
                        goto out;
                }
                ret = glusterd_do_lvm_snapshot_remove (snap_vol, brickinfo,
                                                       mnt_pt,
                                                       entry->mnt_fsname);
                if (mtab)
                        endmntent (mtab);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "remove the snapshot %s (%s)",
                                brickinfo->path, entry->mnt_fsname);
                        goto out;
                }

        }

        ret = 0;
out:
        return ret;
}


int32_t
glusterd_snap_volume_remove (dict_t *rsp_dict,
                             glusterd_volinfo_t *snap_vol,
                             gf_boolean_t remove_lvm,
                             gf_boolean_t force)
{
        int                   ret         = -1;
        int                   save_ret    =  0;
        glusterd_brickinfo_t *brickinfo   = NULL;
        glusterd_volinfo_t   *origin_vol  = NULL;
        xlator_t             *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (snap_vol);

        if (!snap_vol) {
                gf_log(this->name, GF_LOG_WARNING, "snap_vol in NULL");
                ret = -1;
                goto out;
        }

        list_for_each_entry (brickinfo, &snap_vol->bricks, brick_list) {
                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                ret = glusterd_brick_stop (snap_vol, brickinfo, _gf_false);
                if (ret) {
                        gf_log(this->name, GF_LOG_WARNING, "Failed to stop "
                               "brick for volume %s", snap_vol->volname);
                        save_ret = ret;

                        /* Continue to cleaning up the snap in case of error
                           if force flag is enabled */
                        if (!force)
                                goto out;
                }
        }

        /* Only remove the backend lvm when required */
        if (remove_lvm) {
                ret = glusterd_lvm_snapshot_remove (rsp_dict, snap_vol);
                if (ret) {
                        gf_log(this->name, GF_LOG_WARNING, "Failed to remove "
                               "lvm snapshot volume %s", snap_vol->volname);
                        save_ret = ret;
                        if (!force)
                                goto out;
                }
        }

        ret = glusterd_store_delete_volume (snap_vol);
        if (ret) {
                gf_log(this->name, GF_LOG_WARNING, "Failed to remove volume %s "
                       "from store", snap_vol->volname);
                save_ret = ret;
                if (!force)
                        goto out;
        }

        if (!list_empty(&snap_vol->snapvol_list)) {
                ret = glusterd_volinfo_find (snap_vol->parent_volname,
                                             &origin_vol);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "parent volinfo %s  for volume  %s",
                                snap_vol->parent_volname, snap_vol->volname);
                        save_ret = ret;
                        if (!force)
                                goto out;
                }
                origin_vol->snap_count--;
        }

        ret = glusterd_volinfo_delete (snap_vol);
        if (ret) {
                gf_log(this->name, GF_LOG_WARNING, "Failed to remove volinfo "
                       "%s ", snap_vol->volname);
                save_ret = ret;
                if (!force)
                        goto out;
        }

        if (save_ret)
                ret = save_ret;
out:
        gf_log (this->name, GF_LOG_TRACE, "returning %d", ret);
        return ret;
}

int32_t
glusterd_snapobject_delete (glusterd_snap_t *snap)
{
        if (snap == NULL) {
                gf_log(THIS->name, GF_LOG_WARNING, "snap is NULL");
                return -1;
        }

        list_del_init (&snap->snap_list);
        list_del_init (&snap->volumes);
        if (LOCK_DESTROY(&snap->lock))
                gf_log (THIS->name, GF_LOG_WARNING, "Failed destroying lock"
                        "of snap %s", snap->snapname);

        GF_FREE (snap->description);
        GF_FREE (snap);

        return 0;
}

int32_t
glusterd_snap_remove (dict_t *rsp_dict,
                      glusterd_snap_t *snap,
                      gf_boolean_t remove_lvm,
                      gf_boolean_t force)
{
        int                 ret       = -1;
        int                 save_ret  =  0;
        glusterd_volinfo_t *snap_vol  = NULL;
        glusterd_volinfo_t *tmp       = NULL;
        xlator_t           *this      = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (snap);

        if (!snap) {
                gf_log(this->name, GF_LOG_WARNING, "snap is NULL");
                ret = -1;
                goto out;
        }

        list_for_each_entry_safe (snap_vol, tmp, &snap->volumes, vol_list) {
                ret = glusterd_snap_volume_remove (rsp_dict, snap_vol,
                                                   remove_lvm, force);
                if (ret) {
                        gf_log(this->name, GF_LOG_WARNING, "Failed to remove "
                               "volinfo %s for snap %s", snap_vol->volname,
                               snap->snapname);
                        save_ret = ret;

                        /* Continue to cleaning up the snap in case of error
                           if force flag is enabled */
                        if (!force)
                                goto out;
                }
        }

        ret = glusterd_store_delete_snap (snap);
        if (ret) {
                gf_log(this->name, GF_LOG_WARNING, "Failed to remove snap %s "
                       "from store", snap->snapname);
                save_ret = ret;
                if (!force)
                        goto out;
        }

        ret = glusterd_snapobject_delete (snap);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "Failed to delete "
                        "snap object %s", snap->snapname);

        if (save_ret)
                ret = save_ret;
out:
        gf_log (THIS->name, GF_LOG_TRACE, "returning %d", ret);
        return ret;
}

static int
glusterd_snapshot_get_snapvol_detail (dict_t *dict,
                                      glusterd_volinfo_t *snap_vol,
                                      char *keyprefix, int detail)
{
        int                 ret           = -1;
        int                 snap_limit    = 0;
        char                key[PATH_MAX] = {0,};
        char               *value         = NULL;
        glusterd_volinfo_t *origin_vol    = NULL;
        glusterd_conf_t    *conf          = NULL;
        xlator_t           *this          = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (dict);
        GF_ASSERT (snap_vol);
        GF_ASSERT (keyprefix);

        /* Volume Name */
        value = gf_strdup (snap_vol->volname);
        if (!value)
                goto out;

        snprintf (key, sizeof (key), "%s.volname", keyprefix);
        ret = dict_set_dynstr (dict, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "volume name in dictionary: %s", key);
                goto out;
        }

        /* Volume ID */
        value = gf_strdup (uuid_utoa (snap_vol->volume_id));
        if (NULL == value) {
                ret = -1;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.vol-id", keyprefix);
        ret = dict_set_dynstr (dict, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "volume id in dictionary: %s", key);
                goto out;
        }
        value = NULL;

        /* volume status */
        snprintf (key, sizeof (key), "%s.vol-status", keyprefix);
        switch (snap_vol->status) {
        case GLUSTERD_STATUS_STARTED:
                ret = dict_set_str (dict, key, "Started");
                break;
        case GLUSTERD_STATUS_STOPPED:
                ret = dict_set_str (dict, key, "Stopped");
                break;
        case GD_SNAP_STATUS_NONE:
                ret = dict_set_str (dict, key, "None");
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR, "Invalid volume status");
                ret = -1;
                goto out;
        }
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set volume status"
                        " in dictionary: %s", key);
                goto out;
        }


        ret = glusterd_volinfo_find (snap_vol->parent_volname, &origin_vol);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the parent "
                        "volinfo for the volume %s", snap_vol->volname);
                goto out;
        }

        /* Snaps available */
        if (conf->snap_max_hard_limit < origin_vol->snap_max_hard_limit) {
                snap_limit = conf->snap_max_hard_limit;
                gf_log(this->name, GF_LOG_DEBUG, "system snap-max-hard-limit is"
                       " lesser than volume snap-max-hard-limit, "
                       "snap-max-hard-limit value is set to %d", snap_limit);
        } else {
                snap_limit = origin_vol->snap_max_hard_limit;
                gf_log(this->name, GF_LOG_DEBUG, "volume snap-max-hard-limit is"
                       " lesser than system snap-max-hard-limit, "
                       "snap-max-hard-limit value is set to %d", snap_limit);
        }

        snprintf (key, sizeof (key), "%s.snaps-available", keyprefix);
        if (snap_limit > origin_vol->snap_count)
                ret = dict_set_int32 (dict, key,
                        snap_limit - origin_vol->snap_count);
        else
                ret = dict_set_int32 (dict, key, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set available snaps");
                goto out;
        }

        snprintf (key, sizeof (key), "%s.snapcount", keyprefix);
        ret = dict_set_int32 (dict, key, origin_vol->snap_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not save snapcount");
                goto out;
        }

        if (!detail)
                goto out;

        /* Parent volume name */
        value = gf_strdup (snap_vol->parent_volname);
        if (!value)
                goto out;

        snprintf (key, sizeof (key), "%s.origin-volname", keyprefix);
        ret = dict_set_dynstr (dict, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set parent "
                        "volume name in dictionary: %s", key);
                goto out;
        }
        value = NULL;

        ret = 0;
out:
        if (value)
                GF_FREE (value);

        return ret;
}

static int
glusterd_snapshot_get_snap_detail (dict_t *dict, glusterd_snap_t *snap,
                                char *keyprefix, glusterd_volinfo_t *volinfo)
{
        int                 ret           = -1;
        int                 volcount      = 0;
        char                key[PATH_MAX] = {0,};
        char               *value         = NULL;
        char               *timestr       = NULL;
        struct tm          *tmptr         = NULL;
        glusterd_volinfo_t *snap_vol      = NULL;
        glusterd_volinfo_t *tmp_vol       = NULL;
        xlator_t           *this          = NULL;

        this = THIS;

        GF_ASSERT (dict);
        GF_ASSERT (snap);
        GF_ASSERT (keyprefix);

        /* Snap Name */
        value = gf_strdup (snap->snapname);
        if (!value)
                goto out;

        snprintf (key, sizeof (key), "%s.snapname", keyprefix);
        ret = dict_set_dynstr (dict, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "snap name in dictionary");
                goto out;
        }

        /* Snap ID */
        value = gf_strdup (uuid_utoa (snap->snap_id));
        if (NULL == value) {
                ret = -1;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.snap-id", keyprefix);
        ret = dict_set_dynstr (dict, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                                "snap id in dictionary");
                goto out;
        }
        value = NULL;

        tmptr = localtime (&(snap->time_stamp));
        if (NULL == tmptr) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to convert "
                                "time_t to *tm");
                ret = -1;
                goto out;
        }

        timestr = GF_CALLOC (1, PATH_MAX, gf_gld_mt_char);
        if (NULL == timestr) {
                ret = -1;
                goto out;
        }

        ret = strftime (timestr, PATH_MAX, "%Y-%m-%d %H:%M:%S", tmptr);
        if (0 == ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to convert time_t "
                        "to string");
                ret = -1;
                goto out;
        }

        snprintf (key, sizeof (key), "%s.snap-time", keyprefix);
        ret = dict_set_dynstr (dict, key, timestr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                                "snap time stamp in dictionary");
                goto out;
        }
        timestr = NULL;

        /* If snap description is provided then add that into dictionary */
        if (NULL != snap->description) {
                value = gf_strdup (snap->description);
                if (NULL == value) {
                        ret = -1;
                        goto out;
                }

                snprintf (key, sizeof (key), "%s.snap-desc", keyprefix);
                ret = dict_set_dynstr (dict, key, value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                                        "snap description in dictionary");
                        goto out;
                }
                value = NULL;
        }

        snprintf (key, sizeof (key), "%s.snap-status", keyprefix);
        switch (snap->snap_status) {
        case GD_SNAP_STATUS_INIT:
                ret = dict_set_str (dict, key, "Init");
                break;
        case GD_SNAP_STATUS_IN_USE:
                ret = dict_set_str (dict, key, "In-use");
                break;
        case GD_SNAP_STATUS_DECOMMISSION:
                ret = dict_set_str (dict, key, "Decommisioned");
                break;
        case GD_SNAP_STATUS_RESTORED:
                ret = dict_set_str (dict, key, "Restored");
                break;
        case GD_SNAP_STATUS_NONE:
                ret = dict_set_str (dict, key, "None");
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR, "Invalid snap status");
                ret = -1;
                goto out;
        }
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set snap status "
                        "in dictionary");
                goto out;
        }

        if (volinfo) {
                volcount = 1;
                snprintf (key, sizeof (key), "%s.vol%d", keyprefix, volcount);
                ret = glusterd_snapshot_get_snapvol_detail (dict,
                                                        volinfo, key, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "get volume detail %s for snap %s",
                                snap_vol->volname, snap->snapname);
                        goto out;
                }
                goto done;
        }

        list_for_each_entry_safe (snap_vol, tmp_vol, &snap->volumes, vol_list) {
                volcount++;
                snprintf (key, sizeof (key), "%s.vol%d", keyprefix, volcount);
                ret = glusterd_snapshot_get_snapvol_detail (dict,
                                                        snap_vol, key, 1);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "get volume detail %s for snap %s",
                                snap_vol->volname, snap->snapname);
                        goto out;
                }
        }

done:
        snprintf (key, sizeof (key), "%s.vol-count", keyprefix);
        ret = dict_set_int32 (dict, key, volcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set %s",
                        key);
                goto out;
        }

        ret = 0;
out:
        if (value)
                GF_FREE (value);

        if (timestr)
                GF_FREE(timestr);

        return ret;
}

static int
glusterd_snapshot_get_all_snap_info (dict_t *dict)
{
        int                 ret           = -1;
        int                 snapcount     = 0;
        char                key[PATH_MAX] = {0,};
        glusterd_snap_t    *snap          = NULL;
        glusterd_snap_t    *tmp_snap      = NULL;
        glusterd_conf_t    *priv          = NULL;
        xlator_t           *this          = NULL;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        /* General parameter validation */
        GF_ASSERT (dict);

        list_for_each_entry_safe (snap, tmp_snap, &priv->snapshots, snap_list) {
                snapcount++;
                snprintf (key, sizeof (key), "snap%d", snapcount);
                ret = glusterd_snapshot_get_snap_detail (dict, snap, key, NULL);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "snapdetail for snap %s", snap->snapname);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "snap-count", snapcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set snapcount");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_snapshot_get_info_by_volume (dict_t *dict, char *volname,
                                      char *err_str, size_t len)
{
        int                  ret           = -1;
        int                  snapcount     = 0;
        int                  snap_limit    = 0;
        char                *value         = NULL;
        char                 key[PATH_MAX] = "";
        glusterd_volinfo_t  *volinfo       = NULL;
        glusterd_volinfo_t  *snap_vol      = NULL;
        glusterd_volinfo_t  *tmp_vol       = NULL;
        glusterd_conf_t     *conf          = NULL;
        xlator_t            *this          = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (dict);
        GF_ASSERT (volname);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                snprintf (err_str, len, "Volume (%s) does not exist", volname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        /* Snaps available */
        if (conf->snap_max_hard_limit < volinfo->snap_max_hard_limit) {
                snap_limit = conf->snap_max_hard_limit;
                gf_log(this->name, GF_LOG_DEBUG, "system snap-max-hard-limit is"
                       " lesser than volume snap-max-hard-limit, "
                       "snap-max-hard-limit value is set to %d", snap_limit);
        } else {
                snap_limit = volinfo->snap_max_hard_limit;
                gf_log(this->name, GF_LOG_DEBUG, "volume snap-max-hard-limit is"
                       " lesser than system snap-max-hard-limit, "
                       "snap-max-hard-limit value is set to %d", snap_limit);
        }

        if (snap_limit > volinfo->snap_count)
                ret = dict_set_int32 (dict, "snaps-available",
                        snap_limit - volinfo->snap_count);
        else
                ret = dict_set_int32 (dict, "snaps-available", 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set available snaps");
                goto out;
        }

        /* Origin volume name */
        value = gf_strdup (volinfo->volname);
        if (!value)
                goto out;

        ret = dict_set_dynstr (dict, "origin-volname", value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set parent "
                        "volume name in dictionary: %s", key);
                goto out;
        }
        value = NULL;

        list_for_each_entry_safe (snap_vol, tmp_vol, &volinfo->snap_volumes,
                                  snapvol_list) {
                snapcount++;
                snprintf (key, sizeof (key), "snap%d", snapcount);
                ret = glusterd_snapshot_get_snap_detail (dict,
                                                         snap_vol->snapshot,
                                                         key, snap_vol);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "snapdetail for snap %s",
                                snap_vol->snapshot->snapname);
                        goto out;
                }
        }
        ret = dict_set_int32 (dict, "snap-count", snapcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set snapcount");
                goto out;
        }

        ret = 0;
out:
        if (value)
                GF_FREE (value);

        return ret;
}

/* This function will be called from RPC handler routine.
 * This function is responsible for getting the requested
 * snapshot info into the dictionary.
 *
 * @param req   RPC request object. Required for sending a response back.
 * @param op    glusterd operation. Required for sending a response back.
 * @param dict  pointer to dictionary which will contain both
 *              request and response key-pair values.
 * @return -1 on error and 0 on success
 */
int
glusterd_handle_snapshot_info (rpcsvc_request_t *req, glusterd_op_t op,
                               dict_t *dict, char *err_str, size_t len)
{
        int              ret             =       -1;
        int8_t          snap_driven      =        1;
        char            *volname         =      NULL;
        char            *snapname        =      NULL;
        glusterd_snap_t *snap            =      NULL;
        xlator_t        *this            =      NULL;
        int32_t         cmd              =      GF_SNAP_INFO_TYPE_ALL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, req, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);


        ret = dict_get_int32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get type "
                        "of snapshot info");
                goto out;
        }

        switch (cmd) {
                case GF_SNAP_INFO_TYPE_ALL:
                {
                        ret = glusterd_snapshot_get_all_snap_info (dict);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get info of all snaps");
                                goto out;
                        }
                        break;
                }

                case GF_SNAP_INFO_TYPE_SNAP:
                {
                        ret = dict_get_str (dict, "snapname", &snapname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get snap name");
                                goto out;
                        }

                        ret = dict_set_int32 (dict, "snap-count", 1);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set snapcount");
                                goto out;
                        }

                        snap = glusterd_find_snap_by_name (snapname);
                        if (!snap) {
                                snprintf (err_str, len,
                                         "Snap (%s) does not exist", snapname);
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s", err_str);
                                ret = -1;
                                goto out;
                        }
                        ret = glusterd_snapshot_get_snap_detail (dict, snap,
                                                                "snap1", NULL);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get snap detail of snap "
                                        "%s", snap->snapname);
                                goto out;
                        }
                        break;
                }

                case GF_SNAP_INFO_TYPE_VOL:
                {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get volname");
                                goto out;
                        }
                        ret = glusterd_snapshot_get_info_by_volume (dict,
                                                        volname, err_str, len);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get volume info of volume "
                                        "%s", volname);
                                goto out;
                        }
                        snap_driven = 0;
                        break;
                }
        }

        ret = dict_set_int8 (dict, "snap-driven", snap_driven);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set snap-driven");
                goto out;
        }

        /* If everything is successful then send the response back to cli.
         * In case of failure the caller of this function will take care
           of the response */
        ret = glusterd_op_send_cli_response (op, 0, 0, req, dict, err_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to send cli "
                                "response");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

/* This function sets all the snapshot names in the dictionary */
int
glusterd_snapshot_get_all_snapnames (dict_t *dict)
{
        int              ret             = -1;
        int              snapcount       = 0;
        char            *snapname        = NULL;
        char             key[PATH_MAX]   = {0,};
        glusterd_snap_t *snap            = NULL;
        glusterd_snap_t *tmp_snap        = NULL;
        glusterd_conf_t *priv            = NULL;
        xlator_t        *this            = NULL;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (dict);

        list_for_each_entry_safe (snap, tmp_snap, &priv->snapshots, snap_list) {
                snapcount++;
                snapname = gf_strdup (snap->snapname);
                if (!snapname) {
                        gf_log (this->name, GF_LOG_ERROR, "strdup failed");
                        ret = -1;
                        goto out;
                }
                snprintf (key, sizeof (key), "snapname%d", snapcount);
                ret = dict_set_dynstr (dict, key, snapname);
                if (ret) {
                        GF_FREE (snapname);
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set %s",
                                key);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "snap-count", snapcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set snapcount");
                goto out;
        }

        ret = 0;
out:

        return ret;
}

/* This function sets all the snapshot names
   under a given volume in the dictionary */
int
glusterd_snapshot_get_vol_snapnames (dict_t *dict, glusterd_volinfo_t *volinfo)
{
        int                 ret             = -1;
        int                 snapcount       = 0;
        char               *snapname        = NULL;
        char                key[PATH_MAX]   = {0,};
        glusterd_volinfo_t *snap_vol        = NULL;
        glusterd_volinfo_t *tmp_vol         = NULL;
        xlator_t           *this            = NULL;

        this = THIS;
        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        list_for_each_entry_safe (snap_vol, tmp_vol,
                                  &volinfo->snap_volumes, snapvol_list) {
                snapcount++;
                snapname = gf_strdup (snap_vol->snapshot->snapname);
                if (!snapname) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "strdup failed");
                        ret = -1;
                        goto out;
                }
                snprintf (key, sizeof (key), "snapname%d", snapcount);
                ret = dict_set_dynstr (dict, key, snapname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "set %s", key);
                        GF_FREE (snapname);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "snap-count", snapcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set snapcount");
                goto out;
        }

        ret = 0;
out:

        return ret;
}

int
glusterd_handle_snapshot_list (rpcsvc_request_t *req, glusterd_op_t op,
                               dict_t *dict, char *err_str, size_t len)
{
        int                 ret             = -1;
        char               *volname         = NULL;
        glusterd_volinfo_t *volinfo         = NULL;
        xlator_t           *this            = NULL;

        this = THIS;

        GF_VALIDATE_OR_GOTO (this->name, req, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        /* Ignore error for getting volname as it is optional */
        ret = dict_get_str (dict, "volname", &volname);

        if (NULL == volname) {
                ret = glusterd_snapshot_get_all_snapnames (dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get snapshot list");
                        goto out;
                }
        } else {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        snprintf (err_str, len,
                                 "Volume (%s) does not exist", volname);
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s", err_str);
                        goto out;
                }

                ret = glusterd_snapshot_get_vol_snapnames (dict, volinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get snapshot list for volume %s",
                                volname);
                        goto out;
                }
        }

        /* If everything is successful then send the response back to cli.
        In case of failure the caller of this function will take of response.*/
        ret = glusterd_op_send_cli_response (op, 0, 0, req, dict, err_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to send cli "
                                "response");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

/* This is a snapshot create handler function. This function will be
 * executed in the originator node. This function is responsible for
 * calling mgmt_v3 framework to do the actual snap creation on all the bricks
 *
 * @param req           RPC request object
 * @param op            gluster operation
 * @param dict          dictionary containing snapshot restore request
 * @param err_str       In case of an err this string should be populated
 * @param len           length of err_str buffer
 *
 * @return              Negative value on Failure and 0 in success
 */
int
glusterd_handle_snapshot_create (rpcsvc_request_t *req, glusterd_op_t op,
                               dict_t *dict, char *err_str, size_t len)
{
        int           ret                     = -1;
        char         *volname                 = NULL;
        char         *snapname                = NULL;
        int64_t       volcount                = 0;
        xlator_t     *this                    = NULL;
        char          key[PATH_MAX]           = "";
        char         *username                = NULL;
        char         *password                = NULL;
        uuid_t       *uuid_ptr                = NULL;
        uuid_t        tmp_uuid                = {0};
        int           i                       = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        GF_ASSERT (err_str);

        ret = dict_get_int64 (dict, "volcount", &volcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "get the volume count");
                goto out;
        }
        if (volcount <= 0) {
                gf_log (this->name, GF_LOG_ERROR, "Invalid volume count %"PRId64
                        " supplied", volcount);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the snapname");
                goto out;
        }

        if (strlen(snapname) >= GLUSTERD_MAX_SNAP_NAME) {
                snprintf (err_str, len, "snapname cannot exceed 255 "
                          "characters");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                ret = -1;
                goto out;
        }

        uuid_ptr = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!uuid_ptr) {
                gf_log (this->name, GF_LOG_ERROR, "Out Of Memory");
                ret = -1;
                goto out;
        }

        uuid_generate (*uuid_ptr);
        ret = dict_set_bin (dict, "snap-id", uuid_ptr, sizeof(uuid_t));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to set snap-id");
                GF_FREE (uuid_ptr);
                goto out;
        }
        uuid_ptr = NULL;

        ret = dict_set_int64 (dict, "snap-time", (int64_t)time(NULL));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to set snap-time");
                goto out;
        }

        for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "volname%d", i);
                ret = dict_get_str (dict, key, &volname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to get volume name");
                        goto out;
                }

                /* generate internal username and password  for the snap*/
                uuid_generate (tmp_uuid);
                username = gf_strdup (uuid_utoa (tmp_uuid));
                snprintf (key, sizeof(key), "volume%d_username", i);
                ret = dict_set_dynstr (dict, key, username);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set snap "
                                "username for volume %s", volname);
                        GF_FREE (username);
                        goto out;
                }

                uuid_generate (tmp_uuid);
                password = gf_strdup (uuid_utoa (tmp_uuid));
                snprintf (key, sizeof(key), "volume%d_password", i);
                ret = dict_set_dynstr (dict, key, password);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set snap "
                                "password for volume %s", volname);
                        GF_FREE (password);
                        goto out;
                }

                uuid_ptr = GF_CALLOC (1, sizeof(uuid_t), gf_common_mt_uuid_t);
                if (!uuid_ptr) {
                        gf_log (this->name, GF_LOG_ERROR, "Out Of Memory");
                        ret = -1;
                        goto out;
                }

                snprintf (key, sizeof(key) - 1, "vol%d_volid", i);
                uuid_generate (*uuid_ptr);
                ret = dict_set_bin (dict, key, uuid_ptr, sizeof(uuid_t));
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to set snap_volid");
                        GF_FREE (uuid_ptr);
                        goto out;
                }
        }

        ret = glusterd_mgmt_v3_initiate_snap_phases (req, op, dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to initiate snap "
                                "phases");
        }

out:
        return ret;
}

/* This is a snapshot status handler function. This function will be
 * executed in a originator node. This function is responsible for
 * calling mgmt v3 framework to get the actual snapshot status from
 * all the bricks
 *
 * @param req           RPC request object
 * @param op            gluster operation
 * @param dict          dictionary containing snapshot status request
 * @param err_str       In case of an err this string should be populated
 * @param len           length of err_str buffer
 *
 * return :  0  in case of success.
 *          -1  in case of failure.
 *
 */
int
glusterd_handle_snapshot_status (rpcsvc_request_t *req, glusterd_op_t op,
                                 dict_t *dict, char *err_str, size_t len)
{
        int                     ret             =       -1;
        char                    *volname        =       NULL;
        char                    *snapname       =       NULL;
        char                    *buf            =       NULL;
        glusterd_conf_t         *conf           =       NULL;
        xlator_t                *this           =       NULL;
        int32_t                 cmd             =       -1;
        int                     i               =       0;
        dict_t                  *voldict        =       NULL;
        char                    key[PATH_MAX]   =       "";
        glusterd_volinfo_t     *volinfo         =       NULL;
        glusterd_snap_t         *snap           =       NULL;
        glusterd_volinfo_t      *snap_volinfo   =       NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        GF_ASSERT (conf);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        GF_ASSERT (err_str);

        ret = dict_get_int32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not get status type");
                goto out;
        }
        switch (cmd) {
                case GF_SNAP_STATUS_TYPE_ALL:
                {
                        /* IF we give "gluster snapshot status"
                         * then lock is held on all snaps.
                         * This is the place where necessary information
                         * (snapname and snapcount)is populated in dictionary
                         * for locking.
                         */
                        ++i;
                        list_for_each_entry (snap, &conf->snapshots, snap_list)
                        {
                                snprintf (key, sizeof (key), "snapname%d", i);
                                buf = gf_strdup (snap->snapname);
                                if (!buf) {
                                        ret = -1;
                                        goto out;
                                }
                                ret = dict_set_dynstr (dict, key, buf);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Could not save snapname (%s) "
                                                "in the dictionary",
                                                snap->snapname);
                                        GF_FREE (buf);
                                        goto out;
                                }

                                buf = NULL;
                                i++;
                        }

                        ret = dict_set_int32 (dict, "snapcount", i - 1);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Could not "
                                        "save snapcount in the dictionary");
                                goto out;
                        }
                        break;
                }

                case GF_SNAP_STATUS_TYPE_SNAP:
                {
                        /* IF we give "gluster snapshot status <snapname>"
                         * then lock is held on single snap.
                         * This is the place where necessary information
                         * (snapname)is populated in dictionary
                         * for locking.
                         */
                        ret = dict_get_str (dict, "snapname", &snapname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to fetch snap name");
                                goto out;
                        }

                        snap = glusterd_find_snap_by_name (snapname);
                        if (!snap) {
                                snprintf (err_str, len, "Snap (%s)"
                                          "does not exist", snapname);
                                gf_log(this->name, GF_LOG_ERROR,
                                        "%s", err_str);
                                ret = -1;
                                goto out;
                        }
                        break;
                }
                case GF_SNAP_STATUS_TYPE_VOL:
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to fetch volname");
                                goto out;
                        }

                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                snprintf (err_str, len, "Volume (%s) "
                                          "does not exist", volname);
                                gf_log (this->name, GF_LOG_ERROR,
                                        "%s", err_str);
                                goto out;
                        }

                        i = 1;
                        list_for_each_entry (snap_volinfo,
                                        &volinfo->snap_volumes, snapvol_list) {
                                snprintf (key, sizeof (key), "snapname%d", i);

                                buf = gf_strdup
                                             (snap_volinfo->snapshot->snapname);
                                if (!buf) {
                                        ret = -1;
                                        goto out;
                                }

                                ret = dict_set_dynstr (dict, key, buf);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Could not save snapname");
                                        GF_FREE (buf);
                                        goto out;
                                }

                                buf = NULL;
                                i++;
                        }

                        ret = dict_set_int32 (dict, "snapcount", i-1);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not save snapcount");
                                goto out;
                        }
                        break;
                default:
                {
                        gf_log (this->name, GF_LOG_ERROR, "Unknown type");
                        ret = -1;
                        goto out;
                }
        }

        /* Volume lock is not necessary for snapshot status, hence
         * turning it off
         */
        ret = dict_set_int8 (dict, "hold_vol_locks", 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Setting volume lock "
                        "flag failed");
                goto out;
        }

        ret = glusterd_mgmt_v3_initiate_snap_phases (req, op, dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to initiate "
                        "snap phases");
                goto out;
        }

        ret = 0;

out:
        if (voldict) {
                dict_unref (voldict);
        }
        return ret;
}


/* This is a snapshot restore handler function. This function will be
 * executed in the originator node. This function is responsible for
 * calling mgmt_v3 framework to do the actual restore on all the bricks
 *
 * @param req           RPC request object
 * @param op            gluster operation
 * @param dict          dictionary containing snapshot restore request
 * @param err_str       In case of an err this string should be populated
 * @param len           length of err_str buffer
 *
 * @return              Negative value on Failure and 0 in success
 */
int
glusterd_handle_snapshot_restore (rpcsvc_request_t *req, glusterd_op_t op,
                               dict_t *dict, char *err_str, size_t len)
{
        int                     ret             = -1;
        char                    *snapname       = NULL;
        char                    *buf            = NULL;
        glusterd_conf_t         *conf           = NULL;
        xlator_t                *this           = NULL;
        glusterd_snap_t         *snap           = NULL;
        glusterd_volinfo_t      *snap_volinfo   = NULL;
        int32_t                 i               =    0;
        char                    key[PATH_MAX]   =    "";

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        GF_ASSERT (conf);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        GF_ASSERT (err_str);

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                        "get snapname");
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (!snap) {
                snprintf (err_str, len, "Snap (%s) does not exist", snapname);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                ret = -1;
                goto out;
        }

        list_for_each_entry (snap_volinfo, &snap->volumes, vol_list) {
                i++;
                snprintf (key, sizeof (key), "volname%d", i);
                buf = gf_strdup (snap_volinfo->parent_volname);
                if (!buf) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dict, key, buf);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Could not set "
                                "parent volume name %s in the dict",
                                snap_volinfo->parent_volname);
                        GF_FREE (buf);
                        goto out;
                }
                buf = NULL;
        }

        ret = dict_set_int32 (dict, "volcount", i);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not save volume count");
                goto out;
        }

        ret = glusterd_mgmt_v3_initiate_snap_phases (req, op, dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to initiate snap "
                                "phases");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

glusterd_snap_t*
glusterd_create_snap_object (dict_t *dict, dict_t *rsp_dict)
{
        char              *snapname    = NULL;
        uuid_t            *snap_id     = NULL;
        char              *description = NULL;
        glusterd_snap_t   *snap        = NULL;
        xlator_t          *this        = NULL;
        glusterd_conf_t   *priv        = NULL;
        int                ret         = -1;
        int64_t            time_stamp  = 0;

        this = THIS;
        priv = this->private;

        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        /* Fetch snapname, description, id and time from dict */
        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch snapname");
                goto out;
        }

        /* Ignore ret value for description*/
        ret = dict_get_str (dict, "description", &description);

        ret = dict_get_bin (dict, "snap-id", (void **)&snap_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch snap_id");
                goto out;
        }

        ret = dict_get_int64 (dict, "snap-time", &time_stamp);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch snap-time");
                goto out;
        }
        if (time_stamp <= 0) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Invalid time-stamp: %"PRId64,
                        time_stamp);
                goto out;
        }

        list_for_each_entry (snap, &priv->snapshots, snap_list) {
                if (!strcmp (snap->snapname, snapname) ||
                    !uuid_compare (snap->snap_id, *snap_id)) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Found duplicate snap %s (%s)",
                                snap->snapname, uuid_utoa (snap->snap_id));
                        ret = -1;
                        break;
                }
        }
        if (ret) {
                snap = NULL;
                goto out;
        }

        snap = glusterd_new_snap_object ();
        if (!snap) {
                gf_log (this->name, GF_LOG_ERROR, "Could not create "
                        "the snap object for snap %s", snapname);
                goto out;
        }

        strcpy (snap->snapname, snapname);
        uuid_copy (snap->snap_id, *snap_id);
        snap->time_stamp = (time_t)time_stamp;
        /* Set the status as GD_SNAP_STATUS_INIT and once the backend snapshot
           is taken and snap is really ready to use, set the status to
           GD_SNAP_STATUS_IN_USE. This helps in identifying the incomplete
           snapshots and cleaning them up.
        */
        snap->snap_status = GD_SNAP_STATUS_INIT;
        if (description) {
                snap->description = gf_strdup (description);
                if (snap->description == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Saving the Snap Description Failed");
                        ret = -1;
                        goto out;
                 }
        }

        ret = glusterd_store_snap (snap);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Could not store snap"
                        "object %s", snap->snapname);
                goto out;
        }

        list_add_order (&snap->snap_list, &priv->snapshots,
                        glusterd_compare_snap_time);

        gf_log (this->name, GF_LOG_TRACE, "Snap %s added to the list",
                snap->snapname);

        ret = 0;

out:
        if (ret) {
                if (snap)
                        glusterd_snap_remove (rsp_dict, snap,
                                              _gf_true, _gf_true);
                snap = NULL;
        }

        return snap;
}

/* Added missed_snap_entry to rsp_dict */
int32_t
glusterd_add_missed_snaps_to_dict (dict_t *rsp_dict, char *snap_uuid,
                                   glusterd_brickinfo_t *brickinfo,
                                   int32_t brick_number, int32_t op)
{
        char                   *buf                             = NULL;
        char                    missed_snap_entry[PATH_MAX]     = "";
        char                    name_buf[PATH_MAX]              = "";
        int32_t                 missed_snap_count               = -1;
        int32_t                 ret                             = -1;
        xlator_t               *this                            = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (snap_uuid);
        GF_ASSERT (brickinfo);

        snprintf (missed_snap_entry, sizeof(missed_snap_entry),
                  "%s:%s=%d:%s:%d:%d", uuid_utoa(brickinfo->uuid),
                  snap_uuid, brick_number, brickinfo->path, op,
                  GD_MISSED_SNAP_PENDING);

        buf = gf_strdup (missed_snap_entry);
        if (!buf) {
                ret = -1;
                goto out;
        }

        /* Fetch the missed_snap_count from the dict */
        ret = dict_get_int32 (rsp_dict, "missed_snap_count",
                              &missed_snap_count);
        if (ret) {
                /* Initialize the missed_snap_count for the first time */
                missed_snap_count = 0;
        }

        /* Setting the missed_snap_entry in the rsp_dict */
        snprintf (name_buf, sizeof(name_buf), "missed_snaps_%d",
                  missed_snap_count);
        ret = dict_set_dynstr (rsp_dict, name_buf, buf);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set missed_snap_entry (%s) "
                        "in the rsp_dict.", buf);
                GF_FREE (buf);
                goto out;
        }
        missed_snap_count++;

        /* Setting the new missed_snap_count in the dict */
        ret = dict_set_int32 (rsp_dict, "missed_snap_count",
                              missed_snap_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set missed_snap_count for %s "
                        "in the rsp_dict.", missed_snap_entry);
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

/* This function is called to get the device path of the snap lvm. Usually
   if /dev/mapper/<group-name>-<lvm-name> is the device for the lvm,
   then the snap device will be /dev/<group-name>/<snapname>.
   This function takes care of building the path for the snap device.
*/
char *
glusterd_build_snap_device_path (char *device, char *snapname)
{
        char        snap[PATH_MAX]      = "";
        char        msg[1024]           = "";
        char        volgroup[PATH_MAX]  = "";
        char       *snap_device         = NULL;
        xlator_t   *this                = NULL;
        runner_t    runner              = {0,};
        char       *ptr                 = NULL;
        int         ret                 = -1;

        this = THIS;
        GF_ASSERT (this);
        if (!device) {
                gf_log (this->name, GF_LOG_ERROR, "device is NULL");
                goto out;
        }
        if (!snapname) {
                gf_log (this->name, GF_LOG_ERROR, "snapname is NULL");
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, "/sbin/lvs", "--noheadings", "-o", "vg_name",
                         device, NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        snprintf (msg, sizeof (msg), "Get volume group for device %s", device);
        runner_log (&runner, this->name, GF_LOG_DEBUG, msg);
        ret = runner_start (&runner);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get volume group "
                        "for device %s", device);
                runner_end (&runner);
                goto out;
        }
        ptr = fgets(volgroup, sizeof(volgroup),
                    runner_chio (&runner, STDOUT_FILENO));
        if (!ptr || !strlen(volgroup)) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get volume group "
                        "for snap %s", snapname);
                runner_end (&runner);
                ret = -1;
                goto out;
        }
        runner_end (&runner);

        snprintf (snap, sizeof(snap), "/dev/%s/%s", gf_trim(volgroup),
                  snapname);
        snap_device = gf_strdup (snap);
        if (!snap_device) {
                gf_log (this->name, GF_LOG_WARNING, "Cannot copy the "
                        "snapshot device name for snapname: %s)", snapname);
        }

out:
        return snap_device;
}

/* This function actually calls the command (or the API) for taking the
   snapshot of the backend brick filesystem. If this is successful,
   then call the glusterd_snap_create function to create the snap object
   for glusterd
*/
char *
glusterd_take_lvm_snapshot (glusterd_volinfo_t *snap_vol,
                            glusterd_brickinfo_t *brickinfo)
{
        char             msg[NAME_MAX]    = "";
        char             buf[PATH_MAX]    = "";
        char            *snap_device      = NULL;
        char            *ptr              = NULL;
        char            *device           = NULL;
        int              ret              = -1;
        gf_boolean_t     match            = _gf_false;
        runner_t         runner           = {0,};
        xlator_t        *this             = NULL;

        this = THIS;

        if (!brickinfo) {
                gf_log (this->name, GF_LOG_ERROR, "brickinfo NULL");
                goto out;
        }

        device = glusterd_get_brick_mount_details (brickinfo);
        if (!device) {
                gf_log (this->name, GF_LOG_ERROR, "getting device name for "
                        "the brick %s:%s failed", brickinfo->hostname,
                        brickinfo->path);
                goto out;
        }

        /* Figuring out if setactivationskip flag is supported or not */
        runinit (&runner);
        snprintf (msg, sizeof (msg), "running lvcreate help");
        runner_add_args (&runner, LVM_CREATE, "--help", NULL);
        runner_log (&runner, "", GF_LOG_DEBUG, msg);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        ret = runner_start (&runner);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to run lvcreate help");
                runner_end (&runner);
                goto out;
        }

        /* Looking for setactivationskip in lvcreate --help */
        do {
                ptr = fgets(buf, sizeof(buf),
                            runner_chio (&runner, STDOUT_FILENO));
                if (ptr) {
                        if (strstr(buf, "setactivationskip")) {
                                match = _gf_true;
                                break;
                        }
                }
        } while (ptr != NULL);
        runner_end (&runner);

        /* Takng the actual snapshot */
        runinit (&runner);
        snprintf (msg, sizeof (msg), "taking snapshot of the brick %s:%s",
                  brickinfo->hostname, brickinfo->path);
        if (match == _gf_true)
                runner_add_args (&runner, LVM_CREATE, "-s", device,
                                 "--setactivationskip", "n", "--name",
                                 snap_vol->volname, NULL);
        else
                runner_add_args (&runner, LVM_CREATE, "-s", device,
                                 "--name", snap_vol->volname, NULL);
        runner_log (&runner, "", GF_LOG_DEBUG, msg);
        ret = runner_start (&runner);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "taking snapshot of the "
                        "brick (%s:%s) of device %s failed",
                        brickinfo->hostname, brickinfo->path, device);
                runner_end (&runner);
                goto out;
        }
        runner_end (&runner);

        snap_device = glusterd_build_snap_device_path (device,
                                                       snap_vol->volname);
        if (!snap_device) {
                gf_log (this->name, GF_LOG_WARNING, "Cannot copy the snapshot "
                        "device name for snap %s (volume id: %s)",
                        snap_vol->snapshot->snapname, snap_vol->volname);
                ret = -1;
                goto out;
        }

out:
        return snap_device;
}

int32_t
glusterd_snap_brick_create (char *device, glusterd_volinfo_t *snap_volinfo,
                            glusterd_brickinfo_t *original_brickinfo,
                            int32_t brick_count, char *snap_brick_dir)
{
        int32_t          ret                             = -1;
        xlator_t        *this                            = NULL;
        glusterd_conf_t *priv                            = NULL;
        char             snap_brick_mount_path[PATH_MAX] = "";
        char             snap_brick_path[PATH_MAX]       = "";
        char             msg[1024]                       = "";
        struct stat      statbuf                         = {0, };
        runner_t         runner                          = {0, };

        this = THIS;
        priv = this->private;

        GF_ASSERT (device);
        GF_ASSERT (snap_volinfo);
        GF_ASSERT (original_brickinfo);
        GF_ASSERT (snap_brick_dir);

        snprintf (snap_brick_mount_path, sizeof (snap_brick_mount_path),
                  "%s/%s/brick%d",  snap_mount_folder, snap_volinfo->volname,
                  brick_count+1);

        snprintf (snap_brick_path, sizeof (snap_brick_path), "%s%s",
                  snap_brick_mount_path, snap_brick_dir);

        ret = mkdir_p (snap_brick_mount_path, 0777, _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "creating the brick directory"
                        " %s for the snapshot %s(device: %s) failed",
                        snap_brick_mount_path, snap_volinfo->volname, device);
                goto out;
        }
        /* mount the snap logical device on the directory inside
           /run/gluster/snaps/<snapname>/@snap_brick_mount_path
           Way to mount the snap brick via mount api is this.
           ret = mount (device, snap_brick_mount_path, entry->mnt_type,
                        MS_MGC_VAL, "nouuid");
           But for now, mounting using runner apis.
        */
        runinit (&runner);
        snprintf (msg, sizeof (msg), "mounting snapshot of the brick %s:%s",
                  original_brickinfo->hostname, original_brickinfo->path);
        runner_add_args (&runner, "mount", "-o", "nouuid", device,
                         snap_brick_mount_path, NULL);
        runner_log (&runner, "", GF_LOG_DEBUG, msg);

        /* let glusterd get blocked till snapshot is over */
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "mounting the snapshot "
                        "logical device %s failed (error: %s)", device,
                        strerror (errno));
                goto out;
        } else
                gf_log (this->name, GF_LOG_DEBUG, "mounting the snapshot "
                        "logical device %s successful", device);

        ret = stat (snap_brick_path, &statbuf);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "stat of the brick %s"
                        "(brick mount: %s) failed (%s)", snap_brick_path,
                        snap_brick_mount_path, strerror (errno));
                goto out;
        }
        ret = sys_lsetxattr (snap_brick_path,
                             GF_XATTR_VOL_ID_KEY,
                             snap_volinfo->volume_id, 16,
                             XATTR_REPLACE);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "extended attribute %s on %s. Reason: "
                        "%s, snap: %s", GF_XATTR_VOL_ID_KEY,
                        snap_brick_path, strerror (errno),
                        snap_volinfo->volname);
                goto out;
        }

out:
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "unmounting the snap brick"
                        " mount %s", snap_brick_mount_path);
#if !defined(GF_DARWIN_HOST_OS)
                umount (snap_brick_mount_path);
#else
		unmount (snap_brick_mount_path, 0);
#endif
        }

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

static int32_t
glusterd_add_bricks_to_snap_volume (dict_t *dict, dict_t *rsp_dict,
                                    glusterd_volinfo_t  *snap_vol,
                                    glusterd_brickinfo_t *original_brickinfo,
                                    glusterd_brickinfo_t *snap_brickinfo,
                                    char **snap_brick_dir, int64_t volcount,
                                    int32_t brick_count)
{
        char                    key[PATH_MAX]                   = "";
        char                    snap_brick_path[PATH_MAX]       = "";
        char                   *snap_device                     = NULL;
        gf_boolean_t            add_missed_snap                 = _gf_false;
        int32_t                 ret                             = -1;
        xlator_t               *this                            = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (snap_vol);
        GF_ASSERT (original_brickinfo);
        GF_ASSERT (snap_brickinfo);
        GF_ASSERT (snap_brick_dir);

        snprintf (key, sizeof(key) - 1, "vol%"PRId64".brickdir%d", volcount,
                  brick_count);
        ret = dict_get_ptr (dict, key, (void **)snap_brick_dir);
        if (ret) {
                /* Using original brickinfo here because it will be a
                 * pending snapshot and storing the original brickinfo
                 * will help in mapping while recreating the missed snapshot
                 */
                gf_log (this->name, GF_LOG_WARNING, "Unable to fetch "
                        "snap mount path (%s). Using original brickinfo", key);
                snap_brickinfo->snap_status = -1;
                strcpy (snap_brick_path, original_brickinfo->path);

                /* In origiator node add snaps missed
                 * from different nodes to the dict
                 */
                if (is_origin_glusterd (dict) == _gf_true)
                        add_missed_snap = _gf_true;
        } else {
                /* Create brick-path in the format /var/run/gluster/snaps/ *
                 * <snap-uuid>/<original-brick#>/snap-brick-dir *
                 */
                snprintf (snap_brick_path, sizeof(snap_brick_path),
                          "%s/%s/brick%d%s", snap_mount_folder,
                          snap_vol->volname, brick_count+1,
                          *snap_brick_dir);
        }

        if ((snap_brickinfo->snap_status != -1) &&
            (!uuid_compare (original_brickinfo->uuid, MY_UUID)) &&
            (!glusterd_is_brick_started (original_brickinfo))) {
                /* In case if the brick goes down after prevalidate. */
                gf_log (this->name, GF_LOG_WARNING, "brick %s:%s is not"
                        " started (snap: %s)",
                        original_brickinfo->hostname,
                        original_brickinfo->path,
                        snap_vol->snapshot->snapname);

                snap_brickinfo->snap_status = -1;
                strcpy (snap_brick_path, original_brickinfo->path);
                add_missed_snap = _gf_true;
        }

        if (add_missed_snap) {
                ret = glusterd_add_missed_snaps_to_dict (rsp_dict,
                                            snap_vol->volname,
                                            original_brickinfo,
                                            brick_count + 1,
                                            GF_SNAP_OPTION_TYPE_CREATE);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to add missed"
                                " snapshot info for %s:%s in the rsp_dict",
                                original_brickinfo->hostname,
                                original_brickinfo->path);
                        goto out;
                }
        }

        snprintf (key, sizeof(key), "vol%"PRId64".brick_snapdevice%d",
                  volcount, brick_count);
        ret = dict_get_ptr (dict, key, (void **)&snap_device);
        if (ret) {
                /* If the device name is empty, so will be the brick path
                 * Hence the missed snap has already been added above
                 */
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch "
                        "snap device (%s). Leaving empty", key);
        } else
                strcpy (snap_brickinfo->device_path, snap_device);

        ret = gf_canonicalize_path (snap_brick_path);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to canonicalize path");
                goto out;
        }

        strcpy (snap_brickinfo->hostname, original_brickinfo->hostname);
        strcpy (snap_brickinfo->path, snap_brick_path);
        uuid_copy (snap_brickinfo->uuid, original_brickinfo->uuid);
        /* AFR changelog names are based on brick_id and hence the snap
         * volume's bricks must retain the same ID */
        strcpy (snap_brickinfo->brick_id, original_brickinfo->brick_id);
        list_add_tail (&snap_brickinfo->brick_list, &snap_vol->bricks);

out:
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

static int32_t
glusterd_take_brick_snapshot (glusterd_volinfo_t *origin_vol,
                              glusterd_volinfo_t *snap_vol, dict_t *rsp_dict,
                              glusterd_brickinfo_t *original_brickinfo,
                              glusterd_brickinfo_t *snap_brickinfo,
                              char *snap_brick_dir, int32_t brick_count)
{
        char                   *device                          = NULL;
        int32_t                 ret                             = -1;
        xlator_t               *this                            = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (origin_vol);
        GF_ASSERT (snap_vol);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (original_brickinfo);
        GF_ASSERT (snap_brickinfo);
        GF_ASSERT (snap_brick_dir);

        device = glusterd_take_lvm_snapshot (snap_vol, original_brickinfo);
        /* Fail the snapshot even though snapshot on one of
           the bricks fails. At the end when we check whether
           the snapshot volume meets quorum or not, then the
           the snapshot can either be treated as success, or
           in case of failure we can undo the changes and return
           failure to cli.  */
        if (!device) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to take snapshot of %s:%s",
                        original_brickinfo->hostname,
                        original_brickinfo->path);
                goto out;
        }

        /* create the complete brick here */
        ret = glusterd_snap_brick_create (device, snap_vol,
                                          original_brickinfo,
                                          brick_count, snap_brick_dir);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "not able to"
                        " create the brickinfo for the snap %s"
                        ", volume %s", snap_vol->snapshot->snapname,
                        origin_vol->volname);
                goto out;
        }

out:
        if (device)
                GF_FREE (device);

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

/* Look for disconnected peers, for missed snap creates or deletes */
static int32_t
glusterd_find_missed_snap (dict_t *rsp_dict, glusterd_volinfo_t *vol,
                           char *snap_uuid, struct list_head *peers,
                           int32_t op)
{
        int32_t                   brick_count          = -1;
        int32_t                   ret                  = -1;
        xlator_t                 *this                 = NULL;
        glusterd_peerinfo_t      *peerinfo             = NULL;
        glusterd_brickinfo_t     *brickinfo            = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (peers);
        GF_ASSERT (vol);
        GF_ASSERT (snap_uuid);

        brick_count = 0;
        list_for_each_entry (brickinfo, &vol->bricks, brick_list) {
                if (!uuid_compare (brickinfo->uuid, MY_UUID)) {
                        /* If the brick belongs to the same node */
                        brick_count++;
                        continue;
                }

                list_for_each_entry (peerinfo, peers, uuid_list) {
                        if (uuid_compare (peerinfo->uuid, brickinfo->uuid)) {
                                /* If the brick doesnt belong to this peer */
                                continue;
                        }

                        /* Found peer who owns the brick,    *
                         * if peer is not connected or not   *
                         * friend add it to missed snap list */
                        if (!(peerinfo->connected) ||
                           (peerinfo->state.state !=
                             GD_FRIEND_STATE_BEFRIENDED)) {
                                ret = glusterd_add_missed_snaps_to_dict
                                                   (rsp_dict,
                                                    snap_uuid,
                                                    brickinfo,
                                                    brick_count + 1,
                                                    op);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to add missed snapshot "
                                                "info for %s:%s in the "
                                                "rsp_dict", brickinfo->hostname,
                                                brickinfo->path);
                                        goto out;
                                }
                        }
                }
                brick_count++;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

glusterd_volinfo_t *
glusterd_do_snap_vol (glusterd_volinfo_t *origin_vol, glusterd_snap_t *snap,
                      dict_t *dict, dict_t *rsp_dict, int64_t volcount)
{
        char                    key[PATH_MAX]                   = "";
        char                   *snap_brick_dir                  = NULL;
        char                   *username                        = NULL;
        char                   *password                        = NULL;
        glusterd_brickinfo_t   *brickinfo                       = NULL;
        glusterd_conf_t        *priv                            = NULL;
        glusterd_volinfo_t     *snap_vol                        = NULL;
        uuid_t                 *snap_volid                      = NULL;
        int32_t                 ret                             = -1;
        int32_t                 brick_count                     = 0;
        glusterd_brickinfo_t   *snap_brickinfo                  = NULL;
        xlator_t               *this                            = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (dict);
        GF_ASSERT (origin_vol);
        GF_ASSERT (rsp_dict);

        /* fetch username, password and vol_id from dict*/
        snprintf (key, sizeof(key), "volume%"PRId64"_username", volcount);
        ret = dict_get_str (dict, key, &username);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get %s for "
                        "snap %s", key, snap->snapname);
                goto out;
        }

        snprintf (key, sizeof(key), "volume%"PRId64"_password", volcount);
        ret = dict_get_str (dict, key, &password);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get %s for "
                        "snap %s", key, snap->snapname);
                goto out;
        }

        snprintf (key, sizeof(key) - 1, "vol%"PRId64"_volid", volcount);
        ret = dict_get_bin (dict, key, (void **)&snap_volid);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to fetch snap_volid");
                goto out;
        }

        /* We are not setting the username and password here as
         * we need to set the user name and password passed in
         * the dictionary
         */
        ret = glusterd_volinfo_dup (origin_vol, &snap_vol, _gf_false);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to duplicate volinfo "
                        "for the snapshot %s", snap->snapname);
                goto out;
        }

        /* uuid is used as lvm snapshot name.
           This will avoid restrictions on snapshot names provided by user */
        GLUSTERD_GET_UUID_NOHYPHEN (snap_vol->volname, *snap_volid);
        uuid_copy (snap_vol->volume_id, *snap_volid);
        snap_vol->is_snap_volume = _gf_true;
        strcpy (snap_vol->parent_volname, origin_vol->volname);
        snap_vol->snapshot = snap;

        glusterd_auth_set_username (snap_vol, username);
        glusterd_auth_set_password (snap_vol, password);

        /* Adding snap brickinfos to the snap volinfo */
        brick_count = 0;
        list_for_each_entry (brickinfo, &origin_vol->bricks, brick_list) {
                snap_brickinfo = NULL;

                ret = glusterd_brickinfo_new (&snap_brickinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "initializing the brick for the snap "
                                "volume failed (snapname: %s)", snap->snapname);
                        goto out;
                }

                ret = glusterd_add_bricks_to_snap_volume (dict, rsp_dict,
                                                          snap_vol,
                                                          brickinfo,
                                                          snap_brickinfo,
                                                          &snap_brick_dir,
                                                          volcount,
                                                          brick_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to add the snap brick for "
                                "%s:%s to the snap volume",
                                brickinfo->hostname, brickinfo->path);
                        GF_FREE (snap_brickinfo);
                        goto out;
                }

                /* Take snapshot of the brick */
                if ((uuid_compare (brickinfo->uuid, MY_UUID)) ||
                    (snap_brickinfo->snap_status == -1)) {
                        brick_count++;
                        continue;
                }

                ret = glusterd_take_brick_snapshot (origin_vol, snap_vol,
                                                    rsp_dict, brickinfo,
                                                    snap_brickinfo,
                                                    snap_brick_dir,
                                                    brick_count);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to take snapshot for %s:%s",
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }
                brick_count++;
        }

        /*TODO: the quorum check of the snap volume here */

        ret = glusterd_store_volinfo (snap_vol,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to store snapshot "
                        "volinfo (%s) for snap %s", snap_vol->volname,
                        snap->snapname);
                goto out;
        }

        ret = generate_brick_volfiles (snap_vol);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "generating the brick "
                        "volfiles for the snap %s (volume: %s) failed",
                        snap->snapname, origin_vol->volname);
                goto out;
        }

        ret = generate_client_volfiles (snap_vol, GF_CLIENT_TRUSTED);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "generating the trusted "
                        "client volfiles for the snap %s (volume: %s) failed",
                        snap->snapname, origin_vol->volname);
                goto out;
        }

        ret = generate_client_volfiles (snap_vol, GF_CLIENT_OTHER);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "generating the client "
                        "volfiles for the snap %s (volume: %s) failed",
                        snap->snapname, origin_vol->volname);
                goto out;
        }

        ret = glusterd_list_add_snapvol (origin_vol, snap_vol);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "could not add the snap "
                        "volume %s to the list", snap_vol->volname);
                goto out;
        }

        list_for_each_entry (brickinfo, &snap_vol->bricks, brick_list) {
                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                if (brickinfo->snap_status == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "not starting snap brick %s:%s for "
                                "for the snap %s (volume: %s)",
                                brickinfo->hostname, brickinfo->path,
                                snap->snapname, origin_vol->volname);
                        continue;
                }

                ret = glusterd_brick_start (snap_vol, brickinfo, _gf_true);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "starting the "
                                "brick %s:%s for the snap %s (volume: %s) "
                                "failed", brickinfo->hostname, brickinfo->path,
                                snap->snapname, origin_vol->volname);
                        goto out;
                }
        }

        snap_vol->status = GLUSTERD_STATUS_STARTED;
        ret = glusterd_store_volinfo (snap_vol,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to store snap volinfo");
                goto out;
        }

out:
        if (ret) {
                if (snap_vol)
                        glusterd_snap_volume_remove (rsp_dict, snap_vol,
                                                     _gf_true, _gf_true);
                snap_vol = NULL;
        }

        return snap_vol;
}

/* This is a snapshot remove handler function. This function will be
 * executed in the originator node. This function is responsible for
 * calling mgmt v3 framework to do the actual remove on all the bricks
 *
 * @param req           RPC request object
 * @param op            gluster operation
 * @param dict          dictionary containing snapshot remove request
 * @param err_str       In case of an err this string should be populated
 * @param len           length of err_str buffer
 *
 * @return              Negative value on Failure and 0 in success
 */
int
glusterd_handle_snapshot_remove (rpcsvc_request_t *req, glusterd_op_t op,
                                 dict_t *dict, char *err_str, size_t len)
{
        int                      ret            = -1;
        int64_t                  volcount       = 0;
        char                    *snapname       = NULL;
        char                    *volname        = NULL;
        char                     key[PATH_MAX]  = "";
        glusterd_snap_t         *snap           = NULL;
        glusterd_volinfo_t      *snap_vol       = NULL;
        glusterd_volinfo_t      *tmp            = NULL;
        xlator_t                *this           = NULL;

        this = THIS;

        GF_ASSERT (req);
        GF_ASSERT (dict);
        GF_ASSERT (err_str);

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get snapname");
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (!snap) {
                snprintf (err_str, len, "Snap (%s) does not exist", snapname);
                gf_log (this->name, GF_LOG_ERROR,
                        "%s", err_str);
                ret = -1;
                goto out;
        }

        /* Set volnames in the dict to get mgmt_v3 lock */
        list_for_each_entry_safe (snap_vol, tmp, &snap->volumes, vol_list) {
                volcount++;
                volname = gf_strdup (snap_vol->parent_volname);
                if (!volname) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR, "strdup failed");
                        goto out;
                }

                snprintf (key, sizeof (key), "volname%"PRId64, volcount);
                ret = dict_set_dynstr (dict, key, volname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                                "volume name in dictionary");
                        GF_FREE (volname);
                        goto out;
                }
                volname = NULL;
        }
        ret = dict_set_int64 (dict, "volcount", volcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set volcount");
                goto out;
        }

        ret = glusterd_mgmt_v3_initiate_snap_phases (req, op, dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to initiate snap "
                        "phases");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_snapshot_remove_prevalidate (dict_t *dict, char **op_errstr,
                                      dict_t *rsp_dict)
{
        int32_t             ret         = -1;
        char               *snapname    = NULL;
        xlator_t           *this        = NULL;
        glusterd_snap_t    *snap        = NULL;

        this = THIS;

        if (!dict || !op_errstr) {
                gf_log (this->name, GF_LOG_ERROR, "input parameters NULL");
                goto out;
        }

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Getting the snap name "
                        "failed");
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (!snap) {
                gf_log (this->name, GF_LOG_ERROR, "Snap %s does not exist",
                        snapname);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_snapshot_status_prevalidate (dict_t *dict, char **op_errstr,
                                      dict_t *rsp_dict)
{
        int                     ret             =       -1;
        char                    *snapname       =       NULL;
        glusterd_conf_t         *conf           =       NULL;
        xlator_t                *this           =       NULL;
        int32_t                 cmd             =       -1;
        glusterd_volinfo_t      *volinfo        =       NULL;
        char                    *volname        =       NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        GF_ASSERT (conf);
        GF_ASSERT (op_errstr);
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "Input dict is NULL");
                goto out;
        }

        ret = dict_get_int32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not fetch status cmd");
                goto out;
        }

        switch (cmd) {
                case GF_SNAP_STATUS_TYPE_ALL:
                {
                        break;
                }
                case GF_SNAP_STATUS_TYPE_SNAP:
                {
                        ret = dict_get_str (dict, "snapname", &snapname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not fetch snapname");
                                goto out;
                        }

                        if (!glusterd_find_snap_by_name (snapname)) {
                                ret = gf_asprintf (op_errstr, "Snap (%s) "
                                                  "not found", snapname);
                                if (ret < 0) {
                                        goto out;
                                }
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR, "Snap (%s) "
                                        "not found", snapname);
                                goto out;
                        }
                        break;
                }
                case GF_SNAP_STATUS_TYPE_VOL:
                {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not fetch volname");
                                goto out;
                        }

                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                ret = gf_asprintf (op_errstr, "Volume (%s)"
                                                  "not found", volname);
                                if (ret < 0) {
                                        goto out;
                                }
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR, "Volume "
                                        "%s not present", volname);
                                goto out;
                        }
                        break;

                }
                default:
                {
                        gf_log (this->name, GF_LOG_ERROR, "Invalid command");
                        break;
                }
        }
        ret = 0;

out:
        return ret;
}

int32_t
glusterd_snapshot_remove_commit (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        int32_t                   ret                  = -1;
        char                     *snapname             = NULL;
        char                     *dup_snapname         = NULL;
        glusterd_snap_t          *snap                 = NULL;
        glusterd_conf_t          *priv                 = NULL;
        glusterd_volinfo_t       *snap_volinfo         = NULL;
        xlator_t                 *this                 = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (op_errstr);

        priv = this->private;
        GF_ASSERT (priv);

        if (!dict || !op_errstr) {
                gf_log (this->name, GF_LOG_ERROR, "input parameters NULL");
                goto out;
        }

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Getting the snap name "
                        "failed");
                goto out;
        }

        snap = glusterd_find_snap_by_name (snapname);
        if (!snap) {
                gf_log (this->name, GF_LOG_ERROR, "Snap %s does not exist",
                        snapname);
                ret = -1;
                goto out;
        }

        if (is_origin_glusterd (dict) == _gf_true) {
                /* TODO : As of now there is only volume in snapshot.
                 * Change this when multiple volume snapshot is introduced
                 */
                snap_volinfo = list_entry (snap->volumes.next,
                                           glusterd_volinfo_t,
                                           vol_list);
                if (!snap_volinfo) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to fetch snap_volinfo");
                        ret = -1;
                        goto out;
                }

                /* From origin glusterd check if      *
                 * any peers with snap bricks is down */
                ret = glusterd_find_missed_snap (rsp_dict, snap_volinfo,
                                                 snap_volinfo->volname,
                                                 &priv->peers,
                                                 GF_SNAP_OPTION_TYPE_DELETE);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to find missed snap deletes");
                        goto out;
                }
        }

        ret = glusterd_snap_remove (rsp_dict, snap, _gf_true, _gf_false);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to remove snap %s",
                        snapname);
                goto out;
        }

        dup_snapname = gf_strdup (snapname);
        if (!dup_snapname) {
                gf_log (this->name, GF_LOG_ERROR, "Strdup failed");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (rsp_dict, "snapname", dup_snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set the snapname");
                GF_FREE (dup_snapname);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_do_snap_cleanup (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int32_t                  ret                   = -1;
        char                     *name                 = NULL;
        xlator_t                 *this                 = NULL;
        glusterd_conf_t          *conf                 = NULL;
        glusterd_volinfo_t       *volinfo              = NULL;
        glusterd_snap_t          *snap                 = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if (!dict || !op_errstr) {
                gf_log (this->name, GF_LOG_ERROR, "input parameters NULL");
                goto out;
        }

        ret = dict_get_str (dict, "snapname", &name);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "getting the snap "
                        "name failed (volume: %s)", volinfo->volname);
                goto out;
        }

        /*
          If the snapname is not found that means the failure happened at
          staging, or in commit, before the snap object is created, in which
          case there is nothing to cleanup. So set ret to 0.
        */
        snap = glusterd_find_snap_by_name (name);
        if (!snap) {
                gf_log (this->name, GF_LOG_INFO, "snap %s is not found", name);
                ret = 0;
                goto out;
        }

        ret = glusterd_snap_remove (rsp_dict, snap, _gf_true, _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "removing the snap %s failed",
                        name);
                goto out;
        }

        name = NULL;

        ret = 0;

out:

        return ret;
}

/* In case of a successful, delete or create operation, during post_validate *
 * look for missed snap operations and update the missed snap lists */
int32_t
glusterd_snapshot_update_snaps_post_validate (dict_t *dict, char **op_errstr,
                                              dict_t *rsp_dict)
{
        int32_t                  ret                    = -1;
        int32_t                  missed_snap_count      = -1;
        xlator_t                *this                   = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (op_errstr);

        ret = dict_get_int32 (dict, "missed_snap_count",
                              &missed_snap_count);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "No missed snaps");
                ret = 0;
                goto out;
        }

        ret = glusterd_store_update_missed_snaps (dict, missed_snap_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to update missed_snaps_list");
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_snapshot_create_commit (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        int                     ret                     = -1;
        int64_t                 i                       = 0;
        int64_t                 volcount                = 0;
        char                    *snapname               = NULL;
        char                    *volname                = NULL;
        char                    *tmp_name               = NULL;
        char                    key[PATH_MAX]           = "";
        xlator_t                *this                   = NULL;
        glusterd_snap_t         *snap                   = NULL;
        glusterd_volinfo_t      *origin_vol             = NULL;
        glusterd_volinfo_t      *snap_vol               = NULL;
        glusterd_conf_t *priv   = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT(dict);
        GF_ASSERT(op_errstr);
        GF_ASSERT(rsp_dict);
        priv = this->private;
        GF_ASSERT(priv);

        ret = dict_get_int64 (dict, "volcount", &volcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "get the volume count");
                goto out;
        }

        ret = dict_get_str (dict, "snapname", &snapname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch snapname");
                goto out;
        }
        tmp_name = gf_strdup (snapname);
        if (!tmp_name) {
                gf_log (this->name, GF_LOG_ERROR, "Out of memory");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (rsp_dict, "snapname", tmp_name);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to set snapname in rsp_dict");
                GF_FREE (tmp_name);
                goto out;
        }
        tmp_name = NULL;

        snap = glusterd_create_snap_object (dict, rsp_dict);
        if (!snap) {
                gf_log (this->name, GF_LOG_ERROR, "creating the"
                        "snap object %s failed", snapname);
                ret = -1;
                goto out;
        }

        for (i = 1; i <= volcount; i++) {
                snprintf (key, sizeof (key), "volname%"PRId64, i);
                ret = dict_get_str (dict, key, &volname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to get volume name");
                        goto out;
                }

                ret = glusterd_volinfo_find (volname, &origin_vol);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to get the volinfo for "
                                "the volume %s", volname);
                        goto out;
                }

                /* TODO: Create a stub where the bricks are
                   added parallely by worker threads so that
                   the snap creating happens parallely.  */
                snap_vol = glusterd_do_snap_vol (origin_vol, snap, dict,
                                                 rsp_dict, i);
                if (!snap_vol) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_WARNING, "taking the "
                                "snapshot of the volume %s failed", volname);
                        goto out;
                }
        }

        snap->snap_status = GD_SNAP_STATUS_IN_USE;
        ret = glusterd_store_snap (snap);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Could not store snap"
                        "object %s", snap->snapname);
                goto out;
        }

        ret = 0;

out:
        if (ret) {
                if (snap)
                        glusterd_snap_remove (rsp_dict, snap,
                                              _gf_true, _gf_true);
                snap = NULL;
        }

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
snap_max_hard_limit_set_commit (dict_t *dict, uint64_t value,
                                char *volname, char **op_errstr)
{
        char                err_str[PATH_MAX]    = "";
        glusterd_conf_t    *conf                 = NULL;
        glusterd_volinfo_t *volinfo              = NULL;
        int                 ret                  = -1;
        xlator_t           *this                 = NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (volname);
        GF_ASSERT (op_errstr);

        conf = this->private;

        GF_ASSERT (conf);

        /* TODO: Initiate auto deletion when there is a limit change */
        if (!volname) {
                /* For system limit */
                conf->snap_max_hard_limit = value;

                ret = glusterd_store_global_info (this);
                if (ret) {
                        snprintf (err_str, PATH_MAX, "Failed to store "
                                  "snap-max-hard-limit for system");
                        goto out;
                }
        } else {
               /*  For one volume */
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        snprintf (err_str, PATH_MAX, "Failed to get the"
                                   " volinfo for volume %s", volname);
                        goto out;
                }

                volinfo->snap_max_hard_limit = value;

                ret = glusterd_store_volinfo (volinfo,
                                        GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        snprintf (err_str, PATH_MAX, "Failed to store "
                                 "snap-max-hard-limit for volume %s", volname);
                        goto out;
                }
        }

        ret = 0;
out:
        if (ret) {
                *op_errstr = gf_strdup (err_str);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
        }
        return ret;
}

int
snap_max_limits_display_commit (dict_t *rsp_dict, char *volname,
                                char **op_errstr)
{
        char                err_str[PATH_MAX]    = "";
        char                buf[PATH_MAX]        = "";
        glusterd_conf_t    *conf                 = NULL;
        glusterd_volinfo_t *volinfo              = NULL;
        int                 ret                  = -1;
        uint64_t            active_hard_limit    = 0;
        uint64_t            snap_max_limit       = 0;
        uint64_t            soft_limit_value     = -1;
        uint64_t            count                = 0;
        xlator_t           *this                 = NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (volname);
        GF_ASSERT (op_errstr);

        conf = this->private;

        GF_ASSERT (conf);

        if (!volname) {
                /* For system limit */
                list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                        if (volinfo->is_snap_volume == _gf_true)
                                continue;
                        snap_max_limit = volinfo->snap_max_hard_limit;
                        if (snap_max_limit > conf->snap_max_hard_limit)
                                active_hard_limit = conf->snap_max_hard_limit;
                        else
                                active_hard_limit = snap_max_limit;
                        soft_limit_value = (active_hard_limit *
                                            conf->snap_max_soft_limit) / 100;

                        snprintf (buf, sizeof(buf), "volume%"PRIu64"-volname", count);
                        ret = dict_set_str (rsp_dict, buf, volinfo->volname);
                        if (ret) {
                                snprintf (err_str, PATH_MAX,
                                          "Failed to set %s", buf);
                                goto out;
                        }

                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-snap-max-hard-limit", count);
                        ret = dict_set_uint64 (rsp_dict, buf, snap_max_limit);
                        if (ret) {
                                snprintf (err_str, PATH_MAX,
                                          "Failed to set %s", buf);
                                goto out;
                        }

                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-active-hard-limit", count);
                        ret = dict_set_uint64 (rsp_dict, buf,
                                               active_hard_limit);
                        if (ret) {
                                snprintf (err_str, PATH_MAX,
                                          "Failed to set %s", buf);
                                goto out;
                        }

                        snprintf (buf, sizeof(buf),
                                  "volume%"PRIu64"-snap-max-soft-limit", count);
                        ret = dict_set_uint64 (rsp_dict, buf, soft_limit_value);
                        if (ret) {
                                snprintf (err_str, PATH_MAX,
                                          "Failed to set %s", buf);
                                goto out;
                        }
                        count++;
                }

                ret = dict_set_uint64 (rsp_dict, "voldisplaycount", count);
                if (ret) {
                        snprintf (err_str, PATH_MAX,
                                  "Failed to set voldisplaycount");
                        goto out;
                }
        } else {
                /*  For one volume */
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        snprintf (err_str, PATH_MAX, "Failed to get the"
                                   " volinfo for volume %s", volname);
                        goto out;
                }

                snap_max_limit = volinfo->snap_max_hard_limit;
                if (snap_max_limit > conf->snap_max_hard_limit)
                        active_hard_limit = conf->snap_max_hard_limit;
                else
                        active_hard_limit = snap_max_limit;

                soft_limit_value = (active_hard_limit *
                                    conf->snap_max_soft_limit) / 100;

                snprintf (buf, sizeof(buf), "volume%"PRIu64"-volname", count);
                ret = dict_set_str (rsp_dict, buf, volinfo->volname);
                if (ret) {
                        snprintf (err_str, PATH_MAX,
                                  "Failed to set %s", buf);
                        goto out;
                }

                snprintf (buf, sizeof(buf),
                          "volume%"PRIu64"-snap-max-hard-limit", count);
                ret = dict_set_uint64 (rsp_dict, buf, snap_max_limit);
                if (ret) {
                        snprintf (err_str, PATH_MAX,
                                  "Failed to set %s", buf);
                        goto out;
                }

                snprintf (buf, sizeof(buf),
                          "volume%"PRIu64"-active-hard-limit", count);
                ret = dict_set_uint64 (rsp_dict, buf, active_hard_limit);
                if (ret) {
                        snprintf (err_str, PATH_MAX,
                                  "Failed to set %s", buf);
                        goto out;
                }

                snprintf (buf, sizeof(buf),
                          "volume%"PRIu64"-snap-max-soft-limit", count);
                ret = dict_set_uint64 (rsp_dict, buf, soft_limit_value);
                if (ret) {
                        snprintf (err_str, PATH_MAX,
                                  "Failed to set %s", buf);
                        goto out;
                }

                count++;

                ret = dict_set_uint64 (rsp_dict, "voldisplaycount", count);
                if (ret) {
                        snprintf (err_str, PATH_MAX,
                                  "Failed to set voldisplaycount");
                        goto out;
                }

        }

        ret = dict_set_uint64 (rsp_dict, "snap-max-hard-limit",
                               conf->snap_max_hard_limit);
        if (ret) {
                snprintf (err_str, PATH_MAX,
                          "Failed to set sys-snap-max-hard-limit ");
                goto out;
        }

        ret = dict_set_uint64 (rsp_dict, "snap-max-soft-limit",
                               conf->snap_max_soft_limit);
        if (ret) {
                snprintf (err_str, PATH_MAX,
                          "Failed to set sys-snap-max-hard-limit ");
                goto out;
        }

        ret = 0;
out:
        if (ret) {
                *op_errstr = gf_strdup (err_str);
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
        }
        return ret;
}

int
glusterd_snapshot_config_commit (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        char               *volname              = NULL;
        xlator_t           *this                 = NULL;
        int                 ret                  = -1;
        char                err_str[PATH_MAX]    = {0,};
        glusterd_conf_t    *conf                 = NULL;
        int                 config_command       = 0;
        uint64_t            hard_limit           = 0;
        uint64_t            soft_limit           = 0;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        conf = this->private;

        GF_ASSERT (conf);

        ret = dict_get_int32 (dict, "config-command", &config_command);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get config-command type");
                goto out;
        }

        /* Ignore the return value of the following dict_get,
         * as they are optional
         */
        ret = dict_get_str (dict, "volname", &volname);

        ret = dict_get_uint64 (dict, "snap-max-hard-limit", &hard_limit);

        ret = dict_get_uint64 (dict, "snap-max-soft-limit", &soft_limit);

        switch (config_command) {
        case GF_SNAP_CONFIG_TYPE_SET:
                if (hard_limit) {
                        /* Commit ops for snap-max-hard-limit */
                        ret = snap_max_hard_limit_set_commit (dict, hard_limit,
                                                              volname,
                                                              op_errstr);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "snap-max-hard-limit set "
                                        "commit failed.");
                                goto out;
                        }
                }

                if (soft_limit) {
                        /* For system limit */
                        conf->snap_max_soft_limit = soft_limit;

                        ret = glusterd_store_global_info (this);
                        if (ret) {
                                snprintf (err_str, PATH_MAX, "Failed to store "
                                          "snap-max-soft-limit for system");
                                *op_errstr = gf_strdup (err_str);
                                gf_log (this->name, GF_LOG_ERROR, "%s",
                                        err_str);
                                goto out;
                        }
                }
                break;

        case GF_SNAP_CONFIG_DISPLAY:
                /* Reading data from local node only */
                if (!is_origin_glusterd (dict)) {
                        ret = 0;
                        break;
                }

                ret = snap_max_limits_display_commit (rsp_dict, volname,
                                                      op_errstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "snap-max-limit "
                                "display commit failed.");
                        goto out;
                }
                break;
        default:
                break;
        }

out:
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_get_brick_lvm_details (dict_t *rsp_dict,
                               glusterd_brickinfo_t *brickinfo, char *volname,
                                char *device, char *key_prefix)
{

        int                     ret             =       -1;
        glusterd_conf_t         *priv           =       NULL;
        runner_t                runner          =       {0,};
        xlator_t                *this           =       NULL;
        char                    msg[PATH_MAX]   =       "";
        char                    buf[PATH_MAX]   =       "";
        char                    *ptr            =       NULL;
        char                    *token          =       NULL;
        char                    key[PATH_MAX]   =       "";
        char                    *value          =       NULL;

        GF_ASSERT (rsp_dict);
        GF_ASSERT (brickinfo);
        GF_ASSERT (volname);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        device = glusterd_get_brick_mount_details (brickinfo);
        if (!device) {
                gf_log (this->name, GF_LOG_ERROR, "Getting device name for "
                        "the brick %s:%s failed", brickinfo->hostname,
                         brickinfo->path);
                goto out;
        }
        runinit (&runner);
        snprintf (msg, sizeof (msg), "running lvs command, "
                  "for getting snap status");
        /* Using lvs command fetch the Volume Group name,
         * Percentage of data filled and Logical Volume size
         *
         * "-o" argument is used to get the desired information,
         * example : "lvs /dev/VolGroup/thin_vol -o vgname,lv_size",
         * will get us Volume Group name and Logical Volume size.
         *
         * Here separator used is ":",
         * for the above given command with separator ":",
         * The output will be "vgname:lvsize"
         */
        runner_add_args (&runner, LVS, device, "--noheading", "-o",
                         "vg_name,data_percent,lv_size",
                         "--separator", ":", NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        runner_log (&runner, "", GF_LOG_DEBUG, msg);
        ret = runner_start (&runner);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not perform lvs action");
                goto end;
        }
        do {
                ptr = fgets (buf, sizeof (buf),
                             runner_chio (&runner, STDOUT_FILENO));

                if (ptr == NULL)
                        break;
                token = strtok (buf, ":");
                if (token != NULL) {
                        while (token && token[0] == ' ')
                                token++;
                        if (!token) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Invalid vg entry");
                                goto end;
                        }
                        value = gf_strdup (token);
                        if (!value) {
                                ret = -1;
                                goto end;
                        }
                        ret = snprintf (key, sizeof (key), "%s.vgname",
                                        key_prefix);
                        if (ret < 0) {
                                goto end;
                        }

                        ret = dict_set_dynstr (rsp_dict, key, value);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not save vgname ");
                                goto end;
                        }
                }

                token = strtok (NULL, ":");
                if (token != NULL) {
                        value = gf_strdup (token);
                        if (!value) {
                                ret = -1;
                                goto end;
                        }
                        ret = snprintf (key, sizeof (key), "%s.data",
                                        key_prefix);
                        if (ret < 0) {
                                goto end;
                        }

                        ret = dict_set_dynstr (rsp_dict, key, value);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not save data percent ");
                                goto end;
                        }
                }
                token = strtok (NULL, ":");
                if (token != NULL) {
                        value = gf_strdup (token);
                        if (!value) {
                                ret = -1;
                                goto end;
                        }
                        ret = snprintf (key, sizeof (key), "%s.lvsize",
                                        key_prefix);
                        if (ret < 0) {
                                goto end;
                        }

                        ret = dict_set_dynstr (rsp_dict, key, value);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not save meta data percent ");
                                goto end;
                        }
                }

        } while (ptr != NULL);

        ret = 0;

end:
        runner_end (&runner);

out:
        if (ret && value) {
                GF_FREE (value);
        }

        return ret;
}

int
glusterd_get_single_brick_status (char **op_errstr, dict_t *rsp_dict,
                                 char *keyprefix, int index,
                                 glusterd_volinfo_t *snap_volinfo,
                                 glusterd_brickinfo_t *brickinfo)
{
        int             ret                     = -1;
        xlator_t        *this                   = NULL;
        glusterd_conf_t *priv                   = NULL;
        char            key[PATH_MAX]           = "";
        char            *device                 = NULL;
        char            *value                  = NULL;
        char            brick_path[PATH_MAX]    = "";
        char            pidfile[PATH_MAX]       = "";
        pid_t           pid                     = -1;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (keyprefix);
        GF_ASSERT (snap_volinfo);
        GF_ASSERT (brickinfo);

        ret = snprintf (key, sizeof (key), "%s.brick%d.path", keyprefix,
                        index);
        if (ret < 0) {
                goto out;
        }

        ret = snprintf (brick_path, sizeof (brick_path),
                        "%s:%s", brickinfo->hostname, brickinfo->path);
        if (ret < 0) {
                goto out;
        }

        value = gf_strdup (brick_path);
        if (!value) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (rsp_dict, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to store "
                        "brick_path %s", brickinfo->path);
                goto out;
        }

        if (brickinfo->snap_status == -1) {
                /* Setting vgname as "Pending Snapshot" */
                value = gf_strdup ("Pending Snapshot");
                if (!value) {
                        ret = -1;
                        goto out;
                }

                snprintf (key, sizeof (key), "%s.brick%d.vgname",
                          keyprefix, index);
                ret = dict_set_dynstr (rsp_dict, key, value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not save vgname ");
                        goto out;
                }

                ret = 0;
                goto out;
        }
        value = NULL;

        ret = snprintf (key, sizeof (key), "%s.brick%d.status",
                        keyprefix, index);
        if (ret < 0) {
                goto out;
        }

        if (brickinfo->status == GF_BRICK_STOPPED) {
                value = gf_strdup ("No");
                if (!value) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_str (rsp_dict, key, value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not save brick status");
                        goto out;
                }
                value = NULL;
        } else {
                value = gf_strdup ("Yes");
                if (!value) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_str (rsp_dict, key, value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not save brick status");
                        goto out;
                }
                value = NULL;

                GLUSTERD_GET_BRICK_PIDFILE (pidfile, snap_volinfo,
                                            brickinfo, priv);
                ret = gf_is_service_running (pidfile, &pid);

                ret = snprintf (key, sizeof (key), "%s.brick%d.pid",
                                keyprefix, index);
                if (ret < 0) {
                        goto out;
                }

                ret = dict_set_int32 (rsp_dict, key, pid);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not save pid %d", pid);
                        goto out;
                }
        }

        ret = snprintf (key, sizeof (key), "%s.brick%d",
                        keyprefix, index);
        if (ret < 0) {
                goto out;
        }

        ret = glusterd_get_brick_lvm_details (rsp_dict, brickinfo,
                                              snap_volinfo->volname,
                                              device, key);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                        "brick LVM details");
                goto out;
        }
out:
        if (ret && value) {
                GF_FREE (value);
        }

        return ret;
}

int
glusterd_get_single_snap_status (char **op_errstr, dict_t *rsp_dict,
                                 char *keyprefix, glusterd_snap_t *snap)
{
        int                      ret                 =       -1;
        xlator_t                *this                =       NULL;
        char                     key[PATH_MAX]       =       "";
        char                     brickkey[PATH_MAX]  =       "";
        glusterd_volinfo_t      *snap_volinfo        =       NULL;
        glusterd_volinfo_t      *tmp_volinfo         =       NULL;
        glusterd_brickinfo_t    *brickinfo           =       NULL;
        int                      volcount            =       0;
        int                      brickcount          =       0;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (keyprefix);
        GF_ASSERT (snap);

        list_for_each_entry_safe (snap_volinfo, tmp_volinfo, &snap->volumes,
                                  vol_list) {
                ret = snprintf (key, sizeof (key), "%s.vol%d", keyprefix,
                                volcount);
                if (ret < 0) {
                        goto out;
                }
                list_for_each_entry (brickinfo, &snap_volinfo->bricks,
                                     brick_list) {
                        if (!glusterd_is_local_brick (this, snap_volinfo,
                            brickinfo)) {
                                brickcount++;
                                continue;
                        }

                        ret = glusterd_get_single_brick_status (op_errstr,
                                                  rsp_dict, key, brickcount,
                                                  snap_volinfo, brickinfo);

                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Getting "
                                        "single snap status failed");
                                goto out;
                        }
                        brickcount++;
                }
                ret = snprintf (brickkey, sizeof (brickkey), "%s.brickcount",
                                key);
                if (ret < 0) {
                        goto out;
                }

                ret = dict_set_int32 (rsp_dict, brickkey, brickcount);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not save brick count");
                        goto out;
                }
                volcount++;
        }

        ret = snprintf (key, sizeof (key), "%s.volcount", keyprefix);
        if (ret < 0) {
                goto out;
        }

        ret = dict_set_int32 (rsp_dict, key, volcount);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not save volcount");
                goto out;
        }

out:

        return ret;
}

int
glusterd_get_each_snap_object_status (char **op_errstr, dict_t *rsp_dict,
                                      glusterd_snap_t *snap, char *keyprefix)
{
        int                     ret             =       -1;
        char                    key[PATH_MAX]   =       "";
        char                    *temp           =       NULL;
        xlator_t                *this           =       NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (snap);
        GF_ASSERT (keyprefix);

        /* TODO : Get all the snap volume info present in snap object,
         * as of now, There will be only one snapvolinfo per snap object
         */
        ret = snprintf (key, sizeof (key), "%s.snapname", keyprefix);
        if (ret < 0) {
                goto out;
        }

        temp = gf_strdup (snap->snapname);
        if (temp == NULL) {
                ret = -1;
                goto out;
        }
        ret = dict_set_dynstr (rsp_dict, key, temp);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not save "
                        "snap name");
                goto out;
        }

        temp = NULL;

        ret = snprintf (key, sizeof (key), "%s.uuid", keyprefix);
        if (ret < 0) {
                goto out;
        }

        temp = gf_strdup (uuid_utoa (snap->snap_id));
        if (temp == NULL) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (rsp_dict, key, temp);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not save "
                        "snap UUID");
                goto out;
        }

        temp = NULL;

        ret = glusterd_get_single_snap_status (op_errstr, rsp_dict, keyprefix,
                                               snap);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not get single snap status");
                goto out;
        }

        ret = snprintf (key, sizeof (key), "%s.volcount", keyprefix);
        if (ret < 0) {
                goto out;
        }

        ret = dict_set_int32 (rsp_dict, key, 1);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not save volcount");
                goto out;
        }
out:
        if (ret && temp)
                GF_FREE (temp);

        return ret;
}

int
glusterd_get_snap_status_of_volume (char **op_errstr, dict_t *rsp_dict,
                                    char *volname, char *keyprefix) {
        int                     ret              =       -1;
        glusterd_volinfo_t      *snap_volinfo    =       NULL;
        glusterd_volinfo_t      *temp_volinfo    =       NULL;
        glusterd_volinfo_t      *volinfo         =       NULL;
        char                    key[PATH_MAX]    =        "";
        xlator_t                *this            =       NULL;
        glusterd_conf_t         *priv            =       NULL;
        int                     i                =        0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);
        GF_ASSERT (volname);
        GF_ASSERT (keyprefix);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get volinfo of "
                        "volume %s", volname);
                goto out;
        }

        list_for_each_entry_safe (snap_volinfo, temp_volinfo,
                             &volinfo->snap_volumes, snapvol_list) {
                ret = snprintf (key, sizeof (key), "status.snap%d", i);
                if (ret < 0) {
                        goto out;
                }

                ret = glusterd_get_each_snap_object_status (op_errstr,
                                       rsp_dict, snap_volinfo->snapshot, key);

                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Function : "
                                "glusterd_get_single_snap_status failed");
                        goto out;
                }
                i++;
        }

        ret = dict_set_int32 (rsp_dict, "status.snapcount", i);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to save snapcount");
                ret = -1;
                goto out;
        }
out:
        return ret;
}

int
glusterd_get_all_snapshot_status (dict_t *dict, char **op_errstr,
                              dict_t *rsp_dict)
{
        int32_t                 i               =       0;
        int                     ret             =       -1;
        char                    key[PATH_MAX]   =       "";
        glusterd_conf_t         *priv           =       NULL;
        glusterd_snap_t         *snap           =       NULL;
        glusterd_snap_t         *tmp_snap       =       NULL;
        xlator_t                *this           =       NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        list_for_each_entry_safe (snap, tmp_snap,
                                  &priv->snapshots, snap_list) {
                ret = snprintf (key, sizeof (key), "status.snap%d", i);
                if (ret < 0) {
                        goto out;
                }

                ret = glusterd_get_each_snap_object_status (op_errstr,
                                                         rsp_dict, snap, key);

                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Could not get "
                                "the details of a snap object: %s",
                                snap->snapname);
                        goto out;
                }
                i++;
        }

        ret = dict_set_int32 (rsp_dict, "status.snapcount", i);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not save snapcount");
                goto out;
        }

        ret = 0;
out:
        return ret;
}


int
glusterd_snapshot_status_commit (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        xlator_t        *this             =       NULL;
        int             ret               =       -1;
        glusterd_conf_t *conf             =       NULL;
        char            *get_buffer       =       NULL;
        int32_t         cmd               =       -1;
        char            *snapname         =       NULL;
        glusterd_snap_t *snap             =       NULL;
        char            *volname          =       NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        conf = this->private;

        GF_ASSERT (conf);
        ret = dict_get_int32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get status cmd type");
                goto out;
        }

        ret = dict_set_int32 (rsp_dict, "cmd", cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not save status cmd in rsp dictionary");
                goto out;
        }
        switch (cmd) {
                case GF_SNAP_STATUS_TYPE_ALL:
                {
                        ret = glusterd_get_all_snapshot_status (dict, op_errstr,
                                                           rsp_dict);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                        "get snapshot status");
                                goto out;
                        }
                        break;
                }
                case GF_SNAP_STATUS_TYPE_SNAP:
                {

                        ret = dict_get_str (dict, "snapname", &snapname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                        "get snap name");
                                goto out;
                        }

                        snap = glusterd_find_snap_by_name (snapname);
                        if (!snap) {
                                ret = gf_asprintf (op_errstr, "Snap (%s) "
                                                  "not found", snapname);
                                if (ret < 0) {
                                        goto out;
                                }
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                        "get snap volinfo");
                                goto out;
                        }
                        ret = glusterd_get_each_snap_object_status (op_errstr,
                                          rsp_dict, snap, "status.snap0");
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Unable to "
                                        "get status of snap %s", get_buffer);
                                goto out;
                        }
                        break;
                }
                case GF_SNAP_STATUS_TYPE_VOL:
                {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Unable to"
                                        " get volume name");
                                goto out;
                        }

                        ret = glusterd_get_snap_status_of_volume (op_errstr,
                                           rsp_dict, volname, "status.vol0");
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Function :"
                                        " glusterd_get_snap_status_of_volume "
                                        "failed");
                                goto out;
                        }
                }
        }
        ret = 0;
out:
        return ret;
}

int32_t
glusterd_snapshot_create_postvalidate (dict_t *dict, int32_t op_ret,
                                       char **op_errstr, dict_t *rsp_dict)
{
        xlator_t        *this           = NULL;
        glusterd_conf_t *priv           = NULL;
        int              ret            = -1;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        priv = this->private;
        GF_ASSERT (priv);

        if (op_ret) {
                ret = glusterd_do_snap_cleanup (dict, op_errstr, rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "cleanup operation "
                                "failed");
                        goto out;
                }
        } else {
                ret = glusterd_snapshot_update_snaps_post_validate (dict,
                                                                    op_errstr,
                                                                    rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "create snapshot");
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_snapshot (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{

        xlator_t        *this           = NULL;
        glusterd_conf_t *priv           = NULL;
        int32_t          snap_command   = 0;
        int              ret            = -1;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_int32 (dict, "type", &snap_command);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case (GF_SNAP_OPTION_TYPE_CREATE):
                ret = glusterd_snapshot_create_commit (dict, op_errstr,
                                                       rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "create snapshot");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_CONFIG:
                ret = glusterd_snapshot_config_commit (dict, op_errstr,
                                                       rsp_dict);
                break;

        case GF_SNAP_OPTION_TYPE_DELETE:
                ret = glusterd_snapshot_remove_commit (dict, op_errstr,
                                                       rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "delete snapshot");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
                ret = glusterd_snapshot_restore (dict, op_errstr,
                                                 rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                "restore snapshot");
                        goto out;
                }

                break;

        case GF_SNAP_OPTION_TYPE_STATUS:
                ret = glusterd_snapshot_status_commit (dict, op_errstr,
                                                       rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "show snapshot status");
                        goto out;
                }
                break;


        default:
                gf_log (this->name, GF_LOG_WARNING, "invalid snap command");
                goto out;
                break;
        }

        ret = 0;

out:
        return ret;
}

int
glusterd_snapshot_brickop (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int            ret       = -1;
        int64_t        vol_count = 0;
        int64_t        count     = 1;
        char           key[1024] = {0,};
        char           *volname  = NULL;
        int32_t        snap_command = 0;
        xlator_t       *this     = NULL;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (dict, "type", &snap_command);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                ret = dict_get_int64 (dict, "volcount", &vol_count);
                if (ret)
                        goto out;
                while (count <= vol_count) {
                        snprintf (key, 1024, "volname%"PRId64, count);
                        ret = dict_get_str (dict, key, &volname);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unable to get volname");
                                goto out;
                        }
                        ret = dict_set_str (dict, "volname", volname);
                        if (ret)
                                goto out;

                        ret = gd_brick_op_phase (GD_OP_SNAP, NULL, dict,
                                        op_errstr);
                        if (ret)
                                goto out;
                        volname = NULL;
                        count++;
                }

                dict_del (dict, "volname");
                ret = 0;
                break;
        case GF_SNAP_OPTION_TYPE_DELETE:
                break;
        default:
                break;
        }

out:
        return ret;
}

int
glusterd_snapshot_prevalidate (dict_t *dict, char **op_errstr,
                               dict_t *rsp_dict)
{
        int                snap_command          = 0;
        xlator_t           *this                 = NULL;
        int                ret                   = -1;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (dict, "type", &snap_command);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case (GF_SNAP_OPTION_TYPE_CREATE):
                ret = glusterd_snapshot_create_prevalidate (dict, op_errstr,
                                                            rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot create "
                                "pre-validation failed");
                        goto out;
                }
                break;

        case (GF_SNAP_OPTION_TYPE_CONFIG):
                ret = glusterd_snapshot_config_prevalidate (dict, op_errstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot config "
                                "pre-validation failed");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
                ret = glusterd_snapshot_restore_prevalidate (dict, op_errstr,
                                                             rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot restore "
                                        "validation failed");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_DELETE:
                ret = glusterd_snapshot_remove_prevalidate (dict, op_errstr,
                                                            rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot remove "
                                "validation failed");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_STATUS:
                ret = glusterd_snapshot_status_prevalidate (dict, op_errstr,
                                                            rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot status "
                                "validation failed");
                        goto out;
                }
                break;

        default:
                gf_log (this->name, GF_LOG_WARNING, "invalid snap command");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_snapshot_postvalidate (dict_t *dict, int32_t op_ret, char **op_errstr,
                                dict_t *rsp_dict)
{
        int                snap_command          = 0;
        xlator_t           *this                 = NULL;
        int                ret                   = -1;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (dict, "type", &snap_command);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "unable to get the type of "
                        "the snapshot command");
                goto out;
        }

        switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                ret = glusterd_snapshot_create_postvalidate (dict, op_ret,
                                                             op_errstr,
                                                             rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot create "
                                "post-validation failed");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_DELETE:
        case GF_SNAP_OPTION_TYPE_RESTORE:
                ret = glusterd_snapshot_update_snaps_post_validate (dict,
                                                                    op_errstr,
                                                                    rsp_dict);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                "update missed snaps list");
                        goto out;
                }
                break;

        default:
                gf_log (this->name, GF_LOG_WARNING, "invalid snap command");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_handle_snapshot_fn (rpcsvc_request_t *req)
{
        int32_t               ret            = 0;
        dict_t               *dict           = NULL;
        gf_cli_req            cli_req        = {{0},};
        glusterd_op_t         cli_op         = GD_OP_SNAP;
        int                   type           = 0;
        glusterd_conf_t      *conf           = NULL;
        char                 *host_uuid      = NULL;
        char                  err_str[2048]  = {0,};
        xlator_t             *this           = NULL;
        char                 *volname        = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len > 0) {
                dict = dict_new ();
                if (!dict)
                        goto out;

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                }

                dict->extra_stdfree = cli_req.dict.dict_val;

                host_uuid = gf_strdup (uuid_utoa(MY_UUID));
                if (host_uuid == NULL) {
                        snprintf (err_str, sizeof (err_str), "Failed to get "
                                  "the uuid of local glusterd");
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dict, "host-uuid", host_uuid);
                if (ret) {
                        GF_FREE (host_uuid);
                        goto out;
                }


        } else {
                gf_log (this->name, GF_LOG_ERROR, "request dict length is %d",
                        cli_req.dict.dict_len);
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Command type not found");
                gf_log (this->name, GF_LOG_ERROR, "%s", err_str);
                goto out;
        }

        switch (type) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                ret = glusterd_handle_snapshot_create (req, cli_op, dict,
                                                err_str, sizeof (err_str));
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot create "
                                        "failed: %s", err_str);
                }
                break;
        case GF_SNAP_OPTION_TYPE_RESTORE:
                ret = glusterd_handle_snapshot_restore (req, cli_op, dict,
                                                err_str, sizeof (err_str));
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot restore "
                                        "failed: %s", err_str);
                }

                break;
        case GF_SNAP_OPTION_TYPE_INFO:
                ret = glusterd_handle_snapshot_info (req, cli_op, dict,
                                                err_str, sizeof (err_str));
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot info "
                                        "failed");
                }
                break;
        case GF_SNAP_OPTION_TYPE_LIST:
                ret = glusterd_handle_snapshot_list (req, cli_op, dict,
                                                err_str, sizeof (err_str));
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot list "
                                        "failed");
                }
                break;
        case GF_SNAP_OPTION_TYPE_CONFIG:
                /* TODO : Type of lock to be taken when we are setting
                 * limits system wide
                 */
                ret = dict_get_str (dict, "volname", &volname);
                if (!volname) {
                        ret = dict_set_int32 (dict, "hold_vol_locks",
                                              _gf_false);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Unable to set hold_vol_locks value "
                                        "as _gf_false");
                                goto out;
                        }

                }
                ret = glusterd_mgmt_v3_initiate_all_phases (req, cli_op, dict);
                break;
        case GF_SNAP_OPTION_TYPE_DELETE:
                ret = glusterd_handle_snapshot_remove (req, cli_op, dict,
                                                       err_str,
                                                       sizeof (err_str));
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot delete "
                                "failed: %s", err_str);
                }
                break;
        case GF_SNAP_OPTION_TYPE_START:
        case GF_SNAP_OPTION_TYPE_STOP:
        case GF_SNAP_OPTION_TYPE_STATUS:
                ret = glusterd_handle_snapshot_status (req, cli_op, dict,
                                                       err_str,
                                                       sizeof (err_str));
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Snapshot status "
                                "failed: %s", err_str);
                }
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR, "Unkown snapshot request "
                        "type (%d)", type);
                ret = -1; /* Failure */
        }

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }

        return ret;
}

int
glusterd_handle_snapshot (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, glusterd_handle_snapshot_fn);
}

static inline void
glusterd_free_snap_op (glusterd_snap_op_t *snap_op)
{
        if (snap_op) {
                if (snap_op->brick_path)
                        GF_FREE (snap_op->brick_path);

                GF_FREE (snap_op);
        }
}

/* Look for duplicates and accordingly update the list */
int32_t
glusterd_update_missed_snap_entry (glusterd_missed_snap_info *missed_snapinfo,
                                   glusterd_snap_op_t *missed_snap_op)
{
        int32_t                        ret                         = -1;
        glusterd_snap_op_t            *snap_opinfo                 = NULL;
        gf_boolean_t                   match                       = _gf_false;
        xlator_t                      *this                        = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT(missed_snapinfo);
        GF_ASSERT(missed_snap_op);

        list_for_each_entry (snap_opinfo, &missed_snapinfo->snap_ops,
                             snap_ops_list) {
                if ((!strcmp (snap_opinfo->brick_path,
                              missed_snap_op->brick_path)) &&
                    (snap_opinfo->op == missed_snap_op->op)) {
                        /* If two entries have conflicting status
                         * GD_MISSED_SNAP_DONE takes precedence
                         */
                        if ((snap_opinfo->status == GD_MISSED_SNAP_PENDING) &&
                            (missed_snap_op->status == GD_MISSED_SNAP_DONE)) {
                                snap_opinfo->status = GD_MISSED_SNAP_DONE;
                                gf_log (this->name, GF_LOG_INFO,
                                        "Updating missed snap status "
                                        "for %s:%d:%s:%d as DONE",
                                        missed_snapinfo->node_snap_info,
                                        snap_opinfo->brick_num,
                                        snap_opinfo->brick_path,
                                        snap_opinfo->op);
                                ret = 0;
                                glusterd_free_snap_op (missed_snap_op);
                                goto out;
                        }
                        match = _gf_true;
                        break;
                } else if ((snap_opinfo->brick_num ==
                            missed_snap_op->brick_num) &&
                            (snap_opinfo->op == GF_SNAP_OPTION_TYPE_CREATE) &&
                            (missed_snap_op->op ==
                             GF_SNAP_OPTION_TYPE_DELETE)) {
                        /* Optimizing create and delete entries for the same
                         * brick and same node
                         */
                        gf_log (this->name, GF_LOG_INFO,
                                "Updating missed snap status "
                                "for %s:%d:%s:%d as DONE",
                                missed_snapinfo->node_snap_info,
                                snap_opinfo->brick_num,
                                snap_opinfo->brick_path,
                                snap_opinfo->op);
                        snap_opinfo->status = GD_MISSED_SNAP_DONE;
                        ret = 0;
                        glusterd_free_snap_op (missed_snap_op);
                        goto out;
                }
        }

        if (match == _gf_true) {
                gf_log (this->name, GF_LOG_INFO,
                        "Duplicate entry. Not updating");
                glusterd_free_snap_op (missed_snap_op);
        } else {
                list_add_tail (&missed_snap_op->snap_ops_list,
                               &missed_snapinfo->snap_ops);
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

/* Add new missed snap entry to the missed_snaps list. */
int32_t
glusterd_store_missed_snaps_list (char *missed_info, int32_t brick_num,
                                  char *brick_path, int32_t snap_op,
                                  int32_t snap_status)
{
        int32_t                        ret                         = -1;
        glusterd_missed_snap_info     *missed_snapinfo             = NULL;
        glusterd_snap_op_t            *missed_snap_op              = NULL;
        glusterd_conf_t               *priv                        = NULL;
        gf_boolean_t                   match                       = _gf_false;
        gf_boolean_t                   free_missed_snap_info       = _gf_false;
        xlator_t                      *this                        = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT(missed_info);
        GF_ASSERT(brick_path);

        priv = this->private;
        GF_ASSERT (priv);

        /* Create the snap_op object consisting of the *
         * snap id and the op */
        ret = glusterd_missed_snap_op_new (&missed_snap_op);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create new missed snap object.");
                ret = -1;
                goto out;
        }

        missed_snap_op->brick_path = gf_strdup(brick_path);
        if (!missed_snap_op->brick_path) {
                ret = -1;
                goto out;
        }
        missed_snap_op->brick_num = brick_num;
        missed_snap_op->op = snap_op;
        missed_snap_op->status = snap_status;

        /* Look for other entries for the same node and same snap */
        list_for_each_entry (missed_snapinfo, &priv->missed_snaps_list,
                             missed_snaps) {
                if (!strcmp (missed_snapinfo->node_snap_info,
                             missed_info)) {
                        /* Found missed snapshot info for *
                         * the same node and same snap */
                        match = _gf_true;
                        break;
                }
        }

        if (match == _gf_false) {
                /* First snap op missed for the brick */
                ret = glusterd_missed_snapinfo_new (&missed_snapinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to create missed snapinfo");
                        goto out;
                }
                free_missed_snap_info = _gf_true;
                missed_snapinfo->node_snap_info = gf_strdup(missed_info);
                if (!missed_snapinfo->node_snap_info) {
                        ret = -1;
                        goto out;
                }

                list_add_tail (&missed_snap_op->snap_ops_list,
                               &missed_snapinfo->snap_ops);
                list_add_tail (&missed_snapinfo->missed_snaps,
                               &priv->missed_snaps_list);

                ret = 0;
                goto out;
        } else {
                ret = glusterd_update_missed_snap_entry (missed_snapinfo,
                                                         missed_snap_op);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to update existing missed snap entry.");
                        goto out;
                }
        }

out:
        if (ret) {
                glusterd_free_snap_op (missed_snap_op);

                if (missed_snapinfo &&
                    (free_missed_snap_info == _gf_true)) {
                        if (missed_snapinfo->node_snap_info)
                                GF_FREE (missed_snapinfo->node_snap_info);

                        GF_FREE (missed_snapinfo);
                }
        }

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

/* Add  missing snap entries to the in-memory conf->missed_snap_list */
int32_t
glusterd_add_missed_snaps_to_list (dict_t *dict, int32_t missed_snap_count)
{
        char                          *buf                         = NULL;
        char                          *tmp                         = NULL;
        char                          *save_ptr                    = NULL;
        char                          *nodeid                      = NULL;
        char                          *snap_uuid                   = NULL;
        char                          *brick_path                  = NULL;
        char                           missed_info[PATH_MAX]       = "";
        char                           name_buf[PATH_MAX]          = "";
        int32_t                        i                           = -1;
        int32_t                        ret                         = -1;
        int32_t                        brick_num                   = -1;
        int32_t                        snap_op                     = -1;
        int32_t                        snap_status                 = -1;
        glusterd_conf_t               *priv                        = NULL;
        xlator_t                      *this                        = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT(dict);

        priv = this->private;
        GF_ASSERT (priv);

        /* We can update the missed_snaps_list without acquiring *
         * any additional locks as big lock will be held.        */
        for (i = 0; i < missed_snap_count; i++) {
                snprintf (name_buf, sizeof(name_buf), "missed_snaps_%d",
                          i);
                ret = dict_get_str (dict, name_buf, &buf);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to fetch %s", name_buf);
                        goto out;
                }

                gf_log (this->name, GF_LOG_DEBUG, "missed_snap_entry = %s",
                        buf);

                /* Need to make a duplicate string coz the same dictionary *
                 * is resent to the non-originator nodes */
                tmp = gf_strdup (buf);
                if (!tmp) {
                        ret = -1;
                        goto out;
                }

                /* Fetch the node-id, snap-id, brick_num,
                 * brick_path, snap_op and snap status
                 */
                nodeid = strtok_r (tmp, ":", &save_ptr);
                snap_uuid = strtok_r (NULL, "=", &save_ptr);
                brick_num = atoi(strtok_r (NULL, ":", &save_ptr));
                brick_path = strtok_r (NULL, ":", &save_ptr);
                snap_op = atoi(strtok_r (NULL, ":", &save_ptr));
                snap_status = atoi(strtok_r (NULL, ":", &save_ptr));

                if (!nodeid || !snap_uuid || !brick_path ||
                    brick_num < 1 || snap_op < 1 ||
                    snap_status < 1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Invalid missed_snap_entry");
                        ret = -1;
                        goto out;
                }

                snprintf (missed_info, sizeof(missed_info), "%s:%s",
                          nodeid, snap_uuid);

                ret = glusterd_store_missed_snaps_list (missed_info,
                                                        brick_num,
                                                        brick_path,
                                                        snap_op,
                                                        snap_status);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to store missed snaps_list");
                        goto out;
                }

                GF_FREE (tmp);
                tmp = NULL;
        }

out:
        if (tmp)
                GF_FREE (tmp);

        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}
