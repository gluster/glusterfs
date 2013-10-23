/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "glusterfs.h"

#include "glfs-internal.h"
#include "glfs-mem-types.h"


int
glfs_graph_setup (struct glfs *fs, glusterfs_graph_t *graph)
{
	xlator_t      *new_subvol = NULL;
	xlator_t      *old_subvol = NULL;
	inode_table_t *itable = NULL;
	int            ret = -1;

	new_subvol = graph->top;

	/* This is called in a bottom-up context, it should specifically
	   NOT be glfs_lock()
	*/
	pthread_mutex_lock (&fs->mutex);
	{
		if (new_subvol->switched ||
		    new_subvol == fs->active_subvol ||
		    new_subvol == fs->next_subvol) {
			/* Spurious CHILD_UP event on old graph */
			ret = 0;
			goto unlock;
		}

		if (!new_subvol->itable) {
			itable = inode_table_new (131072, new_subvol);
			if (!itable) {
				errno = ENOMEM;
				ret = -1;
				goto unlock;
			}

			new_subvol->itable = itable;
		}

		old_subvol = fs->next_subvol;
		fs->next_subvol = new_subvol;
		fs->next_subvol->winds++; /* first ref */
		ret = 0;
	}
unlock:
	pthread_mutex_unlock (&fs->mutex);

	if (old_subvol)
		/* wasn't picked up so far, skip */
		glfs_subvol_done (fs, old_subvol);

	return ret;
}


int
notify (xlator_t *this, int event, void *data, ...)
{
	glusterfs_graph_t   *graph = NULL;
	struct glfs	    *fs = NULL;

	graph = data;
	fs = this->private;

	switch (event) {
	case GF_EVENT_GRAPH_NEW:
		gf_log (this->name, GF_LOG_INFO, "New graph %s (%d) coming up",
			uuid_utoa ((unsigned char *)graph->graph_uuid),
			graph->id);
		break;
	case GF_EVENT_CHILD_UP:
		glfs_graph_setup (fs, graph);
		glfs_init_done (fs, 0);
		break;
	case GF_EVENT_CHILD_DOWN:
		glfs_graph_setup (fs, graph);
		glfs_init_done (fs, 1);
		break;
	case GF_EVENT_CHILD_CONNECTING:
		break;
	default:
		gf_log (this->name, GF_LOG_DEBUG,
			"got notify event %d", event);
		break;
	}

	return 0;
}


int
mem_acct_init (xlator_t *this)
{
	int ret = -1;

	if (!this)
		return ret;

	ret = xlator_mem_acct_init (this, glfs_mt_end + 1);
	if (ret) {
		gf_log (this->name, GF_LOG_ERROR, "Failed to initialise "
                        "memory accounting");
		return ret;
	}

	return 0;
}


int
init (xlator_t *this)
{
	return 0;
}


void
fini (xlator_t *this)
{

}


struct xlator_dumpops dumpops;


struct xlator_fops fops;


struct xlator_cbks cbks;
