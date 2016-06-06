/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS3_H_
#define _NFS3_H_

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "nfs.h"
#include "nfs3-fh.h"
#include "nfs-common.h"
#include "xdr-nfs3.h"
#include "mem-pool.h"
#include "nlm4.h"
#include "acl3-xdr.h"
#include "acl3.h"
#include <sys/statvfs.h>

#define GF_NFS3                 GF_NFS"-nfsv3"

#define GF_NFS3_DEFAULT_MEMFACTOR       15
#define GF_NFS3_IOBPOOL_MULT            GF_NFS_CONCURRENT_OPS_MULT
#define GF_NFS3_CLTABLE_BUCKETS_MULT    2
#define GF_NFS3_FDTABLE_BUCKETS_MULT    2


/* Static values used for FSINFO
 * To change the maximum rsize and wsize supported by the NFS client, adjust
 * GF_NFS3_FILE_IO_SIZE_MAX. The Gluster NFS server defaults to 1MB(1048576)
 * (same as kernel NFS server). For slower network, rsize/wsize can be trimmed
 * to 16/32/64-KB. rsize and wsize can be tuned through nfs.read-size and
 * nfs.write-size respectively.
 *
 * NB: For Kernel-NFS, NFS_MAX_FILE_IO_SIZE is 1048576U (1MB).
 */
#define GF_NFS3_FILE_IO_SIZE_MAX     (1  * GF_UNIT_MB) /* 1048576 */
#define GF_NFS3_FILE_IO_SIZE_MIN     (4  * GF_UNIT_KB) /* 4096 */

#define GF_NFS3_FILE_IO_SIZE_DEF     GF_NFS3_FILE_IO_SIZE_MAX

#define GF_NFS3_RTMAX          GF_NFS3_FILE_IO_SIZE_MAX
#define GF_NFS3_RTMIN          GF_NFS3_FILE_IO_SIZE_MIN
#define GF_NFS3_RTPREF         GF_NFS3_FILE_IO_SIZE_DEF
#define GF_NFS3_RTMULT         GF_NFS3_FILE_IO_SIZE_MIN

#define GF_NFS3_WTMAX          GF_NFS3_FILE_IO_SIZE_MAX
#define GF_NFS3_WTMIN          GF_NFS3_FILE_IO_SIZE_MIN
#define GF_NFS3_WTPREF         GF_NFS3_FILE_IO_SIZE_DEF
#define GF_NFS3_WTMULT         GF_NFS3_FILE_IO_SIZE_MIN

/* This can be tuned through nfs.readdir-size */
#define GF_NFS3_DTMAX          GF_NFS3_FILE_IO_SIZE_MAX
#define GF_NFS3_DTMIN          GF_NFS3_FILE_IO_SIZE_MIN
#define GF_NFS3_DTPREF         GF_NFS3_FILE_IO_SIZE_DEF

#define GF_NFS3_MAXFILESIZE    (1 * GF_UNIT_PB)

#define GF_NFS3_IO_SIZE        4096 /* 4-KB */
#define GF_NFS3_IO_SHIFT       12   /* 2^12 = 4KB */

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
typedef struct nfs3_state {

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
        uint64_t                readsize;
        uint64_t                writesize;
        uint64_t                readdirsize;

        /* Size of the iobufs used, depends on the sizes of the three params
         * above.
         */
        uint64_t                iobsize;

        struct list_head        fdlru;
        gf_lock_t               fdlrulock;
        int                     fdcount;
        uint32_t                occ_logger;
} nfs3_state_t;

typedef enum nfs3_lookup_type {
        GF_NFS3_REVALIDATE = 1,
        GF_NFS3_FRESH,
} nfs3_lookup_type_t;

typedef union args_ {
        nlm4_stat nlm4_stat;
        nlm4_holder nlm4_holder;
        nlm4_lock nlm4_lock;
        nlm4_share nlm4_share;
        nlm4_testrply nlm4_testrply;
        nlm4_testres nlm4_testres;
        nlm4_testargs nlm4_testargs;
        nlm4_res nlm4_res;
        nlm4_lockargs nlm4_lockargs;
        nlm4_cancargs nlm4_cancargs;
        nlm4_unlockargs nlm4_unlockargs;
        nlm4_shareargs nlm4_shareargs;
        nlm4_shareres nlm4_shareres;
        nlm4_freeallargs nlm4_freeallargs;
        getaclargs getaclargs;
        setaclargs setaclargs;
        getaclreply getaclreply;
        setaclreply setaclreply;
} args;


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
        struct iatt             attr_in;

        /* NFSv3 FH resolver state */
        int                     hardresolved;
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
        struct flock            flock;
        args                    args;
        nlm4_lkowner_t          lkowner;
        char                    cookiebytes[1024];
        struct nfs3_fh          lockfh;
        int                     monitor;
        rpc_transport_t         *trans;
        call_frame_t            *frame;

        /* ACL */
        aclentry                aclentry[NFS_ACL_MAX_ENTRIES];
        aclentry                daclentry[NFS_ACL_MAX_ENTRIES];
        int                     aclcount;
        char                    aclxattr[NFS_ACL_MAX_ENTRIES*8 + 4];
        int                     daclcount;
        char                    daclxattr[NFS_ACL_MAX_ENTRIES*8 + 4];
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

extern int
nfs3_reconfigure_state (xlator_t *nfsx, dict_t *options);

extern uint64_t
nfs3_request_xlator_deviceid (rpcsvc_request_t *req);

#endif
