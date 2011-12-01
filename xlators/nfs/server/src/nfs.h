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

#ifndef __NFS_H__
#define __NFS_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"

#define GF_NFS                  "nfs"

#define GF_NFS_CONCURRENT_OPS_MULT     15

#define GF_NFS_INODE_LRU_MULT           6000

#define GF_RPC_MIN_THREADS      1
#define GF_RPC_MAX_THREADS      16

#define GF_NFS_DEFAULT_MEMFACTOR        15
#define GF_NFS_MIN_MEMFACTOR            1
#define GF_NFS_MAX_MEMFACTOR            30

#define GF_NFS_DVM_ON                   1
#define GF_NFS_DVM_OFF                  2

/* Callback into a version-specific NFS protocol.
 * The return type is used by the nfs.c code to register the protocol.
 * with the RPC service.
 */
typedef rpcsvc_program_t *(*nfs_version_initer_t) (xlator_t *nfsx);

/* List of version-specific protocol initiators */
struct nfs_initer_list {
        struct list_head list;
        nfs_version_initer_t    init;
        rpcsvc_program_t        *program;
};


struct nfs_state {
        rpcsvc_t                *rpcsvc;
        struct list_head        versions;
        struct mount3_state     *mstate;
        struct mem_pool         *foppool;
        unsigned int            memfactor;
        xlator_list_t           *subvols;

        gf_lock_t               svinitlock;
        int                     allsubvols;
        int                     upsubvols;
        xlator_t                **initedxl;
        int                     subvols_started;
        int                     dynamicvolumes;
        int                     enable_ino32;
        unsigned int            override_portnum;
        int                     allow_insecure;
};

#define gf_nfs_dvm_on(nfsstt)   (((struct nfs_state *)nfsstt)->dynamicvolumes == GF_NFS_DVM_ON)
#define gf_nfs_dvm_off(nfsstt)  (((struct nfs_state *)nfsstt)->dynamicvolumes == GF_NFS_DVM_OFF)
#define __gf_nfs_enable_ino32(nfsstt)     (((struct nfs_state *)nfsstt)->enable_ino32)
#define gf_nfs_this_private     ((struct nfs_state *)((xlator_t *)THIS)->private)
#define gf_nfs_enable_ino32()     (__gf_nfs_enable_ino32(gf_nfs_this_private))

/* We have one gid more than the glusterfs maximum since we pass the primary
 * gid as the first element of the array.
 */
#define NFS_NGROUPS         (GF_REQUEST_MAXGROUPS + 1)

/* Index of the primary gid */
#define NFS_PRIMGID_IDX     0

typedef struct nfs_user_info {
        uid_t   uid;
        gid_t   gids[NFS_NGROUPS];
        int     ngrps;
} nfs_user_t;

extern int
nfs_user_root_create (nfs_user_t *newnfu);

extern int
nfs_user_create (nfs_user_t *newnfu, uid_t uid, gid_t gid, gid_t *auxgids,
                 int auxcount);

extern void
nfs_request_user_init (nfs_user_t *nfu, rpcsvc_request_t *req);

extern void
nfs_request_primary_user_init (nfs_user_t *nfu, rpcsvc_request_t *req,
                               uid_t uid, gid_t gid);
extern int
nfs_subvolume_started (struct nfs_state *nfs, xlator_t *xl);
#endif
