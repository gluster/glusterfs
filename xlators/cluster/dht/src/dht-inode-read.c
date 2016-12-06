/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-common.h"

int dht_access2 (xlator_t *this, xlator_t *dst_node,
                 call_frame_t *frame, int ret);
int dht_readv2 (xlator_t *this, xlator_t *dst_node,
                call_frame_t *frame, int ret);
int dht_attr2 (xlator_t *this, xlator_t *dst_node,
               call_frame_t *frame, int ret);
int dht_open2 (xlator_t *this, xlator_t *dst_node,
               call_frame_t *frame, int ret);
int dht_flush2 (xlator_t *this, xlator_t *dst_node,
                call_frame_t *frame, int ret);
int dht_lk2 (xlator_t *this, xlator_t *dst_node,
             call_frame_t *frame, int ret);
int dht_fsync2 (xlator_t *this, xlator_t *dst_node,
                call_frame_t *frame, int ret);



int
dht_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;
        int           ret = 0;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);
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
dht_open2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int          op_errno = EINVAL;

        if (!frame || !frame->local)
                goto out;

        local = frame->local;
        op_errno = ENOENT;

        if (we_are_not_migrating (ret)) {
                /* This DHT layer is not migrating the file */
                DHT_STACK_UNWIND (open, frame, -1, local->op_errno,
                                  NULL, NULL);
                return 0;

        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2;

        STACK_WIND_COOKIE (frame, dht_open_cbk, subvol, subvol,
                           subvol->fops->open, &local->loc,
                           local->rebalance.flags, local->fd, NULL);
        return 0;

out:
        DHT_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);
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

        STACK_WIND_COOKIE (frame, dht_open_cbk, subvol, subvol,
                           subvol->fops->open, loc, flags, fd, xdata);

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
        xlator_t     *subvol1 = 0;
        xlator_t     *subvol2 = 0;
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;
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
                gf_msg_debug (this->name, op_errno,
                              "subvolume %s returned -1",
                              prev->name);
                goto out;
        }

        if (local->call_cnt != 1)
                goto out;

        local->op_errno = op_errno;
        local->op_ret = op_ret;

        /* Check if the rebalance phase2 is true */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (stbuf)) {

                local->rebalance.target_op_fn = dht_attr2;
                dht_set_local_rebalance (this, local, NULL, NULL,
                                         stbuf, xdata);
                inode = (local->fd) ? local->fd->inode : local->loc.inode;

                dht_inode_ctx_get_mig_info (this, inode, &subvol1, &subvol2);
                if (dht_mig_info_is_invalid (local->cached_subvol,
                                             subvol1, subvol2)){
                        /* Phase 2 of migration */
                        ret = dht_rebalance_complete_check (this, frame);
                        if (!ret)
                                return 0;
                } else {
                        /* it is a non-fd op or it is an fd based Fop and
                           opened on the dst.*/
                       if (local->fd &&
                           !dht_fd_open_on_dst (this, local->fd, subvol2)) {
                                ret = dht_rebalance_complete_check (this, frame);
                                if (!ret)
                                        return 0;
                        } else {
                                dht_attr2 (this, subvol2, frame, 0);
                                return 0;
                        }
                }
        }

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (stat, frame, op_ret, op_errno, stbuf, xdata);
err:
        return 0;
}

