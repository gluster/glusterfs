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

int dht_writev2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_truncate2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_setattr2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_fallocate2(xlator_t *this, call_frame_t *frame, int op_ret);
int dht_discard2(xlator_t *this, call_frame_t *frame, int op_ret);
int dht_zerofill2(xlator_t *this, call_frame_t *frame, int op_ret);

int
dht_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t *local = NULL;
        int          ret   = -1;
        xlator_t    *subvol = NULL;

        if (op_ret == -1 && !dht_inode_missing(op_errno)) {
                goto out;
        }

        local = frame->local;
        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (local->call_cnt != 1) {
                /* preserve the modes of source */
                if (local->stbuf.ia_blocks) {
                        dht_iatt_merge (this, postbuf, &local->stbuf, NULL);
                        dht_iatt_merge (this, prebuf, &local->prebuf, NULL);
                }
                goto out;
        }

        local->rebalance.target_op_fn = dht_writev2;

        local->op_errno = op_errno;
        /* Phase 2 of migration */
        if (IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* Check if the rebalance phase1 is true */
        if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                dht_iatt_merge (this, &local->prebuf, prebuf, NULL);

                ret = dht_inode_ctx_get1 (this, local->fd->inode, &subvol);
                if (subvol) {
                        dht_writev2 (this, frame, 0);
                        return 0;
                }
                ret = dht_rebalance_in_progress_check (this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);

        DHT_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);

        return 0;
}

int
dht_writev2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;

        local = frame->local;

        dht_inode_ctx_get1 (this, local->fd->inode, &subvol);

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_writev_cbk,
                    subvol, subvol->fops->writev,
                    local->fd, local->rebalance.vector, local->rebalance.count,
                    local->rebalance.offset, local->rebalance.flags,
                    local->rebalance.iobref, NULL);

        return 0;
}

int
dht_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int count, off_t off, uint32_t flags,
            struct iobref *iobref, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_WRITE);
        if (!local) {

                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }


        local->rebalance.vector = iov_dup (vector, count);
        local->rebalance.offset = off;
        local->rebalance.count = count;
        local->rebalance.flags = flags;
        local->rebalance.iobref = iobref_ref (iobref);
        local->call_cnt = 1;

        STACK_WIND (frame, dht_writev_cbk,
                    subvol, subvol->fops->writev,
                    fd, vector, count, off, flags, iobref, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}



int
dht_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;
        xlator_t    *subvol = NULL;
        inode_t      *inode = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_log (this->name, GF_LOG_DEBUG,
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

        local->rebalance.target_op_fn = dht_truncate2;

        local->op_errno = op_errno;
        /* Phase 2 of migration */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* Check if the rebalance phase1 is true */
        if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                dht_iatt_merge (this, &local->prebuf, prebuf, NULL);
                inode = (local->fd) ? local->fd->inode : local->loc.inode;
                dht_inode_ctx_get1 (this, inode, &subvol);
                if (subvol) {
                        dht_truncate2 (this, frame, 0);
                        return 0;
                }
                ret = dht_rebalance_in_progress_check (this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (truncate, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);
err:
        return 0;
}


int
dht_truncate2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;
        inode_t      *inode = NULL;

        local = frame->local;

        inode = local->fd ? local->fd->inode : local->loc.inode;

        dht_inode_ctx_get1 (this, inode, &subvol);
        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        if (local->fop == GF_FOP_TRUNCATE) {
                STACK_WIND (frame, dht_truncate_cbk, subvol,
                            subvol->fops->truncate, &local->loc,
                            local->rebalance.offset, NULL);
        } else {
                STACK_WIND (frame, dht_truncate_cbk, subvol,
                            subvol->fops->ftruncate, local->fd,
                            local->rebalance.offset, NULL);
        }

        return 0;
}

