/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
#include "glusterd-store.h"

#include "glusterd1.h"
#include "cli1.h"
#include "rpc-clnt.h"
#include "common-utils.h"

#include <sys/resource.h>
#include <inttypes.h>
#include <dirent.h>

static int32_t
glusterd_store_create_volume_dir (char *volname)
{
        int32_t                 ret = -1;
        char                    path[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (volname);
        priv = THIS->private;

        GF_ASSERT (priv);

        snprintf (path, 1024, "%s/vols/%s", priv->workdir,
                  volname);

        ret = mkdir (path, 0777);

        if (-1 == ret) {
                gf_log ("", GF_LOG_ERROR, "mkdir() failed on path %s,"
                        "errno: %d", path, errno);
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_create_brick (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo, int32_t brick_count)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    brickpath[PATH_MAX] = {0,};
        struct  stat            stbuf = {0,};
        char                    buf[4096] = {0,};
        char                    *tmppath = NULL;
        char                    *ptr = NULL;
        glusterd_store_handle_t *shandle = NULL;
        char                    tmpbuf[4096] = {0,};

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        priv = THIS->private;

        GF_ASSERT (priv);

        GLUSTERD_GET_BRICK_DIR (path, volinfo, priv);

        ret = stat (path, &stbuf);

        if (ret == -1 && ENOENT == errno) {
                ret = mkdir (path, 0777);
                if (ret)
                        goto out;
        }

        tmppath = gf_strdup (brickinfo->path);

        ptr = strchr (tmppath, '/');

        while (ptr) {
                *ptr = '-';
                ptr = strchr (tmppath, '/');
        }

        snprintf (brickpath, sizeof (brickpath), "%s/%s:%s",
                  path, brickinfo->hostname, tmppath);

        ret = glusterd_store_handle_new (brickpath, &brickinfo->shandle);

        if (ret)
                goto out;

        shandle = brickinfo->shandle;
        shandle->fd = open (brickpath, O_RDWR | O_CREAT | O_APPEND, 0666);

        if (shandle->fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Open failed on %s",
                        brickpath);
                ret = -1;
                goto out;
        }


        snprintf (buf, sizeof(buf), "%s=%s\n",
                  GLUSTERD_STORE_KEY_BRICK_HOSTNAME, brickinfo->hostname);
        ret = write (shandle->fd, buf, strlen(buf));
        if (ret)
                gf_log ("", GF_LOG_TRACE, "failed to write brick->hostname");
        snprintf (buf, sizeof(buf), "%s=%s\n",
                  GLUSTERD_STORE_KEY_BRICK_PATH, brickinfo->path);
        ret = write (shandle->fd, buf, strlen(buf));
        if (ret)
                gf_log ("", GF_LOG_TRACE, "failed to write brick->path");
        snprintf (buf, sizeof(buf), "%s=%d\n",
                  GLUSTERD_STORE_KEY_BRICK_PORT, brickinfo->port);
        ret = write (shandle->fd, buf, strlen(buf));
        if (ret)
                gf_log ("", GF_LOG_TRACE, "failed to write brick->port");

        ret = 0;

        snprintf (buf, sizeof (buf), "%s-%d",GLUSTERD_STORE_KEY_VOL_BRICK,
                  brick_count);
        snprintf (tmpbuf, sizeof (tmpbuf), "%s:%s", brickinfo->hostname,
                  tmppath);
        ret = glusterd_store_save_value (volinfo->shandle, buf, tmpbuf);

        GF_FREE (tmppath);

out:
        if (shandle->fd > 0) {
                close (shandle->fd);
        }
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_delete_brick (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    brickpath[PATH_MAX] = {0,};
        char                    *ptr = NULL;
        char                    *tmppath = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        priv = THIS->private;

        GF_ASSERT (priv);

        GLUSTERD_GET_BRICK_DIR (path, volinfo, priv);

        tmppath = gf_strdup (brickinfo->path);

        ptr = strchr (tmppath, '/');

        while (ptr) {
                *ptr = '-';
                ptr = strchr (tmppath, '/');
        }

        snprintf (brickpath, sizeof (brickpath), "%s/%s:%s",
                  path, brickinfo->hostname, tmppath);

        GF_FREE (tmppath);

        ret = unlink (brickpath);

        if ((ret < 0) && (errno != ENOENT)) {
                gf_log ("", GF_LOG_ERROR, "Unlink failed on %s, reason: %s",
                        brickpath, strerror(errno));
                ret = -1;
                goto out;
        } else {
                ret = 0;
        }

out:
        if (brickinfo->shandle)
                glusterd_store_handle_destroy (brickinfo->shandle);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_remove_bricks (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = 0;
        glusterd_brickinfo_t    *tmp = NULL;
        glusterd_conf_t         *priv = NULL;
        char                    brickdir [PATH_MAX] = {0,};
        DIR                     *dir = NULL;
        struct dirent           *entry = NULL;
        char                    path[PATH_MAX] = {0,};

        GF_ASSERT (volinfo);

        list_for_each_entry (tmp, &volinfo->bricks, brick_list) {
                ret = glusterd_store_delete_brick (volinfo, tmp);
                if (ret)
                        goto out;
        }

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_BRICK_DIR (brickdir, volinfo, priv);

        dir = opendir (brickdir);

        glusterd_for_each_entry (entry, dir);

        while (entry) {
                snprintf (path, sizeof (path), "%s/%s",
                          brickdir, entry->d_name);
                ret = unlink (path);
                if (ret && errno != ENOENT) {
                        gf_log ("", GF_LOG_ERROR, "Unable to unlink %s, "
                                "reason: %s", path, strerror(errno));
                }
                glusterd_for_each_entry (entry, dir);
        }

        closedir (dir);

        ret = rmdir (brickdir);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

void _setopts (dict_t *this, char *key, data_t *value, void *data)
{
        int                      ret = 0;
        glusterd_store_handle_t *shandle = NULL;
        int                      exists = 0;


        shandle = (glusterd_store_handle_t *) data;

        GF_ASSERT (shandle);
        if (!key)
                return;
        if (!value || !value->data)
                return;

        exists = glusterd_check_option_exists (key, NULL);
        if (1 == exists) {
                gf_log ("", GF_LOG_DEBUG, "Storing in volinfo:key= %s, val=%s",
                        key, value->data);
        } else {
                gf_log ("", GF_LOG_DEBUG, "Discarding:key= %s, val=%s",
                        key, value->data);
                return;
        }

        ret = glusterd_store_save_value (shandle, key, value->data);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to write into store"
                                " handle for path: %s", shandle->path);
                return;
        }
}

int32_t
glusterd_store_create_volume (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;
        char                    filepath[PATH_MAX] = {0,};
        char                    buf[4096] = {0,};
        glusterd_conf_t         *priv = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        int32_t                 brick_count = 0;

        GF_ASSERT (volinfo);
        priv = THIS->private;

        GF_ASSERT (priv);

        ret = glusterd_store_create_volume_dir (volinfo->volname);

        if (ret)
                goto out;

        snprintf (filepath, 1024, "%s/%s/%s/%s", priv->workdir,
                  GLUSTERD_VOLUME_DIR_PREFIX, volinfo->volname,
                  GLUSTERD_VOLUME_INFO_FILE);

        ret = glusterd_store_handle_new (filepath, &volinfo->shandle);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create store"
                        " handle for path: %s", filepath);
                goto out;
        }

        snprintf (buf, sizeof (buf), "%d", volinfo->type);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_TYPE, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->brick_count);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->status);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_STATUS, buf);
        if (ret)
                goto out;

