/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
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

#include <glusterfs/syscall.h>
#include <glusterfs/lvm-defaults.h>

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif

extern char snap_mount_dir[VALID_GLUSTERD_PATHMAX];

int32_t
glusterd_origin_device(char *device, char **origin_device)
{
    int ret = -1;
    char msg[1024] = "";
    char origin_name[PATH_MAX] = "";
    char *ptr = NULL;
    char *snap_device = NULL;
    char origin_dev[PATH_MAX] = "";
    xlator_t *this = NULL;
    runner_t runner = {
        0,
    };
    int32_t len = 0;

    this = THIS;
    GF_ASSERT(this);

    snprintf(msg, sizeof(msg), "Get origin device for the device %s", device);

    runinit(&runner);

    runner_add_args(&runner, "/sbin/lvs", "--noheadings", "-o", "origin",
                    device, NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);

    ret = runner_start(&runner);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_DEBUG, errno, GD_MSG_TPOOL_GET_FAIL,
               "Failed to get thin pool "
               "name for device %s",
               device);
        runner_end(&runner);
        goto out;
    }

    ptr = fgets(origin_name, sizeof(origin_name),
                runner_chio(&runner, STDOUT_FILENO));
    if (!ptr || !strlen(origin_name)) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_TPOOL_GET_FAIL,
               "Failed to get origin device "
               "for device %s",
               device);
        runner_end(&runner);
        ret = -1;
        goto out;
    }

    runner_end(&runner);

    snap_device = gf_strdup(device);
    len = snprintf(origin_dev, sizeof(origin_dev), "%s/%s",
                   dirname(snap_device), origin_name);

    if ((len < 0) || (len >= sizeof(origin_dev))) {
        ret = -1;
        goto out;
    }

    *origin_device = origin_dev;
    ret = 0;

out:
    if (snap_device)
        GF_FREE(snap_device);

    return ret;
}

/* This function will check whether the given brick_path
 * is a thinly provisioned LV or not.
 *
 * @param brick_path    Brick Path
 *
 * @return              _gf_true if LV is thin else _gf_false
 */
gf_boolean_t
glusterd_lvm_probe(char *brick_path)
{
    int ret = -1;
    char msg[1024] = "";
    char pool_name[PATH_MAX] = "";
    char *ptr = NULL;
    char *device = NULL;
    xlator_t *this = NULL;
    runner_t runner = {
        0,
    };
    gf_boolean_t is_thin = _gf_false;

    this = THIS;

    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, brick_path, out);

    device = glusterd_get_brick_mount_device(brick_path);
    if (!device) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
               "getting device name for "
               "the brick %s failed",
               brick_path);
        goto out;
    }

    if (!glusterd_is_cmd_available("/sbin/lvs")) {
        gf_msg(this->name, GF_LOG_DEBUG, 0, GD_MSG_COMMAND_NOT_FOUND,
               "LVM commands not found");
        ret = -1;
        goto out;
    }

    snprintf(msg, sizeof(msg), "Get thin pool name for device %s", device);

    runinit(&runner);

    runner_add_args(&runner, "/sbin/lvs", "--noheadings", "-o", "pool_lv",
                    device, NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);

    ret = runner_start(&runner);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_DEBUG, errno, GD_MSG_TPOOL_GET_FAIL,
               "Failed to get thin pool "
               "name for device %s",
               device);
        runner_end(&runner);
        goto out;
    }

    ptr = fgets(pool_name, sizeof(pool_name),
                runner_chio(&runner, STDOUT_FILENO));
    if (!ptr || !strlen(pool_name)) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_TPOOL_GET_FAIL,
               "Failed to get pool name "
               "for device %s",
               device);
        runner_end(&runner);
        ret = -1;
        goto out;
    }

    runner_end(&runner);

    /* Trim all the whitespaces. */
    ptr = gf_trim(pool_name);

    /* If the LV has thin pool associated with this
     * then it is a thinly provisioned LV else it is
     * regular LV */
    if (0 != ptr[0]) {
        is_thin = _gf_true;
    }

out:
    if (device) {
        GF_FREE(device);
    }

    return is_thin;
}

/* This function is called to get the device path of the snap lvm. Usually
   if /dev/mapper/<group-name>-<lvm-name> is the device for the lvm,
   then the snap device will be /dev/<group-name>/<snapname>.
   This function takes care of building the path for the snap device.
*/

