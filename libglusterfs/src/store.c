/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
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
#include <libgen.h>

#include "glusterfs.h"
#include "store.h"
#include "dict.h"
#include "xlator.h"

int32_t
gf_store_mkdir (char *path)
{
        int32_t     ret = -1;

        ret = mkdir (path, 0777);

        if ((-1 == ret) && (EEXIST != errno)) {
                gf_log ("", GF_LOG_ERROR, "mkdir() failed on path %s,"
                        "errno: %s", path, strerror (errno));
        } else {
                ret = 0;
        }

        return ret;
}

int32_t
gf_store_handle_create_on_absence (gf_store_handle_t **shandle,
                                   char *path)
{
        GF_ASSERT (shandle);
        int32_t     ret = 0;

        if (*shandle == NULL) {
                ret = gf_store_handle_new (path, shandle);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to create store"
                                " handle for path: %s", path);
                }
        }
        return ret;
}

int32_t
gf_store_mkstemp (gf_store_handle_t *shandle)
{
        int     fd = -1;
        char    tmppath[PATH_MAX] = {0,};

        GF_VALIDATE_OR_GOTO ("store", shandle, out);
        GF_VALIDATE_OR_GOTO ("store", shandle->path, out);

        snprintf (tmppath, sizeof (tmppath), "%s.tmp", shandle->path);
        fd = open (tmppath, O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0600);
        if (fd <= 0) {
                gf_log ("", GF_LOG_ERROR, "Failed to open %s, error: %s",
                        tmppath, strerror (errno));
        }
out:
        return fd;
}

int
gf_store_sync_direntry (char *path)
{
        int             ret     = -1;
        int             dirfd   = -1;
        char            *dir    = NULL;
        char            *pdir   = NULL;
        xlator_t        *this = NULL;

        this = THIS;

        dir = gf_strdup (path);
        if (!dir)
                goto out;

        pdir = dirname (dir);
        dirfd = open (pdir, O_RDONLY);
        if (dirfd == -1) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to open directory "
                        "%s, due to %s", pdir, strerror (errno));
                goto out;
        }

        ret = fsync (dirfd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to fsync %s, due to "
                        "%s", pdir, strerror (errno));
                goto out;
        }

        ret = 0;
out:
        if (dirfd >= 0) {
                ret = close (dirfd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to close "
                                "%s, due to %s", pdir, strerror (errno));
                }
        }

        if (dir)
                GF_FREE (dir);

        return ret;
}

int32_t
gf_store_rename_tmppath (gf_store_handle_t *shandle)
{
        int32_t         ret = -1;
        char            tmppath[PATH_MAX] = {0,};

        GF_VALIDATE_OR_GOTO ("store", shandle, out);
        GF_VALIDATE_OR_GOTO ("store", shandle->path, out);

        snprintf (tmppath, sizeof (tmppath), "%s.tmp", shandle->path);
        ret = rename (tmppath, shandle->path);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to rename %s to %s, "
                        "error: %s", tmppath, shandle->path, strerror (errno));
                goto out;
        }

        ret = gf_store_sync_direntry (tmppath);
out:
        return ret;
}

int32_t
gf_store_unlink_tmppath (gf_store_handle_t *shandle)
{
        int32_t         ret = -1;
        char            tmppath[PATH_MAX] = {0,};

        GF_VALIDATE_OR_GOTO ("store", shandle, out);
        GF_VALIDATE_OR_GOTO ("store", shandle->path, out);

        snprintf (tmppath, sizeof (tmppath), "%s.tmp", shandle->path);
        ret = unlink (tmppath);
        if (ret && (errno != ENOENT)) {
                gf_log ("", GF_LOG_ERROR, "Failed to mv %s to %s, error: %s",
                        tmppath, shandle->path, strerror (errno));
        } else {
                ret = 0;
        }
out:
        return ret;
}

