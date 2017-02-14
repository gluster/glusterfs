/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "decompounder.h"
#include "mem-types.h"
#include "compound-fop-utils.h"

void
dc_local_cleanup (dc_local_t *local)
{
        compound_args_cbk_cleanup (local->compound_rsp);
        return;
}

int32_t
dc_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, struct iatt *buf,
             dict_t *xdata)
{

        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (stat, frame, op_ret,
                                             op_errno, buf, xdata);
        return 0;
}

int32_t
dc_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *path,
                 struct iatt *buf, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (readlink, frame, op_ret, op_errno,
                                             path, buf, xdata);
        return 0;
}


int32_t
dc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct iatt *buf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (mknod, frame, op_ret, op_errno,
                                             inode, buf, preparent,
                                             postparent, xdata);
        return 0;
}


int32_t
dc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct iatt *buf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (mkdir, frame, op_ret, op_errno,
                                             inode, buf, preparent, postparent,
                                             xdata);
        return 0;
}


int32_t
dc_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent,
               dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (unlink, frame, op_ret, op_errno,
                                             preparent, postparent, xdata);
        return 0;
}


int32_t
dc_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *preparent,
              struct iatt *postparent,
              dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (rmdir, frame, op_ret, op_errno,
                                             preparent, postparent, xdata);
        return 0;
}


int32_t
dc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (symlink, frame, op_ret, op_errno,
                                             inode, buf, preparent, postparent,
                                             xdata);
        return 0;
}


int32_t
dc_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf,
              struct iatt *preoldparent,
              struct iatt *postoldparent,
              struct iatt *prenewparent,
              struct iatt *postnewparent,
              dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (rename, frame, op_ret, op_errno,
                                             buf, preoldparent, postoldparent,
                                             prenewparent, postnewparent,
                                             xdata);
        return 0;
}


int32_t
dc_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, inode_t *inode,
             struct iatt *buf,
             struct iatt *preparent,
             struct iatt *postparent,
             dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (link, frame, op_ret, op_errno,
                                             inode, buf, preparent, postparent,
                                             xdata);
        return 0;
}


int32_t
dc_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (truncate, frame, op_ret, op_errno,
                                             prebuf, postbuf, xdata);
        return 0;
}


int32_t
dc_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (open, frame, op_ret, op_errno,
                                             fd, xdata);
        return 0;
}


int32_t
dc_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iovec *vector,
              int32_t count,
              struct iatt *stbuf,
              struct iobref *iobref,
              dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (readv, frame, op_ret, op_errno,
                                             vector, count, stbuf, iobref, xdata);
        return 0;
}


int32_t
dc_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
              struct iatt *postbuf,
              dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (writev, frame, op_ret, op_errno,
                                             prebuf, postbuf, xdata);
        return 0;
}


int32_t
dc_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct statvfs *buf,
               dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (statfs, frame, op_ret, op_errno,
                                             buf, xdata);
        return 0;
}


int32_t
dc_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (flush, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
             struct iatt *postbuf,
             dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fsync, frame, op_ret, op_errno,
                                             prebuf, postbuf, xdata);
        return 0;
}


int32_t
dc_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (setxattr, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
                 dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (getxattr, frame, op_ret, op_errno,
                                             dict, xdata);
        return 0;
}


int32_t
dc_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (removexattr, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}

int32_t
dc_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd,
                dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (opendir, frame, op_ret, op_errno,
                                             fd, xdata);
        return 0;
}


int32_t
dc_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fsyncdir, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (access, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd,
               inode_t *inode,
               struct iatt *buf,
               struct iatt *preparent,
               struct iatt *postparent,
               dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (create, frame, op_ret, op_errno,
                                             fd, inode, buf, preparent,
                                             postparent, xdata);
        return 0;
}


int32_t
dc_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (ftruncate, frame, op_ret, op_errno,
                                             prebuf, postbuf, xdata);
        return 0;
}


int32_t
dc_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf,
              dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fstat, frame, op_ret, op_errno,
                                             buf, xdata);
        return 0;
}


int32_t
dc_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
           int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
           dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (lk, frame, op_ret, op_errno,
                                             lock, xdata);
        return 0;
}


int32_t
dc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf,
               dict_t *xdata,
               struct iatt *postparent)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (lookup, frame, op_ret, op_errno,
                                             inode, buf, xdata, postparent);
        return 0;
}


int32_t
dc_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (readdir, frame, op_ret, op_errno,
                                             entries, xdata);
        return 0;
}


int32_t
dc_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (inodelk, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (finodelk, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (entrylk, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fentrylk, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict,
                dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (xattrop, frame, op_ret, op_errno,
                                             dict, xdata);
        return 0;
}


int32_t
dc_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
                 dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fxattrop, frame, op_ret, op_errno,
                                             dict, xdata);
        return 0;
}


