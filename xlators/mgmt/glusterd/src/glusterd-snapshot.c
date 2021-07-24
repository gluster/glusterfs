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
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <signal.h>
#include "glusterd-messages.h"
#include "glusterd-errno.h"

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif

#ifdef __NetBSD__
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#endif

#if defined(GF_DARWIN_HOST_OS) || defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#endif

#include <regex.h>

#include <glusterfs/compat.h>
#include "protocol-common.h"
#include <glusterfs/xlator.h>
#include <glusterfs/logging.h>
#include <glusterfs/timer.h>
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include <glusterfs/run.h>
#include "glusterd-volgen.h"
#include "glusterd-mgmt.h"
#include "glusterd-syncop.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-snapd-svc.h"

#include "glusterfs3.h"

#include <glusterfs/syscall.h>
#include "cli1-xdr.h"
#include "xdr-generic.h"

#include <glusterfs/lvm-defaults.h>
#include <glusterfs/events.h>

#define GLUSTERD_GET_UUID_NOHYPHEN(ret_string, uuid)                           \
    do {                                                                       \
        char *snap_volname_ptr = ret_string;                                   \
        char tmp_uuid[64];                                                     \
        char *snap_volid_ptr = uuid_utoa_r(uuid, tmp_uuid);                    \
        while (*snap_volid_ptr) {                                              \
            if (*snap_volid_ptr == '-') {                                      \
                snap_volid_ptr++;                                              \
            } else {                                                           \
                (*snap_volname_ptr++) = (*snap_volid_ptr++);                   \
            }                                                                  \
        }                                                                      \
        *snap_volname_ptr = '\0';                                              \
    } while (0)

char snap_mount_dir[VALID_GLUSTERD_PATHMAX];
struct snap_create_args_ {
    xlator_t *this;
    dict_t *dict;
    dict_t *rsp_dict;
    glusterd_volinfo_t *snap_vol;
    glusterd_brickinfo_t *brickinfo;
    struct syncargs *args;
    int32_t volcount;
    int32_t brickcount;
    int32_t brickorder;
};

/* This structure is used to store unsupported options and their values
 * for snapshotted volume.
 */
struct gd_snap_unsupported_opt_t {
    char *key;
    char *value;
};

typedef struct snap_create_args_ snap_create_args_t;

/* This function is called to get the device path of the snap lvm. Usually
   if /dev/mapper/<group-name>-<lvm-name> is the device for the lvm,
   then the snap device will be /dev/<group-name>/<snapname>.
   This function takes care of building the path for the snap device.
*/

char *
glusterd_build_snap_device_path(char *device, char *snapname,
                                int32_t brickcount)
{
    char snap[PATH_MAX] = "";
    char msg[1024] = "";
    char volgroup[PATH_MAX] = "";
    char *snap_device = NULL;
    xlator_t *this = THIS;
    runner_t runner = {
        0,
    };
    char *ptr = NULL;
    int ret = -1;

    if (!device) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "device is NULL");
        goto out;
    }
    if (!snapname) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "snapname is NULL");
        goto out;
    }

    runinit(&runner);
    runner_add_args(&runner, "lvs", "--noheadings", "-o", "vg_name", device,
                    NULL);
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

    snprintf(snap, sizeof(snap), "/dev/%s/%s_%d", gf_trim(volgroup), snapname,
             brickcount);
    snap_device = gf_strdup(snap);
    if (!snap_device) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, GD_MSG_NO_MEMORY,
               "Cannot copy the snapshot device name for snapname: %s",
               snapname);
    }

out:
    return snap_device;
}

/* Look for disconnected peers, for missed snap creates or deletes */
static int32_t
glusterd_find_missed_snap(dict_t *rsp_dict, glusterd_volinfo_t *vol,
                          struct cds_list_head *peers, int32_t op)
{
    int32_t brick_count = -1;
    int32_t ret = -1;
    xlator_t *this = THIS;
    glusterd_peerinfo_t *peerinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(peers);
    GF_ASSERT(vol);

    brick_count = 0;
    cds_list_for_each_entry(brickinfo, &vol->bricks, brick_list)
    {
        if (!gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
            /* If the brick belongs to the same node */
            brick_count++;
            continue;
        }

        RCU_READ_LOCK;
        cds_list_for_each_entry_rcu(peerinfo, peers, uuid_list)
        {
            if (gf_uuid_compare(peerinfo->uuid, brickinfo->uuid)) {
                /* If the brick doesn't belong to this peer */
                continue;
            }

            /* Found peer who owns the brick,    *
             * if peer is not connected or not   *
             * friend add it to missed snap list */
            if (!(peerinfo->connected) ||
                (peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)) {
                ret = glusterd_add_missed_snaps_to_dict(
                    rsp_dict, vol, brickinfo, brick_count + 1, op);
                if (ret) {
                    RCU_READ_UNLOCK;
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_MISSED_SNAP_CREATE_FAIL,
                           "Failed to add missed snapshot "
                           "info for %s:%s in the "
                           "rsp_dict",
                           brickinfo->hostname, brickinfo->path);
                    goto out;
                }
            }
        }
        RCU_READ_UNLOCK;
        brick_count++;
    }

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int
snap_max_limits_display_commit(dict_t *rsp_dict, char *volname, char *op_errstr,
                               int len)
{
    char err_str[PATH_MAX] = "";
    char key[64] = "";
    int keylen;
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int ret = -1;
    uint64_t active_hard_limit = 0;
    uint64_t snap_max_limit = 0;
    uint64_t soft_limit_value = -1;
    uint64_t count = 0;
    xlator_t *this = THIS;
    uint64_t opt_hard_max = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
    uint64_t opt_soft_max = GLUSTERD_SNAPS_DEF_SOFT_LIMIT_PERCENT;
    char *auto_delete = "disable";
    char *snap_activate = "disable";

    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_errstr);

    conf = this->private;

    GF_ASSERT(conf);

    /* config values snap-max-hard-limit and snap-max-soft-limit are
     * optional and hence we are not erroring out if values are not
     * present
     */
    gd_get_snap_conf_values_if_present(conf->opts, &opt_hard_max,
                                       &opt_soft_max);

    if (!volname) {
        /* For system limit */
        cds_list_for_each_entry(volinfo, &conf->volumes, vol_list)
        {
            if (volinfo->is_snap_volume == _gf_true)
                continue;

            snap_max_limit = volinfo->snap_max_hard_limit;
            if (snap_max_limit > opt_hard_max)
                active_hard_limit = opt_hard_max;
            else
                active_hard_limit = snap_max_limit;

            soft_limit_value = (opt_soft_max * active_hard_limit) / 100;

            keylen = snprintf(key, sizeof(key), "volume%" PRId64 "-volname",
                              count);
            ret = dict_set_strn(rsp_dict, key, keylen, volinfo->volname);
            if (ret) {
                len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
                if (len < 0) {
                    strcpy(err_str, "<error>");
                }
                goto out;
            }

            snprintf(key, sizeof(key), "volume%" PRId64 "-snap-max-hard-limit",
                     count);
            ret = dict_set_uint64(rsp_dict, key, snap_max_limit);
            if (ret) {
                len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
                if (len < 0) {
                    strcpy(err_str, "<error>");
                }
                goto out;
            }

            snprintf(key, sizeof(key), "volume%" PRId64 "-active-hard-limit",
                     count);
            ret = dict_set_uint64(rsp_dict, key, active_hard_limit);
            if (ret) {
                len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
                if (len < 0) {
                    strcpy(err_str, "<error>");
                }
                goto out;
            }

            snprintf(key, sizeof(key), "volume%" PRId64 "-snap-max-soft-limit",
                     count);
            ret = dict_set_uint64(rsp_dict, key, soft_limit_value);
            if (ret) {
                len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
                if (len < 0) {
                    strcpy(err_str, "<error>");
                }
                goto out;
            }
            count++;
        }

        ret = dict_set_uint64(rsp_dict, "voldisplaycount", count);
        if (ret) {
            snprintf(err_str, PATH_MAX, "Failed to set voldisplaycount");
            goto out;
        }
    } else {
        /*  For one volume */
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(err_str, PATH_MAX,
                     "Volume (%s) does not "
                     "exist",
                     volname);
            goto out;
        }

        snap_max_limit = volinfo->snap_max_hard_limit;
        if (snap_max_limit > opt_hard_max)
            active_hard_limit = opt_hard_max;
        else
            active_hard_limit = snap_max_limit;

        soft_limit_value = (opt_soft_max * active_hard_limit) / 100;

        keylen = snprintf(key, sizeof(key), "volume%" PRId64 "-volname", count);
        ret = dict_set_strn(rsp_dict, key, keylen, volinfo->volname);
        if (ret) {
            len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
            if (len < 0) {
                strcpy(err_str, "<error>");
            }
            goto out;
        }

        snprintf(key, sizeof(key), "volume%" PRId64 "-snap-max-hard-limit",
                 count);
        ret = dict_set_uint64(rsp_dict, key, snap_max_limit);
        if (ret) {
            len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
            if (len < 0) {
                strcpy(err_str, "<error>");
            }
            goto out;
        }

        snprintf(key, sizeof(key), "volume%" PRId64 "-active-hard-limit",
                 count);
        ret = dict_set_uint64(rsp_dict, key, active_hard_limit);
        if (ret) {
            len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
            if (len < 0) {
                strcpy(err_str, "<error>");
            }
            goto out;
        }

        snprintf(key, sizeof(key), "volume%" PRId64 "-snap-max-soft-limit",
                 count);
        ret = dict_set_uint64(rsp_dict, key, soft_limit_value);
        if (ret) {
            len = snprintf(err_str, PATH_MAX, "Failed to set %s", key);
            if (len < 0) {
                strcpy(err_str, "<error>");
            }
            goto out;
        }

        count++;

        ret = dict_set_uint64(rsp_dict, "voldisplaycount", count);
        if (ret) {
            snprintf(err_str, PATH_MAX, "Failed to set voldisplaycount");
            goto out;
        }
    }

    ret = dict_set_uint64(rsp_dict, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                          opt_hard_max);
    if (ret) {
        snprintf(err_str, PATH_MAX, "Failed to set %s in response dictionary",
                 GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
        goto out;
    }

    ret = dict_set_uint64(rsp_dict, GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT,
                          opt_soft_max);
    if (ret) {
        snprintf(err_str, PATH_MAX, "Failed to set %s in response dictionary",
                 GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT);
        goto out;
    }

    /* "auto-delete" might not be set by user explicitly,
     * in that case it's better to consider the default value.
     * Hence not erroring out if Key is not found.
     */
    ret = dict_get_strn(conf->opts, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE,
                        SLEN(GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE),
                        &auto_delete);

    ret = dict_set_dynstr_with_alloc(
        rsp_dict, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE, auto_delete);
    if (ret) {
        snprintf(err_str, PATH_MAX, "Failed to set %s in response dictionary",
                 GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE);
        goto out;
    }

    /* "snap-activate-on-create" might not be set by user explicitly,
     * in that case it's better to consider the default value.
     * Hence not erroring out if Key is not found.
     */
    ret = dict_get_strn(conf->opts, GLUSTERD_STORE_KEY_SNAP_ACTIVATE,
                        SLEN(GLUSTERD_STORE_KEY_SNAP_ACTIVATE), &snap_activate);

    ret = dict_set_dynstr_with_alloc(rsp_dict, GLUSTERD_STORE_KEY_SNAP_ACTIVATE,
                                     snap_activate);
    if (ret) {
        snprintf(err_str, PATH_MAX, "Failed to set %s in response dictionary",
                 GLUSTERD_STORE_KEY_SNAP_ACTIVATE);
        goto out;
    }

    ret = 0;
out:
    if (ret) {
        strncpy(op_errstr, err_str, len);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED, "%s",
               err_str);
    }
    return ret;
}

/* Third argument of scandir(used in glusterd_copy_geo_rep_session_files)
 * is filter function. As we don't want "." and ".." files present in the
 * directory, we are excliding these 2 files.
 * "file_select" function here does the job of filtering.
 */
int
file_select(const struct dirent *entry)
{
    if (entry == NULL)
        return (FALSE);

    if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
        return (FALSE);
    else
        return (TRUE);
}

int32_t
glusterd_copy_geo_rep_session_files(char *session, glusterd_volinfo_t *snap_vol)
{
    int32_t ret = -1;
    char snap_session_dir[PATH_MAX] = "";
    char georep_session_dir[PATH_MAX] = "";
    regex_t *reg_exp = NULL;
    int file_count = -1;
    struct dirent **files = {
        0,
    };
    xlator_t *this = THIS;
    int i = 0;
    char src_path[PATH_MAX] = "";
    char dest_path[PATH_MAX] = "";
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(session);
    GF_ASSERT(snap_vol);

    ret = snprintf(georep_session_dir, sizeof(georep_session_dir), "%s/%s/%s",
                   priv->workdir, GEOREP, session);
    if (ret < 0) { /* Negative value is an error */
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    ret = snprintf(snap_session_dir, sizeof(snap_session_dir), "%s/%s/%s/%s/%s",
                   priv->workdir, GLUSTERD_VOL_SNAP_DIR_PREFIX,
                   snap_vol->snapshot->snapname, GEOREP, session);
    if (ret < 0) { /* Negative value is an error */
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    ret = mkdir_p(snap_session_dir, 0755, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Creating directory %s failed", snap_session_dir);
        goto out;
    }

    /* TODO : good to have - Allocate in stack instead of heap */
    reg_exp = GF_CALLOC(1, sizeof(regex_t), gf_common_mt_regex_t);
    if (!reg_exp) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Failed to allocate memory for regular expression");
        goto out;
    }

    ret = regcomp(reg_exp, "(.*status$)|(.*conf$)\0", REG_EXTENDED);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REG_COMPILE_FAILED,
               "Failed to compile the regular expression");
        goto out;
    }

    /* If there are no files in a particular session then fail it*/
    file_count = scandir(georep_session_dir, &files, file_select, alphasort);
    if (file_count <= 0) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
               "Session files not present "
               "in %s",
               georep_session_dir);
        goto out;
    }

    /* Now compare the file name with regular expression to see if
     * there is a match
     */
    for (i = 0; i < file_count; i++) {
        if (regexec(reg_exp, files[i]->d_name, 0, NULL, 0))
            continue;

        ret = snprintf(src_path, sizeof(src_path), "%s/%s", georep_session_dir,
                       files[i]->d_name);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
            goto out;
        }

        ret = snprintf(dest_path, sizeof(dest_path), "%s/%s", snap_session_dir,
                       files[i]->d_name);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
            goto out;
        }

        ret = glusterd_copy_file(src_path, dest_path);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Could not copy file %s of session %s", files[i]->d_name,
                   session);
            goto out;
        }
    }
out:
    /* files are malloc'd by scandir, free them */
    if (file_count > 0) {
        while (file_count--) {
            free(files[file_count]);
        }
        free(files);
    }

    if (reg_exp)
        GF_FREE(reg_exp);

    return ret;
}

/* This function will take backup of the volume store
 * of the to-be restored volume. This will help us to
 * revert the operation if it fails.
 *
 * @param volinfo volinfo of the origin volume
 *
 * @return 0 on success and -1 on failure
 */
int
glusterd_snapshot_backup_vol(glusterd_volinfo_t *volinfo)
{
    char pathname[PATH_MAX] = "";
    int ret = -1;
    int op_ret = 0;
    char delete_path[PATH_MAX] = "";
    char trashdir[PATH_MAX] = "";
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    int32_t len = 0;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(volinfo);

    GLUSTERD_GET_VOLUME_DIR(pathname, volinfo, priv);

    len = snprintf(delete_path, sizeof(delete_path),
                   "%s/" GLUSTERD_TRASH "/vols-%s.deleted", priv->workdir,
                   volinfo->volname);
    if ((len < 0) || (len >= sizeof(delete_path))) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    len = snprintf(trashdir, sizeof(trashdir), "%s/" GLUSTERD_TRASH,
                   priv->workdir);
    if ((len < 0) || (len >= sizeof(trashdir))) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    /* Create trash folder if it is not there */
    ret = sys_mkdir(trashdir, 0755);
    if (ret && errno != EEXIST) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to create trash directory, reason : %s",
               strerror(errno));
        ret = -1;
        goto out;
    }

    /* Move the origin volume volder to the backup location */
    ret = sys_rename(pathname, delete_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Failed to rename snap "
               "directory %s to %s",
               pathname, delete_path);
        goto out;
    }

    /* Re-create an empty origin volume folder so that restore can
     * happen. */
    ret = sys_mkdir(pathname, 0755);
    if (ret && errno != EEXIST) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to create origin "
               "volume directory (%s), reason : %s",
               pathname, strerror(errno));
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    /* Save the actual return value */
    op_ret = ret;
    if (ret) {
        /* Revert the changes in case of failure */
        ret = sys_rmdir(pathname);
        if (ret) {
            gf_msg_debug(this->name, errno, "Failed to rmdir: %s", pathname);
        }

        ret = sys_rename(delete_path, pathname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                   "Failed to rename directory %s to %s", delete_path,
                   pathname);
        }

        ret = sys_rmdir(trashdir);
        if (ret) {
            gf_msg_debug(this->name, errno, "Failed to rmdir: %s", trashdir);
        }
    }

    gf_msg_trace(this->name, 0, "Returning %d", op_ret);

    return op_ret;
}

static int32_t
glusterd_copy_geo_rep_files(glusterd_volinfo_t *origin_vol,
                            glusterd_volinfo_t *snap_vol, dict_t *rsp_dict)
{
    int32_t ret = -1;
    int i = 0;
    xlator_t *this = THIS;
    char key[32] = "";
    char session[PATH_MAX] = "";
    char secondary[PATH_MAX] = "";
    char snapgeo_dir[PATH_MAX] = "";
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(origin_vol);
    GF_ASSERT(snap_vol);
    GF_ASSERT(rsp_dict);

    /* This condition is not satisfied if the volume
     * is secondary volume.
     */
    if (!origin_vol->gsync_secondaries) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_SECONDARY,
                NULL);
        ret = 0;
        goto out;
    }

    GLUSTERD_GET_SNAP_GEO_REP_DIR(snapgeo_dir, snap_vol->snapshot, priv);

    ret = sys_mkdir(snapgeo_dir, 0755);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Creating directory %s failed", snapgeo_dir);
        goto out;
    }

    for (i = 1; i <= origin_vol->gsync_secondaries->count; i++) {
        ret = snprintf(key, sizeof(key), "secondary%d", i);
        if (ret < 0) /* Negative value is an error */
            goto out;

        ret = glusterd_get_geo_rep_session(key, origin_vol->volname,
                                           origin_vol->gsync_secondaries,
                                           session, secondary);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GEOREP_GET_FAILED,
                   "Failed to get geo-rep session");
            goto out;
        }

        ret = glusterd_copy_geo_rep_session_files(session, snap_vol);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED,
                   "Failed to copy files"
                   " related to session %s",
                   session);
            goto out;
        }
    }

out:
    return ret;
}

/* This function will restore a snapshot volumes
 *
 * @param dict          dictionary containing snapshot restore request
 * @param op_errstr     In case of any failure error message will be returned
 *                      in this variable
 * @return              Negative value on Failure and 0 in success
 */
int
glusterd_snapshot_restore(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;
    int32_t volcount = -1;
    char *snapname = NULL;
    xlator_t *this = THIS;
    glusterd_volinfo_t *snap_volinfo = NULL;
    glusterd_volinfo_t *tmp = NULL;
    glusterd_volinfo_t *parent_volinfo = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_conf_t *priv = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get snap name");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (NULL == snap) {
        ret = gf_asprintf(op_errstr, "Snapshot (%s) does not exist", snapname);
        if (ret < 0) {
            goto out;
        }
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND, "%s",
               *op_errstr);
        ret = -1;
        goto out;
    }

    volcount = 0;
    cds_list_for_each_entry_safe(snap_volinfo, tmp, &snap->volumes, vol_list)
    {
        volcount++;
        ret = glusterd_volinfo_find(snap_volinfo->parent_volname,
                                    &parent_volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
                   "Could not get volinfo of %s", snap_volinfo->parent_volname);
            goto out;
        }

        ret = dict_set_dynstr_with_alloc(rsp_dict, "snapuuid",
                                         uuid_utoa(snap->snap_id));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set snap "
                   "uuid in response dictionary for %s snapshot",
                   snap->snapname);
            goto out;
        }

        ret = dict_set_dynstr_with_alloc(rsp_dict, "volname",
                                         snap_volinfo->parent_volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set snap "
                   "uuid in response dictionary for %s snapshot",
                   snap->snapname);
            goto out;
        }

        ret = dict_set_dynstr_with_alloc(rsp_dict, "volid",
                                         uuid_utoa(parent_volinfo->volume_id));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set snap "
                   "uuid in response dictionary for %s snapshot",
                   snap->snapname);
            goto out;
        }

        if (is_origin_glusterd(dict) == _gf_true) {
            /* From origin glusterd check if      *
             * any peers with snap bricks is down */
            ret = glusterd_find_missed_snap(rsp_dict, snap_volinfo,
                                            &priv->peers,
                                            GF_SNAP_OPTION_TYPE_RESTORE);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSED_SNAP_GET_FAIL,
                       "Failed to find missed snap restores");
                goto out;
            }
        }
        /* During snapshot restore, mount point for stopped snap
         * should exist as it is required to set extended attribute.
         */
        ret = glusterd_recreate_vol_brick_mounts(this, snap_volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRK_MNT_RECREATE_FAIL,
                   "Failed to recreate brick mounts for %s", snap->snapname);
            goto out;
        }

        ret = gd_restore_snap_volume(dict, rsp_dict, parent_volinfo,
                                     snap_volinfo, volcount);
        if (ret) {
            /* No need to update op_errstr because it is assumed
             * that the called function will do that in case of
             * failure.
             */
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
                   "Failed to restore "
                   "snap for %s",
                   snapname);
            goto out;
        }

        /* Detach the volinfo from priv->volumes, so that no new
         * command can ref it any more and then unref it.
         */
        cds_list_del_init(&parent_volinfo->vol_list);
        glusterd_volinfo_unref(parent_volinfo);
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
int32_t
glusterd_snapshot_restore_prevalidate(dict_t *dict, char **op_errstr,
                                      uint32_t *op_errno, dict_t *rsp_dict)
{
    int ret = -1;
    int32_t i = 0;
    int32_t volcount = 0;
    int32_t brick_count = 0;
    gf_boolean_t snap_restored = _gf_false;
    char key[64] = "";
    int keylen;
    char *volname = NULL;
    char *snapname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_snap_t *snap = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);
    GF_ASSERT(rsp_dict);

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get "
               "snap name");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (NULL == snap) {
        ret = gf_asprintf(op_errstr, "Snapshot (%s) does not exist", snapname);
        *op_errno = EG_SNAPEXST;
        if (ret < 0) {
            goto out;
        }
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND, "%s",
               *op_errstr);
        ret = -1;
        goto out;
    }

    snap_restored = snap->snap_restored;

    if (snap_restored) {
        ret = gf_asprintf(op_errstr,
                          "Snapshot (%s) is already "
                          "restored",
                          snapname);
        if (ret < 0) {
            goto out;
        }
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
               *op_errstr);
        ret = -1;
        goto out;
    }

    ret = dict_set_strn(rsp_dict, "snapname", SLEN("snapname"), snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set "
               "snap name(%s)",
               snapname);
        goto out;
    }

    ret = dict_get_int32n(dict, "volcount", SLEN("volcount"), &volcount);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get volume count");
        goto out;
    }

    /* Snapshot restore will only work if all the volumes,
       that are part of the snapshot, are stopped. */
    for (i = 1; i <= volcount; ++i) {
        keylen = snprintf(key, sizeof(key), "volname%d", i);
        ret = dict_get_strn(dict, key, keylen, &volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to "
                   "get volume name");
            goto out;
        }

        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            ret = gf_asprintf(op_errstr,
                              "Volume (%s) "
                              "does not exist",
                              volname);
            *op_errno = EG_NOVOL;
            if (ret < 0) {
                goto out;
            }
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND, "%s",
                   *op_errstr);
            ret = -1;
            goto out;
        }

        if (glusterd_is_volume_started(volinfo)) {
            ret = gf_asprintf(
                op_errstr,
                "Volume (%s) has been "
                "started. Volume needs to be stopped before restoring "
                "a snapshot.",
                volname);
            *op_errno = EG_VOLRUN;
            if (ret < 0) {
                goto out;
            }
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
                   *op_errstr);
            ret = -1;
            goto out;
        }

        /* Take backup of the volinfo folder */
        ret = glusterd_snapshot_backup_vol(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
                   "Failed to backup "
                   "volume backend files for %s volume",
                   volinfo->volname);
            goto out;
        }
    }

    /* Get brickinfo for snap_volumes */
    volcount = 0;
    cds_list_for_each_entry(volinfo, &snap->volumes, vol_list)
    {
        volcount++;
        brick_count = 0;

        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            brick_count++;
            if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
                continue;

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.path", volcount,
                              brick_count);
            ret = dict_set_strn(rsp_dict, key, keylen, brickinfo->path);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.snap_status",
                              volcount, brick_count);
            ret = dict_set_int32n(rsp_dict, key, keylen,
                                  brickinfo->snap_status);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.device_path",
                              volcount, brick_count);
            ret = dict_set_strn(rsp_dict, key, keylen, brickinfo->device_path);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.fs_type",
                              volcount, brick_count);
            ret = dict_set_strn(rsp_dict, key, keylen, brickinfo->fstype);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.snap_type",
                              volcount, brick_count);
            ret = dict_set_strn(rsp_dict, key, keylen, brickinfo->snap_type);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.mnt_opts",
                              volcount, brick_count);
            ret = dict_set_strn(rsp_dict, key, keylen, brickinfo->mnt_opts);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }
        }

        keylen = snprintf(key, sizeof(key), "snap%d.brick_count", volcount);
        ret = dict_set_int32n(rsp_dict, key, keylen, brick_count);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }
    }

    ret = dict_set_int32n(rsp_dict, "volcount", SLEN("volcount"), volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s", key);
        goto out;
    }

out:
    return ret;
}

int
snap_max_hard_limits_validate(dict_t *dict, char *volname, uint64_t value,
                              char **op_errstr)
{
    char err_str[PATH_MAX] = "";
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int ret = -1;
    uint64_t max_limit = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
    xlator_t *this = THIS;
    uint64_t opt_hard_max = GLUSTERD_SNAPS_MAX_HARD_LIMIT;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    conf = this->private;

    GF_ASSERT(conf);

    if (volname) {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (!ret) {
            if (volinfo->is_snap_volume) {
                ret = -1;
                snprintf(err_str, PATH_MAX,
                         "%s is a snap volume. Configuring "
                         "snap-max-hard-limit for a snap "
                         "volume is prohibited.",
                         volname);
                goto out;
            }
        }
    }

    /* "snap-max-hard-limit" might not be set by user explicitly,
     * in that case it's better to use the default value.
     * Hence not erroring out if Key is not found.
     */
    ret = dict_get_uint64(conf->opts, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                          &opt_hard_max);
    if (ret) {
        ret = 0;
        gf_msg_debug(this->name, 0,
                     "%s is not present in "
                     "opts dictionary",
                     GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
    }

    /* volume snap-max-hard-limit cannot exceed system snap-max-hard-limit.
     * Hence during prevalidate following checks are made to ensure the
     * snap-max-hard-limit set on one particular volume does not
     * exceed snap-max-hard-limit set globally (system limit).
     */
    if (value && volname) {
        max_limit = opt_hard_max;
    }

    if (value > max_limit) {
        ret = -1;
        snprintf(err_str, PATH_MAX,
                 "Invalid snap-max-hard-limit "
                 "%" PRIu64 ". Expected range 1 - %" PRIu64,
                 value, max_limit);
        goto out;
    }

    ret = 0;
out:
    if (ret) {
        *op_errstr = gf_strdup(err_str);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
               err_str);
    }
    return ret;
}