/*        snprintf (buf, sizeof (buf), "%d", volinfo->port);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_PORT, buf);
        if (ret)
                goto out;
*/
        snprintf (buf, sizeof (buf), "%d", volinfo->sub_count);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_SUB_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->version);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_VERSION, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->transport_type);
        ret = glusterd_store_save_value (volinfo->shandle,
                                         GLUSTERD_STORE_KEY_VOL_TRANSPORT, buf);
        if (ret)
                goto out;

        ret = glusterd_store_save_value (volinfo->shandle,
                                         GLUSTERD_STORE_KEY_VOL_ID,
                                         uuid_utoa (volinfo->volume_id));
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_store_create_brick (volinfo, brickinfo,
                                                   brick_count);
                if (ret)
                        goto out;
                brick_count++;
        }

        dict_foreach (volinfo->dict, _setopts, volinfo->shandle);

        ret = 0;

out:
        if (ret) {
                glusterd_store_delete_volume (volinfo);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_store_delete_volume (glusterd_volinfo_t *volinfo)
{
        char    pathname[PATH_MAX] = {0,};
        int32_t ret = 0;
        glusterd_conf_t *priv = NULL;
        DIR     *dir = NULL;
        struct dirent *entry = NULL;
        char path[PATH_MAX] = {0,};
        struct stat     st = {0, };

        GF_ASSERT (volinfo);
        priv = THIS->private;

        GF_ASSERT (priv);
        snprintf (pathname, 1024, "%s/vols/%s", priv->workdir,
                  volinfo->volname);

        dir = opendir (pathname);
        if (!dir)
                goto out;
        ret = glusterd_store_remove_bricks (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Remove bricks failed");
        }

        glusterd_for_each_entry (entry, dir);
        while (entry) {

                snprintf (path, PATH_MAX, "%s/%s", pathname, entry->d_name);
                ret = stat (path, &st);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Failed to stat entry: %s:%s",
                                path, strerror (errno));
                        goto stat_failed;
                }

                if (S_ISDIR (st.st_mode))
                        ret = rmdir (path);
                else
                        ret = unlink (path);

                gf_log ("", GF_LOG_NORMAL, "%s %s",
                                ret?"Failed to remove":"Removed",
                                entry->d_name);
                if (ret)
                        gf_log ("", GF_LOG_NORMAL, "errno:%d", errno);
stat_failed:
                memset (path, 0, sizeof(path));
                glusterd_for_each_entry (entry, dir);
        }

        ret = closedir (dir);
        if (ret) {
                gf_log ("", GF_LOG_NORMAL, "Failed to close dir, errno:%d",
                        errno);
        }

        ret = rmdir (pathname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Failed to rmdir: %s, errno: %d",
                        pathname, errno);
        }