int
gf_store_read_and_tokenize (FILE *file, char *str, char **iter_key,
                            char **iter_val, gf_store_op_errno_t *store_errno)
{
        int32_t     ret = -1;
        char        *savetok = NULL;
        char        *key = NULL;
        char        *value = NULL;

        GF_ASSERT (file);
        GF_ASSERT (str);
        GF_ASSERT (iter_key);
        GF_ASSERT (iter_val);
        GF_ASSERT (store_errno);

        ret = fscanf (file, "%s", str);
        if (ret <= 0 || feof (file)) {
                ret = -1;
                *store_errno = GD_STORE_EOF;
                goto out;
        }

        key = strtok_r (str, "=", &savetok);
        if (!key) {
                ret = -1;
                *store_errno = GD_STORE_KEY_NULL;
                goto out;
        }

        value = strtok_r (NULL, "=", &savetok);
        if (!value) {
                ret = -1;
                *store_errno = GD_STORE_VALUE_NULL;
                goto out;
        }

        *iter_key = key;
        *iter_val = value;
        *store_errno = GD_STORE_SUCCESS;
        ret = 0;
out:
        return ret;
}

int32_t
gf_store_retrieve_value (gf_store_handle_t *handle, char *key, char **value)
{
        int32_t         ret = -1;
        char            *scan_str = NULL;
        char            *iter_key = NULL;
        char            *iter_val = NULL;
        char            *free_str = NULL;
        struct stat     st        = {0,};
        gf_store_op_errno_t store_errno = GD_STORE_SUCCESS;

        GF_ASSERT (handle);

        if (handle->locked == F_ULOCK)
                /* no locking is used handle->fd gets closed() after usage */
                handle->fd = open (handle->path, O_RDWR);
        else
                /* handle->fd is valid already, kept open for lockf() */
                lseek (handle->fd, 0, SEEK_SET);

        if (handle->fd == -1) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %s",
                        handle->path, strerror (errno));
                goto out;
        }
        if (!handle->read)
                handle->read = fdopen (dup(handle->fd), "r");
        else
                fseek (handle->read, 0, SEEK_SET);

        if (!handle->read) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %s",
                        handle->path, strerror (errno));
                goto out;
        }

        ret = fstat (handle->fd, &st);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "stat on file %s failed",
                        handle->path);
                ret = -1;
                store_errno = GD_STORE_STAT_FAILED;
                goto out;
        }

        scan_str = GF_CALLOC (1, st.st_size,
                              gf_common_mt_char);
        if (scan_str == NULL) {
                ret = -1;
                store_errno = GD_STORE_ENOMEM;
                goto out;
        }

        free_str = scan_str;

        do {
                ret = gf_store_read_and_tokenize (handle->read, scan_str,
                                                  &iter_key, &iter_val,
                                                  &store_errno);
                if (ret < 0) {
                        gf_log ("", GF_LOG_TRACE, "error while reading key "
                                "'%s': %s", key,
                                gf_store_strerror (store_errno));
                        goto out;
                }

                gf_log ("", GF_LOG_TRACE, "key %s read", iter_key);

                if (!strcmp (key, iter_key)) {
                        gf_log ("", GF_LOG_DEBUG, "key %s found", key);
                        ret = 0;
                        if (iter_val)
                                *value = gf_strdup (iter_val);
                        goto out;
                }
        } while (1);
out:
        if (handle->read) {
                fclose (handle->read);
                handle->read = NULL;
        }

        if (handle->fd > 0 && handle->locked == F_ULOCK) {
                /* only invalidate handle->fd if not locked */
                close (handle->fd);
        }

        GF_FREE (free_str);

        return ret;
}

int32_t
gf_store_save_value (int fd, char *key, char *value)
{
        int32_t         ret = -1;
        int             dup_fd = -1;
        FILE           *fp  = NULL;

        GF_ASSERT (fd > 0);
        GF_ASSERT (key);
        GF_ASSERT (value);

        dup_fd = dup (fd);
        if (dup_fd == -1)
                goto out;

        fp = fdopen (dup_fd, "a+");
        if (fp == NULL) {
                gf_log ("", GF_LOG_WARNING, "fdopen failed.");
                ret = -1;
                goto out;
        }

        ret = fprintf (fp, "%s=%s\n", key, value);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "Unable to store key: %s,"
                        "value: %s, error: %s", key, value,
                        strerror (errno));
                ret = -1;
                goto out;
        }

        ret = fflush (fp);
        if (feof (fp)) {
                gf_log ("", GF_LOG_WARNING,
                        "fflush failed, error: %s",
                        strerror (errno));
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        if (fp)
                fclose (fp);

        gf_log ("", GF_LOG_DEBUG, "returning: %d", ret);
        return ret;
}

