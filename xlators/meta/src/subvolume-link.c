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


static int
subvolume_link_fill (xlator_t *this, inode_t *inode, strfd_t *strfd)
{
	xlator_t *xl = NULL;

	xl = meta_ctx_get (inode, this);

	strprintf (strfd, "../../%s", xl->name);

	return 0;
}


struct meta_ops subvolume_link_ops = {
	.link_fill = subvolume_link_fill
};


int
meta_subvolume_link_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			  dict_t *xdata)
{
	int count = 0;
	int i = 0;
	xlator_t *xl = NULL;
	xlator_list_t *subv = NULL;
	xlator_t *subvol = NULL;

	count = strtol (loc->name, 0, 0);
	xl = meta_ctx_get (loc->parent, this);

	for (subv = xl->children; subv; subv = subv->next) {
		if (i == count) {
			subvol = subv->xlator;
			break;
		}
		i++;
	}

	meta_ctx_set (loc->inode, this, subvol);

	meta_ops_set (loc->inode, this, &subvolume_link_ops);
	return 0;
}
