/*
   Copyright (c) 2007-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterd-op-sm.h"
#include <inttypes.h>


#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "syscall.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-hooks.h"
#include "store.h"
#include "glusterd-store.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-messages.h"

#include "rpc-clnt.h"
#include "common-utils.h"
#include "quota-common-utils.h"

#include <sys/resource.h>
#include <inttypes.h>
#include <dirent.h>

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif

void
glusterd_replace_slash_with_hyphen (char *str)
{
        char                    *ptr = NULL;

        ptr = strchr (str, '/');

        while (ptr) {
                *ptr = '-';
                ptr = strchr (str, '/');
        }
}

int32_t
glusterd_store_create_brick_dir (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;
        char                    brickdirpath[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volinfo);

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_BRICK_DIR (brickdirpath, volinfo, priv);
        ret = gf_store_mkdir (brickdirpath);

        return ret;
}

static void
glusterd_store_key_vol_brick_set (glusterd_brickinfo_t *brickinfo,
                                char *key_vol_brick, size_t len)
{
        GF_ASSERT (brickinfo);
        GF_ASSERT (key_vol_brick);
        GF_ASSERT (len >= PATH_MAX);

        snprintf (key_vol_brick, len, "%s", brickinfo->path);
        glusterd_replace_slash_with_hyphen (key_vol_brick);
}

static void
glusterd_store_brickinfofname_set (glusterd_brickinfo_t *brickinfo,
                                char *brickfname, size_t len)
{
        char                    key_vol_brick[PATH_MAX] = {0};

        GF_ASSERT (brickfname);
        GF_ASSERT (brickinfo);
        GF_ASSERT (len >= PATH_MAX);

        glusterd_store_key_vol_brick_set (brickinfo, key_vol_brick,
                                        sizeof (key_vol_brick));
        snprintf (brickfname, len, "%s:%s", brickinfo->hostname, key_vol_brick);
}

static void
glusterd_store_brickinfopath_set (glusterd_volinfo_t *volinfo,
                              glusterd_brickinfo_t *brickinfo,
                              char *brickpath, size_t len)
{
        char                    brickfname[PATH_MAX] = {0};
        char                    brickdirpath[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (brickpath);
        GF_ASSERT (brickinfo);
        GF_ASSERT (len >= PATH_MAX);

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_BRICK_DIR (brickdirpath, volinfo, priv);
        glusterd_store_brickinfofname_set (brickinfo, brickfname,
                                        sizeof (brickfname));
        snprintf (brickpath, len, "%s/%s", brickdirpath, brickfname);
}

static void
glusterd_store_snapd_path_set (glusterd_volinfo_t *volinfo,
                               char *snapd_path, size_t len)
{
        char                    volpath[PATH_MAX] = {0, };
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (len >= PATH_MAX);

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_VOLUME_DIR (volpath, volinfo, priv);

        snprintf (snapd_path, len, "%s/snapd.info", volpath);
}

gf_boolean_t
glusterd_store_is_valid_brickpath (char *volname, char *brick)
{
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int32_t                 ret = 0;
        size_t                  volname_len = strlen (volname);
        xlator_t                *this = NULL;
        int                     bpath_len = 0;
        const char              delim[2] = "/";
        char                    *sub_dir = NULL;
        char                    *saveptr = NULL;
        char                    *brickpath_ptr = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo, _gf_false,
                                                 NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_BRICK_CREATION_FAIL, "Failed to create brick "
                        "info for brick %s", brick);
                ret = 0;
                goto out;
        }
        ret = glusterd_volinfo_new (&volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "Failed to create volinfo");
                ret = 0;
                goto out;
        }
        if (volname_len >= sizeof (volinfo->volname)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_NAME_TOO_LONG, "volume name too long");
                ret = 0;
                goto out;
        }
        memcpy (volinfo->volname, volname, volname_len+1);

        /* Check whether brickpath is less than PATH_MAX */
        ret = 1;
        bpath_len = strlen (brickinfo->path);

        if (brickinfo->path[bpath_len - 1] != '/') {
                if (strlen (brickinfo->path) >= PATH_MAX) {
                        ret = 0;
                        goto out;
                }
        } else {
                /* Path has a trailing "/" which should not be considered in
                 * length check validation
                 */
                if (strlen (brickinfo->path) >= PATH_MAX + 1) {
                        ret = 0;
                        goto out;
                }
        }

        /* The following validation checks whether each sub directories in the
         * brick path meets the POSIX max length validation
         */

        brickpath_ptr = brickinfo->path;
        sub_dir = strtok_r (brickpath_ptr, delim, &saveptr);

        while (sub_dir != NULL) {
                if (strlen(sub_dir) >= _POSIX_PATH_MAX) {
                        ret = 0;
                        goto out;
                }
                sub_dir = strtok_r (NULL, delim, &saveptr);
        }

out:
        if (brickinfo)
                glusterd_brickinfo_delete (brickinfo);
        if (volinfo)
                glusterd_volinfo_unref (volinfo);

        return ret;
}

int32_t
glusterd_store_volinfo_brick_fname_write (int vol_fd,
                                         glusterd_brickinfo_t *brickinfo,
                                         int32_t brick_count)
{
        char                    key[PATH_MAX] = {0,};
        char                    brickfname[PATH_MAX] = {0,};
        int32_t                 ret = -1;

        snprintf (key, sizeof (key), "%s-%d", GLUSTERD_STORE_KEY_VOL_BRICK,
                  brick_count);
        glusterd_store_brickinfofname_set (brickinfo, brickfname,
                                        sizeof (brickfname));
        ret = gf_store_save_value (vol_fd, key, brickfname);
        if (ret)
                goto out;

out:
        return ret;
}

int32_t
glusterd_store_create_brick_shandle_on_absence (glusterd_volinfo_t *volinfo,
                                                glusterd_brickinfo_t *brickinfo)
{
        char                    brickpath[PATH_MAX] = {0,};
        int32_t                 ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        glusterd_store_brickinfopath_set (volinfo, brickinfo, brickpath,
                                          sizeof (brickpath));
        ret = gf_store_handle_create_on_absence (&brickinfo->shandle,
                                                 brickpath);
        return ret;
}

int32_t
glusterd_store_create_snapd_shandle_on_absence (glusterd_volinfo_t *volinfo)
{
        char                    snapd_path[PATH_MAX] = {0,};
        int32_t                 ret = 0;

        GF_ASSERT (volinfo);

        glusterd_store_snapd_path_set (volinfo, snapd_path,
                                       sizeof (snapd_path));
        ret = gf_store_handle_create_on_absence (&volinfo->snapd.handle,
                                                 snapd_path);
        return ret;
}

/* Store the bricks snapshot details only if required
 *
 * The snapshot details will be stored only if the cluster op-version is
 * greater than or equal to 4
 */
int
gd_store_brick_snap_details_write (int fd, glusterd_brickinfo_t *brickinfo)
{
        int ret = -1;
        xlator_t *this = NULL;
        glusterd_conf_t *conf = NULL;
        char value[256] = {0,};

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (fd > 0), out);
        GF_VALIDATE_OR_GOTO (this->name, (brickinfo != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        if (strlen(brickinfo->device_path) > 0) {
                snprintf (value, sizeof(value), "%s", brickinfo->device_path);
                ret = gf_store_save_value (fd,
                                GLUSTERD_STORE_KEY_BRICK_DEVICE_PATH, value);
                if (ret)
                        goto out;
        }

        if (strlen(brickinfo->mount_dir) > 0) {
                memset (value, 0, sizeof (value));
                snprintf (value, sizeof(value), "%s", brickinfo->mount_dir);
                ret = gf_store_save_value (fd,
                                GLUSTERD_STORE_KEY_BRICK_MOUNT_DIR, value);
                if (ret)
                        goto out;
        }

        if (strlen (brickinfo->fstype) > 0) {
                snprintf (value, sizeof (value), "%s", brickinfo->fstype);
                ret = gf_store_save_value (fd,
                                GLUSTERD_STORE_KEY_BRICK_FSTYPE, value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FS_LABEL_UPDATE_FAIL, "Failed to save "
                                "brick fs type of brick %s", brickinfo->path);
                        goto out;
                }
        }

        if (strlen (brickinfo->mnt_opts) > 0) {
                snprintf (value, sizeof (value), "%s", brickinfo->mnt_opts);
                ret = gf_store_save_value (fd,
                                GLUSTERD_STORE_KEY_BRICK_MNTOPTS, value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRK_MOUNTOPTS_FAIL, "Failed to save "
                                "brick mnt opts of brick %s", brickinfo->path);
                        goto out;
                }
        }

        memset (value, 0, sizeof (value));
        snprintf (value, sizeof(value), "%d", brickinfo->snap_status);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_SNAP_STATUS,
                                   value);

out:
        return ret;
}

int32_t
glusterd_store_brickinfo_write (int fd, glusterd_brickinfo_t *brickinfo)
{
        char                    value[256] = {0,};
        int32_t                 ret = 0;

        GF_ASSERT (brickinfo);
        GF_ASSERT (fd > 0);

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_HOSTNAME,
                                   brickinfo->hostname);
        if (ret)
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_PATH,
                                   brickinfo->path);
        if (ret)
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_REAL_PATH,
                                   brickinfo->path);
        if (ret)
                goto out;

        snprintf (value, sizeof(value), "%d", brickinfo->port);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_PORT, value);

        snprintf (value, sizeof(value), "%d", brickinfo->rdma_port);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_RDMA_PORT,
                                   value);

        snprintf (value, sizeof(value), "%d", brickinfo->decommissioned);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_DECOMMISSIONED,
                                   value);
        if (ret)
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_ID,
                                   brickinfo->brick_id);
        if (ret)
                goto out;

        ret = gd_store_brick_snap_details_write (fd, brickinfo);
        if (ret)
                goto out;

        if (!brickinfo->vg[0])
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_VGNAME,
                                         brickinfo->vg);
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_snapd_write (int fd, glusterd_volinfo_t *volinfo)
{
        char                    value[256] = {0,};
        int32_t                 ret        = 0;
        xlator_t               *this       = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (fd > 0);

        this = THIS;
        GF_ASSERT (this);

        snprintf (value, sizeof(value), "%d", volinfo->snapd.port);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAPD_PORT, value);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_PORT_STORE_FAIL,
                        "failed to store the snapd "
                        "port of volume %s", volinfo->volname);


        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_perform_brick_store (glusterd_brickinfo_t *brickinfo)
{
        int                         fd = -1;
        int32_t                     ret = -1;
        GF_ASSERT (brickinfo);

        fd = gf_store_mkstemp (brickinfo->shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        ret = glusterd_store_brickinfo_write (fd, brickinfo);
        if (ret)
                goto out;

out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (brickinfo->shandle);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_perform_snapd_store (glusterd_volinfo_t *volinfo)
{
        int                         fd  = -1;
        int32_t                     ret = -1;
        xlator_t                  *this = NULL;

        GF_ASSERT (volinfo);

        this = THIS;
        GF_ASSERT (this);

        fd = gf_store_mkstemp (volinfo->snapd.handle);
        if (fd <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "failed to create the "
                        "temporary file for the snapd store handle of volume "
                        "%s", volinfo->volname);
                goto out;
        }

        ret = glusterd_store_snapd_write (fd, volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_PORT_STORE_FAIL,
                        "failed to write snapd port "
                        "info to store handle (volume: %s", volinfo->volname);
                goto out;
        }

        ret = gf_store_rename_tmppath (volinfo->snapd.handle);

out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (volinfo->snapd.handle);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_brickinfo (glusterd_volinfo_t *volinfo,
                          glusterd_brickinfo_t *brickinfo, int32_t brick_count,
                          int vol_fd)
{
        int32_t                 ret = -1;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        ret = glusterd_store_volinfo_brick_fname_write (vol_fd, brickinfo,
                                                       brick_count);
        if (ret)
                goto out;

        ret = glusterd_store_create_brick_dir (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_create_brick_shandle_on_absence (volinfo,
                                                              brickinfo);
        if (ret)
                goto out;

        ret = glusterd_store_perform_brick_store (brickinfo);
out:
        gf_msg_debug (THIS->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_snapd_info (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret  = -1;
        xlator_t               *this = NULL;

        GF_ASSERT (volinfo);

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_store_create_snapd_shandle_on_absence (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_HANDLE_CREATE_FAIL,
                        "failed to create store "
                        "handle for snapd (volume: %s)", volinfo->volname);
                goto out;
        }

        ret = glusterd_store_perform_snapd_store (volinfo);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_INFO_STORE_FAIL,
                        "failed to store snapd info "
                        "of the volume %s", volinfo->volname);

out:
        if (ret)
                gf_store_unlink_tmppath (volinfo->snapd.handle);

        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_delete_brick (glusterd_brickinfo_t *brickinfo, char *delete_path)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    brickpath[PATH_MAX] = {0,};
        char                    *ptr = NULL;
        char                    *tmppath = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brickinfo);

        priv = this->private;
        GF_ASSERT (priv);

        tmppath = gf_strdup (brickinfo->path);

        ptr = strchr (tmppath, '/');

        while (ptr) {
                *ptr = '-';
                ptr = strchr (tmppath, '/');
        }

        snprintf (brickpath, sizeof (brickpath),
                  "%s/"GLUSTERD_BRICK_INFO_DIR"/%s:%s", delete_path,
                  brickinfo->hostname, tmppath);

        GF_FREE (tmppath);

        ret = sys_unlink (brickpath);

        if ((ret < 0) && (errno != ENOENT)) {
                gf_msg_debug (this->name, 0, "Unlink failed on %s",
                              brickpath);
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }

out:
        if (brickinfo->shandle) {
                gf_store_handle_destroy (brickinfo->shandle);
                brickinfo->shandle = NULL;
        }
        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_remove_bricks (glusterd_volinfo_t *volinfo, char *delete_path)
{
        int32_t                ret = 0;
        glusterd_brickinfo_t  *tmp = NULL;
        glusterd_conf_t       *priv = NULL;
        xlator_t              *this = NULL;
        DIR                   *dir = NULL;
        struct dirent         *entry = NULL;
        struct dirent          scratch[2] = {{0,},};
        char                   path[PATH_MAX] = {0,};
        char                   brickdir[PATH_MAX] = {0,};

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);

        cds_list_for_each_entry (tmp, &volinfo->bricks, brick_list) {
                ret = glusterd_store_delete_brick (tmp, delete_path);
                if (ret)
                        goto out;
        }

        priv = this->private;
        GF_ASSERT (priv);

        snprintf (brickdir, sizeof (brickdir), "%s/%s", delete_path,
                  GLUSTERD_BRICK_INFO_DIR);

        dir = sys_opendir (brickdir);

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);

        while (entry) {
                snprintf (path, sizeof (path), "%s/%s",
                          brickdir, entry->d_name);
                ret = sys_unlink (path);
                if (ret && errno != ENOENT) {
                        gf_msg_debug (this->name, 0, "Unable to unlink %s",
                                      path);
                }
                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }

        sys_closedir (dir);

        ret = sys_rmdir (brickdir);

out:
        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;
}

