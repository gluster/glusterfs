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


static struct meta_dirent graph_dir_dirents[] = {
	DOT_DOTDOT,

	{ .name = "top",
	  .type = IA_IFLNK,
	  .hook = meta_top_link_hook,
	},
	{ .name = "volfile",
	  .type = IA_IFREG,
	  .hook = meta_volfile_file_hook,
	},
	{ .name = NULL }
};


static int
graph_dir_fill (xlator_t *this, inode_t *inode, struct meta_dirent **dp)
{
	struct meta_dirent *dirents = NULL;
	glusterfs_graph_t *graph = NULL;
	int i = 0;
	int count = 0;
	xlator_t *xl = NULL;

	graph = meta_ctx_get (inode, this);

	for (xl = graph->first; xl; xl = xl->next)
		count++;

	dirents = GF_CALLOC (sizeof (*dirents), count, gf_meta_mt_dirents_t);
	if (!dirents)
		return -1;

	i = 0;
	for (xl = graph->first; xl; xl = xl->next) {
		dirents[i].name = gf_strdup (xl->name);
		dirents[i].type = IA_IFDIR;
		dirents[i].hook = meta_xlator_dir_hook;
		i++;
	}

	*dp = dirents;
	return i;
}


struct meta_ops graph_dir_ops = {
	.fixed_dirents = graph_dir_dirents,
	.dir_fill = graph_dir_fill,
};


static glusterfs_graph_t *
glusterfs_graph_lookup (xlator_t *this, const char *graph_uuid)
{
	glusterfs_graph_t *graph = NULL;
	glusterfs_graph_t *tmp = NULL;

	list_for_each_entry (tmp, &this->ctx->graphs, list) {
		if (strcmp (graph_uuid, tmp->graph_uuid) == 0) {
			graph = tmp;
			break;
		}
	}

	return graph;
}


int
meta_graph_dir_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     dict_t *xdata)
{
	glusterfs_graph_t *graph = NULL;

	graph = glusterfs_graph_lookup (this, loc->name);

	meta_ops_set (loc->inode, this, &graph_dir_ops);

	meta_ctx_set (loc->inode, this, (void *) graph);

	return 0;
}
