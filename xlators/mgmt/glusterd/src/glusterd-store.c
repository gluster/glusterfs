/*
   Copyright (c) 2007-2013 Red Hat, Inc. <http://www.redhat.com>
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

#include "rpc-clnt.h"
#include "common-utils.h"

#include <sys/resource.h>
#include <inttypes.h>
#include <dirent.h>

void
glusterd_replace_slash_with_hipen (char *str)
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
        glusterd_replace_slash_with_hipen (key_vol_brick);
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

gf_boolean_t
glusterd_store_is_valid_brickpath (char *volname, char *brick)
{
        char                    brickpath[PATH_MAX] = {0};
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        int32_t                 ret = 0;
        size_t                  volname_len = strlen (volname);
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_brickinfo_new_from_brick (brick, &brickinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to create brick "
                        "info for brick %s", brick);
                ret = 0;
                goto out;
        }
        ret = glusterd_volinfo_new (&volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to create volinfo");
                ret = 0;
                goto out;
        }
        if (volname_len >= sizeof (volinfo->volname)) {
                gf_log (this->name, GF_LOG_WARNING, "volume name too long");
                ret = 0;
                goto out;
        }
        memcpy (volinfo->volname, volname, volname_len+1);
        glusterd_store_brickinfopath_set (volinfo, brickinfo, brickpath,
                                                sizeof (brickpath));

        ret = (strlen (brickpath) < _POSIX_PATH_MAX);

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

        if (!brickinfo->vg[0])
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_BRICK_VGNAME,
                                         brickinfo->vg);
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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
        if (fd > 0)
                close (fd);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);
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

        ret = unlink (brickpath);

        if ((ret < 0) && (errno != ENOENT)) {
                gf_log (this->name, GF_LOG_DEBUG, "Unlink failed on %s, "
                        "reason: %s", brickpath, strerror(errno));
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
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_remove_bricks (glusterd_volinfo_t *volinfo, char *delete_path)
{
        int32_t                 ret = 0;
        glusterd_brickinfo_t    *tmp = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    brickdir [PATH_MAX] = {0,};
        DIR                     *dir = NULL;
        struct dirent           *entry = NULL;
        char                    path[PATH_MAX] = {0,};
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);

        list_for_each_entry (tmp, &volinfo->bricks, brick_list) {
                ret = glusterd_store_delete_brick (tmp, delete_path);
                if (ret)
                        goto out;
        }

        priv = this->private;
        GF_ASSERT (priv);

        snprintf (brickdir, sizeof (brickdir), "%s/%s", delete_path,
                  GLUSTERD_BRICK_INFO_DIR);

        dir = opendir (brickdir);

        glusterd_for_each_entry (entry, dir);

        while (entry) {
                snprintf (path, sizeof (path), "%s/%s",
                          brickdir, entry->d_name);
                ret = unlink (path);
                if (ret && errno != ENOENT) {
                        gf_log (this->name, GF_LOG_DEBUG, "Unable to unlink %s, "
                                "reason: %s", path, strerror(errno));
                }
                glusterd_for_each_entry (entry, dir);
        }

        closedir (dir);

        ret = rmdir (brickdir);

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);
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

        gf_log (xl->name, GF_LOG_DEBUG, "Storing in volinfo:key= %s, val=%s",
                key, value->data);

        ret = gf_store_save_value (shandle->fd, key, (char*)value->data);
        if (ret) {
                gf_log (xl->name, GF_LOG_ERROR, "Unable to write into store"
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
                gf_log (xl->name, GF_LOG_DEBUG, "Storing in volinfo:key= %s, "
                        "val=%s", key, value->data);

        } else {
                gf_log (xl->name, GF_LOG_DEBUG, "Discarding:key= %s, val=%s",
                        key, value->data);
                return 0;
        }

        ret = gf_store_save_value (shandle->fd, key, (char*)value->data);
        if (ret) {
                gf_log (xl->name, GF_LOG_ERROR, "Unable to write into store"
                        " handle for path: %s", shandle->path);
                return -1;
        }
        return 0;
}

int32_t
glusterd_volume_exclude_options_write (int fd, glusterd_volinfo_t *volinfo)
{
        char      *str   = NULL;

        GF_ASSERT (fd > 0);
        GF_ASSERT (volinfo);

        char                    buf[PATH_MAX] = {0,};
        int32_t                 ret           = -1;

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

out:
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "Unable to write volume "
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

        snprintf (voldirpath, len, "%s/%s/%s", priv->workdir,
                  GLUSTERD_VOLUME_DIR_PREFIX, volinfo->volname);
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
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning with %d", ret);
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
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static void
glusterd_store_rbstatepath_set (glusterd_volinfo_t *volinfo, char *rbstatepath,
                                size_t len)
{
        char    voldirpath[PATH_MAX] = {0,};
        GF_ASSERT (volinfo);
        GF_ASSERT (rbstatepath);
        GF_ASSERT (len <= PATH_MAX);

        glusterd_store_voldirpath_set (volinfo, voldirpath,
                                       sizeof (voldirpath));
        snprintf (rbstatepath, len, "%s/%s", voldirpath,
                  GLUSTERD_VOLUME_RBSTATE_FILE);
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

int32_t
glusterd_store_create_rbstate_shandle_on_absence (glusterd_volinfo_t *volinfo)
{
        char            rbstatepath[PATH_MAX] = {0};
        int32_t         ret                   = 0;

        GF_ASSERT (volinfo);

        glusterd_store_rbstatepath_set (volinfo, rbstatepath, sizeof (rbstatepath));
        ret = gf_store_handle_create_on_absence (&volinfo->rb_shandle,
                                                 rbstatepath);
        return ret;
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
int32_t
glusterd_store_brickinfos (glusterd_volinfo_t *volinfo, int vol_fd)
{
        int32_t                 ret = 0;
        glusterd_brickinfo_t    *brickinfo = NULL;
        int32_t                 brick_count = 0;

        GF_ASSERT (volinfo);

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_store_brickinfo (volinfo, brickinfo,
                                            brick_count, vol_fd);
                if (ret)
                        goto out;
                brick_count++;
        }
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_rbstate_write (int fd, glusterd_volinfo_t *volinfo)
{
        int     ret             = -1;
        int     port            = 0;
        char    buf[PATH_MAX]   = {0, };

        GF_ASSERT (fd > 0);
        GF_ASSERT (volinfo);

        snprintf (buf, sizeof (buf), "%d", volinfo->rep_brick.rb_status);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_RB_STATUS, buf);
        if (ret)
                goto out;

        if (volinfo->rep_brick.rb_status > GF_RB_STATUS_NONE) {

                snprintf (buf, sizeof (buf), "%s:%s",
                          volinfo->rep_brick.src_brick->hostname,
                          volinfo->rep_brick.src_brick->path);
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_RB_SRC_BRICK,
                                           buf);
                if (ret)
                        goto out;

                snprintf (buf, sizeof (buf), "%s:%s",
                          volinfo->rep_brick.dst_brick->hostname,
                          volinfo->rep_brick.dst_brick->path);
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_RB_DST_BRICK,
                                           buf);
                if (ret)
                        goto out;

                switch (volinfo->transport_type) {
                        case GF_TRANSPORT_RDMA:
                                port = volinfo->rep_brick.dst_brick->rdma_port;
                                break;

                        case GF_TRANSPORT_TCP:
                        case GF_TRANSPORT_BOTH_TCP_RDMA:
                                port = volinfo->rep_brick.dst_brick->port;
                                break;
                }

                snprintf (buf, sizeof (buf), "%d", port);
                ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_RB_DST_PORT,
                                           buf);
                if (ret)
                        goto out;
                uuid_unparse (volinfo->rep_brick.rb_id, buf);
                ret = gf_store_save_value (fd, GF_REPLACE_BRICK_TID_KEY, buf);
        }

        ret = 0;
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_perform_rbstate_store (glusterd_volinfo_t *volinfo)
{
        int                         fd = -1;
        int32_t                     ret = -1;
        GF_ASSERT (volinfo);

        fd = gf_store_mkstemp (volinfo->rb_shandle);
        if (fd <= 0) {
                ret = -1;
                goto out;
        }

        ret = glusterd_store_rbstate_write (fd, volinfo);
        if (ret)
                goto out;

        ret = gf_store_rename_tmppath (volinfo->rb_shandle);
        if (ret)
                goto out;

out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (volinfo->rb_shandle);
        if (fd > 0)
                close (fd);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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

        snprintf (buf, sizeof (buf), "%d", volinfo->rebal.op);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_DEFRAG_OP, buf);
        if (ret)
                goto out;

        uuid_unparse (volinfo->rebal.rebalance_id, buf);
        ret = gf_store_save_value (fd, GF_REBALANCE_TID_KEY, buf);
        if (ret)
                goto out;

        if (volinfo->rebal.dict) {
                dict_foreach (volinfo->rebal.dict, _gd_store_rebalance_dict,
                              &fd);
        }
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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

        ret = gf_store_rename_tmppath (volinfo->node_state_shandle);
        if (ret)
                goto out;

out:
        if (ret && (fd > 0))
                gf_store_unlink_tmppath (volinfo->node_state_shandle);
        if (fd > 0)
                close (fd);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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
        if (fd > 0)
                close (fd);
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
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

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                gf_store_unlink_tmppath (brickinfo->shandle);
        }
}

void
glusterd_store_volume_cleanup_tmp (glusterd_volinfo_t *volinfo)
{
        GF_ASSERT (volinfo);

        glusterd_store_bricks_cleanup_tmp (volinfo);

        gf_store_unlink_tmppath (volinfo->shandle);

        gf_store_unlink_tmppath (volinfo->rb_shandle);

        gf_store_unlink_tmppath (volinfo->node_state_shandle);
}

int32_t
glusterd_store_brickinfos_atomic_update (glusterd_volinfo_t *volinfo)
{
        int                      ret            = -1;
        glusterd_brickinfo_t    *brickinfo      = NULL;

        GF_ASSERT (volinfo);

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
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
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't rename "
                        "temporary file(s): Reason %s", strerror (errno));
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

        ret = glusterd_store_create_rbstate_shandle_on_absence (volinfo);
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

        ret = glusterd_store_perform_rbstate_store (volinfo);
        if (ret)
                goto out;

        ret = glusterd_store_perform_node_state_store (volinfo);
        if (ret)
                goto out;

        //checksum should be computed at the end
        ret = glusterd_compute_cksum (volinfo, _gf_false);
        if (ret)
                goto out;

out:
        if (ret)
                glusterd_store_volume_cleanup_tmp (volinfo);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_store_delete_volume (glusterd_volinfo_t *volinfo)
{
        char             pathname[PATH_MAX]    = {0,};
        int32_t          ret                   = 0;
        glusterd_conf_t *priv                  = NULL;
        DIR             *dir                   = NULL;
        struct dirent   *entry                 = NULL;
        char             path[PATH_MAX]        = {0,};
        char             delete_path[PATH_MAX] = {0,};
        char             trashdir[PATH_MAX]    = {0,};
        struct stat      st                    = {0, };
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

        ret = mkdir (trashdir, 0777);
        if (ret && errno != EEXIST) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create trash "
                        "directory, reason : %s", strerror (errno));
                ret = -1;
                goto out;
        }

        ret = rename (pathname, delete_path);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to rename volume "
                        "directory for volume %s", volinfo->volname);
                rename_fail = _gf_true;
                goto out;
        }

        dir = opendir (delete_path);
        if (!dir) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to open directory %s."
                        " Reason : %s", delete_path, strerror (errno));
                ret = 0;
                goto out;
        }
        ret = glusterd_store_remove_bricks (volinfo, delete_path);

        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Remove bricks failed for %s",
                        volinfo->volname);
        }

        glusterd_for_each_entry (entry, dir);
        while (entry) {

                snprintf (path, PATH_MAX, "%s/%s", delete_path, entry->d_name);
                ret = stat (path, &st);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, "Failed to stat "
                                "entry %s : %s", path, strerror (errno));
                        goto stat_failed;
                }

                if (S_ISDIR (st.st_mode))
                        ret = rmdir (path);
                else
                        ret = unlink (path);

                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG, " Failed to remove "
                                "%s. Reason : %s", path, strerror (errno));
                }

                gf_log (this->name, GF_LOG_DEBUG, "%s %s",
                                ret ? "Failed to remove":"Removed",
                                entry->d_name);
stat_failed:
                memset (path, 0, sizeof(path));
                glusterd_for_each_entry (entry, dir);
        }

        ret = closedir (dir);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to close dir %s. "
                        "Reason : %s",delete_path, strerror (errno));
        }

        ret = rmdir (delete_path);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to rmdir: %s,err: %s",
                        delete_path, strerror (errno));
        }
        ret = rmdir (trashdir);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Failed to rmdir: %s, Reason:"
                        " %s", trashdir, strerror (errno));
        }

out:
        if (volinfo->shandle) {
                gf_store_handle_destroy (volinfo->shandle);
                volinfo->shandle = NULL;
        }
        ret = (rename_fail == _gf_true) ? -1: 0;

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to get store handle");
                        goto out;
                }

                conf->handle = handle;
        } else
                handle = conf->handle;

        /* These options need to be available for all users */
        ret = chmod (handle->path, 0644);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "chmod error for %s: %s",
                        GLUSTERD_INFO_FILE, strerror (errno));
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
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Storing uuid failed ret = %d", ret);
                goto out;
        }

        snprintf (op_version_str, 15, "%d", conf->op_version);
        ret = gf_store_save_value (handle->fd, GD_OP_VERSION_KEY,
                                   op_version_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Storing op-version failed ret = %d", ret);
                goto out;
        }

        ret = gf_store_rename_tmppath (handle);
