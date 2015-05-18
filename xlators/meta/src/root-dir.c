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


static struct meta_dirent root_dir_dirents[] = {
	DOT_DOTDOT,

	{ .name = "graphs",
	  .type = IA_IFDIR,
	  .hook = meta_graphs_dir_hook,
	},
	{ .name = "frames",
	  .type = IA_IFREG,
	  .hook = meta_frames_file_hook,
	},
	{ .name = "logging",
	  .type = IA_IFDIR,
	  .hook = meta_logging_dir_hook,
	},
	{ .name = "process_uuid",
	  .type = IA_IFREG,
	  .hook = meta_process_uuid_file_hook,
	},
	{ .name = "version",
	  .type = IA_IFREG,
	  .hook = meta_version_file_hook,
	},
	{ .name = "cmdline",
	  .type = IA_IFREG,
	  .hook = meta_cmdline_file_hook,
	},
	{ .name = "mallinfo",
	  .type = IA_IFREG,
	  .hook = meta_mallinfo_file_hook,
	},
	{ .name = "master",
	  .type = IA_IFDIR,
	  .hook = meta_master_dir_hook,
	},
	{ .name = "measure_latency",
	  .type = IA_IFREG,
	  .hook = meta_measure_file_hook,
	},
	{ .name = NULL }
};


static struct meta_ops meta_root_dir_ops = {
	.fixed_dirents = root_dir_dirents
};


int
meta_root_dir_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &meta_root_dir_ops);

	return 0;
}
