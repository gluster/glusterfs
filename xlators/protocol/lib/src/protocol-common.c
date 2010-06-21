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


#include "globals.h"
#include "compat.h"
#include "protocol-common.h"
#include "glusterfs-xdr.h"


static int
gf_dirent_nb_size (gf_dirent_t *entries)
{
	return (sizeof (struct gf_dirent_nb) + strlen (entries->d_name) + 1);
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

			entry_nb->d_ino  = entry->d_ino;
			entry_nb->d_off  = entry->d_off;
			entry_nb->d_len  = entry->d_len;
			entry_nb->d_type = entry->d_type;

                        gf_stat_from_iatt (&entry_nb->d_stat, &entry->d_stat);

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
		entry = GF_CALLOC (1, entry_len, gf_common_mt_gf_dirent_t);
		if (!entry) {
			break;
		}

		entry->d_ino  = entry_nb->d_ino;
		entry->d_off  = entry_nb->d_off;
		entry->d_len  = entry_nb->d_len;
		entry->d_type = entry_nb->d_type;

                gf_stat_to_iatt (&entry_nb->d_stat, &entry->d_stat);

		strcpy (entry->d_name, entry_nb->d_name);

		list_add_tail (&entry->list, &entries->list);

		remaining_size -= (sizeof (*entry_nb) + entry_strlen + 1);
		count++;
	}

	return count;
}
