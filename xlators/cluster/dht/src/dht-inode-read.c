/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "dht-common.h"

int dht_access2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_readv2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_attr2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_open2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_flush2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_lk2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_fsync2 (xlator_t *this, call_frame_t *frame, int ret);

int
dht_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = 0;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                gf_msg_debug (this->name, 0,
                              "subvolume %s returned -1 (%s)",
                              prev->this->name, strerror (op_errno));
                goto out;
        }

        if (!op_ret || (local->call_cnt != 1))
                goto out;

        /* rebalance would have happened */
        local->rebalance.target_op_fn = dht_open2;
        ret = dht_rebalance_complete_check (this, frame);
        if (!ret)
                return 0;

out:
        DHT_STACK_UNWIND (open, frame, op_ret, op_errno, local->fd, xdata);

        return 0;
}

int
dht_open2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = ENOENT;
        if (op_ret)
                goto out;

        local->call_cnt = 2;
        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_open_cbk, subvol, subvol->fops->open,
                    &local->loc, local->rebalance.flags, local->fd,
                    NULL);
        return 0;

out:
        DHT_STACK_UNWIND (stat, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int
dht_open (call_frame_t *frame, xlator_t *this,
          loc_t *loc, int flags, fd_t *fd, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, loc, fd, GF_FOP_OPEN);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->rebalance.flags = flags;
        local->call_cnt = 1;

        STACK_WIND (frame, dht_open_cbk, subvol, subvol->fops->open,
                    loc, flags, fd, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
dht_file_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *stbuf, dict_t *xdata)
{
        xlator_t     *subvol = 0;
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;
        inode_t      *inode = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                gf_msg_debug (this->name, 0,
                              "subvolume %s returned -1 (%s)",
                              prev->this->name, strerror (op_errno));
                goto out;
        }

        if (local->call_cnt != 1)
                goto out;

        local->op_errno = op_errno;
        /* Check if the rebalance phase2 is true */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (stbuf)) {
                inode = (local->fd) ? local->fd->inode : local->loc.inode;
                ret = dht_inode_ctx_get1 (this, inode, &subvol);
                if (!subvol) {
                        /* Phase 2 of migration */
                        local->rebalance.target_op_fn = dht_attr2;
                        ret = dht_rebalance_complete_check (this, frame);
                        if (!ret)
                                return 0;
                } else {
                        /* value is already set in fd_ctx, that means no need
                           to check for whether its complete or not. */
                        dht_attr2 (this, frame, 0);
                        return 0;
                }
        }

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (stat, frame, op_ret, op_errno, stbuf, xdata);
err:
        return 0;
}

int
dht_attr2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;
        if (op_ret == -1)
                goto out;

        subvol = local->cached_subvol;
        local->call_cnt = 2;

        if (local->fop == GF_FOP_FSTAT) {
                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->fstat, local->fd, NULL);
        } else {
                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->stat, &local->loc, NULL);
        }
        return 0;

out:
        DHT_STACK_UNWIND (stat, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int
dht_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno, struct iatt *stbuf, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_msg_debug (this->name, 0,
                                      "subvolume %s returned -1 (%s)",
                                      prev->this->name, strerror (op_errno));

                        goto unlock;
                }

                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);
out:
        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (stat, frame, local->op_ret, local->op_errno,
                                  &local->stbuf, xdata);
        }
err:
        return 0;
}

