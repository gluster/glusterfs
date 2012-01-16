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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "call-stub.h"
#include "stack.h"
#include "nfs.h"
#include "nfs-fops.h"
#include "inode.h"
#include "nfs-common.h"
#include "nfs3-helpers.h"
#include <libgen.h>
#include <semaphore.h>

struct nfs_fop_local *
nfs_fop_local_init (xlator_t *nfsx)
{
        struct nfs_fop_local    *l = NULL;

        if (!nfsx)
                return NULL;

        l = mem_get (nfs_fop_mempool (nfsx));

        memset (l, 0, sizeof (*l));
        return l;
}

void
nfs_fop_local_wipe (xlator_t *nfsx, struct nfs_fop_local *l)
{
        if ((!nfsx) || (!l))
                return;

        if (l->iobref)
                iobref_unref (l->iobref);

        if (l->parent)
                inode_unref (l->parent);

        if (l->inode)
                inode_unref (l->inode);

        if (l->newparent)
                inode_unref (l->newparent);

        if (l->dictgfid)
                dict_unref (l->dictgfid);

        mem_put (l);

        return;
}

#define nfs_stack_destroy(nfl, fram)                                    \
        do {                                                            \
                nfs_fop_local_wipe ((nfl)->nfsx, nfl);                  \
                (fram)->local = NULL;                                   \
                STACK_DESTROY ((fram)->root);                           \
        } while (0)                                                     \


pthread_mutex_t         ctr = PTHREAD_MUTEX_INITIALIZER;
unsigned int            cval = 1;


int
nfs_frame_getctr ()
{
        uint64_t val = 0;

        pthread_mutex_lock (&ctr);
        {
                if (cval == 0)
                        cval = 1;
                val = cval;
                cval++;
        }
        pthread_mutex_unlock (&ctr);

        return val;
}


call_frame_t *
nfs_create_frame (xlator_t *xl, nfs_user_t *nfu)
{
        call_frame_t    *frame = NULL;
        int             x = 0;
        int             y = 0;

        if ((!xl) || (!nfu) || (nfu->ngrps > NFS_NGROUPS))
                return NULL;

        frame = create_frame (xl, (call_pool_t *)xl->ctx->pool);
        if (!frame)
                goto err;
        frame->root->pid = NFS_PID;
        frame->root->uid = nfu->uid;
        frame->root->gid = nfu->gids[NFS_PRIMGID_IDX];
        if (nfu->ngrps == 1)
                goto err;       /* Done, we only got primary gid */

        frame->root->ngrps = nfu->ngrps - 1;

        gf_log (GF_NFS, GF_LOG_TRACE,"uid: %d, gid %d, gids: %d",
                frame->root->uid, frame->root->gid, frame->root->ngrps);
        for(y = 0, x = 1;  y < frame->root->ngrps; x++,y++) {
                gf_log (GF_NFS, GF_LOG_TRACE, "gid: %d", nfu->gids[x]);
                frame->root->groups[y] = nfu->gids[x];
        }

        set_lk_owner_from_uint64 (&frame->root->lk_owner, nfs_frame_getctr ());

err:
        return frame;
}

#define nfs_fop_handle_frame_create(fram, xla, nfuser, retval, errlabel)      \
        do {                                                                  \
                fram = nfs_create_frame (xla, (nfuser));                      \
                if (!fram) {                                                  \
                        retval = (-ENOMEM);                                   \
                        gf_log (GF_NFS, GF_LOG_ERROR,"Frame creation failed");\
                        goto errlabel;                                        \
                }                                                             \
        } while (0)                                                           \

/* Look into the  inode and parent inode of a loc and save enough state
 * for us to determine in the callback whether to funge the ino in the stat buf
 * with 1 for the parent.
 */
#define nfs_fop_save_root_ino(locl, loc)                                \
        do {                                                            \
                if (((loc)->inode) &&                                   \
                    __is_root_gfid ((loc)->inode->gfid))                \
                        (locl)->rootinode = 1;                          \
                else if (((loc)->parent) &&                             \
                         __is_root_gfid ((loc)->parent->gfid))          \
                        (locl)->rootparentinode = 1;                    \
        } while (0)

