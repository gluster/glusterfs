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
active_link_fill (xlator_t *this, inode_t *inode, strfd_t *strfd)
{
	strprintf (strfd, "%s", this->ctx->active->graph_uuid);

	return 0;
}


struct meta_ops active_link_ops = {
	.link_fill = active_link_fill
};


int
meta_active_link_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &active_link_ops);

	return 0;
}
