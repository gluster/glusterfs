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


static struct meta_dirent graphs_dir_dirents[] = {
	DOT_DOTDOT,

	{ .name = "active",
	  .type = IA_IFLNK,
	  .hook = meta_active_link_hook,
	},
	{ .name = NULL }
};


static int
graphs_dir_fill (xlator_t *this, inode_t *dir, struct meta_dirent **dp)
{
	glusterfs_graph_t *graph = NULL;
	int graphs_count = 0;
	int i = 0;
	struct meta_dirent *dirents = NULL;

	list_for_each_entry (graph, &this->ctx->graphs, list) {
		graphs_count++;
	}

	dirents = GF_CALLOC (sizeof (*dirents), graphs_count + 3,
			     gf_meta_mt_dirents_t);
	if (!dirents)
		return -1;

	i = 0;
	list_for_each_entry (graph, &this->ctx->graphs, list) {
		dirents[i].name = gf_strdup (graph->graph_uuid);
		dirents[i].type = IA_IFDIR;
		dirents[i].hook = meta_graph_dir_hook;
		i++;
	}

	*dp = dirents;

	return i;
}


struct meta_ops graphs_dir_ops = {
	.fixed_dirents = graphs_dir_dirents,
	.dir_fill = graphs_dir_fill
};


int
meta_graphs_dir_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		      dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &graphs_dir_ops);

	return 0;
}
