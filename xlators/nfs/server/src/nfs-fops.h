/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NFS_FOPS_H_
#define _NFS_FOPS_H_

#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "call-stub.h"
#include "stack.h"
#include "nfs.h"
#include "nfs-common.h"
#include "nfs-messages.h"
#include <semaphore.h>

/* This structure used to communicate state between a fop and its callback.
 * The problem is, when we're calling a fop in the nfs op handler, the callback
 * is the NFS protocol's callback and we have to perform all GlusterFS
 * inode, inode table, fd_ts and fd table operations in the NFS callback. That
 * approach soon gets extremely complicated and confusing because, then we have
 * to try and separate in our heads which source lines in those callbacks are
 * required for serving the NFS op and which ones are needed for satisfying
 * GlusterFS requirements. This structure allows us avoid performing GlusterFS
 * state maintenance operations inside the fops layer itself. Now, before
 * we call the callback registered by the NFS operation, a hidden fops-layer
 * specific callback is called which performs the state maintenance and then
 * calls the NFS callback.
 *
 * These are allocated from a mem-pool stored in the nfs xlator's state.
 * i.e. struct nfs_state.
 * That is initiated in nfs_init_subvolumes in nfs.c.
 */
struct nfs_fop_local {
        /* The local sent along by the user of the fop. */
        void            *proglocal;

        /* The address of the callback supplied by the user. After our
         * callback is executed this one is called.
         * The exact cast destination of this pointer will depend on the
         * fop that is being called.
         */
        void            *progcbk;

        /* Used only for write requests. */
        struct iobref   *iobref;

        inode_t         *parent;
        inode_t         *newparent;
        inode_t         *inode;

        /* Set to 1 by nfs-inodes layer, which uses this to decide whether to
         * link the newly allocated inode into the itable, in case the fop was
         * successful.
         */
        int             newinode;

        /* Used by nfs-fops layer in order to determine whether to funge the
         * ino in a dir's stbuf. This funging of root ino is needed to ensure
         * that the root ino remains 1 even when the NFS server has been
         * restarted. Note that in distribute, a fresh lookup and a revalidate
         * on the root inode returns two different inode numbers and this we
         * need to handle by ourself.
         */
        int             rootinode;

        /* This member is used to determine whether the new parent of a file
         * being renamed is the root directory. If yes, the ino is funged.
         */
        int             newrootinode;
        int             newrootparentinode;

        /* Determines whether to funge the ino in the post and pre parent
         * stbufs for a file/dir where the parent directory could be the root
         * dir. Needed here because of the same reason as above.
         */
        int             rootparentinode;

        char            path[NFS_NAME_MAX + 1];
        char            newpath[NFS_NAME_MAX + 1];
        xlator_t        *nfsx;
        dict_t          *dictgfid;

        fd_t            *fd;
        int             cmd;
        struct gf_flock flock;
};

extern struct nfs_fop_local *
nfs_fop_local_init (xlator_t *xl);

extern void
nfs_fop_local_wipe (xlator_t *xl, struct nfs_fop_local *l);

#define nfs_state(nfsxl)        (nfsxl)->private
#define nfs_fop_mempool(nfxl)   (((struct nfs_state *)nfs_state(nfxl))->foppool)

#define prog_data_to_nfl(nf,nflocal, fram, pcbk, plocal)                \
        do {                                                            \
                nflocal = nfs_fop_local_init (nf);                      \
                if (nflocal) {                                          \
                        nflocal->proglocal = plocal;                    \
                        nflocal->progcbk = *VOID(&pcbk);                \
                        nflocal->nfsx = nf;                             \
                        if (fram)                                       \
                                ((call_frame_t *)fram)->local = nflocal;\
                }                                                       \
        } while (0)                                                     \



#define nfl_to_prog_data(nflocal, pcbk, fram)                           \
        do {                                                            \
                nflocal = fram->local;                                  \
                fram->local = nflocal->proglocal;                       \
                pcbk = nflocal->progcbk;                                \
        } while (0)                                                     \

#define nfs_fop_handle_local_init(fram,nfx, nfloc, cbck,prgloc,retval,lab)  \
        do {                                                                \
                prog_data_to_nfl (nfx, nfloc, fram, cbck, prgloc);          \
                if (!nfloc) {                                               \
                        gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM,               \
                                NFS_MSG_NO_MEMORY, "Failed to init local"); \
                        retval = -ENOMEM;                                   \
                        goto lab;                                           \
                }                                                           \
        } while (0)                                                         \

extern int
nfs_fop_fstat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               fop_stat_cbk_t cbk, void *local);

extern int
nfs_fop_readdirp (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *dirfd,
                  size_t bufsize, off_t offset, fop_readdir_cbk_t cbk,
                  void *local);
extern int
nfs_fop_lookup (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                fop_lookup_cbk_t cbk, void *local);

extern int
nfs_fop_create (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                int flags, mode_t mode, fd_t *fd, fop_create_cbk_t cbk,
                void *local);
extern int
nfs_fop_flush (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               fop_flush_cbk_t cbk, void *local);

extern int
nfs_fop_mkdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               mode_t mode, fop_mkdir_cbk_t cbk, void *local);

extern int
nfs_fop_truncate (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                  off_t offset, fop_truncate_cbk_t cbk, void *local);

extern int
nfs_fop_read (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
              size_t size, off_t offset, fop_readv_cbk_t cbk, void *local);

extern int
nfs_fop_fsync (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               int32_t datasync, fop_fsync_cbk_t cbk, void *local);

extern int
nfs_fop_write (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               struct iobref *srciobref, struct iovec *vector, int32_t count,
               off_t offset, fop_writev_cbk_t cbk, void *local);

extern int
nfs_fop_open (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
              int32_t flags, fd_t *fd, fop_open_cbk_t cbk,
              void *local);

extern int
nfs_fop_rename (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
                loc_t *newloc, fop_rename_cbk_t cbk, void *local);

extern int
nfs_fop_link (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
              loc_t *newloc, fop_link_cbk_t cbk, void *local);

extern int
nfs_fop_unlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                fop_unlink_cbk_t cbk, void *local);

extern int
nfs_fop_rmdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               fop_rmdir_cbk_t cbk, void *local);

extern int
nfs_fop_mknod (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               mode_t mode, dev_t dev, fop_mknod_cbk_t cbk, void *local);

extern int
nfs_fop_readlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                  size_t size, fop_readlink_cbk_t cbk, void *local);

extern int
nfs_fop_symlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, char *target,
                 loc_t *pathloc, fop_symlink_cbk_t cbk, void *local);

extern int
nfs_fop_setattr (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 struct iatt *buf, int32_t valid, fop_setattr_cbk_t cbk,
                 void *local);

extern int
nfs_fop_statfs (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                fop_statfs_cbk_t cbk, void *local);

extern int
nfs_fop_opendir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 fd_t *dirfd, fop_opendir_cbk_t cbk, void *local);

extern int
nfs_fop_stat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
              fop_stat_cbk_t cbk, void *local);

extern int
nfs_fop_access (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                int32_t accesstest, fop_access_cbk_t cbk, void *local);

extern int
nfs_fop_lk (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
            int cmd, struct gf_flock *flock, fop_lk_cbk_t cbk, void *local);

extern int
nfs_fop_getxattr (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                  char *name, dict_t *xdata, fop_getxattr_cbk_t cbk, void *local);

extern int
nfs_fop_setxattr (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu,
                  loc_t *loc, dict_t *dict, int32_t flags, dict_t *xdata,
                  fop_setxattr_cbk_t cbk, void *local);

#endif
