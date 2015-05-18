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
#include "strfd.h"



static int
xldump_options (dict_t *this, char *key, data_t *value,	void *strfd)
{
	strprintf (strfd, "    option %s %s\n", key, value->data);
	return 0;
}


static void
xldump_subvolumes (xlator_t *this, void *strfd)
{
	xlator_list_t *subv = NULL;

	if (!this->children)
		return;

	strprintf (strfd, "    subvolumes");

	for (subv = this->children; subv; subv= subv->next)
		strprintf (strfd, " %s", subv->xlator->name);

	strprintf (strfd, "\n");
}


static void
xldump (xlator_t *each, void *strfd)
{
	strprintf (strfd, "volume %s\n", each->name);
	strprintf (strfd, "    type %s\n", each->type);
	dict_foreach (each->options, xldump_options, strfd);

	xldump_subvolumes (each, strfd);

	strprintf (strfd, "end-volume\n");
	strprintf (strfd, "\n");
}


static int
volfile_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	glusterfs_graph_t *graph = NULL;

	graph = meta_ctx_get (file, this);

	xlator_foreach_depth_first (graph->top, xldump, strfd);

	return strfd->size;
}


static struct meta_ops volfile_file_ops = {
	.file_fill = volfile_file_fill,
};


int
meta_volfile_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &volfile_file_ops);

	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	return 0;
}