int
glusterd_snapshot_config_prevalidate(dict_t *dict, char **op_errstr,
                                     uint32_t *op_errno)
{
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    int config_command = 0;
    char err_str[PATH_MAX] = "";
    glusterd_conf_t *conf = NULL;
    uint64_t hard_limit = 0;
    uint64_t soft_limit = 0;
    gf_loglevel_t loglevel = GF_LOG_ERROR;
    uint64_t max_limit = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
    int32_t cur_auto_delete = 0;
    int32_t req_auto_delete = 0;
    int32_t cur_snap_activate = 0;
    int32_t req_snap_activate = 0;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    conf = this->private;

    GF_ASSERT(conf);

    ret = dict_get_int32n(dict, "config-command", SLEN("config-command"),
                          &config_command);
    if (ret) {
        snprintf(err_str, sizeof(err_str), "failed to get config-command type");
        goto out;
    }

    if (config_command != GF_SNAP_CONFIG_TYPE_SET) {
        ret = 0;
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (volname) {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(err_str, sizeof(err_str), "Volume (%s) does not exist.",
                     volname);
            *op_errno = EG_NOVOL;
            goto out;
        }
    }

    /* config values snap-max-hard-limit and snap-max-soft-limit are
     * optional and hence we are not erroring out if values are not
     * present
     */
    gd_get_snap_conf_values_if_present(dict, &hard_limit, &soft_limit);

    if (hard_limit) {
        /* Validations for snap-max-hard-limits */
        ret = snap_max_hard_limits_validate(dict, volname, hard_limit,
                                            op_errstr);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HARD_LIMIT_SET_FAIL,
                   "snap-max-hard-limit validation failed.");
            *op_errno = EINVAL;
            goto out;
        }
    }

    if (soft_limit) {
        max_limit = GLUSTERD_SNAPS_MAX_SOFT_LIMIT_PERCENT;
        if (soft_limit > max_limit) {
            ret = -1;
            snprintf(err_str, PATH_MAX,
                     "Invalid "
                     "snap-max-soft-limit "
                     "%" PRIu64 ". Expected range 1 - %" PRIu64,
                     soft_limit, max_limit);
            *op_errno = EINVAL;
            goto out;
        }
    }

    if (hard_limit || soft_limit) {
        ret = 0;
        goto out;
    }

    if (dict_getn(dict, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE,
                  SLEN(GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE))) {
        req_auto_delete = dict_get_str_boolean(
            dict, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE, _gf_false);
        if (req_auto_delete < 0) {
            ret = -1;
            snprintf(err_str, sizeof(err_str),
                     "Please enter a "
                     "valid boolean value for auto-delete");
            *op_errno = EINVAL;
            goto out;
        }

        /* Ignoring the error as the auto-delete is optional and
           might not be present in the options dictionary.*/
        cur_auto_delete = dict_get_str_boolean(
            conf->opts, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE, _gf_false);

        if (cur_auto_delete == req_auto_delete) {
            ret = -1;
            if (cur_auto_delete == _gf_true)
                snprintf(err_str, sizeof(err_str),
                         "auto-delete is already enabled");
            else
                snprintf(err_str, sizeof(err_str),
                         "auto-delete is already disabled");
            *op_errno = EINVAL;
            goto out;
        }
    } else if (dict_getn(dict, GLUSTERD_STORE_KEY_SNAP_ACTIVATE,
                         SLEN(GLUSTERD_STORE_KEY_SNAP_ACTIVATE))) {
        req_snap_activate = dict_get_str_boolean(
            dict, GLUSTERD_STORE_KEY_SNAP_ACTIVATE, _gf_false);
        if (req_snap_activate < 0) {
            ret = -1;
            snprintf(err_str, sizeof(err_str),
                     "Please enter a "
                     "valid boolean value for activate-on-create");
            *op_errno = EINVAL;
            goto out;
        }

        /* Ignoring the error as the activate-on-create is optional and
           might not be present in the options dictionary.*/
        cur_snap_activate = dict_get_str_boolean(
            conf->opts, GLUSTERD_STORE_KEY_SNAP_ACTIVATE, _gf_false);

        if (cur_snap_activate == req_snap_activate) {
            ret = -1;
            if (cur_snap_activate == _gf_true)
                snprintf(err_str, sizeof(err_str),
                         "activate-on-create is already enabled");
            else
                snprintf(err_str, sizeof(err_str),
                         "activate-on-create is already disabled");
            *op_errno = EINVAL;
            goto out;
        }
    } else {
        ret = -1;
        snprintf(err_str, sizeof(err_str), "Invalid option");
        *op_errno = EINVAL;
        goto out;
    }

    ret = 0;
out:

    if (ret && err_str[0] != '\0') {
        gf_msg(this->name, loglevel, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
               err_str);
        *op_errstr = gf_strdup(err_str);
    }

    return ret;
}

/* This function will be called from RPC handler routine.
 * This function is responsible for getting the requested
 * snapshot config into the dictionary.
 *
 * @param req   RPC request object. Required for sending a response back.
 * @param op    glusterd operation. Required for sending a response back.
 * @param dict  pointer to dictionary which will contain both
 *              request and response key-pair values.
 * @return -1 on error and 0 on success
 */
int
glusterd_handle_snapshot_config(rpcsvc_request_t *req, glusterd_op_t op,
                                dict_t *dict, char *err_str, size_t len)
{
    int32_t ret = -1;
    char *volname = NULL;
    xlator_t *this = THIS;
    int config_command = 0;

    GF_VALIDATE_OR_GOTO(this->name, req, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);

    /* TODO : Type of lock to be taken when we are setting
     * limits system wide
     */
    ret = dict_get_int32n(dict, "config-command", SLEN("config-command"),
                          &config_command);
    if (ret) {
        snprintf(err_str, len, "Failed to get config-command type");
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=config-command", NULL);
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    switch (config_command) {
        case GF_SNAP_CONFIG_TYPE_SET:
            if (!volname) {
                ret = dict_set_int32n(dict, "hold_vol_locks",
                                      SLEN("hold_vol_locks"), _gf_false);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                           "Unable to set hold_vol_locks value "
                           "as _gf_false");
                    goto out;
                }
            }
            ret = glusterd_mgmt_v3_initiate_all_phases(req, op, dict);
            break;
        case GF_SNAP_CONFIG_DISPLAY:
            /* Reading data from local node only */
            ret = snap_max_limits_display_commit(dict, volname, err_str, len);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HARD_LIMIT_SET_FAIL,
                       "snap-max-limit "
                       "display commit failed.");
                goto out;
            }

            /* If everything is successful then send the response
             * back to cli
             */
            ret = glusterd_op_send_cli_response(op, 0, 0, req, dict, err_str);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_CLI_RESP,
                       "Failed to send cli "
                       "response");
                goto out;
            }

            break;
        default:
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_COMMAND_NOT_FOUND,
                   "Unknown config type");
            ret = -1;
            break;
    }
out:
    return ret;
}
int
glusterd_snap_create_clone_pre_val_use_rsp_dict(dict_t *dst, dict_t *src)
{
    char *snap_brick_dir = NULL;
    char *snap_device = NULL;
    char key[64] = "";
    int keylen;
    char *value = "";
    char snapbrckcnt[PATH_MAX] = "";
    char snapbrckord[PATH_MAX] = "";
    int ret = -1;
    int64_t i = -1;
    int64_t j = -1;
    int64_t volume_count = 0;
    int64_t brick_count = 0;
    int64_t brick_order = 0;
    xlator_t *this = THIS;
    int32_t brick_online = 0;

    GF_ASSERT(dst);
    GF_ASSERT(src);

    ret = dict_get_int64(src, "volcount", &volume_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to "
               "get the volume count");
        goto out;
    }

    for (i = 0; i < volume_count; i++) {
        ret = snprintf(snapbrckcnt, sizeof(snapbrckcnt) - 1,
                       "vol%" PRId64 "_brickcount", i + 1);
        ret = dict_get_int64(src, snapbrckcnt, &brick_count);
        if (ret) {
            gf_msg_trace(this->name, 0,
                         "No bricks for this volume in this dict");
            continue;
        }

        for (j = 0; j < brick_count; j++) {
            /* Fetching data from source dict */
            snprintf(key, sizeof(key), "vol%" PRId64 ".brickdir%" PRId64, i + 1,
                     j);
            ret = dict_get_ptr(src, key, (void **)&snap_brick_dir);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch %s", key);
                continue;
            }

            /* Fetching brick order from source dict */
            snprintf(snapbrckord, sizeof(snapbrckord) - 1,
                     "vol%" PRId64 ".brick%" PRId64 ".order", i + 1, j);
            ret = dict_get_int64(src, snapbrckord, &brick_order);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get brick order");
                goto out;
            }

            snprintf(key, sizeof(key), "vol%" PRId64 ".brickdir%" PRId64, i + 1,
                     brick_order);
            ret = dict_set_dynstr_with_alloc(dst, key, snap_brick_dir);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "vol%" PRId64 ".fstype%" PRId64,
                              i + 1, j);
            ret = dict_get_strn(src, key, keylen, &value);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch %s", key);
                continue;
            }

            snprintf(key, sizeof(key), "vol%" PRId64 ".fstype%" PRId64, i + 1,
                     brick_order);
            ret = dict_set_dynstr_with_alloc(dst, key, value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            snprintf(key, sizeof(key), "vol%" PRId64 ".snap_type%" PRId64,
                     i + 1, j);
            ret = dict_get_str(src, key, &value);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch %s", key);
                continue;
            }

            snprintf(key, sizeof(key), "vol%" PRId64 ".snap_type%" PRId64,
                     i + 1, brick_order);
            ret = dict_set_dynstr_with_alloc(dst, key, value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key),
                              "vol%" PRId64 ".mnt_opts%" PRId64, i + 1, j);
            ret = dict_get_strn(src, key, keylen, &value);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch %s", key);
                continue;
            }

            snprintf(key, sizeof(key), "vol%" PRId64 ".mnt_opts%" PRId64, i + 1,
                     brick_order);
            ret = dict_set_dynstr_with_alloc(dst, key, value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            snprintf(key, sizeof(key),
                     "vol%" PRId64 ".brick_snapdevice%" PRId64, i + 1, j);
            ret = dict_get_ptr(src, key, (void **)&snap_device);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch snap_device");
                goto out;
            }

            snprintf(key, sizeof(key),
                     "vol%" PRId64 ".brick_snapdevice%" PRId64, i + 1,
                     brick_order);
            ret = dict_set_dynstr_with_alloc(dst, key, snap_device);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key),
                              "vol%" PRId64 ".brick%" PRId64 ".status", i + 1,
                              brick_order);
            ret = dict_get_int32n(src, key, keylen, &brick_online);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "failed to "
                       "get the brick status");
                goto out;
            }

            ret = dict_set_int32n(dst, key, keylen, brick_online);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "failed to "
                       "set the brick status");
                goto out;
            }
            brick_online = 0;
        }
    }
    ret = 0;
out:

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Aggregate brickinfo's of the snap volumes to be restored from */
int32_t
glusterd_snap_restore_use_rsp_dict(dict_t *dst, dict_t *src)
{
    char key[64] = "";
    int keylen;
    char *strvalue = NULL;
    int32_t value = -1;
    int32_t i = -1;
    int32_t j = -1;
    int32_t vol_count = -1;
    int32_t brickcount = -1;
    int32_t ret = -1;
    xlator_t *this = THIS;

    if (!dst || !src) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Source or Destination "
               "dict is empty.");
        goto out;
    }

    ret = dict_get_int32(src, "volcount", &vol_count);
    if (ret) {
        gf_msg_debug(this->name, 0, "No volumes");
        ret = 0;
        goto out;
    }

    for (i = 1; i <= vol_count; i++) {
        keylen = snprintf(key, sizeof(key), "snap%d.brick_count", i);
        ret = dict_get_int32n(src, key, keylen, &brickcount);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get %s", key);
            goto out;
        }

        for (j = 1; j <= brickcount; j++) {
            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.path", i, j);
            ret = dict_get_strn(src, key, keylen, &strvalue);
            if (ret) {
                /* The brickinfo will be present in
                 * another rsp_dict */
                gf_msg_debug(this->name, 0, "%s not present", key);
                ret = 0;
                continue;
            }
            ret = dict_set_dynstr_with_alloc(dst, key, strvalue);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.snap_status", i,
                              j);
            ret = dict_get_int32n(src, key, keylen, &value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get %s", key);
                goto out;
            }
            ret = dict_set_int32n(dst, key, keylen, value);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.device_path", i,
                              j);
            ret = dict_get_strn(src, key, keylen, &strvalue);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get %s", key);
                goto out;
            }
            ret = dict_set_dynstr_with_alloc(dst, key, strvalue);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.fs_type", i, j);
            ret = dict_get_strn(src, key, keylen, &strvalue);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get %s", key);
                goto out;
            }
            ret = dict_set_dynstr_with_alloc(dst, key, strvalue);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to set %s", key);
                goto out;
            }

            keylen = snprintf(key, sizeof(key), "snap%d.brick%d.mnt_opts", i,
                              j);
            ret = dict_get_strn(src, key, keylen, &strvalue);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get %s", key);
                goto out;
            }
            ret = dict_set_dynstr_with_alloc(dst, key, strvalue);
            if (ret) {
                gf_msg_debug(this->name, 0, "Failed to set %s", key);
                goto out;
            }
        }
    }

out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_snap_pre_validate_use_rsp_dict(dict_t *dst, dict_t *src)
{
    int ret = -1;
    int32_t snap_command = 0;
    xlator_t *this = THIS;

    if (!dst || !src) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "Source or Destination "
               "dict is empty.");
        goto out;
    }

    ret = dict_get_int32n(dst, "type", SLEN("type"), &snap_command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "unable to get the type of "
               "the snapshot command");
        goto out;
    }

    switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
        case GF_SNAP_OPTION_TYPE_CLONE:
            ret = glusterd_snap_create_clone_pre_val_use_rsp_dict(dst, src);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to use "
                       "rsp dict");
                goto out;
            }
            break;
        case GF_SNAP_OPTION_TYPE_RESTORE:
            ret = glusterd_snap_restore_use_rsp_dict(dst, src);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RSP_DICT_USE_FAIL,
                       "Unable to use "
                       "rsp dict");
                goto out;
            }
            break;
        default:
            break;
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_add_brick_status_to_dict(dict_t *dict, glusterd_volinfo_t *volinfo,
                                  glusterd_brickinfo_t *brickinfo,
                                  char *key_prefix)
{
    char pidfile[PATH_MAX] = "";
    int32_t brick_online = 0;
    pid_t pid = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    int ret = -1;

    GF_ASSERT(dict);
    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);

    conf = this->private;
    GF_ASSERT(conf);

    if (!key_prefix) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "key prefix is NULL");
        goto out;
    }

    GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo, conf);

    brick_online = gf_is_service_running(pidfile, &pid);

    ret = dict_set_int32(dict, key_prefix, brick_online);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s", key_prefix);
        goto out;
    }
    brick_online = 0;

    ret = 0;

out:
    return ret;
}

/* This function will check whether the given device
 * is a thinly provisioned LV or not.
 *
 * @param device        LV device path
 *
 * @return              _gf_true if LV is thin else _gf_false
 */
gf_boolean_t
glusterd_is_thinp_brick(char *device, uint32_t *op_errno)
{
    int ret = -1;
    char msg[1024] = "";
    char pool_name[PATH_MAX] = "";
    char *ptr = NULL;
    xlator_t *this = THIS;
    runner_t runner = {
        0,
    };
    gf_boolean_t is_thin = _gf_false;

    GF_VALIDATE_OR_GOTO(this->name, device, out);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    snprintf(msg, sizeof(msg), "Get thin pool name for device %s", device);

    runinit(&runner);

    runner_add_args(&runner, "lvs", "--noheadings", "-o", "pool_lv", device,
                    NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);

    ret = runner_start(&runner);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_TPOOL_GET_FAIL,
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
    if (!is_thin)
        *op_errno = EG_NOTTHINP;

    return is_thin;
}

int
glusterd_snap_create_clone_common_prevalidate(
    dict_t *rsp_dict, int flags, char *snapname, char *err_str,
    char *snap_volname, int64_t volcount, glusterd_volinfo_t *volinfo,
    gf_loglevel_t *loglevel, int clone, uint32_t *op_errno)
{
    char *device = NULL;
    char *orig_device = NULL;
    char key[128] = "";
    int ret = -1;
    int64_t i = 1;
    int64_t brick_order = 0;
    int64_t brick_count = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    int32_t len = 0;

    conf = this->private;
    GF_ASSERT(conf);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    if (!snapname || !volinfo) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Failed to validate "
               "snapname or volume information");
        ret = -1;
        goto out;
    }

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
            brick_order++;
            continue;
        }

        if (!glusterd_is_brick_started(brickinfo)) {
            if (!clone) {
                snprintf(err_str, PATH_MAX,
                         "One or more bricks are not running. "
                         "Please run volume status command to see "
                         "brick status.\n"
                         "All bricks have to be online to take a snapshot."
                         "Please start the stopped brick "
                         "and then issue snapshot create command.");
                gf_smsg(
                    this->name, GF_LOG_ERROR, errno, GD_MSG_BRICK_NOT_RUNNING,
                    "Please run volume status command to see brick "
                    "status. All bricks have to be online to take a snapshot."
                    "Please start the stopped brick and then issue "
                    "snapshot create command.",
                    NULL);
            } else {
                snprintf(err_str, PATH_MAX,
                         "One or more bricks are not running. "
                         "Please run snapshot status command to see "
                         "brick status.\n"
                         "Please start the stopped brick "
                         "and then issue snapshot clone "
                         "command ");
                gf_smsg(this->name, GF_LOG_ERROR, errno,
                        GD_MSG_BRICK_NOT_RUNNING,
                        "Please run snapshot status command to see brick "
                        "status. Please start the stopped brick and then issue "
                        "snapshot clone command.",
                        NULL);
            }
            *op_errno = EG_BRCKDWN;
            ret = -1;
            goto out;
        }

        orig_device = glusterd_get_brick_mount_device(brickinfo->path);
        if (!orig_device) {
            len = snprintf(err_str, PATH_MAX,
                           "getting device name for the brick "
                           "%s:%s failed",
                           brickinfo->hostname, brickinfo->path);
            if (len < 0) {
                strcpy(err_str, "<error>");
            }
            gf_smsg(this->name, GF_LOG_ERROR, errno,
                    GD_MSG_BRK_MNTPATH_GET_FAIL,
                    "Brick_hostname=%s, Brick_path=%s", brickinfo->hostname,
                    brickinfo->path, NULL);
            ret = -1;
            goto out;
        }

        if (!glusterd_snapshot_probe(brickinfo->path, brickinfo)) {
            snprintf(err_str, PATH_MAX,
                     "Snapshots not supported for"
                     " all bricks in volume %s.",
                     volinfo->volname);
            *op_errno = EG_NOTTHINP;
            ret = -1;
            goto out;
        }

        device = brickinfo->snap->device(orig_device, snap_volname,
                                         brick_count);
        if (!device) {
            snprintf(err_str, PATH_MAX,
                     "cannot copy the snapshot device "
                     "name (volname: %s, snapname: %s)",
                     volinfo->volname, snapname);
            gf_smsg(this->name, GF_LOG_ERROR, errno,
                    GD_MSG_SNAP_DEVICE_NAME_GET_FAIL, "Volname=%s, Snapname=%s",
                    volinfo->volname, snapname, NULL);
            *loglevel = GF_LOG_WARNING;
            ret = -1;
            goto out;
        }

        GF_FREE(orig_device);
        orig_device = NULL;

        snprintf(key, sizeof(key), "vol%" PRId64 ".brick_snapdevice%" PRId64, i,
                 brick_count);
        ret = dict_set_dynstr_with_alloc(rsp_dict, key, device);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }

        ret = glusterd_update_mntopts(brickinfo->path, brickinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRK_MOUNTOPTS_FAIL,
                   "Failed to "
                   "update mount options for %s brick",
                   brickinfo->path);
        }

        snprintf(key, sizeof(key), "vol%" PRId64 ".fstype%" PRId64, i,
                 brick_count);
        ret = dict_set_dynstr_with_alloc(rsp_dict, key, brickinfo->fstype);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }

        snprintf(key, sizeof(key), "vol%" PRId64 ".snap_type%" PRId64, i,
                 brick_count);
        ret = dict_set_dynstr_with_alloc(rsp_dict, key, brickinfo->snap_type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }

        snprintf(key, sizeof(key), "vol%" PRId64 ".mnt_opts%" PRId64, i,
                 brick_count);
        ret = dict_set_dynstr_with_alloc(rsp_dict, key, brickinfo->mnt_opts);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }

        snprintf(key, sizeof(key), "vol%" PRId64 ".brickdir%" PRId64, i,
                 brick_count);
        ret = dict_set_dynstr_with_alloc(rsp_dict, key, brickinfo->mount_dir);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }

        snprintf(key, sizeof(key) - 1, "vol%" PRId64 ".brick%" PRId64 ".order",
                 i, brick_count);
        ret = dict_set_int64(rsp_dict, key, brick_order);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }

        snprintf(key, sizeof(key), "vol%" PRId64 ".brick%" PRId64 ".status", i,
                 brick_order);

        ret = glusterd_add_brick_status_to_dict(rsp_dict, volinfo, brickinfo,
                                                key);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "failed to "
                   "add brick status to dict");
            goto out;
        }
        brick_count++;
        brick_order++;
        if (device) {
            GF_FREE(device);
            device = NULL;
        }
    }
    snprintf(key, sizeof(key) - 1, "vol%" PRId64 "_brickcount", volcount);
    ret = dict_set_int64(rsp_dict, key, brick_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s", key);
        goto out;
    }
    ret = 0;
out:
    if (orig_device)
        GF_FREE(orig_device);

    if (device)
        GF_FREE(device);

    return ret;
}

int
glusterd_snapshot_clone_prevalidate(dict_t *dict, char **op_errstr,
                                    dict_t *rsp_dict, uint32_t *op_errno)
{
    char *clonename = NULL;
    char *snapname = NULL;
    char device_name[64] = "";
    glusterd_snap_t *snap = NULL;
    char err_str[PATH_MAX] = "";
    int ret = -1;
    int64_t volcount = 1;
    glusterd_volinfo_t *snap_vol = NULL;
    xlator_t *this = THIS;
    uuid_t *snap_volid = NULL;
    gf_loglevel_t loglevel = GF_LOG_ERROR;
    glusterd_volinfo_t *volinfo = NULL;

    GF_ASSERT(op_errstr);
    GF_ASSERT(dict);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    ret = dict_get_strn(dict, "clonename", SLEN("clonename"), &clonename);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Failed to "
                 "get the clone name");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        snprintf(err_str, sizeof(err_str), "Failed to get snapname");
        goto out;
    }

    ret = glusterd_volinfo_find(clonename, &volinfo);
    if (!ret) {
        ret = -1;
        snprintf(err_str, sizeof(err_str),
                 "Volume with name:%s "
                 "already exists",
                 clonename);
        *op_errno = EG_VOLEXST;
        goto out;
    }
    /* need to find snap volinfo*/
    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        ret = -1;
        snprintf(err_str, sizeof(err_str),
                 "Failed to find :%s "
                 "snap",
                 snapname);
        goto out;
    }

    /* TODO : As of now there is only one volume in snapshot.
     * Change this when multiple volume snapshot is introduced
     */
    snap_vol = list_entry(snap->volumes.next, glusterd_volinfo_t, vol_list);
    if (!snap_vol) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Failed to get snap "
               "volinfo %s",
               snap->snapname);
        goto out;
    }

    if (!glusterd_is_volume_started(snap_vol)) {
        snprintf(err_str, sizeof(err_str),
                 "Snapshot %s is "
                 "not activated",
                 snap->snapname);
        loglevel = GF_LOG_WARNING;
        *op_errno = EG_VOLSTP;
        goto out;
    }

    ret = dict_get_bin(dict, "vol1_volid", (void **)&snap_volid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch snap_volid");
        goto out;
    }

    GLUSTERD_GET_UUID_NOHYPHEN(device_name, *snap_volid);

    /* Adding snap bricks mount paths to the dict */
    ret = glusterd_snap_create_clone_common_prevalidate(
        rsp_dict, 0, snapname, err_str, device_name, 1, snap_vol, &loglevel, 1,
        op_errno);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PRE_VALIDATION_FAIL,
               "Failed to pre validate");
        goto out;
    }

    ret = dict_set_int64(rsp_dict, "volcount", volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set volcount");
        goto out;
    }

out:

    if (ret && err_str[0] != '\0') {
        gf_msg(this->name, loglevel, 0, GD_MSG_SNAP_CLONE_PREVAL_FAILED, "%s",
               err_str);
        *op_errstr = gf_strdup(err_str);
    }

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_snapshot_create_prevalidate(dict_t *dict, char **op_errstr,
                                     dict_t *rsp_dict, uint32_t *op_errno)
{
    char *volname = NULL;
    char *snapname = NULL;
    char key[64] = "";
    int keylen;
    char snap_volname[64] = "";
    char err_str[PATH_MAX] = "";
    int ret = -1;
    int64_t i = 0;
    int64_t volcount = 0;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    uuid_t *snap_volid = NULL;
    gf_loglevel_t loglevel = GF_LOG_ERROR;
    glusterd_conf_t *conf = NULL;
    int64_t effective_max_limit = 0;
    int flags = 0;
    uint64_t opt_hard_max = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
    char *description = NULL;

    GF_ASSERT(op_errstr);
    conf = this->private;
    GF_ASSERT(conf);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    ret = dict_get_int64(dict, "volcount", &volcount);
    if (ret) {
        snprintf(err_str, sizeof(err_str),
                 "Failed to "
                 "get the volume count");
        goto out;
    }
    if (volcount <= 0) {
        snprintf(err_str, sizeof(err_str),
                 "Invalid volume count %" PRId64 " supplied", volcount);
        ret = -1;
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        snprintf(err_str, sizeof(err_str), "Failed to get snapname");
        goto out;
    }

    ret = dict_get_strn(dict, "description", SLEN("description"), &description);
    if (description && !(*description)) {
        /* description should have a non-null value */
        ret = -1;
        snprintf(err_str, sizeof(err_str),
                 "Snapshot cannot be "
                 "created with empty description");
        goto out;
    }

    ret = dict_get_int32n(dict, "flags", SLEN("flags"), &flags);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get flags");
        goto out;
    }

    if (glusterd_find_snap_by_name(snapname)) {
        ret = -1;
        snprintf(err_str, sizeof(err_str),
                 "Snapshot %s already "
                 "exists",
                 snapname);
        *op_errno = EG_SNAPEXST;
        goto out;
    }

    for (i = 1; i <= volcount; i++) {
        keylen = snprintf(key, sizeof(key), "volname%" PRId64, i);
        ret = dict_get_strn(dict, key, keylen, &volname);
        if (ret) {
            snprintf(err_str, sizeof(err_str), "failed to get volume name");
            goto out;
        }
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(err_str, sizeof(err_str), "Volume (%s) does not exist ",
                     volname);
            *op_errno = EG_NOVOL;
            goto out;
        }

        ret = -1;
        if (!glusterd_is_volume_started(volinfo)) {
            snprintf(err_str, sizeof(err_str),
                     "volume %s is "
                     "not started",
                     volinfo->volname);
            loglevel = GF_LOG_WARNING;
            *op_errno = EG_VOLSTP;
            goto out;
        }

        if (glusterd_is_defrag_on(volinfo)) {
            snprintf(err_str, sizeof(err_str),
                     "rebalance process is running for the "
                     "volume %s",
                     volname);
            loglevel = GF_LOG_WARNING;
            *op_errno = EG_RBALRUN;
            goto out;
        }

        if (gd_vol_is_geo_rep_active(volinfo)) {
            snprintf(err_str, sizeof(err_str),
                     "geo-replication session is running for "
                     "the volume %s. Session needs to be "
                     "stopped before taking a snapshot.",
                     volname);
            loglevel = GF_LOG_WARNING;
            *op_errno = EG_GEOREPRUN;
            goto out;
        }

        if (volinfo->is_snap_volume == _gf_true) {
            snprintf(err_str, sizeof(err_str), "Volume %s is a snap volume",
                     volname);
            loglevel = GF_LOG_WARNING;
            *op_errno = EG_ISSNAP;
            goto out;
        }

        /* "snap-max-hard-limit" might not be set by user explicitly,
         * in that case it's better to consider the default value.
         * Hence not erroring out if Key is not found.
         */
        ret = dict_get_uint64(
            conf->opts, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT, &opt_hard_max);
        if (ret) {
            ret = 0;
            gf_msg_debug(this->name, 0,
                         "%s is not present "
                         "in opts dictionary",
                         GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
        }

        if (volinfo->snap_max_hard_limit < opt_hard_max)
            effective_max_limit = volinfo->snap_max_hard_limit;
        else
            effective_max_limit = opt_hard_max;

        if (volinfo->snap_count >= effective_max_limit) {
            ret = -1;
            snprintf(err_str, sizeof(err_str),
                     "The number of existing snaps has reached "
                     "the effective maximum limit of %" PRIu64
                     ", "
                     "for the volume (%s). Please delete few "
                     "snapshots before taking further snapshots.",
                     effective_max_limit, volname);
            loglevel = GF_LOG_WARNING;
            *op_errno = EG_HRDLMT;
            goto out;
        }

        snprintf(key, sizeof(key), "vol%" PRId64 "_volid", i);
        ret = dict_get_bin(dict, key, (void **)&snap_volid);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to fetch snap_volid");
            goto out;
        }

        /* snap volume uuid is used as lvm snapshot name.
           This will avoid restrictions on snapshot names
           provided by user */
        GLUSTERD_GET_UUID_NOHYPHEN(snap_volname, *snap_volid);

        ret = glusterd_snap_create_clone_common_prevalidate(
            rsp_dict, flags, snapname, err_str, snap_volname, i, volinfo,
            &loglevel, 0, op_errno);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PRE_VALIDATION_FAIL,
                   "Failed to pre validate");
            goto out;
        }
    }

    ret = dict_set_int64(rsp_dict, "volcount", volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set volcount");
        goto out;
    }

    ret = 0;

out:
    if (ret && err_str[0] != '\0') {
        gf_msg(this->name, loglevel, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
               err_str);
        *op_errstr = gf_strdup(err_str);
    }

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

glusterd_snap_t *
glusterd_new_snap_object()
{
    glusterd_snap_t *snap = NULL;

    snap = GF_CALLOC(1, sizeof(*snap), gf_gld_mt_snap_t);

    if (snap) {
        if (LOCK_INIT(&snap->lock)) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_LOCK_INIT_FAILED,
                   "Failed initiating"
                   " snap lock");
            GF_FREE(snap);
            return NULL;
        }

        CDS_INIT_LIST_HEAD(&snap->snap_list);
        CDS_INIT_LIST_HEAD(&snap->volumes);
        snap->snapname[0] = 0;
        snap->snap_status = GD_SNAP_STATUS_INIT;
    }

    return snap;
};