int
dht_attr2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (stat, frame, local->op_ret, op_errno,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }


        if (subvol == NULL)
                goto out;

        local->call_cnt = 2;

        if (local->fop == GF_FOP_FSTAT) {
                STACK_WIND_COOKIE (frame, dht_file_attr_cbk, subvol, subvol,
                                   subvol->fops->fstat, local->fd, NULL);
        } else {
                STACK_WIND_COOKIE (frame, dht_file_attr_cbk, subvol, subvol,
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
        xlator_t     *prev = NULL;

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
                        gf_msg_debug (this->name, op_errno,
                                      "subvolume %s returned -1",
                                      prev->name);

                        goto unlock;
                }

                dht_iatt_merge (this, &local->stbuf, stbuf, prev);

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

                STACK_WIND_COOKIE (frame, dht_file_attr_cbk, subvol, subvol,
                                   subvol->fops->stat, loc, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                subvol = layout->list[i].xlator;

                STACK_WIND_COOKIE (frame, dht_attr_cbk, subvol, subvol,
                                   subvol->fops->stat, loc, xdata);
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

                STACK_WIND_COOKIE (frame, dht_file_attr_cbk, subvol, subvol,
                                   subvol->fops->fstat, fd, xdata);

                return 0;
        }

        local->call_cnt = call_cnt = layout->cnt;

        for (i = 0; i < call_cnt; i++) {
                subvol = layout->list[i].xlator;
                STACK_WIND_COOKIE (frame, dht_attr_cbk, subvol, subvol,
                                   subvol->fops->fstat, fd, xdata);
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
        xlator_t    *src_subvol = 0;
        xlator_t    *dst_subvol = 0;

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

                local->op_ret = op_ret;
                local->rebalance.target_op_fn = dht_readv2;
                dht_set_local_rebalance (this, local, NULL, NULL,
                                         stbuf, xdata);
                /* File would be migrated to other node */
                ret = dht_inode_ctx_get_mig_info (this, local->fd->inode,
                                                  &src_subvol,
                                                  &dst_subvol);

                if (dht_mig_info_is_invalid (local->cached_subvol,
                                             src_subvol, dst_subvol)
                        || !dht_fd_open_on_dst(this, local->fd, dst_subvol)) {

                        ret = dht_rebalance_complete_check (this, frame);
                        if (!ret)
                                return 0;
                } else {
                        /* value is already set in fd_ctx, that means no need
                           to check for whether its complete or not. */
                        dht_readv2 (this, dst_subvol, frame, 0);
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
dht_readv2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local  = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */
                DHT_STACK_UNWIND (readv, frame, local->op_ret, op_errno,
                                  NULL, 0, &local->rebalance.postbuf,
                                  NULL, local->rebalance.xdata);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2;

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
        xlator_t    *prev = NULL;

        local = frame->local;
        prev = cookie;

        if (!prev)
                goto out;
        if (local->call_cnt != 1)
                goto out;
        if ((op_ret == -1) && ((op_errno == ENOTCONN) ||
                dht_inode_missing(op_errno)) &&
                IA_ISDIR(local->loc.inode->ia_type)) {
                subvol = dht_subvol_next_available (this, prev);
                if (!subvol)
                        goto out;

                /* check if we are done with visiting every node */
                if (subvol == local->cached_subvol) {
                        goto out;
                }

                STACK_WIND_COOKIE (frame, dht_access_cbk, subvol, subvol,
                                   subvol->fops->access, &local->loc,
                                   local->rebalance.flags, NULL);
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
dht_access2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;

        if (we_are_not_migrating (ret)) {
                /* This dht xlator is not migrating the file. Unwind and
                 * pass on the original mode bits so the higher DHT layer
                 * can handle this.
                 */

                DHT_STACK_UNWIND (access, frame, -1, op_errno, NULL);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2;

        STACK_WIND_COOKIE (frame, dht_access_cbk, subvol, subvol,
                           subvol->fops->access, &local->loc,
                           local->rebalance.flags, NULL);

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

        STACK_WIND_COOKIE (frame, dht_access_cbk, subvol, subvol,
                           subvol->fops->access, loc, mask, xdata);

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
        dht_local_t     *local  = NULL;
        xlator_t        *subvol = 0;
        int             ret = 0;

        local = frame->local;

        local->op_errno = op_errno;

        if (local->call_cnt != 1)
                goto out;

        local->rebalance.target_op_fn = dht_flush2;

        local->op_ret = op_ret;
        local->op_errno = op_errno;

        if (xdata)
                local->rebalance.xdata = dict_ref (xdata);

        /* If context is set, then send flush() it to the destination */
        dht_inode_ctx_get_mig_info (this, local->fd->inode, NULL, &subvol);
        if (subvol && dht_fd_open_on_dst (this, local->fd, subvol)) {
                dht_flush2 (this, subvol, frame, 0);
                return 0;
        }

        if (op_errno == EREMOTE) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret) {
                        return 0;
                }
        }

out:
        DHT_STACK_UNWIND (flush, frame, op_ret, op_errno, xdata);

        return 0;
}

int
dht_flush2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if ((frame == NULL) || (frame->local == NULL))
                goto out;

        local = frame->local;

        op_errno = local->op_errno;

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_flush_cbk,
                    subvol, subvol->fops->flush, local->fd,
                    local->rebalance.xdata);

        return 0;

out:
        DHT_STACK_UNWIND (flush, frame, -1, op_errno, NULL);
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
        xlator_t     *prev = NULL;
        int           ret = -1;
        inode_t      *inode = NULL;
        xlator_t     *src_subvol = 0;
        xlator_t     *dst_subvol = 0;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if (op_ret == -1 && !dht_inode_missing(op_errno)) {
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
        inode = local->fd->inode;

        local->rebalance.target_op_fn = dht_fsync2;
        dht_set_local_rebalance (this, local, NULL, prebuf,
                                 postbuf, xdata);

        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                ret = dht_rebalance_complete_check (this, frame);
                if (!ret)
                        return 0;
        }

        /* Check if the rebalance phase1 is true */
        if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {

                dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                dht_iatt_merge (this, &local->prebuf, prebuf, NULL);

                dht_inode_ctx_get_mig_info (this, inode, &src_subvol, &dst_subvol);

                if (dht_mig_info_is_invalid (local->cached_subvol, src_subvol,
                                     dst_subvol) ||
                      !dht_fd_open_on_dst (this, local->fd, dst_subvol)) {

                        ret = dht_rebalance_in_progress_check (this, frame);
                        if (!ret)
                                return 0;
                } else {
                        dht_fsync2 (this, dst_subvol, frame, 0);
                        return 0;
                }
        }


out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);

        DHT_STACK_UNWIND (fsync, frame, op_ret, op_errno,
                          prebuf, postbuf, xdata);

        return 0;
}

