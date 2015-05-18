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


static struct meta_dirent view_dir_dirents[] = {
	DOT_DOTDOT,

	{ .name = NULL }
};


static struct meta_ops view_dir_ops = {
	.fixed_dirents = view_dir_dirents
};


int
meta_view_dir_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    dict_t *xdata)
{
	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	meta_ops_set (loc->inode, this, &view_dir_ops);

	return 0;
}