out:
        if (handle) {
                if (ret && (handle->fd > 0))
                        gf_store_unlink_tmppath (handle);

                if (handle->fd > 0) {
                        close (handle->fd);
                        handle->fd = 0;
                }
        }

        if (uuid_str)
                GF_FREE (uuid_str);

        if (ret)
                gf_log (this->name, GF_LOG_ERROR,
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
                        gf_log ("", GF_LOG_DEBUG, "Unable to get store "
                                "handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = gf_store_retrieve_value (priv->handle, GD_OP_VERSION_KEY,
                                       &op_version_str);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No previous op_version present");
                goto out;
        }

        tmp_version = strtol (op_version_str, &tmp, 10);
        if ((tmp_version <= 0) || (tmp && strlen (tmp) > 1)) {
                gf_log (this->name, GF_LOG_WARNING, "invalid version number");
                goto out;
        }

        *op_version = tmp_version;

        ret = 0;
out:
        if (op_version_str)
                GF_FREE (op_version_str);

        return ret;
}

static int
glusterd_restore_op_version (xlator_t *this)
{
        glusterd_conf_t *conf = NULL;
        int ret = 0;
        int op_version = 0;

        conf = this->private;

        ret = glusterd_retrieve_op_version (this, &op_version);
        if (!ret) {
                if ((op_version < GD_OP_VERSION_MIN) ||
                    (op_version > GD_OP_VERSION_MAX)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "wrong op-version (%d) retrieved", op_version);
                        ret = -1;
                        goto out;
                }
                conf->op_version = op_version;
                gf_log ("glusterd", GF_LOG_INFO,
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
                gf_log (this->name, GF_LOG_INFO, "Detected new install. Setting"
                        " op-version to maximum : %d", GD_OP_VERSION_MAX);
                conf->op_version = GD_OP_VERSION_MAX;
        } else {
                gf_log (this->name, GF_LOG_INFO, "Upgrade detected. Setting"
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
        char            path[PATH_MAX] = {0,};

        priv = THIS->private;

        if (!priv->handle) {
                snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                          GLUSTERD_INFO_FILE);
                ret = gf_store_handle_retrieve (path, &handle);

                if (ret) {
                        gf_log ("", GF_LOG_DEBUG, "Unable to get store"
                                "handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = gf_store_retrieve_value (priv->handle, GLUSTERD_STORE_UUID_KEY,
                                       &uuid_str);

        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "No previous uuid is present");
                goto out;
        }

        uuid_parse (uuid_str, priv->uuid);

out:
        GF_FREE (uuid_str);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
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
        struct pmap_registry    *pmap = NULL;
        gf_store_op_errno_t     op_errno = GD_STORE_SUCCESS;

        GF_ASSERT (volinfo);
        GF_ASSERT (volinfo->volname);

        priv = THIS->private;

        GLUSTERD_GET_BRICK_DIR (brickdir, volinfo, priv)

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
                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to iterate "
                                "the store for brick: %s, reason: %s", path,
                                gf_store_strerror (op_errno));
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
                        } else if (!strncmp (key,
                                    GLUSTERD_STORE_KEY_BRICK_VGNAME,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_VGNAME))) {
                                strncpy (brickinfo->vg, value,
                                         sizeof (brickinfo->vg));
                        } else {
                                gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
                                        key);
                        }

                        GF_FREE (key);
                        GF_FREE (value);
                        key = NULL;
                        value = NULL;

                        ret = gf_store_iter_get_next (iter, &key, &value,
                                                      &op_errno);
                }

                if (op_errno != GD_STORE_EOF)
                        goto out;
                ret = gf_store_iter_destroy (iter);

                if (ret)
                        goto out;

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick_count++;
        }

        ret = gf_store_iter_destroy (tmpiter);
        if (ret)
                goto out;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