/* Do the same for an fd */
#define nfs_fop_save_root_fd_ino(locl, fdesc)                           \
        do {                                                            \
                if (__is_root_gfid ((fdesc)->inode->gfid))              \
                        (locl)->rootinode = 1;                          \
        } while (0)


/* Use the state saved by the previous macro to funge the ino in the appropriate
 * structure.
 */
#define nfs_fop_restore_root_ino(locl, fopret, preattr, postattr, prepar, postpar)  \
        do {                                                                \
                if (fopret == -1)                                           \
                        break;                                              \
                if ((locl)->rootinode) {                                    \
                        if ((preattr)) {                                    \
                                ((struct iatt *)(preattr))->ia_ino = 1;     \
                                ((struct iatt *)(preattr))->ia_dev = 0;     \
                        }                                                   \
                        if ((postattr)) {                                   \
                                ((struct iatt *)(postattr))->ia_ino = 1;    \
                                ((struct iatt *)(postattr))->ia_dev = 0;    \
                        }                                                   \
                } else if ((locl)->rootparentinode) {                       \
                        if ((prepar)) {                                     \
                                ((struct iatt *)(prepar))->ia_ino = 1;      \
                                ((struct iatt *)(prepar))->ia_dev = 0;      \
                        }                                                   \
                        if ((postpar)) {                                    \
                                ((struct iatt *)(postpar))->ia_ino = 1;     \
                                ((struct iatt *)(postpar))->ia_dev = 0;     \
                        }                                                   \
                }                                                           \
        } while (0)                                                         \

/* If the newly created, inode's parent is root, we'll need to funge the ino
 * in the parent attr when we receive them in the callback.
 */
#define nfs_fop_newloc_save_root_ino(locl, newloc)                      \
        do {                                                            \
                if (((newloc)->inode) &&                                \
                    __is_root_gfid ((newloc)->inode->gfid))             \
                        (locl)->newrootinode = 1;                       \
                else if (((newloc)->parent) &&                          \
                         __is_root_gfid ((newloc)->parent->gfid))       \
                        (locl)->newrootparentinode = 1;                 \
        } while (0)

#define nfs_fop_newloc_restore_root_ino(locl, fopret, preattr, postattr, prepar, postpar)  \
        do {                                                                   \
                if (fopret == -1)                                              \
                        break;                                                 \
                                                                               \
                if ((locl)->newrootinode) {                                    \
                        if ((preattr))                                         \
                                ((struct iatt *)(preattr))->ia_ino = 1;        \
                        if ((postattr))                                        \
                                ((struct iatt *)(postattr))->ia_ino = 1;       \
                } else if ((locl)->newrootparentinode) {                       \
                        if ((prepar))                                          \
                                ((struct iatt *)(prepar))->ia_ino = 1;         \
                        if ((postpar))                                         \
                                ((struct iatt *)(postpar))->ia_ino = 1;        \
                }                                                              \
        } while (0)                                                            \

dict_t *
nfs_gfid_dict (inode_t *inode)
{
        uuid_t  newgfid = {0, };
        char    *dyngfid = NULL;
        dict_t  *dictgfid = NULL;
        int     ret = -1;
        uuid_t  rootgfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

        dyngfid = GF_CALLOC (1, sizeof (uuid_t), gf_common_mt_char);
        uuid_generate (newgfid);

        if (uuid_compare (inode->gfid, rootgfid) == 0)
                memcpy (dyngfid, rootgfid, sizeof (uuid_t));
        else
                memcpy (dyngfid, newgfid, sizeof (uuid_t));

        dictgfid = dict_new ();
        if (!dictgfid) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to create gfid dict");
                goto out;
        }

        ret = dict_set_bin (dictgfid, "gfid-req", dyngfid, sizeof (uuid_t));
        if (ret < 0) {
                dict_unref (dictgfid);
                dictgfid = NULL;
        }

out:
        return dictgfid;
}

#define nfs_fop_gfid_setup(nflcl, inode, retval, erlbl)                 \
        do {                                                            \
                if (nflcl) {                                            \
                        (nflcl)->dictgfid = nfs_gfid_dict (inode);      \
                                                                        \
                        if (!((nflcl)->dictgfid)) {                     \
                                retval = -EFAULT;                       \
                                goto erlbl;                             \
                        }                                               \
                }                                                       \
        } while (0)                                                     \