static int
_storeslaves (dict_t *this, char *key, data_t *value, void *data)
{
        int32_t                      ret = 0;
        gf_store_handle_t           *shandle = NULL;
        xlator_t                    *xl = NULL;

        xl = THIS;
        GF_ASSERT (xl);

        shandle = (gf_store_handle_t*)data;

        GF_ASSERT (shandle);
        GF_ASSERT (shandle->fd > 0);
        GF_ASSERT (shandle->path);
        GF_ASSERT (key);
        GF_ASSERT (value && value->data);

        if ((!shandle) || (shandle->fd <= 0) || (!shandle->path))
                return -1;

        if (!key)
                return -1;
        if (!value || !value->data)
                return -1;

        gf_msg_debug (xl->name, 0, "Storing in volinfo:key= %s, val=%s",
                key, value->data);

        ret = gf_store_save_value (shandle->fd, key, (char*)value->data);
        if (ret) {
                gf_msg (xl->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_HANDLE_WRITE_FAIL,
                        "Unable to write into store"
                        " handle for path: %s", shandle->path);
                return -1;
        }
        return 0;
}


int _storeopts (dict_t *this, char *key, data_t *value, void *data)
{
        int32_t                      ret = 0;
        int32_t                      exists = 0;
        gf_store_handle_t           *shandle = NULL;
        xlator_t                    *xl = NULL;

        xl = THIS;
        GF_ASSERT (xl);

        shandle = (gf_store_handle_t*)data;

        GF_ASSERT (shandle);
        GF_ASSERT (shandle->fd > 0);
        GF_ASSERT (shandle->path);
        GF_ASSERT (key);
        GF_ASSERT (value && value->data);

        if ((!shandle) || (shandle->fd <= 0) || (!shandle->path))
                return -1;

        if (!key)
                return -1;
        if (!value || !value->data)
                return -1;

        if (is_key_glusterd_hooks_friendly (key)) {
                exists = 1;

        } else {
                exists = glusterd_check_option_exists (key, NULL);
        }

        if (1 == exists) {
                gf_msg_debug (xl->name, 0, "Storing in volinfo:key= %s, "
                        "val=%s", key, value->data);

        } else {
                gf_msg_debug (xl->name, 0, "Discarding:key= %s, val=%s",
                        key, value->data);
                return 0;
        }

        ret = gf_store_save_value (shandle->fd, key, (char*)value->data);
        if (ret) {
                gf_msg (xl->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_HANDLE_WRITE_FAIL,
                        "Unable to write into store"
                        " handle for path: %s", shandle->path);
                return -1;
        }
        return 0;
}

/* Store the volumes snapshot details only if required
 *
 * The snapshot details will be stored only if the cluster op-version is
 * greater than or equal to 4
 */
int
glusterd_volume_write_snap_details (int fd, glusterd_volinfo_t *volinfo)
{
        int              ret           = -1;
        xlator_t        *this          = NULL;
        glusterd_conf_t *conf          = NULL;
        char             buf[PATH_MAX] = {0,};

        this = THIS;
        GF_ASSERT (this != NULL);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        GF_VALIDATE_OR_GOTO (this->name, (fd > 0), out);
        GF_VALIDATE_OR_GOTO (this->name, (volinfo != NULL), out);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        snprintf (buf, sizeof (buf), "%s", volinfo->parent_volname);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PARENT_VOLNAME, buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_FAIL, "Failed to store "
                        GLUSTERD_STORE_KEY_PARENT_VOLNAME);
                goto out;
        }

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_RESTORED_SNAP,
                                   uuid_utoa (volinfo->restored_from_snap));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_HANDLE_WRITE_FAIL,
                        "Unable to write restored_from_snap");
                goto out;
        }

        memset (buf, 0, sizeof (buf));
        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->snap_max_hard_limit);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                                   buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HARD_LIMIT_SET_FAIL,
                        "Unable to write snap-max-hard-limit");
                goto out;
        }

        ret = glusterd_store_snapd_info (volinfo);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_INFO_STORE_FAIL, "snapd info store failed "
                        "volume: %s", volinfo->volname);

out:
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPINFO_WRITE_FAIL,
                        "Failed to write snap details"
                        " for volume %s", volinfo->volname);
        return ret;
}

int32_t
glusterd_volume_write_tier_details (int fd, glusterd_volinfo_t *volinfo)
{
        int32_t      ret            = -1;
        char         buf[PATH_MAX]  = "";

        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                ret = 0;
                goto out;
        }

        snprintf (buf, sizeof (buf), "%d", volinfo->tier_info.cold_brick_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_COLD_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d",
                  volinfo->tier_info.cold_replica_count);
        ret = gf_store_save_value (fd,
                                   GLUSTERD_STORE_KEY_COLD_REPLICA_COUNT,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->tier_info.cold_disperse_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_COLD_DISPERSE_COUNT,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d",
                  volinfo->tier_info.cold_redundancy_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_COLD_REDUNDANCY_COUNT,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->tier_info.hot_brick_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_HOT_COUNT,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->tier_info.hot_replica_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_HOT_REPLICA_COUNT,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->tier_info.hot_type);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_HOT_TYPE, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->tier_info.cold_type);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_COLD_TYPE, buf);
        if (ret)
                goto out;

 out:
        return ret;
}

int32_t
glusterd_volume_exclude_options_write (int fd, glusterd_volinfo_t *volinfo)
{
        char        *str            = NULL;
        char         buf[PATH_MAX]  = "";
        int32_t      ret            = -1;
        xlator_t    *this           = NULL;
        glusterd_conf_t  *conf      = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (fd > 0);
        GF_ASSERT (volinfo);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, (conf != NULL), out);

        snprintf (buf, sizeof (buf), "%d", volinfo->type);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_TYPE, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->brick_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->status);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_STATUS, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->sub_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_SUB_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->stripe_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_STRIPE_CNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->replica_count);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_REPLICA_CNT,
                                   buf);
        if (ret)
                goto out;

        if ((conf->op_version >= GD_OP_VERSION_3_7_6) &&
            volinfo->arbiter_count) {
                snprintf (buf, sizeof (buf), "%d", volinfo->arbiter_count);
                ret = gf_store_save_value (fd,
                                           GLUSTERD_STORE_KEY_VOL_ARBITER_CNT,
                                           buf);
                if (ret)
                        goto out;
        }

        if (conf->op_version >= GD_OP_VERSION_3_6_0) {
                snprintf (buf, sizeof (buf), "%d", volinfo->disperse_count);
                ret = gf_store_save_value (fd,
                                           GLUSTERD_STORE_KEY_VOL_DISPERSE_CNT,
                                           buf);
                if (ret)
                        goto out;

                snprintf (buf, sizeof (buf), "%d", volinfo->redundancy_count);
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_REDUNDANCY_CNT,
                                           buf);
                if (ret)
                        goto out;
        }

        snprintf (buf, sizeof (buf), "%d", volinfo->version);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_VERSION, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->transport_type);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_TRANSPORT, buf);
        if (ret)
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_ID,
                                   uuid_utoa (volinfo->volume_id));
        if (ret)
                goto out;

        str = glusterd_auth_get_username (volinfo);
        if (str) {
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_USERNAME,
                                           str);
                if (ret)
                goto out;
        }

        str = glusterd_auth_get_password (volinfo);
        if (str) {
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PASSWORD,
                                           str);
                if (ret)
                        goto out;
        }

        snprintf (buf, sizeof (buf), "%d", volinfo->op_version);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_OP_VERSION, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->client_op_version);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_CLIENT_OP_VERSION,
                                   buf);
        if (ret)
                goto out;
        if (volinfo->caps) {
                snprintf (buf, sizeof (buf), "%d", volinfo->caps);
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_CAPS,
                                           buf);
                if (ret)
                        goto out;
        }

        if (conf->op_version >= GD_OP_VERSION_3_7_6) {
                snprintf (buf, sizeof (buf), "%d",
                          volinfo->quota_xattr_version);
                ret = gf_store_save_value (fd,
                                           GLUSTERD_STORE_KEY_VOL_QUOTA_VERSION,
                                           buf);
                if (ret)
                        goto out;

                snprintf (buf, sizeof (buf), "%d", volinfo->is_tier_enabled);
                ret = gf_store_save_value (fd, GF_TIER_ENABLED, buf);
                if (ret)
                        goto out;

        }

        ret = glusterd_volume_write_tier_details (fd, volinfo);

        ret = glusterd_volume_write_snap_details (fd, volinfo);

out:
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_VALS_WRITE_FAIL, "Unable to write volume "
                        "values for %s", volinfo->volname);
        return ret;
}

static void
glusterd_store_voldirpath_set (glusterd_volinfo_t *volinfo, char *voldirpath,
                               size_t len)
{
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volinfo);
        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_VOLUME_DIR (voldirpath, volinfo, priv);
}

static int32_t
glusterd_store_create_volume_dir (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;
        char                    voldirpath[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);

        glusterd_store_voldirpath_set (volinfo, voldirpath,
                                       sizeof (voldirpath));
        ret = gf_store_mkdir (voldirpath);

        gf_msg_debug (THIS->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_create_snap_dir (glusterd_snap_t *snap)
{
        int32_t            ret                   = -1;
        char               snapdirpath[PATH_MAX] = {0,};
        glusterd_conf_t   *priv                  = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (snap);

        GLUSTERD_GET_SNAP_DIR (snapdirpath, snap, priv);

        ret = mkdir_p (snapdirpath, 0755, _gf_true);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Failed to create snaps dir "
                        "%s", snapdirpath);
        }
        return ret;
}

int32_t
glusterd_store_volinfo_write (int fd, glusterd_volinfo_t *volinfo)
{
        int32_t                         ret = -1;
        gf_store_handle_t              *shandle = NULL;
        GF_ASSERT (fd > 0);
        GF_ASSERT (volinfo);
        GF_ASSERT (volinfo->shandle);

        shandle = volinfo->shandle;
        ret = glusterd_volume_exclude_options_write (fd, volinfo);
        if (ret)
                goto out;

        shandle->fd = fd;
        dict_foreach (volinfo->dict, _storeopts, shandle);

        dict_foreach (volinfo->gsync_slaves, _storeslaves, shandle);
        shandle->fd = 0;
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_snapinfo_write (glusterd_snap_t *snap)
{
        int32_t  ret            = -1;
        int      fd             = 0;
        char     buf[PATH_MAX]  = "";

        GF_ASSERT (snap);

        fd = gf_store_mkstemp (snap->shandle);
        if (fd <= 0)
               goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAP_ID,
                                   uuid_utoa (snap->snap_id));
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", snap->snap_status);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAP_STATUS, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", snap->snap_restored);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAP_RESTORED, buf);
        if (ret)
                goto out;

        if (snap->description) {
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAP_DESC,
                                           snap->description);
                if (ret)
                        goto out;
        }

        snprintf (buf, sizeof (buf), "%ld", snap->time_stamp);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_SNAP_TIMESTAMP, buf);

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

static void
glusterd_store_volfpath_set (glusterd_volinfo_t *volinfo, char *volfpath,
                             size_t len)
{
        char    voldirpath[PATH_MAX] = {0,};
        GF_ASSERT (volinfo);
        GF_ASSERT (volfpath);
        GF_ASSERT (len <= PATH_MAX);

        glusterd_store_voldirpath_set (volinfo, voldirpath,
                                       sizeof (voldirpath));
        snprintf (volfpath, len, "%s/%s", voldirpath, GLUSTERD_VOLUME_INFO_FILE);
}

static void
glusterd_store_node_state_path_set (glusterd_volinfo_t *volinfo,
                                    char *node_statepath, size_t len)
{
        char    voldirpath[PATH_MAX] = {0,};
        GF_ASSERT (volinfo);
        GF_ASSERT (node_statepath);
        GF_ASSERT (len <= PATH_MAX);

        glusterd_store_voldirpath_set (volinfo, voldirpath,
                                       sizeof (voldirpath));
        snprintf (node_statepath, len, "%s/%s", voldirpath,
                  GLUSTERD_NODE_STATE_FILE);
}

static void
glusterd_store_quota_conf_path_set (glusterd_volinfo_t *volinfo,
                                    char *quota_conf_path, size_t len)
{
        char    voldirpath[PATH_MAX] = {0,};
        GF_ASSERT (volinfo);
        GF_ASSERT (quota_conf_path);
        GF_ASSERT (len <= PATH_MAX);

        glusterd_store_voldirpath_set (volinfo, voldirpath,
                                       sizeof (voldirpath));
        snprintf (quota_conf_path, len, "%s/%s", voldirpath,
                  GLUSTERD_VOLUME_QUOTA_CONFIG);
}

static void
glusterd_store_missed_snaps_list_path_set (char *missed_snaps_list,
                                           size_t len)
{
        glusterd_conf_t *priv = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (missed_snaps_list);
        GF_ASSERT (len <= PATH_MAX);

        snprintf (missed_snaps_list, len, "%s/snaps/"
                  GLUSTERD_MISSED_SNAPS_LIST_FILE, priv->workdir);
}

static void
glusterd_store_snapfpath_set (glusterd_snap_t *snap, char *snap_fpath,
                              size_t len)
{
        glusterd_conf_t *priv = NULL;
        priv = THIS->private;
        GF_ASSERT (priv);
        GF_ASSERT (snap);
        GF_ASSERT (snap_fpath);
        GF_ASSERT (len <= PATH_MAX);

        snprintf (snap_fpath, len, "%s/snaps/%s/%s", priv->workdir,
                  snap->snapname, GLUSTERD_SNAP_INFO_FILE);
}

int32_t
glusterd_store_create_vol_shandle_on_absence (glusterd_volinfo_t *volinfo)
{
        char            volfpath[PATH_MAX] = {0};
        int32_t         ret = 0;

        GF_ASSERT (volinfo);

        glusterd_store_volfpath_set (volinfo, volfpath, sizeof (volfpath));
        ret = gf_store_handle_create_on_absence (&volinfo->shandle, volfpath);
        return ret;
}

int32_t
glusterd_store_create_nodestate_sh_on_absence (glusterd_volinfo_t *volinfo)
{
        char            node_state_path[PATH_MAX] = {0};
        int32_t         ret                   = 0;

        GF_ASSERT (volinfo);

        glusterd_store_node_state_path_set (volinfo, node_state_path,
                                            sizeof (node_state_path));
        ret =
          gf_store_handle_create_on_absence (&volinfo->node_state_shandle,
                                             node_state_path);

        return ret;
}

int32_t
glusterd_store_create_quota_conf_sh_on_absence (glusterd_volinfo_t *volinfo)
{
        char            quota_conf_path[PATH_MAX] = {0};
        int32_t         ret                       = 0;

        GF_ASSERT (volinfo);

        glusterd_store_quota_conf_path_set (volinfo, quota_conf_path,
                                            sizeof (quota_conf_path));
        ret =
          gf_store_handle_create_on_absence (&volinfo->quota_conf_shandle,
                                             quota_conf_path);

        return ret;
}

static int32_t
glusterd_store_create_missed_snaps_list_shandle_on_absence ()
{
        char               missed_snaps_list[PATH_MAX] = "";
        int32_t            ret                         = -1;
        glusterd_conf_t   *priv                        = NULL;
        xlator_t          *this                        = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        glusterd_store_missed_snaps_list_path_set (missed_snaps_list,
                                                   sizeof(missed_snaps_list));

        ret = gf_store_handle_create_on_absence
                                            (&priv->missed_snaps_list_shandle,
                                             missed_snaps_list);
        return ret;
}

