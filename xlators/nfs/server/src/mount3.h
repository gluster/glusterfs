/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _MOUNT3_H_
#define _MOUNT3_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "nfs.h"
#include "list.h"
#include "xdr-nfs3.h"
#include "locking.h"

/* Registered with portmap */
#define GF_MOUNTV3_PORT         38465
#define GF_MOUNTV3_IOB          (2 * GF_UNIT_KB)
#define GF_MOUNTV3_IOBPOOL      (GF_MOUNTV3_IOB * 50)

#define GF_MOUNTV1_PORT         38466
#define GF_MNT                  GF_NFS"-mount"

extern rpcsvc_program_t *
mnt3svc_init (xlator_t *nfsx);

extern rpcsvc_program_t *
mnt1svc_init (xlator_t *nfsx);

/* Data structureused to store the list of mounts points currently
 * in use by NFS clients.
 */
struct mountentry {
        /* Links to mount3_state->mountlist.  */
        struct list_head        mlist;

        /* The export name */
        char                    exname[MNTPATHLEN];
        char                    hostname[MNTPATHLEN];
};

struct mount3_state {
        xlator_t                *nfsx;

        /* The buffers for all network IO are got from this pool. */
        struct iobuf_pool       *iobpool;
        xlator_list_t           *exports;

        /* List of current mount points over all the exports from this
         * server.
         */
        struct list_head        mountlist;

        /* Used to protect the mountlist. */
        gf_lock_t               mountlock;
};
#endif
