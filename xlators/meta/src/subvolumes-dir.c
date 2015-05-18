/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"

#include "meta-mem-types.h"
#include "meta.h"
#include "meta-hooks.h"


static int
subvolumes_dir_fill (xlator_t *this, inode_t *dir, struct meta_dirent **dp)
{
	struct meta_dirent *dirents = NULL;
	xlator_t *xl = NULL;
	xlator_list_t *subv = NULL;
	int i = 0;
	int count = 0;

	xl = meta_ctx_get (dir, this);

	for (subv = xl->children; subv; subv = subv->next)
		count++;

	dirents = GF_CALLOC (sizeof (*dirents), count, gf_meta_mt_dirents_t);
	if (!dirents)
		return -1;

	for (subv = xl->children; subv; subv = subv->next) {
		char num[16] = { };
		snprintf (num, 16, "%d", i);

		dirents[i].name = gf_strdup (num);
		dirents[i].type = IA_IFLNK;
		dirents[i].hook = meta_subvolume_link_hook;
		i++;
	}

	*dp = dirents;

	return count;
}


static struct meta_ops subvolumes_dir_ops = {
	.dir_fill = subvolumes_dir_fill
};


int
meta_subvolumes_dir_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			  dict_t *xdata)
{
	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	meta_ops_set (loc->inode, this, &subvolumes_dir_ops);

	return 0;
}