int
dht_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           i = 0;
        int           call_cnt = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);


        local = dht_local_init (frame, loc, NULL, GF_FOP_STAT);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_msg_debug (this->name, 0,
                              "no layout for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (IA_ISREG (loc->inode->ia_type)) {
                local->call_cnt = 1;

                subvol = local->cached_subvol;

                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->stat, loc, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                subvol = layout->list[i].xlator;

                STACK_WIND (frame, dht_attr_cbk,
                            subvol, subvol->fops->stat,
                            loc, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (stat, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           i = 0;
        int           call_cnt = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FSTAT);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_msg_debug (this->name, 0,
                              "no layout for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        if (IA_ISREG (fd->inode->ia_type)) {
                local->call_cnt = 1;

                subvol = local->cached_subvol;

                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->fstat, fd, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                subvol = layout->list[i].xlator;
                STACK_WIND (frame, dht_attr_cbk,
                            subvol, subvol->fops->fstat,
                            fd, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fstat, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
dht_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno,
               struct iovec *vector, int count, struct iatt *stbuf,
               struct iobref *iobref, dict_t *xdata)
{
        dht_local_t *local      = NULL;
        int          ret        = 0;
        inode_t     *inode      = NULL;
        xlator_t    *subvol = 0;

        local = frame->local;
        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        /* This is already second try, no need for re-check */
        if (local->call_cnt != 1)
                goto out;

        if ((op_ret == -1) && !dht_inode_missing(op_errno))
                goto out;

        local->op_errno = op_errno;
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (stbuf)) {
                /* File would be migrated to other node */
                ret = dht_inode_ctx_get1 (this, inode, &subvol);
                if (!subvol) {
                        local->rebalance.target_op_fn = dht_readv2;
                        ret = dht_rebalance_complete_check (this, frame);
                        if (!ret)
                                return 0;
                } else {
                        /* value is already set in fd_ctx, that means no need
                           to check for whether its complete or not. */
                        dht_readv2 (this, frame, 0);
                        return 0;
                }
        }

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count, stbuf,
                          iobref, xdata);

        return 0;
}

int
dht_readv2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;
        if (op_ret == -1)
                goto out;

        local->call_cnt = 2;
        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_readv_cbk, subvol, subvol->fops->readv,
                    local->fd, local->rebalance.size, local->rebalance.offset,
                    local->rebalance.flags, NULL);

        return 0;

out:
        DHT_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);
        return 0;
}

int
dht_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t off, uint32_t flags, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_READ);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->rebalance.offset = off;
        local->rebalance.size   = size;
        local->rebalance.flags  = flags;
        local->call_cnt = 1;

        STACK_WIND (frame, dht_readv_cbk,
                    subvol, subvol->fops->readv,
                    fd, size, off, flags, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);

        return 0;
}

int
dht_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, dict_t *xdata)
{
        int          ret = -1;
        dht_local_t *local = NULL;
        xlator_t    *subvol = NULL;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev = cookie;

        if (!prev || !prev->this)
                goto out;
        if (local->call_cnt != 1)
                goto out;
        if ((op_ret == -1) && ((op_errno == ENOTCONN) ||
                dht_inode_missing(op_errno)) &&
                IA_ISDIR(local->loc.inode->ia_type)) {
                subvol = dht_subvol_next_available (this, prev->this);
                if (!subvol)
                        goto out;

                /* check if we are done with visiting every node */
                if (subvol == local->cached_subvol) {
                        goto out;
                }

                STACK_WIND (frame, dht_access_cbk, subvol, subvol->fops->access,
                            &local->loc, local->rebalance.flags, NULL);
                return 0;
        }
        if ((op_ret == -1) && dht_inode_missing(op_errno) &&
                !(IA_ISDIR(local->loc.inode->ia_type))) {
                /* File would be migrated to other node */
                local->op_errno = op_errno;
                local->rebalance.target_op_fn = dht_access2;
                ret = dht_rebalance_complete_check (frame->this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STACK_UNWIND (access, frame, op_ret, op_errno, xdata);
        return 0;
}

int
dht_access2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;
        if (op_ret == -1)
                goto out;

        local->call_cnt = 2;
        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_access_cbk, subvol, subvol->fops->access,
                    &local->loc, local->rebalance.flags, NULL);

        return 0;

out:
        DHT_STACK_UNWIND (access, frame, -1, op_errno, NULL);
        return 0;
}


int
dht_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
            dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_ACCESS);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.flags = mask;
        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_access_cbk, subvol, subvol->fops->access,
                    loc, mask, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (access, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local  = NULL;
        inode_t      *inode  = NULL;
        xlator_t     *subvol = 0;

        local = frame->local;

        local->op_errno = op_errno;

        if (local->call_cnt != 1)
                goto out;

        /* If context is set, then send flush() it to the destination */
        dht_inode_ctx_get1 (this, inode, &subvol);
        if (subvol) {
                dht_flush2 (this, frame, 0);
                return 0;
        }

out:
        DHT_STACK_UNWIND (flush, frame, op_ret, op_errno, xdata);

        return 0;
}

int
dht_flush2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;

        local = frame->local;

        dht_inode_ctx_get1 (this, local->fd->inode, &subvol);

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_flush_cbk,
                    subvol, subvol->fops->flush, local->fd, NULL);

        return 0;
}


