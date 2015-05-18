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
#include "globals.h"
#include "lkowner.h"


static int
name_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	xlator_t *xl = NULL;

	xl = meta_ctx_get (file, this);

	strprintf (strfd, "%s\n", xl->name);

	return strfd->size;
}


static struct meta_ops name_file_ops = {
	.file_fill = name_file_fill,
};


int
meta_name_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &name_file_ops);

	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	return 0;
}
