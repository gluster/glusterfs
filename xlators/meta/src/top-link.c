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
top_link_fill (xlator_t *this, inode_t *inode, strfd_t *strfd)
{
	glusterfs_graph_t *graph = NULL;

	graph = meta_ctx_get (inode, this);

	strprintf (strfd, "%s", ((xlator_t *)graph->top)->name);

	return 0;
}


struct meta_ops top_link_ops = {
	.link_fill = top_link_fill
};


int
meta_top_link_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &top_link_ops);

	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	return 0;
}
