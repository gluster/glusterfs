/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _NFS_INODES_H_
#define _NFS_INODES_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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