int
dht_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
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

        local = dht_local_init (frame, loc, NULL, GF_FOP_TRUNCATE);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.offset = offset;
        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_truncate_cbk,
                    subvol, subvol->fops->truncate,
                    loc, offset, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
dht_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FTRUNCATE);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.offset = offset;
        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_truncate_cbk,
                    subvol, subvol->fops->ftruncate,
                    fd, offset, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int
dht_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;
        xlator_t    *subvol = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_log (this->name, GF_LOG_DEBUG,
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
        local->rebalance.target_op_fn = dht_fallocate2;

        /* Phase 2 of migration */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* Check if the rebalance phase1 is true */
        if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                dht_iatt_merge (this, &local->prebuf, prebuf, NULL);
                dht_inode_ctx_get1 (this, local->fd->inode, &subvol);
                if (subvol) {
                        dht_fallocate2 (this, frame, 0);
                        return 0;
                }
                ret = dht_rebalance_in_progress_check (this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (fallocate, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);
err:
        return 0;
}

int
dht_fallocate2(xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;

        local = frame->local;

        dht_inode_ctx_get1 (this, local->fd->inode, &subvol);

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

	STACK_WIND(frame, dht_fallocate_cbk, subvol, subvol->fops->fallocate,
		   local->fd, local->rebalance.flags, local->rebalance.offset,
		   local->rebalance.size, NULL);

        return 0;
}

int
dht_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
	      off_t offset, size_t len, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FALLOCATE);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

	local->rebalance.flags = mode;
        local->rebalance.offset = offset;
	local->rebalance.size = len;

        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_fallocate_cbk,
                    subvol, subvol->fops->fallocate,
                    fd, mode, offset, len, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int
dht_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;
        xlator_t    *subvol = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_log (this->name, GF_LOG_DEBUG,
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
        local->rebalance.target_op_fn = dht_discard2;

        /* Phase 2 of migration */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* Check if the rebalance phase1 is true */
        if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                dht_iatt_merge (this, &local->prebuf, prebuf, NULL);
                dht_inode_ctx_get1 (this, local->fd->inode, &subvol);
                if (subvol) {
                        dht_discard2 (this, frame, 0);
                        return 0;
                }
                ret = dht_rebalance_in_progress_check (this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (discard, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);
err:
        return 0;
}

int
dht_discard2(xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;

        local = frame->local;

        dht_inode_ctx_get1 (this, local->fd->inode, &subvol);

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

	STACK_WIND(frame, dht_discard_cbk, subvol, subvol->fops->discard,
		   local->fd, local->rebalance.offset, local->rebalance.size,
		   NULL);

        return 0;
}

int
dht_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	    size_t len, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_DISCARD);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.offset = offset;
	local->rebalance.size = len;

        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_discard_cbk, subvol, subvol->fops->discard,
                    fd, offset, len, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (discard, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
dht_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev  = NULL;
        int           ret   = -1;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_log (this->name, GF_LOG_DEBUG,
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
        local->rebalance.target_op_fn = dht_zerofill2;
        /* Phase 2 of migration */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* Check if the rebalance phase1 is true */
        if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                dht_iatt_merge (this, &local->prebuf, prebuf, NULL);
                ret = fd_ctx_get (local->fd, this, NULL);
                if (!ret) {
                        dht_zerofill2 (this, frame, 0);
                        return 0;
                }
                ret = dht_rebalance_in_progress_check (this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (zerofill, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);
err:
        return 0;
}

int
dht_zerofill2(xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local          = NULL;
        xlator_t     *subvol         = NULL;
        uint64_t      tmp_subvol     = 0;
        int           ret            = -1;

        local = frame->local;

        if (local->fd)
                ret = fd_ctx_get (local->fd, this, &tmp_subvol);
        if (!ret)
                subvol = (xlator_t *)(long)tmp_subvol;

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND(frame, dht_zerofill_cbk, subvol, subvol->fops->zerofill,
                   local->fd, local->rebalance.offset, local->rebalance.size,
                   NULL);

        return 0;
}

int
dht_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
        xlator_t     *subvol       = NULL;
        int           op_errno     = -1;
        dht_local_t  *local        = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_ZEROFILL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.offset = offset;
        local->rebalance.size = len;

        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_zerofill_cbk, subvol, subvol->fops->zerofill,
                    fd, offset, len, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (zerofill, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}



/* handle cases of migration here for 'setattr()' calls */
int
dht_file_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "subvolume %s returned -1 (%s)",
                        prev->this->name, strerror (op_errno));
                goto out;
        }

        if (local->call_cnt != 1)
                goto out;

        local->rebalance.target_op_fn = dht_setattr2;

        /* Phase 2 of migration */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* At the end of the migration process, whatever 'attr' we
           have on source file will be migrated to destination file
           in one shot, hence we don't need to check for in progress
           state here (ie, PHASE1) */