/* Fops Layer Explained
 * The fops layer has three types of functions. They can all be identified by
 * their names. Here are the three patterns:
 *
 * nfs_fop_<fopname>
 * This is the lowest level function that knows nothing about states and
 * callbacks. At most this is required to create a frame and call the
 * fop. The idea here is to provide a convenient way to call fops than
 * directly use STACK_WINDs. If this type of interface is used, the caller's
 * callback is responsible for doing the relevant GlusterFS state
 * maintenance operations on the data returned in the callbacks.
 *
 * nfs_<fopname>
 * Unlike the nfs_fop_<fopname> variety, this is the stateful type of fop, in
 * that it silently performs all the relevant GlusterFS state maintenance
 * operations on the data returned to the callbacks, leaving the caller's
 * callback to just use the data returned for whatever it needs to do with that
 * data, for eg. the nfs_lookup, will take care of looking up the inodes,
 * revalidating them if needed and linking new inodes into the table, while
 * the caller's callback, for eg, the NFSv3 LOOKUP callback can just use
 * the stat bufs returned to create file handle, map the file handle into the
 * fh cache and finally encode the fh and the stat bufs into a NFS reply.
 *
 */

int32_t
nfs_fop_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xattr, struct iatt *postparent)
{
        struct nfs_fop_local    *local = NULL;
        fop_lookup_cbk_t        progcbk;

        nfl_to_prog_data (local, progcbk, frame);
        nfs_fop_restore_root_ino (local, op_ret, buf, NULL, NULL, postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         xattr, postparent);

        nfs_stack_destroy (local, frame);
        return 0;
}


int
nfs_fop_lookup (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                fop_lookup_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!xl) || (!loc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Lookup: %s", loc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, loc);
        nfs_fop_gfid_setup (nfl, loc->inode, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_lookup_cbk, xl, xl,
                           xl->fops->lookup, loc, nfl->dictgfid);

        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}

int32_t
nfs_fop_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_access_cbk_t        progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno);

        nfs_stack_destroy (nfl, frame);
        return 0;
}

int
nfs_fop_access (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                int32_t accesstest, fop_access_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;
        uint32_t                accessbits = 0;

        if ((!xl) || (!loc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Access: %s", loc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, loc);

        accessbits = nfs3_request_to_accessbits (accesstest);
        STACK_WIND_COOKIE (frame, nfs_fop_access_cbk, xl, xl, xl->fops->access,
                           loc, accessbits);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}

int32_t
nfs_fop_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_stat_cbk_t          progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, buf, NULL, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, buf);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_stat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
              fop_stat_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!xl) || (!loc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Stat: %s", loc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, loc);

        STACK_WIND_COOKIE (frame, nfs_fop_stat_cbk, xl, xl, xl->fops->stat,
                           loc);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_fstat_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, buf, NULL, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, buf);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_fstat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               fop_fstat_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!fd) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "FStat");
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_fd_ino (nfl, fd);

        STACK_WIND_COOKIE (frame, nfs_fop_fstat_cbk, xl, xl, xl->fops->fstat,
                           fd);

        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_opendir_cbk_t       progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, fd);
        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_opendir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 fd_t *dirfd, fop_opendir_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!dirfd) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Opendir: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_opendir_cbk, xl, xl,
                           xl->fops->opendir, pathloc, dirfd);
        ret = 0;

err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}

int
nfs_fop_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_flush_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_flush (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               fop_flush_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!fd) || (!nfu))
                return ret;

        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_flush_cbk, xl, xl, xl->fops->flush,
                           fd);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_readdirp_cbk_t      progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, entries);

        nfs_stack_destroy (nfl, frame);

        return 0;
}


int
nfs_fop_readdirp (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *dirfd,
                  size_t bufsize, off_t offset, fop_readdirp_cbk_t cbk,
                  void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!dirfd) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "readdir");
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_readdirp_cbk, xl, xl,
                           xl->fops->readdirp, dirfd, bufsize, offset);

        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{

        struct nfs_fop_local    *nfl = NULL;
        fop_statfs_cbk_t        progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, buf);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_statfs (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                fop_statfs_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Statfs: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_statfs_cbk, xl, xl,
                           xl->fops->statfs, pathloc);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_create_cbk_t        progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, buf, NULL, preparent,
                                  postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, fd, inode, buf,
                         preparent, postparent);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_create (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                int flags, mode_t mode, fd_t *fd, fop_create_cbk_t cbk,
                void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Create: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);
        nfs_fop_gfid_setup (nfl, pathloc->inode, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_create_cbk, xl, xl, xl->fops->create,
                           pathloc, flags, mode, fd, nfl->dictgfid);

        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *pre,
                     struct iatt *post)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_setattr_cbk_t       progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, pre, post, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, pre, post);
        nfs_stack_destroy (nfl, frame);
        return 0;
}



