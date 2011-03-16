/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "compat.h"
#include "xlator.h"

gf_dirent_t *
gf_dirent_for_namelen (int len)
{
        gf_dirent_t *gf_dirent = NULL;

        /* TODO: use mem-pool */
        gf_dirent = CALLOC (len, sizeof(char));
        if (!gf_dirent)
                return NULL;

        INIT_LIST_HEAD (&gf_dirent->list);

        gf_dirent->d_off = 0;
        gf_dirent->d_ino = -1;
        gf_dirent->d_type = 0;

        return gf_dirent;
}


gf_dirent_t *
gf_dirent_for_name (const char *name)
{
        gf_dirent_t *gf_dirent = NULL;

        /* TODO: use mem-pool */
        gf_dirent = GF_CALLOC (gf_dirent_size (name), 1,
                               gf_common_mt_gf_dirent_t);
        if (!gf_dirent)
                return NULL;

        INIT_LIST_HEAD (&gf_dirent->list);
        strcpy (gf_dirent->d_name, name);

        gf_dirent->d_off = 0;
        gf_dirent->d_ino = -1;
        gf_dirent->d_type = 0;
        gf_dirent->d_len = strlen (name);

        return gf_dirent;
}


void
gf_dirent_free (gf_dirent_t *entries)
{
        gf_dirent_t *entry = NULL;
        gf_dirent_t *tmp = NULL;

        if (!entries)
                return;

        if (list_empty (&entries->list))
                return;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                list_del (&entry->list);
                GF_FREE (entry);
        }
}