int32_t
glusterd_store_retrieve_rbstate (char   *volname)
{
        int32_t                   ret                   = -1;
        glusterd_volinfo_t        *volinfo              = NULL;
        gf_store_iter_t           *iter                 = NULL;
        char                      *key                  = NULL;
        char                      *value                = NULL;
        char                      volpath[PATH_MAX]     = {0,};
        glusterd_conf_t           *priv                 = NULL;
        char                      path[PATH_MAX]        = {0,};
        gf_store_op_errno_t       op_errno              = GD_STORE_SUCCESS;

        priv = THIS->private;

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't get"
                        "volinfo for %s.", volname);
                goto out;
        }

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, priv);
        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_VOLUME_RBSTATE_FILE);

        ret = gf_store_handle_retrieve (path, &volinfo->rb_shandle);

        if (ret)
                goto out;

        ret = gf_store_iter_new (volinfo->rb_shandle, &iter);

        if (ret)
                goto out;

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        if (ret)
                goto out;

        while (!ret) {
                if (!strncmp (key, GLUSTERD_STORE_KEY_RB_STATUS,
                              strlen (GLUSTERD_STORE_KEY_RB_STATUS))) {
                        volinfo->rep_brick.rb_status = atoi (value);
                }

                if (volinfo->rep_brick.rb_status > GF_RB_STATUS_NONE) {
                        if (!strncmp (key, GLUSTERD_STORE_KEY_RB_SRC_BRICK,
                                      strlen (GLUSTERD_STORE_KEY_RB_SRC_BRICK))) {
                                ret = glusterd_brickinfo_new_from_brick (value,
                                                &volinfo->rep_brick.src_brick);
                                if (ret)
                                        goto out;
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_RB_DST_BRICK,
                                             strlen (GLUSTERD_STORE_KEY_RB_DST_BRICK))) {
                                ret = glusterd_brickinfo_new_from_brick (value,
                                                &volinfo->rep_brick.dst_brick);
                                if (ret)
                                        goto out;
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_RB_DST_PORT,
                                             strlen (GLUSTERD_STORE_KEY_RB_DST_PORT))) {
                                switch (volinfo->transport_type) {
                                case GF_TRANSPORT_RDMA:
                                volinfo->rep_brick.dst_brick->rdma_port =
                                                 atoi (value);
                                        break;

                                case GF_TRANSPORT_TCP:
                                case GF_TRANSPORT_BOTH_TCP_RDMA:
                                volinfo->rep_brick.dst_brick->port =
                                                atoi (value);
                                        break;
                                }
                        } else if (!strncmp (key, GF_REPLACE_BRICK_TID_KEY,
                                             strlen (GF_REPLACE_BRICK_TID_KEY))) {
                                        uuid_parse (value,
                                                    volinfo->rep_brick.rb_id);
                        }
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

        if (ret)
                goto out;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_store_retrieve_node_state (char   *volname)
{
        int32_t              ret               = -1;
        glusterd_volinfo_t  *volinfo           = NULL;
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

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't get"
                        "volinfo for %s.", volname);
                goto out;
        }

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
                } else if (!strncmp (key, GF_REBALANCE_TID_KEY,
                                     strlen (GF_REBALANCE_TID_KEY))) {
                        uuid_parse (value, volinfo->rebal.rebalance_id);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_DEFRAG_OP,
                                        strlen (GLUSTERD_STORE_KEY_DEFRAG_OP))) {
                        volinfo->rebal.op = atoi (value);
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
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to strdup value string");
                                goto out;
                        }
                        ret = dict_set_str (tmp_dict, key, dup_value);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
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
        if (tmp_dict)
                volinfo->rebal.dict = dict_ref (tmp_dict);

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
        if (ret && volinfo->rebal.dict)
                dict_unref (volinfo->rebal.dict);
        if (tmp_dict)
                dict_unref (tmp_dict);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_store_retrieve_volume (char    *volname)
{
        int32_t                   ret                   = -1;
        glusterd_volinfo_t        *volinfo              = NULL;
        gf_store_iter_t           *iter                 = NULL;
        char                      *key                  = NULL;
        char                      *value                = NULL;
        char                      volpath[PATH_MAX]     = {0,};
        glusterd_conf_t           *priv                 = NULL;
        char                      path[PATH_MAX]        = {0,};
        int                       exists                = 0;
        gf_store_op_errno_t       op_errno              = GD_STORE_SUCCESS;

        ret = glusterd_volinfo_new (&volinfo);
        if (ret)
                goto out;

        strncpy (volinfo->volname, volname, GLUSTERD_MAX_VOLUME_NAME);

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, priv);
        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_VOLUME_INFO_FILE);

        ret = gf_store_handle_retrieve (path, &volinfo->shandle);
        if (ret)
                goto out;

        ret = gf_store_iter_new (volinfo->shandle, &iter);
        if (ret)
                goto out;

        ret = gf_store_iter_get_next (iter, &key, &value, &op_errno);
        if (ret)
                goto out;

        while (!ret) {
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
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_TRANSPORT,
                                     strlen (GLUSTERD_STORE_KEY_VOL_TRANSPORT))) {
                        volinfo->transport_type = atoi (value);
                        volinfo->nfs_transport_type = volinfo->transport_type;
                        if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) {
                                volinfo->nfs_transport_type = GF_DEFAULT_NFS_TRANSPORT;
                        }
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_ID,
                                     strlen (GLUSTERD_STORE_KEY_VOL_ID))) {
                        ret = uuid_parse (value, volinfo->volume_id);
                        if (ret)
                                gf_log ("", GF_LOG_WARNING,
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
                                gf_log ("",GF_LOG_ERROR, "Error in "
                                                "dict_set_str");
                                goto out;
                        }
                        gf_log ("", GF_LOG_DEBUG, "Parsed as "GEOREP" "
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
                                gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
                                        key);
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
                                        gf_log ("",GF_LOG_ERROR, "Error in "
                                                "dict_set_str");
                                        goto out;
                                }
                                gf_log ("", GF_LOG_DEBUG, "Parsed as Volume-"
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

        if (ret)
                goto out;

        ret = glusterd_store_retrieve_bricks (volinfo);
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


        list_add_tail (&volinfo->vol_list, &priv->volumes);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

inline void
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
        gf_store_handle_destroy (shandle);
        if (fd >=0 )
                close (fd);
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
glusterd_store_retrieve_volumes (xlator_t  *this)
{
        int32_t                ret              = 0;
        char                   path[PATH_MAX]   = {0,};
        glusterd_conf_t       *priv             = NULL;
        DIR                   *dir              = NULL;
        struct dirent         *entry            = NULL;
        glusterd_volinfo_t    *volinfo          = NULL;

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                        GLUSTERD_VOLUME_DIR_PREFIX);

        dir = opendir (path);

        if (!dir) {
                gf_log ("", GF_LOG_ERROR, "Unable to open dir %s", path);
                ret = -1;
                goto out;
        }

        glusterd_for_each_entry (entry, dir);

        while (entry) {
                ret = glusterd_store_retrieve_volume (entry->d_name);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to restore "
                                "volume: %s", entry->d_name);
                        goto out;
                }

                ret = glusterd_store_retrieve_rbstate (entry->d_name);
                if (ret) {
                        /* Backward compatibility */
                        gf_log ("", GF_LOG_INFO, "Creating a new rbstate "
                                "for volume: %s.", entry->d_name);
                        ret = glusterd_volinfo_find (entry->d_name, &volinfo);
                        ret = glusterd_store_create_rbstate_shandle_on_absence (volinfo);
                        ret = glusterd_store_perform_rbstate_store (volinfo);
                }

                ret = glusterd_store_retrieve_node_state (entry->d_name);
                if (ret) {
                        /* Backward compatibility */
                        gf_log ("", GF_LOG_INFO, "Creating a new node_state "
                                "for volume: %s.", entry->d_name);
                        ret = glusterd_volinfo_find (entry->d_name, &volinfo);
                        ret =
                        glusterd_store_create_nodestate_sh_on_absence (volinfo);
                        ret = glusterd_store_perform_node_state_store (volinfo);

                }
                glusterd_for_each_entry (entry, dir);
        }