char *
glusterd_lvm_snapshot_device(char *brick_path, char *snapname)
{
    char snap[PATH_MAX] = "";
    char msg[1024] = "";
    char volgroup[PATH_MAX] = "";
    char *device = NULL;
    char *snap_device = NULL;
    xlator_t *this = NULL;
    runner_t runner = {
        0,
    };
    char *ptr = NULL;
    int ret = -1;

    this = THIS;
    GF_ASSERT(this);
    if (!brick_path) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "brick path is NULL");
        goto out;
    }
    if (!snapname) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "snapname is NULL");
        goto out;
    }

    device = glusterd_get_brick_mount_device(brick_path);
    if (!device) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
               "getting device name for "
               "the brick %s failed",
               brick_path);
        goto out;
    }

    runinit(&runner);
    runner_add_args(&runner, "/sbin/lvs", "--noheadings", "-o", "vg_name",
                    device, NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    snprintf(msg, sizeof(msg), "Get volume group for device %s", device);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_start(&runner);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_VG_GET_FAIL,
               "Failed to get volume group "
               "for device %s",
               device);
        runner_end(&runner);
        goto out;
    }
    ptr = fgets(volgroup, sizeof(volgroup),
                runner_chio(&runner, STDOUT_FILENO));
    if (!ptr || !strlen(volgroup)) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_VG_GET_FAIL,
               "Failed to get volume group "
               "for snap %s",
               snapname);
        runner_end(&runner);
        ret = -1;
        goto out;
    }
    runner_end(&runner);

    snprintf(snap, sizeof(snap), "/dev/%s/%s", gf_trim(volgroup), snapname);
    snap_device = gf_strdup(snap);
    if (!snap_device) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, GD_MSG_NO_MEMORY,
               "Cannot copy the snapshot device name for snapname: %s",
               snapname);
    }

out:
    if (device) {
        GF_FREE(device);
    }
    return snap_device;
}