out:
        if (volinfo->shandle)
                glusterd_store_handle_destroy (volinfo->shandle);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}



int32_t
glusterd_store_retrieve_value (glusterd_store_handle_t *handle,
                               char *key, char **value)
{
        int32_t         ret = -1;
        char            scan_str[4096] = {0,};
        char            *iter_key = NULL;
        char            *iter_val = NULL;
        char            *str = NULL;
        char            *free_str = NULL;

        GF_ASSERT (handle);

        handle->fd = open (handle->path, O_RDWR);

        if (!handle->read)
                handle->read = fdopen (handle->fd, "r");

        if (!handle->read) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %d",
                        handle->path, errno);
                goto out;
        }

        ret = fscanf (handle->read, "%s", scan_str);

        while (ret != EOF) {
                if (free_str) {
                        GF_FREE (free_str);
                        free_str = NULL;
                }
                str = gf_strdup (scan_str);
                if (!str)
                        goto out;
                else
                        free_str = str;
                iter_key = strtok (str, "=");
                gf_log ("", GF_LOG_DEBUG, "key %s read", iter_key);

                if (!strcmp (key, iter_key)) {
                        gf_log ("", GF_LOG_DEBUG, "key %s found", key);
                        iter_val = strtok (NULL, "=");
                        ret = 0;
                        if (iter_val)
                                *value = gf_strdup (iter_val);
                        goto out;
                }

                ret = fscanf (handle->read, "%s", scan_str);
        }

        if (EOF == ret)
                ret = -1;
out:
        if (handle->fd > 0) {
                close (handle->fd);
                handle->read = NULL;
        }

        if (free_str)
                GF_FREE (free_str);

        return ret;
}