int32_t
gf_store_handle_new (char *path, gf_store_handle_t **handle)
{
        int32_t                 ret = -1;
        gf_store_handle_t *shandle = NULL;
        int                     fd = -1;
        char                    *spath = NULL;

        shandle = GF_CALLOC (1, sizeof (*shandle), gf_common_mt_store_handle_t);
        if (!shandle)
                goto out;

        spath = gf_strdup (path);

        if (!spath)
                goto out;

        fd = open (path, O_RDWR | O_CREAT | O_APPEND, 0600);
        if (fd <= 0) {
                gf_log ("", GF_LOG_ERROR, "Failed to open file: %s, error: %s",
                        path, strerror (errno));
                goto out;
        }

        ret = gf_store_sync_direntry (spath);
        if (ret)
                goto out;

        shandle->path = spath;
        shandle->locked = F_ULOCK;
        *handle = shandle;

        ret = 0;
out:
        if (fd > 0)
                close (fd);

        if (ret == -1) {
                GF_FREE (spath);
                GF_FREE (shandle);
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
gf_store_handle_retrieve (char *path, gf_store_handle_t **handle)
{
        int32_t                 ret = -1;
        struct stat statbuf = {0};

        ret = stat (path, &statbuf);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Path corresponding to "
                        "%s, returned error: (%s)",
                        path, strerror (errno));
                goto out;
        }
        ret =  gf_store_handle_new (path, handle);
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_store_handle_destroy (gf_store_handle_t *handle)
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
gf_store_iter_new (gf_store_handle_t  *shandle, gf_store_iter_t  **iter)
{
        int32_t                 ret = -1;
        FILE                    *fp = NULL;
        gf_store_iter_t         *tmp_iter = NULL;

        GF_ASSERT (shandle);
        GF_ASSERT (iter);

        fp = fopen (shandle->path, "r");
        if (!fp) {
                gf_log ("", GF_LOG_ERROR, "Unable to open file %s errno: %d",
                        shandle->path, errno);
                goto out;
        }

        tmp_iter = GF_CALLOC (1, sizeof (*tmp_iter),
                              gf_common_mt_store_iter_t);
        if (!tmp_iter)
                goto out;

        strncpy (tmp_iter->filepath, shandle->path, sizeof (tmp_iter->filepath));
        tmp_iter->filepath[sizeof (tmp_iter->filepath) - 1] = 0;
        tmp_iter->file = fp;

        *iter = tmp_iter;
        tmp_iter = NULL;
        ret = 0;

out:
        if (ret && fp)
                fclose (fp);

        GF_FREE (tmp_iter);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
gf_store_validate_key_value (char *storepath, char *key, char *val,
                             gf_store_op_errno_t *op_errno)
{
        int ret = 0;

        GF_ASSERT (op_errno);
        GF_ASSERT (storepath);

        if ((key == NULL) && (val == NULL)) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "Glusterd store may be corrupted, "
                        "Invalid key and value (null) in %s", storepath);
                *op_errno = GD_STORE_KEY_VALUE_NULL;
        } else if (key == NULL) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "Glusterd store may be corrupted, "
                        "Invalid key (null) in %s", storepath);
                *op_errno = GD_STORE_KEY_NULL;
        } else if (val == NULL) {
                ret = -1;
                gf_log ("", GF_LOG_ERROR, "Glusterd store may be corrupted, "
                        "Invalid value (null) for key %s in %s", key,
                        storepath);
                *op_errno = GD_STORE_VALUE_NULL;
        } else {
                ret = 0;
                *op_errno = GD_STORE_SUCCESS;
        }

        return ret;
}