out:
        if (dir)
                closedir (dir);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_store_delete_peerinfo (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;
        glusterd_conf_t                 *priv = NULL;
        char                            peerdir[PATH_MAX] = {0,};
        char                            filepath[PATH_MAX] = {0,};
        char                            hostname_path[PATH_MAX] = {0,};


        if (!peerinfo) {
                ret = 0;
                goto out;
        }

        priv = THIS->private;

        snprintf (peerdir, PATH_MAX, "%s/peers", priv->workdir);


        if (uuid_is_null (peerinfo->uuid)) {

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

                ret = unlink (hostname_path);

                if (!ret)
                        goto out;
        }

        ret = unlink (filepath);
        if (ret && (errno == ENOENT))
                ret = 0;

out:
        if (peerinfo->shandle) {
                gf_store_handle_destroy (peerinfo->shandle);
                peerinfo->shandle = NULL;
        }
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

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

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
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
        uuid_unparse (peerinfo->uuid, str);
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
        ret = stat (peerfpath, &stbuf);
        if (!ret) {
                if (peerinfo->shandle)
                        gf_store_handle_destroy (peerinfo->shandle);
                peerinfo->shandle = NULL;
                ret = unlink (peerfpath);
        }
        return ret;
}

int32_t
glusterd_store_create_peer_shandle (glusterd_peerinfo_t *peerinfo)
{
        int32_t                 ret = 0;

        GF_ASSERT (peerinfo);

        if (glusterd_peerinfo_is_uuid_unknown (peerinfo)) {
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
        char                    buf[50] = {0};
        int32_t                 ret = 0;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PEER_UUID,
                                   uuid_utoa (peerinfo->uuid));
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", peerinfo->state.state);
        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PEER_STATE, buf);
        if (ret)
                goto out;

        ret = gf_store_save_value (fd, GLUSTERD_STORE_KEY_PEER_HOSTNAME "1",
                                   peerinfo->hostname);
        if (ret)
                goto out;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
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
        if (fd > 0)
                close (fd);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
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
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_retrieve_peers (xlator_t *this)
{
        int32_t                 ret = 0;
        glusterd_conf_t         *priv = NULL;
        DIR                     *dir = NULL;
        struct dirent           *entry = NULL;
        char                    path[PATH_MAX] = {0,};
        glusterd_peerinfo_t     *peerinfo = NULL;
        uuid_t                  uuid = {0,};
        char                    *hostname = NULL;
        int32_t                 state = 0;
        gf_store_handle_t       *shandle = NULL;
        char                    filepath[PATH_MAX] = {0,};
        gf_store_iter_t         *iter = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        glusterd_peerctx_args_t args = {0};
        gf_store_op_errno_t     op_errno = GD_STORE_SUCCESS;

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                  GLUSTERD_PEER_DIR_PREFIX);

        dir = opendir (path);

        if (!dir) {
                gf_log ("", GF_LOG_ERROR, "Unable to open dir %s", path);
                ret = -1;
                goto out;
        }

        glusterd_for_each_entry (entry, dir);

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

                while (!ret) {

                        if (!strncmp (GLUSTERD_STORE_KEY_PEER_UUID, key,
                                      strlen (GLUSTERD_STORE_KEY_PEER_UUID))) {
                                if (value)
                                        uuid_parse (value, uuid);
                        } else if (!strncmp (GLUSTERD_STORE_KEY_PEER_STATE,
                                    key,
                                    strlen (GLUSTERD_STORE_KEY_PEER_STATE))) {
                                state = atoi (value);
                        } else if (!strncmp (GLUSTERD_STORE_KEY_PEER_HOSTNAME,
                                   key,
                                   strlen (GLUSTERD_STORE_KEY_PEER_HOSTNAME))) {
                                hostname = gf_strdup (value);
                        } else {
                                gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
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
                        GF_FREE(hostname);
                        goto out;
                }

                (void) gf_store_iter_destroy (iter);

                ret = glusterd_friend_add (hostname, 0, state, &uuid,
                                           &peerinfo, 1, NULL);

                GF_FREE (hostname);
                if (ret)
                        goto out;

                peerinfo->shandle = shandle;
                glusterd_for_each_entry (entry, dir);
        }

        args.mode = GD_MODE_ON;
        list_for_each_entry (peerinfo, &priv->peers, uuid_list) {
                ret = glusterd_friend_rpc_create (this, peerinfo, &args);
                if (ret)
                        goto out;
        }