int32_t
glusterd_store_save_value (glusterd_store_handle_t *handle,
                           char *key, char *value)
{
        int32_t         ret = -1;
        char            buf[4096] = {0,};

        GF_ASSERT (handle);
        GF_ASSERT (key);
        GF_ASSERT (value);

        handle->fd = open (handle->path, O_RDWR | O_APPEND);

        if (handle->fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to open %s, errno: %d",
                        handle->path, errno);
                goto out;
        }

        snprintf (buf, sizeof (buf), "%s=%s\n", key, value);
        ret = write (handle->fd, buf, strlen (buf));

        if (ret < 0) {
                gf_log ("", GF_LOG_CRITICAL, "Unable to store key: %s,"
                        "value: %s, error: %s", key, value,
                        strerror (errno));
                ret = -1;
                goto out;
        }

        ret = 0;

out:

        if (handle->fd > 0) {
                close (handle->fd);
                handle->fd = -1;
        }

        gf_log ("", GF_LOG_DEBUG, "returning: %d", ret);
        return ret;
}

int32_t
glusterd_store_handle_new (char *path, glusterd_store_handle_t **handle)
{
        int32_t                 ret = -1;
        glusterd_store_handle_t *shandle = NULL;

        shandle = GF_CALLOC (1, sizeof (*shandle), gf_gld_mt_store_handle_t);
        if (!shandle)
                goto out;

        shandle->path = gf_strdup (path);

        if (!shandle->path)
                goto out;

        shandle->fd = open (path, O_RDWR | O_CREAT | O_APPEND, 0644);
        if (!shandle->fd)
                goto out;

        *handle = shandle;

        ret = 0;

out:
        if (ret == -1) {
                if (shandle) {
                        if (shandle->path)
                                GF_FREE (shandle->path);
                        if (shandle->fd > 0)
                                close (shandle->fd);
                        GF_FREE (shandle);
                }
        } else {
                close (shandle->fd);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_store_handle_retrieve (char *path, glusterd_store_handle_t **handle)
{
        int32_t                 ret = -1;
        struct stat statbuf = {0};

        ret = stat (path, &statbuf);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to retrieve store "
                        "handle for %s, error: %s", path, strerror (errno));
                goto out;
        }
        ret =  glusterd_store_handle_new (path, handle);
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_handle_destroy (glusterd_store_handle_t *handle)
{
        int32_t                 ret = -1;

        if (!handle) {
                ret = 0;
                goto out;
        }

        GF_FREE (handle->path);

        GF_FREE (handle);

        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_store_handle_truncate (glusterd_store_handle_t *handle)
{
        int32_t         ret = -1;

        GF_ASSERT (handle);
        GF_ASSERT (handle->path);

        ret = truncate (handle->path, 0);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
glusterd_store_uuid ()
{
        glusterd_conf_t *priv = NULL;
        char            path[PATH_MAX] = {0,};
        int32_t         ret = -1;
        glusterd_store_handle_t *handle = NULL;

        priv = THIS->private;

        snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                  GLUSTERD_INFO_FILE);

        if (!priv->handle) {
                ret = glusterd_store_handle_new (path, &handle);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get store"
                                " handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = glusterd_store_save_value (priv->handle, GLUSTERD_STORE_UUID_KEY,
                                         uuid_utoa (priv->uuid));

        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Storing uuid failed"
                        "ret = %d", ret);
                goto out;
        }


out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
glusterd_retrieve_uuid ()
{
        char            *uuid_str = NULL;
        int32_t         ret = -1;
        glusterd_store_handle_t *handle = NULL;
        glusterd_conf_t *priv = NULL;
        char            path[PATH_MAX] = {0,};

        priv = THIS->private;


        if (!priv->handle) {
                snprintf (path, PATH_MAX, "%s/%s", priv->workdir,
                          GLUSTERD_INFO_FILE);
                ret = glusterd_store_handle_retrieve (path, &handle);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to get store "
                                "handle!");
                        goto out;
                }

                priv->handle = handle;
        }

        ret = glusterd_store_retrieve_value (priv->handle,
                                             GLUSTERD_STORE_UUID_KEY,
                                             &uuid_str);

        if (ret) {
                gf_log ("", GF_LOG_NORMAL, "No previous uuid is present");
                goto out;
        }

        uuid_parse (uuid_str, priv->uuid);

