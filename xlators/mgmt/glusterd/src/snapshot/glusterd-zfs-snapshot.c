/*
   Copyright (c) 2021 iXsystems, Inc <https://www.ixsystems.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>

#include "glusterd-messages.h"

#include "glusterd-utils.h"
#include "glusterd-snapshot-utils.h"

#include <glusterfs/dict.h>
#include <glusterfs/run.h>

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif

#define ZFS_COMMAND "/sbin/zfs"

extern char snap_mount_dir[VALID_GLUSTERD_PATHMAX];

int32_t
glusterd_zfs_dataset(char *brick_path, char **pool_name)
{
    char msg[1024] = "";
    char dataset[PATH_MAX] = "";
    xlator_t *this = NULL;
    runner_t runner = {
        0,
    };
    char *ptr = NULL;
    int32_t ret = 0;

    this = THIS;
    GF_ASSERT(this);

    runinit(&runner);
    snprintf(msg, sizeof(msg),
             "running zfs command, "
             "for getting zfs pool name from brick path");
    runner_add_args(&runner, "zfs", "list", "-Ho", "name", brick_path, NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);
    ret = runner_start(&runner);
    if (ret == -1) {
        gf_log(this->name, GF_LOG_ERROR,
               "Failed to get dataset name "
               "for the brick_path %s",
               brick_path);
        runner_end(&runner);
        goto out;
    }
    ptr = fgets(dataset, sizeof(dataset), runner_chio(&runner, STDOUT_FILENO));

    if (!ptr || !strlen(dataset)) {
        gf_log(this->name, GF_LOG_ERROR,
               "Failed to get datset name "
               "for the brick_path %s",
               brick_path);
        runner_end(&runner);
        ret = -1;
        goto out;
    }
    runner_end(&runner);

    *pool_name = strtok(dataset, "\n");

out:
    return ret;
}

gf_boolean_t
glusterd_zfs_probe(char *brick_path)
{
    int ret = -1;
    xlator_t *this = NULL;
    gf_boolean_t is_zfs = _gf_false;
    char *mnt_pt = NULL;
    char buff[PATH_MAX] = "";
    struct mntent *entry = NULL;
    struct mntent save_entry = {
        0,
    };

    this = THIS;

    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, brick_path, out);

    if (!glusterd_is_cmd_available(ZFS_COMMAND)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "ZFS commands not found");
        ret = -1;
        goto out;
    }

    ret = glusterd_get_brick_root(brick_path, &mnt_pt);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICKPATH_ROOT_GET_FAIL,
               "getting the root "
               "of the brick (%s) failed ",
               brick_path);
        goto out;
    }

    entry = glusterd_get_mnt_entry_info(mnt_pt, buff, sizeof(buff),
                                        &save_entry);
    if (!entry) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MNTENTRY_GET_FAIL,
               "getting the mount entry for "
               "the brick (%s) failed",
               brick_path);
        goto out;
    }

    if (0 == strncmp("zfs", entry->mnt_type, 5)) {
        is_zfs = _gf_true;
    }

out:
    if (mnt_pt)
        GF_FREE(mnt_pt);

    return is_zfs;
}

int32_t
glusterd_zfs_snapshot_create_or_clone(glusterd_brickinfo_t *snap_brickinfo,
                                      int clone, char *snapname,
                                      char *snap_volume_id, char *clonename,
                                      char *clone_volume_id, int32_t brick_num)
{
    char msg[4096] = "";
    int ret = -1;
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;
    char snap_device[NAME_MAX] = "";
    char clone_device[NAME_MAX] = "";
    char *dataset = NULL;
    int32_t len = 0;

    GF_ASSERT(snap_brickinfo);

    ret = glusterd_zfs_dataset(snap_brickinfo->origin_path, &dataset);
    if (ret)
        goto out;

    len = snprintf(snap_device, sizeof(snap_device), "%s@%s_%d", dataset,
                   snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(snap_device))) {
        goto out;
    }

    /* Taking the actual snapshot */
    runinit(&runner);
    snprintf(msg, sizeof(msg), "taking snapshot of the brick %s",
             snap_brickinfo->origin_path);

    if (clone) {
        len = snprintf(clone_device, sizeof(clone_device), "%s/%s_%d", dataset,
                       clone_volume_id, brick_num);
        if ((len < 0) || (len >= sizeof(clone_device))) {
            goto out;
        }

        runner_add_args(&runner, ZFS_COMMAND, "clone", snap_device,
                        clone_device, NULL);
    } else
        runner_add_args(&runner, ZFS_COMMAND, "snapshot", snap_device, NULL);

    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        if (clone)
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CLONE_FAILED,
                   "taking clone of the "
                   "brick (%s) failed",
                   snap_brickinfo->origin_path);
        else
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
                   "taking snapshot of the "
                   "brick (%s) failed",
                   snap_brickinfo->origin_path);
    }