int32_t
dc_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict,
        dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fgetxattr, frame, op_ret, op_errno,
                                             dict, xdata);
        return 0;
}


int32_t
dc_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fsetxattr, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, uint32_t weak_cksum,
                  uint8_t *strong_cksum, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (rchecksum, frame, op_ret, op_errno,
                                             weak_cksum, strong_cksum, xdata);
        return 0;
}


int32_t
dc_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                struct iatt *statpost, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (setattr, frame, op_ret, op_errno,
                                             statpre, statpost, xdata);
        return 0;
}


int32_t
dc_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                 struct iatt *statpost, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fsetattr, frame, op_ret, op_errno,
                                             statpre, statpost, xdata);
        return 0;
}


int32_t
dc_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                 dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (readdirp, frame, op_ret, op_errno,
                                             entries, xdata);
        return 0;
}


int32_t
dc_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fremovexattr, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_fallocate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *pre,
                  struct iatt *post, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (fallocate, frame, op_ret, op_errno,
                                             pre, post, xdata);
        return 0;
}


int32_t
dc_discard_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *pre,
                struct iatt *post, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (discard, frame, op_ret, op_errno,
                                             pre, post, xdata);
        return 0;
}


int32_t
dc_zerofill_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *pre,
                 struct iatt *post, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (zerofill, frame, op_ret, op_errno,
                                             pre, post, xdata);
        return 0;
}


int32_t
dc_ipc_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (ipc, frame, op_ret, op_errno,
                                             xdata);
        return 0;
}


int32_t
dc_seek_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, off_t offset, dict_t *xdata)
{
        DC_FOP_RESPONSE_STORE_AND_WIND_NEXT (seek, frame, op_ret, op_errno,
                                             offset, xdata);
        return 0;
}