int32_t
glusterd_store_create_snap_shandle_on_absence (glusterd_snap_t *snap)
{
        char            snapfpath[PATH_MAX] = {0};
        int32_t         ret = 0;

        GF_ASSERT (snap);

        glusterd_store_snapfpath_set (snap, snapfpath, sizeof (snapfpath));
        ret = gf_store_handle_create_on_absence (&snap->shandle, snapfpath);
        return ret;
}

int32_t
glusterd_store_brickinfos (glusterd_volinfo_t *volinfo, int vol_fd)
{
        int32_t                 ret = 0;
        glusterd_brickinfo_t    *brickinfo = NULL;
        int32_t                 brick_count = 0;

        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_store_brickinfo (volinfo, brickinfo,
                                brick_count, vol_fd);
                if (ret)
                        goto out;
                brick_count++;
        }
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int
_gd_store_rebalance_dict (dict_t *dict, char *key, data_t *value, void *data)
{
        int             ret = -1;
        int             fd = 0;

        fd = *(int *)data;

        ret = gf_store_save_value (fd, key, value->data);

        return ret;
}

int32_t
glusterd_store_state_tier_write (int fd, glusterd_volinfo_t *volinfo)
{
        int     ret             = -1;
        char    buf[PATH_MAX]   = {0, };

        GF_VALIDATE_OR_GOTO (THIS->name, (fd > 0), out);
        GF_VALIDATE_OR_GOTO (THIS->name, volinfo, out);

        /*tier counter values are stored here. so that after restart
         * of glusterd tier resumes at the state is was brought down
         */

        if (volinfo->tier.defrag_cmd == GF_DEFRAG_CMD_STATUS) {
                ret = 0;
                goto out;
        }

        snprintf (buf, sizeof (buf), "%d", volinfo->tier.defrag_status);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_TIER_STATUS,
                                   buf);
        if (ret)
                goto out;


        snprintf (buf, sizeof (buf), "%d", volinfo->tier.op);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_TIER_DETACH_OP, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->tier.rebalance_files);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_MIGRATED_FILES,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->tier.rebalance_data);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_MIGRATED_SIZE,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->tier.lookedup_files);
        ret = gf_store_save_value (fd,
                                   GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SCANNED,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64,
                        volinfo->tier.rebalance_failures);
        ret = gf_store_save_value (fd,
                                   GLUSTERD_STORE_KEY_VOL_MIGRATIONS_FAILURES,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->tier.skipped_files);
        ret = gf_store_save_value (fd,
                                   GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SKIPPED,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%f", volinfo->tier.rebalance_time);
        ret = gf_store_save_value (fd,
                                   GLUSTERD_STORE_KEY_VOL_MIGRATION_RUN_TIME,
                                   buf);
        if (ret)
                goto out;

        gf_uuid_unparse (volinfo->tier.rebalance_id, buf);
        ret = gf_store_save_value (fd, GF_TIER_TID_KEY, buf);
        if (ret)
                goto out;

        if (volinfo->tier.dict) {
                dict_foreach (volinfo->tier.dict, _gd_store_rebalance_dict,
                              &fd);
        }
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_node_state_write (int fd, glusterd_volinfo_t *volinfo)
{
        int     ret             = -1;
        char    buf[PATH_MAX]   = {0, };

        GF_ASSERT (fd > 0);
        GF_ASSERT (volinfo);

        if (volinfo->rebal.defrag_cmd == GF_DEFRAG_CMD_STATUS) {
                ret = 0;
                goto out;
        }

        snprintf (buf, sizeof (buf), "%d", volinfo->rebal.defrag_cmd);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->rebal.defrag_status);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_STATUS,
                                   buf);
        if (ret)
                goto out;


        snprintf (buf, sizeof (buf), "%d", volinfo->rebal.op);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_DEFRAG_OP, buf);
        if (ret)
                goto out;

        gf_uuid_unparse (volinfo->rebal.rebalance_id, buf);
        ret = gf_store_save_value (fd, GF_REBALANCE_TID_KEY, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->rebal.rebalance_files);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_REB_FILES,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->rebal.rebalance_data);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_SIZE, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->rebal.lookedup_files);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_SCANNED,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->rebal.rebalance_failures);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_FAILURES,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%"PRIu64, volinfo->rebal.skipped_files);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_SKIPPED,
                                   buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%lf", volinfo->rebal.rebalance_time);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_VOL_DEFRAG_RUN_TIME,
                                   buf);
        if (ret)
                goto out;

        if (volinfo->rebal.dict) {
                dict_foreach (volinfo->rebal.dict, _gd_store_rebalance_dict,
                              &fd);
        }
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_perform_node_state_store (glusterd_volinfo_t *volinfo)
{
        int                         fd = -1;
        int32_t                     ret = -1;
        GF_ASSERT (volinfo);

        fd = gf_store_mkstemp (volinfo->node_state_shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        ret = glusterd_store_node_state_write (fd, volinfo);
        if (ret)
                goto out;

        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                ret = glusterd_store_state_tier_write (fd, volinfo);
                if (ret)
                        goto out;
        }

        ret = gf_store_rename_tmppath (volinfo->node_state_shandle);
        if (ret)
                goto out;

out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (volinfo->node_state_shandle);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_perform_volume_store (glusterd_volinfo_t *volinfo)
{
        int                         fd = -1;
        int32_t                     ret = -1;
        GF_ASSERT (volinfo);

        fd = gf_store_mkstemp (volinfo->shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo_write (fd, volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_brickinfos (volinfo, fd);
        if (ret)
                goto out;

out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (volinfo->shandle);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}

void
glusterd_perform_volinfo_version_action (glusterd_volinfo_t *volinfo,
                                         glusterd_volinfo_ver_ac_t ac)
{
        GF_ASSERT (volinfo);

        switch (ac) {
        case GLUSTERD_VOLINFO_VER_AC_NONE:
        break;
        case GLUSTERD_VOLINFO_VER_AC_INCREMENT:
                volinfo->version++;
        break;
        case GLUSTERD_VOLINFO_VER_AC_DECREMENT:
                volinfo->version--;
        break;
        }
}

void
glusterd_store_bricks_cleanup_tmp (glusterd_volinfo_t *volinfo)
{
        glusterd_brickinfo_t    *brickinfo           = NULL;

        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_store_unlink_tmppath (brickinfo->shandle);
        }
}

void
glusterd_store_volume_cleanup_tmp (glusterd_volinfo_t *volinfo)
{
        GF_ASSERT (volinfo);

        glusterd_store_bricks_cleanup_tmp (volinfo);

        gf_store_unlink_tmppath (volinfo->shandle);

        gf_store_unlink_tmppath (volinfo->node_state_shandle);

        gf_store_unlink_tmppath (volinfo->snapd.handle);
}

int32_t
glusterd_store_brickinfos_atomic_update (glusterd_volinfo_t *volinfo)
{
        int                      ret            = -1;
        glusterd_brickinfo_t    *brickinfo      = NULL;

        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = gf_store_rename_tmppath (brickinfo->shandle);
                if (ret)
                        goto out;
        }
out:
        return ret;
}

int32_t
glusterd_store_volinfo_atomic_update (glusterd_volinfo_t *volinfo)
{
        int ret = -1;
        GF_ASSERT (volinfo);

        ret = gf_store_rename_tmppath (volinfo->shandle);
        if (ret)
                goto out;

out:
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Couldn't rename "
                        "temporary file(s)");
        return ret;
}

int32_t
glusterd_store_volume_atomic_update (glusterd_volinfo_t *volinfo)
{
        int ret = -1;
        GF_ASSERT (volinfo);

        ret = glusterd_store_brickinfos_atomic_update (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_volinfo_atomic_update (volinfo);

out:
        return ret;
}

int32_t
glusterd_store_snap_atomic_update (glusterd_snap_t *snap)
{
        int ret = -1;
        GF_ASSERT (snap);

        ret = gf_store_rename_tmppath (snap->shandle);
        if (ret)
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "Couldn't rename "
                        "temporary file(s)");

        return ret;
}

int32_t
glusterd_store_snap (glusterd_snap_t *snap)
{
        int32_t                 ret = -1;

        GF_ASSERT (snap);

        ret = glusterd_store_create_snap_dir (snap);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPDIR_CREATE_FAIL,
                        "Failed to create snap dir");
                goto out;
        }

        ret = glusterd_store_create_snap_shandle_on_absence (snap);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                       GD_MSG_SNAPINFO_CREATE_FAIL,
                       "Failed to create snap info "
                        "file");
                goto out;
        }

        ret = glusterd_store_snapinfo_write (snap);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPINFO_WRITE_FAIL,
                        "Failed to write snap info");
                goto out;
        }

        ret = glusterd_store_snap_atomic_update (snap);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_AUTOMIC_UPDATE_FAIL,
                        "Failed to do automic update");
                goto out;
        }

out:
        if (ret && snap->shandle)
                gf_store_unlink_tmppath (snap->shandle);

        gf_msg_trace (THIS->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_volinfo (glusterd_volinfo_t *volinfo, glusterd_volinfo_ver_ac_t ac)
{
        int32_t                 ret = -1;

        GF_ASSERT (volinfo);

        glusterd_perform_volinfo_version_action (volinfo, ac);
        ret = glusterd_store_create_volume_dir (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_create_vol_shandle_on_absence (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_create_nodestate_sh_on_absence (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_perform_volume_store (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_volume_atomic_update (volinfo);
        if (ret) {
                glusterd_perform_volinfo_version_action (volinfo,
                                                         GLUSTERD_VOLINFO_VER_AC_DECREMENT);
                goto out;
        }

        ret = glusterd_store_perform_node_state_store (volinfo);
        if (ret)
                goto out;

        /* checksum should be computed at the end */
        ret = glusterd_compute_cksum (volinfo, _gf_false);
        if (ret)
                goto out;

out:
        if (ret)
                glusterd_store_volume_cleanup_tmp (volinfo);

        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int32_t
glusterd_store_delete_volume (glusterd_volinfo_t *volinfo)
{
        char             pathname[PATH_MAX]    = {0,};
        int32_t          ret                   = 0;
        glusterd_conf_t *priv                  = NULL;
        char             delete_path[PATH_MAX] = {0,};
        char             trashdir[PATH_MAX]    = {0,};
        xlator_t        *this                  = NULL;
        gf_boolean_t     rename_fail           = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);
        priv = this->private;

        GF_ASSERT (priv);

        GLUSTERD_GET_VOLUME_DIR (pathname, volinfo, priv);

        snprintf (delete_path, sizeof (delete_path),
        "%s/"GLUSTERD_TRASH"/%s.deleted", priv->workdir,
        uuid_utoa (volinfo->volume_id));

        snprintf (trashdir, sizeof (trashdir), "%s/"GLUSTERD_TRASH,
                  priv->workdir);

        ret = sys_mkdir (trashdir, 0777);
        if (ret && errno != EEXIST) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Failed to create trash "
                        "directory");
                ret = -1;
                goto out;
        }

        ret = sys_rename (pathname, delete_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED, "Failed to rename volume "
                        "directory for volume %s", volinfo->volname);
                rename_fail = _gf_true;
                goto out;
        }

        ret = recursive_rmdir (trashdir);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to rmdir: %s",
                                 trashdir);
        }

out:
        if (volinfo->shandle) {
                gf_store_handle_destroy (volinfo->shandle);
                volinfo->shandle = NULL;
        }
        ret = (rename_fail == _gf_true) ? -1: 0;

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

/*TODO: cleanup the duplicate code and implement a generic function for
 * deleting snap/volume depending on the parameter flag */
int32_t
glusterd_store_delete_snap (glusterd_snap_t *snap)
{
        char             pathname[PATH_MAX]    = {0,};
        int32_t          ret                   = 0;
        glusterd_conf_t *priv                  = NULL;
        DIR             *dir                   = NULL;
        struct dirent   *entry                 = NULL;
        struct dirent    scratch[2]            = {{0,},};
        char             path[PATH_MAX]        = {0,};
        char             delete_path[PATH_MAX] = {0,};
        char             trashdir[PATH_MAX]    = {0,};
        struct stat      st                    = {0, };
        xlator_t        *this                  = NULL;
        gf_boolean_t     rename_fail           = _gf_false;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (snap);
        GLUSTERD_GET_SNAP_DIR (pathname, snap, priv);

        snprintf (delete_path, sizeof (delete_path),
        "%s/"GLUSTERD_TRASH"/snap-%s.deleted", priv->workdir,
        uuid_utoa (snap->snap_id));

        snprintf (trashdir, sizeof (trashdir), "%s/"GLUSTERD_TRASH,
                  priv->workdir);

        ret = sys_mkdir (trashdir, 0777);
        if (ret && errno != EEXIST) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Failed to create trash "
                        "directory");
                ret = -1;
                goto out;
        }

        ret = sys_rename (pathname, delete_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED, "Failed to rename snap "
                        "directory %s to %s", pathname, delete_path);
                rename_fail = _gf_true;
                goto out;
        }

        dir = sys_opendir (delete_path);
        if (!dir) {
                gf_msg_debug (this->name, 0, "Failed to open directory %s.",
                                 delete_path);
                ret = 0;
                goto out;
        }

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        while (entry) {
                snprintf (path, PATH_MAX, "%s/%s", delete_path, entry->d_name);
                ret = sys_stat (path, &st);
                if (ret == -1) {
                        gf_msg_debug (this->name, 0, "Failed to stat "
                                "entry %s", path);
                        goto stat_failed;
                }

                if (S_ISDIR (st.st_mode))
                        ret = sys_rmdir (path);
                else
                        ret = sys_unlink (path);

                if (ret) {
                        gf_msg_debug (this->name, 0, " Failed to remove "
                                "%s", path);
                }

                gf_msg_debug (this->name, 0, "%s %s",
                                ret ? "Failed to remove":"Removed",
                                entry->d_name);
stat_failed:
                memset (path, 0, sizeof(path));
                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }

        ret = sys_closedir (dir);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to close dir %s.",
                                delete_path);
        }

        ret = sys_rmdir (delete_path);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to rmdir: %s",
                        delete_path);
        }
        ret = sys_rmdir (trashdir);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to rmdir: %s",
                                 trashdir);
        }

out:
        if (snap->shandle) {
                gf_store_handle_destroy (snap->shandle);
                snap->shandle = NULL;
        }
        ret = (rename_fail == _gf_true) ? -1: 0;

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_store_global_info (xlator_t *this)
{
        int                     ret                     = -1;
        glusterd_conf_t         *conf                   = NULL;
        char                    op_version_str[15]      = {0,};
        char                    path[PATH_MAX]          = {0,};
        gf_store_handle_t       *handle                 = NULL;
        char                    *uuid_str               = NULL;

        conf = this->private;

        uuid_str = gf_strdup (uuid_utoa (MY_UUID));
        if (!uuid_str)
                goto out;

        if (!conf->handle) {
                snprintf (path, PATH_MAX, "%s/%s", conf->workdir,
                          GLUSTERD_INFO_FILE);
                ret = gf_store_handle_new (path, &handle);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STORE_HANDLE_GET_FAIL,
                                "Unable to get store handle");
                        goto out;
                }

                conf->handle = handle;
        } else
                handle = conf->handle;

        /* These options need to be available for all users */
        ret = sys_chmod (handle->path, 0644);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "chmod error for %s",
                        GLUSTERD_INFO_FILE);
                goto out;
        }

        handle->fd = gf_store_mkstemp (handle);
        if (handle->fd <= 0) {
                ret = -1;
                goto out;
        }

        ret = gf_store_save_value (handle->fd, GLUSTERD_STORE_UUID_KEY,
                                   uuid_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_UUID_SET_FAIL,
                        "Storing uuid failed ret = %d", ret);
                goto out;
        }

        snprintf (op_version_str, 15, "%d", conf->op_version);
        ret = gf_store_save_value (handle->fd, GD_OP_VERSION_KEY,
                                   op_version_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_VERS_STORE_FAIL,
                        "Storing op-version failed ret = %d", ret);
                goto out;
        }

        ret = gf_store_rename_tmppath (handle);
