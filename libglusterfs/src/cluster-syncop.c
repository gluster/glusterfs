/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* Perform fop on all subvolumes represented by list[] array and wait
   for all callbacks to return */

/* NOTE: Cluster-syncop, like syncop blocks the executing thread until the
 * responses are gathered if it is not executed as part of synctask. So it
 * shouldn't be invoked in epoll worker thread */
#include "cluster-syncop.h"
#include "defaults.h"

#define FOP_ONLIST(subvols, on, numsubvols, replies, output, frame, fop, args ...) do {\
        int __i = 0;                                                    \
        int __count = 0;					        \
        cluster_local_t __local = {0,};                                 \
        void    *__old_local = frame->local;                            \
                                                                        \
        __local.replies = replies;                                      \
        memset (output, 0, numsubvols);                                 \
        cluster_replies_wipe (replies, numsubvols);                     \
        for (__i = 0; __i < numsubvols; __i++)                          \
                INIT_LIST_HEAD (&replies[__i].entries.list);            \
        if (syncbarrier_init (&__local.barrier))                        \
                break;                                                  \
        frame->local = &__local;                                        \
        for (__i = 0; __i < numsubvols; __i++) {		        \
                if (!on[__i])                                           \
                        continue;				        \
                STACK_WIND_COOKIE (frame, cluster_##fop##_cbk,          \
                                   (void *)(long) __i, subvols[__i],    \
                                   subvols[__i]->fops->fop, args);      \
                __count++;						\
        }								\
        syncbarrier_wait (&__local.barrier, __count);			\
        syncbarrier_destroy (&__local.barrier);                         \
        frame->local = __old_local;                                     \
        STACK_RESET (frame->root);                                      \
        } while (0)

#define FOP_SEQ(subvols, on, numsubvols, replies, output, frame, fop, args ...) do {\
        int __i = 0;					                \
                                                                        \
        cluster_local_t __local = {0,};                                 \
        void    *__old_local = frame->local;                            \
        __local.replies = replies;                                      \
        memset (output, 0, numsubvols);                                 \
        cluster_replies_wipe (replies, numsubvols);                     \
        for (__i = 0; __i < numsubvols; __i++)                          \
                INIT_LIST_HEAD (&replies[__i].entries.list);            \
        if (syncbarrier_init (&__local.barrier))                        \
                break;                                                  \
        frame->local = &__local;                                        \
        for (__i = 0; __i < numsubvols; __i++) {		        \
                if (!on[__i])                                           \
                        continue;				        \
                STACK_WIND_COOKIE (frame, cluster_##fop##_cbk,          \
                                   (void *)(long) __i, subvols[__i],    \
                                   subvols[__i]->fops->fop, args);      \
                syncbarrier_wait (&__local.barrier, 1);                 \
        }                                                               \
        syncbarrier_destroy (&__local.barrier);                         \
        frame->local = __old_local;                                     \
        STACK_RESET (frame->root);                                      \
        } while (0)

#define FOP_CBK(fop, frame, cookie, args ...) do {\
        cluster_local_t *__local = frame->local;                        \
        int __i = (long)cookie;                                         \
        args_##fop##_cbk_store (&__local->replies[__i], args);          \
        __local->replies[__i].valid = 1;                                \
        syncbarrier_wake (&__local->barrier);                           \
        } while (0)

static int
fop_success_fill (default_args_cbk_t *replies, int numsubvols,
                  unsigned char *success)
{
        int i = 0;
        int count = 0;

        for (i = 0; i < numsubvols; i++) {
                if (replies[i].valid && replies[i].op_ret >= 0) {
                        success[i] = 1;
                        count++;
                } else {
                        success[i] = 0;
                }
        }

        return count;
}

void
cluster_replies_wipe (default_args_cbk_t *replies, int numsubvols)
{
        int i = 0;
        for (i = 0; i < numsubvols; i++)
                args_cbk_wipe (&replies[i]);
        memset (replies, 0, numsubvols * sizeof (*replies));
}

int32_t
cluster_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        FOP_CBK (lookup, frame, cookie, op_ret, op_errno, inode, buf,
                 xdata, postparent);
        return 0;
}

int32_t
cluster_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  dict_t *xdata)
{
        FOP_CBK (stat, frame, cookie, op_ret, op_errno, buf, xdata);
        return 0;
}


int32_t
cluster_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf,
                      dict_t *xdata)
{
        FOP_CBK (truncate, frame, cookie, op_ret, op_errno, prebuf,
                 postbuf, xdata);
        return 0;
}