out:
        if (uuid_str)
                GF_FREE (uuid_str);
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_store_iter_new (glusterd_store_handle_t  *shandle,
                         glusterd_store_iter_t  **iter)
{
        int32_t                 ret = -1;
        glusterd_store_iter_t   *tmp_iter = NULL;
        int                     fd = -1;

        GF_ASSERT (shandle);
        GF_ASSERT (iter);

        tmp_iter = GF_CALLOC (1, sizeof (*tmp_iter),
                             gf_gld_mt_store_iter_t);

        if (!tmp_iter) {
                gf_log ("", GF_LOG_ERROR, "Out of Memory");
                goto out;
        }

        fd = open (shandle->path, O_RDWR);

        if (fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to open %s, errno: %d",
                        shandle->path, errno);
                goto out;
        }

        tmp_iter->fd = fd;

        tmp_iter->file = fdopen (shandle->fd, "r");

        if (!tmp_iter->file) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %d",
                        shandle->path, errno);
                goto out;
        }

        strncpy (tmp_iter->filepath, shandle->path, sizeof (tmp_iter->filepath));
        *iter = tmp_iter;
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_validate_key_value (char *storepath, char *key, char*val,
                                   glusterd_store_op_errno_t *op_errno)
{
        int ret = 0;

        GF_ASSERT (op_errno);
        GF_ASSERT (storepath);

        if ((key == NULL) && (val == NULL)) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Glusterd store may be "
                        "corrupted, Invalid key and value (null) in %s",
                        storepath);
                *op_errno = GD_STORE_KEY_VALUE_NULL;
        } else if (key == NULL) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Glusterd store may be "
                        "corrupted, Invalid key (null) in %s", storepath);
                *op_errno = GD_STORE_KEY_NULL;
        } else if (val == NULL) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Glusterd store may be "
                        "corrupted, Invalid value (null) for key %s in %s",
                        key, storepath);
                *op_errno = GD_STORE_VALUE_NULL;
        } else {
                ret = 0;
                *op_errno = GD_STORE_SUCCESS;
        }

        return ret;
}

int32_t
glusterd_store_iter_get_next (glusterd_store_iter_t *iter,
                              char  **key, char **value,
                              glusterd_store_op_errno_t *op_errno)
{
        int32_t         ret = -1;
        char            scan_str[4096] = {0,};
        char            *str = NULL;
        char            *free_str = NULL;
        char            *iter_key = NULL;
        char            *iter_val = NULL;
        glusterd_store_op_errno_t store_errno = GD_STORE_SUCCESS;

        GF_ASSERT (iter);
        GF_ASSERT (iter->file);
        GF_ASSERT (key);
        GF_ASSERT (value);

        *key = NULL;
        *value = NULL;

        ret = fscanf (iter->file, "%s", scan_str);

        if (ret <= 0) {
                ret = -1;
                store_errno = GD_STORE_EOF;
                goto out;
        }

        str = gf_strdup (scan_str);
        if (!str) {
                ret = -1;
                store_errno = GD_STORE_ENOMEM;
                goto out;
        } else {
                free_str = str;
        }

        iter_key = strtok (str, "=");
        iter_val = strtok (NULL, "=");

        ret = glusterd_store_validate_key_value (iter->filepath, iter_key,
                                                 iter_val, &store_errno);
        if (ret)
                goto out;

        *value = gf_strdup (iter_val);

        *key   = gf_strdup (iter_key);
        if (!iter_key || !iter_val) {
                ret = -1;
                store_errno = GD_STORE_ENOMEM;
                goto out;
        }

        ret = 0;

out:
        if (ret) {
                if (*key) {
                        GF_FREE (*key);
                        *key = NULL;
                }
                if (*value) {
                        GF_FREE (*value);
                        *value = NULL;
                }
        }
        if (free_str)
                GF_FREE (free_str);
        if (op_errno)
                *op_errno = store_errno;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_iter_get_matching (glusterd_store_iter_t *iter,
                              char  *key, char **value)
{
        int32_t ret = -1;
        char    *tmp_key = NULL;
        char    *tmp_value = NULL;

        ret = glusterd_store_iter_get_next (iter, &tmp_key, &tmp_value,
                                            NULL);
        while (!ret) {
                if (!strncmp (key, tmp_key, strlen (key))){
                        *value = tmp_value;
                        GF_FREE (tmp_key);
                        goto out;
                }
                GF_FREE (tmp_key);
                GF_FREE (tmp_value);
                ret = glusterd_store_iter_get_next (iter, &tmp_key,
                                                    &tmp_value, NULL);
        }
out:
        return ret;
}

int32_t
glusterd_store_iter_destroy (glusterd_store_iter_t *iter)
{
        int32_t         ret = -1;

        GF_ASSERT (iter);
        GF_ASSERT (iter->fd > 0);

        ret = fclose (iter->file);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to close fd: %d, ret: %d, "
                        "errno: %d" ,iter->fd, ret, errno);
        }

        GF_FREE (iter);

        return ret;
}