out:
        if (handle) {
                if (ret && (handle->fd > 0))
                        gf_store_unlink_tmppath (handle);

                if (handle->fd > 0) {
                        handle->fd = 0;
                }
        }

        if (uuid_str)
                GF_FREE (uuid_str);

        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_GLOBAL_INFO_STORE_FAIL,
                        "Failed to store glusterd global-info");

        return ret;
}

int
glusterd_retrieve_op_version (xlator_t *this, int *op_version)
{
        char                    *op_version_str = NULL;
        glusterd_conf_t         *priv           = NULL;
        int                     ret             = -1;
        int                     tmp_version     = 0;
        char                    *tmp            = NULL;
        char                    path[PATH_MAX]  = {0,};
        gf_store_handle_t       *handle         = NULL;

        priv = this->private;

        if (!priv->handle) {
                snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                          GLUSTERD_INFO_FILE);
                ret = gf_store_handle_retrieve (path, &handle);

                if (ret) {
                        gf_msg_debug (this->name, 0, "Unable to get store "
                                "handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = gf_store_retrieve_value (priv->handle, GD_OP_VERSION_KEY,
                                       &op_version_str);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "No previous op_version present");
                goto out;
        }

        tmp_version = strtol (op_version_str, &tmp, 10);
        if ((tmp_version <= 0) || (tmp && strlen (tmp) > 1)) {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        GD_MSG_UNSUPPORTED_VERSION, "invalid version number");
                goto out;
        }

        *op_version = tmp_version;

        ret = 0;
out:
        if (op_version_str)
                GF_FREE (op_version_str);

        return ret;
}

int
glusterd_retrieve_sys_snap_max_limit (xlator_t *this, uint64_t *limit,
                                      char *key)
{
        char                    *limit_str      = NULL;
        glusterd_conf_t         *priv           = NULL;
        int                     ret             = -1;
        uint64_t                tmp_limit       = 0;
        char                    *tmp            = NULL;
        char                    path[PATH_MAX]  = {0,};
        gf_store_handle_t       *handle         = NULL;

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);
        GF_ASSERT (limit);
        GF_ASSERT (key);

        if (!priv->handle) {
                snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                          GLUSTERD_INFO_FILE);
                ret = gf_store_handle_retrieve (path, &handle);

                if (ret) {
                        gf_msg_debug (this->name, 0, "Unable to get store "
                                "handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = gf_store_retrieve_value (priv->handle,
                                       key,
                                       &limit_str);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "No previous %s present", key);
                goto out;
        }

        tmp_limit = strtoul (limit_str, &tmp, 10);
        if ((tmp_limit <= 0) || (tmp && strlen (tmp) > 1)) {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        GD_MSG_UNSUPPORTED_VERSION, "invalid version number");
                goto out;
        }

        *limit = tmp_limit;

        ret = 0;
out:
        if (limit_str)
                GF_FREE (limit_str);

        return ret;
}

int
glusterd_restore_op_version (xlator_t *this)
{
        glusterd_conf_t *conf           = NULL;
        int              ret            = 0;
        int              op_version     = 0;

        conf = this->private;

        ret = glusterd_retrieve_op_version (this, &op_version);
        if (!ret) {
                if ((op_version < GD_OP_VERSION_MIN) ||
                    (op_version > GD_OP_VERSION_MAX)) {
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_UNSUPPORTED_VERSION,
                                "wrong op-version (%d) retrieved", op_version);
                        ret = -1;
                        goto out;
                }
                conf->op_version = op_version;
                gf_msg ("glusterd", GF_LOG_INFO, 0,
                        GD_MSG_OP_VERS_INFO,
                        "retrieved op-version: %d", conf->op_version);
                goto out;
        }

        /* op-version can be missing from the store file in 2 cases,
         * 1. This is a new install of glusterfs
         * 2. This is an upgrade of glusterfs from a version without op-version
         *    to a version with op-version (eg. 3.3 -> 3.4)
         *
         * Detection of a new install or an upgrade from an older install can be
         * done by checking for the presence of the its peer-id in the store
         * file.  If peer-id is present, the installation is an upgrade else, it
         * is a new install.
         *
         * For case 1, set op-version to GD_OP_VERSION_MAX.
         * For case 2, set op-version to GD_OP_VERSION_MIN.
         */
        ret = glusterd_retrieve_uuid();
        if (ret) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_OP_VERS_SET_INFO,
                        "Detected new install. Setting"
                        " op-version to maximum : %d", GD_OP_VERSION_MAX);
                conf->op_version = GD_OP_VERSION_MAX;
        } else {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_OP_VERS_SET_INFO,
                        "Upgrade detected. Setting"
                        " op-version to minimum : %d", GD_OP_VERSION_MIN);
                conf->op_version = GD_OP_VERSION_MIN;
        }
        ret = 0;
out:
        return ret;
}

int32_t
glusterd_retrieve_uuid ()
{
        char            *uuid_str = NULL;
        int32_t         ret = -1;
        gf_store_handle_t *handle = NULL;
        glusterd_conf_t *priv = NULL;
        xlator_t        *this = NULL;
        char            path[PATH_MAX] = {0,};

        this = THIS;
        priv = this->private;

        if (!priv->handle) {
                snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                          GLUSTERD_INFO_FILE);
                ret = gf_store_handle_retrieve (path, &handle);

                if (ret) {
                        gf_msg_debug (this->name, 0, "Unable to get store"
                                "handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = gf_store_retrieve_value (priv->handle, GLUSTERD_STORE_UUID_KEY,
                                       &uuid_str);

        if (ret) {
                gf_msg_debug (this->name, 0, "No previous uuid is present");
                goto out;
        }

        gf_uuid_parse (uuid_str, priv->uuid);

out:
        GF_FREE (uuid_str);
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_store_retrieve_snapd (glusterd_volinfo_t *volinfo)
{
        int                     ret                     = -1;
        char                    *key                    = NULL;
        char                    *value                  = NULL;
        char                    volpath[PATH_MAX]       = {0,};
        char                    path[PATH_MAX]          = {0,};
        xlator_t                *this                   = NULL;
        glusterd_conf_t         *conf                   = NULL;
        gf_store_iter_t         *iter                   = NULL;
        gf_store_op_errno_t     op_errno                = GD_STORE_SUCCESS;

        this = THIS;
        GF_ASSERT (this);
        conf = THIS->private;
        GF_ASSERT (volinfo);

        if (conf->op_version < GD_OP_VERSION_3_6_0) {
                ret = 0;
                goto out;
        }

        /*
         * This is needed for upgrade situations. Say a volume is created with
         * older version of glusterfs and upgraded to a glusterfs version equal
         * to or greater than GD_OP_VERSION_3_6_0. The older glusterd would not
         * have created the snapd.info file related to snapshot daemon for user
         * serviceable snapshots. So as part of upgrade when the new glusterd
         * starts, as part of restore (restoring the volume to be precise), it
         * tries to snapd related info from snapd.info file. But since there was
         * no such file till now, the restore operation fails. Thus, to prevent
         * it from happening check whether user serviceable snapshots features
         * is enabled before restoring snapd. If its disbaled, then simply
         * exit by returning success (without even checking for the snapd.info).
         */

        if (!dict_get_str_boolean (volinfo->dict, "features.uss", _gf_false)) {
                ret = 0;
                goto out;
        }

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, conf);

        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_VOLUME_SNAPD_INFO_FILE);

        ret = gf_store_handle_retrieve (path, &volinfo->snapd.handle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HANDLE_NULL, "volinfo handle is NULL");
                goto out;
        }

        ret = gf_store_iter_new (volinfo->snapd.handle, &iter);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_GET_FAIL, "Failed to get new store "
                        "iter");
                goto out;
        }

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_GET_FAIL, "Failed to get next store "
                        "iter");
                goto out;
        }

        while (!ret) {
                if (!strncmp (key, GLUSTERD_STORE_KEY_SNAPD_PORT,
                              strlen (GLUSTERD_STORE_KEY_SNAPD_PORT))) {
                        volinfo->snapd.port = atoi (value);
                }

                ret = gf_store_iter_get_next (iter, &key, &value,
                                              &op_errno);
        }

        if (op_errno != GD_STORE_EOF)
                goto out;

        ret = gf_store_iter_destroy (iter);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_DESTROY_FAIL,
                        "Failed to destroy store "
                        "iter");
                goto out;
        }

        ret = 0;

out:
        return ret;
}

int32_t
glusterd_store_retrieve_bricks (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = 0;
        glusterd_brickinfo_t    *brickinfo = NULL;
        gf_store_iter_t         *iter = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        char                    brickdir[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;
        int32_t                 brick_count = 0;
        char                    tmpkey[4096] = {0,};
        gf_store_iter_t         *tmpiter = NULL;
        char                    *tmpvalue = NULL;
        char                     abspath[PATH_MAX] = {0};
        struct pmap_registry    *pmap = NULL;
        xlator_t                *this = NULL;
        int                      brickid = 0;
        gf_store_op_errno_t     op_errno = GD_STORE_SUCCESS;

        GF_ASSERT (volinfo);
        GF_ASSERT (volinfo->volname);

        this = THIS;
        priv = this->private;

        GLUSTERD_GET_BRICK_DIR (brickdir, volinfo, priv);

        ret = gf_store_iter_new (volinfo->shandle, &tmpiter);

        if (ret)
                goto out;

        while (brick_count < volinfo->brick_count) {
                ret = glusterd_brickinfo_new (&brickinfo);

                if (ret)
                        goto out;
                snprintf (tmpkey, sizeof (tmpkey), "%s-%d",
                          GLUSTERD_STORE_KEY_VOL_BRICK,brick_count);
                ret = gf_store_iter_get_matching (tmpiter, tmpkey, &tmpvalue);
                snprintf (path, sizeof (path), "%s/%s", brickdir, tmpvalue);

                GF_FREE (tmpvalue);

                tmpvalue = NULL;

                ret = gf_store_handle_retrieve (path, &brickinfo->shandle);

                if (ret)
                        goto out;

                ret = gf_store_iter_new (brickinfo->shandle, &iter);

                if (ret)
                        goto out;

                ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, op_errno,
                                GD_MSG_STORE_ITER_GET_FAIL, "Unable to iterate "
                                "the store for brick: %s", path);
                        goto out;
                }
                while (!ret) {
                        if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_HOSTNAME,
                                      strlen (GLUSTERD_STORE_KEY_BRICK_HOSTNAME))) {
                                strncpy (brickinfo->hostname, value, 1024);
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_PATH,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_PATH))) {
                                strncpy (brickinfo->path, value,
                                         sizeof (brickinfo->path));
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_REAL_PATH,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_REAL_PATH))) {
                                strncpy (brickinfo->real_path, value,
                                         sizeof (brickinfo->real_path));
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_PORT,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_PORT))) {
                                gf_string2int (value, &brickinfo->port);

                                if (brickinfo->port < priv->base_port) {
                                        /* This is required to adhere to the
                                           IANA standards */
                                        brickinfo->port = 0;
                                } else {
                                        /* This is required to have proper ports
                                           assigned to bricks after restart */
                                        pmap = pmap_registry_get (THIS);
                                        if (pmap->last_alloc <= brickinfo->port)
                                                pmap->last_alloc =
                                                        brickinfo->port + 1;
                                }
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_RDMA_PORT,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_RDMA_PORT))) {
                                gf_string2int (value, &brickinfo->rdma_port);

                                if (brickinfo->rdma_port < priv->base_port) {
                                        /* This is required to adhere to the
                                           IANA standards */
                                        brickinfo->rdma_port = 0;
                                } else {
                                        /* This is required to have proper ports
                                           assigned to bricks after restart */
                                        pmap = pmap_registry_get (THIS);
                                        if (pmap->last_alloc <=
                                            brickinfo->rdma_port)
                                                pmap->last_alloc =
                                                        brickinfo->rdma_port +1;
                                }

                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_DECOMMISSIONED,
                                             strlen (GLUSTERD_STORE_KEY_BRICK_DECOMMISSIONED))) {
                                gf_string2int (value, &brickinfo->decommissioned);
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_DEVICE_PATH,
                                             strlen (GLUSTERD_STORE_KEY_BRICK_DEVICE_PATH))) {
                                strncpy (brickinfo->device_path, value,
                                         sizeof (brickinfo->device_path));
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_MOUNT_DIR,
                                             strlen (GLUSTERD_STORE_KEY_BRICK_MOUNT_DIR))) {
                                strncpy (brickinfo->mount_dir, value,
                                         sizeof (brickinfo->mount_dir));
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_SNAP_STATUS,
                                             strlen (GLUSTERD_STORE_KEY_BRICK_SNAP_STATUS))) {
                                gf_string2int (value, &brickinfo->snap_status);
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_FSTYPE,
                                             strlen (GLUSTERD_STORE_KEY_BRICK_FSTYPE))) {
                                strncpy (brickinfo->fstype, value,
                                         sizeof (brickinfo->fstype));
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_MNTOPTS,
                                             strlen (GLUSTERD_STORE_KEY_BRICK_MNTOPTS))) {
                                strncpy (brickinfo->mnt_opts, value,
                                         sizeof (brickinfo->mnt_opts));
                        } else if (!strncmp (key,
                                    GLUSTERD_STORE_KEY_BRICK_VGNAME,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_VGNAME))) {
                                strncpy (brickinfo->vg, value,
                                         sizeof (brickinfo->vg));
                        } else if (!strcmp(key, GLUSTERD_STORE_KEY_BRICK_ID)) {
                                strncpy (brickinfo->brick_id, value,
                                         sizeof (brickinfo->brick_id));
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_UNKNOWN_KEY, "Unknown key: %s",
                                        key);
                        }

                        GF_FREE (key);
                        GF_FREE (value);
                        key = NULL;
                        value = NULL;

                        ret = gf_store_iter_get_next (iter, &key, &value,
                                                      &op_errno);
                }

                if (op_errno != GD_STORE_EOF) {
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                GD_MSG_PARSE_BRICKINFO_FAIL,
                                "Error parsing brickinfo: "
                                "op_errno=%d", op_errno);
                        goto out;
                }
                ret = gf_store_iter_destroy (iter);

                if (ret)
                        goto out;

                if (brickinfo->brick_id[0] == '\0') {
                        /* This is an old volume upgraded to op_version 4 */
                       GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO (brickinfo, volinfo,
                                                             brickid++);
                }
                /* Populate brickinfo->real_path for normal volumes, for
                 * snapshot or snapshot restored volume this would be done post
                 * creating the brick mounts
                 */
                if (brickinfo->real_path[0] == '\0' && !volinfo->is_snap_volume
                    && gf_uuid_is_null (volinfo->restored_from_snap)) {
                        /* By now if the brick is a local brick then it will be
                         * able to resolve which is the only thing we want now
                         * for checking  whether the brickinfo->uuid matches
                         * with MY_UUID for realpath check. Hence do not handle
                         * error
                         */
                        (void)glusterd_resolve_brick (brickinfo);
                        if (!gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
                                if (!realpath (brickinfo->path, abspath)) {
                                        gf_msg (this->name, GF_LOG_CRITICAL,
                                                errno,
                                                GD_MSG_BRICKINFO_CREATE_FAIL,
                                                "realpath() failed for brick %s"
                                                ". The underlying file system "
                                                "may be in bad state",
                                                brickinfo->path);
                                        ret = -1;
                                        goto out;
                                }
                                strncpy (brickinfo->real_path, abspath,
                                         strlen(abspath));
                        }
                }
                cds_list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick_count++;
        }

        assign_brick_groups (volinfo);
        ret = gf_store_iter_destroy (tmpiter);
        if (ret)
                goto out;