/* This function actually calls the command (or the API) for taking the
   snapshot of the backend brick filesystem. If this is successful,
   then call the glusterd_snap_create function to create the snap object
   for glusterd
*/
int32_t
glusterd_lvm_snapshot_create_clone(glusterd_brickinfo_t *snap_brickinfo,
                                   int clone, char *snapname,
                                   char *snap_volume_id, char *clonename,
                                   char *clone_volume_id, int32_t brick_num)
{
    char msg[4096] = "";
    char buf[PATH_MAX] = "";
    char *ptr = NULL;
    char *origin_device = NULL;
    char brick_snapname[NAME_MAX] = "";
    char *snap_device = NULL;
    int ret = -1;
    gf_boolean_t match = _gf_false;
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(snap_brickinfo);

    if (!clone) {
        origin_device = glusterd_get_brick_mount_device(
            snap_brickinfo->origin_path);
        if (!origin_device) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
                   "getting device name for "
                   "the brick %s failed",
                   snap_brickinfo->origin_path);
            goto out;
        }
    } else {
        ret = snprintf(brick_snapname, sizeof(brick_snapname), "%s_%d",
                       snap_volume_id, brick_num);
        if (ret < 0) {
            goto out;
        }

        /* If the Snapshot created before origin_path is introduced */
        if (strlen(snap_brickinfo->device_path) > 0) {
            ret = glusterd_origin_device(snap_brickinfo->device_path,
                                         &origin_device);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
                       "getting origin_device for "
                       "the snap %s(Brick: %s) failed",
                       snapname, snap_brickinfo->path);
                goto out;
            }
        } else
            origin_device = glusterd_lvm_snapshot_device(
                snap_brickinfo->origin_path, brick_snapname);

        if (!origin_device) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
                   "getting device name for "
                   "the snap %s(Brick: %s) failed",
                   snapname, snap_brickinfo->path);
            goto out;
        }
    }

    /* Figuring out if setactivationskip flag is supported or not */
    runinit(&runner);
    snprintf(msg, sizeof(msg), "running lvcreate help");
    runner_add_args(&runner, LVM_CREATE, "--help", NULL);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    ret = runner_start(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_LVCREATE_FAIL,
               "Failed to run lvcreate help");
        runner_end(&runner);
        goto out;
    }

    /* Looking for setactivationskip in lvcreate --help */
    do {
        ptr = fgets(buf, sizeof(buf), runner_chio(&runner, STDOUT_FILENO));
        if (ptr) {
            if (strstr(buf, "setactivationskip")) {
                match = _gf_true;
                break;
            }
        }
    } while (ptr != NULL);
    runner_end(&runner);

    if (clone)
        ret = snprintf(brick_snapname, sizeof(brick_snapname), "%s_%d",
                       clone_volume_id, brick_num);
    else
        ret = snprintf(brick_snapname, sizeof(brick_snapname), "%s_%d",
                       snap_volume_id, brick_num);

    if (ret < 0) {
        goto out;
    }

    if (strlen(snap_brickinfo->device_path) > 0)
        snap_device = gf_strdup(snap_brickinfo->device_path);
    else
        snap_device = glusterd_lvm_snapshot_device(snap_brickinfo->origin_path,
                                                   brick_snapname);

    if (!snap_device) {
        gf_msg(this->name, GF_LOG_ERROR, ENXIO,
               GD_MSG_SNAP_DEVICE_NAME_GET_FAIL,
               "failed to get device name for the snapshot "
               "%s (Brick: %s)",
               brick_snapname, snap_brickinfo->path);
        ret = -1;
        goto out;
    }

    /* Taking the actual snapshot */
    runinit(&runner);
    snprintf(msg, sizeof(msg), "taking snapshot of the brick %s",
             snap_brickinfo->origin_path);
    if (match == _gf_true)
        runner_add_args(&runner, LVM_CREATE, "-s", origin_device,
                        "--setactivationskip", "n", "--name", snap_device,
                        NULL);
    else
        runner_add_args(&runner, LVM_CREATE, "-s", origin_device, "--name",
                        snap_device, NULL);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "taking snapshot of the "
               "brick (%s) of device %s failed",
               snap_brickinfo->origin_path, origin_device);
    }

    /* After the snapshot both the origin brick (LVM brick) and
     * the snapshot brick will have the same file-system label. This
     * will cause lot of problems at mount time. Therefore we must
     * generate a new label for the snapshot brick
     */
    ret = glusterd_update_fs_label(snap_brickinfo->path, snap_brickinfo->fstype,
                                   snap_device);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FS_LABEL_UPDATE_FAIL,
               "Failed to update "
               "file-system label for %s brick",
               snap_brickinfo->path);
        /* Failing to update label should not cause snapshot failure.
         * Currently label is updated only for XFS and ext2/ext3/ext4
         * file-system.
         */
    }

out:
    if (origin_device)
        GF_FREE(origin_device);

    if (snap_device)
        GF_FREE(snap_device);

    return ret;
}

int32_t
glusterd_lvm_snapshot_create(glusterd_brickinfo_t *snap_brickinfo,
                             char *snapname, char *snap_volume_id,
                             int32_t brick_num)
{
    return glusterd_lvm_snapshot_create_clone(
        snap_brickinfo, 0, snapname, snap_volume_id, NULL, NULL, brick_num);
}

int32_t
glusterd_lvm_snapshot_clone(glusterd_brickinfo_t *snap_brickinfo,
                            char *snapname, char *snap_volume_id,
                            char *clonename, char *clone_volume_id,
                            int32_t brick_num)
{
    return glusterd_lvm_snapshot_create_clone(snap_brickinfo, 1, snapname,
                                              snap_volume_id, clonename,
                                              clone_volume_id, brick_num);
}