int
dht_fsync2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
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
                DHT_STACK_UNWIND (fsync, frame, local->op_ret,
                                  op_errno, &local->rebalance.prebuf,
                                  &local->rebalance.postbuf,
                                  local->rebalance.xdata);
                return 0;
        }

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND_COOKIE (frame, dht_fsync_cbk, subvol, subvol,
                           subvol->fops->fsync, local->fd,
                           local->rebalance.flags, NULL);

        return 0;

out:
        DHT_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);
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

        STACK_WIND_COOKIE (frame, dht_fsync_cbk, subvol, subvol,
                           subvol->fops->fsync, fd, datasync, xdata);

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
        dht_local_t     *local  = NULL;
        int             ret     = -1;
        xlator_t        *subvol = NULL;

        local = frame->local;

        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (local->call_cnt != 1)
                goto out;

        local->rebalance.target_op_fn = dht_lk2;

        local->op_ret = op_ret;
        local->op_errno = op_errno;

        if (xdata)
                local->rebalance.xdata = dict_ref (xdata);

        if (op_errno == EREMOTE) {
                dht_inode_ctx_get_mig_info (this, local->fd->inode,
                                            NULL, &subvol);
                if (subvol && dht_fd_open_on_dst (this, local->fd, subvol)) {
                        dht_lk2 (this, subvol, frame, 0);
                        return 0;
                } else {
                        ret = dht_rebalance_complete_check (this, frame);
                        if (!ret) {
                                return 0;
                        }
                }
        }

out:
        dht_lk_inode_unref (frame, op_ret);
        DHT_STACK_UNWIND (lk, frame, op_ret, op_errno, flock, xdata);

        return 0;
}

int
dht_lk2 (xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
        dht_local_t *local    = NULL;
        int32_t      op_errno = EINVAL;

        if ((frame == NULL) || (frame->local == NULL))
                goto out;

        local = frame->local;

        op_errno = local->op_errno;

        if (subvol == NULL)
                goto out;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_lk_cbk, subvol, subvol->fops->lk, local->fd,
                    local->rebalance.lock_cmd, &local->rebalance.flock,
                    local->rebalance.xdata);

        return 0;

out:
        DHT_STACK_UNWIND (lk, frame, -1, op_errno, NULL,  NULL);
        return 0;
}

int
dht_lk (call_frame_t *frame, xlator_t *this,
        fd_t *fd, int cmd, struct gf_flock *flock, dict_t *xdata)
{
        xlator_t        *lock_subvol    = NULL;
        int             op_errno        = -1;
        dht_local_t     *local          = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_LK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->lock_type = flock->l_type;
        lock_subvol = dht_get_lock_subvolume (this, flock, local);
        if (!lock_subvol) {
                gf_msg_debug (this->name, 0,
                              "no lock subvolume for path=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->rebalance.flock = *flock;
        local->rebalance.lock_cmd = cmd;

        local->call_cnt = 1;

        STACK_WIND (frame, dht_lk_cbk, lock_subvol, lock_subvol->fops->lk, fd,
                    cmd, flock, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
dht_lease_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, struct gf_lease *lease, dict_t *xdata)
{
        DHT_STACK_UNWIND (lease, frame, op_ret, op_errno, lease, xdata);

        return 0;
}

int
dht_lease (call_frame_t *frame, xlator_t *this,
           loc_t *loc, struct gf_lease *lease, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        subvol = dht_subvol_get_cached (this, loc->inode);
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        /* TODO: for rebalance, we need to preserve the fop arguments */
        STACK_WIND (frame, dht_lease_cbk, subvol, subvol->fops->lease,
                    loc, lease, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lease, frame, -1, op_errno, NULL, NULL);

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

        local = dht_local_init (frame, loc, NULL, GF_FOP_XATTROP);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for gfid=%s",
                              uuid_utoa (loc->inode->gfid));
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
        dht_lk_inode_unref (frame, op_ret);
        DHT_STACK_UNWIND (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int32_t
dht_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        xlator_t        *lock_subvol            = NULL;
        int              op_errno               = -1;
        dht_local_t     *local                  = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_INODELK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->lock_type = lock->l_type;
        lock_subvol = dht_get_lock_subvolume (this, lock, local);
        if (!lock_subvol) {
                gf_msg_debug (this->name, 0,
                              "no lock subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame,
                    dht_inodelk_cbk,
                    lock_subvol, lock_subvol->fops->inodelk,
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

        dht_lk_inode_unref (frame, op_ret);
        DHT_STACK_UNWIND (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
dht_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
              fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        xlator_t     *lock_subvol       = NULL;
        dht_local_t  *local             = NULL;
        int           op_errno          = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_INODELK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->call_cnt = 1;
        local->lock_type = lock->l_type;

        lock_subvol = dht_get_lock_subvolume (this, lock, local);
        if (!lock_subvol) {
                gf_msg_debug (this->name, 0,
                              "no lock subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }


        STACK_WIND (frame, dht_finodelk_cbk, lock_subvol,
                    lock_subvol->fops->finodelk,
                    volume, fd, cmd, lock, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (finodelk, frame, -1, op_errno, NULL);

        return 0;
}