/* Function glusterd_list_add_snapvol adds the volinfo object (snapshot volume)
   to the snapshot object list and to the parent volume list */
int32_t
glusterd_list_add_snapvol(glusterd_volinfo_t *origin_vol,
                          glusterd_volinfo_t *snap_vol)
{
    int ret = -1;
    glusterd_snap_t *snap = NULL;

    GF_VALIDATE_OR_GOTO("glusterd", origin_vol, out);
    GF_VALIDATE_OR_GOTO("glusterd", snap_vol, out);

    snap = snap_vol->snapshot;
    GF_ASSERT(snap);

    cds_list_add_tail(&snap_vol->vol_list, &snap->volumes);
    LOCK(&origin_vol->lock);
    {
        glusterd_list_add_order(&snap_vol->snapvol_list,
                                &origin_vol->snap_volumes,
                                glusterd_compare_snap_vol_time);

        origin_vol->snap_count++;
    }
    UNLOCK(&origin_vol->lock);

    gf_msg_debug(THIS->name, 0, "Snapshot %s added to the list",
                 snap->snapname);
    ret = 0;
out:
    return ret;
}

glusterd_snap_t *
glusterd_find_snap_by_name(char *snapname)
{
    glusterd_snap_t *snap = NULL;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);
    GF_ASSERT(snapname);

    cds_list_for_each_entry(snap, &priv->snapshots, snap_list)
    {
        if (!strcmp(snap->snapname, snapname)) {
            gf_msg_debug(THIS->name, 0,
                         "Found "
                         "snap %s (%s)",
                         snap->snapname, uuid_utoa(snap->snap_id));
            goto out;
        }
    }
    snap = NULL;
out:
    return snap;
}

glusterd_snap_t *
glusterd_find_snap_by_id(uuid_t snap_id)
{
    glusterd_snap_t *snap = NULL;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    if (gf_uuid_is_null(snap_id))
        goto out;

    cds_list_for_each_entry(snap, &priv->snapshots, snap_list)
    {
        if (!gf_uuid_compare(snap->snap_id, snap_id)) {
            gf_msg_debug(THIS->name, 0,
                         "Found "
                         "snap %s (%s)",
                         snap->snapname, uuid_utoa(snap->snap_id));
            goto out;
        }
    }
    snap = NULL;
out:
    return snap;
}

int
glusterd_snapshot_umount(glusterd_volinfo_t *snap_vol,
                         glusterd_brickinfo_t *brickinfo, const char *mount_pt)
{
    int ret = -1;
    int retry_count = 0;
    glusterd_conf_t *priv = NULL;
    char pidfile[PATH_MAX] = {
        0,
    };
    pid_t pid = -1;
    gf_boolean_t unmount = _gf_true;
    char *mnt_pt = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(mount_pt);
    GF_ASSERT(brickinfo);
    GF_ASSERT(snap_vol);

    GLUSTERD_GET_BRICK_PIDFILE(pidfile, snap_vol, brickinfo, priv);
    if (gf_is_service_running(pidfile, &pid)) {
        (void)send_attach_req(this, brickinfo->rpc, brickinfo->path, NULL, NULL,
                              GLUSTERD_BRICK_TERMINATE, _gf_false);
        brickinfo->status = GF_BRICK_STOPPED;
    }

    /* Check if the brick is mounted and then try unmounting the brick */
    ret = glusterd_get_brick_root(brickinfo->path, &mnt_pt);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_BRICK_PATH_UNMOUNTED,
               "Getting the root "
               "of the brick for volume %s (snap %s) (device %s) "
               "failed.",
               snap_vol->volname, snap_vol->snapshot->snapname,
               brickinfo->device_path);
        /* The brick path is already unmounted. Remove the lv only *
         * Need not fail the operation */
        ret = 0;
        unmount = _gf_false;
    }

    if ((unmount == _gf_true) && (strcmp(mnt_pt, mount_pt))) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_BRICK_PATH_UNMOUNTED,
               "Volume is not mounted for brick %s:%s "
               "device (%s).",
               brickinfo->hostname, brickinfo->path, brickinfo->device_path);
        /* The brick path is already unmounted. Remove the lv only *
         * Need not fail the operation */
        unmount = _gf_false;
    }

    /* umount cannot be done when the brick process is still in the process
       of shutdown, so give three re-tries */
    while ((unmount == _gf_true) && (retry_count < 3)) {
        retry_count++;
        /*umount2 system call doesn't cleanup mtab entry after un-mount.
          So use external umount command*/
        ret = glusterd_umount(mount_pt, _gf_false);
        if (!ret)
            break;

        gf_msg_debug(this->name, errno,
                     "umount failed for path %s (brick: %s). Retry(%d)",
                     mount_pt, brickinfo->path, retry_count);

        /*
         * This used to be one second, but that wasn't long enough
         * to get past the spurious EPERM errors that prevent some
         * tests (especially bug-1162462.t) from passing reliably.
         *
         * TBD: figure out where that garbage is coming from
         */
        sleep(3);
    }
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNOUNT_FAILED,
               "umount failed for "
               "path %s (brick: %s): %s.",
               mount_pt, brickinfo->path, strerror(errno));
        /*
         * This is cheating, but necessary until we figure out how to
         * shut down a brick within a still-living brick daemon so that
         * random translators aren't keeping the mountpoint alive.
         *
         * TBD: figure out a real solution
         */
        ret = 0;
    }
    return ret;
}

int
glusterd_lvm_snapshot_remove(glusterd_volinfo_t *snap_vol,
                             glusterd_brickinfo_t *brickinfo,
                             const char *mount_pt, const char *snap_device)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    char msg[1024] = "";
    char *mnt_pt = NULL;
    int len;

    priv = this->private;
    GF_ASSERT(priv);

    if (!brickinfo) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "brickinfo NULL");
        goto out;
    }
    GF_ASSERT(snap_vol);
    GF_ASSERT(mount_pt);
    GF_ASSERT(snap_device);

    ret = glusterd_snapshot_umount(snap_vol, brickinfo, mount_pt);
    if (ret) {
        goto out;
    }

    runinit(&runner);
    len = snprintf(msg, sizeof(msg),
                   "remove snapshot of the brick %s:%s, "
                   "device: %s",
                   brickinfo->hostname, brickinfo->path, snap_device);
    if (len < 0) {
        strcpy(msg, "<error>");
    }
    runner_add_args(&runner, LVM_REMOVE, "-f", snap_device, NULL);
    runner_log(&runner, "", GF_LOG_DEBUG, msg);

    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "removing snapshot of the "
               "brick (%s:%s) of device %s failed",
               brickinfo->hostname, brickinfo->path, snap_device);
        goto out;
    }

out:
    if (mnt_pt)
        GF_FREE(mnt_pt);

    return ret;
}

int32_t
glusterd_snapshot_remove(dict_t *rsp_dict, glusterd_volinfo_t *snap_vol)
{
    int32_t brick_count = -1;
    int32_t ret = -1;
    int32_t err = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    xlator_t *this = THIS;
    char brick_dir[PATH_MAX] = "";
    char snap_path[PATH_MAX] = "";
    char *tmp = NULL;
    char *brick_mount_path = NULL;
    gf_boolean_t is_brick_dir_present = _gf_false;
    struct stat stbuf = {
        0,
    };

    GF_ASSERT(snap_vol);

    if ((snap_vol->is_snap_volume == _gf_false) &&
        (gf_uuid_is_null(snap_vol->restored_from_snap))) {
        gf_msg_debug(this->name, 0,
                     "Not a snap volume, or a restored snap volume.");
        ret = 0;
        goto out;
    }

    brick_count = -1;
    cds_list_for_each_entry(brickinfo, &snap_vol->bricks, brick_list)
    {
        brick_count++;
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
            gf_msg_debug(this->name, 0, "%s:%s belongs to a different node",
                         brickinfo->hostname, brickinfo->path);
            continue;
        }

        /* Fetch the brick mount path from the brickinfo->path */
        ret = glusterd_find_brick_mount_path(brickinfo->path,
                                             &brick_mount_path);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
                   "Failed to find brick_mount_path for %s", brickinfo->path);
            ret = 0;
            continue;
        }

        /* As deactivated snapshot have no active mount point we
         * check only for activated snapshot.
         */
        if (snap_vol->status == GLUSTERD_STATUS_STARTED) {
            ret = sys_lstat(brick_mount_path, &stbuf);
            if (ret) {
                gf_msg_debug(this->name, 0, "Brick %s:%s already deleted.",
                             brickinfo->hostname, brickinfo->path);
                ret = 0;
                continue;
            }
        }

        if (brickinfo->snap_status == -1) {
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SNAPSHOT_PENDING,
                   "snapshot was pending. snapshot supports "
                   "not present for brick %s:%s of the snap "
                   "%s.",
                   brickinfo->hostname, brickinfo->path,
                   snap_vol->snapshot->snapname);

            if (rsp_dict && (snap_vol->is_snap_volume == _gf_true)) {
                /* Adding missed delete to the dict */
                ret = glusterd_add_missed_snaps_to_dict(
                    rsp_dict, snap_vol, brickinfo, brick_count + 1,
                    GF_SNAP_OPTION_TYPE_DELETE);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_MISSED_SNAP_CREATE_FAIL,
                           "Failed to add missed snapshot "
                           "info for %s:%s in the "
                           "rsp_dict",
                           brickinfo->hostname, brickinfo->path);
                    goto out;
                }
            }

            continue;
        }

        /* Check if the brick has a LV associated with it */
        if (strlen(brickinfo->device_path) == 0) {
            gf_msg_debug(this->name, 0,
                         "Brick (%s:%s) does not have a device "
                         "associated with it. Removing the brick path",
                         brickinfo->hostname, brickinfo->path);
            goto remove_brick_path;
        }

        /* Verify if the device path exists or not */
        ret = sys_stat(brickinfo->device_path, &stbuf);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Device (%s) for brick (%s:%s) not present. "
                         "Removing the brick path",
                         brickinfo->device_path, brickinfo->hostname,
                         brickinfo->path);
            /* Making ret = 0 as absence of device path should *
             * not fail the remove operation */
            ret = 0;
            goto remove_brick_path;
        }

        if (!glusterd_snapshot_probe(brickinfo->device_path, brickinfo)) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
                   "Can not remove snapshot %s (%s) from "
                   "volume which does not support snapshots.",
                   brickinfo->path, brickinfo->device_path);
            ret = -1;
            goto out;
        }

        ret = brickinfo->snap->remove(snap_vol, brickinfo, brick_mount_path,
                                      brickinfo->device_path);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
                   "Failed to "
                   "remove the snapshot %s (%s)",
                   brickinfo->path, brickinfo->device_path);
            err = -1; /* We need to record this failure */
        }

    remove_brick_path:
        /* After removing the brick dir fetch the parent path
         * i.e /var/run/gluster/snaps/<snap-vol-id>/
         */
        if (is_brick_dir_present == _gf_false) {
            /* Need to fetch brick_dir to be removed from
             * brickinfo->path, as in a restored volume,
             * snap_vol won't have the non-hyphenated snap_vol_id
             */
            tmp = strstr(brick_mount_path, "brick");
            if (!tmp) {
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                       "Invalid brick %s", brickinfo->path);
                GF_FREE(brick_mount_path);
                brick_mount_path = NULL;
                continue;
            }

            strncpy(brick_dir, brick_mount_path,
                    (size_t)(tmp - brick_mount_path));

            /* Peers not hosting bricks will have _gf_false */
            is_brick_dir_present = _gf_true;
        }

        GF_FREE(brick_mount_path);
        brick_mount_path = NULL;
    }

    if (is_brick_dir_present == _gf_true) {
        ret = recursive_rmdir(brick_dir);
        if (ret) {
            if (errno == ENOTEMPTY) {
                /* Will occur when multiple glusterds
                 * are running in the same node
                 */
                gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_DIR_OP_FAILED,
                       "Failed to rmdir: %s, err: %s. "
                       "More than one glusterd running "
                       "on this node.",
                       brick_dir, strerror(errno));
                ret = 0;
                goto out;
            } else
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                       "Failed to rmdir: %s, err: %s", brick_dir,
                       strerror(errno));
            goto out;
        }

        /* After removing brick_dir, fetch and remove snap path
         * i.e. /var/run/gluster/snaps/<snap-name>.
         */
        if (!snap_vol->snapshot) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, GD_MSG_INVALID_ENTRY,
                   "snapshot not"
                   "present in snap_vol");
            ret = -1;
            goto out;
        }

        snprintf(snap_path, sizeof(snap_path), "%s/%s", snap_mount_dir,
                 snap_vol->snapshot->snapname);
        ret = recursive_rmdir(snap_path);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                   "Failed to remove "
                   "%s directory : error : %s",
                   snap_path, strerror(errno));
            goto out;
        }
    }

    ret = 0;
out:
    if (err) {
        ret = err;
    }
    GF_FREE(brick_mount_path);
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_snap_volume_remove(dict_t *rsp_dict, glusterd_volinfo_t *snap_vol,
                            gf_boolean_t remove_snapshot, gf_boolean_t force)
{
    int ret = -1;
    int save_ret = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_volinfo_t *origin_vol = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap_vol);

    if (!snap_vol) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, GD_MSG_INVALID_ENTRY,
               "snap_vol in NULL");
        ret = -1;
        goto out;
    }

    cds_list_for_each_entry(brickinfo, &snap_vol->bricks, brick_list)
    {
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
            continue;

        ret = glusterd_brick_stop(snap_vol, brickinfo, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_BRICK_STOP_FAIL,
                   "Failed to stop "
                   "brick for volume %s",
                   snap_vol->volname);
            save_ret = ret;

            /* Don't clean up the snap on error when
               force flag is disabled */
            if (!force)
                goto out;
        }
    }

    /* Only remove the backend snapshot when required */
    if (remove_snapshot) {
        ret = glusterd_snapshot_remove(rsp_dict, snap_vol);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                   "Failed to remove "
                   "snapshot volume %s",
                   snap_vol->volname);
            save_ret = ret;
            if (!force)
                goto out;
        }
    }

    ret = glusterd_store_delete_volume(snap_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_DELETE_FAIL,
               "Failed to remove volume %s "
               "from store",
               snap_vol->volname);
        save_ret = ret;
        if (!force)
            goto out;
    }

    if (!cds_list_empty(&snap_vol->snapvol_list)) {
        ret = glusterd_volinfo_find(snap_vol->parent_volname, &origin_vol);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
                   "Failed to get "
                   "parent volinfo %s  for volume  %s",
                   snap_vol->parent_volname, snap_vol->volname);
            save_ret = ret;
            if (!force)
                goto out;
        }
        origin_vol->snap_count--;
    }

    glusterd_volinfo_unref(snap_vol);

    if (save_ret)
        ret = save_ret;
out:
    gf_msg_trace(this->name, 0, "returning %d", ret);
    return ret;
}

int32_t
glusterd_snap_remove(dict_t *rsp_dict, glusterd_snap_t *snap,
                     gf_boolean_t remove_snapshot, gf_boolean_t force,
                     gf_boolean_t is_clone)
{
    int ret = -1;
    int save_ret = 0;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *tmp = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap);

    if (!snap) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, GD_MSG_INVALID_ENTRY,
               "snap is NULL");
        ret = -1;
        goto out;
    }

    cds_list_for_each_entry_safe(snap_vol, tmp, &snap->volumes, vol_list)
    {
        ret = glusterd_snap_volume_remove(rsp_dict, snap_vol, remove_snapshot,
                                          force);
        if (ret && !force) {
            /* Don't clean up the snap on error when
               force flag is disabled */
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                   "Failed to remove "
                   "volinfo %s for snap %s",
                   snap_vol->volname, snap->snapname);
            save_ret = ret;
            goto out;
        }
    }

    /* A clone does not persist snap info in /var/lib/glusterd/snaps/ *
     * and hence there is no snap info to be deleted from there       *
     */
    if (!is_clone) {
        ret = glusterd_store_delete_snap(snap);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                   "Failed to remove snap %s from store", snap->snapname);
            save_ret = ret;
            if (!force)
                goto out;
        }
    }

    ret = glusterd_snapobject_delete(snap);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "Failed to delete "
               "snap object %s",
               snap->snapname);

    if (save_ret)
        ret = save_ret;
out:
    gf_msg_trace(THIS->name, 0, "returning %d", ret);
    return ret;
}

static int
glusterd_snapshot_get_snapvol_detail(dict_t *dict, glusterd_volinfo_t *snap_vol,
                                     const char *keyprefix, const int detail)
{
    int ret = -1;
    int snap_limit = 0;
    char key[64] = ""; /* keyprefix is quite small, up to 32 byts */
    int keylen;
    char *value = NULL;
    glusterd_volinfo_t *origin_vol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    uint64_t opt_hard_max = GLUSTERD_SNAPS_MAX_HARD_LIMIT;

    conf = this->private;
    GF_ASSERT(conf);

    GF_ASSERT(dict);
    GF_ASSERT(snap_vol);
    GF_ASSERT(keyprefix);

    /* Volume Name */
    value = gf_strdup(snap_vol->volname);
    if (!value)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.volname", keyprefix);
    ret = dict_set_dynstrn(dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set "
               "volume name in dictionary: %s",
               key);
        goto out;
    }

    /* Volume ID */
    value = gf_strdup(uuid_utoa(snap_vol->volume_id));
    if (NULL == value) {
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.vol-id", keyprefix);
    ret = dict_set_dynstrn(dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_MEMORY,
               "Failed to set "
               "volume id in dictionary: %s",
               key);
        goto out;
    }
    value = NULL;

    /* volume status */
    keylen = snprintf(key, sizeof(key), "%s.vol-status", keyprefix);
    switch (snap_vol->status) {
        case GLUSTERD_STATUS_STARTED:
            ret = dict_set_nstrn(dict, key, keylen, "Started", SLEN("Started"));
            break;
        case GLUSTERD_STATUS_STOPPED:
            ret = dict_set_nstrn(dict, key, keylen, "Stopped", SLEN("Stopped"));
            break;
        case GD_SNAP_STATUS_NONE:
            ret = dict_set_nstrn(dict, key, keylen, "None", SLEN("None"));
            break;
        default:
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Invalid volume status");
            ret = -1;
            goto out;
    }
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set volume status"
               " in dictionary: %s",
               key);
        goto out;
    }

    ret = glusterd_volinfo_find(snap_vol->parent_volname, &origin_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
               "failed to get the parent "
               "volinfo for the volume %s",
               snap_vol->volname);
        goto out;
    }

    /* "snap-max-hard-limit" might not be set by user explicitly,
     * in that case it's better to consider the default value.
     * Hence not erroring out if Key is not found.
     */
    ret = dict_get_uint64(conf->opts, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                          &opt_hard_max);
    if (ret) {
        ret = 0;
        gf_msg_debug(this->name, 0,
                     "%s is not present in "
                     "opts dictionary",
                     GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
    }

    if (opt_hard_max < origin_vol->snap_max_hard_limit) {
        snap_limit = opt_hard_max;
        gf_msg_debug(this->name, 0,
                     "system snap-max-hard-limit is"
                     " lesser than volume snap-max-hard-limit, "
                     "snap-max-hard-limit value is set to %d",
                     snap_limit);
    } else {
        snap_limit = origin_vol->snap_max_hard_limit;
        gf_msg_debug(this->name, 0,
                     "volume snap-max-hard-limit is"
                     " lesser than system snap-max-hard-limit, "
                     "snap-max-hard-limit value is set to %d",
                     snap_limit);
    }

    keylen = snprintf(key, sizeof(key), "%s.snaps-available", keyprefix);
    if (snap_limit > origin_vol->snap_count)
        ret = dict_set_int32n(dict, key, keylen,
                              snap_limit - origin_vol->snap_count);
    else
        ret = dict_set_int32(dict, key, 0);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set available snaps");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.snapcount", keyprefix);
    ret = dict_set_int32n(dict, key, keylen, origin_vol->snap_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save snapcount");
        goto out;
    }

    if (!detail)
        goto out;

    /* Parent volume name */
    value = gf_strdup(snap_vol->parent_volname);
    if (!value)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.origin-volname", keyprefix);
    ret = dict_set_dynstrn(dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set parent "
               "volume name in dictionary: %s",
               key);
        goto out;
    }
    value = NULL;

    ret = 0;
out:
    if (value)
        GF_FREE(value);

    return ret;
}

static int
glusterd_snapshot_get_snap_detail(dict_t *dict, glusterd_snap_t *snap,
                                  const char *keyprefix,
                                  glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    int volcount = 0;
    char key[32] = ""; /* keyprefix is quite small, up to 16 bytes */
    int keylen;
    char timestr[GF_TIMESTR_SIZE] = "";
    char *value = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *tmp_vol = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(snap);
    GF_ASSERT(keyprefix);

    /* Snap Name */
    value = gf_strdup(snap->snapname);
    if (!value)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.snapname", keyprefix);
    ret = dict_set_dynstrn(dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set "
               "snap name in dictionary");
        goto out;
    }

    /* Snap ID */
    value = gf_strdup(uuid_utoa(snap->snap_id));
    if (NULL == value) {
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.snap-id", keyprefix);
    ret = dict_set_dynstrn(dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set "
               "snap id in dictionary");
        goto out;
    }
    value = NULL;

    gf_time_fmt(timestr, sizeof timestr, snap->time_stamp, gf_timefmt_FT);
    value = gf_strdup(timestr);

    if (NULL == value) {
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.snap-time", keyprefix);
    ret = dict_set_dynstrn(dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set "
               "snap time stamp in dictionary");
        goto out;
    }
    value = NULL;

    /* If snap description is provided then add that into dictionary */
    if (NULL != snap->description) {
        value = gf_strdup(snap->description);
        if (NULL == value) {
            ret = -1;
            goto out;
        }

        keylen = snprintf(key, sizeof(key), "%s.snap-desc", keyprefix);
        ret = dict_set_dynstrn(dict, key, keylen, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set "
                   "snap description in dictionary");
            goto out;
        }
        value = NULL;
    }

    keylen = snprintf(key, sizeof(key), "%s.snap-status", keyprefix);
    switch (snap->snap_status) {
        case GD_SNAP_STATUS_INIT:
            ret = dict_set_nstrn(dict, key, keylen, "Init", SLEN("Init"));
            break;
        case GD_SNAP_STATUS_IN_USE:
            ret = dict_set_nstrn(dict, key, keylen, "In-use", SLEN("In-use"));
            break;
        case GD_SNAP_STATUS_DECOMMISSION:
            ret = dict_set_nstrn(dict, key, keylen, "Decommisioned",
                                 SLEN("Decommisioned"));
            break;
        case GD_SNAP_STATUS_RESTORED:
            ret = dict_set_nstrn(dict, key, keylen, "Restored",
                                 SLEN("Restored"));
            break;
        case GD_SNAP_STATUS_NONE:
            ret = dict_set_nstrn(dict, key, keylen, "None", SLEN("None"));
            break;
        default:
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Invalid snap status");
            ret = -1;
            goto out;
    }
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap status "
               "in dictionary");
        goto out;
    }

    if (volinfo) {
        volcount = 1;
        snprintf(key, sizeof(key), "%s.vol%d", keyprefix, volcount);
        ret = glusterd_snapshot_get_snapvol_detail(dict, volinfo, key, 0);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_DICT_GET_FAILED,
                   "Failed to "
                   "get volume detail %s for snap %s",
                   snap_vol->volname, snap->snapname);
            goto out;
        }
        goto done;
    }

    cds_list_for_each_entry_safe(snap_vol, tmp_vol, &snap->volumes, vol_list)
    {
        volcount++;
        snprintf(key, sizeof(key), "%s.vol%d", keyprefix, volcount);
        ret = glusterd_snapshot_get_snapvol_detail(dict, snap_vol, key, 1);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to "
                   "get volume detail %s for snap %s",
                   snap_vol->volname, snap->snapname);
            goto out;
        }
    }

done:
    keylen = snprintf(key, sizeof(key), "%s.vol-count", keyprefix);
    ret = dict_set_int32n(dict, key, keylen, volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s", key);
        goto out;
    }

    ret = 0;
out:
    if (value)
        GF_FREE(value);

    return ret;
}

static int
glusterd_snapshot_get_all_snap_info(dict_t *dict)
{
    int ret = -1;
    int snapcount = 0;
    char key[16] = "";
    glusterd_snap_t *snap = NULL;
    glusterd_snap_t *tmp_snap = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    GF_ASSERT(priv);

    /* General parameter validation */
    GF_ASSERT(dict);

    cds_list_for_each_entry_safe(snap, tmp_snap, &priv->snapshots, snap_list)
    {
        snapcount++;
        snprintf(key, sizeof(key), "snap%d", snapcount);
        ret = glusterd_snapshot_get_snap_detail(dict, snap, key, NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "snapdetail for snap %s",
                   snap->snapname);
            goto out;
        }
    }

    ret = dict_set_int32n(dict, "snapcount", SLEN("snapcount"), snapcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snapcount");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_snapshot_get_info_by_volume(dict_t *dict, char *volname, char *err_str,
                                     size_t len)
{
    int ret = -1;
    int snapcount = 0;
    int snap_limit = 0;
    char *value = NULL;
    char key[16] = "";
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *tmp_vol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    uint64_t opt_hard_max = GLUSTERD_SNAPS_MAX_HARD_LIMIT;

    conf = this->private;
    GF_ASSERT(conf);

    GF_ASSERT(dict);
    GF_ASSERT(volname);

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(err_str, len, "Volume (%s) does not exist", volname);
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND, "%s",
               err_str);
        goto out;
    }

    /* "snap-max-hard-limit" might not be set by user explicitly,
     * in that case it's better to consider the default value.
     * Hence not erroring out if Key is not found.
     */
    ret = dict_get_uint64(conf->opts, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                          &opt_hard_max);
    if (ret) {
        ret = 0;
        gf_msg_debug(this->name, 0,
                     "%s is not present in "
                     "opts dictionary",
                     GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
    }

    if (opt_hard_max < volinfo->snap_max_hard_limit) {
        snap_limit = opt_hard_max;
        gf_msg_debug(this->name, 0,
                     "system snap-max-hard-limit is"
                     " lesser than volume snap-max-hard-limit, "
                     "snap-max-hard-limit value is set to %d",
                     snap_limit);
    } else {
        snap_limit = volinfo->snap_max_hard_limit;
        gf_msg_debug(this->name, 0,
                     "volume snap-max-hard-limit is"
                     " lesser than system snap-max-hard-limit, "
                     "snap-max-hard-limit value is set to %d",
                     snap_limit);
    }

    if (snap_limit > volinfo->snap_count)
        ret = dict_set_int32n(dict, "snaps-available", SLEN("snaps-available"),
                              snap_limit - volinfo->snap_count);
    else
        ret = dict_set_int32n(dict, "snaps-available", SLEN("snaps-available"),
                              0);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set available snaps");
        goto out;
    }

    /* Origin volume name */
    value = gf_strdup(volinfo->volname);
    if (!value)
        goto out;

    ret = dict_set_dynstrn(dict, "origin-volname", SLEN("origin-volname"),
                           value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set parent "
               "volume name in dictionary: %s",
               value);
        goto out;
    }
    value = NULL;

    cds_list_for_each_entry_safe(snap_vol, tmp_vol, &volinfo->snap_volumes,
                                 snapvol_list)
    {
        snapcount++;
        snprintf(key, sizeof(key), "snap%d", snapcount);
        ret = glusterd_snapshot_get_snap_detail(dict, snap_vol->snapshot, key,
                                                snap_vol);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "snapdetail for snap %s",
                   snap_vol->snapshot->snapname);
            goto out;
        }
    }
    ret = dict_set_int32n(dict, "snapcount", SLEN("snapcount"), snapcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snapcount");
        goto out;
    }

    ret = 0;
out:
    if (value)
        GF_FREE(value);

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
glusterd_handle_snapshot_info(rpcsvc_request_t *req, glusterd_op_t op,
                              dict_t *dict, char *err_str, size_t len)
{
    int ret = -1;
    int8_t snap_driven = 1;
    char *volname = NULL;
    char *snapname = NULL;
    glusterd_snap_t *snap = NULL;
    xlator_t *this = THIS;
    int32_t cmd = GF_SNAP_INFO_TYPE_ALL;

    GF_VALIDATE_OR_GOTO(this->name, req, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);

    ret = dict_get_int32n(dict, "sub-cmd", SLEN("sub-cmd"), &cmd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get type "
               "of snapshot info");
        goto out;
    }

    switch (cmd) {
        case GF_SNAP_INFO_TYPE_ALL: {
            ret = glusterd_snapshot_get_all_snap_info(dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get info of all snaps");
                goto out;
            }
            break;
        }

        case GF_SNAP_INFO_TYPE_SNAP: {
            ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get snap name");
                goto out;
            }

            ret = dict_set_int32n(dict, "snapcount", SLEN("snapcount"), 1);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set snapcount");
                goto out;
            }

            snap = glusterd_find_snap_by_name(snapname);
            if (!snap) {
                snprintf(err_str, len, "Snapshot (%s) does not exist",
                         snapname);
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
                       "%s", err_str);
                ret = -1;
                goto out;
            }
            ret = glusterd_snapshot_get_snap_detail(dict, snap, "snap1", NULL);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND,
                       "Failed to get snap detail of snap "
                       "%s",
                       snap->snapname);
                goto out;
            }
            break;
        }

        case GF_SNAP_INFO_TYPE_VOL: {
            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                       "Failed to get volname");
                goto out;
            }
            ret = glusterd_snapshot_get_info_by_volume(dict, volname, err_str,
                                                       len);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
                       "Failed to get volume info of volume "
                       "%s",
                       volname);
                goto out;
            }
            snap_driven = 0;
            break;
        }
    }

    ret = dict_set_int8(dict, "snap-driven", snap_driven);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap-driven");
        goto out;
    }

    /* If everything is successful then send the response back to cli.
     * In case of failure the caller of this function will take care
       of the response */
    ret = glusterd_op_send_cli_response(op, 0, 0, req, dict, err_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_CLI_RESP,
               "Failed to send cli "
               "response");
        goto out;
    }

    ret = 0;