out:
        gf_msg_debug (this->name, 0, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_store_retrieve_node_state (glusterd_volinfo_t *volinfo)
{
        int32_t              ret               = -1;
        gf_store_iter_t     *iter              = NULL;
        char                *key               = NULL;
        char                *value             = NULL;
        char                *dup_value         = NULL;
        char                 volpath[PATH_MAX] = {0,};
        glusterd_conf_t     *priv              = NULL;
        char                 path[PATH_MAX]    = {0,};
        gf_store_op_errno_t  op_errno          = GD_STORE_SUCCESS;
        dict_t              *tmp_dict          = NULL;
        xlator_t            *this              = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (volinfo);

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, priv);
        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_NODE_STATE_FILE);

        ret = gf_store_handle_retrieve (path, &volinfo->node_state_shandle);
        if (ret)
                goto out;

        ret = gf_store_iter_new (volinfo->node_state_shandle, &iter);

        if (ret)
                goto out;

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        if (ret)
                goto out;

        while (ret == 0) {
                if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG,
                              strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG))) {
                        volinfo->rebal.defrag_cmd = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_STATUS,
                           strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_STATUS))) {
                                volinfo->rebal.defrag_status = atoi (value);
                } else if (!strncmp (key, GF_REBALANCE_TID_KEY,
                                     strlen (GF_REBALANCE_TID_KEY))) {
                        gf_uuid_parse (value, volinfo->rebal.rebalance_id);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_DEFRAG_OP,
                                     strlen (GLUSTERD_STORE_KEY_DEFRAG_OP))) {
                        volinfo->rebal.op = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_REB_FILES,
                           strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_REB_FILES))) {
                        volinfo->rebal.rebalance_files = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_SIZE,
                           strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_SIZE))) {
                                volinfo->rebal.rebalance_data = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_SCANNED,
                              strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_SCANNED))) {
                        volinfo->rebal.lookedup_files = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_FAILURES,
                           strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_FAILURES))) {
                        volinfo->rebal.rebalance_failures = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_SKIPPED,
                           strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_SKIPPED))) {
                                volinfo->rebal.skipped_files = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DEFRAG_RUN_TIME,
                           strlen (GLUSTERD_STORE_KEY_VOL_DEFRAG_RUN_TIME))) {
                                volinfo->rebal.rebalance_time = atoi (value);

                /* if none of the above keys match then its related to tier
                 * so we get the values and store it on volinfo->tier
                 */
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_TIER_STATUS,
                           strlen (GLUSTERD_STORE_KEY_VOL_TIER_STATUS))) {
                                volinfo->tier.defrag_status = atoi (value);
                } else if (!strncmp (key, GF_TIER_TID_KEY,
                                     strlen (GF_TIER_TID_KEY))) {
                        gf_uuid_parse (value, volinfo->tier.rebalance_id);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_TIER_DETACH_OP,
                           strlen (GLUSTERD_STORE_KEY_TIER_DETACH_OP))) {
                        volinfo->tier.op = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_MIGRATED_FILES,
                           strlen (GLUSTERD_STORE_KEY_VOL_MIGRATED_FILES))) {
                        volinfo->tier.rebalance_files = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_MIGRATED_SIZE,
                           strlen (GLUSTERD_STORE_KEY_VOL_MIGRATED_SIZE))) {
                                volinfo->tier.rebalance_data = atoi (value);
                } else if (!strncmp (key,
                           GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SCANNED,
                           strlen (GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SCANNED))) {
                        volinfo->tier.lookedup_files = atoi (value);
                } else if (!strncmp (key,
                           GLUSTERD_STORE_KEY_VOL_MIGRATIONS_FAILURES,
                           strlen (GLUSTERD_STORE_KEY_VOL_MIGRATIONS_FAILURES))) {
                        volinfo->tier.rebalance_failures = atoi (value);
                } else if (!strncmp (key,
                           GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SKIPPED,
                           strlen (GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SKIPPED))) {
                                volinfo->tier.skipped_files = atoi (value);
                } else if (!strncmp (key,
                           GLUSTERD_STORE_KEY_VOL_MIGRATION_RUN_TIME,
                           strlen (GLUSTERD_STORE_KEY_VOL_MIGRATION_RUN_TIME))) {
                                volinfo->tier.rebalance_time = atoi (value);
                } else {
                        if (!tmp_dict) {
                                tmp_dict = dict_new ();
                                if (!tmp_dict) {
                                        ret = -1;
                                        goto out;
                                }
                        }
                        dup_value = gf_strdup (value);
                        if (!dup_value) {
                                ret = -1;
                                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                        GD_MSG_NO_MEMORY,
                                        "Failed to strdup value string");
                                goto out;
                        }
                        ret = dict_set_str (tmp_dict, key, dup_value);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Error setting data in rebal "
                                        "dict.");
                                goto out;
                        }
                        dup_value = NULL;
                }

                GF_FREE (key);
                GF_FREE (value);
                key = NULL;
                value = NULL;

                ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        }
        if (tmp_dict) {
                if (volinfo->type == GF_CLUSTER_TYPE_TIER)
                        volinfo->tier.dict = dict_ref (tmp_dict);
                else
                        volinfo->rebal.dict = dict_ref (tmp_dict);
        }

        if (op_errno != GD_STORE_EOF) {
                ret = -1;
                goto out;
        }

        ret = gf_store_iter_destroy (iter);

        if (ret)
                goto out;

out:
        if (dup_value)
                GF_FREE (dup_value);
        if (ret) {
                if (volinfo->rebal.dict)
                        dict_unref (volinfo->rebal.dict);
                else if (volinfo->tier.dict)
                        dict_unref (volinfo->tier.dict);
        }
        if (tmp_dict)
                dict_unref (tmp_dict);

        gf_msg_trace (this->name, 0, "Returning with %d", ret);

        return ret;
}


int
glusterd_store_update_volinfo (glusterd_volinfo_t *volinfo)
{
        int                     ret                     = -1;
        int                     exists                  = 0;
        char                    *key                    = NULL;
        char                    *value                  = NULL;
        char                    volpath[PATH_MAX]       = {0,};
        char                    path[PATH_MAX]          = {0,};
        xlator_t                *this                   = NULL;
        glusterd_conf_t         *conf                   = NULL;
        gf_store_iter_t         *iter                   = NULL;
        gf_store_op_errno_t     op_errno                = GD_STORE_SUCCESS;

        this = THIS;
        GF_ASSERT (this);
        conf = THIS->private;
        GF_ASSERT (volinfo);

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, conf);

        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_VOLUME_INFO_FILE);

        ret = gf_store_handle_retrieve (path, &volinfo->shandle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HANDLE_NULL, "volinfo handle is NULL");
                goto out;
        }

        ret = gf_store_iter_new (volinfo->shandle, &iter);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_GET_FAIL, "Failed to get new store "
                        "iter");
                goto out;
        }

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_GET_FAIL, "Failed to get next store "
                        "iter");
                goto out;
        }

        while (!ret) {
                gf_msg_debug (this->name, 0, "key = %s value = %s", key, value);
                if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_TYPE,
                              strlen (GLUSTERD_STORE_KEY_VOL_TYPE))) {
                        volinfo->type = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_COUNT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_COUNT))) {
                        volinfo->brick_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_STATUS,
                                     strlen (GLUSTERD_STORE_KEY_VOL_STATUS))) {
                        volinfo->status = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_VERSION,
                                     strlen (GLUSTERD_STORE_KEY_VOL_VERSION))) {
                        volinfo->version = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_PORT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_PORT))) {
                        volinfo->port = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_SUB_COUNT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_SUB_COUNT))) {
                        volinfo->sub_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_STRIPE_CNT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_STRIPE_CNT))) {
                        volinfo->stripe_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_REPLICA_CNT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_REPLICA_CNT))) {
                        volinfo->replica_count = atoi (value);
                } else if (!strcmp (key, GLUSTERD_STORE_KEY_VOL_ARBITER_CNT)) {
                        volinfo->arbiter_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_DISPERSE_CNT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_DISPERSE_CNT))) {
                        volinfo->disperse_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_REDUNDANCY_CNT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_REDUNDANCY_CNT))) {
                        volinfo->redundancy_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_TRANSPORT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_TRANSPORT))) {
                        volinfo->transport_type = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_ID,
                                     strlen (GLUSTERD_STORE_KEY_VOL_ID))) {
                        ret = gf_uuid_parse (value, volinfo->volume_id);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_UUID_PARSE_FAIL,
                                        "failed to parse uuid");

                } else if (!strncmp (key, GLUSTERD_STORE_KEY_USERNAME,
                                     strlen (GLUSTERD_STORE_KEY_USERNAME))) {

                        glusterd_auth_set_username (volinfo, value);

                } else if (!strncmp (key, GLUSTERD_STORE_KEY_PASSWORD,
                                     strlen (GLUSTERD_STORE_KEY_PASSWORD))) {

                        glusterd_auth_set_password (volinfo, value);

                } else if (strstr (key, "slave")) {
                        ret = dict_set_dynstr (volinfo->gsync_slaves, key,
                                                gf_strdup (value));
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "Error in "
                                        "dict_set_str");
                                goto out;
                        }
                        gf_msg_debug (this->name, 0, "Parsed as "GEOREP" "
                                " slave:key=%s,value:%s", key, value);

                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_OP_VERSION,
                                strlen (GLUSTERD_STORE_KEY_VOL_OP_VERSION))) {
                        volinfo->op_version = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_CLIENT_OP_VERSION,
                                strlen (GLUSTERD_STORE_KEY_VOL_CLIENT_OP_VERSION))) {
                        volinfo->client_op_version = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_CAPS,
                                     strlen (GLUSTERD_STORE_KEY_VOL_CAPS))) {
                        volinfo->caps = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT,
                                strlen (GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT))) {
                        volinfo->snap_max_hard_limit = (uint64_t) atoll (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_RESTORED_SNAP,
                              strlen (GLUSTERD_STORE_KEY_VOL_RESTORED_SNAP))) {
                        ret = gf_uuid_parse (value, volinfo->restored_from_snap);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_UUID_PARSE_FAIL,
                                        "failed to parse restored snap's uuid");
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_PARENT_VOLNAME,
                                strlen (GLUSTERD_STORE_KEY_PARENT_VOLNAME))) {
                        strncpy (volinfo->parent_volname, value,
                                 sizeof(volinfo->parent_volname) - 1);
                } else if (!strncmp (key, GF_TIER_ENABLED,
                                     strlen (GF_TIER_ENABLED))) {
                        volinfo->is_tier_enabled = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_COLD_COUNT,
                                     strlen (key))) {
                        volinfo->tier_info.cold_brick_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_COLD_REPLICA_COUNT,
                                     strlen (key))) {
                        volinfo->tier_info.cold_replica_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_COLD_DISPERSE_COUNT,
                                     strlen (key))) {
                        volinfo->tier_info.cold_disperse_count = atoi (value);
                } else if (!strncmp (key,
                                     GLUSTERD_STORE_KEY_COLD_REDUNDANCY_COUNT,
                                     strlen (key))) {
                        volinfo->tier_info.cold_redundancy_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_HOT_COUNT,
                                     strlen (key))) {
                        volinfo->tier_info.hot_brick_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_HOT_REPLICA_COUNT,
                                     strlen (key))) {
                        volinfo->tier_info.hot_replica_count = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_HOT_TYPE,
                                     strlen (key))) {
                        volinfo->tier_info.hot_type = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_COLD_TYPE,
                                     strlen (key))) {
                        volinfo->tier_info.cold_type = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_QUOTA_VERSION,
                            strlen (GLUSTERD_STORE_KEY_VOL_QUOTA_VERSION))) {
                        volinfo->quota_xattr_version = atoi (value);
                } else {

                        if (is_key_glusterd_hooks_friendly (key)) {
                                exists = 1;

                        } else  {
                                exists = glusterd_check_option_exists (key,
                                                                       NULL);
                        }

                        switch (exists) {
                        case -1:
                                ret = -1;
                                goto out;

                        case 0:
                                 /*Ignore GLUSTERD_STORE_KEY_VOL_BRICK since
                                  glusterd_store_retrieve_bricks gets it later*/
                                if (!strstr (key, GLUSTERD_STORE_KEY_VOL_BRICK))
                                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                                GD_MSG_UNKNOWN_KEY,
                                                "Unknown key: %s", key);
                                break;

                        case 1:
                                /*The following strcmp check is to ensure that
                                 * glusterd does not restore the quota limits
                                 * into volinfo->dict post upgradation from 3.3
                                 * to 3.4 as the same limits will now be stored
                                 * in xattrs on the respective directories.
                                 */
                                if (!strcmp (key, "features.limit-usage"))
                                        break;
                                ret = dict_set_str(volinfo->dict, key,
                                                   gf_strdup (value));
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_SET_FAILED,
                                                "Error in "
                                                "dict_set_str");
                                        goto out;
                                }
                                gf_msg_debug (this->name, 0, "Parsed as Volume-"
                                        "set:key=%s,value:%s", key, value);
                                break;
                        }
                }

                GF_FREE (key);
                GF_FREE (value);
                key = NULL;
                value = NULL;

                ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        }

        /* backward compatibility */
        {

                switch (volinfo->type) {

                        case GF_CLUSTER_TYPE_NONE:
                                volinfo->stripe_count  = 1;
                                volinfo->replica_count = 1;
                        break;

                        case GF_CLUSTER_TYPE_STRIPE:
                                volinfo->stripe_count  = volinfo->sub_count;
                                volinfo->replica_count = 1;
                        break;

                        case GF_CLUSTER_TYPE_REPLICATE:
                                volinfo->stripe_count  = 1;
                                volinfo->replica_count = volinfo->sub_count;
                        break;

                        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                                /* Introduced in 3.3 */
                                GF_ASSERT (volinfo->stripe_count > 0);
                                GF_ASSERT (volinfo->replica_count > 0);
                        break;

                        case GF_CLUSTER_TYPE_DISPERSE:
                                GF_ASSERT (volinfo->disperse_count > 0);
                                GF_ASSERT (volinfo->redundancy_count > 0);
                        break;

                        case GF_CLUSTER_TYPE_TIER:
                        if (volinfo->tier_info.cold_type ==
                                        GF_CLUSTER_TYPE_DISPERSE)
                                volinfo->tier_info.cold_dist_leaf_count
                                        = volinfo->disperse_count;
                        else
                                volinfo->tier_info.cold_dist_leaf_count
                                        = glusterd_calc_dist_leaf_count (
                                                        volinfo->tier_info.
                                                        cold_replica_count,
                                                        1);

                        break;

                        default:
                                GF_ASSERT (0);
                        break;
                }

                volinfo->dist_leaf_count = glusterd_get_dist_leaf_count (volinfo);

                volinfo->subvol_count = (volinfo->brick_count /
                                         volinfo->dist_leaf_count);

                /* Only calculate volume op-versions if they are not found */
                if (!volinfo->op_version && !volinfo->client_op_version)
                        gd_update_volume_op_versions (volinfo);
        }

        if (op_errno != GD_STORE_EOF)
                goto out;

        ret = gf_store_iter_destroy (iter);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_DESTROY_FAIL,
                        "Failed to destroy store "
                        "iter");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

