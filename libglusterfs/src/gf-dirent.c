/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "compat.h"
#include "xlator.h"
#include "byte-order.h"
#include "protocol.h"


struct gf_dirent_nb {
	uint64_t       d_ino;
	uint64_t       d_off;
	uint32_t       d_len;
	uint32_t       d_type;
        struct gf_stat d_stat;
	char           d_name[0];
} __attribute__((packed));


int
gf_dirent_nb_size (gf_dirent_t *entries)
{
	return (sizeof (struct gf_dirent_nb) + strlen (entries->d_name) + 1);
}


gf_dirent_t *
gf_dirent_for_name (const char *name)
{
	gf_dirent_t *gf_dirent = NULL;

	/* TODO: use mem-pool */
	gf_dirent = CALLOC (gf_dirent_size (name), 1);
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
		FREE (entry);
	}
}


int
gf_dirent_serialize (gf_dirent_t *entries, char *buf, size_t buf_size)
{
	struct gf_dirent_nb *entry_nb = NULL;
	gf_dirent_t         *entry = NULL;
	int                  size = 0;
	int                  entry_size = 0;


	list_for_each_entry (entry, &entries->list, list) {
		entry_size = gf_dirent_nb_size (entry);

		if (buf && (size + entry_size <= buf_size)) {
			entry_nb = (void *) (buf + size);

			entry_nb->d_ino  = hton64 (entry->d_ino);
			entry_nb->d_off  = hton64 (entry->d_off);	
			entry_nb->d_len  = hton32 (entry->d_len);
			entry_nb->d_type = hton32 (entry->d_type);

                        gf_stat_from_stat (&entry_nb->d_stat, &entry->d_stat);

			strcpy (entry_nb->d_name, entry->d_name);
		}
		size += entry_size;
	}

	return size;
}


int
gf_dirent_unserialize (gf_dirent_t *entries, const char *buf, size_t buf_size)
{
	struct gf_dirent_nb *entry_nb = NULL;
	int                  remaining_size = 0;
	int                  least_dirent_size = 0;
	int                  count = 0;
	gf_dirent_t         *entry = NULL;
	int                  entry_strlen = 0;
	int                  entry_len = 0;


	remaining_size = buf_size;
	least_dirent_size = (sizeof (struct gf_dirent_nb) + 2);

	while (remaining_size >= least_dirent_size) {
		entry_nb = (void *)(buf + (buf_size - remaining_size));

		entry_strlen = strnlen (entry_nb->d_name, remaining_size);
		if (entry_strlen == remaining_size) {
			break;
		}

		entry_len = sizeof (gf_dirent_t) + entry_strlen + 1;
		entry = CALLOC (1, entry_len);
		if (!entry) {
			break;
		}

		entry->d_ino  = ntoh64 (entry_nb->d_ino);
		entry->d_off  = ntoh64 (entry_nb->d_off);
		entry->d_len  = ntoh32 (entry_nb->d_len);
		entry->d_type = ntoh32 (entry_nb->d_type);

                gf_stat_to_stat (&entry_nb->d_stat, &entry->d_stat);

		strcpy (entry->d_name, entry_nb->d_name);

		list_add_tail (&entry->list, &entries->list);

		remaining_size -= (sizeof (*entry_nb) + entry_strlen + 1);
		count++;
	}

	return count;
}