out:
    return ret;
}

/* This function sets all the snapshot names in the dictionary */
int
glusterd_snapshot_get_all_snapnames(dict_t *dict)
{
    int ret = -1;
    int snapcount = 0;
    char *snapname = NULL;
    char key[64] = "";
    int keylen;
    glusterd_snap_t *snap = NULL;
    glusterd_snap_t *tmp_snap = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(dict);

    cds_list_for_each_entry_safe(snap, tmp_snap, &priv->snapshots, snap_list)
    {
        snapcount++;
        snapname = gf_strdup(snap->snapname);
        if (!snapname) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "strdup failed");
            ret = -1;
            goto out;
        }
        keylen = snprintf(key, sizeof(key), "snapname%d", snapcount);
        ret = dict_set_dynstrn(dict, key, keylen, snapname);
        if (ret) {
            GF_FREE(snapname);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }
    }

    ret = dict_set_int32n(dict, "snapcount", SLEN("snapcount"), snapcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snapcount");
        goto out;
    }

    ret = 0;
out:

    return ret;
}

/* This function sets all the snapshot names
   under a given volume in the dictionary */
int
glusterd_snapshot_get_vol_snapnames(dict_t *dict, glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    int snapcount = 0;
    char *snapname = NULL;
    char key[32] = "";
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *tmp_vol = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(volinfo);

    cds_list_for_each_entry_safe(snap_vol, tmp_vol, &volinfo->snap_volumes,
                                 snapvol_list)
    {
        snapcount++;
        snprintf(key, sizeof(key), "snapname%d", snapcount);

        ret = dict_set_dynstr_with_alloc(dict, key,
                                         snap_vol->snapshot->snapname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to "
                   "set %s",
                   key);
            GF_FREE(snapname);
            goto out;
        }
    }

    ret = dict_set_int32n(dict, "snapcount", SLEN("snapcount"), snapcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snapcount");
        goto out;
    }

    ret = 0;
out:

    return ret;
}

int
glusterd_handle_snapshot_list(rpcsvc_request_t *req, glusterd_op_t op,
                              dict_t *dict, char *err_str, size_t len,
                              uint32_t *op_errno)
{
    int ret = -1;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;

    GF_VALIDATE_OR_GOTO(this->name, req, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    /* Ignore error for getting volname as it is optional */
    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    if (NULL == volname) {
        ret = glusterd_snapshot_get_all_snapnames(dict);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_LIST_GET_FAIL,
                   "Failed to get snapshot list");
            goto out;
        }
    } else {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(err_str, len, "Volume (%s) does not exist", volname);
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND, "%s",
                   err_str);
            *op_errno = EG_NOVOL;
            goto out;
        }

        ret = glusterd_snapshot_get_vol_snapnames(dict, volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_LIST_GET_FAIL,
                   "Failed to get snapshot list for volume %s", volname);
            goto out;
        }
    }

    /* If everything is successful then send the response back to cli.
    In case of failure the caller of this function will take of response.*/
    ret = glusterd_op_send_cli_response(op, 0, 0, req, dict, err_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_CLI_RESP,
               "Failed to send cli "
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
glusterd_handle_snapshot_create(rpcsvc_request_t *req, glusterd_op_t op,
                                dict_t *dict, char *err_str, size_t len)
{
    int ret = -1;
    char *volname = NULL;
    char *snapname = NULL;
    int64_t volcount = 0;
    xlator_t *this = THIS;
    char key[64] = "";
    int keylen;
    char *username = NULL;
    char *password = NULL;
    uuid_t *uuid_ptr = NULL;
    uuid_t tmp_uuid = {0};
    int i = 0;
    int timestamp = 0;
    char snap_volname[GD_VOLUME_NAME_MAX] = "";
    char new_snapname[GLUSTERD_MAX_SNAP_NAME] = "";
    char gmt_snaptime[GLUSTERD_MAX_SNAP_NAME] = "";
    time_t snap_time;
    GF_ASSERT(req);
    GF_ASSERT(dict);
    GF_ASSERT(err_str);

    ret = dict_get_int64(dict, "volcount", &volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to "
               "get the volume count");
        goto out;
    }
    if (volcount <= 0) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Invalid volume count %" PRId64 " supplied", volcount);
        ret = -1;
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get the snapname");
        goto out;
    }

    timestamp = dict_get_str_boolean(dict, "no-timestamp", _gf_false);
    if (timestamp == -1) {
        gf_log(this->name, GF_LOG_ERROR,
               "Failed to get "
               "no-timestamp flag ");
        goto out;
    }

    snap_time = gf_time();
    ret = dict_set_int64(dict, "snap-time", (int64_t)snap_time);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set snap-time");
        goto out;
    }

    if (!timestamp) {
        strftime(gmt_snaptime, sizeof(gmt_snaptime), "_GMT-%Y.%m.%d-%H.%M.%S",
                 gmtime(&snap_time));
        snprintf(new_snapname, sizeof(new_snapname), "%s%s", snapname,
                 gmt_snaptime);
        ret = dict_set_dynstr_with_alloc(dict, "snapname", new_snapname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to update "
                   "snap-name");
            goto out;
        }
        snapname = new_snapname;
    }

    if (strlen(snapname) >= GLUSTERD_MAX_SNAP_NAME) {
        snprintf(err_str, len,
                 "snapname cannot exceed 255 "
                 "characters");
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               err_str);
        ret = -1;
        goto out;
    }

    uuid_ptr = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!uuid_ptr) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out Of Memory");
        ret = -1;
        goto out;
    }

    gf_uuid_generate(*uuid_ptr);
    ret = dict_set_bin(dict, "snap-id", uuid_ptr, sizeof(uuid_t));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set snap-id");
        GF_FREE(uuid_ptr);
        goto out;
    }
    uuid_ptr = NULL;

    for (i = 1; i <= volcount; i++) {
        keylen = snprintf(key, sizeof(key), "volname%d", i);
        ret = dict_get_strn(dict, key, keylen, &volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get volume name");
            goto out;
        }

        /* generate internal username and password  for the snap*/
        gf_uuid_generate(tmp_uuid);
        username = gf_strdup(uuid_utoa(tmp_uuid));
        keylen = snprintf(key, sizeof(key), "volume%d_username", i);
        ret = dict_set_dynstrn(dict, key, keylen, username);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set snap "
                   "username for volume %s",
                   volname);
            GF_FREE(username);
            goto out;
        }

        gf_uuid_generate(tmp_uuid);
        password = gf_strdup(uuid_utoa(tmp_uuid));
        keylen = snprintf(key, sizeof(key), "volume%d_password", i);
        ret = dict_set_dynstrn(dict, key, keylen, password);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set snap "
                   "password for volume %s",
                   volname);
            GF_FREE(password);
            goto out;
        }

        uuid_ptr = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
        if (!uuid_ptr) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Out Of Memory");
            ret = -1;
            goto out;
        }

        snprintf(key, sizeof(key), "vol%d_volid", i);
        gf_uuid_generate(*uuid_ptr);
        ret = dict_set_bin(dict, key, uuid_ptr, sizeof(uuid_t));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set snap_volid");
            GF_FREE(uuid_ptr);
            goto out;
        }
        GLUSTERD_GET_UUID_NOHYPHEN(snap_volname, *uuid_ptr);
        snprintf(key, sizeof(key), "snap-volname%d", i);
        ret = dict_set_dynstr_with_alloc(dict, key, snap_volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set snap volname");
            GF_FREE(uuid_ptr);
            goto out;
        }
    }

    ret = glusterd_mgmt_v3_initiate_snap_phases(req, op, dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_INIT_FAIL,
               "Failed to initiate snap "
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
glusterd_handle_snapshot_status(rpcsvc_request_t *req, glusterd_op_t op,
                                dict_t *dict, char *err_str, size_t len)
{
    int ret = -1;

    GF_ASSERT(req);
    GF_ASSERT(dict);
    GF_ASSERT(err_str);

    ret = glusterd_mgmt_v3_initiate_snap_phases(req, op, dict);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_INIT_FAIL,
               "Failed to initiate "
               "snap phases");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

/* This is a snapshot clone handler function. This function will be
 * executed in the originator node. This function is responsible for
 * calling mgmt_v3 framework to do the actual snap clone on all the bricks
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
glusterd_handle_snapshot_clone(rpcsvc_request_t *req, glusterd_op_t op,
                               dict_t *dict, char *err_str, size_t len)
{
    int ret = -1;
    char *clonename = NULL;
    char *snapname = NULL;
    xlator_t *this = THIS;
    char key[64] = "";
    int keylen;
    char *username = NULL;
    char *password = NULL;
    char *volname = NULL;
    uuid_t *uuid_ptr = NULL;
    uuid_t tmp_uuid = {0};
    int i = 0;
    char snap_volname[GD_VOLUME_NAME_MAX] = "";

    GF_ASSERT(req);
    GF_ASSERT(dict);
    GF_ASSERT(err_str);

    ret = dict_get_strn(dict, "clonename", SLEN("clonename"), &clonename);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to "
               "get the clone name");
        goto out;
    }
    /*We need to take a volume lock on clone name*/
    volname = gf_strdup(clonename);
    keylen = snprintf(key, sizeof(key), "volname1");
    ret = dict_set_dynstrn(dict, key, keylen, volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set clone "
               "name for volume locking");
        GF_FREE(volname);
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get the snapname");
        goto out;
    }

    uuid_ptr = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!uuid_ptr) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out Of Memory");
        ret = -1;
        goto out;
    }

    gf_uuid_generate(*uuid_ptr);
    ret = dict_set_bin(dict, "clone-id", uuid_ptr, sizeof(uuid_t));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set clone-id");
        GF_FREE(uuid_ptr);
        goto out;
    }
    uuid_ptr = NULL;

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get snapname name");
        goto out;
    }

    gf_uuid_generate(tmp_uuid);
    username = gf_strdup(uuid_utoa(tmp_uuid));
    keylen = snprintf(key, sizeof(key), "volume1_username");
    ret = dict_set_dynstrn(dict, key, keylen, username);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set clone "
               "username for volume %s",
               clonename);
        GF_FREE(username);
        goto out;
    }

    gf_uuid_generate(tmp_uuid);
    password = gf_strdup(uuid_utoa(tmp_uuid));
    keylen = snprintf(key, sizeof(key), "volume1_password");
    ret = dict_set_dynstrn(dict, key, keylen, password);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set clone "
               "password for volume %s",
               clonename);
        GF_FREE(password);
        goto out;
    }

    uuid_ptr = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!uuid_ptr) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out Of Memory");
        ret = -1;
        goto out;
    }

    snprintf(key, sizeof(key), "vol1_volid");
    gf_uuid_generate(*uuid_ptr);
    ret = dict_set_bin(dict, key, uuid_ptr, sizeof(uuid_t));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set clone_volid");
        GF_FREE(uuid_ptr);
        goto out;
    }
    snprintf(key, sizeof(key), "clone-volname%d", i);
    ret = dict_set_dynstr_with_alloc(dict, key, snap_volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set snap volname");
        GF_FREE(uuid_ptr);
        goto out;
    }

    ret = glusterd_mgmt_v3_initiate_snap_phases(req, op, dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_INIT_FAIL,
               "Failed to initiate "
               "snap phases");
    }

out:
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
glusterd_handle_snapshot_restore(rpcsvc_request_t *req, glusterd_op_t op,
                                 dict_t *dict, char *err_str,
                                 uint32_t *op_errno, size_t len)
{
    int ret = -1;
    char *snapname = NULL;
    char *buf = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *snap_volinfo = NULL;
    int32_t i = 0;
    char key[64] = "";
    int keylen;

    conf = this->private;

    GF_ASSERT(conf);
    GF_ASSERT(req);
    GF_ASSERT(dict);
    GF_ASSERT(err_str);

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to "
               "get snapname");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        snprintf(err_str, len, "Snapshot (%s) does not exist", snapname);
        *op_errno = EG_NOSNAP;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND, "%s",
               err_str);
        ret = -1;
        goto out;
    }

    list_for_each_entry(snap_volinfo, &snap->volumes, vol_list)
    {
        i++;
        keylen = snprintf(key, sizeof(key), "volname%d", i);
        buf = gf_strdup(snap_volinfo->parent_volname);
        if (!buf) {
            ret = -1;
            goto out;
        }
        ret = dict_set_dynstrn(dict, key, keylen, buf);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not set "
                   "parent volume name %s in the dict",
                   snap_volinfo->parent_volname);
            GF_FREE(buf);
            goto out;
        }
        buf = NULL;
    }

    ret = dict_set_int32n(dict, "volcount", SLEN("volcount"), i);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save volume count");
        goto out;
    }

    ret = glusterd_mgmt_v3_initiate_snap_phases(req, op, dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_INIT_FAIL,
               "Failed to initiate snap phases");
        goto out;
    }

    ret = 0;

out:
    return ret;
}

glusterd_snap_t *
glusterd_create_snap_object(dict_t *dict, dict_t *rsp_dict)
{
    char *snapname = NULL;
    uuid_t *snap_id = NULL;
    char *description = NULL;
    glusterd_snap_t *snap = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int ret = -1;
    int64_t time_stamp = 0;

    priv = this->private;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    /* Fetch snapname, description, id and time from dict */
    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch snapname");
        goto out;
    }

    /* Ignore ret value for description*/
    ret = dict_get_strn(dict, "description", SLEN("description"), &description);

    ret = dict_get_bin(dict, "snap-id", (void **)&snap_id);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch snap_id");
        goto out;
    }

    ret = dict_get_int64(dict, "snap-time", &time_stamp);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch snap-time");
        goto out;
    }
    if (time_stamp <= 0) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Invalid time-stamp: %" PRId64, time_stamp);
        goto out;
    }

    cds_list_for_each_entry(snap, &priv->snapshots, snap_list)
    {
        if (!strcmp(snap->snapname, snapname) ||
            !gf_uuid_compare(snap->snap_id, *snap_id)) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
                   "Found duplicate snap %s (%s)", snap->snapname,
                   uuid_utoa(snap->snap_id));
            ret = -1;
            break;
        }
    }
    if (ret) {
        snap = NULL;
        goto out;
    }

    snap = glusterd_new_snap_object();
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Could not create "
               "the snap object for snap %s",
               snapname);
        goto out;
    }

    gf_strncpy(snap->snapname, snapname, sizeof(snap->snapname));
    gf_uuid_copy(snap->snap_id, *snap_id);
    snap->time_stamp = (time_t)time_stamp;
    /* Set the status as GD_SNAP_STATUS_INIT and once the backend snapshot
       is taken and snap is really ready to use, set the status to
       GD_SNAP_STATUS_IN_USE. This helps in identifying the incomplete
       snapshots and cleaning them up.
    */
    snap->snap_status = GD_SNAP_STATUS_INIT;
    if (description) {
        snap->description = gf_strdup(description);
        if (snap->description == NULL) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
                   "Saving the Snapshot Description Failed");
            ret = -1;
            goto out;
        }
    }

    ret = glusterd_store_snap(snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Could not store snap"
               "object %s",
               snap->snapname);
        goto out;
    }

    glusterd_list_add_order(&snap->snap_list, &priv->snapshots,
                            glusterd_compare_snap_time);

    gf_msg_trace(this->name, 0, "Snapshot %s added to the list",
                 snap->snapname);

    ret = 0;

out:
    if (ret) {
        if (snap)
            glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_true, _gf_false);
        snap = NULL;
    }

    return snap;
}

/* Added missed_snap_entry to rsp_dict */
int32_t
glusterd_add_missed_snaps_to_dict(dict_t *rsp_dict,
                                  glusterd_volinfo_t *snap_vol,
                                  glusterd_brickinfo_t *brickinfo,
                                  int32_t brick_number, int32_t op)
{
    char *snap_uuid = NULL;
    char missed_snap_entry[PATH_MAX] = "";
    char name_buf[PATH_MAX] = "";
    int32_t missed_snap_count = -1;
    int32_t ret = -1;
    xlator_t *this = THIS;
    int32_t len = 0;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap_vol);
    GF_ASSERT(brickinfo);

    snap_uuid = gf_strdup(uuid_utoa(snap_vol->snapshot->snap_id));
    if (!snap_uuid) {
        ret = -1;
        goto out;
    }

    len = snprintf(missed_snap_entry, sizeof(missed_snap_entry),
                   "%s:%s=%s:%d:%s:%d:%d", uuid_utoa(brickinfo->uuid),
                   snap_uuid, snap_vol->volname, brick_number, brickinfo->path,
                   op, GD_MISSED_SNAP_PENDING);
    if ((len < 0) || (len >= sizeof(missed_snap_entry))) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    /* Fetch the missed_snap_count from the dict */
    ret = dict_get_int32n(rsp_dict, "missed_snap_count",
                          SLEN("missed_snap_count"), &missed_snap_count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=missed_snap_count", NULL);
        /* Initialize the missed_snap_count for the first time */
        missed_snap_count = 0;
    }

    /* Setting the missed_snap_entry in the rsp_dict */
    snprintf(name_buf, sizeof(name_buf), "missed_snaps_%d", missed_snap_count);
    ret = dict_set_dynstr_with_alloc(rsp_dict, name_buf, missed_snap_entry);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set missed_snap_entry (%s) "
               "in the rsp_dict.",
               missed_snap_entry);
        goto out;
    }
    missed_snap_count++;

    /* Setting the new missed_snap_count in the dict */
    ret = dict_set_int32n(rsp_dict, "missed_snap_count",
                          SLEN("missed_snap_count"), missed_snap_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set missed_snap_count for %s "
               "in the rsp_dict.",
               missed_snap_entry);
        goto out;
    }

out:
    if (snap_uuid)
        GF_FREE(snap_uuid);

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* This function actually calls the command (or the API) for taking the
   snapshot of the backend brick filesystem. If this is successful,
   then call the glusterd_snap_create function to create the snap object
   for glusterd
*/
int32_t
glusterd_lvm_snapshot_create(glusterd_brickinfo_t *brickinfo,
                             char *origin_brick_path)
{
    char msg[NAME_MAX] = "";
    char buf[PATH_MAX] = "";
    char *ptr = NULL;
    char *origin_device = NULL;
    int ret = -1;
    gf_boolean_t match = _gf_false;
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(brickinfo);
    GF_ASSERT(origin_brick_path);

    origin_device = glusterd_get_brick_mount_device(origin_brick_path);
    if (!origin_device) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
               "getting device name for "
               "the brick %s failed",
               origin_brick_path);
        goto out;
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

    /* Taking the actual snapshot */
    runinit(&runner);
    snprintf(msg, sizeof(msg), "taking snapshot of the brick %s",
             origin_brick_path);
    if (match == _gf_true)
        runner_add_args(&runner, LVM_CREATE, "-s", origin_device,
                        "--setactivationskip", "n", "--name",
                        brickinfo->device_path, NULL);
    else
        runner_add_args(&runner, LVM_CREATE, "-s", origin_device, "--name",
                        brickinfo->device_path, NULL);
    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "taking snapshot of the "
               "brick (%s) of device %s failed",
               origin_brick_path, origin_device);
    }

    /* After the snapshot both the origin brick (LVM brick) and
     * the snapshot brick will have the same file-system label. This
     * will cause lot of problems at mount time. Therefore we must
     * generate a new label for the snapshot brick
     */
    ret = glusterd_update_fs_label(brickinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FS_LABEL_UPDATE_FAIL,
               "Failed to update "
               "file-system label for %s brick",
               brickinfo->path);
        /* Failing to update label should not cause snapshot failure.
         * Currently label is updated only for XFS and ext2/ext3/ext4
         * file-system.
         */
    }

out:
    if (origin_device)
        GF_FREE(origin_device);

    return ret;
}

int32_t
glusterd_snap_brick_create(glusterd_volinfo_t *snap_volinfo,
                           glusterd_brickinfo_t *brickinfo, int32_t brick_count,
                           int32_t clone)
{
    int32_t ret = -1;
    xlator_t *this = THIS;
    char snap_brick_mount_path[PATH_MAX] = "";
    char clone_uuid[64] = "";
    struct stat statbuf = {
        0,
    };
    int32_t len = 0;

    GF_ASSERT(snap_volinfo);
    GF_ASSERT(brickinfo);

    if (clone) {
        GLUSTERD_GET_UUID_NOHYPHEN(clone_uuid, snap_volinfo->volume_id);
        len = snprintf(snap_brick_mount_path, sizeof(snap_brick_mount_path),
                       "%s/%s/brick%d", snap_mount_dir, clone_uuid,
                       brick_count + 1);
    } else {
        len = snprintf(snap_brick_mount_path, sizeof(snap_brick_mount_path),
                       "%s/%s/brick%d", snap_mount_dir, snap_volinfo->volname,
                       brick_count + 1);
    }
    if ((len < 0) || (len >= sizeof(snap_brick_mount_path))) {
        goto out;
    }

    ret = mkdir_p(snap_brick_mount_path, 0755, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "creating the brick directory"
               " %s for the snapshot %s(device: %s) failed",
               snap_brick_mount_path, snap_volinfo->volname,
               brickinfo->device_path);
        goto out;
    }
    /* mount the snap logical device on the directory inside
       /run/gluster/snaps/<snapname>/@snap_brick_mount_path
       Way to mount the snap brick via mount api is this.
       ret = mount (device, snap_brick_mount_path, entry->mnt_type,
                    MS_MGC_VAL, "nouuid");
       But for now, mounting using runner apis.
    */
    ret = brickinfo->snap->mount(brickinfo, snap_brick_mount_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_LVM_MOUNT_FAILED,
               "Failed to mount snapshot.");
        goto out;
    }

    ret = sys_stat(brickinfo->path, &statbuf);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "stat of the brick %s"
               "(brick mount: %s) failed (%s)",
               brickinfo->path, snap_brick_mount_path, strerror(errno));
        goto out;
    }
    ret = sys_lsetxattr(brickinfo->path, GF_XATTR_VOL_ID_KEY,
                        snap_volinfo->volume_id, 16, XATTR_REPLACE);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_SET_XATTR_FAIL,
               "Failed to set "
               "extended attribute %s on %s. Reason: "
               "%s, snap: %s",
               GF_XATTR_VOL_ID_KEY, brickinfo->path, strerror(errno),
               snap_volinfo->volname);
        goto out;
    }

out:
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_UMOUNTING_SNAP_BRICK,
               "unmounting the snap brick"
               " mount %s",
               snap_brick_mount_path);
        /*umount2 system call doesn't cleanup mtab entry after un-mount.
          So use external umount command*/
        glusterd_umount(snap_brick_mount_path, _gf_false);
    }

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_add_brick_to_snap_volume(dict_t *dict, dict_t *rsp_dict,
                                  glusterd_volinfo_t *snap_vol,
                                  glusterd_brickinfo_t *original_brickinfo,
                                  int64_t volcount, int32_t brick_count,
                                  int clone)
{
    char key[64] = "";
    int keylen;
    char *value = NULL;
    char *snap_brick_dir = NULL;
    char snap_brick_path[PATH_MAX] = "";
    char clone_uuid[64] = "";
    char *snap_device = NULL;
    glusterd_brickinfo_t *snap_brickinfo = NULL;
    gf_boolean_t add_missed_snap = _gf_false;
    int32_t ret = -1;
    xlator_t *this = THIS;
    char abspath[PATH_MAX] = "";
    int32_t len = 0;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap_vol);
    GF_ASSERT(original_brickinfo);

    snprintf(key, sizeof(key), "vol%" PRId64 ".origin_brickpath%d", volcount,
             brick_count);
    ret = dict_set_dynstr_with_alloc(dict, key, original_brickinfo->path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s", key);
        goto out;
    }

    ret = glusterd_brickinfo_new(&snap_brickinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_NEW_INFO_FAIL,
               "initializing the brick for the snap "
               "volume failed (snapname: %s)",
               snap_vol->snapshot->snapname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "vol%" PRId64 ".fstype%d", volcount,
                      brick_count);
    ret = dict_get_strn(dict, key, keylen, &value);
    if (!ret) {
        /* Update the fstype in original brickinfo as well */
        gf_strncpy(original_brickinfo->fstype, value,
                   sizeof(original_brickinfo->fstype));
        gf_strncpy(snap_brickinfo->fstype, value,
                   sizeof(snap_brickinfo->fstype));
    } else {
        if (is_origin_glusterd(dict) == _gf_true)
            add_missed_snap = _gf_true;
    }

    keylen = snprintf(key, sizeof(key), "vol%" PRId64 ".snap_type%d", volcount,
                      brick_count);
    ret = dict_get_strn(dict, key, keylen, &value);
    if (!ret) {
        /* Update the snap_type in original brickinfo as well */
        gf_strncpy(original_brickinfo->snap_type, value,
                   sizeof(original_brickinfo->snap_type));
        gf_strncpy(snap_brickinfo->snap_type, value,
                   sizeof(snap_brickinfo->snap_type));
    } else {
        if (is_origin_glusterd(dict) == _gf_true)
            add_missed_snap = _gf_true;
    }

    keylen = snprintf(key, sizeof(key), "vol%" PRId64 ".mnt_opts%d", volcount,
                      brick_count);
    ret = dict_get_strn(dict, key, keylen, &value);
    if (!ret) {
        /* Update the mnt_opts in original brickinfo as well */
        gf_strncpy(original_brickinfo->mnt_opts, value,
                   sizeof(original_brickinfo->mnt_opts));
        gf_strncpy(snap_brickinfo->mnt_opts, value,
                   sizeof(snap_brickinfo->mnt_opts));
    } else {
        if (is_origin_glusterd(dict) == _gf_true)
            add_missed_snap = _gf_true;
    }

    keylen = snprintf(key, sizeof(key), "vol%" PRId64 ".brickdir%d", volcount,
                      brick_count);
    ret = dict_get_strn(dict, key, keylen, &snap_brick_dir);
    if (ret) {
        /* Using original brickinfo here because it will be a
         * pending snapshot and storing the original brickinfo
         * will help in mapping while recreating the missed snapshot
         */
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_NOT_FOUND,
               "Unable to fetch "
               "snap mount path(%s). Adding to missed_snap_list",
               key);
        snap_brickinfo->snap_status = -1;

        snap_brick_dir = original_brickinfo->mount_dir;

        /* In origiator node add snaps missed
         * from different nodes to the dict
         */
        if (is_origin_glusterd(dict) == _gf_true)
            add_missed_snap = _gf_true;
    }

    if ((snap_brickinfo->snap_status != -1) &&
        (!gf_uuid_compare(original_brickinfo->uuid, MY_UUID)) &&
        (!glusterd_is_brick_started(original_brickinfo))) {
        /* In case if the brick goes down after prevalidate. */
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_BRICK_DISCONNECTED,
               "brick %s:%s is not"
               " started (snap: %s)",
               original_brickinfo->hostname, original_brickinfo->path,
               snap_vol->snapshot->snapname);

        snap_brickinfo->snap_status = -1;
        add_missed_snap = _gf_true;
    }

    if (add_missed_snap) {
        ret = glusterd_add_missed_snaps_to_dict(
            rsp_dict, snap_vol, original_brickinfo, brick_count + 1,
            GF_SNAP_OPTION_TYPE_CREATE);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSEDSNAP_INFO_SET_FAIL,
                   "Failed to add missed"
                   " snapshot info for %s:%s in the rsp_dict",
                   original_brickinfo->hostname, original_brickinfo->path);
            goto out;
        }
    }

    /* Create brick-path in the format /var/run/gluster/snaps/ *
     * <snap-uuid>/<original-brick#>/snap-brick-dir *
     */
    if (clone) {
        GLUSTERD_GET_UUID_NOHYPHEN(clone_uuid, snap_vol->volume_id);
        len = snprintf(snap_brick_path, sizeof(snap_brick_path),
                       "%s/%s/brick%d%s", snap_mount_dir, clone_uuid,
                       brick_count + 1, snap_brick_dir);
    } else {
        len = snprintf(snap_brick_path, sizeof(snap_brick_path),
                       "%s/%s/brick%d%s", snap_mount_dir, snap_vol->volname,
                       brick_count + 1, snap_brick_dir);
    }
    if ((len < 0) || (len >= sizeof(snap_brick_path))) {
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "vol%" PRId64 ".brick_snapdevice%d",
                      volcount, brick_count);
    ret = dict_get_strn(dict, key, keylen, &snap_device);
    if (ret) {
        /* If the device name is empty, so will be the brick path
         * Hence the missed snap has already been added above
         */
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND,
               "Unable to fetch "
               "snap device (%s). Leaving empty",
               key);
    } else
        gf_strncpy(snap_brickinfo->device_path, snap_device,
                   sizeof(snap_brickinfo->device_path));

    ret = gf_canonicalize_path(snap_brick_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CANONICALIZE_FAIL,
               "Failed to canonicalize path");
        goto out;
    }

    gf_strncpy(snap_brickinfo->hostname, original_brickinfo->hostname,
               sizeof(snap_brickinfo->hostname));
    gf_strncpy(snap_brickinfo->path, snap_brick_path,
               sizeof(snap_brickinfo->path));

    if (!realpath(snap_brick_path, abspath)) {
        /* ENOENT indicates that brick path has not been created which
         * is a valid scenario */
        if (errno != ENOENT) {
            gf_msg(this->name, GF_LOG_CRITICAL, errno,
                   GD_MSG_BRICKINFO_CREATE_FAIL,
                   "realpath () "
                   "failed for brick %s. The underlying filesystem"
                   " may be in bad state",
                   snap_brick_path);
            ret = -1;
            goto out;
        }
    }
    gf_strncpy(snap_brickinfo->real_path, abspath,
               sizeof(snap_brickinfo->real_path));

    gf_strncpy(snap_brickinfo->mount_dir, original_brickinfo->mount_dir,
               sizeof(snap_brickinfo->mount_dir));
    gf_uuid_copy(snap_brickinfo->uuid, original_brickinfo->uuid);
    /* AFR changelog names are based on brick_id and hence the snap
     * volume's bricks must retain the same ID */
    cds_list_add_tail(&snap_brickinfo->brick_list, &snap_vol->bricks);

    if (clone) {
        GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO(snap_brickinfo, snap_vol,
                                             brick_count);
    } else
        gf_strncpy(snap_brickinfo->brick_id, original_brickinfo->brick_id,
                   sizeof(snap_brickinfo->brick_id));