char*
glusterd_store_strerror (glusterd_store_op_errno_t op_errno)
{
        switch (op_errno) {
        case GD_STORE_SUCCESS:
                return "Success";
        case GD_STORE_KEY_NULL:
                return "Invalid Key";
        case GD_STORE_VALUE_NULL:
                return "Invalid Value";
        case GD_STORE_KEY_VALUE_NULL:
                return "Invalid Key and Value";
        case GD_STORE_EOF:
                return "No data";
        case GD_STORE_ENOMEM:
                return "No memory";
        default:
                return "Invalid errno";
        }
        return "Invalid errno";
}

int32_t
glusterd_store_retrieve_bricks (glusterd_volinfo_t *volinfo)
{

        int32_t                 ret = 0;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_store_iter_t   *iter = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        char                    brickdir[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;
        int32_t                 brick_count = 0;
        char                    tmpkey[4096] = {0,};
        glusterd_store_iter_t   *tmpiter = NULL;
        char                    *tmpvalue = NULL;
        struct pmap_registry *pmap = NULL;
        glusterd_store_op_errno_t       op_errno = GD_STORE_SUCCESS;

        GF_ASSERT (volinfo);
        GF_ASSERT (volinfo->volname);

        priv = THIS->private;

        GLUSTERD_GET_BRICK_DIR (brickdir, volinfo, priv)

        ret = glusterd_store_iter_new (volinfo->shandle, &tmpiter);

        if (ret)
                goto out;

        while (brick_count < volinfo->brick_count) {
                ret = glusterd_brickinfo_new (&brickinfo);

                if (ret)
                        goto out;
                snprintf (tmpkey, sizeof (tmpkey), "%s-%d",
                          GLUSTERD_STORE_KEY_VOL_BRICK,brick_count);
                ret = glusterd_store_iter_get_matching (tmpiter, tmpkey,
                                                        &tmpvalue);
                snprintf (path, sizeof (path), "%s/%s", brickdir, tmpvalue);

                GF_FREE (tmpvalue);

                tmpvalue = NULL;

                ret = glusterd_store_handle_retrieve (path, &brickinfo->shandle);

                if (ret)
                        goto out;

                ret = glusterd_store_iter_new (brickinfo->shandle, &iter);

                if (ret)
                        goto out;

                ret = glusterd_store_iter_get_next (iter, &key, &value,
                                                    &op_errno);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to iterate "
                                "the store for brick: %s, reason: %s", path,
                                glusterd_store_strerror (op_errno));
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
                                /* This is required to have proper ports
                                   assigned to bricks after restart */
                                pmap = pmap_registry_get (THIS);
                                if (pmap->last_alloc <= brickinfo->port)
                                        pmap->last_alloc = brickinfo->port + 1;
                        } else {
                                gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
                                        key);
                        }

                        GF_FREE (key);
                        GF_FREE (value);
                        key = NULL;
                        value = NULL;

                        ret = glusterd_store_iter_get_next (iter, &key, &value,
                                                            &op_errno);
                }

                if (op_errno != GD_STORE_EOF)
                        goto out;
                ret = glusterd_store_iter_destroy (iter);

                if (ret)
                        goto out;

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                brick_count++;
        }

        ret = glusterd_store_iter_destroy (tmpiter);
        if (ret)
                goto out;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


