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

#ifndef _NFS3_H_
#define _NFS3_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "nfs.h"
#include "nfs3-fh.h"
#include "nfs-common.h"
#include "xdr-nfs3.h"
#include "mem-pool.h"

#include <sys/statvfs.h>

#define GF_NFS3                 GF_NFS"-nfsv3"
#define GF_NFS3_PORT            38467

#define GF_NFS3_DEFAULT_MEMFACTOR       15
#define GF_NFS3_IOBPOOL_MULT            GF_NFS_CONCURRENT_OPS_MULT
#define GF_NFS3_CLTABLE_BUCKETS_MULT    2
#define GF_NFS3_FDTABLE_BUCKETS_MULT    2


/* Static values used for FSINFO
FIXME: This should be configurable */
#define GF_NFS3_RTMAX      (64 * GF_UNIT_KB)
#define GF_NFS3_RTPREF     (64 * GF_UNIT_KB)
#define GF_NFS3_RTMULT     (4 * GF_UNIT_KB)
#define GF_NFS3_WTMAX      (64 * GF_UNIT_KB)
#define GF_NFS3_WTPREF     (64 * GF_UNIT_KB)
#define GF_NFS3_WTMULT     (4 * GF_UNIT_KB)
#define GF_NFS3_DTMIN      (4 * GF_UNIT_KB)
#define GF_NFS3_DTPREF     (64 * GF_UNIT_KB)
#define GF_NFS3_MAXFILE    (1 * GF_UNIT_PB)
/* FIXME: Handle time resolutions */
#define GF_NFS3_TIMEDELTA_SECS     {1,0}
#define GF_NFS3_TIMEDELTA_NSECS    {0,1}
#define GF_NFS3_TIMEDELTA_MSECS    {0,1000000}

#define GF_NFS3_FS_PROP    (FSF3_LINK | FSF3_SYMLINK | FSF3_HOMOGENEOUS | FSF3_CANSETTIME)

#define GF_NFS3_DIRFD_VALID        1
#define GF_NFS3_DIRFD_INVALID      0

#define GF_NFS3_VOLACCESS_RW    1
#define GF_NFS3_VOLACCESS_RO    2


#define GF_NFS3_FDCACHE_SIZE    512
/* This should probably be moved to a more generic layer so that if needed
 * different versions of NFS protocol can use the same thing.
 */
struct nfs3_fd_entry {
        fd_t                    *cachedfd;
        struct list_head        list;
};

/* Per subvolume nfs3 specific state */
struct nfs3_export {
        struct list_head        explist;
        xlator_t                *subvol;
        uuid_t                  volumeid;
        int                     access;
        int                     trusted_sync;
        int                     trusted_write;
        int                     rootlookedup;
};

#define GF_NFS3_DEFAULT_VOLACCESS       (GF_NFS3_VOLACCESS_RW)

/* The NFSv3 protocol state */
struct nfs3_state {

        /* The NFS xlator pointer. The NFS xlator can be running
         * multiple versions of the NFS protocol.
         */
        xlator_t                *nfsx;

        /* The iob pool from which memory allocations are made for receiving
         * and sending network messages.
         */
        struct iobuf_pool       *iobpool;

        /* List of child subvolumes for the NFSv3 protocol.
         * Right now, is simply referring to the list of children in nfsx above.
         */
        xlator_list_t           *exportslist;

        struct list_head        exports;
        /* Mempool for allocations of struct nfs3_local */
        struct mem_pool         *localpool;

        /* Server start-up timestamp, currently used for write verifier. */
        uint64_t                serverstart;

        /* NFSv3 Protocol configurables */
        size_t                  readsize;
        size_t                  writesize;
        size_t                  readdirsize;

        /* Size of the iobufs used, depends on the sizes of the three params
         * above.
         */
        size_t                  iobsize;

        unsigned int            memfactor;

        struct list_head        fdlru;
        gf_lock_t               fdlrulock;
        int                     fdcount;
};

typedef enum nfs3_lookup_type {
        GF_NFS3_REVALIDATE = 1,
        GF_NFS3_FRESH,
} nfs3_lookup_type_t;

typedef int (*nfs3_resume_fn_t) (void *cs);
/* Structure used to communicate state between a fop and its callback.
 * Not all members are used at all times. Usage is fop and NFS request
 * dependent.
 *
 * I wish we could have a smaller structure for communicating state
 * between callers and callbacks. It could be broken into smaller parts
 * but I feel that will lead to a proliferation of types/structures and then
 * we'll just be tracking down which structure is used by which fop, not
 * to mention that having one type allows me to used a single mem-pool.
 * Imagine the chaos if we need a mem-pool for each one of those sub-structures.
 */
struct nfs3_local {
        rpcsvc_request_t        *req;
        xlator_t                *vol;
        nfs3_resume_fn_t        resume_fn;
        xlator_t                *nfsx;
        struct nfs3_state       *nfs3state;

        /* The list hook to attach this call state to the inode's queue till
         * the opening of the fd on the inode completes.
         */
        struct list_head        openwait_q;

        /* Per-NFSv3 Op state */
        struct nfs3_fh          parent;
        struct nfs3_fh          fh;
        fd_t                    *fd;
        uint32_t                accessbits;
        int                     operrno;
        count3                  dircount;
        count3                  maxcount;
        struct statvfs          fsstat;
        gf_dirent_t             entries;
        struct iatt             stbuf;
        struct iatt             preparent;
        struct iatt             postparent;
        int32_t                 setattr_valid;
        nfstime3                timestamp;
        loc_t                   oploc;
        int                     writetype;
        count3                  datacount;
        offset3                 dataoffset;
        struct iobuf            *iob;
        struct iobref           *iobref;
        createmode3             createmode;
        uint64_t                cookieverf;
        int                     sattrguardcheck;
        char                    *pathname;
        ftype3                  mknodtype;
        specdata3               devnums;
        cookie3                 cookie;
        struct iovec            datavec;
        mode_t                  mode;

        /* NFSv3 FH resolver state */
	int			hardresolved;
        struct nfs3_fh          resolvefh;
        loc_t                   resolvedloc;
        int                     resolve_ret;
        int                     resolve_errno;
        int                     hashidx;
        fd_t                    *resolve_dir_fd;
        char                    *resolventry;
        nfs3_lookup_type_t      lookuptype;
        gf_dirent_t             *hashmatch;
        gf_dirent_t             *entrymatch;
        off_t                   lastentryoffset;
};

#define nfs3_is_revalidate_lookup(cst) ((cst)->lookuptype == GF_NFS3_REVALIDATE)
#define nfs3_lookup_op(cst) (rpcsvc_request_procnum(cst->req) == NFS3_LOOKUP)
#define nfs3_create_op(cst) (rpcsvc_request_procnum(cst->req) == NFS3_CREATE)
#define nfs3_create_exclusive_op(cst) ((cst)->createmode == EXCLUSIVE)

typedef struct nfs3_local nfs3_call_state_t;

/* Queue of ops waiting for open fop to return. */
struct inode_op_queue {
        struct list_head        opq;
        pthread_mutex_t         qlock;
};




extern rpcsvc_program_t *
nfs3svc_init (xlator_t *nfsx);
#endif