out:
    if (ret && snap_brickinfo)
        GF_FREE(snap_brickinfo);

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* This function will update the file-system label of the
 * backend snapshot brick.
 *
 * @param brickinfo     brickinfo of the snap volume
 *
 * @return 0 on success and -1 on failure
 */
int
glusterd_update_fs_label(glusterd_brickinfo_t *brickinfo)
{
    int32_t ret = -1;
    char msg[PATH_MAX] = "";
    char label[NAME_MAX] = "";
    uuid_t uuid = {
        0,
    };
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;
    int32_t len = 0;

    GF_ASSERT(brickinfo);

    /* Generate a new UUID */
    gf_uuid_generate(uuid);

    GLUSTERD_GET_UUID_NOHYPHEN(label, uuid);

    runinit(&runner);

    /* Call the file-system specific tools to update the file-system
     * label. Currently we are only supporting xfs and ext2/ext3/ext4
     * file-system.
     */
    if (0 == strcmp(brickinfo->fstype, "xfs")) {
        /* XFS label is of size 12. Therefore we should truncate the
         * label to 12 bytes*/
        label[12] = '\0';
        len = snprintf(msg, sizeof(msg),
                       "Changing filesystem label "
                       "of %s brick to %s",
                       brickinfo->path, label);
        if (len < 0) {
            strcpy(msg, "<error>");
        }
        /* Run the run xfs_admin tool to change the label
         * of the file-system */
        runner_add_args(&runner, "xfs_admin", "-L", label,
                        brickinfo->device_path, NULL);
    } else if (0 == strcmp(brickinfo->fstype, "ext4") ||
               0 == strcmp(brickinfo->fstype, "ext3") ||
               0 == strcmp(brickinfo->fstype, "ext2")) {
        /* Ext2/Ext3/Ext4 label is of size 16. Therefore we should
         * truncate the label to 16 bytes*/
        label[16] = '\0';
        len = snprintf(msg, sizeof(msg),
                       "Changing filesystem label "
                       "of %s brick to %s",
                       brickinfo->path, label);
        if (len < 0) {
            strcpy(msg, "<error>");
        }
        /* For ext2/ext3/ext4 run tune2fs to change the
         * file-system label */
        runner_add_args(&runner, "tune2fs", "-L", label, brickinfo->device_path,
                        NULL);
    } else {
        gf_msg(this->name, GF_LOG_WARNING, EOPNOTSUPP, GD_MSG_OP_UNSUPPORTED,
               "Changing file-system "
               "label of %s file-system is not supported as of now",
               brickinfo->fstype);
        runner_end(&runner);
        ret = -1;
        goto out;
    }

    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FS_LABEL_UPDATE_FAIL,
               "Failed to change "
               "filesystem label of %s brick to %s",
               brickinfo->path, label);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int32_t
glusterd_take_brick_snapshot(dict_t *dict, glusterd_volinfo_t *snap_vol,
                             glusterd_brickinfo_t *brickinfo, int32_t volcount,
                             int32_t brick_count, int32_t clone)
{
    char *origin_brick_path = NULL;
    char key[64] = "";
    int keylen;
    int32_t ret = -1;
    gf_boolean_t snap_activate = _gf_false;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(dict);
    GF_ASSERT(snap_vol);
    GF_ASSERT(brickinfo);
    GF_ASSERT(priv);

    if (strlen(brickinfo->device_path) == 0) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Device path is empty "
               "brick %s:%s",
               brickinfo->hostname, brickinfo->path);
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "vol%d.origin_brickpath%d", volcount,
                      brick_count);
    ret = dict_get_strn(dict, key, keylen, &origin_brick_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch "
               "brick path (%s)",
               key);
        goto out;
    }

    if (!glusterd_snapshot_probe(origin_brick_path, brickinfo)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Snapshots not supported on brick %s:%s", brickinfo->hostname,
               origin_brick_path);
        ret = -1;
        goto out;
    }

    if (clone)
      ret = brickinfo->snap->clone(brickinfo, origin_brick_path);
    else
      ret = brickinfo->snap->create(brickinfo, origin_brick_path);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Failed to take snapshot of "
               "brick %s:%s",
               brickinfo->hostname, origin_brick_path);
        goto out;
    }

    /* create the complete brick here in case of clone and
     * activate-on-create configuration.
     */
    snap_activate = dict_get_str_boolean(
        priv->opts, GLUSTERD_STORE_KEY_SNAP_ACTIVATE, _gf_false);
    if (clone || snap_activate) {
        ret = glusterd_snap_brick_create(snap_vol, brickinfo, brick_count,
                                         clone);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_CREATION_FAIL,
                   "not able to "
                   "create the brick for the snap %s, volume %s",
                   snap_vol->snapshot->snapname, snap_vol->volname);
            goto out;
        }
    }

out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_snap_clear_unsupported_opt(
    glusterd_volinfo_t *volinfo,
    struct gd_snap_unsupported_opt_t *unsupported_opt)
{
    int ret = -1;
    int i = 0;

    GF_VALIDATE_OR_GOTO("glusterd", volinfo, out);

    for (i = 0; unsupported_opt[i].key; i++) {
        glusterd_volinfo_get(volinfo, unsupported_opt[i].key,
                             &unsupported_opt[i].value);

        if (unsupported_opt[i].value) {
            unsupported_opt[i].value = gf_strdup(unsupported_opt[i].value);
            if (!unsupported_opt[i].value) {
                ret = -1;
                goto out;
            }
            dict_del(volinfo->dict, unsupported_opt[i].key);
        }
    }

    ret = 0;
out:
    return ret;
}

static int
glusterd_snap_set_unsupported_opt(
    glusterd_volinfo_t *volinfo,
    struct gd_snap_unsupported_opt_t *unsupported_opt)
{
    int ret = -1;
    int i = 0;

    GF_VALIDATE_OR_GOTO("glusterd", volinfo, out);

    for (i = 0; unsupported_opt[i].key; i++) {
        if (!unsupported_opt[i].value)
            continue;

        ret = dict_set_dynstr(volinfo->dict, unsupported_opt[i].key,
                              unsupported_opt[i].value);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                   "dict set failed");
            goto out;
        }
        unsupported_opt[i].value = NULL;
    }

    ret = 0;
out:
    return ret;
}

glusterd_volinfo_t *
glusterd_do_snap_vol(glusterd_volinfo_t *origin_vol, glusterd_snap_t *snap,
                     dict_t *dict, dict_t *rsp_dict, int64_t volcount,
                     int clone)
{
    char key[64] = "";
    int keylen;
    char *username = NULL;
    char *password = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    uuid_t *snap_volid = NULL;
    int32_t ret = -1;
    int32_t brick_count = 0;
    xlator_t *this = THIS;
    char *clonename = NULL;
    gf_boolean_t conf_present = _gf_false;
    int i = 0;

    struct gd_snap_unsupported_opt_t unsupported_opt[] = {
        {.key = VKEY_FEATURES_QUOTA, .value = NULL},
        {.key = VKEY_FEATURES_INODE_QUOTA, .value = NULL},
        {.key = "feature.deem-statfs", .value = NULL},
        {.key = "features.quota-deem-statfs", .value = NULL},
        {.key = NULL, .value = NULL}};

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(dict);
    GF_ASSERT(origin_vol);
    GF_ASSERT(rsp_dict);

    /* fetch username, password and vol_id from dict*/
    keylen = snprintf(key, sizeof(key), "volume%" PRId64 "_username", volcount);
    ret = dict_get_strn(dict, key, keylen, &username);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get %s for "
               "snap %s",
               key, snap->snapname);
        goto out;
    }
    keylen = snprintf(key, sizeof(key), "volume%" PRId64 "_password", volcount);
    ret = dict_get_strn(dict, key, keylen, &password);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get %s for "
               "snap %s",
               key, snap->snapname);
        goto out;
    }

    snprintf(key, sizeof(key), "vol%" PRId64 "_volid", volcount);
    ret = dict_get_bin(dict, key, (void **)&snap_volid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch snap_volid");
        goto out;
    }

    /* We are not setting the username and password here as
     * we need to set the user name and password passed in
     * the dictionary
     */
    ret = glusterd_volinfo_dup(origin_vol, &snap_vol, _gf_false);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
               "Failed to duplicate volinfo "
               "for the snapshot %s",
               snap->snapname);
        goto out;
    }

    /* uuid is used as snapshot name.
       This will avoid restrictions on snapshot names provided by user */
    gf_uuid_copy(snap_vol->volume_id, *snap_volid);
    snap_vol->is_snap_volume = _gf_true;
    snap_vol->snapshot = snap;

    if (clone) {
        snap_vol->is_snap_volume = _gf_false;
        ret = dict_get_strn(dict, "clonename", SLEN("clonename"), &clonename);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get %s "
                   "for snap %s",
                   key, snap->snapname);
            goto out;
        }
        cds_list_add_tail(&snap_vol->vol_list, &snap->volumes);
        gf_strncpy(snap_vol->volname, clonename, sizeof(snap_vol->volname));
        gf_uuid_copy(snap_vol->restored_from_snap,
                     origin_vol->snapshot->snap_id);

    } else {
        GLUSTERD_GET_UUID_NOHYPHEN(snap_vol->volname, *snap_volid);
        gf_strncpy(snap_vol->parent_volname, origin_vol->volname,
                   sizeof(snap_vol->parent_volname));
        ret = glusterd_list_add_snapvol(origin_vol, snap_vol);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_LIST_SET_FAIL,
                   "could not add the "
                   "snap volume %s to the list",
                   snap_vol->volname);
            goto out;
        }
        /* TODO : Sync before taking a snapshot */
        /* Copy the status and config files of geo-replication before
         * taking a snapshot. During restore operation these files needs
         * to be copied back in /var/lib/glusterd/georeplication/
         */
        ret = glusterd_copy_geo_rep_files(origin_vol, snap_vol, rsp_dict);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
                   "Failed to copy "
                   "geo-rep config and status files for volume %s",
                   origin_vol->volname);
            goto out;
        }
    }

    glusterd_auth_set_username(snap_vol, username);
    glusterd_auth_set_password(snap_vol, password);

    /* Adding snap brickinfos to the snap volinfo */
    brick_count = 0;
    cds_list_for_each_entry(brickinfo, &origin_vol->bricks, brick_list)
    {
        ret = glusterd_add_brick_to_snap_volume(
            dict, rsp_dict, snap_vol, brickinfo, volcount, brick_count, clone);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_ADD_FAIL,
                   "Failed to add the snap brick for "
                   "%s:%s to the snap volume",
                   brickinfo->hostname, brickinfo->path);
            goto out;
        }
        brick_count++;
    }

    /* During snapshot creation if I/O is in progress,
     * then barrier value is enabled. Hence during snapshot create
     * and in-turn snapshot restore the barrier value is set to enable.
     * Because of this further I/O on the mount point fails.
     * Hence remove the barrier key from newly created snap volinfo
     * before storing and generating the brick volfiles. Also update
     * the snap vol's version after removing the barrier key.
     */
    dict_deln(snap_vol->dict, "features.barrier", SLEN("features.barrier"));
    gd_update_volume_op_versions(snap_vol);

    /* *
     * Create the export file from the node where ganesha.enable "on"
     * is executed
     * */
    if (glusterd_is_ganesha_cluster() &&
        glusterd_check_ganesha_export(snap_vol)) {
        if (is_origin_glusterd(dict)) {
            ret = manage_export_config(clonename, "on", NULL);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_EXPORT_FILE_CREATE_FAIL,
                       "Failed to create"
                       "export file for NFS-Ganesha\n");
                goto out;
            }
        }

        ret = dict_set_dynstr_with_alloc(snap_vol->dict,
                                         "features.cache-invalidation", "on");
        ret = gd_ganesha_send_dbus(clonename, "on");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_EXPORT_FILE_CREATE_FAIL,
                   "Dynamic export addition/deletion failed."
                   " Please see log file for details. Clone name = %s",
                   clonename);
            goto out;
        }
    }
    if (!glusterd_is_ganesha_cluster() &&
        glusterd_check_ganesha_export(snap_vol)) {
        /* This happens when a snapshot was created when Ganesha was
         * enabled globally. Then Ganesha disabled from the cluster.
         * In such cases, we will have the volume level option set
         * on dict, So we have to disable it as it doesn't make sense
         * to keep the option.
         */

        ret = dict_set_dynstr(snap_vol->dict, "ganesha.enable", "off");
        if (ret)
            goto out;
    }

    ret = glusterd_store_volinfo(snap_vol, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_SET_FAIL,
               "Failed to store snapshot "
               "volinfo (%s) for snap %s",
               snap_vol->volname, snap->snapname);
        goto out;
    }

    ret = glusterd_copy_quota_files(origin_vol, snap_vol, &conf_present);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_VOL_CONFIG_FAIL,
               "Failed to copy quota "
               "config and cksum for volume %s",
               origin_vol->volname);
        goto out;
    }

    if (snap_vol->is_snap_volume) {
        ret = glusterd_snap_clear_unsupported_opt(snap_vol, unsupported_opt);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
                   "Failed to clear quota "
                   "option for the snap %s (volume: %s)",
                   snap->snapname, origin_vol->volname);
            goto out;
        }
    }

    ret = generate_brick_volfiles(snap_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "generating the brick "
               "volfiles for the snap %s (volume: %s) failed",
               snap->snapname, origin_vol->volname);
        goto reset_option;
    }

    ret = generate_client_volfiles(snap_vol, GF_CLIENT_TRUSTED);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "generating the trusted "
               "client volfiles for the snap %s (volume: %s) failed",
               snap->snapname, origin_vol->volname);
        goto reset_option;
    }

    ret = generate_client_volfiles(snap_vol, GF_CLIENT_OTHER);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "generating the client "
               "volfiles for the snap %s (volume: %s) failed",
               snap->snapname, origin_vol->volname);
        goto reset_option;
    }

reset_option:
    if (snap_vol->is_snap_volume) {
        if (glusterd_snap_set_unsupported_opt(snap_vol, unsupported_opt)) {
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
                   "Failed to reset quota "
                   "option for the snap %s (volume: %s)",
                   snap->snapname, origin_vol->volname);
        }
    }
out:
    if (ret) {
        for (i = 0; unsupported_opt[i].key; i++)
            GF_FREE(unsupported_opt[i].value);

        if (snap_vol) {
            if (glusterd_is_ganesha_cluster() &&
                glusterd_check_ganesha_export(snap_vol)) {
                if (is_origin_glusterd(dict)) {
                    ret = manage_export_config(clonename, "on", NULL);
                    if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               GD_MSG_EXPORT_FILE_CREATE_FAIL,
                               "Failed to create"
                               "export file for NFS-Ganesha\n");
                    }
                }

                ret = gd_ganesha_send_dbus(clonename, "off");
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_EXPORT_FILE_CREATE_FAIL,
                           "Dynamic export addition/deletion failed."
                           " Please see log file for details. Clone name = %s",
                           clonename);
                }
            }

            glusterd_snap_volume_remove(rsp_dict, snap_vol, _gf_true, _gf_true);
        }
        snap_vol = NULL;
    }

    return snap_vol;
}

/*This is the prevalidate function for both activate and deactive of snap
 * For Activate operation pass is_op_activate as _gf_true
 * For Deactivate operation pass is_op_activate as _gf_false
 * */
int
glusterd_snapshot_activate_deactivate_prevalidate(dict_t *dict,
                                                  char **op_errstr,
                                                  uint32_t *op_errno,
                                                  dict_t *rsp_dict,
                                                  gf_boolean_t is_op_activate)
{
    int32_t ret = -1;
    char *snapname = NULL;
    xlator_t *this = THIS;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *snap_volinfo = NULL;
    char err_str[PATH_MAX] = "";
    gf_loglevel_t loglevel = GF_LOG_ERROR;
    glusterd_volume_status volume_status = GLUSTERD_STATUS_STOPPED;
    int flags = 0;

    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    if (!dict || !op_errstr) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "input parameters NULL");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Getting the snap name "
               "failed");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        snprintf(err_str, sizeof(err_str),
                 "Snapshot (%s) does not "
                 "exist.",
                 snapname);
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND,
                "Snapname=%s", snapname, NULL);
        *op_errno = EG_NOSNAP;
        ret = -1;
        goto out;
    }

    /*If its activation of snap then fetch the flags*/
    if (is_op_activate) {
        ret = dict_get_int32n(dict, "flags", SLEN("flags"), &flags);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get flags");
            goto out;
        }
    }

    /* TODO : As of now there is only volume in snapshot.
     * Change this when multiple volume snapshot is introduced
     */
    snap_volinfo = cds_list_entry(snap->volumes.next, glusterd_volinfo_t,
                                  vol_list);
    if (!snap_volinfo) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOLINFO_GET_FAIL,
               "Unable to fetch snap_volinfo");
        ret = -1;
        goto out;
    }

    /*TODO: When multiple snapvolume are involved a cumulative
     * logic is required to tell whether is snapshot is
     * started/partially started/stopped*/
    if (is_op_activate) {
        volume_status = GLUSTERD_STATUS_STARTED;
    }

    if (snap_volinfo->status == volume_status) {
        if (is_op_activate) {
            /* if flag is to GF_CLI_FLAG_OP_FORCE
             * try to start the snap volume, even
             * if the volume_status is GLUSTERD_STATUS_STARTED.
             * By doing so we try to bring
             * back the brick processes that are down*/
            if (!(flags & GF_CLI_FLAG_OP_FORCE)) {
                snprintf(err_str, sizeof(err_str),
                         "Snapshot %s is already activated.", snapname);
                *op_errno = EINVAL;
                ret = -1;
            }
        } else {
            snprintf(err_str, sizeof(err_str),
                     "Snapshot %s is already deactivated.", snapname);
            *op_errno = EINVAL;
            ret = -1;
        }
        goto out;
    }
    ret = 0;
out:

    if (ret && err_str[0] != '\0' && op_errstr) {
        gf_msg(this->name, loglevel, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
               err_str);
        *op_errstr = gf_strdup(err_str);
    }

    return ret;
}

int32_t
glusterd_handle_snapshot_delete_vol(dict_t *dict, char *err_str,
                                    uint32_t *op_errno, int len)
{
    int32_t ret = -1;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    char *volname = NULL;

    GF_ASSERT(dict);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get "
               "volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(err_str, len, "Volume (%s) does not exist", volname);
        *op_errno = EG_NOVOL;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
               "Failed to get volinfo of "
               "volume %s",
               volname);
        goto out;
    }

    ret = glusterd_snapshot_get_vol_snapnames(dict, volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_LIST_GET_FAIL,
               "Failed to get snapshot list for volume %s", volname);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int32_t
glusterd_handle_snapshot_delete_all(dict_t *dict)
{
    int32_t ret = -1;
    int32_t i = 0;
    char key[32] = "";
    glusterd_conf_t *priv = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_snap_t *tmp_snap = NULL;
    xlator_t *this = THIS;

    this = THIS;
    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(dict);

    cds_list_for_each_entry_safe(snap, tmp_snap, &priv->snapshots, snap_list)
    {
        /* indexing from 1 to n, to keep it uniform with other code
         * paths
         */
        i++;
        ret = snprintf(key, sizeof(key), "snapname%d", i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_set_dynstr_with_alloc(dict, key, snap->snapname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save "
                   "snap name");
            goto out;
        }
    }

    ret = dict_set_int32n(dict, "snapcount", SLEN("snapcount"), i);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save snapcount");
        goto out;
    }

    ret = 0;

out:
    return ret;
}

int32_t
glusterd_handle_snapshot_delete_type_snap(rpcsvc_request_t *req,
                                          glusterd_op_t op, dict_t *dict,
                                          char *err_str, uint32_t *op_errno,
                                          size_t len)
{
    int32_t ret = -1;
    int64_t volcount = 0;
    char *snapname = NULL;
    char *volname = NULL;
    char key[64] = "";
    int keylen;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *tmp = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(req);
    GF_ASSERT(dict);
    GF_ASSERT(err_str);

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get snapname");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        snprintf(err_str, len, "Snapshot (%s) does not exist", snapname);
        *op_errno = EG_NOSNAP;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND, "%s",
               err_str);
        ret = -1;
        goto out;
    }

    /* Set volnames in the dict to get mgmt_v3 lock */
    cds_list_for_each_entry_safe(snap_vol, tmp, &snap->volumes, vol_list)
    {
        volcount++;
        volname = gf_strdup(snap_vol->parent_volname);
        if (!volname) {
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "strdup failed");
            goto out;
        }

        keylen = snprintf(key, sizeof(key), "volname%" PRId64, volcount);
        ret = dict_set_dynstrn(dict, key, keylen, volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set "
                   "volume name in dictionary");
            GF_FREE(volname);
            goto out;
        }
        volname = NULL;
    }
    ret = dict_set_int64(dict, "volcount", volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set volcount");
        goto out;
    }

    ret = glusterd_mgmt_v3_initiate_snap_phases(req, op, dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_INIT_FAIL,
               "Failed to initiate snap "
               "phases");
        goto out;
    }

    ret = 0;

out:
    return ret;
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
glusterd_handle_snapshot_delete(rpcsvc_request_t *req, glusterd_op_t op,
                                dict_t *dict, char *err_str, uint32_t *op_errno,
                                size_t len)
{
    int ret = -1;
    xlator_t *this = THIS;
    int32_t delete_cmd = -1;

    GF_ASSERT(req);
    GF_ASSERT(dict);
    GF_ASSERT(err_str);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    ret = dict_get_int32n(dict, "sub-cmd", SLEN("sub-cmd"), &delete_cmd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "Failed to get sub-cmd");
        goto out;
    }

    switch (delete_cmd) {
        case GF_SNAP_DELETE_TYPE_SNAP:
        case GF_SNAP_DELETE_TYPE_ITER:
            ret = glusterd_handle_snapshot_delete_type_snap(
                req, op, dict, err_str, op_errno, len);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Failed to handle "
                       "snapshot delete for type SNAP");
                goto out;
            }
            break;

        case GF_SNAP_DELETE_TYPE_ALL:
            ret = glusterd_handle_snapshot_delete_all(dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Failed to handle "
                       "snapshot delete for type ALL");
                goto out;
            }
            break;

        case GF_SNAP_DELETE_TYPE_VOL:
            ret = glusterd_handle_snapshot_delete_vol(dict, err_str, op_errno,
                                                      len);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Failed to handle "
                       "snapshot delete for type VOL");
                goto out;
            }
            break;

        default:
            *op_errno = EINVAL;
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                   "Wrong snapshot delete type");
            break;
    }

    if (ret == 0 && (delete_cmd == GF_SNAP_DELETE_TYPE_ALL ||
                     delete_cmd == GF_SNAP_DELETE_TYPE_VOL)) {
        ret = glusterd_op_send_cli_response(op, 0, 0, req, dict, err_str);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NO_CLI_RESP,
                   "Failed to send cli "
                   "response");
            goto out;
        }
    }
    ret = 0;
out:
    return ret;
}

int
glusterd_snapshot_remove_prevalidate(dict_t *dict, char **op_errstr,
                                     uint32_t *op_errno, dict_t *rsp_dict)
{
    int32_t ret = -1;
    char *snapname = NULL;
    xlator_t *this = NULL;
    glusterd_snap_t *snap = NULL;

    this = THIS;

    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    if (!dict || !op_errstr) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "input parameters NULL");
        goto out;
    }

    ret = dict_get_str(dict, "snapname", &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Getting the snap name "
               "failed");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot (%s) does not exist", snapname);
        *op_errno = EG_NOSNAP;
        ret = -1;
        goto out;
    }
    ret = dict_set_dynstr_with_alloc(dict, "snapuuid",
                                     uuid_utoa(snap->snap_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap "
               "uuid in response dictionary for %s snapshot",
               snap->snapname);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_snapshot_status_prevalidate(dict_t *dict, char **op_errstr,
                                     uint32_t *op_errno, dict_t *rsp_dict)
{
    int ret = -1;
    char *snapname = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    int32_t cmd = -1;
    glusterd_volinfo_t *volinfo = NULL;
    char *volname = NULL;

    conf = this->private;
    GF_ASSERT(conf);
    GF_ASSERT(op_errstr);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    if (!dict) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "Input dict is NULL");
        goto out;
    }

    ret = dict_get_int32n(dict, "sub-cmd", SLEN("sub-cmd"), &cmd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Could not fetch status cmd");
        goto out;
    }

    switch (cmd) {
        case GF_SNAP_STATUS_TYPE_ALL: {
            break;
        }
        case GF_SNAP_STATUS_TYPE_ITER:
        case GF_SNAP_STATUS_TYPE_SNAP: {
            ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Could not fetch snapname");
                goto out;
            }

            if (!glusterd_find_snap_by_name(snapname)) {
                ret = gf_asprintf(op_errstr,
                                  "Snapshot (%s) "
                                  "does not exist",
                                  snapname);
                *op_errno = EG_NOSNAP;
                if (ret < 0) {
                    goto out;
                }
                ret = -1;
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
                       "Snapshot (%s) does not exist", snapname);
                goto out;
            }
            break;
        }
        case GF_SNAP_STATUS_TYPE_VOL: {
            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Could not fetch volname");
                goto out;
            }

            ret = glusterd_volinfo_find(volname, &volinfo);
            if (ret) {
                ret = gf_asprintf(op_errstr,
                                  "Volume (%s) "
                                  "does not exist",
                                  volname);
                *op_errno = EG_NOVOL;
                if (ret < 0) {
                    goto out;
                }
                ret = -1;
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
                       "Volume "
                       "%s not present",
                       volname);
                goto out;
            }
            break;
        }
        default: {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_COMMAND_NOT_FOUND,
                   "Invalid command");
            *op_errno = EINVAL;
            break;
        }
    }
    ret = 0;

out:
    return ret;
}