out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);

        return 0;
}

int
dht_setattr2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;
        inode_t      *inode = NULL;

        local = frame->local;

        inode = (local->fd) ? local->fd->inode : local->loc.inode;

        dht_inode_ctx_get1 (this, inode, &subvol);

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        if (local->fop == GF_FOP_SETATTR) {
                STACK_WIND (frame, dht_file_setattr_cbk, subvol,
                            subvol->fops->setattr, &local->loc,
                            &local->rebalance.stbuf, local->rebalance.flags,
                            NULL);
        } else {
                STACK_WIND (frame, dht_file_setattr_cbk, subvol,
                            subvol->fops->fsetattr, local->fd,
                            &local->rebalance.stbuf, local->rebalance.flags,
                            NULL);
        }

        return 0;
}


/* Keep the existing code same for all the cases other than regular file */
int
dht_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, struct iatt *statpre,
                 struct iatt *statpost, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;


        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }

                dht_iatt_merge (this, &local->prebuf, statpre, prev->this);
                dht_iatt_merge (this, &local->stbuf, statpost, prev->this);

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt))
                DHT_STACK_UNWIND (setattr, frame, local->op_ret, local->op_errno,
                                  &local->prebuf, &local->stbuf, xdata);

        return 0;
}


int
dht_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        dht_layout_t *layout = NULL;
        dht_local_t  *local  = NULL;
        int           op_errno = -1;
        int           i = -1;
        int           call_cnt = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_SETATTR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (!layout_is_sane (layout)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "layout is not sane for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (IA_ISREG (loc->inode->ia_type)) {
                /* in the regular file _cbk(), we need to check for
                   migration possibilities */
                local->rebalance.stbuf = *stbuf;
                local->rebalance.flags = valid;
                local->call_cnt = 1;
                subvol = local->cached_subvol;

                STACK_WIND (frame, dht_file_setattr_cbk, subvol,
                            subvol->fops->setattr,
                            loc, stbuf, valid, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_setattr_cbk,
                            layout->list[i].xlator,
                            layout->list[i].xlator->fops->setattr,
                            loc, stbuf, valid, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int
dht_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
              int32_t valid, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        dht_layout_t *layout = NULL;
        dht_local_t  *local  = NULL;
        int           op_errno = -1;
        int           i = -1;
        int           call_cnt = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FSETATTR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        if (!layout_is_sane (layout)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "layout is not sane for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        if (IA_ISREG (fd->inode->ia_type)) {
                /* in the regular file _cbk(), we need to check for
                   migration possibilities */
                local->rebalance.stbuf = *stbuf;
                local->rebalance.flags = valid;
                local->call_cnt = 1;
                subvol = local->cached_subvol;

                STACK_WIND (frame, dht_file_setattr_cbk, subvol,
                            subvol->fops->fsetattr,
                            fd, stbuf, valid, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_setattr_cbk,
                            layout->list[i].xlator,
                            layout->list[i].xlator->fops->fsetattr,
                            fd, stbuf, valid, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}