int
nfs_fop_setattr (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 struct iatt *buf, int32_t valid, fop_setattr_cbk_t cbk,
                 void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Setattr: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);

        STACK_WIND_COOKIE (frame, nfs_fop_setattr_cbk, xl, xl,
                           xl->fops->setattr, pathloc, buf, valid);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_mkdir_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, buf, NULL,preparent, postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);
        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_mkdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               mode_t mode, fop_mkdir_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Mkdir: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);
        nfs_fop_gfid_setup (nfl, pathloc->inode, ret, err);

        STACK_WIND_COOKIE  (frame, nfs_fop_mkdir_cbk, xl, xl, xl->fops->mkdir,
                            pathloc, mode, nfl->dictgfid);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_symlink_cbk_t       progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret,buf, NULL, preparent, postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);
        nfs_stack_destroy (nfl, frame);
        return 0;
}

int
nfs_fop_symlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, char *target,
                 loc_t *pathloc, fop_symlink_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!target) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Symlink: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);
        nfs_fop_gfid_setup (nfl, pathloc->inode, ret, err);

        STACK_WIND_COOKIE  (frame, nfs_fop_symlink_cbk, xl, xl,
                            xl->fops->symlink, target, pathloc, nfl->dictgfid);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, const char *path,
                      struct iatt *buf)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_readlink_cbk_t      progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, buf, NULL, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, path, buf);
        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_readlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                  size_t size, fop_readlink_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Readlink: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);

        STACK_WIND_COOKIE (frame, nfs_fop_readlink_cbk, xl, xl,
                           xl->fops->readlink, pathloc, size);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_mknod_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret,buf, NULL, preparent, postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);
        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_mknod (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               mode_t mode, dev_t dev, fop_mknod_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Mknod: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);
        nfs_fop_gfid_setup (nfl, pathloc->inode, ret, err);

        STACK_WIND_COOKIE  (frame, nfs_fop_mknod_cbk, xl, xl, xl->fops->mknod,
                            pathloc, mode, dev, nfl->dictgfid);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}

int32_t
nfs_fop_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = frame->local;
        fop_rmdir_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, NULL, NULL, preparent,
                                  postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, preparent,
                         postparent);
        nfs_stack_destroy (nfl, frame);
        return 0;
}



int
nfs_fop_rmdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               fop_rmdir_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Rmdir: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);

        STACK_WIND_COOKIE (frame, nfs_fop_rmdir_cbk, xl, xl, xl->fops->rmdir,
                           pathloc, 0);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;

}


int32_t
nfs_fop_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = frame->local;
        fop_unlink_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, NULL, NULL, preparent,
                                  postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, preparent,
                         postparent);
        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_unlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                fop_unlink_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Unlink: %s", pathloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, pathloc);

        STACK_WIND_COOKIE (frame, nfs_fop_unlink_cbk, xl, xl,
                           xl->fops->unlink, pathloc);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;

}


int32_t
nfs_fop_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_link_cbk_t          progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, buf, NULL, preparent,
                                  postparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_link (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
              loc_t *newloc, fop_link_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!oldloc) || (!newloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Link: %s -> %s", newloc->path,
                oldloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, newloc);

        STACK_WIND_COOKIE (frame, nfs_fop_link_cbk, xl, xl, xl->fops->link,
                           oldloc, newloc);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent)
{

        struct nfs_fop_local    *nfl = NULL;
        fop_rename_cbk_t        progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        /* The preattr arg needs to be NULL instead of @buf because it is
         * possible that the new parent is not root whereas the source dir
         * could have been. That is handled in the next macro.
         */
        nfs_fop_restore_root_ino (nfl, op_ret, NULL, NULL, preoldparent,
                                  postoldparent);
        nfs_fop_newloc_restore_root_ino (nfl, op_ret, buf, NULL, prenewparent,
                                         postnewparent);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, buf,
                         preoldparent, postoldparent, prenewparent,
                         postnewparent);
        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_rename (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
                loc_t *newloc, fop_rename_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!oldloc) || (!newloc) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Rename: %s -> %s", oldloc->path,
                newloc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, oldloc);
        nfs_fop_newloc_save_root_ino (nfl, newloc);

        STACK_WIND_COOKIE (frame, nfs_fop_rename_cbk, xl, xl,
                           xl->fops->rename, oldloc, newloc);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_open_cbk_t          progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, fd);
        nfs_stack_destroy (nfl, frame);

        return 0;
}