int32_t
gf_store_iter_get_next (gf_store_iter_t *iter, char  **key, char **value,
                        gf_store_op_errno_t *op_errno)
{
        int32_t         ret       = -1;
        char            *scan_str = NULL;
        char            *iter_key = NULL;
        char            *iter_val = NULL;
        struct stat     st        = {0,};
        gf_store_op_errno_t store_errno = GD_STORE_SUCCESS;

        GF_ASSERT (iter);
        GF_ASSERT (key);
        GF_ASSERT (value);

        ret = stat (iter->filepath, &st);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "stat on file failed");
                ret = -1;
                store_errno = GD_STORE_STAT_FAILED;
                goto out;
        }

        scan_str = GF_CALLOC (1, st.st_size,
                              gf_common_mt_char);
        if (!scan_str) {
                ret = -1;
                store_errno = GD_STORE_ENOMEM;
                goto out;
        }

        ret = gf_store_read_and_tokenize (iter->file, scan_str,
                                          &iter_key, &iter_val,
                                          &store_errno);
        if (ret < 0) {
                goto out;
        }

        ret = gf_store_validate_key_value (iter->filepath, iter_key,
                                           iter_val, &store_errno);
        if (ret)
                goto out;

        *key = gf_strdup (iter_key);
        if (!*key) {
                ret = -1;
                store_errno = GD_STORE_ENOMEM;
                goto out;
        }
        *value = gf_strdup (iter_val);
        if (!*value) {
                ret = -1;
                store_errno = GD_STORE_ENOMEM;
                goto out;
        }
        ret = 0;

out:
        GF_FREE (scan_str);
        if (ret) {
                GF_FREE (*key);
                GF_FREE (*value);
                *key = NULL;
                *value = NULL;
        }
        if (op_errno)
                *op_errno = store_errno;

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int32_t
gf_store_iter_get_matching (gf_store_iter_t *iter, char *key, char **value)
{
        int32_t ret = -1;
        char    *tmp_key = NULL;
        char    *tmp_value = NULL;

        ret = gf_store_iter_get_next (iter, &tmp_key, &tmp_value, NULL);
        while (!ret) {
                if (!strncmp (key, tmp_key, strlen (key))){
                        *value = tmp_value;
                        GF_FREE (tmp_key);
                        goto out;
                }
                GF_FREE (tmp_key);
                GF_FREE (tmp_value);
                ret = gf_store_iter_get_next (iter, &tmp_key, &tmp_value,
                                              NULL);
        }
out:
        return ret;
}

int32_t
gf_store_iter_destroy (gf_store_iter_t *iter)
{
        int32_t         ret = -1;

        if (!iter)
                return 0;

        /* gf_store_iter_new will not return a valid iter object with iter->file
         * being NULL*/
        ret = fclose (iter->file);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "Unable to close file: %s, ret: %d, "
                        "errno: %d" ,iter->filepath, ret, errno);

        GF_FREE (iter);
        return ret;
}

char*
gf_store_strerror (gf_store_op_errno_t op_errno)
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

int
gf_store_lock (gf_store_handle_t *sh)
{
        int                     ret;

        GF_ASSERT (sh);
        GF_ASSERT (sh->path);
        GF_ASSERT (sh->locked == F_ULOCK);

        sh->fd = open (sh->path, O_RDWR);
        if (sh->fd == -1) {
                gf_log ("", GF_LOG_ERROR, "Failed to open '%s': %s", sh->path,
                        strerror (errno));
                return -1;
        }

        ret = lockf (sh->fd, F_LOCK, 0);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "Failed to gain lock on '%s': %s",
                        sh->path, strerror (errno));
        else
                /* sh->locked is protected by the lockf(sh->fd) above */
                sh->locked = F_LOCK;

        return ret;
}

void
gf_store_unlock (gf_store_handle_t *sh)
{
        GF_ASSERT (sh);
        GF_ASSERT (sh->locked == F_LOCK);

        sh->locked = F_ULOCK;
        lockf (sh->fd, F_ULOCK, 0);
        close (sh->fd);
}

int
gf_store_locked_local (gf_store_handle_t *sh)
{
        GF_ASSERT (sh);

        return (sh->locked == F_LOCK);
}