static int
glusterd_lvm_brick_details(dict_t *rsp_dict,
                           glusterd_brickinfo_t *snap_brickinfo, char *snapname,
                           char *snap_volume_id, int32_t brick_num,
                           char *key_prefix)
{
    int ret = -1;
    char *device = NULL;
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;
    char msg[PATH_MAX] = "";
    char buf[PATH_MAX] = "";
    char *ptr = NULL;
    char *token = NULL;
    char key[160] = ""; /* key_prefix is 128 bytes at most */
    char *value = NULL;
    char *snap_device = NULL;
    char brick_snapname[NAME_MAX] = "";

    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap_brickinfo);
    GF_ASSERT(key_prefix);
    priv = this->private;
    GF_ASSERT(priv);

    ret = snprintf(brick_snapname, sizeof(brick_snapname), "%s_%d",
                   snap_volume_id, brick_num);
    if (ret < 0) {
        goto out;
    }

    if (strlen(snap_brickinfo->device_path) > 0)
        snap_device = gf_strdup(snap_brickinfo->device_path);
    else
        snap_device = glusterd_lvm_snapshot_device(snap_brickinfo->origin_path,
                                                   brick_snapname);

    if (!snap_device) {
        gf_msg(this->name, GF_LOG_ERROR, ENXIO,
               GD_MSG_SNAP_DEVICE_NAME_GET_FAIL,
               "failed to get device name for the snapshot "
               "%s (Brick: %s)",
               brick_snapname, snap_brickinfo->path);
        ret = -1;
        goto out;
    }

    runinit(&runner);
    snprintf(msg, sizeof(msg),
             "running lvs command, "
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
    runner_add_args(&runner, LVS, snap_device, "--noheading", "-o",
                    "vg_name,data_percent,lv_size", "--separator", ":", NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);
    ret = runner_start(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_LVS_FAIL,
               "Could not perform lvs action");
        goto end;
    }
    do {
        ptr = fgets(buf, sizeof(buf), runner_chio(&runner, STDOUT_FILENO));

        if (ptr == NULL)
            break;
        token = strtok(buf, ":");

        while (token && token[0] == ' ')
            token++;
        if (!token) {
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Invalid vg entry");
            goto end;
        }
        value = gf_strdup(token);
        if (!value) {
            ret = -1;
            goto end;
        }
        ret = snprintf(key, sizeof(key), "%s.vgname", key_prefix);
        if (ret < 0) {
            goto end;
        }

        ret = dict_set_dynstr(rsp_dict, key, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save vgname ");
            goto end;
        }

        token = strtok(NULL, ":");
        if (token != NULL) {
            value = gf_strdup(token);
            if (!value) {
                ret = -1;
                goto end;
            }
            ret = snprintf(key, sizeof(key), "%s.data", key_prefix);
            if (ret < 0) {
                goto end;
            }

            ret = dict_set_dynstr(rsp_dict, key, value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Could not save data percent ");
                goto end;
            }
        }
        token = strtok(NULL, ":");
        if (token != NULL) {
            value = gf_strdup(token);
            if (!value) {
                ret = -1;
                goto end;
            }
            ret = snprintf(key, sizeof(key), "%s.lvsize", key_prefix);
            if (ret < 0) {
                goto end;
            }

            ret = dict_set_dynstr(rsp_dict, key, value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Could not save meta data percent ");
                goto end;
            }
        }

    } while (ptr != NULL);

    ret = 0;

end:
    runner_end(&runner);

out:
    if (ret && value) {
        GF_FREE(value);
    }

    if (device)
        GF_FREE(device);

    if (snap_device)
        GF_FREE(snap_device);

    return ret;
}

int
glusterd_lvm_snapshot_remove(glusterd_brickinfo_t *snap_brickinfo,
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
    char *snap_device = NULL;
    int32_t len = 0;
    char brick_snapname[NAME_MAX] = "";
    struct stat stbuf = {
        0,
    };

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(snap_brickinfo);

    len = snprintf(brick_snapname, sizeof(brick_snapname), "%s_%d",
                   snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(brick_snapname))) {
        goto out;
    }

    if (strlen(snap_brickinfo->device_path) > 0)
        snap_device = gf_strdup(snap_brickinfo->device_path);
    else
        snap_device = glusterd_lvm_snapshot_device(snap_brickinfo->origin_path,
                                                   brick_snapname);

    if (!snap_device) {
        gf_msg(this->name, GF_LOG_ERROR, ENXIO,
               GD_MSG_SNAP_DEVICE_NAME_GET_FAIL,
               "failed to get device name for the snapshot "
               "%s (Brick: %s)",
               snapname, snap_brickinfo->path);
        ret = -1;
        goto out;
    }

    /* Verify if the device path exists or not */
    ret = sys_stat(snap_device, &stbuf);
    if (ret) {
        gf_msg_debug(this->name, 0,
                     "Device (%s) for brick (%s:%s) not present. "
                     "Removing the brick path",
                     snap_device, snap_brickinfo->hostname,
                     snap_brickinfo->path);
        /* Making ret = 0 as absence of device path should *
         * not fail the remove operation */
        ret = 0;
        goto out;
    }

    runinit(&runner);
    len = snprintf(msg, sizeof(msg),
                   "remove snapshot of the brick %s, "
                   "snap name: %s (device: %s)",
                   snap_brickinfo->path, brick_snapname, snap_device);
    if (len < 0) {
        strcpy(msg, "<error>");
    }
    runner_add_args(&runner, LVM_REMOVE, "-f", snap_device, NULL);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);

    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "removing snapshot of the "
               "brick (%s) of snapshot %s (device: %s) failed",
               snap_brickinfo->path, brick_snapname, snap_device);
        goto out;
    }

