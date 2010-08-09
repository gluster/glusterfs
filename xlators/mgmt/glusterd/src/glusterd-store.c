/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
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

        ret = mkdir (path, 0x777);

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
                             glusterd_brickinfo_t *brickinfo)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    brickpath[PATH_MAX] = {0,};
        struct  stat            stbuf = {0,};
        int                     fd = -1;
        char                    buf[4096] = {0,};
        char                    *tmppath = NULL;
        char                    *ptr = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (brickinfo);

        priv = THIS->private;

        GF_ASSERT (priv);

        GLUSTERD_GET_BRICK_DIR (path, volinfo, priv);

        ret = stat (path, &stbuf);

        if (ret == -1 && ENOENT == errno) {
                ret = mkdir (path, 0x777);
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

        GF_FREE (tmppath);

        fd = open (brickpath, O_RDWR | O_CREAT | O_APPEND, 0666);

        if (fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Open failed on %s",
                        brickpath);
                ret = -1;
                goto out;
        }


        snprintf (buf, sizeof(buf), "hostname=%s\n", brickinfo->hostname);
        write (fd, buf, strlen(buf));
        snprintf (buf, sizeof(buf), "path=%s\n", brickinfo->path);
        write (fd, buf, strlen(buf));

        ret = 0;

out:
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

        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "Unlink failed on %s",
                        brickpath);
                ret = -1;
                goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_remove_bricks (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;
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
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to unlink %s",
                                path);
                }
                glusterd_for_each_entry (entry, dir);
        }

        closedir (dir);

        ret = rmdir (brickdir);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_create_volume (glusterd_volinfo_t *volinfo)
{
        int32_t                 ret = -1;
        char                    filepath[PATH_MAX] = {0,};
        char                    buf[4096] = {0,};
        glusterd_conf_t         *priv = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;

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

        snprintf (buf, sizeof (buf), "%d", volinfo->status);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_PORT, buf);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", volinfo->sub_count);
        ret = glusterd_store_save_value (volinfo->shandle,
                                        GLUSTERD_STORE_KEY_VOL_SUB_COUNT, buf);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_store_create_brick (volinfo, brickinfo);
                if (ret)
                        goto out;
        }

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
        int32_t ret = -1;
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
                str = gf_strdup (scan_str);
                if (!str)
                        goto out;
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
        if (handle->fd >= 0)
                close (handle->fd);

        return ret;
}

int32_t
glusterd_store_save_value (glusterd_store_handle_t *handle,
                           char *key, char *value)
{
        int32_t         ret = -1;
        char            buf[4096] = {0,};

        GF_ASSERT (handle);
        GF_ASSERT (handle->fd > 0);
        GF_ASSERT (key);
        GF_ASSERT (value);

        if (!handle->write)
                handle->write = fdopen (handle->fd, "a+");

        if (!handle->write) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %d",
                        handle->path, errno);
                goto out;
        }

        snprintf (buf, sizeof (buf), "%s=%s\n", key, value);
        ret = write (handle->fd, buf, strlen (buf));
        //ret = fprintf (handle->write, "%s=%s\n", key, value);

        if (ret < 0) {
                gf_log ("", GF_LOG_CRITICAL, "Unable to store key: %s,"
                        "value: %s, error: %s", key, value,
                        strerror (errno));
                ret = -1;
                goto out;
        }

        ret = 0;

out:

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
                        GF_FREE (shandle);
                }
        }

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
        char            str[GLUSTERD_UUID_LEN] = {0,};
        glusterd_conf_t *priv = NULL;
        char            path[PATH_MAX] = {0,};
        int32_t         ret = -1;
        glusterd_store_handle_t *handle = NULL;

        priv = THIS->private;

        uuid_unparse (priv->uuid, str);

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
                                         str);

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
                ret = glusterd_store_handle_new (path, &handle);

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
                gf_log ("", GF_LOG_CRITICAL, "Retrieving uuid failed"
                        " ret = %d", ret);
                goto out;
        }

        uuid_parse (uuid_str, priv->uuid);