int32_t
cluster_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf,
                       dict_t *xdata)
{
        FOP_CBK (ftruncate, frame, cookie, op_ret, op_errno, prebuf,
                 postbuf, xdata);
        return 0;
}

int32_t
cluster_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    dict_t *xdata)
{
        FOP_CBK (access, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}

int32_t
cluster_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, const char *path,
                      struct iatt *buf, dict_t *xdata)
{
        FOP_CBK (readlink, frame, cookie, op_ret, op_errno, path, buf,
                 xdata);
        return 0;
}


int32_t
cluster_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        FOP_CBK (mknod, frame, cookie, op_ret, op_errno, inode,
                 buf, preparent, postparent, xdata);
        return 0;
}

int32_t
cluster_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        FOP_CBK (mkdir, frame, cookie, op_ret, op_errno, inode,
                 buf, preparent, postparent, xdata);
        return 0;
}

int32_t
cluster_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        FOP_CBK (unlink, frame, cookie, op_ret, op_errno, preparent,
                 postparent, xdata);
        return 0;
}

int32_t
cluster_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent,
                   dict_t *xdata)
{
        FOP_CBK (rmdir, frame, cookie, op_ret, op_errno, preparent,
                 postparent, xdata);
        return 0;
}


int32_t
cluster_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        FOP_CBK (symlink, frame, cookie, op_ret, op_errno, inode, buf,
                 preparent, postparent, xdata);
        return 0;
}


int32_t
cluster_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        FOP_CBK (rename, frame, cookie, op_ret, op_errno, buf, preoldparent,
                 postoldparent, prenewparent, postnewparent, xdata);
        return 0;
}


int32_t
cluster_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent,
                  dict_t *xdata)
{
        FOP_CBK (link, frame, cookie, op_ret, op_errno, inode, buf,
                 preparent, postparent, xdata);
        return 0;
}


int32_t
cluster_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent,
                    dict_t *xdata)
{
        FOP_CBK (create, frame, cookie, op_ret, op_errno, fd, inode, buf,
                 preparent, postparent, xdata);
        return 0;
}

int32_t
cluster_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd,
                  dict_t *xdata)
{
        FOP_CBK (open, frame, cookie, op_ret, op_errno, fd, xdata);
        return 0;
}

int32_t
cluster_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *stbuf, struct iobref *iobref,
                   dict_t *xdata)
{
        FOP_CBK (readv, frame, cookie, op_ret, op_errno, vector, count,
                 stbuf, iobref, xdata);
        return 0;
}


int32_t
cluster_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf,
                    dict_t *xdata)
{
        FOP_CBK (writev, frame, cookie, op_ret, op_errno, prebuf, postbuf,
                 xdata);
        return 0;
}


int32_t
cluster_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   dict_t *xdata)
{
        FOP_CBK (flush, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}



int32_t
cluster_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf,
                   dict_t *xdata)
{
        FOP_CBK (fsync, frame, cookie, op_ret, op_errno, prebuf, postbuf,
                 xdata);
        return 0;
}

int32_t
cluster_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   dict_t *xdata)
{
        FOP_CBK (fstat, frame, cookie, op_ret, op_errno, buf, xdata);
        return 0;
}

int32_t
cluster_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd,
                     dict_t *xdata)
{
        FOP_CBK (opendir, frame, cookie, op_ret, op_errno, fd, xdata);
        return 0;
}

int32_t
cluster_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      dict_t *xdata)
{
        FOP_CBK (fsyncdir, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}

int32_t
cluster_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                    dict_t *xdata)
{
        FOP_CBK (statfs, frame, cookie, op_ret, op_errno, buf, xdata);
        return 0;
}


int32_t
cluster_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      dict_t *xdata)
{
        FOP_CBK (setxattr, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}


int32_t
cluster_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *xdata)
{
        FOP_CBK (fsetxattr, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}



int32_t
cluster_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict,
                       dict_t *xdata)
{
        FOP_CBK (fgetxattr, frame, cookie, op_ret, op_errno, dict, xdata);
        return 0;
}


int32_t
cluster_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
        FOP_CBK (getxattr, frame, cookie, op_ret, op_errno, dict, xdata);
        return 0;
}

int32_t
cluster_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        FOP_CBK (xattrop, frame, cookie, op_ret, op_errno, dict, xdata);
        return 0;
}