int32_t
glusterd_snapshot_activate_commit(dict_t *dict, char **op_errstr,
                                  dict_t *rsp_dict)
{
    int32_t ret = -1;
    char *snapname = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *snap_volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    xlator_t *this = THIS;
    int flags = 0;
    int brick_count = -1;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_errstr);

    if (!dict || !op_errstr) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "input parameters NULL");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Getting the snap name "
               "failed");
        goto out;
    }

    ret = dict_get_int32n(dict, "flags", SLEN("flags"), &flags);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get flags");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot (%s) does not exist", snapname);
        ret = -1;
        goto out;
    }

    /* TODO : As of now there is only volume in snapshot.
     * Change this when multiple volume snapshot is introduced
     */
    snap_volinfo = cds_list_entry(snap->volumes.next, glusterd_volinfo_t,
                                  vol_list);
    if (!snap_volinfo) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Unable to fetch snap_volinfo");
        ret = -1;
        goto out;
    }

    /* create the complete brick here */
    cds_list_for_each_entry(brickinfo, &snap_volinfo->bricks, brick_list)
    {
        brick_count++;
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
            continue;
        ret = glusterd_snap_brick_create(snap_volinfo, brickinfo, brick_count,
                                         _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_CREATION_FAIL,
                   "not able to "
                   "create the brick for the snap %s, volume %s",
                   snap_volinfo->snapshot->snapname, snap_volinfo->volname);
            goto out;
        }
    }

    ret = glusterd_start_volume(snap_volinfo, flags, _gf_true);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_ACTIVATE_FAIL,
               "Failed to activate snap volume %s of the snap %s",
               snap_volinfo->volname, snap->snapname);
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(rsp_dict, "snapuuid",
                                     uuid_utoa(snap->snap_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap "
               "uuid in response dictionary for %s snapshot",
               snap->snapname);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int32_t
glusterd_snapshot_deactivate_commit(dict_t *dict, char **op_errstr,
                                    dict_t *rsp_dict)
{
    int32_t ret = -1;
    char *snapname = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *snap_volinfo = NULL;
    xlator_t *this = THIS;
    char snap_path[PATH_MAX] = "";

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_errstr);

    if (!dict || !op_errstr) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "input parameters NULL");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Getting the snap name "
               "failed");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot (%s) does not exist", snapname);
        ret = -1;
        goto out;
    }

    /* TODO : As of now there is only volume in snapshot.
     * Change this when multiple volume snapshot is introduced
     */
    snap_volinfo = cds_list_entry(snap->volumes.next, glusterd_volinfo_t,
                                  vol_list);
    if (!snap_volinfo) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Unable to fetch snap_volinfo");
        ret = -1;
        goto out;
    }

    ret = glusterd_stop_volume(snap_volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_DEACTIVATE_FAIL,
               "Failed to deactivate"
               "snap %s",
               snapname);
        goto out;
    }

    ret = glusterd_snap_unmount(this, snap_volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_UMOUNT_FAIL,
               "Failed to unmounts for %s", snap->snapname);
    }

    /*Remove /var/run/gluster/snaps/<snap-name> entry for deactivated snaps.
     * This entry will be created again during snap activate.
     */
    snprintf(snap_path, sizeof(snap_path), "%s/%s", snap_mount_dir, snapname);
    ret = recursive_rmdir(snap_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to remove "
               "%s directory : error : %s",
               snap_path, strerror(errno));
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(rsp_dict, "snapuuid",
                                     uuid_utoa(snap->snap_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap "
               "uuid in response dictionary for %s snapshot",
               snap->snapname);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int32_t
glusterd_snapshot_remove_commit(dict_t *dict, char **op_errstr,
                                dict_t *rsp_dict)
{
    int32_t ret = -1;
    char *snapname = NULL;
    char *dup_snapname = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *snap_volinfo = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_errstr);

    priv = this->private;
    GF_ASSERT(priv);

    if (!dict || !op_errstr) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "input parameters NULL");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Getting the snap name "
               "failed");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot (%s) does not exist", snapname);
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(rsp_dict, "snapuuid",
                                     uuid_utoa(snap->snap_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap uuid in "
               "response dictionary for %s snapshot",
               snap->snapname);
        goto out;
    }

    /* Save the snap status as GD_SNAP_STATUS_DECOMMISSION so
     * that if the node goes down the snap would be removed
     */
    snap->snap_status = GD_SNAP_STATUS_DECOMMISSION;
    ret = glusterd_store_snap(snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_OBJECT_STORE_FAIL,
               "Failed to "
               "store snap object %s",
               snap->snapname);
        goto out;
    } else
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_OP_SUCCESS,
               "Successfully marked "
               "snap %s for decommission.",
               snap->snapname);

    if (is_origin_glusterd(dict) == _gf_true) {
        /* TODO : As of now there is only volume in snapshot.
         * Change this when multiple volume snapshot is introduced
         */
        snap_volinfo = cds_list_entry(snap->volumes.next, glusterd_volinfo_t,
                                      vol_list);
        if (!snap_volinfo) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
                   "Unable to fetch snap_volinfo");
            ret = -1;
            goto out;
        }

        /* From origin glusterd check if      *
         * any peers with snap bricks is down */
        ret = glusterd_find_missed_snap(rsp_dict, snap_volinfo, &priv->peers,
                                        GF_SNAP_OPTION_TYPE_DELETE);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSED_SNAP_GET_FAIL,
                   "Failed to find missed snap deletes");
            goto out;
        }
    }

    ret = glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_false, _gf_false);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "Failed to remove snap %s", snapname);
        goto out;
    }

    dup_snapname = gf_strdup(snapname);
    if (!dup_snapname) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Strdup failed");
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr(rsp_dict, "snapname", dup_snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set the snapname");
        GF_FREE(dup_snapname);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int32_t
glusterd_do_snap_cleanup(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int32_t ret = -1;
    char *name = NULL;
    char *volname = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_snap_t *snap = NULL;

    conf = this->private;
    GF_ASSERT(conf);

    if (!dict || !op_errstr) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
               "input parameters NULL");
        goto out;
    }

    /* As of now snapshot of multiple volumes are not supported */
    ret = dict_get_strn(dict, "volname1", SLEN("volname1"), &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get"
               " volume name");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &name);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "getting the snap "
               "name failed (volume: %s)",
               volname);
        goto out;
    }

    /*
      If the snapname is not found that means the failure happened at
      staging, or in commit, before the snap object is created, in which
      case there is nothing to cleanup. So set ret to 0.
    */
    snap = glusterd_find_snap_by_name(name);
    if (!snap) {
        gf_msg(this->name, GF_LOG_INFO, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot (%s) does not exist", name);
        ret = 0;
        goto out;
    }

    ret = glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_true, _gf_false);
    if (ret) {
        /* Ignore failure as this is a cleanup of half cooked
           snapshot */
        gf_msg_debug(this->name, 0, "removing the snap %s failed", name);
        ret = 0;
    }

    name = NULL;

    ret = 0;

out:

    return ret;
}

/* In case of a successful, delete or create operation, during post_validate *
 * look for missed snap operations and update the missed snap lists */
int32_t
glusterd_snapshot_update_snaps_post_validate(dict_t *dict, char **op_errstr,
                                             dict_t *rsp_dict)
{
    int32_t ret = -1;
    int32_t missed_snap_count = -1;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_errstr);

    ret = dict_get_int32n(dict, "missed_snap_count", SLEN("missed_snap_count"),
                          &missed_snap_count);
    if (ret) {
        gf_msg_debug(this->name, 0, "No missed snaps");
        ret = 0;
        goto out;
    }

    ret = glusterd_add_missed_snaps_to_list(dict, missed_snap_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSEDSNAP_INFO_SET_FAIL,
               "Failed to add missed snaps to list");
        goto out;
    }

    ret = glusterd_store_update_missed_snaps();
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSEDSNAP_INFO_SET_FAIL,
               "Failed to update missed_snaps_list");
        goto out;
    }

out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_take_brick_snapshot_task(void *opaque)
{
    int ret = 0;
    int32_t clone = 0;
    snap_create_args_t *snap_args = NULL;
    char *clonename = NULL;
    char key[64] = "";
    int keylen;

    GF_ASSERT(opaque);

    snap_args = (snap_create_args_t *)opaque;
    THIS = snap_args->this;

    /* Try and fetch clonename. If present set status with clonename *
     * else do so as snap-vol */
    ret = dict_get_strn(snap_args->dict, "clonename", SLEN("clonename"),
                        &clonename);
    if (ret) {
        keylen = snprintf(key, sizeof(key), "snap-vol%d.brick%d.status",
                          snap_args->volcount, snap_args->brickorder);
    } else {
        keylen = snprintf(key, sizeof(key), "clone%d.brick%d.status",
                          snap_args->volcount, snap_args->brickorder);
        clone = 1;
    }

    ret = glusterd_take_brick_snapshot(
        snap_args->dict, snap_args->snap_vol, snap_args->brickinfo,
        snap_args->volcount, snap_args->brickorder, clone);

    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Failed to "
               "take backend snapshot for brick "
               "%s:%s volume(%s)",
               snap_args->brickinfo->hostname, snap_args->brickinfo->path,
               snap_args->snap_vol->volname);
    }

    if (dict_set_int32n(snap_args->rsp_dict, key, keylen, (ret) ? 0 : 1)) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to "
               "add %s to dict",
               key);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int32_t
glusterd_take_brick_snapshot_cbk(int ret, call_frame_t *frame, void *opaque)
{
    snap_create_args_t *snap_args = NULL;
    struct syncargs *args = NULL;

    GF_ASSERT(opaque);

    snap_args = (snap_create_args_t *)opaque;
    args = snap_args->args;

    if (ret)
        args->op_ret = ret;

    GF_FREE(opaque);
    synctask_barrier_wake(args);
    return 0;
}

int32_t
glusterd_schedule_brick_snapshot(dict_t *dict, dict_t *rsp_dict,
                                 glusterd_snap_t *snap)
{
    int ret = -1;
    int32_t volcount = 0;
    int32_t brickcount = 0;
    int32_t brickorder = 0;
    int32_t taskcount = 0;
    char key[64] = "";
    int keylen;
    xlator_t *this = THIS;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    struct syncargs args = {0};
    snap_create_args_t *snap_args = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(snap);

    ret = synctask_barrier_init((&args));
    if (ret)
        goto out;
    cds_list_for_each_entry(snap_vol, &snap->volumes, vol_list)
    {
        volcount++;
        brickcount = 0;
        brickorder = 0;
        cds_list_for_each_entry(brickinfo, &snap_vol->bricks, brick_list)
        {
            keylen = snprintf(key, sizeof(key), "snap-vol%d.brick%d.order",
                              volcount, brickcount);
            ret = dict_set_int32n(rsp_dict, key, keylen, brickorder);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set %s", key);
                goto out;
            }

            if ((gf_uuid_compare(brickinfo->uuid, MY_UUID)) ||
                (brickinfo->snap_status == -1)) {
                if (!gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
                    brickcount++;
                    keylen = snprintf(key, sizeof(key),
                                      "snap-vol%d.brick%d.status", volcount,
                                      brickorder);
                    ret = dict_set_int32n(rsp_dict, key, keylen, 0);
                    if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, 0,
                               GD_MSG_DICT_SET_FAILED,
                               "failed to add %s to "
                               "dict",
                               key);
                        goto out;
                    }
                }
                brickorder++;
                continue;
            }

            snap_args = GF_CALLOC(1, sizeof(*snap_args),
                                  gf_gld_mt_snap_create_args_t);
            if (!snap_args) {
                ret = -1;
                goto out;
            }

            snap_args->this = this;
            snap_args->dict = dict;
            snap_args->rsp_dict = rsp_dict;
            snap_args->snap_vol = snap_vol;
            snap_args->brickinfo = brickinfo;
            snap_args->volcount = volcount;
            snap_args->brickcount = brickcount;
            snap_args->brickorder = brickorder;
            snap_args->args = &args;

            ret = synctask_new(
                this->ctx->env, glusterd_take_brick_snapshot_task,
                glusterd_take_brick_snapshot_cbk, NULL, snap_args);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
                       "Failed to "
                       "spawn task for snapshot create");
                GF_FREE(snap_args);
                goto out;
            }
            taskcount++;
            brickcount++;
            brickorder++;
        }

        snprintf(key, sizeof(key), "snap-vol%d_brickcount", volcount);
        ret = dict_set_int64(rsp_dict, key, brickcount);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "failed to "
                   "add %s to dict",
                   key);
            goto out;
        }
    }
    synctask_barrier_wait((&args), taskcount);
    taskcount = 0;

    if (args.op_ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Failed to create snapshot");

    ret = args.op_ret;
out:
    if (ret && taskcount)
        synctask_barrier_wait((&args), taskcount);

    return ret;
}

glusterd_snap_t *
glusterd_create_snap_object_for_clone(dict_t *dict, dict_t *rsp_dict)
{
    char *snapname = NULL;
    uuid_t *snap_id = NULL;
    glusterd_snap_t *snap = NULL;
    xlator_t *this = THIS;
    int ret = -1;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    /* Fetch snapname, description, id and time from dict */
    ret = dict_get_strn(dict, "clonename", SLEN("clonename"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch clonename");
        goto out;
    }

    ret = dict_get_bin(dict, "clone-id", (void **)&snap_id);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch clone_id");
        goto out;
    }

    snap = glusterd_new_snap_object();
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_OBJ_NEW_FAIL,
               "Could not create "
               "the snap object for snap %s",
               snapname);
        goto out;
    }

    gf_strncpy(snap->snapname, snapname, sizeof(snap->snapname));
    gf_uuid_copy(snap->snap_id, *snap_id);

    ret = 0;

out:
    if (ret) {
        snap = NULL;
    }

    return snap;
}

int32_t
glusterd_snapshot_clone_commit(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;
    int64_t volcount = 0;
    char *snapname = NULL;
    char *volname = NULL;
    char *tmp_name = NULL;
    xlator_t *this = THIS;
    glusterd_snap_t *snap_parent = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *origin_vol = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_conf_t *priv = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "clonename", SLEN("clonename"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch clonename");
        goto out;
    }
    tmp_name = gf_strdup(snapname);
    if (!tmp_name) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out of memory");
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr(rsp_dict, "clonename", tmp_name);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set clonename in rsp_dict");
        GF_FREE(tmp_name);
        goto out;
    }
    tmp_name = NULL;

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get snap name");
        goto out;
    }

    snap_parent = glusterd_find_snap_by_name(volname);
    if (!snap_parent) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "Failed to "
               "fetch snap %s",
               volname);
        goto out;
    }

    /* TODO : As of now there is only one volume in snapshot.
     * Change this when multiple volume snapshot is introduced
     */
    origin_vol = cds_list_entry(snap_parent->volumes.next, glusterd_volinfo_t,
                                vol_list);
    if (!origin_vol) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Failed to get snap "
               "volinfo %s",
               snap_parent->snapname);
        goto out;
    }

    snap = glusterd_create_snap_object_for_clone(dict, rsp_dict);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_OBJ_NEW_FAIL,
               "creating the"
               "snap object %s failed",
               snapname);
        ret = -1;
        goto out;
    }

    snap_vol = glusterd_do_snap_vol(origin_vol, snap, dict, rsp_dict, 1, 1);
    if (!snap_vol) {
        ret = -1;
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CREATION_FAIL,
               "taking the "
               "snapshot of the volume %s failed",
               volname);
        goto out;
    }

    volcount = 1;
    ret = dict_set_int64(rsp_dict, "volcount", volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set volcount");
        goto out;
    }

    ret = glusterd_schedule_brick_snapshot(dict, rsp_dict, snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_BACKEND_MAKE_FAIL,
               "Failed to take backend "
               "snapshot %s",
               snap->snapname);
        goto out;
    }

    cds_list_del_init(&snap_vol->vol_list);
    ret = dict_set_dynstr_with_alloc(rsp_dict, "snapuuid",
                                     uuid_utoa(snap_vol->volume_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap "
               "uuid in response dictionary for %s snapshot",
               snap->snapname);
        goto out;
    }

    glusterd_list_add_order(&snap_vol->vol_list, &priv->volumes,
                            glusterd_compare_volume_name);

    ret = 0;

out:
    if (ret) {
        if (snap)
            glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_true, _gf_true);
        snap = NULL;
    }

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_snapshot_create_commit(dict_t *dict, char **op_errstr,
                                uint32_t *op_errno, dict_t *rsp_dict)
{
    int ret = -1;
    int64_t i = 0;
    int64_t volcount = 0;
    int32_t snap_activate = 0;
    int32_t flags = 0;
    char *snapname = NULL;
    char *volname = NULL;
    char *tmp_name = NULL;
    char key[64] = "";
    int keylen;
    xlator_t *this = THIS;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *origin_vol = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_conf_t *priv = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);
    GF_ASSERT(rsp_dict);
    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_int64(dict, "volcount", &volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to "
               "get the volume count");
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch snapname");
        goto out;
    }
    tmp_name = gf_strdup(snapname);
    if (!tmp_name) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out of memory");
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr(rsp_dict, "snapname", tmp_name);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set snapname in rsp_dict");
        GF_FREE(tmp_name);
        goto out;
    }
    tmp_name = NULL;

    snap = glusterd_create_snap_object(dict, rsp_dict);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "creating the"
               "snap object %s failed",
               snapname);
        ret = -1;
        goto out;
    }

    for (i = 1; i <= volcount; i++) {
        keylen = snprintf(key, sizeof(key), "volname%" PRId64, i);
        ret = dict_get_strn(dict, key, keylen, &volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "failed to get volume name");
            goto out;
        }

        ret = glusterd_volinfo_find(volname, &origin_vol);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
                   "failed to get the volinfo for "
                   "the volume %s",
                   volname);
            goto out;
        }

        if (is_origin_glusterd(dict)) {
            ret = glusterd_is_snap_soft_limit_reached(origin_vol, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAPSHOT_OP_FAILED,
                       "Failed to "
                       "check soft limit exceeded or not, "
                       "for volume %s ",
                       origin_vol->volname);
                goto out;
            }
        }

        snap_vol = glusterd_do_snap_vol(origin_vol, snap, dict, rsp_dict, i, 0);
        if (!snap_vol) {
            ret = -1;
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CREATION_FAIL,
                   "taking the "
                   "snapshot of the volume %s failed",
                   volname);
            goto out;
        }
    }
    ret = dict_set_int64(rsp_dict, "volcount", volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set volcount");
        goto out;
    }

    ret = glusterd_schedule_brick_snapshot(dict, rsp_dict, snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Failed to take backend "
               "snapshot %s",
               snap->snapname);
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(rsp_dict, "snapuuid",
                                     uuid_utoa(snap->snap_id));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snap "
               "uuid in response dictionary for %s snapshot",
               snap->snapname);
        goto out;
    }

    snap_activate = dict_get_str_boolean(
        priv->opts, GLUSTERD_STORE_KEY_SNAP_ACTIVATE, _gf_false);
    if (!snap_activate) {
        cds_list_for_each_entry(snap_vol, &snap->volumes, vol_list)
        {
            snap_vol->status = GLUSTERD_STATUS_STOPPED;
            ret = glusterd_store_volinfo(snap_vol,
                                         GLUSTERD_VOLINFO_VER_AC_INCREMENT);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_SET_FAIL,
                       "Failed to store snap volinfo %s", snap_vol->volname);
                goto out;
            }
        }

        goto out;
    }

    /* Activate created bricks in case of activate-on-create config. */
    ret = dict_get_int32n(dict, "flags", SLEN("flags"), &flags);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get flags");
        goto out;
    }

    cds_list_for_each_entry(snap_vol, &snap->volumes, vol_list)
    {
        ret = glusterd_start_volume(snap_vol, flags, _gf_true);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_ACTIVATE_FAIL,
                   "Failed to activate snap volume %s of the "
                   "snap %s",
                   snap_vol->volname, snap->snapname);
            goto out;
        }
    }

    ret = 0;

out:
    if (ret) {
        if (snap)
            glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_true, _gf_false);
        snap = NULL;
    }

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

int
snap_max_hard_limit_set_commit(dict_t *dict, uint64_t value, char *volname,
                               char **op_errstr)
{
    char err_str[PATH_MAX] = "";
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int ret = -1;
    xlator_t *this = THIS;
    char *next_version = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    conf = this->private;

    GF_ASSERT(conf);

    /* TODO: Initiate auto deletion when there is a limit change */
    if (!volname) {
        /* For system limit */
        ret = dict_set_uint64(conf->opts,
                              GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to store "
                   "%s in the options",
                   GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT);
            goto out;
        }

        ret = glusterd_get_next_global_opt_version_str(conf->opts,
                                                       &next_version);
        if (ret)
            goto out;

        ret = dict_set_strn(conf->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                            SLEN(GLUSTERD_GLOBAL_OPT_VERSION), next_version);
        if (ret)
            goto out;

        ret = glusterd_store_options(this, conf->opts);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STORE_FAIL,
                   "Failed to store "
                   "options");
            goto out;
        }
    } else {
        /*  For one volume */
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            snprintf(err_str, PATH_MAX,
                     "Failed to get the"
                     " volinfo for volume %s",
                     volname);
            goto out;
        }

        volinfo->snap_max_hard_limit = value;

        ret = glusterd_store_volinfo(volinfo,
                                     GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
            snprintf(err_str, PATH_MAX,
                     "Failed to store "
                     "snap-max-hard-limit for volume %s",
                     volname);
            goto out;
        }
    }

    ret = 0;
out:
    if (ret) {
        *op_errstr = gf_strdup(err_str);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAPSHOT_OP_FAILED, "%s",
               err_str);
    }
    return ret;
}

int
glusterd_snapshot_config_commit(dict_t *dict, char **op_errstr,
                                dict_t *rsp_dict)
{
    char *volname = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    int config_command = 0;
    uint64_t hard_limit = 0;
    uint64_t soft_limit = 0;
    char *next_version = NULL;
    char *auto_delete = NULL;
    char *snap_activate = NULL;
    gf_boolean_t system_conf = _gf_false;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    conf = this->private;

    GF_ASSERT(conf);

    ret = dict_get_int32n(dict, "config-command", SLEN("config-command"),
                          &config_command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "failed to get config-command type");
        goto out;
    }
    if (config_command != GF_SNAP_CONFIG_TYPE_SET) {
        ret = 0;
        goto out;
    }

    ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);

    /* config values snap-max-hard-limit and snap-max-soft-limit are
     * optional and hence we are not erroring out if values are not
     * present
     */
    gd_get_snap_conf_values_if_present(dict, &hard_limit, &soft_limit);

    if (hard_limit) {
        /* Commit ops for snap-max-hard-limit */
        ret = snap_max_hard_limit_set_commit(dict, hard_limit, volname,
                                             op_errstr);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HARD_LIMIT_SET_FAIL,
                   "snap-max-hard-limit set commit failed.");
            goto out;
        }
    }

    if (soft_limit) {
        /* For system limit */
        system_conf = _gf_true;
        ret = dict_set_uint64(
            conf->opts, GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT, soft_limit);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to save %s in the dictionary",
                   GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT);
            goto out;
        }
    }

    if (hard_limit || soft_limit) {
        ret = 0;
        goto done;
    }

    if (!dict_get_strn(dict, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE,
                       SLEN(GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE),
                       &auto_delete)) {
        system_conf = _gf_true;
        ret = dict_set_dynstr_with_alloc(
            conf->opts, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE, auto_delete);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not "
                   "save auto-delete value in conf->opts");
            goto out;
        }
    } else if (!dict_get_strn(dict, GLUSTERD_STORE_KEY_SNAP_ACTIVATE,
                              SLEN(GLUSTERD_STORE_KEY_SNAP_ACTIVATE),
                              &snap_activate)) {
        system_conf = _gf_true;
        ret = dict_set_dynstr_with_alloc(
            conf->opts, GLUSTERD_STORE_KEY_SNAP_ACTIVATE, snap_activate);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save "
                   "snap-activate-on-create value in conf->opts");
            goto out;
        }
    } else {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Invalid option");
        goto out;
    }

done:
    if (system_conf) {
        ret = glusterd_get_next_global_opt_version_str(conf->opts,
                                                       &next_version);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_GLOBAL_OP_VERSION_GET_FAIL,
                   "Failed to get next global opt-version");
            goto out;
        }

        ret = dict_set_strn(conf->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                            SLEN(GLUSTERD_GLOBAL_OPT_VERSION), next_version);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_GLOBAL_OP_VERSION_SET_FAIL,
                   "Failed to set next global opt-version");
            goto out;
        }

        ret = glusterd_store_options(this, conf->opts);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STORE_FAIL,
                   "Failed to store options");
            goto out;
        }
    }

out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_lvm_brick_details(dict_t *rsp_dict, glusterd_brickinfo_t *brickinfo,
                           char *volname, char *device, const char *key_prefix)
{
    int ret = -1;
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

    GF_ASSERT(rsp_dict);
    GF_ASSERT(brickinfo);
    GF_ASSERT(volname);
    priv = this->private;
    GF_ASSERT(priv);

    device = glusterd_get_brick_mount_device(brickinfo->path);
    if (!device) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
               "Getting device name for "
               "the brick %s:%s failed",
               brickinfo->hostname, brickinfo->path);
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
    runner_add_args(&runner, LVS, device, "--noheading", "-o",
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
        if (token != NULL) {
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
        }

        token = strtok(NULL, ":");
        if (token != NULL) {
            value = gf_strdup(token);
            if (!value) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
                        "token=%s", token, NULL);
                ret = -1;
                goto end;
            }
            ret = snprintf(key, sizeof(key), "%s.data", key_prefix);
            if (ret < 0) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL,
                        NULL);
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
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
                        "token=%s", token, NULL);
                ret = -1;
                goto end;
            }
            ret = snprintf(key, sizeof(key), "%s.lvsize", key_prefix);
            if (ret < 0) {
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL,
                        NULL);
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

    return ret;
}

static int
glusterd_get_single_brick_status(char **op_errstr, dict_t *rsp_dict,
                                 const char *keyprefix, int index,
                                 glusterd_volinfo_t *snap_volinfo,
                                 glusterd_brickinfo_t *brickinfo)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    char key[128] = ""; /* keyprefix is not longer than 64 bytes */
    int keylen;
    char *device = NULL;
    char *value = NULL;
    char brick_path[PATH_MAX] = "";
    char pidfile[PATH_MAX] = "";
    pid_t pid = -1;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(keyprefix);
    GF_ASSERT(snap_volinfo);
    GF_ASSERT(brickinfo);

    keylen = snprintf(key, sizeof(key), "%s.brick%d.path", keyprefix, index);
    if (keylen < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        ret = -1;
        goto out;
    }

    ret = snprintf(brick_path, sizeof(brick_path), "%s:%s", brickinfo->hostname,
                   brickinfo->path);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    value = gf_strdup(brick_path);
    if (!value) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
                "brick_path=%s", brick_path, NULL);
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstrn(rsp_dict, key, keylen, value);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to store "
               "brick_path %s",
               brickinfo->path);
        goto out;
    }

    if (brickinfo->snap_status == -1) {
        /* Setting vgname as "Pending Snapshot" */
        value = gf_strdup("Pending Snapshot");
        if (!value) {
            ret = -1;
            goto out;
        }

        keylen = snprintf(key, sizeof(key), "%s.brick%d.vgname", keyprefix,
                          index);
        ret = dict_set_dynstrn(rsp_dict, key, keylen, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save vgname ");
            goto out;
        }

        ret = 0;
        goto out;
    }
    value = NULL;

    keylen = snprintf(key, sizeof(key), "%s.brick%d.status", keyprefix, index);
    if (keylen < 0) {
        ret = -1;
        goto out;
    }

    if (brickinfo->status == GF_BRICK_STOPPED) {
        value = gf_strdup("No");
        if (!value) {
            ret = -1;
            goto out;
        }
        ret = dict_set_strn(rsp_dict, key, keylen, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save brick status");
            goto out;
        }
        value = NULL;
    } else {
        value = gf_strdup("Yes");
        if (!value) {
            ret = -1;
            goto out;
        }
        ret = dict_set_strn(rsp_dict, key, keylen, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save brick status");
            goto out;
        }
        value = NULL;

        GLUSTERD_GET_BRICK_PIDFILE(pidfile, snap_volinfo, brickinfo, priv);

        if (gf_is_service_running(pidfile, &pid)) {
            keylen = snprintf(key, sizeof(key), "%s.brick%d.pid", keyprefix,
                              index);
            if (keylen < 0) {
                ret = -1;
                gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL,
                        NULL);
                goto out;
            }

            ret = dict_set_int32n(rsp_dict, key, keylen, pid);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Could not save pid %d", pid);
                goto out;
            }
        }
    }

    keylen = snprintf(key, sizeof(key), "%s.brick%d", keyprefix, index);
    if (keylen < 0) {
        ret = -1;
        goto out;
    }
    /* While getting snap status we should show relevant information
     * for deactivated snaps.
     */
    if (snap_volinfo->status == GLUSTERD_STATUS_STOPPED) {
        /* Setting vgname as "Deactivated Snapshot" */
        value = gf_strdup("N/A (Deactivated Snapshot)");
        if (!value) {
            ret = -1;
            goto out;
        }

        keylen = snprintf(key, sizeof(key), "%s.brick%d.vgname", keyprefix,
                          index);
        ret = dict_set_dynstrn(rsp_dict, key, keylen, value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save vgname ");
            goto out;
        }

        ret = 0;
        goto out;
    }

    if (!glusterd_snapshot_probe(brickinfo->path, brickinfo)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
               "Snapshot brick details not supported on %s:%s",
               snap_volinfo->volname, brickinfo->path);
        ret = -1;
        goto out;
    }

    ret = brickinfo->snap->details(rsp_dict, brickinfo, snap_volinfo->volname,
                                   device, key);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_GET_INFO_FAIL,
               "Failed to get "
               "brick details");
        goto out;
    }
out:
    if (ret && value) {
        GF_FREE(value);
    }

    return ret;
}

static int
glusterd_get_single_snap_status(char **op_errstr, dict_t *rsp_dict,
                                const char *keyprefix, glusterd_snap_t *snap)
{
    int ret = -1;
    xlator_t *this = THIS;
    char key[64] = ""; /* keyprefix is "status.snap0" */
    int keylen;
    char brickkey[PATH_MAX] = "";
    glusterd_volinfo_t *snap_volinfo = NULL;
    glusterd_volinfo_t *tmp_volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    int volcount = 0;
    int brickcount = 0;

    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(keyprefix);
    GF_ASSERT(snap);

    cds_list_for_each_entry_safe(snap_volinfo, tmp_volinfo, &snap->volumes,
                                 vol_list)
    {
        keylen = snprintf(key, sizeof(key), "%s.vol%d", keyprefix, volcount);
        if (keylen < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
            ret = -1;
            goto out;
        }
        cds_list_for_each_entry(brickinfo, &snap_volinfo->bricks, brick_list)
        {
            if (!glusterd_is_local_brick(this, snap_volinfo, brickinfo)) {
                brickcount++;
                continue;
            }

            ret = glusterd_get_single_brick_status(
                op_errstr, rsp_dict, key, brickcount, snap_volinfo, brickinfo);

            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Getting "
                       "single snap status failed");
                goto out;
            }
            brickcount++;
        }
        keylen = snprintf(brickkey, sizeof(brickkey), "%s.brickcount", key);
        if (keylen < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
            goto out;
        }

        ret = dict_set_int32n(rsp_dict, brickkey, keylen, brickcount);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save brick count");
            goto out;
        }
        volcount++;
    }

    keylen = snprintf(key, sizeof(key), "%s.volcount", keyprefix);
    if (keylen < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        ret = -1;
        goto out;
    }

    ret = dict_set_int32n(rsp_dict, key, keylen, volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save volcount");
        goto out;
    }

out:

    return ret;
}

