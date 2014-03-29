/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __META_HOOKS_H
#define __META_HOOKS_H
#include "xlator.h"

#define DECLARE_HOOK(name) int meta_##name##_hook (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)

DECLARE_HOOK(root_dir);
DECLARE_HOOK(graphs_dir);
DECLARE_HOOK(frames_file);
DECLARE_HOOK(graph_dir);
DECLARE_HOOK(active_link);
DECLARE_HOOK(xlator_dir);
DECLARE_HOOK(top_link);
DECLARE_HOOK(logging_dir);
DECLARE_HOOK(logfile_link);
DECLARE_HOOK(loglevel_file);
DECLARE_HOOK(process_uuid_file);
DECLARE_HOOK(volfile_file);
DECLARE_HOOK(view_dir);
DECLARE_HOOK(subvolumes_dir);
DECLARE_HOOK(subvolume_link);
DECLARE_HOOK(type_file);
DECLARE_HOOK(version_file);
DECLARE_HOOK(options_dir);
DECLARE_HOOK(option_file);
DECLARE_HOOK(cmdline_file);
DECLARE_HOOK(name_file);
DECLARE_HOOK(private_file);
DECLARE_HOOK(mallinfo_file);
DECLARE_HOOK(history_file);
DECLARE_HOOK(master_dir);
DECLARE_HOOK(meminfo_file);
DECLARE_HOOK(measure_file);
DECLARE_HOOK(profile_file);

#endif
