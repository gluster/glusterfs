/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __MARKER_MEM_TYPES_H__
#define __MARKER_MEM_TYPES_H__

#include <glusterfs/mem-types.h>

enum gf_marker_mem_types_ {
    /* Those are used by ALLOCATE_OR_GOTO macro */
    gf_marker_mt_marker_conf_t = GF_MEM_TYPE_START,
    gf_marker_mt_loc_t,
    gf_marker_mt_volume_mark,
    gf_marker_mt_int64_t,
    gf_marker_mt_quota_inode_ctx_t,
    gf_marker_mt_marker_inode_ctx_t,
    gf_marker_mt_inode_contribution_t,
    gf_marker_mt_quota_meta_t,
    gf_marker_mt_quota_synctask_t,
    gf_marker_mt_end
};
#endif