out:
    if (snap_device)
        GF_FREE(snap_device);

    return ret;
}

int32_t
glusterd_lvm_snapshot_activate(glusterd_brickinfo_t *snap_brickinfo,
                               char *snapname, char *snap_volume_id,
                               int32_t brick_num)
{
    char msg[4096] = "";
    char mnt_opts[1024] = "";
    char buff[PATH_MAX] = {0};
    int32_t ret = -1;
    runner_t runner = {
        0,
    };
    xlator_t *this = NULL;
    struct mntent *entry = NULL;
    struct mntent save_entry = {0};
    char *snap_device = NULL;
    char brick_snapname[NAME_MAX] = "";
    char *snap_brick_mount_path = NULL;
    int32_t len = 0;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(snap_brickinfo);

    /* Fetch the brick mount path from the brickinfo->path */
    ret = glusterd_find_brick_mount_path(snap_brickinfo->path,
                                         &snap_brick_mount_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRK_MNTPATH_GET_FAIL,
               "Failed to find snap_brick_mount_path for %s",
               snap_brickinfo->path);
        goto out;
    }

    ret = mkdir_p(snap_brick_mount_path, 0755, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "creating the brick directory"
               " %s for the snapshot %s failed",
               snap_brick_mount_path, snapname);
        goto out;
    }

    /* Check to see if the brick is already mounted */
    entry = glusterd_get_mnt_entry_info(snap_brick_mount_path, buff,
                                        sizeof(buff), &save_entry);
    gf_log(this->name, GF_LOG_DEBUG,
           "Checking to see if %s "
           "is already mounted @ %s",
           snap_brickinfo->path, snap_brick_mount_path);
    if (entry) {
        gf_log(this->name, GF_LOG_INFO, "Snapshot already mounted at %s",
               snap_brick_mount_path);
        ret = 0;
        goto out;
    }

    len = snprintf(brick_snapname, sizeof(brick_snapname), "%s_%d",
                   snap_volume_id, brick_num);
    if ((len < 0) || (len >= sizeof(brick_snapname))) {
        goto out;
    }

    if (strlen(snap_brickinfo->device_path) > 0)
        snap_device = gf_strdup(snap_brickinfo->device_path);
    else
        snap_device = glusterd_lvm_snapshot_device(snap_brickinfo->origin_path,
                                                   brick_snapname);

    if (!snap_device) {
        gf_msg(this->name, GF_LOG_ERROR, ENXIO,
               GD_MSG_SNAP_DEVICE_NAME_GET_FAIL,
               "failed to get device name for the snapshot "
               "%s (Brick: %s)",
               snapname, snap_brickinfo->path);
        ret = -1;
        goto out;
    }

    runinit(&runner);
    snprintf(msg, sizeof(msg), "mount %s %s", snap_device,
             snap_brick_mount_path);

    strcpy(mnt_opts, snap_brickinfo->mnt_opts);

    /* XFS file-system does not allow to mount file-system with duplicate
     * UUID. File-system UUID of snapshot and its origin volume is same.
     * Therefore to mount such a snapshot in XFS we need to pass nouuid
     * option
     */
    if (!strcmp(snap_brickinfo->fstype, "xfs") &&
        !glusterd_mntopts_exists(mnt_opts, "nouuid")) {
        if (strlen(mnt_opts) > 0)
            strcat(mnt_opts, ",");
        strcat(mnt_opts, "nouuid");
    }

    if (strlen(mnt_opts) > 0) {
        runner_add_args(&runner, "mount", "-o", mnt_opts, snap_device,
                        snap_brick_mount_path, NULL);
    } else {
        runner_add_args(&runner, "mount", snap_device, snap_brick_mount_path,
                        NULL);
    }

    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_MOUNT_FAIL,
               "mounting the snapshot "
               "logical device %s failed (error: %s)",
               snap_device, strerror(errno));
        goto out;
    } else
        gf_msg_debug(this->name, 0,
                     "mounting the snapshot "
                     "logical device %s successful",
                     snap_device);

