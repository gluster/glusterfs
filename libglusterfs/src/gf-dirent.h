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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "iatt.h"
#include "inode.h"

#define gf_dirent_size(name) (sizeof (gf_dirent_t) + strlen (name) + 1)

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
void gf_dirent_free (gf_dirent_t *entries);
int gf_link_inodes_from_dirent (xlator_t *this, inode_t *parent,
                                gf_dirent_t *entries);

#endif /* _GF_DIRENT_H */