int32_t
glusterd_store_retrieve_volume (char    *volname)
{
        int32_t                 ret = -1;
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_store_iter_t   *iter = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        char                    volpath[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;
        char                    path[PATH_MAX] = {0,};
        int                     exists = 0;
        glusterd_store_op_errno_t op_errno = GD_STORE_SUCCESS;

        ret = glusterd_volinfo_new (&volinfo);

        if (ret)
                goto out;

        strncpy (volinfo->volname, volname, GLUSTERD_MAX_VOLUME_NAME);

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, priv);
        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_VOLUME_INFO_FILE);

        ret = glusterd_store_handle_retrieve (path, &volinfo->shandle);

        if (ret)
                goto out;

        ret = glusterd_store_iter_new (volinfo->shandle, &iter);

        if (ret)
                goto out;

        ret = glusterd_store_iter_get_next (iter, &key, &value, &op_errno);
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
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_TRANSPORT,
                            strlen (GLUSTERD_STORE_KEY_VOL_TRANSPORT))) {
                        volinfo->transport_type = atoi (value);
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_ID,
                            strlen (GLUSTERD_STORE_KEY_VOL_ID))) {
                        ret = uuid_parse (value, volinfo->volume_id);
                        if (ret)
                                gf_log ("", GF_LOG_WARNING,
                                        "failed to parse uuid");
                } else {
                        exists = glusterd_check_option_exists (key, NULL);
                        if (exists == -1) {
                                ret = -1;
                                goto out;
                        }
                        if (exists) {
                                ret = dict_set_str(volinfo->dict, key,
                                                     gf_strdup (value));
                                if (ret) {
                                        gf_log ("",GF_LOG_ERROR, "Error in "
                                                        "dict_set_str");
                                        goto out;
                                }
                                gf_log ("", GF_LOG_DEBUG, "Parsed as Volume-"
                                                "set:key=%s,value:%s",
                                                                key, value);
                        }
                        else
                                gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
                                        key);
                }

                GF_FREE (key);
                GF_FREE (value);
                key = NULL;
                value = NULL;

                ret = glusterd_store_iter_get_next (iter, &key, &value,
                                                    &op_errno);
        }
        if (op_errno != GD_STORE_EOF)
                goto out;

        ret = glusterd_store_iter_destroy (iter);

        if (ret)
                goto out;

        ret = glusterd_store_retrieve_bricks (volinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_compute_cksum (volinfo);
        if (ret)
                goto out;

        list_add_tail (&volinfo->vol_list, &priv->volumes);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


int32_t
glusterd_store_retrieve_volumes (xlator_t  *this)
{
        int32_t         ret = 0;
        char            path[PATH_MAX] = {0,};
        glusterd_conf_t *priv = NULL;
        DIR             *dir = NULL;
        struct dirent   *entry = NULL;

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
                glusterd_for_each_entry (entry, dir);
        }

out:
        if (dir)
                closedir (dir);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}

int32_t
glusterd_store_update_volume (glusterd_volinfo_t *volinfo)
{
        int32_t         ret = -1;
        char            buf[1024] = {0,};
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_brickinfo_t    *tmp = NULL;
        int32_t         brick_count = 0;


        list_for_each_entry (tmp, &volinfo->bricks, brick_list) {
                ret = glusterd_store_delete_brick (volinfo, tmp);
                //if (ret)
                  //      goto out;
        }

        ret = glusterd_store_handle_truncate (volinfo->shandle);

        snprintf (buf, sizeof (buf), "%d", volinfo->type);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_TYPE, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->brick_count);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->status);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_STATUS, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->sub_count);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_SUB_COUNT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->version);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_VERSION, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->transport_type);
        ret = glusterd_store_save_value (volinfo->shandle,
                                         GLUSTERD_STORE_KEY_VOL_TRANSPORT, buf);
        if (ret)
                goto out;

        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_ID,
                                        uuid_utoa (volinfo->volume_id));
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_store_create_brick (volinfo, brickinfo,
                                                   brick_count);
                if (ret)
                        goto out;
                brick_count++;
        }

        dict_foreach (volinfo->dict, _setopts, volinfo->shandle);

        ret = 0;


