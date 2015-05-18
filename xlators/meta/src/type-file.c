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
type_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	xlator_t *xl = NULL;

	xl = meta_ctx_get (file, this);

	strprintf (strfd, "%s\n", xl->type);

	return strfd->size;
}


static struct meta_ops type_file_ops = {
	.file_fill = type_file_fill,
};


int
meta_type_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		     dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &type_file_ops);

	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	return 0;
}