glusterd_volinfo_t*
glusterd_store_retrieve_volume (char *volname, glusterd_snap_t *snap)
{
        int32_t                    ret                  = -1;
        glusterd_volinfo_t        *volinfo              = NULL;
        glusterd_volinfo_t        *origin_volinfo       = NULL;
        glusterd_conf_t           *priv                 = NULL;
        xlator_t                  *this                 = NULL;


        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (volname);

        ret = glusterd_volinfo_new (&volinfo);
        if (ret)
                goto out;

        strncpy (volinfo->volname, volname, GD_VOLUME_NAME_MAX);
        volinfo->snapshot = snap;
        if (snap)
                volinfo->is_snap_volume = _gf_true;

        ret = glusterd_store_update_volinfo (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_UPDATE_FAIL, "Failed to update volinfo "
                        "for %s volume", volname);
                goto out;
        }

        ret = glusterd_store_retrieve_bricks (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_retrieve_snapd (volinfo);
        if (ret)
                goto out;

        ret = glusterd_compute_cksum (volinfo, _gf_false);
        if (ret)
                goto out;

        ret = glusterd_store_retrieve_quota_version (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_create_quota_conf_sh_on_absence (volinfo);
        if (ret)
                goto out;

        ret = glusterd_compute_cksum (volinfo, _gf_true);
        if (ret)
                goto out;

        ret = glusterd_store_save_quota_version_and_cksum (volinfo);
        if (ret)
                goto out;


        if (!snap) {
                glusterd_list_add_order (&volinfo->vol_list, &priv->volumes,
                                         glusterd_compare_volume_name);

        } else {
                ret = glusterd_volinfo_find (volinfo->parent_volname,
                                             &origin_volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_GET_FAIL, "Parent volinfo "
                                "not found for %s volume", volname);
                        goto out;
                }
                glusterd_list_add_snapvol (origin_volinfo, volinfo);
        }

out:
        if (ret) {
                if (volinfo)
                        glusterd_volinfo_unref (volinfo);
                volinfo = NULL;
        }

        gf_msg_trace (this->name, 0, "Returning with %d", ret);

        return volinfo;
}

static void
glusterd_store_set_options_path (glusterd_conf_t *conf, char *path, size_t len)
{
        snprintf (path, len, "%s/options", conf->workdir);
}

int
_store_global_opts (dict_t *this, char *key, data_t *value, void *data)
{
        gf_store_handle_t *shandle = data;

        gf_store_save_value (shandle->fd, key, (char*)value->data);
        return 0;
}

int32_t
glusterd_store_options (xlator_t *this, dict_t *opts)
{
        gf_store_handle_t               *shandle = NULL;
        glusterd_conf_t                 *conf = NULL;
        char                            path[PATH_MAX] = {0};
        int                             fd = -1;
        int32_t                         ret = -1;

        conf = this->private;
        glusterd_store_set_options_path (conf, path, sizeof (path));

        ret = gf_store_handle_new (path, &shandle);
        if (ret)
                goto out;

        fd = gf_store_mkstemp (shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        shandle->fd = fd;
        dict_foreach (opts, _store_global_opts, shandle);
        shandle->fd = 0;
        ret = gf_store_rename_tmppath (shandle);
        if (ret)
                goto out;
out:
        if ((ret < 0) && (fd > 0))
                gf_store_unlink_tmppath (shandle);
        gf_store_handle_destroy (shandle);
        return ret;
}

int32_t
glusterd_store_retrieve_options (xlator_t *this)
{
        char                            path[PATH_MAX] = {0};
        glusterd_conf_t                 *conf = NULL;
        gf_store_handle_t               *shandle = NULL;
        gf_store_iter_t                 *iter = NULL;
        char                            *key = NULL;
        char                            *value = NULL;
        gf_store_op_errno_t             op_errno = 0;
        int                             ret = -1;

        conf = this->private;
        glusterd_store_set_options_path (conf, path, sizeof (path));

        ret = gf_store_handle_retrieve (path, &shandle);
        if (ret)
                goto out;

        ret = gf_store_iter_new (shandle, &iter);
        if (ret)
                goto out;

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        while (!ret) {
                ret = dict_set_dynstr (conf->opts, key, value);
                if (ret) {
                        GF_FREE (key);
                        GF_FREE (value);
                        goto out;
                }
                GF_FREE (key);
                key = NULL;
                value = NULL;

                ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        }
        if (op_errno != GD_STORE_EOF)
                goto out;
        ret = 0;
out:
        gf_store_iter_destroy (iter);
        gf_store_handle_destroy (shandle);
        return ret;
}

int32_t
glusterd_store_retrieve_volumes (xlator_t  *this, glusterd_snap_t *snap)
{
        int32_t                ret              = -1;
        char                   path[PATH_MAX]   = {0,};
        glusterd_conf_t       *priv             = NULL;
        DIR                   *dir              = NULL;
        struct dirent         *entry            = NULL;
        struct dirent          scratch[2]       = {{0,},};
        glusterd_volinfo_t    *volinfo          = NULL;
        struct stat            st               = {0,};
        char                   entry_path[PATH_MAX]   = {0,};

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        if (snap)
                snprintf (path, PATH_MAX, "%s/snaps/%s", priv->workdir,
                          snap->snapname);
        else
                snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                          GLUSTERD_VOLUME_DIR_PREFIX);

        dir = sys_opendir (path);

        if (!dir) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED, "Unable to open dir %s", path);
                goto out;
        }

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);

        while (entry) {
                if (snap && ((!strcmp (entry->d_name, "geo-replication")) ||
                             (!strcmp (entry->d_name, "info"))))
                        goto next;

                snprintf (entry_path, PATH_MAX, "%s/%s", path, entry->d_name);
                ret = sys_lstat (entry_path, &st);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_INVALID_ENTRY,
                                        "Failed to stat entry %s : %s", path,
                                        strerror (errno));
                        goto next;
                }

                if (!S_ISDIR (st.st_mode)) {
                        gf_msg_debug (this->name, 0, "%s is not a valid volume"
                                      , entry->d_name);
                        goto next;
                }

                volinfo = glusterd_store_retrieve_volume (entry->d_name, snap);
                if (!volinfo) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOL_RESTORE_FAIL, "Unable to restore "
                                "volume: %s", entry->d_name);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_store_retrieve_node_state (volinfo);
                if (ret) {
                        /* Backward compatibility */
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_NEW_NODE_STATE_CREATION,
                                "Creating a new node_state "
                                "for volume: %s.", entry->d_name);
                        glusterd_store_create_nodestate_sh_on_absence (volinfo);
                        ret = glusterd_store_perform_node_state_store (volinfo);

                }
next:
                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }

        ret = 0;

out:
        if (dir)
                sys_closedir (dir);
        gf_msg_debug (this->name, 0, "Returning with %d", ret);

        return ret;
}

/* Figure out the brick mount path, from the brick path */
int32_t
glusterd_find_brick_mount_path (char *brick_path, char **brick_mount_path)
{
        char                    *ptr                 = NULL;
        int32_t                  ret                 = -1;
        xlator_t                *this                = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brick_path);
        GF_ASSERT (brick_mount_path);

        *brick_mount_path = gf_strdup (brick_path);
        if (!*brick_mount_path) {
                ret = -1;
                goto out;
        }

        /* Finding the pointer to the end of
         * /var/run/gluster/snaps/<snap-uuid>
         */
        ptr = strstr (*brick_mount_path, "brick");
        if (!ptr) {
                /* Snapshot bricks must have brick num as part
                 * of the brickpath
                 */
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY,
                        "Invalid brick path(%s)", brick_path);
                ret = -1;
                goto out;
        }

        /* Moving the pointer to the end of
         * /var/run/gluster/snaps/<snap-uuid>/<brick_num>
         * and assigning '\0' to it.
         */
        while ((*ptr != '\0') && (*ptr != '/'))
                ptr++;

        if (*ptr == '/') {
                *ptr = '\0';
        }

        ret = 0;
out:
        if (ret && *brick_mount_path) {
                GF_FREE (*brick_mount_path);
                *brick_mount_path = NULL;
        }
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

/* Check if brick_mount_path is already mounted. If not, mount the device_path
 * at the brick_mount_path
 */
int32_t
glusterd_mount_brick_paths (char *brick_mount_path,
                            glusterd_brickinfo_t *brickinfo)
{
        int32_t                  ret                 = -1;
        runner_t                 runner              = {0, };
        char                     buff [PATH_MAX]     = {0, };
        struct mntent            save_entry          = {0, };
        struct mntent           *entry               = NULL;
        xlator_t                *this                = NULL;
        glusterd_conf_t         *priv                = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (brick_mount_path);
        GF_ASSERT (brickinfo);

        priv = this->private;
        GF_ASSERT (priv);

        /* Check if the brick_mount_path is already mounted */
        entry = glusterd_get_mnt_entry_info (brick_mount_path, buff,
                                             sizeof (buff), &save_entry);
        if (entry) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_ALREADY_MOUNTED,
                        "brick_mount_path (%s) already mounted.",
                        brick_mount_path);
                ret = 0;
                goto out;
        }

        /* TODO RHEL 6.5 has the logical volumes inactive by default
         * on reboot. Hence activating the logical vol. Check behaviour
         * on other systems
         */
        /* Activate the snapshot */
        runinit (&runner);
        runner_add_args (&runner, "lvchange", "-ay", brickinfo->device_path,
                         NULL);
        ret = runner_run (&runner);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_SNAP_ACTIVATE_FAIL,
                        "Failed to activate %s.",
                        brickinfo->device_path);
                goto out;
        } else
                gf_msg_debug (this->name, 0,
                        "Activating %s successful", brickinfo->device_path);

        /* Mount the snapshot */
        ret = glusterd_mount_lvm_snapshot (brickinfo, brick_mount_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_MOUNT_FAIL,
                        "Failed to mount lvm snapshot.");
                goto out;
        }

out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

static int32_t
glusterd_recreate_vol_brick_mounts (xlator_t  *this,
                                    glusterd_volinfo_t *volinfo)
{
        char                    *brick_mount_path    = NULL;
        glusterd_brickinfo_t    *brickinfo           = NULL;
        int32_t                  ret                 = -1;
        struct stat              st_buf              = {0, };
        char                     abspath[PATH_MAX]   = {0};

        GF_ASSERT (this);
        GF_ASSERT (volinfo);

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                /* If the brick is not of this node, or its
                 * snapshot is pending, or the brick is not
                 * a snapshotted brick, we continue
                */
                if ((gf_uuid_compare (brickinfo->uuid, MY_UUID)) ||
                    (brickinfo->snap_status == -1) ||
                    (strlen(brickinfo->device_path) == 0))
                        continue;

                /* Fetch the brick mount path from the brickinfo->path */
                ret = glusterd_find_brick_mount_path (brickinfo->path,
                                                      &brick_mount_path);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRK_MNTPATH_GET_FAIL,
                                "Failed to find brick_mount_path for %s",
                                brickinfo->path);
                        goto out;
                }

                /* Check if the brickinfo path is present.
                 * If not create the brick_mount_path */
                ret = sys_lstat (brickinfo->path, &st_buf);
                if (ret) {
                        if (errno == ENOENT) {
                                ret = mkdir_p (brick_mount_path, 0777,
                                               _gf_true);
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                GD_MSG_CREATE_DIR_FAILED,
                                                "Failed to create %s. ",
                                                 brick_mount_path);
                                        goto out;
                                }
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_FILE_OP_FAILED,
                                        "Brick Path(%s) not valid. ",
                                         brickinfo->path);
                                goto out;
                        }
                }

                /* Check if brick_mount_path is already mounted.
                 * If not, mount the device_path at the brick_mount_path */
                ret = glusterd_mount_brick_paths (brick_mount_path, brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRK_MNTPATH_MOUNT_FAIL,
                                "Failed to mount brick_mount_path");
                }
                if (!gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
                        if (brickinfo->real_path[0] == '\0') {
                                if (!realpath (brickinfo->path, abspath)) {
                                        gf_msg (this->name, GF_LOG_CRITICAL,
                                                errno,
                                                GD_MSG_BRICKINFO_CREATE_FAIL,
                                                "realpath() failed for brick %s"
                                                ". The underlying file system "
                                                "may be in bad state",
                                                brickinfo->path);
                                        ret = -1;
                                        goto out;
                                }
                                strncpy (brickinfo->real_path, abspath,
                                         strlen(abspath));
                        }
                }

                if (brick_mount_path) {
                        GF_FREE (brick_mount_path);
                        brick_mount_path = NULL;
                }
        }

        ret = 0;
out:
        if (ret && brick_mount_path)
                GF_FREE (brick_mount_path);

        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_resolve_snap_bricks (xlator_t  *this, glusterd_snap_t *snap)
{
        int32_t                 ret = -1;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;

        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, snap, out);

        cds_list_for_each_entry (volinfo, &snap->volumes, vol_list) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        ret = glusterd_resolve_brick (brickinfo);
                        if (ret) {
                                gf_event (EVENT_BRICKPATH_RESOLVE_FAILED,
                                          "peer=%s;volume=%s;brick=%s",
                                          brickinfo->hostname, volinfo->volname,
                                          brickinfo->path);
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_RESOLVE_BRICK_FAIL,
                                        "resolve brick failed in restore");
                                goto out;
                        }
                }
        }

        ret = 0;

out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);

        return ret;
}