out:
    if (snap_brick_mount_path)
        GF_FREE(snap_brick_mount_path);

    if (snap_device)
        GF_FREE(snap_device);

    gf_msg_trace(this->name, 0, "Returning with %d", ret);
    return ret;
}

int32_t
glusterd_lvm_snapshot_deactivate(glusterd_brickinfo_t *snap_brickinfo,
                                 char *snapname, char *snap_volume_id,
                                 int32_t brick_num)
{
    char msg[NAME_MAX] = "";
    int32_t ret = -1;
    runner_t runner = {
        0,
    };
    xlator_t *this = NULL;
    char *snap_brick_mount_path = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(snap_brickinfo);

    /* Fetch the brick mount path from the brickinfo->path */
    ret = glusterd_find_brick_mount_path(snap_brickinfo->path,
                                         &snap_brick_mount_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRK_MNTPATH_GET_FAIL,
               "Failed to find snap_brick_mount_path for %s",
               snap_brickinfo->path);
        goto out;
    }

    if (!glusterd_is_path_mounted(snap_brick_mount_path)) {
        return 0;
    }

    /*umount2 system call doesn't cleanup mtab entry after un-mount.
      So use external umount command*/
    runinit(&runner);
    snprintf(msg, sizeof(msg), "umount path %s", snap_brick_mount_path);
    runner_add_args(&runner, _PATH_UMOUNT, "-f", snap_brick_mount_path, NULL);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_GLUSTERD_UMOUNT_FAIL,
               "umounting %s failed (%s)", snap_brick_mount_path,
               strerror(errno));
        goto out;
    }
out:
    if (snap_brick_mount_path)
        GF_FREE(snap_brick_mount_path);

    gf_msg_trace(this->name, 0, "Returning with %d", ret);
    return ret;
}

int32_t
glusterd_lvm_snapshot_restore(glusterd_brickinfo_t *snap_brickinfo,
                              char *snapname, char *snap_volume_id,
                              int32_t brick_num,
                              gf_boolean_t *retain_origin_path)
{
    int32_t ret = -1;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    GF_ASSERT(snap_brickinfo);

    ret = glusterd_lvm_snapshot_activate(snap_brickinfo, snapname,
                                         snap_volume_id, brick_num);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_MOUNT_FAIL,
               "Failed to mount snapshot to restore.");
        goto out;
    }

out:
    return ret;
}

int32_t
glusterd_lvm_snap_clone_brick_path(char *snap_mount_dir,
                                   char *origin_brick_path, int clone,
                                   char *snap_clone_name,
                                   char *snap_clone_volume_id,
                                   char *snap_brick_dir, int brick_num,
                                   glusterd_brickinfo_t *brickinfo, int restore)
{
    int32_t len = 0;
    int ret = 0;

    if (strcmp(snap_brick_dir, "/") == 0) {
        snap_brick_dir = "";
    }

    len = snprintf(brickinfo->path, sizeof(brickinfo->path), "%s/%s/brick%d%s",
                   snap_mount_dir, snap_clone_volume_id, brick_num + 1,
                   snap_brick_dir);

    if ((len < 0) || (len >= sizeof(brickinfo->path))) {
        brickinfo->path[0] = 0;
        ret = -1;
    }

    return ret;
}

struct glusterd_snap_ops lvm_snap_ops = {
    .name = "LVM",
    .probe = glusterd_lvm_probe,
    .details = glusterd_lvm_brick_details,
    .create = glusterd_lvm_snapshot_create,
    .clone = glusterd_lvm_snapshot_clone,
    .remove = glusterd_lvm_snapshot_remove,
    .activate = glusterd_lvm_snapshot_activate,
    .deactivate = glusterd_lvm_snapshot_deactivate,
    .restore = glusterd_lvm_snapshot_restore,
    .brick_path = glusterd_lvm_snap_clone_brick_path};