out:
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
        if (peerinfo->shandle)
                glusterd_store_handle_destroy(peerinfo->shandle);
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);

        return ret;
}


int32_t
glusterd_store_update_peerinfo (glusterd_peerinfo_t *peerinfo)
{
        int32_t                         ret = -1;
        struct  stat                    stbuf = {0,};
        glusterd_conf_t                 *priv = NULL;
        char                            peerdir[PATH_MAX] = {0,};
        char                            filepath[PATH_MAX] = {0,};
        char                            buf[4096] = {0,};
        char                            hostname_path[PATH_MAX] = {0,};


        if (!peerinfo) {
                ret = 0;
                goto out;
        }

        priv = THIS->private;

        snprintf (peerdir, PATH_MAX, "%s/peers", priv->workdir);

        ret = stat (peerdir, &stbuf);

        if (-1 == ret) {
                ret = mkdir (peerdir, 0777);
                if (ret)
                        goto out;
        }

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

                ret = stat (hostname_path, &stbuf);

                if (!ret) {
                        gf_log ("", GF_LOG_DEBUG, "Destroying store handle");
                        glusterd_store_handle_destroy (peerinfo->shandle);
                        peerinfo->shandle = NULL;
                        ret = remove (hostname_path);
                }
        }


        if (!peerinfo->shandle) {
                ret = glusterd_store_handle_new (filepath, &peerinfo->shandle);
                if (ret)
                        goto out;
                ret = glusterd_store_handle_truncate (peerinfo->shandle);
        } else {
                ret = glusterd_store_handle_truncate (peerinfo->shandle);
                if (ret)
                        goto out;
        }

        ret = glusterd_store_save_value (peerinfo->shandle,
                                         GLUSTERD_STORE_KEY_PEER_UUID,
                                         uuid_utoa (peerinfo->uuid));
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", peerinfo->state.state);
        ret = glusterd_store_save_value (peerinfo->shandle,
                                         GLUSTERD_STORE_KEY_PEER_STATE, buf);
        if (ret)
                goto out;

        ret = glusterd_store_save_value (peerinfo->shandle,
                                         GLUSTERD_STORE_KEY_PEER_HOSTNAME "1",
                                         peerinfo->hostname);

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
        glusterd_store_handle_t *shandle = NULL;
        char                    filepath[PATH_MAX] = {0,};
        glusterd_store_iter_t   *iter = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        glusterd_peerctx_args_t args = {0};
        glusterd_store_op_errno_t op_errno = GD_STORE_SUCCESS;

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
                ret = glusterd_store_handle_retrieve (filepath, &shandle);
                if (ret)
                        goto out;

                ret = glusterd_store_iter_new (shandle, &iter);
                if (ret)
                        goto out;

                ret = glusterd_store_iter_get_next (iter, &key, &value,
                                                    &op_errno);
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

                        ret = glusterd_store_iter_get_next (iter, &key, &value,
                                                            &op_errno);
                }
                if (op_errno != GD_STORE_EOF)
                        goto out;

                (void) glusterd_store_iter_destroy (iter);

                args.mode = GD_MODE_SWITCH_ON;
                ret = glusterd_friend_add (hostname, 0, state, &uuid,
                                           NULL, &peerinfo, 1, &args);

                if (ret)
                        goto out;

                peerinfo->shandle = shandle;
                glusterd_for_each_entry (entry, dir);
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
        int             ret = -1;
        xlator_t        *this = NULL;

        this = THIS;

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
