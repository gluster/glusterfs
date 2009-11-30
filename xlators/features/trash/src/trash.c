/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#include "trash.h"

int32_t
trash_unlink_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct stat *buf,
                         struct stat *preoldparent, struct stat *postoldparent,
                         struct stat *prenewparent, struct stat *postnewparent);

void
trash_local_wipe (trash_local_t *local)
{
        if (!local)
                goto out;

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->newfd)
                fd_unref (local->newfd);

        FREE (local);
out:
        return;
}

int32_t
trash_common_unwind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         struct stat *preparent, struct stat *postparent)
{
        TRASH_STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
        return 0;
}

int32_t
trash_unlink_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct stat *stbuf, struct stat *preparent,
                        struct stat *postparent)
{
        trash_local_t *local       = NULL;
        char          *tmp_str     = NULL;
        char          *tmp_path    = NULL;
        char          *tmp_dirname = NULL;
        char          *dir_name    = NULL;
        int32_t        count       = 0;
        int32_t        loop_count  = 0;
        int            i           = 0;
        loc_t          tmp_loc     = {0,};

        local   = frame->local;
        tmp_str = strdup (local->newpath);
        if (!tmp_str) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
        }
        loop_count = local->loop_count;

        if ((op_ret == -1) &&  (op_errno == ENOENT)) {
                tmp_dirname = strchr (tmp_str, '/');
                while (tmp_dirname) {
                        count = tmp_dirname - tmp_str;
                        if (count == 0)
                                count = 1;
                        i++;
                        if (i > loop_count)
                                break;
                        tmp_dirname = strchr (tmp_str + count + 1, '/');
                }
                tmp_path = strndup (local->newpath, count);
                if (!tmp_path) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }

                tmp_loc.path = tmp_path;

                /* TODO:create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_path,
                                   this->children->xlator,
                                   this->children->xlator->fops->mkdir,
                                   &tmp_loc, 0755);

                goto out;
        }

        if (op_ret == 0) {
                dir_name = dirname (tmp_str);
                if (strcmp((char*)cookie, dir_name) == 0) {
                        tmp_loc.path = local->newpath;
                        STACK_WIND (frame, trash_unlink_rename_cbk,
                                    this->children->xlator,
                                    this->children->xlator->fops->rename,
                                    &local->loc, &tmp_loc);
                        goto out;
                }
        }

        LOCK (&frame->lock);
        {
                loop_count = ++local->loop_count;
        }
        UNLOCK (&frame->lock);
        tmp_dirname = strchr (tmp_str, '/');
        while (tmp_dirname) {
                count = tmp_dirname - tmp_str;
                if (count == 0)
                        count = 1;
                i++;
                if ((i > loop_count) || (count > PATH_MAX))
                        break;
                tmp_dirname = strchr (tmp_str + count + 1, '/');
        }
        tmp_path = strndup (local->newpath, count);
        if (!tmp_path) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
        }
        tmp_loc.path = tmp_path;

        STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_path,
                           this->children->xlator,
                           this->children->xlator->fops->mkdir,
                           &tmp_loc, 0755);

out:
        free (cookie);
        free (tmp_str);

        return 0;
}


int32_t
trash_unlink_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct stat *buf,
                         struct stat *preoldparent, struct stat *postoldparent,
                         struct stat *prenewparent, struct stat *postnewparent)
{
        trash_local_t   *local      = NULL;
        trash_private_t *priv       = NULL;
        char            *tmp_str    = NULL;
        char            *dir_name   = NULL;
        char            *tmp_cookie = NULL;
        loc_t            tmp_loc    = {0,};

        priv  = this->private;
        local = frame->local;

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                tmp_str = strdup (local->newpath);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                dir_name = dirname (tmp_str);

                tmp_loc.path = dir_name;

                tmp_cookie = strdup (dir_name);
                if (!tmp_cookie) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }
                /* TODO: create the directory with proper permissions */
                STACK_WIND_COOKIE (frame, trash_unlink_mkdir_cbk, tmp_cookie,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mkdir,
                                   &tmp_loc, 0755);

                free (tmp_str);

                return 0;
        }

        if ((op_ret == -1) && (op_errno == ENOTDIR)) {

                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists, cannot keep the copy, deleting",
                        local->newpath);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->unlink, &local->loc);

                return 0;
        }

        if ((op_ret == -1) && (op_errno == EISDIR)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "target(%s) exists as directory, cannot keep copy, "
                        "deleting", local->newpath);

                STACK_WIND (frame, trash_common_unwind_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, &local->loc);
                return 0;
        }

        /* All other cases, unlink should return success */
        TRASH_STACK_UNWIND (frame, 0, op_errno, &local->preparent,
                            &local->postparent);

        return 0;
}


int32_t
trash_unlink_stat_cbk (call_frame_t *frame,  void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        trash_private_t *priv    = NULL;
        trash_local_t   *local   = NULL;
        loc_t            new_loc = {0,};

        priv  = this->private;
        local = frame->local;

        if (-1 == op_ret) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: %s",
                        local->loc.path, strerror (op_errno));
                goto fail;
        }

        if ((buf->st_size == 0) ||
            (buf->st_size > priv->max_trash_file_size)) {
                /* if the file is too big or zero, just unlink it */

                if (buf->st_size > priv->max_trash_file_size) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file size too big (%"GF_PRI_SIZET") to "
                                "move into trash directory",
                                local->loc.path, buf->st_size);
                }

                STACK_WIND (frame, trash_common_unwind_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->unlink, &local->loc);
                return 0;
        }

        new_loc.path = local->newpath;

        STACK_WIND (frame, trash_unlink_rename_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->rename,
                    &local->loc, &new_loc);

        return 0;