int32_t
cluster_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
        FOP_CBK (fxattrop, frame, cookie, op_ret, op_errno, dict, xdata);
        return 0;
}


int32_t
cluster_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno,
                         dict_t *xdata)
{
        FOP_CBK (removexattr, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}


int32_t
cluster_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          dict_t *xdata)
{
        FOP_CBK (fremovexattr, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}

int32_t
cluster_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                dict_t *xdata)
{
        FOP_CBK (lk, frame, cookie, op_ret, op_errno, lock, xdata);
        return 0;
}

int32_t
cluster_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     dict_t *xdata)
{
        FOP_CBK (inodelk, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}


int32_t
cluster_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      dict_t *xdata)
{
        FOP_CBK (finodelk, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}

int32_t
cluster_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     dict_t *xdata)
{
        FOP_CBK (entrylk, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}

int32_t
cluster_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      dict_t *xdata)
{
        FOP_CBK (fentrylk, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}


int32_t
cluster_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, uint32_t weak_checksum,
                       uint8_t *strong_checksum,
                       dict_t *xdata)
{
        FOP_CBK (rchecksum, frame, cookie, op_ret, op_errno, weak_checksum,
                             strong_checksum, xdata);
        return 0;
}


int32_t
cluster_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                     dict_t *xdata)
{
        FOP_CBK (readdir, frame, cookie, op_ret, op_errno, entries, xdata);
        return 0;
}


int32_t
cluster_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                      dict_t *xdata)
{
        FOP_CBK (readdirp, frame, cookie, op_ret, op_errno, entries, xdata);
        return 0;
}

int32_t
cluster_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost,
                     dict_t *xdata)
{
        FOP_CBK (setattr, frame, cookie, op_ret, op_errno, statpre,
                 statpost, xdata);
        return 0;
}

int32_t
cluster_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                      struct iatt *statpost,
                      dict_t *xdata)
{
        FOP_CBK (fsetattr, frame, cookie, op_ret, op_errno, statpre,
                 statpost, xdata);
        return 0;
}

int32_t
cluster_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *pre,
                      struct iatt *post, dict_t *xdata)
{
        FOP_CBK (fallocate, frame, cookie, op_ret, op_errno, pre, post, xdata);
        return 0;
}

int32_t
cluster_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *pre,
                    struct iatt *post, dict_t *xdata)
{
        FOP_CBK (discard, frame, cookie, op_ret, op_errno, pre, post, xdata);
        return 0;
}

int32_t
cluster_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *pre,
                    struct iatt *post, dict_t *xdata)
{
        FOP_CBK (zerofill, frame, cookie, op_ret, op_errno, pre,
                 post, xdata);
        return 0;
}


int32_t
cluster_ipc_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        FOP_CBK (ipc, frame, cookie, op_ret, op_errno, xdata);
        return 0;
}