int
glusterd_store_update_snap (glusterd_snap_t *snap)
{
        int                     ret = -1;
        char                    *key                    = NULL;
        char                    *value                  = NULL;
        char                    snappath[PATH_MAX]      = {0,};
        char                    path[PATH_MAX]          = {0,};
        xlator_t                *this                   = NULL;
        glusterd_conf_t         *conf                   = NULL;
        gf_store_iter_t         *iter                   = NULL;
        gf_store_op_errno_t     op_errno                = GD_STORE_SUCCESS;

        this = THIS;
        conf = this->private;
        GF_ASSERT (snap);

        GLUSTERD_GET_SNAP_DIR (snappath, snap, conf);

        snprintf (path, sizeof (path), "%s/%s", snappath,
                  GLUSTERD_SNAP_INFO_FILE);

        ret = gf_store_handle_retrieve (path, &snap->shandle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_HANDLE_NULL, "snap handle is NULL");
                goto out;
        }

        ret = gf_store_iter_new (snap->shandle, &iter);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_GET_FAIL, "Failed to get new store "
                        "iter");
                goto out;
        }

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_GET_FAIL, "Failed to get next store "
                        "iter");
                goto out;
        }

        while (!ret) {
                gf_msg_debug (this->name, 0, "key = %s value = %s",
                        key, value);

                if (!strncmp (key, GLUSTERD_STORE_KEY_SNAP_ID,
                                     strlen (GLUSTERD_STORE_KEY_SNAP_ID))) {
                        ret = gf_uuid_parse (value, snap->snap_id);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_UUID_PARSE_FAIL,
                                        "Failed to parse uuid");
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_SNAP_RESTORED,
                                   strlen (GLUSTERD_STORE_KEY_SNAP_RESTORED))) {
                        snap->snap_restored = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_SNAP_STATUS,
                                     strlen (GLUSTERD_STORE_KEY_SNAP_STATUS))) {
                        snap->snap_status = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_SNAP_DESC,
                                     strlen (GLUSTERD_STORE_KEY_SNAP_DESC))) {
                        snap->description = gf_strdup (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_SNAP_TIMESTAMP,
                                  strlen (GLUSTERD_STORE_KEY_SNAP_TIMESTAMP))) {
                        snap->time_stamp = atoi (value);
                }

                GF_FREE (key);
                GF_FREE (value);
                key = NULL;
                value = NULL;

                ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        }

        if (op_errno != GD_STORE_EOF)
                goto out;

        ret = gf_store_iter_destroy (iter);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_ITER_DESTROY_FAIL,
                        "Failed to destroy store "
                        "iter");
        }

out:
        return ret;
}

int32_t
glusterd_store_retrieve_snap (char *snapname)
{
        int32_t                ret      = -1;
        glusterd_snap_t       *snap     = NULL;
        glusterd_conf_t       *priv     = NULL;
        xlator_t              *this     = NULL;

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (snapname);

        snap = glusterd_new_snap_object ();
        if (!snap) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_OBJECT_STORE_FAIL, "Failed to create "
                        " snap object");
                goto out;
        }

        strncpy (snap->snapname, snapname, strlen(snapname));
        ret = glusterd_store_update_snap (snap);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPSHOT_UPDATE_FAIL,
                        "Failed to update snapshot "
                        "for %s snap", snapname);
                goto out;
        }

        ret = glusterd_store_retrieve_volumes (this, snap);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_VOL_RETRIEVE_FAIL, "Failed to retrieve "
                        "snap volumes for snap %s", snapname);
                goto out;
        }

        /* TODO: list_add_order can do 'N-square' comparisons and
           is not efficient. Find a better solution to store the snap
           in order */
        glusterd_list_add_order (&snap->snap_list, &priv->snapshots,
                                 glusterd_compare_snap_time);

out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

/* Read the missed_snap_list and update the in-memory structs */
int32_t
glusterd_store_retrieve_missed_snaps_list (xlator_t  *this)
{
        char                   buf[PATH_MAX]    = "";
        char                   path[PATH_MAX]   = "";
        char                  *snap_vol_id      = NULL;
        char                  *missed_node_info = NULL;
        char                  *brick_path       = NULL;
        char                  *value            = NULL;
        char                  *save_ptr         = NULL;
        FILE                  *fp               = NULL;
        int32_t                brick_num        = -1;
        int32_t                snap_op          = -1;
        int32_t                snap_status      = -1;
        int32_t                ret              = -1;
        glusterd_conf_t       *priv             = NULL;
        gf_store_op_errno_t    store_errno      = GD_STORE_SUCCESS;

        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        /* Get the path of the missed_snap_list */
        glusterd_store_missed_snaps_list_path_set (path, sizeof(path));

        fp = fopen (path, "r");
        if (!fp) {
                /* If errno is ENOENT then there are no missed snaps yet */
                if (errno != ENOENT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED,
                                "Failed to open %s. ",
                                path);
                } else {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_MISSED_SNAP_LIST_EMPTY,
                                "No missed snaps list.");
                        ret = 0;
                }
                goto out;
        }

        do {
                ret = gf_store_read_and_tokenize (fp, buf, sizeof (buf),
                                                  &missed_node_info, &value,
                                                  &store_errno);
                if (ret) {
                        if (store_errno == GD_STORE_EOF) {
                                gf_msg_debug (this->name,
                                        0,
                                        "EOF for missed_snap_list");
                                ret = 0;
                                break;
                        }
                        gf_msg (this->name, GF_LOG_ERROR, store_errno,
                                GD_MSG_MISSED_SNAP_GET_FAIL,
                                "Failed to fetch data from "
                                "missed_snaps_list.");
                        goto out;
                }

                /* Fetch the brick_num, brick_path, snap_op and snap status */
                snap_vol_id = strtok_r (value, ":", &save_ptr);
                brick_num = atoi(strtok_r (NULL, ":", &save_ptr));
                brick_path = strtok_r (NULL, ":", &save_ptr);
                snap_op = atoi(strtok_r (NULL, ":", &save_ptr));
                snap_status = atoi(strtok_r (NULL, ":", &save_ptr));

                if (!missed_node_info || !brick_path || !snap_vol_id ||
                    brick_num < 1 || snap_op < 1 ||
                    snap_status < 1) {
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_MISSED_SNAP_ENTRY,
                                "Invalid missed_snap_entry");
                        ret = -1;
                        goto out;
                }

                ret = glusterd_add_new_entry_to_list (missed_node_info,
                                                      snap_vol_id,
                                                      brick_num,
                                                      brick_path,
                                                      snap_op,
                                                      snap_status);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MISSED_SNAP_LIST_STORE_FAIL,
                                "Failed to store missed snaps_list");
                        goto out;
                }

        } while (store_errno == GD_STORE_SUCCESS);

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_retrieve_snaps (xlator_t  *this)
{
        int32_t                ret              = 0;
        char                   path[PATH_MAX]   = {0,};
        glusterd_conf_t       *priv             = NULL;
        DIR                   *dir              = NULL;
        struct dirent         *entry            = NULL;
        struct dirent          scratch[2]       = {{0,},};

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        snprintf (path, PATH_MAX, "%s/snaps", priv->workdir);

        dir = sys_opendir (path);

        if (!dir) {
                /* If snaps dir doesn't exists ignore the error for
                   backward compatibility */
                if (errno != ENOENT) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DIR_OP_FAILED, "Unable to open dir %s",
                                path);
                }
                goto out;
        }

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);

        while (entry) {
                if (strcmp (entry->d_name, GLUSTERD_MISSED_SNAPS_LIST_FILE)) {
                        ret = glusterd_store_retrieve_snap (entry->d_name);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAP_RESTORE_FAIL,
                                        "Unable to restore snapshot: %s",
                                        entry->d_name);
                                goto out;
                        }
                }
                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }

        /* Retrieve missed_snaps_list */
        ret = glusterd_store_retrieve_missed_snaps_list (this);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Failed to retrieve missed_snaps_list");
                goto out;
        }

out:
        if (dir)
                sys_closedir (dir);
        gf_msg_debug (this->name, 0, "Returning with %d", ret);

        return ret;
}

/* Writes all the contents of conf->missed_snap_list */
int32_t
glusterd_store_write_missed_snapinfo (int32_t fd)
{
        char                           key[PATH_MAX]               = "";
        char                           value[PATH_MAX]             = "";
        int32_t                        ret                         = -1;
        glusterd_conf_t               *priv                        = NULL;
        glusterd_missed_snap_info     *missed_snapinfo             = NULL;
        glusterd_snap_op_t            *snap_opinfo                 = NULL;
        xlator_t                      *this                        = NULL;

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;
        GF_ASSERT (priv);

        /* Write the missed_snap_entry */
        cds_list_for_each_entry (missed_snapinfo, &priv->missed_snaps_list,
                                 missed_snaps) {
                cds_list_for_each_entry (snap_opinfo,
                                         &missed_snapinfo->snap_ops,
                                         snap_ops_list) {
                        snprintf (key, sizeof(key), "%s:%s",
                                  missed_snapinfo->node_uuid,
                                  missed_snapinfo->snap_uuid);
                        snprintf (value, sizeof(value), "%s:%d:%s:%d:%d",
                                  snap_opinfo->snap_vol_id,
                                  snap_opinfo->brick_num,
                                  snap_opinfo->brick_path,
                                  snap_opinfo->op, snap_opinfo->status);
                        ret = gf_store_save_value (fd, key, value);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_MISSEDSNAP_INFO_SET_FAIL,
                                        "Failed to write missed snapinfo");
                                goto out;
                        }
                }
        }

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Adds the missed snap entries to the in-memory conf->missed_snap_list *
 * and writes them to disk */
int32_t
glusterd_store_update_missed_snaps ()
{
        int32_t                        fd                          = -1;
        int32_t                        ret                         = -1;
        glusterd_conf_t               *priv                        = NULL;
        xlator_t                      *this                        = NULL;

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_store_create_missed_snaps_list_shandle_on_absence ();
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MISSED_SNAP_LIST_STORE_HANDLE_GET_FAIL,
                        "Unable to obtain "
                        "missed_snaps_list store handle.");
                goto out;
        }

        fd = gf_store_mkstemp (priv->missed_snaps_list_shandle);
        if (fd <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "Failed to create tmp file");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_write_missed_snapinfo (fd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MISSED_SNAP_CREATE_FAIL,
                        "Failed to write missed snaps to disk");
                goto out;
        }

        ret = gf_store_rename_tmppath (priv->missed_snaps_list_shandle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED,
                        "Failed to rename the tmp file");
                goto out;
        }
out:
        if (ret && (fd > 0)) {
                ret = gf_store_unlink_tmppath (priv->missed_snaps_list_shandle);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_TMP_FILE_UNLINK_FAIL,
                                "Failed to unlink the tmp file");
                }
                ret = -1;
        }

        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_delete_peerinfo (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;
        glusterd_conf_t                 *priv = NULL;
        xlator_t                        *this = NULL;
        char                            peerdir[PATH_MAX] = {0,};
        char                            filepath[PATH_MAX] = {0,};
        char                            hostname_path[PATH_MAX] = {0,};


        if (!peerinfo) {
                ret = 0;
                goto out;
        }

        this = THIS;
        priv = this->private;

        snprintf (peerdir, PATH_MAX, "%s/peers", priv->workdir);


        if (gf_uuid_is_null (peerinfo->uuid)) {

                if (peerinfo->hostname) {
                        snprintf (filepath, PATH_MAX, "%s/%s", peerdir,
                                  peerinfo->hostname);
                } else {
                       ret = 0;
                       goto out;
                }
        } else {

                snprintf (filepath, PATH_MAX, "%s/%s", peerdir,
                          uuid_utoa (peerinfo->uuid));
                snprintf (hostname_path, PATH_MAX, "%s/%s",
                          peerdir, peerinfo->hostname);

                ret = sys_unlink (hostname_path);

                if (!ret)
                        goto out;
        }

        ret = sys_unlink (filepath);
        if (ret && (errno == ENOENT))
                ret = 0;

out:
        if (peerinfo && peerinfo->shandle) {
                        gf_store_handle_destroy (peerinfo->shandle);
                        peerinfo->shandle = NULL;
                }
        gf_msg_debug (this->name, 0, "Returning with %d", ret);

        return ret;
}

void
glusterd_store_peerinfo_dirpath_set (char *path, size_t len)
{
        glusterd_conf_t                 *priv = NULL;
        GF_ASSERT (path);
        GF_ASSERT (len >= PATH_MAX);

        priv = THIS->private;
        snprintf (path, len, "%s/peers", priv->workdir);
}