static int
glusterd_get_each_snap_object_status(char **op_errstr, dict_t *rsp_dict,
                                     glusterd_snap_t *snap,
                                     const char *keyprefix)
{
    int ret = -1;
    char key[32] = ""; /* keyprefix is "status.snap0" */
    int keylen;
    char *temp = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(snap);
    GF_ASSERT(keyprefix);

    /* TODO : Get all the snap volume info present in snap object,
     * as of now, There will be only one snapvolinfo per snap object
     */
    keylen = snprintf(key, sizeof(key), "%s.snapname", keyprefix);
    if (keylen < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        ret = -1;
        goto out;
    }

    temp = gf_strdup(snap->snapname);
    if (temp == NULL) {
        ret = -1;
        goto out;
    }
    ret = dict_set_dynstrn(rsp_dict, key, keylen, temp);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save "
               "snap name");
        goto out;
    }

    temp = NULL;

    keylen = snprintf(key, sizeof(key), "%s.uuid", keyprefix);
    if (keylen < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        ret = -1;
        goto out;
    }

    temp = gf_strdup(uuid_utoa(snap->snap_id));
    if (temp == NULL) {
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstrn(rsp_dict, key, keylen, temp);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save "
               "snap UUID");
        goto out;
    }

    temp = NULL;

    ret = glusterd_get_single_snap_status(op_errstr, rsp_dict, keyprefix, snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_STATUS_FAIL,
               "Could not get single snap status");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.volcount", keyprefix);
    if (keylen < 0) {
        ret = keylen;
        goto out;
    }

    ret = dict_set_int32n(rsp_dict, key, keylen, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save volcount");
        goto out;
    }
out:
    if (ret && temp)
        GF_FREE(temp);

    return ret;
}

int
glusterd_get_snap_status_of_volume(char **op_errstr, dict_t *rsp_dict,
                                   char *volname, char *keyprefix)
{
    int ret = -1;
    glusterd_volinfo_t *snap_volinfo = NULL;
    glusterd_volinfo_t *temp_volinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char key[64] = "";
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int i = 0;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);
    GF_ASSERT(volname);
    GF_ASSERT(keyprefix);

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
               "Failed to get volinfo of "
               "volume %s",
               volname);
        goto out;
    }

    cds_list_for_each_entry_safe(snap_volinfo, temp_volinfo,
                                 &volinfo->snap_volumes, snapvol_list)
    {
        ret = snprintf(key, sizeof(key), "status.snap%d.snapname", i);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
            goto out;
        }

        ret = dict_set_dynstr_with_alloc(rsp_dict, key,
                                         snap_volinfo->snapshot->snapname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save "
                   "snap name");
            goto out;
        }

        i++;
    }

    ret = dict_set_int32n(rsp_dict, "status.snapcount",
                          SLEN("status.snapcount"), i);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to save snapcount");
        ret = -1;
        goto out;
    }
out:
    return ret;
}

int
glusterd_get_all_snapshot_status(dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
    int32_t i = 0;
    int ret = -1;
    char key[64] = "";
    glusterd_conf_t *priv = NULL;
    glusterd_snap_t *snap = NULL;
    glusterd_snap_t *tmp_snap = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    GF_ASSERT(priv);

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    cds_list_for_each_entry_safe(snap, tmp_snap, &priv->snapshots, snap_list)
    {
        ret = snprintf(key, sizeof(key), "status.snap%d.snapname", i);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
            goto out;
        }

        ret = dict_set_dynstr_with_alloc(rsp_dict, key, snap->snapname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Could not save "
                   "snap name");
            goto out;
        }

        i++;
    }

    ret = dict_set_int32n(rsp_dict, "status.snapcount",
                          SLEN("status.snapcount"), i);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save snapcount");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_snapshot_status_commit(dict_t *dict, char **op_errstr,
                                dict_t *rsp_dict)
{
    xlator_t *this = THIS;
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    int32_t cmd = -1;
    char *snapname = NULL;
    glusterd_snap_t *snap = NULL;
    char *volname = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    conf = this->private;

    GF_ASSERT(conf);
    ret = dict_get_int32n(dict, "sub-cmd", SLEN("sub-cmd"), &cmd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get status cmd type");
        goto out;
    }

    ret = dict_set_int32n(rsp_dict, "sub-cmd", SLEN("sub-cmd"), cmd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Could not save status cmd in rsp dictionary");
        goto out;
    }
    switch (cmd) {
        case GF_SNAP_STATUS_TYPE_ALL: {
            ret = glusterd_get_all_snapshot_status(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Unable to "
                       "get snapshot status");
                goto out;
            }
            break;
        }
        case GF_SNAP_STATUS_TYPE_ITER:
        case GF_SNAP_STATUS_TYPE_SNAP: {
            ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to "
                       "get snap name");
                goto out;
            }

            snap = glusterd_find_snap_by_name(snapname);
            if (!snap) {
                ret = gf_asprintf(op_errstr,
                                  "Snapshot (%s) "
                                  "does not exist",
                                  snapname);
                if (ret < 0) {
                    goto out;
                }
                ret = -1;
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
                       "Unable to "
                       "get snap volinfo");
                goto out;
            }
            ret = glusterd_get_each_snap_object_status(op_errstr, rsp_dict,
                                                       snap, "status.snap0");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Unable to "
                       "get status of snap");
                goto out;
            }

            ret = dict_set_int32n(rsp_dict, "status.snapcount",
                                  SLEN("status.snapcount"), 1);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Unable to "
                       "set snapcount to 1");
                goto out;
            }
            break;
        }
        case GF_SNAP_STATUS_TYPE_VOL: {
            ret = dict_get_strn(dict, "volname", SLEN("volname"), &volname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to"
                       " get volume name");
                goto out;
            }

            ret = glusterd_get_snap_status_of_volume(op_errstr, rsp_dict,
                                                     volname, "status.vol0");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Function :"
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
glusterd_handle_snap_limit(dict_t *dict, dict_t *rsp_dict)
{
    int32_t ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    uint64_t effective_max_limit = 0;
    int64_t volcount = 0;
    int i = 0;
    char *volname = NULL;
    char key[64] = "";
    int keylen;
    char msg[PATH_MAX] = "";
    glusterd_volinfo_t *volinfo = NULL;
    uint64_t limit = 0;
    int64_t count = 0;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *tmp_volinfo = NULL;
    uint64_t opt_max_hard = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
    uint64_t opt_max_soft = GLUSTERD_SNAPS_DEF_SOFT_LIMIT_PERCENT;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_int64(dict, "volcount", &volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get the volcount");
        goto out;
    }

    for (i = 1; i <= volcount; i++) {
        keylen = snprintf(key, sizeof(key), "volname%d", i);
        ret = dict_get_strn(dict, key, keylen, &volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "failed to get the "
                   "volname");
            goto out;
        }

        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
                   "volinfo for %s "
                   "not found",
                   volname);
            goto out;
        }

        /* config values snap-max-hard-limit and snap-max-soft-limit are
         * optional and hence we are not erroring out if values are not
         * present
         */
        gd_get_snap_conf_values_if_present(priv->opts, &opt_max_hard,
                                           &opt_max_soft);

        /* The minimum of the 2 limits i.e system wide limit and
           volume wide limit will be considered
        */
        if (volinfo->snap_max_hard_limit < opt_max_hard)
            effective_max_limit = volinfo->snap_max_hard_limit;
        else
            effective_max_limit = opt_max_hard;

        limit = (opt_max_soft * effective_max_limit) / 100;

        count = volinfo->snap_count - limit;
        if (count <= 0)
            goto out;

        tmp_volinfo = cds_list_entry(volinfo->snap_volumes.next,
                                     glusterd_volinfo_t, snapvol_list);
        snap = tmp_volinfo->snapshot;
        GF_ASSERT(snap);

        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SOFT_LIMIT_REACHED,
               "Soft-limit "
               "(value = %" PRIu64
               ") of volume %s is reached. "
               "Deleting snapshot %s.",
               limit, volinfo->volname, snap->snapname);

        snprintf(msg, sizeof(msg),
                 "snapshot_name=%s;"
                 "snapshot_uuid=%s",
                 snap->snapname, uuid_utoa(snap->snap_id));

        LOCK(&snap->lock);
        {
            snap->snap_status = GD_SNAP_STATUS_DECOMMISSION;
            ret = glusterd_store_snap(snap);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_SNAP_OBJECT_STORE_FAIL,
                       "could "
                       "not store snap object %s",
                       snap->snapname);
                goto unlock;
            }

            ret = glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_true,
                                       _gf_false);
            if (ret)
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "failed to remove snap %s", snap->snapname);
        }
    unlock:
        UNLOCK(&snap->lock);
        if (is_origin_glusterd(dict) == _gf_true) {
            if (ret)
                gf_event(EVENT_SNAPSHOT_DELETE_FAILED, "%s", msg);
            else
                gf_event(EVENT_SNAPSHOT_DELETED, "%s", msg);
        }
    }

out:
    return ret;
}

int32_t
glusterd_snapshot_clone_postvalidate(dict_t *dict, int32_t op_ret,
                                     char **op_errstr, dict_t *rsp_dict)
{
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int ret = -1;
    int32_t cleanup = 0;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    char *clonename = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_strn(dict, "clonename", SLEN("clonename"), &clonename);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch "
               "clonename");
        goto out;
    }

    ret = glusterd_volinfo_find(clonename, &snap_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "unable to find clone "
               "%s volinfo",
               clonename);
        goto out;
    }

    if (snap_vol)
        snap = snap_vol->snapshot;
    else {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot volume is null");
        goto out;
    }

    /* Fetch snap object from snap_vol and delete it all in case of *
     * a failure, or else, just delete the snap object as it is not *
     * needed in case of a clone                                    *
     */
    if (op_ret) {
        ret = dict_get_int32n(dict, "cleanup", SLEN("cleanup"), &cleanup);
        if (!ret && cleanup && snap) {
            glusterd_snap_remove(rsp_dict, snap, _gf_true, _gf_true, _gf_true);
        }
        /* Irrespective of status of cleanup its better
         * to return from this function. As the functions
         * following this block is not required to be
         * executed in case of failure scenario.
         */
        ret = 0;
        goto out;
    }

    ret = glusterd_snapobject_delete(snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "Failed to delete "
               "snap object %s",
               snap->snapname);
        goto out;
    }
    snap_vol->snapshot = NULL;

out:
    return ret;
}

int32_t
glusterd_snapshot_create_postvalidate(dict_t *dict, int32_t op_ret,
                                      char **op_errstr, dict_t *rsp_dict)
{
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int ret = -1;
    int32_t cleanup = 0;
    glusterd_snap_t *snap = NULL;
    char *snapname = NULL;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    uint64_t opt_hard_max = GLUSTERD_SNAPS_MAX_HARD_LIMIT;
    uint64_t opt_max_soft = GLUSTERD_SNAPS_DEF_SOFT_LIMIT_PERCENT;
    int64_t effective_max_limit = 0;
    int64_t soft_limit = 0;
    int32_t snap_activate = _gf_false;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    priv = this->private;
    GF_ASSERT(priv);

    if (op_ret) {
        ret = dict_get_int32n(dict, "cleanup", SLEN("cleanup"), &cleanup);
        if (!ret && cleanup) {
            ret = glusterd_do_snap_cleanup(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CLEANUP_FAIL,
                       "cleanup "
                       "operation failed");
                goto out;
            }
        }
        /* Irrespective of status of cleanup its better
         * to return from this function. As the functions
         * following this block is not required to be
         * executed in case of failure scenario.
         */
        ret = 0;
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &snapname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch "
               "snapname");
        goto out;
    }

    snap = glusterd_find_snap_by_name(snapname);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND,
               "unable to find snap "
               "%s",
               snapname);
        goto out;
    }

    snap->snap_status = GD_SNAP_STATUS_IN_USE;
    ret = glusterd_store_snap(snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_OBJECT_STORE_FAIL,
               "Could not store snap"
               "object %s",
               snap->snapname);
        goto out;
    }

    ret = glusterd_snapshot_update_snaps_post_validate(dict, op_errstr,
                                                       rsp_dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
               "Failed to "
               "create snapshot");
        goto out;
    }

    /*
     * If activate_on_create was enabled, and we have reached this  *
     * section of the code, that means, that after successfully     *
     * creating the snapshot, we have also successfully started the *
     * snapshot bricks on all nodes. So from originator node we can *
     * send EVENT_SNAPSHOT_ACTIVATED event.                         *
     *                                                              *
     * Also check, if hard limit and soft limit is reached in case  *
     * of successfully creating the snapshot, and generate the event *
     */
    if (is_origin_glusterd(dict) == _gf_true) {
        snap_activate = dict_get_str_boolean(
            priv->opts, GLUSTERD_STORE_KEY_SNAP_ACTIVATE, _gf_false);

        if (snap_activate == _gf_true) {
            gf_event(EVENT_SNAPSHOT_ACTIVATED,
                     "snapshot_name=%s;"
                     "snapshot_uuid=%s",
                     snap->snapname, uuid_utoa(snap->snap_id));
        }

        ret = dict_get_strn(dict, "volname1", SLEN("volname1"), &volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get volname.");
            goto out;
        }

        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                   "Failed to get volinfo.");
            goto out;
        }

        /* config values snap-max-hard-limit and snap-max-soft-limit are
         * optional and hence we are not erroring out if values are not
         * present
         */
        gd_get_snap_conf_values_if_present(priv->opts, &opt_hard_max,
                                           &opt_max_soft);

        if (volinfo->snap_max_hard_limit < opt_hard_max)
            effective_max_limit = volinfo->snap_max_hard_limit;
        else
            effective_max_limit = opt_hard_max;

        /*
         * Check for hard limit. If it is reached after taking *
         * this snapshot, then generate event for the same. If *
         * it is not reached, then check for the soft limit,   *
         * and generate event accordingly.                     *
         */
        if (volinfo->snap_count >= effective_max_limit) {
            gf_event(EVENT_SNAPSHOT_HARD_LIMIT_REACHED,
                     "volume_name=%s;volume_id=%s", volname,
                     uuid_utoa(volinfo->volume_id));
        } else {
            soft_limit = (opt_max_soft * effective_max_limit) / 100;
            if (volinfo->snap_count >= soft_limit) {
                gf_event(EVENT_SNAPSHOT_SOFT_LIMIT_REACHED,
                         "volume_name=%s;volume_id=%s", volname,
                         uuid_utoa(volinfo->volume_id));
            }
        }
    }

    /* "auto-delete" might not be set by user explicitly,
     * in that case it's better to consider the default value.
     * Hence not erroring out if Key is not found.
     */
    ret = dict_get_str_boolean(priv->opts, GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE,
                               _gf_false);
    if (_gf_true == ret) {
        ret = glusterd_handle_snap_limit(dict, rsp_dict);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                   "failed to remove snap");
            /* ignore the errors of autodelete */
            ret = 0;
        }
    }

out:
    return ret;
}

int32_t
glusterd_snapshot(dict_t *dict, char **op_errstr, uint32_t *op_errno,
                  dict_t *rsp_dict)
{
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int32_t snap_command = 0;
    char *snap_name = NULL;
    char temp[PATH_MAX] = "";
    int ret = -1;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_int32n(dict, "type", SLEN("type"), &snap_command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "unable to get the type of "
               "the snapshot command");
        goto out;
    }

    switch (snap_command) {
        case (GF_SNAP_OPTION_TYPE_CREATE):
            ret = glusterd_snapshot_create_commit(dict, op_errstr, op_errno,
                                                  rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CREATION_FAIL,
                       "Failed to "
                       "create snapshot");
                goto out;
            }
            break;

        case (GF_SNAP_OPTION_TYPE_CLONE):
            ret = glusterd_snapshot_clone_commit(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CLONE_FAILED,
                       "Failed to "
                       "clone snapshot");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_CONFIG:
            ret = glusterd_snapshot_config_commit(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CONFIG_FAIL,
                       "snapshot config failed");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_DELETE:
            ret = glusterd_snapshot_remove_commit(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Failed to "
                       "delete snapshot");
                if (*op_errstr) {
                    /* If error string is already set
                     * then goto out */
                    goto out;
                }

                ret = dict_get_strn(dict, "snapname", SLEN("snapname"),
                                    &snap_name);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                           "Failed to get snapname");
                    snap_name = "NA";
                }

                snprintf(temp, sizeof(temp),
                         "Snapshot %s might "
                         "not be in an usable state.",
                         snap_name);

                *op_errstr = gf_strdup(temp);
                ret = -1;
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
            ret = glusterd_snapshot_restore(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_RESTORE_FAIL,
                       "Failed to "
                       "restore snapshot");
                goto out;
            }

            break;
        case GF_SNAP_OPTION_TYPE_ACTIVATE:
            ret = glusterd_snapshot_activate_commit(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_ACTIVATE_FAIL,
                       "Failed to "
                       "activate snapshot");
                goto out;
            }

            break;

        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
            ret = glusterd_snapshot_deactivate_commit(dict, op_errstr,
                                                      rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_SNAP_DEACTIVATE_FAIL,
                       "Failed to "
                       "deactivate snapshot");
                goto out;
            }

            break;

        case GF_SNAP_OPTION_TYPE_STATUS:
            ret = glusterd_snapshot_status_commit(dict, op_errstr, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Failed to "
                       "show snapshot status");
                goto out;
            }
            break;

        default:
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, GD_MSG_INVALID_ENTRY,
                   "invalid snap command");
            goto out;
            break;
    }

    ret = 0;

out:
    return ret;
}

int
glusterd_snapshot_brickop(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int ret = -1;
    int64_t vol_count = 0;
    int64_t count = 1;
    char key[64] = "";
    int keylen;
    char *volname = NULL;
    int32_t snap_command = 0;
    xlator_t *this = THIS;
    char *op_type = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    ret = dict_get_int32n(dict, "type", SLEN("type"), &snap_command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "unable to get the type of "
               "the snapshot command");
        goto out;
    }

    switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:

            /* op_type with tell us whether its pre-commit operation
             * or post-commit
             */
            ret = dict_get_strn(dict, "operation-type", SLEN("operation-type"),
                                &op_type);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to fetch "
                       "operation type");
                goto out;
            }

            if (strcmp(op_type, "pre") == 0) {
                /* BRICK OP PHASE for enabling barrier, Enable barrier
                 * if its a pre-commit operation
                 */
                ret = glusterd_set_barrier_value(dict, "enable");
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                           "Failed to "
                           "set barrier value as enable in dict");
                    goto out;
                }
            } else if (strcmp(op_type, "post") == 0) {
                /* BRICK OP PHASE for disabling barrier, Disable barrier
                 * if its a post-commit operation
                 */
                ret = glusterd_set_barrier_value(dict, "disable");
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                           "Failed to "
                           "set barrier value as disable in "
                           "dict");
                    goto out;
                }
            } else {
                ret = -1;
                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                       "Invalid op_type");
                goto out;
            }

            ret = dict_get_int64(dict, "volcount", &vol_count);
            if (ret)
                goto out;
            while (count <= vol_count) {
                keylen = snprintf(key, sizeof(key), "volname%" PRId64, count);
                ret = dict_get_strn(dict, key, keylen, &volname);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                           "Unable to get volname");
                    goto out;
                }
                ret = dict_set_strn(dict, "volname", SLEN("volname"), volname);
                if (ret)
                    goto out;

                ret = gd_brick_op_phase(GD_OP_SNAP, NULL, dict, op_errstr);
                if (ret)
                    goto out;
                volname = NULL;
                count++;
            }

            dict_deln(dict, "volname", SLEN("volname"));
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
glusterd_snapshot_prevalidate(dict_t *dict, char **op_errstr, dict_t *rsp_dict,
                              uint32_t *op_errno)
{
    int snap_command = 0;
    xlator_t *this = THIS;
    int ret = -1;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);

    ret = dict_get_int32n(dict, "type", SLEN("type"), &snap_command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "unable to get the type of "
               "the snapshot command");
        goto out;
    }

    switch (snap_command) {
        case (GF_SNAP_OPTION_TYPE_CREATE):
            ret = glusterd_snapshot_create_prevalidate(dict, op_errstr,
                                                       rsp_dict, op_errno);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CREATION_FAIL,
                       "Snapshot create "
                       "pre-validation failed");
                goto out;
            }
            break;

        case (GF_SNAP_OPTION_TYPE_CLONE):
            ret = glusterd_snapshot_clone_prevalidate(dict, op_errstr, rsp_dict,
                                                      op_errno);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_SNAP_CLONE_PREVAL_FAILED,
                       "Snapshot clone "
                       "pre-validation failed");
                goto out;
            }
            break;

        case (GF_SNAP_OPTION_TYPE_CONFIG):
            ret = glusterd_snapshot_config_prevalidate(dict, op_errstr,
                                                       op_errno);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CONFIG_FAIL,
                       "Snapshot config "
                       "pre-validation failed");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
            ret = glusterd_snapshot_restore_prevalidate(dict, op_errstr,
                                                        op_errno, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_RESTORE_FAIL,
                       "Snapshot restore "
                       "validation failed");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_ACTIVATE:
            ret = glusterd_snapshot_activate_deactivate_prevalidate(
                dict, op_errstr, op_errno, rsp_dict, _gf_true);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_ACTIVATE_FAIL,
                       "Snapshot activate "
                       "validation failed");
                goto out;
            }
            break;
        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
            ret = glusterd_snapshot_activate_deactivate_prevalidate(
                dict, op_errstr, op_errno, rsp_dict, _gf_false);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_SNAP_DEACTIVATE_FAIL,
                       "Snapshot deactivate validation failed");
                goto out;
            }
            break;
        case GF_SNAP_OPTION_TYPE_DELETE:
            ret = glusterd_snapshot_remove_prevalidate(dict, op_errstr,
                                                       op_errno, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Snapshot remove "
                       "validation failed");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_STATUS:
            ret = glusterd_snapshot_status_prevalidate(dict, op_errstr,
                                                       op_errno, rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Snapshot status "
                       "validation failed");
                goto out;
            }
            break;

        default:
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, GD_MSG_COMMAND_NOT_FOUND,
                   "invalid snap command");
            *op_errno = EINVAL;
            goto out;
    }

    ret = 0;
out:
    return ret;
}

/* This function is called to remove the trashpath, in cases
 * when the restore operation is successful and we don't need
 * the backup, and incases when the restore op is failed before
 * commit, and we don't need to revert the backup.
 *
 * @param volname  name of the volume which is being restored
 *
 * @return 0 on success or -1 on failure
 */
int
glusterd_remove_trashpath(char *volname)
{
    int ret = -1;
    char delete_path[PATH_MAX] = {
        0,
    };
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    struct stat stbuf = {
        0,
    };
    int32_t len = 0;

    priv = this->private;

    GF_ASSERT(volname);

    len = snprintf(delete_path, sizeof(delete_path),
                   "%s/" GLUSTERD_TRASH "/vols-%s.deleted", priv->workdir,
                   volname);
    if ((len < 0) || (len >= sizeof(delete_path))) {
        goto out;
    }

    ret = sys_lstat(delete_path, &stbuf);
    if (ret) {
        /* If the trash dir does not exist, return *
         * without failure                         *
         */
        if (errno == ENOENT) {
            ret = 0;
            goto out;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                   "Failed to lstat "
                   "backup dir (%s)",
                   delete_path);
            goto out;
        }
    }

    /* Delete the backup copy of volume folder */
    ret = recursive_rmdir(delete_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to remove "
               "backup dir (%s)",
               delete_path);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

/* This function is called if snapshot restore operation
 * is successful. It will cleanup the backup files created
 * during the restore operation.
 *
 * @param rsp_dict Response dictionary
 * @param volinfo  volinfo of the volume which is being restored
 * @param snap     snap object
 *
 * @return 0 on success or -1 on failure
 */
int
glusterd_snapshot_restore_cleanup(dict_t *rsp_dict, char *volname,
                                  glusterd_snap_t *snap)
{
    int ret = -1;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(volname);
    GF_ASSERT(snap);

    /* Now delete the snap entry. */
    ret = glusterd_snap_remove(rsp_dict, snap, _gf_false, _gf_true, _gf_false);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
               "Failed to delete "
               "snap %s",
               snap->snapname);
        goto out;
    }

    /* Delete the backup copy of volume folder */
    ret = glusterd_remove_trashpath(volname);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to remove "
               "backup dir");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

/* This function is called when the snapshot restore operation failed
 * for some reasons. In such case we revert the restore operation.
 *
 * @param volinfo               volinfo of the origin volume
 *
 * @return 0 on success and -1 on failure
 */
int
glusterd_snapshot_revert_partial_restored_vol(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    char pathname[PATH_MAX] = "";
    char trash_path[PATH_MAX] = "";
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_volinfo_t *reverted_vol = NULL;
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *tmp_vol = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    int32_t len = 0;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(volinfo);

    GLUSTERD_GET_VOLUME_DIR(pathname, volinfo, priv);

    len = snprintf(trash_path, sizeof(trash_path),
                   "%s/" GLUSTERD_TRASH "/vols-%s.deleted", priv->workdir,
                   volinfo->volname);
    if ((len < 0) || (len >= sizeof(trash_path))) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        ret = -1;
        goto out;
    }

    /* Since snapshot restore failed we cannot rely on the volume
     * data stored under vols folder. Therefore delete the origin
     * volume's backend folder.*/
    ret = recursive_rmdir(pathname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to remove "
               "%s directory",
               pathname);
        goto out;
    }

    /* Now move the backup copy of the vols to its original
     * location.*/
    ret = sys_rename(trash_path, pathname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "Failed to rename folder "
               "from %s to %s",
               trash_path, pathname);
        goto out;
    }

    /* Retrieve the volume from the store */
    reverted_vol = glusterd_store_retrieve_volume(volinfo->volname, NULL);
    if (NULL == reverted_vol) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
               "Failed to load restored "
               "%s volume",
               volinfo->volname);
        goto out;
    }

    /* Retrieve the snap_volumes list from the older volinfo */
    reverted_vol->snap_count = volinfo->snap_count;
    cds_list_for_each_entry_safe(snap_vol, tmp_vol, &volinfo->snap_volumes,
                                 snapvol_list)
    {
        cds_list_add_tail(&snap_vol->snapvol_list, &reverted_vol->snap_volumes);

        cds_list_for_each_entry(brickinfo, &snap_vol->bricks, brick_list)
        {
            /*
             * If the brick is not of this peer, or snapshot is    *
             * missed for the brick don't restore the xattr for it *
             */
            if ((!gf_uuid_compare(brickinfo->uuid, MY_UUID)) &&
                (brickinfo->snap_status != -1)) {
                /*
                 * We need to restore volume id of all snap *
                 * bricks to volume id of the snap volume.  *
                 */
                ret = sys_lsetxattr(brickinfo->path, GF_XATTR_VOL_ID_KEY,
                                    snap_vol->volume_id,
                                    sizeof(snap_vol->volume_id), XATTR_REPLACE);
                if (ret == -1) {
                    gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_SET_XATTR_FAIL,
                            "Attribute=%s, Path=%s, Reason=%s, Snap=%s",
                            GF_XATTR_VOL_ID_KEY, brickinfo->path,
                            strerror(errno), snap_vol->volname, NULL);
                    goto out;
                }
            }
        }
    }

    /* Since we retrieved the volinfo from store now we don't
     * want the older volinfo. Therefore delete the older volinfo */
    glusterd_volinfo_unref(volinfo);
    ret = 0;
out:
    return ret;
}

/* This function is called when glusterd is started and we need
 * to revert a failed snapshot restore.
 *
 * @param snap snapshot object of the restored snap
 *
 * @return 0 on success and -1 on failure
 */
int
glusterd_snapshot_revert_restore_from_snap(glusterd_snap_t *snap)
{
    int ret = -1;
    char volname[PATH_MAX] = "";
    glusterd_volinfo_t *snap_volinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    GF_ASSERT(snap);

    /* TODO : As of now there is only one volume in snapshot.
     * Change this when multiple volume snapshot is introduced
     */
    snap_volinfo = cds_list_entry(snap->volumes.next, glusterd_volinfo_t,
                                  vol_list);

    gf_strncpy(volname, snap_volinfo->parent_volname, sizeof(volname));

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
               "Could not get volinfo of "
               "%s",
               snap_volinfo->parent_volname);
        goto out;
    }

    ret = glusterd_snapshot_revert_partial_restored_vol(volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_REVERT_FAIL,
               "Failed to revert snapshot "
               "restore operation for %s volume",
               volname);
        goto out;
    }
out:
    return ret;
}

/* This function is called from post-validation. Based on the op_ret
 * it will take a decision on whether to revert the operation or
 * perform cleanup.
 *
 * @param dict          dictionary object
 * @param op_ret        return value of the restore operation
 * @param op_errstr     error string
 * @param rsp_dict      Response dictionary
 *
 * @return 0 on success and -1 on failure
 */
