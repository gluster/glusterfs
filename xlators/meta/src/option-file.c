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


static int
option_file_fill (xlator_t *this, inode_t *inode, strfd_t *strfd)
{
	data_t *data = NULL;

	data = meta_ctx_get (inode, this);

	strprintf (strfd, "%s\n", data_to_str (data));

	return strfd->size;
}


static struct meta_ops option_file_ops = {
	.file_fill = option_file_fill
};


int
meta_option_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       dict_t *xdata)
{
	xlator_t *xl = NULL;

	xl = meta_ctx_get (loc->parent, this);

	meta_ctx_set (loc->inode, this,
		      dict_get (xl->options, (char *) loc->name));

	meta_ops_set (loc->inode, this, &option_file_ops);

	return 0;
}