int32_t
glusterd_store_create_peer_dir ()
{
        int32_t                             ret = 0;
        char                            path[PATH_MAX];

        glusterd_store_peerinfo_dirpath_set (path, sizeof (path));
        ret = gf_store_mkdir (path);

        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

static void
glusterd_store_uuid_peerpath_set (glusterd_peerinfo_t *peerinfo, char *peerfpath,
                             size_t len)
{
        char                    peerdir[PATH_MAX];
        char                    str[50] = {0};

        GF_ASSERT (peerinfo);
        GF_ASSERT (peerfpath);
        GF_ASSERT (len >= PATH_MAX);

        glusterd_store_peerinfo_dirpath_set (peerdir, sizeof (peerdir));
        gf_uuid_unparse (peerinfo->uuid, str);
        snprintf (peerfpath, len, "%s/%s", peerdir, str);
}

static void
glusterd_store_hostname_peerpath_set (glusterd_peerinfo_t *peerinfo,
                                       char *peerfpath, size_t len)
{
        char                    peerdir[PATH_MAX];

        GF_ASSERT (peerinfo);
        GF_ASSERT (peerfpath);
        GF_ASSERT (len >= PATH_MAX);

        glusterd_store_peerinfo_dirpath_set (peerdir, sizeof (peerdir));
        snprintf (peerfpath, len, "%s/%s", peerdir, peerinfo->hostname);
}

int32_t
glusterd_store_peerinfo_hostname_shandle_create (glusterd_peerinfo_t *peerinfo)
{
        char                    peerfpath[PATH_MAX];
        int32_t                 ret = -1;

        glusterd_store_hostname_peerpath_set (peerinfo, peerfpath,
                                              sizeof (peerfpath));
        ret = gf_store_handle_create_on_absence (&peerinfo->shandle,
                                                 peerfpath);
        return ret;
}

int32_t
glusterd_store_peerinfo_uuid_shandle_create (glusterd_peerinfo_t *peerinfo)
{
        char                    peerfpath[PATH_MAX];
        int32_t                 ret = -1;

        glusterd_store_uuid_peerpath_set (peerinfo, peerfpath,
                                          sizeof (peerfpath));
        ret = gf_store_handle_create_on_absence (&peerinfo->shandle,
                                                 peerfpath);
        return ret;
}

int32_t
glusterd_peerinfo_hostname_shandle_check_destroy (glusterd_peerinfo_t *peerinfo)
{
        char                    peerfpath[PATH_MAX];
        int32_t                 ret = -1;
        struct  stat            stbuf = {0,};

        glusterd_store_hostname_peerpath_set (peerinfo, peerfpath,
                                              sizeof (peerfpath));
        ret = sys_stat (peerfpath, &stbuf);
        if (!ret) {
                if (peerinfo->shandle)
                        gf_store_handle_destroy (peerinfo->shandle);
                peerinfo->shandle = NULL;
                ret = sys_unlink (peerfpath);
        }
        return ret;
}

int32_t
glusterd_store_create_peer_shandle (glusterd_peerinfo_t *peerinfo)
{
        int32_t                 ret = 0;

        GF_ASSERT (peerinfo);

        if (gf_uuid_is_null (peerinfo->uuid)) {
                ret = glusterd_store_peerinfo_hostname_shandle_create (peerinfo);
        } else {
                ret = glusterd_peerinfo_hostname_shandle_check_destroy (peerinfo);
                ret = glusterd_store_peerinfo_uuid_shandle_create (peerinfo);
        }
        return ret;
}

int32_t
glusterd_store_peer_write (int fd, glusterd_peerinfo_t *peerinfo)
{
        char                      buf[50] = {0};
        int32_t                   ret = 0;
        int32_t                   i = 1;
        glusterd_peer_hostname_t *hostname = NULL;
        char                     *key = NULL;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PEER_UUID,
                                   uuid_utoa (peerinfo->uuid));
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", peerinfo->state.state);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PEER_STATE, buf);
        if (ret)
                goto out;

        cds_list_for_each_entry (hostname, &peerinfo->hostnames,
                                 hostname_list) {
                ret = gf_asprintf (&key, GLUSTERD_STORE_KEY_PEER_HOSTNAME"%d",
                                   i);
                if (ret < 0)
                        goto out;
                ret = gf_store_save_value (fd, key, hostname->hostname);
                if (ret)
                        goto out;
                GF_FREE (key);
                key = NULL;
                i++;
        }

out:
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_perform_peer_store (glusterd_peerinfo_t *peerinfo)
{
        int                                 fd = -1;
        int32_t                             ret = -1;

        GF_ASSERT (peerinfo);

        fd = gf_store_mkstemp (peerinfo->shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        ret = glusterd_store_peer_write (fd, peerinfo);
        if (ret)
                goto out;

        ret = gf_store_rename_tmppath (peerinfo->shandle);
out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (peerinfo->shandle);
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_peerinfo (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;

        GF_ASSERT (peerinfo);

        ret = glusterd_store_create_peer_dir ();
        if (ret)
                goto out;

        ret = glusterd_store_create_peer_shandle (peerinfo);
        if (ret)
                goto out;

        ret = glusterd_store_perform_peer_store (peerinfo);
out:
        gf_msg_debug ("glusterd", 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_retrieve_peers (xlator_t *this)
{
        int32_t                   ret                = 0;
        glusterd_conf_t          *priv               = NULL;
        DIR                      *dir                = NULL;
        struct dirent            *entry              = NULL;
        struct dirent             scratch[2]         = {{0,},};
        char                      path[PATH_MAX]     = {0,};
        glusterd_peerinfo_t      *peerinfo           = NULL;
        gf_store_handle_t        *shandle            = NULL;
        char                      filepath[PATH_MAX] = {0,};
        gf_store_iter_t          *iter               = NULL;
        char                     *key                = NULL;
        char                     *value              = NULL;
        glusterd_peerctx_args_t   args               = {0};
        gf_store_op_errno_t       op_errno           = GD_STORE_SUCCESS;
        glusterd_peer_hostname_t *address            = NULL;

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                  GLUSTERD_PEER_DIR_PREFIX);

        dir = sys_opendir (path);

        if (!dir) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DIR_OP_FAILED,
                        "Unable to open dir %s", path);
                ret = -1;
                goto out;
        }

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);

        while (entry) {
                snprintf (filepath, PATH_MAX, "%s/%s", path, entry->d_name);
                ret = gf_store_handle_retrieve (filepath, &shandle);
                if (ret)
                        goto out;

                ret = gf_store_iter_new (shandle, &iter);
                if (ret)
                        goto out;

                ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
                if (ret)
                        goto out;

                /* Create an empty peerinfo object before reading in the
                 * details
                 */
                peerinfo = glusterd_peerinfo_new (GD_FRIEND_STATE_DEFAULT, NULL,
                                                  NULL, 0);
                if (peerinfo == NULL) {
                        ret = -1;
                        goto out;
                }

                while (!ret) {

                        if (!strncmp (GLUSTERD_STORE_KEY_PEER_UUID, key,
                                      strlen (GLUSTERD_STORE_KEY_PEER_UUID))) {
                                if (value)
                                        gf_uuid_parse (value, peerinfo->uuid);
                        } else if (!strncmp (GLUSTERD_STORE_KEY_PEER_STATE,
                                    key,
                                    strlen (GLUSTERD_STORE_KEY_PEER_STATE))) {
                                peerinfo->state.state = atoi (value);
                        } else if (!strncmp (GLUSTERD_STORE_KEY_PEER_HOSTNAME,
                                   key,
                                   strlen (GLUSTERD_STORE_KEY_PEER_HOSTNAME))) {
                                ret = gd_add_address_to_peer (peerinfo, value);
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_UNKNOWN_KEY, "Unknown key: %s",
                                        key);
                        }

                        GF_FREE (key);
                        GF_FREE (value);
                        key = NULL;
                        value = NULL;

                        ret = gf_store_iter_get_next (iter, &key, &value,
                                                      &op_errno);
                }
                if (op_errno != GD_STORE_EOF) {
                        goto out;
                }

                (void) gf_store_iter_destroy (iter);

                /* Set first hostname from peerinfo->hostnames to
                 * peerinfo->hostname
                 */
                address = cds_list_entry (peerinfo->hostnames.next,
                                          glusterd_peer_hostname_t,
                                          hostname_list);
                if (!address) {
                        ret = -1;
                        goto out;
                }
                peerinfo->hostname = gf_strdup (address->hostname);

                ret = glusterd_friend_add_from_peerinfo (peerinfo, 1, NULL);
                if (ret)
                        goto out;

                peerinfo->shandle = shandle;
                peerinfo = NULL;
                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }

        args.mode = GD_MODE_ON;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &priv->peers, uuid_list) {
                ret = glusterd_friend_rpc_create (this, peerinfo, &args);
                if (ret)
                        break;
        }
        rcu_read_unlock ();
        peerinfo = NULL;

out:
        if (peerinfo)
                glusterd_peerinfo_cleanup (peerinfo);

        if (dir)
                sys_closedir (dir);
        gf_msg_debug (this->name, 0, "Returning with %d", ret);

        return ret;
}

/* Bricks for snap volumes are hosted at /var/run/gluster/snaps
 * When a volume is restored, it points to the bricks of the snap
 * volume it was restored from. Hence on a node restart these
 * paths need to be recreated and re-mounted
 */
int32_t
glusterd_recreate_all_snap_brick_mounts (xlator_t  *this)
{
        int32_t                  ret       = 0;
        glusterd_conf_t         *priv      = NULL;
        glusterd_volinfo_t      *volinfo   = NULL;
        glusterd_snap_t         *snap      = NULL;

        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        /* Recreate bricks of volumes restored from snaps */
        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                /* If the volume is not a restored volume then continue */
                if (gf_uuid_is_null (volinfo->restored_from_snap))
                        continue;

                ret = glusterd_recreate_vol_brick_mounts (this, volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRK_MNT_RECREATE_FAIL,
                                "Failed to recreate brick mounts "
                                "for %s", volinfo->volname);
                        goto out;
                }
        }

        /* Recreate bricks of snapshot volumes */
        cds_list_for_each_entry (snap, &priv->snapshots, snap_list) {
                cds_list_for_each_entry (volinfo, &snap->volumes, vol_list) {
                        ret = glusterd_recreate_vol_brick_mounts (this,
                                                                  volinfo);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_BRK_MNT_RECREATE_FAIL,
                                        "Failed to recreate brick mounts "
                                        "for %s", snap->snapname);
                                goto out;
                        }
                }
        }

out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

/* When the snapshot command from cli is received, the on disk and
 * in memory structures for the snapshot are created (with the status)
 * being marked as GD_SNAP_STATUS_INIT. Once the backend snapshot is
 * taken, the status is changed to GD_SNAP_STATUS_IN_USE. If glusterd
 * dies after taking the backend snapshot, but before updating the
 * status, then when glusterd comes up, it should treat that snapshot
 * as a failed snapshot and clean it up.
 *
 * Restore operation starts by setting the status to
 * GD_SNAP_STATUS_RESTORED. If the server goes down before changing
 * the status the status back we need to revert the partial snapshot
 * taken.
 */
int32_t
glusterd_snap_cleanup (xlator_t  *this)
{
        dict_t               *dict = NULL;
        int32_t               ret  = 0;
        glusterd_conf_t      *priv = NULL;
        glusterd_snap_t      *snap = NULL;
        glusterd_snap_t      *tmp_snap = NULL;

        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        dict = dict_new();
        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to create dict");
                ret = -1;
                goto out;
        }

        cds_list_for_each_entry_safe (snap, tmp_snap, &priv->snapshots,
                                      snap_list) {
                if (snap->snap_status == GD_SNAP_STATUS_RESTORED) {
                        ret = glusterd_snapshot_revert_restore_from_snap (snap);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_SNAP_RESTORE_REVERT_FAIL,
                                        "Failed to "
                                        "revert partially restored snapshot "
                                        "(%s)", snap->snapname);
                                goto out;
                        }
                } else if (snap->snap_status != GD_SNAP_STATUS_IN_USE) {
                        ret = glusterd_snap_remove (dict, snap,
                                                    _gf_true, _gf_true,
                                                    _gf_false);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAP_REMOVE_FAIL,
                                        "Failed to remove the snapshot %s",
                                        snap->snapname);
                                goto out;
                        }
                }
        }
out:
        if (dict)
                dict_unref (dict);

        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_resolve_all_bricks (xlator_t  *this)
{
        int32_t                 ret        = 0;
        glusterd_conf_t         *priv      = NULL;
        glusterd_volinfo_t      *volinfo   = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_snap_t         *snap      = NULL;

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        /* Resolve bricks of volumes */
        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                cds_list_for_each_entry (brickinfo, &volinfo->bricks,
                                         brick_list) {
                        ret = glusterd_resolve_brick (brickinfo);
                        if (ret) {
                                gf_event (EVENT_BRICKPATH_RESOLVE_FAILED,
                                          "peer=%s;volume=%s;brick=%s",
                                          brickinfo->hostname, volinfo->volname,
                                          brickinfo->path);
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_RESOLVE_BRICK_FAIL,
                                        "resolve brick failed in restore");
                                goto out;
                        }
                }
        }

        /* Resolve bricks of snapshot volumes */
        cds_list_for_each_entry (snap, &priv->snapshots, snap_list) {
                ret = glusterd_resolve_snap_bricks (this, snap);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAP_RESOLVE_BRICK_FAIL,
                                "resolving the snap bricks"
                                " failed for snap: %s",
                                snap->snapname);
                        goto out;
                }
        }

out:
        gf_msg_trace (this->name, 0, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_restore ()
{
        int32_t         ret = -1;
        xlator_t        *this = NULL;

        this = THIS;

        ret = glusterd_store_retrieve_volumes (this, NULL);
        if (ret)
                goto out;

        ret = glusterd_store_retrieve_peers (this);
        if (ret)
                goto out;

        /* While retrieving snapshots, if the snapshot status
           is not GD_SNAP_STATUS_IN_USE, then the snapshot is
           cleaned up. To do that, the snap volume has to be
           stopped by stopping snapshot volume's bricks. And for
           that the snapshot bricks should be resolved. But without
           retrieving the peers, resolving bricks will fail. So
           do retrieving of snapshots after retrieving peers.
        */
        ret = glusterd_store_retrieve_snaps (this);
        if (ret)
                goto out;

        ret = glusterd_resolve_all_bricks (this);
        if (ret)
                goto out;

        ret = glusterd_snap_cleanup (this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_CLEANUP_FAIL, "Failed to perform "
                        "a cleanup of the snapshots");
                goto out;
        }

        ret = glusterd_recreate_all_snap_brick_mounts (this);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAP_BRK_MNT_RECREATE_FAIL, "Failed to recreate "
                        "all snap brick mounts");
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_store_retrieve_quota_version (glusterd_volinfo_t *volinfo)
{
        int                 ret                  = -1;
        uint32_t            version              = 0;
        char                cksum_path[PATH_MAX] = {0,};
        char                path[PATH_MAX]       = {0,};
        char               *version_str          = NULL;
        char               *tmp                  = NULL;
        xlator_t           *this                 = NULL;
        glusterd_conf_t    *conf                 = NULL;
        gf_store_handle_t  *handle               = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, conf);
        snprintf (cksum_path, sizeof (cksum_path), "%s/%s", path,
                  GLUSTERD_VOL_QUOTA_CKSUM_FILE);

        ret = gf_store_handle_new (cksum_path, &handle);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_HANDLE_GET_FAIL,
                        "Unable to get store handle "
                        "for %s", cksum_path);
                goto out;
        }

        ret = gf_store_retrieve_value (handle, "version", &version_str);
        if (ret) {
                gf_msg_debug (this->name, 0, "Version absent");
                ret = 0;
                goto out;
        }

        version = strtoul (version_str, &tmp, 10);
	if ((errno == ERANGE) || (errno == EINVAL)) {
                gf_msg_debug (this->name, 0, "Invalid version number");
                goto out;
        }
        volinfo->quota_conf_version = version;
        ret = 0;

out:
        if (version_str)
                GF_FREE (version_str);
        gf_store_handle_destroy (handle);
        return ret;
}

int
glusterd_store_save_quota_version_and_cksum (glusterd_volinfo_t *volinfo)
{
        gf_store_handle_t               *shandle = NULL;
        glusterd_conf_t                 *conf = NULL;
        xlator_t                        *this = NULL;
        char                            path[PATH_MAX] = {0};
        char                            cksum_path[PATH_MAX] = {0,};
        char                            buf[256] = {0};
        int                             fd = -1;
        int32_t                         ret = -1;

        this = THIS;
        conf = this->private;

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, conf);
        snprintf (cksum_path, sizeof (cksum_path), "%s/%s", path,
                  GLUSTERD_VOL_QUOTA_CKSUM_FILE);

        ret = gf_store_handle_new (cksum_path, &shandle);
        if (ret)
                goto out;

        fd = gf_store_mkstemp (shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        snprintf (buf, sizeof (buf)-1, "%u", volinfo->quota_conf_cksum);
        ret = gf_store_save_value (fd, "cksum", buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CKSUM_STORE_FAIL, "Failed to store cksum");
                goto out;
        }

        memset (buf, 0, sizeof (buf));
        snprintf (buf, sizeof (buf)-1, "%u", volinfo->quota_conf_version);
        ret = gf_store_save_value (fd, "version", buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VERS_STORE_FAIL, "Failed to store version");
                goto out;
        }

        ret = gf_store_rename_tmppath (shandle);
        if (ret)
                goto out;

out:
        if ((ret < 0) && (fd > 0))
                gf_store_unlink_tmppath (shandle);
        gf_store_handle_destroy (shandle);
        return ret;
}

int32_t
glusterd_quota_conf_write_header (int fd)
{
        int                 header_len    = 0;
        int                 ret           = -1;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *conf          = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("quota", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);


        if (conf->op_version < GD_OP_VERSION_3_7_0) {
                header_len = strlen (QUOTA_CONF_HEADER_1_1);
                ret = gf_nwrite (fd, QUOTA_CONF_HEADER_1_1, header_len);
        } else {
                header_len = strlen (QUOTA_CONF_HEADER);
                ret = gf_nwrite (fd, QUOTA_CONF_HEADER, header_len);
        }

        if (ret != header_len) {
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        if (ret < 0)
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  GD_MSG_QUOTA_CONF_WRITE_FAIL,
                                  "failed to write "
                                  "header to a quota conf");

        return ret;
}

int32_t
glusterd_quota_conf_write_gfid (int fd, void *buf, char type)
{
        int                 ret        = -1;
        xlator_t           *this       = NULL;
        glusterd_conf_t    *conf       = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("quota", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);


        ret = gf_nwrite (fd, buf, 16);
        if (ret != 16) {
                ret = -1;
                goto out;
        }

        if (conf->op_version >= GD_OP_VERSION_3_7_0) {
                ret = gf_nwrite (fd, &type, 1);
                if (ret != 1) {
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

out:
        if (ret < 0)
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  GD_MSG_QUOTA_CONF_WRITE_FAIL,
                                  "failed to write "
                                  "gfid %s to a quota conf", uuid_utoa (buf));

        return ret;
}