int
glusterd_snapshot_restore_postop(dict_t *dict, int32_t op_ret, char **op_errstr,
                                 dict_t *rsp_dict)
{
    int ret = -1;
    char *name = NULL;
    char *volname = NULL;
    int cleanup = 0;
    glusterd_snap_t *snap = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &name);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "getting the snap "
               "name failed (volume: %s)",
               name);
        goto out;
    }

    snap = glusterd_find_snap_by_name(name);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_NOT_FOUND,
               "Snapshot (%s) does not exist", name);
        ret = -1;
        goto out;
    }

    /* TODO: fix this when multiple volume support will come */
    ret = dict_get_strn(dict, "volname1", SLEN("volname1"), &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Volume (%s) does not exist ", volname);
        goto out;
    }

    ret = dict_get_strn(dict, "snapname", SLEN("snapname"), &name);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "getting the snap "
               "name failed (volume: %s)",
               volinfo->volname);
        goto out;
    }

    snap = glusterd_find_snap_by_name(name);
    if (!snap) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_SNAP_NOT_FOUND,
               "snap %s is not found", name);
        ret = -1;
        goto out;
    }

    /* On success perform the cleanup operation */
    if (0 == op_ret) {
        ret = glusterd_snapshot_restore_cleanup(rsp_dict, volname, snap);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_CLEANUP_FAIL,
                   "Failed to perform "
                   "snapshot restore cleanup for %s volume",
                   volname);
            goto out;
        }
    } else { /* On failure revert snapshot restore */
        ret = dict_get_int32n(dict, "cleanup", SLEN("cleanup"), &cleanup);
        /* Perform cleanup only when required */
        if (ret || (0 == cleanup)) {
            /* Delete the backup copy of volume folder */
            ret = glusterd_remove_trashpath(volinfo->volname);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                       "Failed to remove backup dir");
                goto out;
            }
            ret = 0;
            goto out;
        }

        ret = glusterd_snapshot_revert_partial_restored_vol(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_REVERT_FAIL,
                   "Failed to revert "
                   "restore operation for %s volume",
                   volname);
            goto out;
        }

        snap->snap_status = GD_SNAP_STATUS_IN_USE;
        /* We need to save this in disk */
        ret = glusterd_store_snap(snap);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_OBJECT_STORE_FAIL,
                   "Could not store snap object for %s snap", snap->snapname);
            goto out;
        }

        /* After restore fails, we have to remove mount point for
         * deactivated snaps which was created at start of restore op.
         */
        if (volinfo->status == GLUSTERD_STATUS_STOPPED) {
            ret = glusterd_snap_unmount(this, volinfo);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_UMOUNT_FAIL,
                       "Failed to unmounts for %s", snap->snapname);
            }
        }
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_snapshot_postvalidate(dict_t *dict, int32_t op_ret, char **op_errstr,
                               dict_t *rsp_dict)
{
    int snap_command = 0;
    xlator_t *this = THIS;
    int ret = -1;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);

    ret = dict_get_int32n(dict, "type", SLEN("type"), &snap_command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
               "unable to get the type of "
               "the snapshot command");
        goto out;
    }

    switch (snap_command) {
        case GF_SNAP_OPTION_TYPE_CREATE:
            ret = glusterd_snapshot_create_postvalidate(dict, op_ret, op_errstr,
                                                        rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CREATION_FAIL,
                       "Snapshot create "
                       "post-validation failed");
                goto out;
            }
            glusterd_fetchsnap_notify(this);
            break;
        case GF_SNAP_OPTION_TYPE_CLONE:
            ret = glusterd_snapshot_clone_postvalidate(dict, op_ret, op_errstr,
                                                       rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_SNAP_CLONE_POSTVAL_FAILED,
                       "Snapshot create "
                       "post-validation failed");
                goto out;
            }
            glusterd_fetchsnap_notify(this);
            break;
        case GF_SNAP_OPTION_TYPE_DELETE:
            if (op_ret) {
                gf_msg_debug(this->name, 0,
                             "op_ret = %d. Not performing delete "
                             "post_validate",
                             op_ret);
                ret = 0;
                goto out;
            }
            ret = glusterd_snapshot_update_snaps_post_validate(dict, op_errstr,
                                                               rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                       "Failed to "
                       "update missed snaps list");
                goto out;
            }
            glusterd_fetchsnap_notify(this);
            break;
        case GF_SNAP_OPTION_TYPE_RESTORE:
            ret = glusterd_snapshot_update_snaps_post_validate(dict, op_errstr,
                                                               rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
                       "Failed to "
                       "update missed snaps list");
                goto out;
            }

            ret = glusterd_snapshot_restore_postop(dict, op_ret, op_errstr,
                                                   rsp_dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
                       "Failed to "
                       "perform snapshot restore post-op");
                goto out;
            }
            glusterd_fetchsnap_notify(this);
            break;
        case GF_SNAP_OPTION_TYPE_ACTIVATE:
        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
            glusterd_fetchsnap_notify(this);
            break;
        case GF_SNAP_OPTION_TYPE_STATUS:
        case GF_SNAP_OPTION_TYPE_CONFIG:
        case GF_SNAP_OPTION_TYPE_INFO:
        case GF_SNAP_OPTION_TYPE_LIST:
            /*Nothing to be done. But want to
             * avoid the default case warning*/
            ret = 0;
            break;
        default:
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_COMMAND_NOT_FOUND,
                   "invalid snap command");
            goto out;
    }

    ret = 0;
out:
    return ret;
}

/*
  Verify availability of a command
*/

gf_boolean_t
glusterd_is_cmd_available(char *cmd)
{
    int32_t ret = 0;
    struct stat buf = {
        0,
    };

    if (!cmd)
        return _gf_false;

    ret = sys_stat(cmd, &buf);
    if (ret != 0) {
        gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "stat fails on %s, exiting. (errno = %d (%s))", cmd, errno,
               strerror(errno));
        return _gf_false;
    }

    if ((!ret) && (!S_ISREG(buf.st_mode))) {
        gf_msg(THIS->name, GF_LOG_CRITICAL, EINVAL, GD_MSG_COMMAND_NOT_FOUND,
               "Provided command %s is not a regular file,"
               "exiting",
               cmd);
        return _gf_false;
    }

    if ((!ret) && (!(buf.st_mode & S_IXUSR))) {
        gf_msg(THIS->name, GF_LOG_CRITICAL, 0, GD_MSG_NO_EXEC_PERMS,
               "Provided command %s has no exec permissions,"
               "exiting",
               cmd);
        return _gf_false;
    }

    return _gf_true;
}

/* This function will check whether the given device
 * is a thinly provisioned LV or not.
 *
 * @param device        LV device path
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
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND,
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
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_TPOOL_GET_FAIL,
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

int
glusterd_handle_snapshot_fn(rpcsvc_request_t *req)
{
    int32_t ret = 0;
    dict_t *dict = NULL;
    gf_cli_req cli_req = {
        {0},
    };
    glusterd_op_t cli_op = GD_OP_SNAP;
    int type = 0;
    glusterd_conf_t *conf = NULL;
    char *host_uuid = NULL;
    char err_str[2048] = "";
    xlator_t *this = THIS;
    uint32_t op_errno = 0;

    GF_ASSERT(req);

    conf = this->private;
    GF_ASSERT(conf);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto out;
    }

    if (cli_req.dict.dict_len > 0) {
        dict = dict_new();
        if (!dict)
            goto out;

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        }

        dict->extra_stdfree = cli_req.dict.dict_val;

        host_uuid = gf_strdup(uuid_utoa(MY_UUID));
        if (host_uuid == NULL) {
            snprintf(err_str, sizeof(err_str),
                     "Failed to get "
                     "the uuid of local glusterd");
            ret = -1;
            goto out;
        }
        ret = dict_set_dynstrn(dict, "host-uuid", SLEN("host-uuid"), host_uuid);
        if (ret) {
            GF_FREE(host_uuid);
            goto out;
        }

    } else {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "request dict length is %d", cli_req.dict.dict_len);
        goto out;
    }

    if (conf->op_version < GD_OP_VERSION_3_6_0) {
        snprintf(err_str, sizeof(err_str),
                 "Cluster operating version"
                 " is lesser than the supported version "
                 "for a snapshot");
        op_errno = EG_OPNOTSUP;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
               "%s (%d < %d)", err_str, conf->op_version, GD_OP_VERSION_3_6_0);
        ret = -1;
        goto out;
    }

    ret = dict_get_int32n(dict, "type", SLEN("type"), &type);
    if (ret < 0) {
        snprintf(err_str, sizeof(err_str), "Command type not found");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMAND_NOT_FOUND, "%s",
               err_str);
        goto out;
    }

    switch (type) {
        case GF_SNAP_OPTION_TYPE_CREATE:
            ret = glusterd_handle_snapshot_create(req, cli_op, dict, err_str,
                                                  sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CREATION_FAIL,
                       "Snapshot create failed: %s", err_str);
            }
            break;

        case GF_SNAP_OPTION_TYPE_CLONE:
            ret = glusterd_handle_snapshot_clone(req, cli_op, dict, err_str,
                                                 sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CLONE_FAILED,
                       "Snapshot clone "
                       "failed: %s",
                       err_str);
            }
            break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
            ret = glusterd_handle_snapshot_restore(req, cli_op, dict, err_str,
                                                   &op_errno, sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_RESTORE_FAIL,
                       "Snapshot restore failed: %s", err_str);
            }

            break;
        case GF_SNAP_OPTION_TYPE_INFO:
            ret = glusterd_handle_snapshot_info(req, cli_op, dict, err_str,
                                                sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_INFO_FAIL,
                       "Snapshot info failed");
            }
            break;
        case GF_SNAP_OPTION_TYPE_LIST:
            ret = glusterd_handle_snapshot_list(req, cli_op, dict, err_str,
                                                sizeof(err_str), &op_errno);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_LIST_GET_FAIL,
                       "Snapshot list failed");
            }
            break;
        case GF_SNAP_OPTION_TYPE_CONFIG:
            ret = glusterd_handle_snapshot_config(req, cli_op, dict, err_str,
                                                  sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_CONFIG_FAIL,
                       "snapshot config failed");
            }
            break;
        case GF_SNAP_OPTION_TYPE_DELETE:
            ret = glusterd_handle_snapshot_delete(req, cli_op, dict, err_str,
                                                  &op_errno, sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Snapshot delete failed: %s", err_str);
            }
            break;
        case GF_SNAP_OPTION_TYPE_ACTIVATE:
            ret = glusterd_mgmt_v3_initiate_snap_phases(req, cli_op, dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_ACTIVATE_FAIL,
                       "Snapshot activate failed: %s", err_str);
            }
            break;
        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
            ret = glusterd_mgmt_v3_initiate_snap_phases(req, cli_op, dict);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0,
                       GD_MSG_SNAP_DEACTIVATE_FAIL,
                       "Snapshot deactivate failed: %s", err_str);
            }
            break;
        case GF_SNAP_OPTION_TYPE_STATUS:
            ret = glusterd_handle_snapshot_status(req, cli_op, dict, err_str,
                                                  sizeof(err_str));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_STATUS_FAIL,
                       "Snapshot status failed: %s", err_str);
            }
            break;
        default:
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_COMMAND_NOT_FOUND,
                   "Unknown snapshot request "
                   "type (%d)",
                   type);
            ret = -1; /* Failure */
    }

out:
    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");

        if (ret && (op_errno == 0))
            op_errno = EG_INTRNL;

        ret = glusterd_op_send_cli_response(cli_op, ret, op_errno, req, dict,
                                            err_str);
    }

    return ret;
}

int
glusterd_handle_snapshot(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, glusterd_handle_snapshot_fn);
}

static void
glusterd_free_snap_op(glusterd_snap_op_t *snap_op)
{
    if (snap_op) {
        if (snap_op->brick_path)
            GF_FREE(snap_op->brick_path);

        GF_FREE(snap_op);
    }
}

static void
glusterd_free_missed_snapinfo(glusterd_missed_snap_info *missed_snapinfo)
{
    glusterd_snap_op_t *snap_opinfo = NULL;
    glusterd_snap_op_t *tmp = NULL;

    if (missed_snapinfo) {
        cds_list_for_each_entry_safe(snap_opinfo, tmp,
                                     &missed_snapinfo->snap_ops, snap_ops_list)
        {
            glusterd_free_snap_op(snap_opinfo);
            snap_opinfo = NULL;
        }

        if (missed_snapinfo->node_uuid)
            GF_FREE(missed_snapinfo->node_uuid);

        if (missed_snapinfo->snap_uuid)
            GF_FREE(missed_snapinfo->snap_uuid);

        GF_FREE(missed_snapinfo);
    }
}

/* Look for duplicates and accordingly update the list */
int32_t
glusterd_update_missed_snap_entry(glusterd_missed_snap_info *missed_snapinfo,
                                  glusterd_snap_op_t *missed_snap_op)
{
    int32_t ret = -1;
    glusterd_snap_op_t *snap_opinfo = NULL;
    gf_boolean_t match = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(missed_snapinfo);
    GF_ASSERT(missed_snap_op);

    cds_list_for_each_entry(snap_opinfo, &missed_snapinfo->snap_ops,
                            snap_ops_list)
    {
        /* If the entry is not for the same snap_vol_id
         * then continue
         */
        if (strcmp(snap_opinfo->snap_vol_id, missed_snap_op->snap_vol_id))
            continue;

        if ((!strcmp(snap_opinfo->brick_path, missed_snap_op->brick_path)) &&
            (snap_opinfo->op == missed_snap_op->op)) {
            /* If two entries have conflicting status
             * GD_MISSED_SNAP_DONE takes precedence
             */
            if ((snap_opinfo->status == GD_MISSED_SNAP_PENDING) &&
                (missed_snap_op->status == GD_MISSED_SNAP_DONE)) {
                snap_opinfo->status = GD_MISSED_SNAP_DONE;
                gf_msg(this->name, GF_LOG_INFO, 0,
                       GD_MSG_MISSED_SNAP_STATUS_DONE,
                       "Updating missed snap status "
                       "for %s:%s=%s:%d:%s:%d as DONE",
                       missed_snapinfo->node_uuid, missed_snapinfo->snap_uuid,
                       snap_opinfo->snap_vol_id, snap_opinfo->brick_num,
                       snap_opinfo->brick_path, snap_opinfo->op);
                ret = 0;
                glusterd_free_snap_op(missed_snap_op);
                goto out;
            }
            match = _gf_true;
            break;
        } else if ((snap_opinfo->brick_num == missed_snap_op->brick_num) &&
                   (snap_opinfo->op == GF_SNAP_OPTION_TYPE_CREATE) &&
                   ((missed_snap_op->op == GF_SNAP_OPTION_TYPE_DELETE) ||
                    (missed_snap_op->op == GF_SNAP_OPTION_TYPE_RESTORE))) {
            /* Optimizing create and delete entries for the same
             * brick and same node
             */
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_MISSED_SNAP_STATUS_DONE,
                   "Updating missed snap status "
                   "for %s:%s=%s:%d:%s:%d as DONE",
                   missed_snapinfo->node_uuid, missed_snapinfo->snap_uuid,
                   snap_opinfo->snap_vol_id, snap_opinfo->brick_num,
                   snap_opinfo->brick_path, snap_opinfo->op);
            snap_opinfo->status = GD_MISSED_SNAP_DONE;
            ret = 0;
            glusterd_free_snap_op(missed_snap_op);
            goto out;
        }
    }

    if (match == _gf_true) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DUP_ENTRY,
               "Duplicate entry. Not updating");
        glusterd_free_snap_op(missed_snap_op);
    } else {
        cds_list_add_tail(&missed_snap_op->snap_ops_list,
                          &missed_snapinfo->snap_ops);
    }

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Add new missed snap entry to the missed_snaps list. */
int32_t
glusterd_add_new_entry_to_list(char *missed_info, char *snap_vol_id,
                               int32_t brick_num, char *brick_path,
                               int32_t snap_op, int32_t snap_status)
{
    char *buf = NULL;
    char *save_ptr = NULL;
    char node_snap_info[PATH_MAX] = "";
    int32_t ret = -1;
    glusterd_missed_snap_info *missed_snapinfo = NULL;
    glusterd_snap_op_t *missed_snap_op = NULL;
    glusterd_conf_t *priv = NULL;
    gf_boolean_t match = _gf_false;
    gf_boolean_t free_missed_snap_info = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(missed_info);
    GF_ASSERT(snap_vol_id);
    GF_ASSERT(brick_path);

    priv = this->private;
    GF_ASSERT(priv);

    /* Create the snap_op object consisting of the *
     * snap id and the op */
    ret = glusterd_missed_snap_op_new(&missed_snap_op);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSED_SNAP_CREATE_FAIL,
               "Failed to create new missed snap object.");
        ret = -1;
        goto out;
    }

    missed_snap_op->snap_vol_id = gf_strdup(snap_vol_id);
    if (!missed_snap_op->snap_vol_id) {
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
    cds_list_for_each_entry(missed_snapinfo, &priv->missed_snaps_list,
                            missed_snaps)
    {
        snprintf(node_snap_info, sizeof(node_snap_info), "%s:%s",
                 missed_snapinfo->node_uuid, missed_snapinfo->snap_uuid);
        if (!strcmp(node_snap_info, missed_info)) {
            /* Found missed snapshot info for *
             * the same node and same snap */
            match = _gf_true;
            break;
        }
    }

    if (match == _gf_false) {
        /* First snap op missed for the brick */
        ret = glusterd_missed_snapinfo_new(&missed_snapinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSED_SNAP_CREATE_FAIL,
                   "Failed to create missed snapinfo");
            goto out;
        }
        free_missed_snap_info = _gf_true;
        buf = strtok_r(missed_info, ":", &save_ptr);
        if (!buf) {
            ret = -1;
            goto out;
        }
        missed_snapinfo->node_uuid = gf_strdup(buf);
        if (!missed_snapinfo->node_uuid) {
            ret = -1;
            goto out;
        }

        buf = strtok_r(NULL, ":", &save_ptr);
        if (!buf) {
            ret = -1;
            goto out;
        }
        missed_snapinfo->snap_uuid = gf_strdup(buf);
        if (!missed_snapinfo->snap_uuid) {
            ret = -1;
            goto out;
        }

        cds_list_add_tail(&missed_snap_op->snap_ops_list,
                          &missed_snapinfo->snap_ops);
        cds_list_add_tail(&missed_snapinfo->missed_snaps,
                          &priv->missed_snaps_list);

        ret = 0;
        goto out;
    } else {
        ret = glusterd_update_missed_snap_entry(missed_snapinfo,
                                                missed_snap_op);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MISSED_SNAP_CREATE_FAIL,
                   "Failed to update existing missed snap entry.");
            goto out;
        }
    }

out:
    if (ret) {
        glusterd_free_snap_op(missed_snap_op);

        if (missed_snapinfo && (free_missed_snap_info == _gf_true))
            glusterd_free_missed_snapinfo(missed_snapinfo);
    }

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Add  missing snap entries to the in-memory conf->missed_snap_list */
int32_t
glusterd_add_missed_snaps_to_list(dict_t *dict, int32_t missed_snap_count)
{
    char *buf = NULL;
    char *tmp = NULL;
    char *save_ptr = NULL;
    char *nodeid = NULL;
    char *snap_uuid = NULL;
    char *snap_vol_id = NULL;
    char *brick_path = NULL;
    char missed_info[PATH_MAX] = "";
    char key[64] = "";
    int keylen;
    int32_t i = -1;
    int32_t ret = -1;
    int32_t brick_num = -1;
    int32_t snap_op = -1;
    int32_t snap_status = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);

    priv = this->private;
    GF_ASSERT(priv);

    /* We can update the missed_snaps_list without acquiring *
     * any additional locks as big lock will be held.        */
    for (i = 0; i < missed_snap_count; i++) {
        keylen = snprintf(key, sizeof(key), "missed_snaps_%d", i);
        ret = dict_get_strn(dict, key, keylen, &buf);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to fetch %s", key);
            goto out;
        }

        gf_msg_debug(this->name, 0, "missed_snap_entry = %s", buf);

        /* Need to make a duplicate string coz the same dictionary *
         * is resent to the non-originator nodes */
        tmp = gf_strdup(buf);
        if (!tmp) {
            ret = -1;
            goto out;
        }

        /* Fetch the node-id, snap-id, brick_num,
         * brick_path, snap_op and snap status
         */
        nodeid = strtok_r(tmp, ":", &save_ptr);
        snap_uuid = strtok_r(NULL, "=", &save_ptr);
        snap_vol_id = strtok_r(NULL, ":", &save_ptr);
        brick_num = atoi(strtok_r(NULL, ":", &save_ptr));
        brick_path = strtok_r(NULL, ":", &save_ptr);
        snap_op = atoi(strtok_r(NULL, ":", &save_ptr));
        snap_status = atoi(strtok_r(NULL, ":", &save_ptr));

        if (!nodeid || !snap_uuid || !brick_path || !snap_vol_id ||
            brick_num < 1 || snap_op < 1 || snap_status < 1) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_INVALID_MISSED_SNAP_ENTRY,
                   "Invalid missed_snap_entry");
            ret = -1;
            goto out;
        }

        snprintf(missed_info, sizeof(missed_info), "%s:%s", nodeid, snap_uuid);

        ret = glusterd_add_new_entry_to_list(missed_info, snap_vol_id,
                                             brick_num, brick_path, snap_op,
                                             snap_status);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                   "Failed to store missed snaps_list");
            goto out;
        }

        GF_FREE(tmp);
        tmp = NULL;
    }

    ret = 0;
out:
    if (tmp)
        GF_FREE(tmp);

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* This function will restore origin volume to it's snap.
 * The restore operation will simply replace the Gluster origin
 * volume with the snap volume.
 * TODO: Multi-volume delete to be done.
 *       Cleanup in case of restore failure is pending.
 *
 * @param orig_vol      volinfo of origin volume
 * @param snap_vol      volinfo of snapshot volume
 *
 * @return 0 on success and negative value on error
 */
int
gd_restore_snap_volume(dict_t *dict, dict_t *rsp_dict,
                       glusterd_volinfo_t *orig_vol,
                       glusterd_volinfo_t *snap_vol, int32_t volcount)
{
    int ret = -1;
    glusterd_volinfo_t *new_volinfo = NULL;
    glusterd_snap_t *snap = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *temp_volinfo = NULL;
    glusterd_volinfo_t *voliter = NULL;
    gf_boolean_t conf_present = _gf_false;

    GF_ASSERT(dict);
    GF_ASSERT(rsp_dict);
    conf = this->private;
    GF_ASSERT(conf);

    GF_VALIDATE_OR_GOTO(this->name, orig_vol, out);
    GF_VALIDATE_OR_GOTO(this->name, snap_vol, out);
    snap = snap_vol->snapshot;
    GF_VALIDATE_OR_GOTO(this->name, snap, out);

    /* Set the status to under restore so that if the
     * the node goes down during restore and comes back
     * the state of the volume can be reverted correctly
     */
    snap->snap_status = GD_SNAP_STATUS_UNDER_RESTORE;

    /* We need to save this in disk so that if node goes
     * down the status is in updated state.
     */
    ret = glusterd_store_snap(snap);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED,
               "Could not store snap "
               "object for %s snap of %s volume",
               snap_vol->volname, snap_vol->parent_volname);
        goto out;
    }

    /* Snap volume must be stopped before performing the
     * restore operation.
     */
    ret = glusterd_stop_volume(snap_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_STOP_FAILED,
               "Failed to stop "
               "snap volume");
        goto out;
    }

    /* Create a new volinfo for the restored volume */
    ret = glusterd_volinfo_dup(snap_vol, &new_volinfo, _gf_true);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
               "Failed to create volinfo");
        goto out;
    }

    /* Following entries need to be derived from origin volume. */
    gf_strncpy(new_volinfo->volname, orig_vol->volname,
               sizeof(new_volinfo->volname));
    gf_uuid_copy(new_volinfo->volume_id, orig_vol->volume_id);
    new_volinfo->snap_count = orig_vol->snap_count;
    gf_uuid_copy(new_volinfo->restored_from_snap, snap_vol->snapshot->snap_id);

    /* Use the same version as the original version */
    new_volinfo->version = orig_vol->version;

    /* Copy the snap vol info to the new_volinfo.*/
    ret = glusterd_snap_volinfo_restore(dict, rsp_dict, new_volinfo, snap_vol,
                                        volcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
               "Failed to restore snap");
        goto out;
    }

    /* In case a new node is added to the peer, after a snapshot was
     * taken, the geo-rep files are not synced to that node. This
     * leads to the failure of snapshot restore. Hence, ignoring the
     * missing geo-rep files in the new node, and proceeding with
     * snapshot restore. Once the restore is successful, the missing
     * geo-rep files can be generated with "gluster volume geo-rep
     * <primary-vol> <secondary-vol> create push-pem force"
     */
    ret = glusterd_restore_geo_rep_files(snap_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_RESTORE_FAIL,
               "Failed to restore "
               "geo-rep files for snap %s",
               snap_vol->snapshot->snapname);
    }

    /* Need not save cksum, as we will copy cksum file in *
     * this function                                           *
     */
    ret = glusterd_copy_quota_files(snap_vol, orig_vol, &conf_present);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SNAP_RESTORE_FAIL,
               "Failed to restore "
               "quota files for snap %s",
               snap_vol->snapshot->snapname);
        goto out;
    }

    /* New volinfo always shows the status as created. Therefore
     * set the status to the original volume's status. */
    glusterd_set_volume_status(new_volinfo, orig_vol->status);

    cds_list_add_tail(&new_volinfo->vol_list, &conf->volumes);

    ret = glusterd_store_volinfo(new_volinfo,
                                 GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_OP_FAILED,
               "Failed to store volinfo");
        goto out;
    }

    ret = 0;
out:
    if (ret) {
        /* In case of any failure we should free new_volinfo. Doing
         * this will also remove the entry we added in conf->volumes
         * if it was added there.
         */
        if (new_volinfo)
            (void)glusterd_volinfo_delete(new_volinfo);
    } else {
        cds_list_for_each_entry_safe(voliter, temp_volinfo,
                                     &orig_vol->snap_volumes, snapvol_list)
        {
            cds_list_add_tail(&voliter->snapvol_list,
                              &new_volinfo->snap_volumes);
        }
    }

    return ret;
}

int
glusterd_snapshot_get_volnames_uuids(dict_t *dict, char *volname,
                                     gf_getsnap_name_uuid_rsp *snap_info_rsp)
{
    int ret = -1;
    int snapcount = 0;
    char key[32] = "";
    glusterd_volinfo_t *snap_vol = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_volinfo_t *tmp_vol = NULL;
    xlator_t *this = THIS;
    int op_errno = 0;

    GF_ASSERT(volname);
    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, dict, out, op_errno, EINVAL);
    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, volname, out, op_errno, EINVAL);
    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, snap_info_rsp, out, op_errno,
                                   EINVAL);

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_VOL_NOT_FOUND,
               "Failed to get volinfo of volume %s", volname);
        op_errno = EINVAL;
        goto out;
    }

    cds_list_for_each_entry_safe(snap_vol, tmp_vol, &volinfo->snap_volumes,
                                 snapvol_list)
    {
        if (GLUSTERD_STATUS_STARTED != snap_vol->status)
            continue;

        snapcount++;

        /* Set Snap Name */
        snprintf(key, sizeof(key), "snapname.%d", snapcount);
        ret = dict_set_dynstr_with_alloc(dict, key,
                                         snap_vol->snapshot->snapname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set "
                   "snap name in dictionary");
            goto out;
        }

        /* Set Snap ID */
        snprintf(key, sizeof(key), "snap-id.%d", snapcount);
        ret = dict_set_dynstr_with_alloc(
            dict, key, uuid_utoa(snap_vol->snapshot->snap_id));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set "
                   "snap id in dictionary");
            goto out;
        }

        /* Snap Volname which is used to activate the snap vol */
        snprintf(key, sizeof(key), "snap-volname.%d", snapcount);
        ret = dict_set_dynstr_with_alloc(dict, key, snap_vol->volname);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set "
                   "snap id in dictionary");
            goto out;
        }
    }

    ret = dict_set_int32n(dict, "snap-count", SLEN("snap-count"), snapcount);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set snapcount");
        op_errno = -ret;
        goto out;
    }

    ret = dict_allocate_and_serialize(dict, &snap_info_rsp->dict.dict_val,
                                      &snap_info_rsp->dict.dict_len);
    if (ret) {
        op_errno = -ret;
        ret = -1;
        goto out;
    }

    ret = 0;

out:
    snap_info_rsp->op_ret = ret;
    snap_info_rsp->op_errno = op_errno;
    snap_info_rsp->op_errstr = "";

    return ret;
}

/* This function is called to get the device path of the snap lvm. Usually
   if /dev/mapper/<group-name>-<lvm-name> is the device for the lvm,
   then the snap device will be /dev/<group-name>/<snapname>.
   This function takes care of building the path for the snap device.
*/

char *
glusterd_lvm_snapshot_device(char *device, char *snapname, int32_t brickcount)
{
    char snap[PATH_MAX] = "";
    char msg[1024] = "";
    char volgroup[PATH_MAX] = "";
    char *snap_device = NULL;
    xlator_t *this = NULL;
    runner_t runner = {
        0,
    };
    char *ptr = NULL;
    int ret = -1;

    this = THIS;
    GF_ASSERT(this);
    if (!device) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "device is NULL");
        goto out;
    }
    if (!snapname) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "snapname is NULL");
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

    snprintf(snap, sizeof(snap), "/dev/%s/%s_%d", gf_trim(volgroup), snapname,
             brickcount);
    snap_device = gf_strdup(snap);
    if (!snap_device) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, GD_MSG_NO_MEMORY,
               "Cannot copy the snapshot device name for snapname: %s",
               snapname);
    }

out:
    return snap_device;
}

struct glusterd_snap_ops lvm_snap_ops = {
    .name = "LVM",
    .probe = glusterd_lvm_probe,
    .details = glusterd_lvm_brick_details,
    .device = glusterd_lvm_snapshot_device,
    .create = glusterd_lvm_snapshot_create,
    .clone = glusterd_lvm_snapshot_create,
    .missed = glusterd_lvm_snapshot_missed,
    .remove = glusterd_lvm_snapshot_remove,
    .mount = glusterd_snapshot_mount,
};