int32_t
dc_compound_fop_wind (call_frame_t *frame, xlator_t *this)
{
        dc_local_t              *local          = frame->local;
        compound_args_t         *c_req          = local->compound_req;
        compound_args_cbk_t     *c_rsp          = local->compound_rsp;
        int                     counter         = local->counter;
        default_args_t          *curr_fop       = &c_req->req_list[counter];
        int                     op_ret          = 0;
        int                     op_errno        = ENOMEM;

        if (local->counter == local->length)
                goto done;

        c_rsp->enum_list[counter] = c_req->enum_list[counter];

        switch (c_req->enum_list[counter]) {
        case GF_FOP_STAT:
                STACK_WIND (frame, dc_stat_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->stat,
                            &curr_fop->loc, curr_fop->xdata);
                break;
        case GF_FOP_READLINK:
                STACK_WIND (frame, dc_readlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readlink,
                            &curr_fop->loc, curr_fop->size,
                            curr_fop->xdata);
                break;
        case GF_FOP_MKNOD:
                STACK_WIND (frame, dc_mknod_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                            &curr_fop->loc, curr_fop->mode, curr_fop->rdev,
                            curr_fop->umask, curr_fop->xdata);
                break;
        case GF_FOP_MKDIR:
                STACK_WIND (frame, dc_mkdir_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                            &curr_fop->loc, curr_fop->mode,
                            curr_fop->umask, curr_fop->xdata);
                break;
        case GF_FOP_UNLINK:
                STACK_WIND (frame, dc_unlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                            &curr_fop->loc, curr_fop->xflag, curr_fop->xdata);
                break;
        case GF_FOP_RMDIR:
                STACK_WIND (frame, dc_rmdir_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                            &curr_fop->loc, curr_fop->flags, curr_fop->xdata);
                break;
        case GF_FOP_SYMLINK:
                STACK_WIND (frame, dc_symlink_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                            curr_fop->linkname, &curr_fop->loc,
                            curr_fop->umask, curr_fop->xdata);
                break;
        case GF_FOP_RENAME:
                STACK_WIND (frame, dc_rename_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                            &curr_fop->loc, &curr_fop->loc2, curr_fop->xdata);
                break;
        case GF_FOP_LINK:
                STACK_WIND (frame, dc_link_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                            &curr_fop->loc, &curr_fop->loc2, curr_fop->xdata);
                break;
        case GF_FOP_TRUNCATE:
                STACK_WIND (frame, dc_truncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            &curr_fop->loc, curr_fop->offset, curr_fop->xdata);
                break;
        case GF_FOP_OPEN:
                STACK_WIND (frame, dc_open_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                            &curr_fop->loc, curr_fop->flags, curr_fop->fd,
                            curr_fop->xdata);
                break;
        case GF_FOP_READ:
                STACK_WIND (frame, dc_readv_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                            curr_fop->fd, curr_fop->size, curr_fop->offset,
                            curr_fop->flags, curr_fop->xdata);
                break;
        case GF_FOP_WRITE:
                STACK_WIND (frame, dc_writev_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                            curr_fop->fd, curr_fop->vector, curr_fop->count,
                            curr_fop->offset, curr_fop->flags, curr_fop->iobref,
                            curr_fop->xdata);
                break;
        case GF_FOP_STATFS:
                STACK_WIND (frame, dc_statfs_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->statfs,
                            &curr_fop->loc, curr_fop->xdata);
                break;
        case GF_FOP_FLUSH:
                STACK_WIND (frame, dc_flush_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->flush,
                            curr_fop->fd, curr_fop->xdata);
                break;
        case GF_FOP_FSYNC:
                STACK_WIND (frame, dc_fsync_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsync,
                            curr_fop->fd, curr_fop->datasync, curr_fop->xdata);
                break;
        case GF_FOP_SETXATTR:
                STACK_WIND (frame, dc_setxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            &curr_fop->loc, curr_fop->xattr, curr_fop->flags,
                            curr_fop->xdata);
                break;
        case GF_FOP_GETXATTR:
                STACK_WIND (frame, dc_getxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->getxattr,
                            &curr_fop->loc, curr_fop->name, curr_fop->xdata);
                break;
        case GF_FOP_REMOVEXATTR:
                STACK_WIND (frame, dc_removexattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            &curr_fop->loc, curr_fop->name, curr_fop->xdata);
                break;
        case GF_FOP_OPENDIR:
                STACK_WIND (frame, dc_opendir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir,
                            &curr_fop->loc, curr_fop->fd, curr_fop->xdata);
                break;
        case GF_FOP_FSYNCDIR:
                STACK_WIND (frame, dc_fsyncdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsyncdir,
                            curr_fop->fd, curr_fop->datasync, curr_fop->xdata);
                break;
        case GF_FOP_ACCESS:
                STACK_WIND (frame, dc_access_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->access,
                            &curr_fop->loc, curr_fop->mask, curr_fop->xdata);
                break;
        case GF_FOP_CREATE:
                STACK_WIND (frame, dc_create_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                            &curr_fop->loc, curr_fop->flags, curr_fop->mode,
                            curr_fop->umask, curr_fop->fd, curr_fop->xdata);
                break;
        case GF_FOP_FTRUNCATE:
                STACK_WIND (frame, dc_ftruncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            curr_fop->fd, curr_fop->offset, curr_fop->xdata);
                break;
        case GF_FOP_FSTAT:
                STACK_WIND (frame, dc_fstat_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->fstat,
                            curr_fop->fd, curr_fop->xdata);
                break;
        case GF_FOP_LK:
                STACK_WIND (frame, dc_lk_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->lk,
                            curr_fop->fd,
                            curr_fop->cmd, &curr_fop->lock, curr_fop->xdata);
                break;
        case GF_FOP_LOOKUP:
                STACK_WIND (frame, dc_lookup_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup,
                            &curr_fop->loc, curr_fop->xdata);
                break;
        case GF_FOP_READDIR:
                STACK_WIND (frame, dc_readdir_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdir,
                            curr_fop->fd, curr_fop->size, curr_fop->offset,
                            curr_fop->xdata);
                break;
        case GF_FOP_INODELK:
                STACK_WIND (frame, dc_inodelk_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->inodelk,
                            curr_fop->volume, &curr_fop->loc,
                            curr_fop->cmd, &curr_fop->lock, curr_fop->xdata);
                break;
        case GF_FOP_FINODELK:
                STACK_WIND (frame, dc_finodelk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->finodelk,
                            curr_fop->volume, curr_fop->fd,
                            curr_fop->cmd, &curr_fop->lock, curr_fop->xdata);
                break;
        case GF_FOP_ENTRYLK:
                STACK_WIND (frame, dc_entrylk_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->entrylk,
                            curr_fop->volume, &curr_fop->loc,
                            curr_fop->name, curr_fop->entrylkcmd,
                            curr_fop->entrylktype, curr_fop->xdata);
                break;
        case GF_FOP_FENTRYLK:
                STACK_WIND (frame, dc_fentrylk_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fentrylk,
                            curr_fop->volume, curr_fop->fd,
                            curr_fop->name, curr_fop->entrylkcmd,
                            curr_fop->entrylktype, curr_fop->xdata);
                break;
        case GF_FOP_XATTROP:
                STACK_WIND (frame, dc_xattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->xattrop,
                            &curr_fop->loc, curr_fop->optype, curr_fop->xattr,
                            curr_fop->xdata);
                break;
        case GF_FOP_FXATTROP:
                STACK_WIND (frame, dc_fxattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fxattrop,
                            curr_fop->fd, curr_fop->optype, curr_fop->xattr,
                            curr_fop->xdata);
                break;
        case GF_FOP_FGETXATTR:
                STACK_WIND (frame, dc_fgetxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fgetxattr,
                            curr_fop->fd, curr_fop->name, curr_fop->xdata);
                break;
        case GF_FOP_FSETXATTR:
                STACK_WIND (frame, dc_fsetxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            curr_fop->fd, curr_fop->xattr, curr_fop->flags,
                            curr_fop->xdata);
                break;
        case GF_FOP_RCHECKSUM:
                STACK_WIND (frame, dc_rchecksum_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rchecksum,
                            curr_fop->fd, curr_fop->offset, curr_fop->size,
                            curr_fop->xdata);
                break;
        case GF_FOP_SETATTR:
                STACK_WIND (frame, dc_setattr_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->setattr,
                            &curr_fop->loc, &curr_fop->stat, curr_fop->valid,
                            curr_fop->xdata);
                break;
        case GF_FOP_FSETATTR:
                STACK_WIND (frame, dc_fsetattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetattr,
                            curr_fop->fd, &curr_fop->stat, curr_fop->valid,
                            curr_fop->xdata);
                break;
        case GF_FOP_READDIRP:
                STACK_WIND (frame, dc_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            curr_fop->fd, curr_fop->size, curr_fop->offset,
                            curr_fop->xdata);
                break;
        case GF_FOP_FREMOVEXATTR:
                STACK_WIND (frame, dc_fremovexattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fremovexattr,
                            curr_fop->fd, curr_fop->name, curr_fop->xdata);
                break;
        case GF_FOP_FALLOCATE:
                STACK_WIND (frame, dc_fallocate_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->fallocate,
                            curr_fop->fd, curr_fop->flags, curr_fop->offset,
                            curr_fop->size, curr_fop->xdata);
                break;
        case GF_FOP_DISCARD:
                STACK_WIND (frame, dc_discard_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->discard,
                            curr_fop->fd, curr_fop->offset, curr_fop->size,
                            curr_fop->xdata);
                break;
        case GF_FOP_ZEROFILL:
                STACK_WIND (frame, dc_zerofill_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->zerofill,
                            curr_fop->fd, curr_fop->offset, curr_fop->size,
                            curr_fop->xdata);
                break;
        case GF_FOP_IPC:
                STACK_WIND (frame, dc_ipc_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->ipc,
                            curr_fop->cmd, curr_fop->xdata);
                break;
        case GF_FOP_SEEK:
                STACK_WIND (frame, dc_seek_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->seek,
                            curr_fop->fd, curr_fop->offset, curr_fop->what,
                            curr_fop->xdata);
                break;
        default:
                return -ENOTSUP;
        }
        return 0;
done:
        DC_STACK_UNWIND (frame, op_ret, op_errno, c_rsp, NULL);
        return 0;
}

