/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "compat.h"
#include "xlator.h"

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
                if (entry->dict)
                        dict_unref (entry->dict);
                if (entry->inode)
                        inode_unref (entry->inode);

                list_del (&entry->list);
                GF_FREE (entry);
        }
}

/* TODO: Currently, with this function, we will be breaking the
   policy of 1-1 mapping of kernel nlookup refs with our inode_t's
   nlookup count.
   Need more thoughts before finalizing this function
*/
int
gf_link_inodes_from_dirent (xlator_t *this, inode_t *parent,
                            gf_dirent_t *entries)
{
        gf_dirent_t *entry      = NULL;
        inode_t     *link_inode = NULL;

        list_for_each_entry (entry, &entries->list, list) {
                if (entry->inode) {
                        link_inode = inode_link (entry->inode, parent,
                                                 entry->d_name, &entry->d_stat);
                        inode_lookup (link_inode);
                        inode_unref (link_inode);
                }
        }

        return 0;
}
