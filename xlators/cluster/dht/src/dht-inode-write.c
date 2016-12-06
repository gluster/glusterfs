/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "dht-common.h"

int dht_writev2 (xlator_t *this, xlator_t *subvol,
                 call_frame_t *frame, int ret);
int dht_truncate2 (xlator_t *this, xlator_t *subvol,
                   call_frame_t *frame, int ret);
int dht_setattr2 (xlator_t *this, xlator_t *subvol,
                  call_frame_t *frame, int ret);
int dht_fallocate2 (xlator_t *this, xlator_t *subvol,
                    call_frame_t *frame, int ret);
int dht_discard2 (xlator_t *this, xlator_t *subvol,
                  call_frame_t *frame, int ret);
int dht_zerofill2 (xlator_t *this, xlator_t *subvol,
                   call_frame_t *frame, int ret);

int
dht_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        dht_local_t *local = NULL;
        xlator_t    *prev = NULL;
        int          ret   = -1;
        xlator_t    *subvol1 = NULL;
        xlator_t    *subvol2 = NULL;

        local = frame->local;
        prev = cookie;

        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (op_ret == -1 && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_msg_debug (this->name, 0,
                              "subvolume %s returned -1 (%s)",
                              prev->name, strerror (op_errno));
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

        local->op_ret = op_ret;
        local->op_errno = op_errno;

        /* We might need to pass the stbuf information to the higher DHT
         * layer for appropriate handling.
         */

        dht_set_local_rebalance (this, local, NULL, prebuf, postbuf, xdata);

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

                ret = dht_inode_ctx_get_mig_info (this, local->fd->inode,
                                                  &subvol1, &subvol2);
                if (!dht_mig_info_is_invalid (local->cached_subvol,
                                              subvol1, subvol2)) {
                        if (dht_fd_open_on_dst (this, local->fd, subvol2)) {
                                dht_writev2 (this, subvol2, frame, 0);
                                return 0;
                        }
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
dht_writev2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if ((frame == NULL) || (frame->local == NULL))
                goto out;

        local = frame->local;
        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (writev, frame, local->op_ret,
                                  local->op_errno, &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }


        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND_COOKIE (frame, dht_writev_cbk, subvol,
                           subvol, subvol->fops->writev,
                           local->fd, local->rebalance.vector,
                           local->rebalance.count,
                           local->rebalance.offset, local->rebalance.flags,
                           local->rebalance.iobref, NULL);

        return 0;

out:
        DHT_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);

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
                gf_msg_debug (this->name, 0,
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

        STACK_WIND_COOKIE (frame, dht_writev_cbk, subvol, subvol,
                           subvol->fops->writev, fd, vector, count, off, flags,
                           iobref, xdata);

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
        dht_local_t  *local      = NULL;
        xlator_t     *prev       = NULL;
        int           ret        = -1;
        xlator_t     *src_subvol = NULL;
        xlator_t     *dst_subvol = NULL;
        inode_t      *inode      = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);

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

        local->op_ret = op_ret;
        local->op_errno = op_errno;

        /* We might need to pass the stbuf information to the higher DHT
         * layer for appropriate handling.
         */

        dht_set_local_rebalance (this, local, NULL, prebuf, postbuf, xdata);

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

                dht_inode_ctx_get_mig_info (this, inode, &src_subvol,
                                            &dst_subvol);
                if (!dht_mig_info_is_invalid (local->cached_subvol,
                                              src_subvol, dst_subvol)) {
                        if ((!local->fd) || ((local->fd) &&
                            dht_fd_open_on_dst (this, local->fd, dst_subvol))) {
                                dht_truncate2 (this, dst_subvol, frame, 0);
                                return 0;
                        }
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
dht_truncate2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if (!frame || !frame->local)
                goto out;

        local = frame->local;
        op_errno = local->op_errno;

        /* This dht xlator is not migrating the file  */
        if (we_are_not_migrating (ret)) {

                DHT_STACK_UNWIND (truncate, frame, local->op_ret,
                                  local->op_errno, &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        if (local->fop == GF_FOP_TRUNCATE) {
                STACK_WIND_COOKIE (frame, dht_truncate_cbk, subvol, subvol,
                                   subvol->fops->truncate, &local->loc,
                                   local->rebalance.offset, NULL);
        } else {
                STACK_WIND_COOKIE (frame, dht_truncate_cbk, subvol, subvol,
                                   subvol->fops->ftruncate, local->fd,
                                   local->rebalance.offset, NULL);
        }

        return 0;

out:
        DHT_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);
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

        local = dht_local_init (frame, loc, NULL, GF_FOP_TRUNCATE);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.offset = offset;
        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for gfid=%s",
                              uuid_utoa (loc->inode->gfid));
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND_COOKIE (frame, dht_truncate_cbk, subvol, subvol,
                           subvol->fops->truncate, loc, offset, xdata);

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
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND_COOKIE (frame, dht_truncate_cbk, subvol, subvol,
                           subvol->fops->ftruncate, fd, offset, xdata);

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
        xlator_t     *prev = NULL;
        int           ret = -1;
        xlator_t    *src_subvol = NULL;
        xlator_t    *dst_subvol = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);

                goto out;
        }

        if (local->call_cnt != 1) {
                if (local->stbuf.ia_blocks) {
                        dht_iatt_merge (this, postbuf, &local->stbuf, NULL);
                        dht_iatt_merge (this, prebuf, &local->prebuf, NULL);
                }
                goto out;
        }

        local->op_ret = op_ret;
        local->op_errno = op_errno;
        local->rebalance.target_op_fn = dht_fallocate2;

        dht_set_local_rebalance (this, local, NULL, prebuf, postbuf, xdata);

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

                dht_inode_ctx_get_mig_info (this, local->fd->inode, &src_subvol,
                                            &dst_subvol);
                if (!dht_mig_info_is_invalid (local->cached_subvol,
                                              src_subvol, dst_subvol)) {
                        if (dht_fd_open_on_dst (this, local->fd, dst_subvol)) {
                                dht_fallocate2 (this, dst_subvol, frame, 0);
                                return 0;
                        }
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
dht_fallocate2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if (!frame || !frame->local)
                goto out;

        local = frame->local;
        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (fallocate, frame, local->op_ret,
                                  local->op_errno,
                                  &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

	STACK_WIND_COOKIE (frame, dht_fallocate_cbk, subvol, subvol,
                           subvol->fops->fallocate, local->fd,
                           local->rebalance.flags, local->rebalance.offset,
		           local->rebalance.size, NULL);

        return 0;

out:
        DHT_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
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
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND_COOKIE (frame, dht_fallocate_cbk, subvol, subvol,
                           subvol->fops->fallocate, fd, mode, offset, len,
                           xdata);

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
        xlator_t     *prev = NULL;
        int           ret = -1;
        xlator_t    *src_subvol = NULL;
        xlator_t    *dst_subvol = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);

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
        local->op_ret = op_ret;
        local->op_errno = op_errno;

        dht_set_local_rebalance (this, local, NULL, prebuf, postbuf, xdata);

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

                dht_inode_ctx_get_mig_info (this, local->fd->inode, &src_subvol,
                                            &dst_subvol);
                if (!dht_mig_info_is_invalid(local->cached_subvol,
                                             src_subvol, dst_subvol)) {
                        if (dht_fd_open_on_dst (this, local->fd, dst_subvol)) {
                                dht_discard2 (this, dst_subvol, frame, 0);
                                return 0;
                        }
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
dht_discard2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if (!frame || !frame->local)
                goto out;

        local = frame->local;
        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (discard, frame, local->op_ret,
                                  local->op_errno,
                                  &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

	STACK_WIND_COOKIE (frame, dht_discard_cbk, subvol, subvol,
                           subvol->fops->discard, local->fd,
                           local->rebalance.offset, local->rebalance.size,
		           NULL);

        return 0;

out:
        DHT_STACK_UNWIND (discard, frame, -1, op_errno, NULL, NULL, NULL);
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
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND_COOKIE (frame, dht_discard_cbk, subvol, subvol,
                           subvol->fops->discard, fd, offset, len, xdata);

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
        dht_local_t  *local   = NULL;
        xlator_t     *prev    = NULL;
        int           ret     = -1;
        xlator_t     *subvol1 = NULL, *subvol2 = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                local->op_errno = op_errno;
                local->op_ret = -1;
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);
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
        local->op_ret = op_ret;
        local->op_errno = op_errno;

        dht_set_local_rebalance (this, local, NULL, prebuf, postbuf, xdata);

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

                ret = dht_inode_ctx_get_mig_info (this, local->fd->inode,
                                                  &subvol1, &subvol2);
                if (!dht_mig_info_is_invalid (local->cached_subvol,
                                              subvol1, subvol2)) {
                        if (dht_fd_open_on_dst (this, local->fd, subvol2)) {
                                dht_zerofill2 (this, subvol2, frame, 0);
                                return 0;
                        }
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
dht_zerofill2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if (!frame || !frame->local)
                goto out;

        local = frame->local;

        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (zerofill, frame, local->op_ret,
                                  local->op_errno,
                                  &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);

                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND_COOKIE (frame, dht_zerofill_cbk, subvol, subvol,
                           subvol->fops->zerofill,
                           local->fd, local->rebalance.offset,
                           local->rebalance.size, NULL);

        return 0;

out:

        DHT_STACK_UNWIND (zerofill, frame, -1, op_errno, NULL, NULL, NULL);
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
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND_COOKIE (frame, dht_zerofill_cbk, subvol, subvol,
                           subvol->fops->zerofill, fd, offset, len, xdata);

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
        xlator_t     *prev = NULL;
        int           ret = -1;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);
                goto out;
        }

        if (local->call_cnt != 1)
                goto out;

        local->op_ret = op_ret;
        local->op_errno = op_errno;

        local->rebalance.target_op_fn = dht_setattr2;


        /* Phase 2 of migration */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {

                dht_set_local_rebalance (this, local, NULL, prebuf,
                                         postbuf, xdata);

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
dht_setattr2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if (!frame || !frame->local)
                goto out;

        local = frame->local;
        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (setattr, frame, local->op_ret,
                                  local->op_errno,
                                  &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        if (local->fop == GF_FOP_SETATTR) {
                STACK_WIND_COOKIE (frame, dht_file_setattr_cbk, subvol,
                                   subvol, subvol->fops->setattr, &local->loc,
                                   &local->rebalance.stbuf, local->rebalance.flags,
                                   NULL);
        } else {
                STACK_WIND_COOKIE (frame, dht_file_setattr_cbk, subvol,
                                   subvol, subvol->fops->fsetattr, local->fd,
                                   &local->rebalance.stbuf, local->rebalance.flags,
                                   NULL);
        }

        return 0;

out:
        DHT_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);
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
        xlator_t     *prev = NULL;


        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_msg_debug (this->name, op_errno,
                                      "subvolume %s returned -1",
                                      prev->name);
                        goto unlock;
                }

                dht_iatt_merge (this, &local->prebuf, statpre, prev);
                dht_iatt_merge (this, &local->stbuf, statpost, prev);

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
		if (local->op_ret == 0)
			dht_inode_ctx_time_set (local->loc.inode, this,
						&local->stbuf);
                DHT_STACK_UNWIND (setattr, frame, local->op_ret, local->op_errno,
                                  &local->prebuf, &local->stbuf, xdata);
	}

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
                gf_msg_debug (this->name, 0,
                              "no layout for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (!layout_is_sane (layout)) {
                gf_msg_debug (this->name, 0,
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

                STACK_WIND_COOKIE (frame, dht_file_setattr_cbk, subvol,
                                   subvol, subvol->fops->setattr, loc, stbuf,
                                   valid, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND_COOKIE (frame, dht_setattr_cbk,
                                   layout->list[i].xlator,
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
                gf_msg_debug (this->name, 0,
                              "no layout for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        if (!layout_is_sane (layout)) {
                gf_msg_debug (this->name, 0,
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

                STACK_WIND_COOKIE (frame, dht_file_setattr_cbk, subvol,
                                   subvol, subvol->fops->fsetattr, fd, stbuf,
                                   valid, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND_COOKIE (frame, dht_setattr_cbk,
                                   layout->list[i].xlator,
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