int32_t
dc_compound (call_frame_t *frame, xlator_t *this, void *data, dict_t *xdata)
{
        compound_args_t         *compound_req = NULL;
        compound_args_cbk_t     *compound_rsp = NULL;
        int                     ret           = 0;
        int                     op_errno      = ENOMEM;
        dc_local_t              *local        = NULL;

        compound_req = data;

        GF_ASSERT_AND_GOTO_WITH_ERROR (this, compound_req, out, op_errno,
                                       EINVAL);

        local = mem_get0 (this->local_pool);
        if (!local)
                goto out;

        frame->local = local;

        local->compound_rsp = compound_args_cbk_alloc (compound_req->fop_length,
                                                       NULL);
        if (!local->compound_rsp)
                goto out;

        compound_rsp = local->compound_rsp;

        local->length =  compound_req->fop_length;
        local->counter = 0;
        local->compound_req = compound_req;

        if (!local->length) {
                op_errno = EINVAL;
                goto out;
        }

        ret = dc_compound_fop_wind (frame, this);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }
        return 0;
out:
        DC_STACK_UNWIND (frame, -1, op_errno, compound_rsp, NULL);
        return 0;
}

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {NULL} },
};

struct xlator_fops fops = {
        .compound = dc_compound,
};

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_dc_mt_end + 1);

        return ret;
}

int32_t
init (xlator_t *this)
{
        int     ret = 0;

        if (!this->children) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DC_MSG_VOL_MISCONFIGURED, "Decompounder must have"
                        " a subvol.");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DC_MSG_VOL_MISCONFIGURED, "Volume is dangling.");
                ret = -1;
                goto out;
        }

        this->local_pool = mem_pool_new (dc_local_t, 128);
        if (!this->local_pool) {
                ret = -1;
                goto out;
        }

out:
        return ret;
}

int32_t
fini (xlator_t *this)
{
        return 0;
}