out:
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
        GF_ASSERT (shandle->fd > 0);
        GF_ASSERT (iter);

        tmp_iter = GF_CALLOC (1, sizeof (*tmp_iter),
                             gf_gld_mt_store_iter_t);

        if (!tmp_iter) {
                gf_log ("", GF_LOG_ERROR, "Out of Memory");
                goto out;
        }

        fd = open (shandle->path, O_RDWR);

        if (fd < 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to open %s",
                        shandle->path);
                goto out;
        }

        tmp_iter->fd = fd;

        tmp_iter->file = fdopen (shandle->fd, "r");

        if (!tmp_iter->file) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %d",
                        shandle->path, errno);
                goto out;
        }

        *iter = tmp_iter;
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_iter_get_next (glusterd_store_iter_t *iter,
                              char  **key, char **value)
{
        int32_t         ret = -1;
        char            scan_str[4096] = {0,};
        char            *str = NULL;
        char            *iter_key = NULL;
        char            *iter_val = NULL;

        GF_ASSERT (iter);
        GF_ASSERT (iter->file);

        ret = fscanf (iter->file, "%s", scan_str);

        if (ret <= 0) {
                ret = -1;
                goto out;
        }

        str = gf_strdup (scan_str);
        if (!str)
                goto out;

        iter_key = strtok (str, "=");
        gf_log ("", GF_LOG_DEBUG, "key %s read", iter_key);


        iter_val = strtok (NULL, "=");
        gf_log ("", GF_LOG_DEBUG, "value %s read", iter_val);

        if (iter_val)
                *value = gf_strdup (iter_val);
        *key   = gf_strdup (iter_key);

        ret = 0;

out:
        if (str)
                GF_FREE (str);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
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

int32_t
glusterd_store_retrieve_bricks (glusterd_volinfo_t *volinfo)
{

        int32_t                 ret = -1;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_store_iter_t   *iter = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        char                    brickdir[PATH_MAX] = {0,};
        char                    path[PATH_MAX] = {0,};
        glusterd_conf_t         *priv = NULL;
        DIR                     *dir = NULL;
        struct dirent           *entry = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (volinfo->volname);

        priv = THIS->private;

        GLUSTERD_GET_BRICK_DIR (brickdir, volinfo, priv);

        dir = opendir (brickdir);

        glusterd_for_each_entry (entry, dir);

        while (entry) {
                ret = glusterd_brickinfo_new (&brickinfo);

                if (ret)
                        goto out;

                snprintf (path, sizeof (path), "%s/%s", brickdir,
                          entry->d_name);

                ret = glusterd_store_handle_new (path, &brickinfo->shandle);

                if (ret)
                        goto out;

                ret = glusterd_store_iter_new (brickinfo->shandle, &iter);

                if (ret)
                        goto out;

                ret = glusterd_store_iter_get_next (iter, &key, &value);

                while (!ret) {
                        if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_HOSTNAME,
                                      strlen (GLUSTERD_STORE_KEY_BRICK_HOSTNAME))) {
                                strncpy (brickinfo->hostname, value, 1024);
                        } else if (!strncmp (key, GLUSTERD_STORE_KEY_BRICK_PATH,
                                    strlen (GLUSTERD_STORE_KEY_BRICK_PATH))) {
                                strncpy (brickinfo->path, value,
                                         sizeof (brickinfo->path));
                        }else {
                                gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
                                        key);
                        }

                        GF_FREE (key);
                        GF_FREE (value);

                        ret = glusterd_store_iter_get_next (iter, &key, &value);
                }

                ret = glusterd_store_iter_destroy (iter);

                if (ret)
                        goto out;

                list_add_tail (&brickinfo->brick_list, &volinfo->bricks);
                glusterd_for_each_entry (entry, dir);
        }

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

        ret = glusterd_volinfo_new (&volinfo);

        if (ret)
                goto out;

        strncpy (volinfo->volname, volname, GLUSTERD_MAX_VOLUME_NAME);

        priv = THIS->private;

        GLUSTERD_GET_VOLUME_DIR(volpath, volinfo, priv);
        snprintf (path, sizeof (path), "%s/%s", volpath,
                  GLUSTERD_VOLUME_INFO_FILE);

        ret = glusterd_store_handle_new (path, &volinfo->shandle);

        if (ret)
                goto out;

        ret = glusterd_store_iter_new (volinfo->shandle, &iter);

        if (ret)
                goto out;

        ret = glusterd_store_iter_get_next (iter, &key, &value);

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
                } else if (!strncmp (key, GLUSTERD_STORE_KEY_VOL_PORT,
                            strlen (GLUSTERD_STORE_KEY_VOL_PORT))) {
                        volinfo->port = atoi (value);
                } else {
                        gf_log ("", GF_LOG_ERROR, "Unknown key: %s",
                                        key);
                }

                GF_FREE (key);
                GF_FREE (value);

                ret = glusterd_store_iter_get_next (iter, &key, &value);
        }

        ret = glusterd_store_iter_destroy (iter);

        if (ret)
                goto out;

        ret = glusterd_store_retrieve_bricks (volinfo);

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

        ret = glusterd_store_delete_volume (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to delete "
                        "volume: %s", volinfo->volname);
                goto out;
        }

        ret = glusterd_store_create_volume (volinfo);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create "
                        "volume: %s", volinfo->volname);
                goto out;
        }

