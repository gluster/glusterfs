/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS_COMMON_H_
#define _NFS_COMMON_H_

#include <unistd.h>

#include "xlator.h"
#include "rpcsvc.h"
#include "iatt.h"
#include "compat-uuid.h"

//NFS_PATH_MAX hard-coded to 4096 as a work around for bug 2476.
//nfs server crashes when path received is longer than PATH_MAX
#define NFS_PATH_MAX    4096
#define NFS_NAME_MAX    NAME_MAX

#define NFS_DEFAULT_CREATE_MODE 0600

extern xlator_t *
nfs_xlid_to_xlator (xlator_list_t *cl, uint8_t xlid);

extern uint16_t
nfs_xlator_to_xlid (xlator_list_t *cl, xlator_t *xl);

extern xlator_t *
nfs_path_to_xlator (xlator_list_t *cl, char *path);

extern xlator_t *
nfs_mntpath_to_xlator (xlator_list_t *cl, char *path);

extern void
nfs_loc_wipe (loc_t *loc);

extern int
nfs_loc_copy (loc_t *dst, loc_t *src);

extern int
nfs_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path);

#define NFS_RESOLVE_EXIST       1
#define NFS_RESOLVE_CREATE      2

extern int
nfs_inode_loc_fill (inode_t *inode, loc_t *loc, int how);

extern int
nfs_ino_loc_fill (inode_table_t *itable, uuid_t gfid, loc_t *l);

extern int
nfs_entry_loc_fill (xlator_t *this, inode_table_t *itable, uuid_t pargfid,
                    char *entry, loc_t *loc, int how,
                    gf_boolean_t *freshlookup);

extern int
nfs_root_loc_fill (inode_table_t *itable, loc_t *loc);

extern uint32_t
nfs_hash_gfid (uuid_t gfid);

extern int
nfs_gfid_loc_fill (inode_table_t *itable, uuid_t gfid, loc_t *loc, int how);

void
nfs_fix_generation (xlator_t *this, inode_t *inode);
#endif
