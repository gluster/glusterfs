/*
   Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "leases.h"

int32_t
leases_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int32_t
leases_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
             fd_t *fd, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = 0;
        int              ret             = 0;
        lease_fd_ctx_t  *fd_ctx          = NULL;
        char            *lease_id        = NULL;

        EXIT_IF_LEASES_OFF (this, out);

        fd_ctx = GF_CALLOC (1, sizeof (*fd_ctx), gf_leases_mt_fd_ctx_t);

        fd_ctx->client_uid = gf_strdup (frame->root->client->client_uid);
        if (!fd_ctx->client_uid) {
                op_errno = ENOMEM;
                goto err;
        }

        GET_FLAGS (frame->root->op, flags);
        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        if (lease_id != NULL)
                memcpy (fd_ctx->lease_id, lease_id, LEASE_ID_SIZE);
        else
                memset (fd_ctx->lease_id, 0, LEASE_ID_SIZE);

        ret = fd_ctx_set (fd, this, (uint64_t)fd_ctx);
        if (ret) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, open, frame, this,
                         loc, flags, fd, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
leases_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);

        return 0;
}


int32_t
leases_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int count, off_t off, uint32_t flags,
               struct iobref *iobref, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, writev, frame, this, fd, vector, count,
                         off, flags, iobref, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, off, flags, iobref, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
leases_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno,
                  struct iovec *vector, int count, struct iatt *stbuf,
                  struct iobref *iobref, dict_t *xdata)
{
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector,
                             count, stbuf, iobref, xdata);

        return 0;
}

int32_t
leases_readv (call_frame_t *frame, xlator_t *this,
              fd_t *fd, size_t size, off_t offset,
              uint32_t flags, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, readv, frame, this,
                         fd, size, offset, flags, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0,
                             NULL, NULL, NULL);
        return 0;
}

int32_t
leases_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
               dict_t *xdata)
{
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock, xdata);

        return 0;
}

int32_t
leases_lk (call_frame_t *frame, xlator_t *this,
           fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        int32_t         op_errno         = 0;
        uint32_t        fop_flags        = 0;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS_LK (cmd, flock->l_type, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, lk, frame, this,
                         fd, cmd, flock, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_lk_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, flock, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (lk, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
leases_lease (call_frame_t *frame, xlator_t *this,
             loc_t *loc, struct gf_lease *lease, dict_t *xdata)
{
        int32_t         op_errno         = 0;
        int             ret              = 0;
        struct gf_lease nullease         = {0, };
        int32_t         op_ret           = 0;

        EXIT_IF_LEASES_OFF (this, out);

        ret = process_lease_req (frame, this, loc->inode, lease);
        if (ret < 0) {
                op_errno = -ret;
                op_ret = -1;
        }
        goto unwind;

out:
        gf_msg (this->name, GF_LOG_ERROR, EINVAL, LEASE_MSG_NOT_ENABLED,
                "\"features/leases\" translator is not enabled. "
                "You need to enable it for proper functioning of your "
                "application");
        op_errno = ENOSYS;
        op_ret = -1;

unwind:
        STACK_UNWIND_STRICT (lease, frame, op_ret, op_errno,
                             (op_errno == ENOSYS) ? &nullease : lease, xdata);
        return 0;
}

int32_t
leases_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);

        return 0;
}

int32_t
leases_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                 dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, 0);

        ret = check_lease_conflict (frame, loc->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (loc->inode, truncate, frame, this,
                         loc, offset, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    loc, offset, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
leases_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, struct iatt *statpre,
                    struct iatt *statpost, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno,
                             statpre, statpost, xdata);

        return 0;
}

int32_t
leases_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, 0);

        ret = check_lease_conflict (frame, loc->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (loc->inode, setattr, frame, this,
                         loc, stbuf, valid, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_setattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
leases_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
{
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno,
                             stbuf, preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);

        return 0;
}

int32_t
leases_rename (call_frame_t *frame, xlator_t *this,
               loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        /* should the lease be also checked for newloc */
        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, 0);

        ret = check_lease_conflict (frame, oldloc->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (oldloc->inode, rename, frame, this,
                         oldloc, newloc, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
leases_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        return 0;
}

int32_t
leases_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
               dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, 0);

        ret = check_lease_conflict (frame, loc->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (loc->inode, unlink, frame, this,
                         loc, xflag, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
leases_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
                 struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int32_t
leases_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
             loc_t *newloc, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, 0);

        ret = check_lease_conflict (frame, oldloc->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (oldloc->inode, link, frame, this,
                         oldloc, newloc, xdata);
        return 0;
out:
        STACK_WIND (frame, leases_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
leases_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int32_t
leases_create (call_frame_t *frame, xlator_t *this,
               loc_t *loc, int32_t flags, mode_t mode,
               mode_t umask, fd_t *fd, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, create, frame, this,
                         loc, flags, mode, umask, fd, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, NULL);
        return 0;
}

int32_t
leases_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf,
                  dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}

int32_t
leases_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
              int32_t flags, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, fsync, frame, this,
                         fd, flags, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_fsync_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, flags, xdata);
        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
