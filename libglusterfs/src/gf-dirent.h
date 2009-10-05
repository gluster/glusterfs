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


#ifndef _GF_DIRENT_H
#define _GF_DIRENT_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define gf_dirent_size(name) (sizeof (gf_dirent_t) + strlen (name) + 1)

struct _dir_entry_t {
        struct _dir_entry_t *next;
	char                *name;
	char                *link;
	struct stat          buf;
};


struct _gf_dirent_t {
	union {
		struct list_head             list;
		struct {
			struct _gf_dirent_t *next;
			struct _gf_dirent_t *prev;
		};
	};
	uint64_t                             d_ino;
	uint64_t                             d_off;
	uint32_t                             d_len;
	uint32_t                             d_type;
        struct stat                          d_stat;
	char                                 d_name[0];
};


gf_dirent_t *gf_dirent_for_name (const char *name);
void gf_dirent_free (gf_dirent_t *entries);
int gf_dirent_serialize (gf_dirent_t *entries, char *buf, size_t size);
int gf_dirent_unserialize (gf_dirent_t *entries, const char *buf, size_t size);

#endif /* _GF_DIRENT_H */