out:
    return ret;
}

/* This function actually calls the command for creating
   a snapshot of the backend brick filesystem.
*/
int32_t
glusterd_zfs_snapshot_create(glusterd_brickinfo_t *snap_brickinfo,
                             char *snapname, char *snap_volume_id,
                             int32_t brick_num)
{
    return glusterd_zfs_snapshot_create_or_clone(
        snap_brickinfo, 0, snapname, snap_volume_id, NULL, NULL, brick_num);
}

/* This function actually calls the command for cloning
   a snapshot of the backend brick filesystem.
*/
int32_t
glusterd_zfs_snapshot_clone(glusterd_brickinfo_t *snap_brickinfo,
                            char *snapname, char *snap_volume_id,
                            char *clonename, char *clone_volume_id,
                            int32_t brick_num)
{
    return glusterd_zfs_snapshot_create_or_clone(snap_brickinfo, 1, snapname,
                                                 snap_volume_id, clonename,
                                                 clone_volume_id, brick_num);
}

static int
glusterd_zfs_brick_details(dict_t *rsp_dict,
                           glusterd_brickinfo_t *snap_brickinfo, char *snapname,
                           char *snap_volume_id, int32_t brick_num,
                           char *key_prefix)
{
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    char key[160] = ""; /* key_prefix is 128 bytes at most */
    char *dataset = NULL;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap_brickinfo);
    GF_ASSERT(key_prefix);
    priv = this->private;
    GF_ASSERT(priv);

    ret = glusterd_zfs_dataset(snap_brickinfo->origin_path, &dataset);
    if (ret)
        goto out;

    ret = snprintf(key, sizeof(key), "%s.vgname", key_prefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_set_str(rsp_dict, key, dataset);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save vgname ");
        goto out;
    }

    ret = snprintf(key, sizeof(key), "%s.data", key_prefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_set_str(rsp_dict, key, "-");
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save data percent ");
        goto out;
    }

    ret = snprintf(key, sizeof(key), "%s.lvsize", key_prefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_set_str(rsp_dict, key, "-");
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save meta data percent ");
        goto out;
    }

    ret = 0;

out:
    return ret;
}

int
glusterd_zfs_snapshot_remove(glusterd_brickinfo_t *snap_brickinfo,
                             char *snapname, char *snap_volume_id,
                             int32_t brick_num)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    char msg[1024] = "";
    int len;
    char snap_device[NAME_MAX] = "";
    char *dataset = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(snap_brickinfo);

    ret = glusterd_zfs_dataset(snap_brickinfo->origin_path, &dataset);
    if (ret)
        goto out;

    len = snprintf(snap_device, sizeof(snap_device), "%s@%s_%d", dataset,
                   snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(snap_device))) {
        goto out;
    }

    runinit(&runner);
    len = snprintf(msg, sizeof(msg),
                   "remove snapshot of the brick %s, "
                   "snap name: %s",
                   snap_brickinfo->origin_path, snapname);
    if (len < 0) {
        strcpy(msg, "<error>");
    }
    runner_add_args(&runner, ZFS_COMMAND, "destroy", snap_device, NULL);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);

    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "removing snapshot of the "
               "brick (%s) of snapshot %s failed",
               snap_brickinfo->origin_path, snapname);
        goto out;
    }

out:
    return ret;
}

/* No op since .zfs directory is used */
int32_t
glusterd_zfs_snapshot_activate(glusterd_brickinfo_t *snap_brickinfo,
                               char *snapname, char *snap_volume_id,
                               int32_t brick_num)
{
    int32_t ret = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(snap_brickinfo);

    gf_msg_trace(this->name, 0, "Returning with %d", ret);
    return ret;
}