leases_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf,
                      dict_t *xdata)
{
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}

int32_t
leases_ftruncate (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, off_t offset, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, 0); /* TODO:fd->flags?*/

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, ftruncate, frame, this,
                         fd, offset, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_ftruncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (ftruncate, frame, -1, op_errno, NULL,
                             NULL, NULL);
        return 0;
}

int32_t
leases_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                             statpre, statpost, xdata);
        return 0;
}

int32_t
leases_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt  *stbuf, int32_t valid, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, fsetattr, frame, this,
                         fd, stbuf, valid, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_fsetattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (fsetattr, frame, -1, op_errno, NULL,
                             NULL, NULL);
        return 0;
}

int32_t
leases_fallocate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *pre,
                      struct iatt *post, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fallocate, frame, op_ret, op_errno, pre,
                             post, xdata);

        return 0;
}

int32_t
leases_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  int32_t mode, off_t offset, size_t len, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, fallocate, frame, this,
                         fd, mode, offset, len, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_fallocate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fallocate,
                    fd, mode, offset, len, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (fallocate, frame, -1, op_errno, NULL,
                             NULL, NULL);
        return 0;
}

int32_t
leases_discard_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *pre,
                    struct iatt *post, dict_t *xdata)
{
        STACK_UNWIND_STRICT (discard, frame, op_ret, op_errno, pre,
                             post, xdata);

        return 0;
}

int32_t
leases_discard (call_frame_t *frame, xlator_t *this, fd_t *fd,
                off_t offset, size_t len, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, discard, frame, this,
                         fd, offset, len, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_discard_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->discard,
                    fd, offset, len, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (discard, frame, -1, op_errno, NULL,
                             NULL, NULL);
        return 0;
}

int32_t
leases_zerofill_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *pre,
                     struct iatt *post, dict_t *xdata)
{
        STACK_UNWIND_STRICT (zerofill, frame, op_ret, op_errno, pre,
                             post, xdata);

        return 0;
}

int
leases_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 off_t offset, off_t len, dict_t *xdata)
{
        uint32_t         fop_flags       = 0;
        int32_t          op_errno        = -1;
        char            *lease_id        = NULL;
        int              ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, zerofill, frame, this,
                         fd, offset, len, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_zerofill_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->zerofill,
                    fd, offset, len, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (zerofill, frame, -1, op_errno, NULL,
                             NULL, NULL);
        return 0;
}

int
leases_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);

        return 0;
}

int
leases_flush (call_frame_t *frame, xlator_t *this,
              fd_t *fd, dict_t *xdata)
{
        int32_t       op_errno       = -1;
        uint32_t      fop_flags      = 0;
        char         *lease_id       = NULL;
        int          ret             = 0;

        EXIT_IF_LEASES_OFF (this, out);

        GET_LEASE_ID (xdata, lease_id, frame->root->client->client_uid);
        GET_FLAGS (frame->root->op, fd->flags);

        ret = check_lease_conflict (frame, fd->inode, lease_id, fop_flags);
        if (ret < 0)
                goto err;
        else if (ret == BLOCK_FOP)
                goto block;
        else if (ret == WIND_FOP)
                goto out;

block:
        LEASE_BLOCK_FOP (fd->inode, flush, frame, this,
                         fd, xdata);
        return 0;

out:
        STACK_WIND (frame, leases_flush_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush, fd, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_leases_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM, LEASE_MSG_NO_MEM,
                        "mem account init failed");
                return ret;
        }

        return ret;
}

static int
leases_init_priv (xlator_t *this)
{
        int               ret  = 0;
        leases_private_t *priv = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        if (!priv->timer_wheel) {
                if (!glusterfs_global_timer_wheel (this)) {
                        gf_msg_debug (this->name, 0, "Initing the global "
                                      "timer wheel");
                        ret = glusterfs_global_timer_wheel_init (this->ctx);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        LEASE_MSG_NO_TIMER_WHEEL,
                                        "Initing the global timer "
                                        "wheel failed");
                                goto out;
                        }
                }
                priv->timer_wheel = glusterfs_global_timer_wheel (this);
        }

        if (!priv->inited_recall_thr) {
                pthread_create (&priv->recall_thr, NULL,
                                expired_recall_cleanup, this);
                priv->inited_recall_thr = _gf_true;
        }