int32_t
cluster_fgetxattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fgetxattr, fd,
                    name, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fsetxattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                   int32_t flags, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fsetxattr, fd,
                    dict, flags, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_setxattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                  int32_t flags, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, setxattr, loc,
                    dict, flags, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_statfs (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, statfs, loc,
                    xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fsyncdir (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
                  dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fsyncdir, fd,
                    flags, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_opendir (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
                 dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, opendir, loc,
                    fd, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fstat (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fstat, fd,
                    xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fsync (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
               dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fsync, fd,
                    flags, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_flush (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, flush, fd,
                    xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_writev (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int32_t count, off_t off, uint32_t flags,
                struct iobref *iobref, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, writev, fd,
                    vector, count, off, flags, iobref, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_readv (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, uint32_t flags, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, readv, fd, size,
                    offset, flags, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_open (xlator_t **subvols, unsigned char *on, int numsubvols,
              default_args_cbk_t *replies, unsigned char *output,
              call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              fd_t *fd, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, open, loc,
                    flags, fd, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_create (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, create, loc,
                    flags, mode, umask, fd, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_link (xlator_t **subvols, unsigned char *on, int numsubvols,
              default_args_cbk_t *replies, unsigned char *output,
              call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, link, oldloc,
                    newloc, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_rename (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, rename, oldloc,
                    newloc, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int
cluster_symlink (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, const char *linkpath,
                 loc_t *loc, mode_t umask, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, symlink,
                    linkpath, loc, umask, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_rmdir (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
               dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, rmdir, loc,
                    flags, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_unlink (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, unlink, loc,
                    xflag, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int
cluster_mkdir (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               mode_t umask, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, mkdir, loc,
                    mode, umask, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int
cluster_mknod (xlator_t **subvols, unsigned char *on, int numsubvols,
               default_args_cbk_t *replies, unsigned char *output,
               call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               dev_t rdev, mode_t umask, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, mknod, loc,
                    mode, rdev, umask, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_readlink (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                  dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, readlink, loc,
                    size, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_access (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
                dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, access, loc,
                    mask, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_ftruncate (xlator_t **subvols, unsigned char *on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, ftruncate, fd,
                    offset, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_getxattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, getxattr, loc,
                    name, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_xattrop (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, loc_t *loc,
                 gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, xattrop, loc,
                    flags, dict, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fxattrop (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, fd_t *fd,
                  gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fxattrop, fd,
                    flags, dict, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_removexattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                     default_args_cbk_t *replies, unsigned char *output,
                     call_frame_t *frame, xlator_t *this, loc_t *loc,
                     const char *name, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, removexattr,
                    loc, name, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fremovexattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                      default_args_cbk_t *replies, unsigned char *output,
                      call_frame_t *frame, xlator_t *this, fd_t *fd,
                      const char *name, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fremovexattr,
                    fd, name, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_lk (xlator_t **subvols, unsigned char *on, int numsubvols,
            default_args_cbk_t *replies, unsigned char *output,
            call_frame_t *frame, xlator_t *this, fd_t *fd,
            int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, lk, fd, cmd,
                    lock, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_rchecksum (xlator_t **subvols, unsigned char *on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   int32_t len, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, rchecksum, fd,
                    offset, len, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_readdir (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t off, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, readdir, fd,
                    size, off, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_readdirp (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, fd_t *fd,
                  size_t size, off_t off, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, readdirp, fd,
                    size, off, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_setattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, loc_t *loc,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, setattr, loc,
                    stbuf, valid, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_truncate (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                  dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, truncate, loc,
                    offset, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_stat (xlator_t **subvols, unsigned char *on, int numsubvols,
              default_args_cbk_t *replies, unsigned char *output,
              call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, stat, loc,
                    xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_lookup (xlator_t **subvols, unsigned char *on, int numsubvols,
                default_args_cbk_t *replies, unsigned char *output,
                call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, lookup, loc,
                    xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fsetattr (xlator_t **subvols, unsigned char *on, int numsubvols,
                  default_args_cbk_t *replies, unsigned char *output,
                  call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, fsetattr, fd,
                    stbuf, valid, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_fallocate (xlator_t **subvols, unsigned char *on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, fd_t *fd,
                   int32_t keep_size, off_t offset, size_t len, dict_t *xdata)
{
        FOP_ONLIST(subvols, on, numsubvols, replies, output, frame, fallocate, fd,
                   keep_size, offset, len, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_discard (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, fd_t *fd,
                 off_t offset, size_t len, dict_t *xdata)
{
        FOP_ONLIST(subvols, on, numsubvols, replies, output, frame, discard, fd,
                   offset, len, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int32_t
cluster_zerofill(xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *output,
                 call_frame_t *frame, xlator_t *this, fd_t *fd,
                 off_t offset, off_t len, dict_t *xdata)
{
        FOP_ONLIST(subvols, on, numsubvols, replies, output, frame, zerofill, fd,
                   offset, len, xdata);
        return fop_success_fill (replies, numsubvols, output);
}


int32_t
cluster_ipc (xlator_t **subvols, unsigned char *on, int numsubvols,
             default_args_cbk_t *replies, unsigned char *output,
             call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        FOP_ONLIST (subvols, on, numsubvols, replies, output, frame, ipc, op, xdata);
        return fop_success_fill (replies, numsubvols, output);
}

int
cluster_uninodelk (xlator_t **subvols, unsigned char *locked_on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, char *dom,
                   inode_t *inode, off_t off, size_t size)
{
        loc_t loc = {0,};
        struct gf_flock flock = {0, };


        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);

        flock.l_type = F_UNLCK;
        flock.l_start = off;
        flock.l_len = size;

        FOP_ONLIST (subvols, locked_on, numsubvols, replies, output, frame, inodelk,
                    dom, &loc, F_SETLK, &flock, NULL);

        loc_wipe (&loc);

        return fop_success_fill (replies, numsubvols, output);
}

int
cluster_tryinodelk (xlator_t **subvols, unsigned char *on, int numsubvols,
                    default_args_cbk_t *replies, unsigned char *locked_on,
                    call_frame_t *frame, xlator_t *this, char *dom,
                    inode_t *inode, off_t off, size_t size)
{
        struct gf_flock flock = {0, };
        loc_t loc = {0};

        flock.l_type = F_WRLCK;
        flock.l_start = off;
        flock.l_len = size;

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        FOP_ONLIST (subvols, on, numsubvols, replies, locked_on, frame, inodelk, dom,
                    &loc, F_SETLK, &flock, NULL);

        loc_wipe (&loc);
        return fop_success_fill (replies, numsubvols, locked_on);
}

int
cluster_inodelk (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *locked_on,
                 call_frame_t *frame, xlator_t *this, char *dom,
                 inode_t *inode, off_t off, size_t size)
{
        struct gf_flock flock = {0, };
        int i = 0;
        loc_t loc = {0};
        unsigned char *output = NULL;

        flock.l_type = F_WRLCK;
        flock.l_start = off;
        flock.l_len = size;

        output = alloca(numsubvols);
        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        FOP_ONLIST (subvols, on, numsubvols, replies, locked_on, frame,
                    inodelk, dom, &loc, F_SETLK, &flock, NULL);

        for (i = 0; i < numsubvols; i++) {
                if (replies[i].op_ret == -1 && replies[i].op_errno == EAGAIN) {
                        fop_success_fill (replies, numsubvols, locked_on);
                        cluster_uninodelk (subvols, locked_on, numsubvols,
                                           replies, output, frame, this, dom, inode, off, size);

                        FOP_SEQ (subvols, on, numsubvols, replies, locked_on,
                                 frame, inodelk, dom, &loc, F_SETLKW, &flock,
                                 NULL);
                        break;
                }
        }

        loc_wipe (&loc);
        return fop_success_fill (replies, numsubvols, locked_on);
}


int
cluster_unentrylk (xlator_t **subvols, unsigned char *locked_on, int numsubvols,
                   default_args_cbk_t *replies, unsigned char *output,
                   call_frame_t *frame, xlator_t *this, char *dom,
                   inode_t *inode, const char *name)
{
        loc_t loc = {0,};


        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);

        FOP_ONLIST (subvols, locked_on, numsubvols, replies, output, frame,
                    entrylk, dom, &loc, name, ENTRYLK_UNLOCK, ENTRYLK_WRLCK,
                    NULL);

        loc_wipe (&loc);

        return fop_success_fill (replies, numsubvols, output);
}

int
cluster_tryentrylk (xlator_t **subvols, unsigned char *on, int numsubvols,
                    default_args_cbk_t *replies, unsigned char *locked_on,
                    call_frame_t *frame, xlator_t *this, char *dom,
                    inode_t *inode, const char *name)
{
        loc_t loc = {0};

        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        FOP_ONLIST (subvols, on, numsubvols, replies, locked_on, frame,
                    entrylk, dom, &loc, name, ENTRYLK_LOCK_NB, ENTRYLK_WRLCK,
                    NULL);

        loc_wipe (&loc);
        return fop_success_fill (replies, numsubvols, locked_on);
}

int
cluster_entrylk (xlator_t **subvols, unsigned char *on, int numsubvols,
                 default_args_cbk_t *replies, unsigned char *locked_on,
                 call_frame_t *frame, xlator_t *this, char *dom,
                 inode_t *inode, const char *name)
{
        int i = 0;
        loc_t loc = {0};
        unsigned char *output = NULL;

        output = alloca(numsubvols);
        loc.inode = inode_ref (inode);
        gf_uuid_copy (loc.gfid, inode->gfid);
        FOP_ONLIST (subvols, on, numsubvols, replies, locked_on, frame,
                    entrylk, dom, &loc, name, ENTRYLK_LOCK_NB, ENTRYLK_WRLCK,
                    NULL);

        for (i = 0; i < numsubvols; i++) {
                if (replies[i].op_ret == -1 && replies[i].op_errno == EAGAIN) {
                        fop_success_fill (replies, numsubvols, locked_on);
                        cluster_unentrylk (subvols, locked_on, numsubvols,
                                           replies, output, frame, this, dom,
                                           inode, name);
                        FOP_SEQ (subvols, on, numsubvols, replies,
                                 locked_on, frame, entrylk, dom, &loc, name,
                                 ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);
                        break;
                }
        }

        loc_wipe (&loc);
        return fop_success_fill (replies, numsubvols, locked_on);
}