int
nfs_fop_open (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
              int32_t flags, fd_t *fd, int32_t wbflags, fop_open_cbk_t cbk,
              void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!loc) || (!fd) || (!nfu))
                return ret;

        gf_log (GF_NFS, GF_LOG_TRACE, "Open: %s", loc->path);
        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);

        STACK_WIND_COOKIE (frame, nfs_fop_open_cbk, xl, xl, xl->fops->open,
                           loc, flags, fd, wbflags);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_writev_cbk_t        progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, prebuf, postbuf, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, prebuf,postbuf);

        nfs_stack_destroy (nfl, frame);

        return 0;
}


int
nfs_fop_write (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               struct iobref *srciobref, struct iovec *vector, int32_t count,
               off_t offset, fop_writev_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!fd) || (!vector) || (!nfu) || (!srciobref))
                return ret;

        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_fd_ino (nfl, fd);
/*
        nfl->iobref = iobref_new ();
        if (!nfl->iobref) {
                gf_log (GF_NFS, GF_LOG_ERROR, "iobref creation failed");
                ret = -ENOMEM;
                goto err;
        }

        iobref_add (nfl->iobref, srciob);
*/
        STACK_WIND_COOKIE (frame, nfs_fop_writev_cbk, xl, xl,xl->fops->writev
                           , fd, vector, count, offset, srciobref);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_fsync_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, prebuf, postbuf, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, prebuf,postbuf);
        nfs_stack_destroy (nfl, frame);
        return 0;
}



int
nfs_fop_fsync (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
               int32_t datasync, fop_fsync_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!fd))
                return ret;

        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_fd_ino (nfl, fd);

        STACK_WIND_COOKIE (frame, nfs_fop_fsync_cbk, xl, xl,
                           xl->fops->fsync, fd, datasync);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *stbuf, struct iobref *iobref)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_readv_cbk_t         progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, stbuf, NULL, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, vector, count,
                         stbuf, iobref);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_read (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
              size_t size, off_t offset, fop_readv_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!xl) || (!fd) || (!nfu))
                return ret;

        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_fd_ino (nfl, fd);

        STACK_WIND_COOKIE (frame, nfs_fop_readv_cbk, xl, xl, xl->fops->readv,
                           fd, size, offset);
        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}


int32_t
nfs_fop_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_truncate_cbk_t      progcbk = NULL;

        nfl_to_prog_data (nfl, progcbk, frame);
        nfs_fop_restore_root_ino (nfl, op_ret, prebuf, postbuf, NULL, NULL);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, prebuf,postbuf);

        nfs_stack_destroy (nfl, frame);
        return 0;
}


int
nfs_fop_truncate (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                  off_t offset, fop_truncate_cbk_t cbk, void *local)
{
        call_frame_t            *frame = NULL;
        int                     ret = -EFAULT;
        struct nfs_fop_local    *nfl = NULL;

        if ((!nfsx) || (!xl) || (!loc) || (!nfu))
                return ret;

        nfs_fop_handle_frame_create (frame, nfsx, nfu, ret, err);
        nfs_fop_handle_local_init (frame, nfsx, nfl, cbk, local, ret, err);
        nfs_fop_save_root_ino (nfl, loc);

        STACK_WIND_COOKIE  (frame, nfs_fop_truncate_cbk, xl, xl,
                            xl->fops->truncate, loc, offset);

        ret = 0;
err:
        if (ret < 0) {
                if (frame)
                        nfs_stack_destroy (nfl, frame);
        }

        return ret;
}