int
dht_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FLUSH);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame, dht_flush_cbk,
                    subvol, subvol->fops->flush, fd, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (flush, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, struct iatt *prebuf, struct iatt *postbuf,
               dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;
        inode_t      *inode = NULL;
        xlator_t     *subvol = 0;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if (op_ret == -1 && !dht_inode_missing(op_errno)) {
                gf_msg_debug (this->name, 0,
                              "subvolume %s returned -1 (%s)",
                              prev->this->name, strerror (op_errno));
                goto out;
        }

        if (local->call_cnt != 1) {
                if (local->stbuf.ia_blocks) {
                        dht_iatt_merge (this, postbuf, &local->stbuf, NULL);
                        dht_iatt_merge (this, prebuf, &local->prebuf, NULL);
                }
                goto out;
        }

        local->op_errno = op_errno;
        dht_inode_ctx_get1 (this, inode, &subvol);
        if (!subvol) {
                local->rebalance.target_op_fn = dht_fsync2;

                /* Check if the rebalance phase1 is true */
                if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                        dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                        dht_iatt_merge (this, &local->prebuf, prebuf, NULL);

                        ret = dht_rebalance_in_progress_check (this, frame);
                }

                /* Check if the rebalance phase2 is true */
                if (IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                        ret = dht_rebalance_complete_check (this, frame);
                }
                if (!ret)
                        return 0;
        } else {
                dht_fsync2 (this, frame, 0);
                return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (fsync, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);

        return 0;
}

int
dht_fsync2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;

        local = frame->local;

        dht_inode_ctx_get1 (this, local->fd->inode, &subvol);
        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_fsync_cbk, subvol, subvol->fops->fsync,
                    local->fd, local->rebalance.flags, NULL);

        return 0;
}

int
dht_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
           dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FSYNC);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        local->call_cnt = 1;
        local->rebalance.flags = datasync;

        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_fsync_cbk, subvol, subvol->fops->fsync,
                    fd, datasync, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


/* TODO: for 'lk()' call, we need some other special error, may be ESTALE to
   indicate that lock migration happened on the fd, so we can consider it as
   phase 2 of migration */
int
dht_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int op_ret, int op_errno, struct gf_flock *flock, dict_t *xdata)
{
        DHT_STACK_UNWIND (lk, frame, op_ret, op_errno, flock, xdata);

        return 0;
}


int
dht_lk (call_frame_t *frame, xlator_t *this,
        fd_t *fd, int cmd, struct gf_flock *flock, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        /* TODO: for rebalance, we need to preserve the fop arguments */
        STACK_WIND (frame, dht_lk_cbk, subvol, subvol->fops->lk, fd,
                    cmd, flock, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);

        return 0;
}

/* Symlinks are currently not migrated, so no need for any check here */
int
dht_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, const char *path,
                  struct iatt *stbuf, dict_t *xdata)
{
        dht_local_t *local = NULL;

        local = frame->local;
        if (op_ret == -1)
                goto err;

        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
        }

err:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (readlink, frame, op_ret, op_errno, path, stbuf, xdata);

        return 0;
}


int
dht_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
              dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_READLINK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_readlink_cbk,
                    subvol, subvol->fops->readlink,
                    loc, size, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

/* Currently no translators on top of 'distribute' will be using
 * below fops, hence not implementing 'migration' related checks
 */

int
dht_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        DHT_STACK_UNWIND (xattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
dht_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_XATTROP);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame,
                    dht_xattrop_cbk,
                    subvol, subvol->fops->xattrop,
                    loc, flags, dict, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        DHT_STACK_UNWIND (fxattrop, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
dht_fxattrop (call_frame_t *frame, xlator_t *this,
              fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame,
                    dht_fxattrop_cbk,
                    subvol, subvol->fops->fxattrop,
                    fd, flags, dict, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fxattrop, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_inodelk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        DHT_STACK_UNWIND (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int32_t
dht_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_INODELK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame,
                    dht_inodelk_cbk,
                    subvol, subvol->fops->inodelk,
                    volume, loc, cmd, lock, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (inodelk, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        DHT_STACK_UNWIND (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
dht_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
              fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }


        STACK_WIND (frame, dht_finodelk_cbk, subvol, subvol->fops->finodelk,
                    volume, fd, cmd, lock, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (finodelk, frame, -1, op_errno, NULL);

        return 0;
}
