/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS_INODES_H_
#define _NFS_INODES_H_

#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "call-stub.h"
#include "stack.h"
#include "nfs-fops.h"


extern int
nfs_link_inode (inode_t *newi, inode_t *parent, char *name,
                struct iatt *newstat);

extern int
nfs_inode_create (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu,
                  loc_t *pathloc, int flags, int mode, fop_create_cbk_t cbk,
                  void *local);

extern int
nfs_inode_mkdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 int mode, fop_mkdir_cbk_t cbk, void *local);

extern int
nfs_inode_open (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                int32_t flags, fop_open_cbk_t cbk,
                void *local);

extern int
nfs_inode_rename (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
                  loc_t *newloc, fop_rename_cbk_t cbk, void *local);

extern int
nfs_inode_link (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
                loc_t *newloc, fop_link_cbk_t cbk, void *local);

extern int
nfs_inode_unlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                  fop_unlink_cbk_t cbk, void *local);

extern int
nfs_inode_rmdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 fop_rmdir_cbk_t cbk, void *local);

extern int
nfs_inode_symlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, char *target,
                   loc_t *pathloc, fop_symlink_cbk_t cbk, void *local);

extern int
nfs_inode_opendir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                   fop_opendir_cbk_t cbk, void *local);

extern int
nfs_inode_mknod (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 mode_t mode, dev_t dev, fop_mknod_cbk_t cbk, void *local);

extern int
nfs_inode_lookup (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                  fop_lookup_cbk_t cbk, void *local);
#endif
