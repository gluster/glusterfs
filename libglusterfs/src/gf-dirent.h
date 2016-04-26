/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _GF_DIRENT_H
#define _GF_DIRENT_H

#include "iatt.h"
#include "inode.h"

#define gf_dirent_size(name) (sizeof (gf_dirent_t) + strlen (name) + 1)

int
gf_deitransform(xlator_t *this, uint64_t y);

int
gf_itransform (xlator_t *this, uint64_t x, uint64_t *y_p, int client_id);

uint64_t
gf_dirent_orig_offset (xlator_t *this, uint64_t offset);


struct _dir_entry_t {
        struct _dir_entry_t *next;
	char                *name;
	char                *link;
	struct iatt          buf;
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
        struct iatt                          d_stat;
        dict_t                              *dict;
        inode_t                             *inode;
	char                                 d_name[];
};

#define DT_ISDIR(mode) (mode == DT_DIR)

gf_dirent_t *gf_dirent_for_name (const char *name);
gf_dirent_t *entry_copy (gf_dirent_t *source);
void gf_dirent_entry_free (gf_dirent_t *entry);
void gf_dirent_free (gf_dirent_t *entries);
int gf_link_inodes_from_dirent (xlator_t *this, inode_t *parent,
                                gf_dirent_t *entries);
int
gf_fill_iatt_for_dirent (gf_dirent_t *entry, inode_t *parent,
                         xlator_t *subvol);

void
gf_link_inode_from_dirent (xlator_t *this, inode_t *parent, gf_dirent_t *entry);
#endif /* _GF_DIRENT_H */
