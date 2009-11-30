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