/* No op since .zfs directory is used */
int32_t
glusterd_zfs_snapshot_deactivate(glusterd_brickinfo_t *snap_brickinfo,
                                 char *snapname, char *snap_volume_id,
                                 int32_t brick_num)
{
    int32_t ret = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(snap_brickinfo);

    gf_msg_trace(this->name, 0, "Returning with %d", ret);
    return ret;
}

int32_t
glusterd_zfs_snapshot_restore(glusterd_brickinfo_t *snap_brickinfo,
                              char *snapname, char *snap_volume_id,
                              int32_t brick_num,
                              gf_boolean_t *retain_origin_path)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    char msg[1024] = "";
    int len;
    char snap_device[NAME_MAX] = "";
    char clone_device[NAME_MAX] = "";
    char mnt_pt[PATH_MAX] = "";
    char *dataset = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(snap_brickinfo);

    ret = glusterd_zfs_dataset(snap_brickinfo->origin_path, &dataset);
    if (ret)
        goto out;

    len = snprintf(snap_device, sizeof(snap_device), "%s@%s_%d", dataset,
                   snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(snap_device))) {
        goto out;
    }

    len = snprintf(clone_device, sizeof(clone_device), "%s/%s_%d", dataset,
                   snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(clone_device))) {
        goto out;
    }

    runinit(&runner);
    len = snprintf(msg, sizeof(msg),
                   "restore snapshot of the brick %s, "
                   "snap name: %s",
                   snap_brickinfo->origin_path, snapname);
    if (len < 0) {
        strcpy(msg, "<error>");
    }
    runner_add_args(&runner, ZFS_COMMAND, "clone", snap_device, clone_device,
                    NULL);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);

    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
               "restoring snapshot of the "
               "brick (%s) of snapshot %s failed",
               snap_brickinfo->origin_path, snapname);
        goto out;
    }

    runinit(&runner);
    len = snprintf(mnt_pt, sizeof(mnt_pt), "mountpoint=%s/%s/brick%d",
                   snap_mount_dir, snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(mnt_pt))) {
        goto out;
    }

    runner_add_args(&runner, ZFS_COMMAND, "set", mnt_pt, clone_device, NULL);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);

    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
               "setting mountpoint of restore of the "
               "brick (%s) of snapshot %s failed",
               snap_brickinfo->origin_path, snapname);
        goto out;
    }

out:
    return ret;
}

int32_t
glusterd_zfs_snap_clone_brick_path(char *snap_mount_dir,
                                   char *origin_brick_path, int clone,
                                   char *snap_clone_name,
                                   char *snap_clone_volume_id,
                                   char *snap_brick_dir, int brick_num,
                                   glusterd_brickinfo_t *brickinfo, int restore)
{
    int32_t len = 0;
    int ret = 0;
    char brick_path[PATH_MAX] = "";
    char *origin_brick_mount = NULL;
    char *origin_brick = NULL;

    origin_brick = gf_strdup(origin_brick_path);
    origin_brick_mount = dirname(origin_brick);

    if (clone)
        len = snprintf(brickinfo->path, sizeof(brickinfo->path), "%s/%s_%d/%s",
                       origin_brick_mount, snap_clone_volume_id, brick_num,
                       snap_brick_dir);
    else {
        if (restore)
            len = snprintf(brickinfo->path, sizeof(brickinfo->path),
                           "%s/%s/brick%d%s", snap_mount_dir,
                           snap_clone_volume_id, brick_num, snap_brick_dir);
        else
            len = snprintf(brickinfo->path, sizeof(brickinfo->path),
                           "%s/.zfs/snapshot/%s_%d/%s", origin_brick_mount,
                           snap_clone_volume_id, brick_num, snap_brick_dir);
    }

    if ((len < 0) || (len >= sizeof(brick_path))) {
        brickinfo->path[0] = 0;
        ret = -1;
    }

    if (origin_brick)
        GF_FREE(origin_brick);

    return ret;
}

struct glusterd_snap_ops zfs_snap_ops = {
    .name = "ZFS",
    .probe = glusterd_zfs_probe,
    .details = glusterd_zfs_brick_details,
    .create = glusterd_zfs_snapshot_create,
    .clone = glusterd_zfs_snapshot_clone,
    .remove = glusterd_zfs_snapshot_remove,
    .activate = glusterd_zfs_snapshot_activate,
    .deactivate = glusterd_zfs_snapshot_deactivate,
    .restore = glusterd_zfs_snapshot_restore,
    .brick_path = glusterd_zfs_snap_clone_brick_path};