fail:
        TRASH_STACK_UNWIND (frame, op_ret, op_errno, buf,
                            NULL, NULL, NULL, NULL);

        return 0;

}


int32_t
trash_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        trash_elim_pattern_t  *trav  = NULL;
        trash_private_t *priv = NULL;
        trash_local_t   *local = NULL;
        struct tm       *tm = NULL;
        char             timestr[256] = {0,};
        time_t           utime = 0;
        int32_t          match = 0;

        priv = this->private;

        if (priv->eliminate) {
                trav = priv->eliminate;
                while (trav) {
                        if (fnmatch(trav->pattern, loc->name, 0) == 0) {
                                match++;
                                break;
                        }
                        trav = trav->next;
                }
        }

        if ((strncmp (loc->path, priv->trash_dir,
                      strlen (priv->trash_dir)) == 0) || (match)) {
                if (match) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: file matches eliminate pattern, "
                                "not moved to trash", loc->name);
                } else {
                        /* unlink from the trash-dir, not keeping any copy */
                        ;
                }

                STACK_WIND (frame, trash_common_unwind_cbk,
                            this->children->xlator,
                            this->children->xlator->fops->unlink, loc);
                return 0;
        }

        local = CALLOC (1, sizeof (trash_local_t));
        if (!local) {
                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                TRASH_STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
                return 0;
        }
        frame->local = local;
        loc_copy (&local->loc, loc);

        strcpy (local->origpath, loc->path);
        strcpy (local->newpath, priv->trash_dir);
        strcat (local->newpath, loc->path);

        {
                /* append timestamp to file name */
                /* TODO: can we make it optional? */
                utime = time (NULL);
                tm    = localtime (&utime);
                strftime (timestr, 256, ".%Y-%m-%d-%H%M%S", tm);
                strcat (local->newpath, timestr);
        }

        LOCK_INIT (&frame->lock);

        STACK_WIND (frame, trash_unlink_stat_cbk,
                    this->children->xlator,
                    this->children->xlator->fops->stat, loc);

        return 0;
}

/**
 * trash_init -
 */
int32_t
init (xlator_t *this)
{
        int32_t                ret   = 0;
        data_t                *data  = NULL;
        trash_private_t       *_priv = NULL;
        trash_elim_pattern_t  *trav  = NULL;
        char                  *tmp_str = NULL;
        char                  *strtokptr = NULL;
        char                  *component = NULL;
        char                   trash_dir[PATH_MAX] = {0,};

        /* Create .trashcan directory in init */
        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "not configured with exactly one child. exiting");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        _priv = CALLOC (1, sizeof (*_priv));
        if (!_priv) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                return -1;
        }

        data = dict_get (this->options, "trash-dir");
        if (!data) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "no option specified for 'trash-dir', "
                        "using \"/.trashcan/\"");
                _priv->trash_dir = strdup ("/.trashcan");
        } else {
                /* Need a path with '/' as the first char, if not
                   given, append it */
                if (data->data[0] == '/') {
                        _priv->trash_dir = strdup (data->data);
                } else {
                        /* TODO: Make sure there is no ".." in the path */
                        strcpy (trash_dir, "/");
                        strcat (trash_dir, data->data);
                        _priv->trash_dir = strdup (trash_dir);
                }
        }

        data = dict_get (this->options, "eliminate-pattern");
        if (!data) {
                gf_log (this->name, GF_LOG_TRACE,
                        "no option specified for 'eliminate', using NULL");
        } else {
                tmp_str = strdup (data->data);
                if (!tmp_str) {
                        gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                }

                /*  Match Filename to option specified in eliminate. */
                component = strtok_r (tmp_str, "|", &strtokptr);
                while (component) {
                        trav = CALLOC (1, sizeof (*trav));
                        if (!trav) {
                                gf_log (this->name, GF_LOG_DEBUG, "out of memory");
                        }
                        trav->pattern = component;
                        trav->next = _priv->eliminate;
                        _priv->eliminate = trav;

                        component = strtok_r (NULL, "|", &strtokptr);
                }
        }

        /* TODO: do gf_string2sizet () */
        data = dict_get (this->options, "max-trashable-file-size");
        if (!data) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no option specified for 'max-trashable-file-size', "
                        "using default = %lld MB",
                        GF_DEFAULT_MAX_FILE_SIZE / GF_UNIT_MB);
                _priv->max_trash_file_size = GF_DEFAULT_MAX_FILE_SIZE;
        } else {
                ret = gf_string2bytesize (data->data,
                                          &_priv->max_trash_file_size);
                if( _priv->max_trash_file_size > GF_ALLOWED_MAX_FILE_SIZE ) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Size specified for max-size(in MB) is too "
                                "large so using 1GB as max-size (NOT IDEAL)");
                        _priv->max_trash_file_size = GF_ALLOWED_MAX_FILE_SIZE;
                }
                gf_log (this->name, GF_LOG_DEBUG, "%"GF_PRI_SIZET" max-size",
                        _priv->max_trash_file_size);
        }

        this->private = (void *)_priv;
        return 0;
}

void
fini (xlator_t *this)
{
        trash_private_t *priv = NULL;

        priv = this->private;
        if (priv)
                FREE (priv);

        return;
}

struct xlator_fops fops = {
        .unlink = trash_unlink,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = { "trash-directory" },
          .type = GF_OPTION_TYPE_PATH,
        },
        { .key  = { "eliminate-pattern" },
          .type = GF_OPTION_TYPE_STR,
        },
        { .key  = { "max-trashable-file-size" },
          .type = GF_OPTION_TYPE_SIZET,
        },
        { .key  = {NULL} },
};
