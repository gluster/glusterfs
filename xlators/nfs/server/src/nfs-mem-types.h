/*
   Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef __NFS_MEM_TYPES_H__
#define __NFS_MEM_TYPES_H__

#include "mem-types.h"

enum gf_nfs_mem_types_ {
        gf_nfs_mt_mountentry  = gf_common_mt_end + 1,
        gf_nfs_mt_mountbody,
        gf_nfs_mt_nfs_state,
        gf_nfs_mt_char,
        gf_nfs_mt_exportnode,
        gf_nfs_mt_groupnode,
        gf_nfs_mt_mount3_state,
        gf_nfs_mt_write3args,
        gf_nfs_mt_nfs3_export,
        gf_nfs_mt_nfs3_state,
        gf_nfs_mt_entry3,
        gf_nfs_mt_entryp3,
        gf_nfs_mt_nfs3_fd_entry,
        gf_nfs_mt_nfs3_fh,
        gf_nfs_mt_nfs_initer_list,
        gf_nfs_mt_xlator_t,
        gf_nfs_mt_list_head,
        gf_nfs_mt_mnt3_resolve,
        gf_nfs_mt_mnt3_export,
        gf_nfs_mt_mnt3_auth_params,
        gf_nfs_mt_int,
        gf_nfs_mt_mountres3,
        gf_nfs_mt_mountstat3,
        gf_nfs_mt_inode_q,
        gf_nfs_mt_nlm4_state,
        gf_nfs_mt_nlm4_cm,
        gf_nfs_mt_nlm4_fde,
        gf_nfs_mt_nlm4_nlmclnt,
        gf_nfs_mt_nlm4_share,
        gf_nfs_mt_aux_gids,
        gf_nfs_mt_inode_ctx,
        gf_nfs_mt_auth_spec,
        gf_nfs_mt_arr,
        gf_nfs_mt_auth_cache,
        gf_nfs_mt_auth_cache_entry,
        gf_nfs_mt_end
};
#endif