out:
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
        char                            str[512] = {0,};
        char                            buf[4096] = {0,};
        glusterd_peer_hostname_t        *hname = NULL;
        int                             i = 0;
        char                            hostname_path[PATH_MAX] = {0,};

        GF_ASSERT (peerinfo);

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
                        GF_ASSERT (peerinfo->uuid || peerinfo->hostname);
                }
        } else {
                uuid_unparse (peerinfo->uuid, str);

                snprintf (filepath, PATH_MAX, "%s/%s", peerdir, str);
                snprintf (hostname_path, PATH_MAX, "%s/%s",
                          peerdir, peerinfo->hostname);

                ret = stat (hostname_path, &stbuf);

                if (!ret) {
                        gf_log ("", GF_LOG_DEBUG, "Destroying store handle");
                        glusterd_store_handle_destroy (peerinfo->shandle);
                        peerinfo->shandle = NULL;
                }
        }


        if (!peerinfo->shandle) {
                ret = glusterd_store_handle_new (filepath, &peerinfo->shandle);
                if (ret)
                        goto out;
        } else {
                ret = glusterd_store_handle_truncate (peerinfo->shandle);
                if (ret)
                        goto out;
        }

        ret = glusterd_store_save_value (peerinfo->shandle,
                                         GLUSTERD_STORE_KEY_PEER_UUID, str);
        if (ret)
                goto out;

        snprintf (buf, sizeof (buf), "%d", peerinfo->state.state);
        ret = glusterd_store_save_value (peerinfo->shandle,
                                         GLUSTERD_STORE_KEY_PEER_STATE, buf);
        if (ret)
                goto out;

        list_for_each_entry (hname, &peerinfo->hostnames, hostname_list) {
                i++;
                snprintf (buf, sizeof (buf), "%s%d",
                          GLUSTERD_STORE_KEY_PEER_HOSTNAME, i);
                ret = glusterd_store_save_value (peerinfo->shandle,
                                                 buf, hname->hostname);
                if (ret)
                        goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
glusterd_store_retrieve_peers (xlator_t *this)
{
        int32_t                 ret = -1;
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
                ret = glusterd_store_handle_new (filepath, &shandle);
                if (ret)
                        goto out;

                ret = glusterd_store_iter_new (shandle, &iter);
                if (ret)
                        goto out;

                ret = glusterd_store_iter_get_next (iter, &key, &value);

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

                        ret = glusterd_store_iter_get_next (iter, &key, &value);
                }

                (void) glusterd_store_iter_destroy (iter);

                ret = glusterd_friend_add (hostname, 0, state, &uuid,
                                           NULL, &peerinfo, 1);

                if (ret)
                        goto out;

                peerinfo->shandle = shandle;
                glusterd_for_each_entry (entry, dir);
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

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