out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        leases_private_t *priv                   = NULL;
        int               ret                    = -1;

        priv = this->private;
        GF_ASSERT (priv);

        /* TODO: In case of reconfigure, if its enabling the leases
         * its not an issue, but if its disabling the leases, there
         * is more to it, like recall all the existing leases, wait
         * for unlock of all the leases etc., hence not supporting the
         * reconfigure for now.

        GF_OPTION_RECONF ("leases", priv->leases_enabled,
                          options, bool, out);

        if (priv->leases_enabled) {
                ret = leases_init_priv (this);
                if (ret)
                        goto out;
        }
        */

        GF_OPTION_RECONF ("lease-lock-recall-timeout",
                          priv->recall_lease_timeout,
                          options, int32, out);

        ret = 0;
out:
        return ret;
}

int
init (xlator_t *this)
{
        int                       ret        = -1;
        leases_private_t         *priv       = NULL;

        priv = GF_CALLOC (1, sizeof (*priv),
                          gf_leases_mt_private_t);
        if (!priv) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM, LEASE_MSG_NO_MEM,
                        "Leases init failed");
                goto out;
        }

        GF_OPTION_INIT ("leases", priv->leases_enabled,
                        bool, out);
        GF_OPTION_INIT ("lease-lock-recall-timeout",
                        priv->recall_lease_timeout, int32, out);
        pthread_mutex_init (&priv->mutex, NULL);
        INIT_LIST_HEAD (&priv->client_list);
        INIT_LIST_HEAD (&priv->recall_list);

        this->private = priv;

        if (priv->leases_enabled) {
                ret = leases_init_priv (this);
                if (ret)
                        goto out;
        }

        ret = 0;

out:
        if (ret) {
                GF_FREE (priv);
                this->private = NULL;
        }

        return ret;
}

int
fini (xlator_t *this)
{
        leases_private_t *priv = NULL;

        priv = this->private;
        if (!priv) {
                return 0;
        }
        this->private = NULL;

        priv->fini = _gf_true;
        pthread_cond_broadcast (&priv->cond);
        pthread_join (priv->recall_thr, NULL);

        priv->inited_recall_thr = _gf_false;

        GF_FREE (priv);

        return 0;
}

static int
leases_forget (xlator_t *this, inode_t *inode)
{
        /* TODO:leases_cleanup_inode_ctx (this, inode); */
        return 0;
}

static int
leases_release (xlator_t *this, fd_t *fd)
{
        /* TODO:cleanup fd_ctx */
        return 0;
}

static int
leases_clnt_disconnect_cbk (xlator_t *this, client_t *client)
{
        int ret = 0;

        EXIT_IF_LEASES_OFF (this, out);

        ret = cleanup_client_leases (this, client->client_uid);
out:
        return ret;
}

struct xlator_fops fops = {
        /* Metadata modifying fops */
        .fsetattr    = leases_fsetattr,
        .setattr     = leases_setattr,

        /* File Data reading fops */
        .open        = leases_open,
        .readv       = leases_readv,

        /* File Data modifying fops */
        .truncate    = leases_truncate,
        .ftruncate   = leases_ftruncate,
        .writev      = leases_writev,
        .zerofill    = leases_zerofill,
        .fallocate   = leases_fallocate,
        .discard     = leases_discard,
        .lk          = leases_lk,
        .fsync       = leases_fsync,
        .flush       = leases_flush,
        .lease       = leases_lease,

        /* Directory Data modifying fops */
        .create      = leases_create,
        .rename      = leases_rename,
        .unlink      = leases_unlink,
        .link        = leases_link,

#ifdef NOT_SUPPORTED
        /* internal lk fops */
        .inodelk     = leases_inodelk,
        .finodelk    = leases_finodelk,
        .entrylk     = leases_entrylk,
        .fentrylk    = leases_fentrylk,

        /* Internal special fops*/
        .xattrop     = leases_xattrop,
        .fxattrop    = leases_fxattrop,
#endif
};

struct xlator_cbks cbks = {
        .forget            = leases_forget,
        .release           = leases_release,
        .client_disconnect = leases_clnt_disconnect_cbk,
};

struct volume_options options[] = {
        { .key  = {"leases"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "When \"on\", enables leases support"
        },
        { .key  = {"lease-lock-recall-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = RECALL_LEASE_LK_TIMEOUT,
          .description = "After 'timeout' seconds since the recall_lease"
                         " request has been sent to the client, the lease lock"
                         " will be forcefully purged by the server."
        },
        { .key = {NULL} },
};