out:
        if (dir)
                closedir (dir);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_resolve_all_bricks (xlator_t  *this)
{
        int32_t                 ret = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;

        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                        ret = glusterd_resolve_brick (brickinfo);
                        if (ret) {
                                gf_log ("glusterd", GF_LOG_ERROR,
                                        "resolve brick failed in restore");
                                goto out;
                        }
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_restore ()
{
        int32_t         ret = -1;
        xlator_t        *this = NULL;

        this = THIS;

        ret = glusterd_restore_op_version (this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to restore op_version");
                goto out;
        }

        ret = glusterd_store_retrieve_volumes (this);
        if (ret)
                goto out;

        ret = glusterd_store_retrieve_peers (this);
        if (ret)
                goto out;

        ret = glusterd_resolve_all_bricks (this);
        if (ret)
                goto out;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
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
                gf_log (this->name, GF_LOG_ERROR, "Unable to get store handle "
                        "for %s", cksum_path);
                goto out;
        }

        ret = gf_store_retrieve_value (handle, "version", &version_str);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "Version absent");
                ret = 0;
                goto out;
        }

        version = strtoul (version_str, &tmp, 10);
         if (version < 0) {
                 gf_log (this->name, GF_LOG_DEBUG, "Invalid version number");
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
        int                 ret                  = -1;
        char                cksum_path[PATH_MAX] = {0,};
        char                path[PATH_MAX]       = {0,};
        xlator_t           *this                 = NULL;
        glusterd_conf_t    *conf                 = NULL;
        char                buf[256]             = {0,};
        int                 fd                   = -1;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GLUSTERD_GET_VOLUME_DIR (path, volinfo, conf);
        snprintf (cksum_path, sizeof (cksum_path), "%s/%s", path,
                  GLUSTERD_VOL_QUOTA_CKSUM_FILE);

        fd = open (cksum_path, O_RDWR | O_APPEND | O_CREAT| O_TRUNC, 0600);

        if (-1 == fd) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to open %s,"
                        "Reason: %s", cksum_path, strerror (errno));
                ret = -1;
                goto out;
        }

        snprintf (buf, sizeof (buf)-1, "%u", volinfo->quota_conf_cksum);
        ret = gf_store_save_value (fd, "cksum", buf);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to store cksum");
                goto out;
        }

        memset (buf, 0, sizeof (buf));
        snprintf (buf, sizeof (buf)-1, "%u", volinfo->quota_conf_version);
        ret = gf_store_save_value (fd, "version", buf);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to store version");
                goto out;
        }

        ret = 0;

out:
        if (fd != -1)
                close (fd);
        return ret;
}
