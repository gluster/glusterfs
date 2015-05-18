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
dict_key_add (dict_t *dict, char *key, data_t *value, void *data)
{
	struct meta_dirent **direntp = data;

	(*direntp)->name = gf_strdup (key);
	(*direntp)->type = IA_IFREG;
	(*direntp)->hook = meta_option_file_hook;

	(*direntp)++;
	return 0;
}


static int
options_dir_fill (xlator_t *this, inode_t *inode, struct meta_dirent **dp)
{
	struct meta_dirent *dirent = NULL;
	struct meta_dirent *direntp = NULL;
	xlator_t *xl = NULL;

	xl = meta_ctx_get (inode, this);

	dirent = GF_CALLOC (sizeof (*dirent), xl->options->count,
			    gf_meta_mt_dirents_t);
	if (!dirent)
		return -1;

	direntp = dirent;

	dict_foreach (xl->options, dict_key_add, &direntp);

	*dp = dirent;

	return xl->options->count;
}


static struct meta_ops options_dir_ops = {
	.dir_fill = options_dir_fill
};


int
meta_options_dir_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       dict_t *xdata)
{
	meta_ctx_set (loc->inode, this, meta_ctx_get (loc->parent, this));

	meta_ops_set (loc->inode, this, &options_dir_ops);

	return 0;
}
