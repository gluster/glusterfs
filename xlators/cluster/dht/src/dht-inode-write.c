/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-common.h"

void
dht_inode_write_wind(call_frame_t *frame, xlator_t *wind_subvol);

int
dht_inode_write2(xlator_t *this, xlator_t *subvol, call_frame_t *frame,
                 int ret);

void
dht_inode_write_unwind(glusterfs_fop_t fop, call_frame_t *frame, int32_t op_ret,
                       int32_t op_errno, struct iatt *prestat,
                       struct iatt *poststat, dict_t *xdata)
{
    switch (fop) {
        case GF_FOP_WRITE:
            DHT_STACK_UNWIND(writev, frame, op_ret, op_errno, prestat, poststat,
                             xdata);
            break;

        case GF_FOP_ZEROFILL:
            DHT_STACK_UNWIND(zerofill, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;

        case GF_FOP_DISCARD:
            DHT_STACK_UNWIND(discard, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;

        case GF_FOP_TRUNCATE:
            DHT_STACK_UNWIND(truncate, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;

        case GF_FOP_FTRUNCATE:
            DHT_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;

        case GF_FOP_FALLOCATE:
            DHT_STACK_UNWIND(fallocate, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;
            /*TODO: too many cbk for dirs is different, cbk for mds is
             * different*/
        case GF_FOP_FSETATTR:
            DHT_STACK_UNWIND(fsetattr, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;

        case GF_FOP_SETATTR:
            DHT_STACK_UNWIND(setattr, frame, op_ret, op_errno, prestat,
                             poststat, xdata);
            break;
        default:
            GF_ASSERT(0);
            break;
    }
}

static int
dht_set_protect_from_external_writes(call_frame_t *frame)
{
    dht_local_t *local = frame->local;
    xlator_t *this = frame->this;
    int ret = 0;
    if (local->fop == GF_FOP_WRITE) {
        if (!local->xattr_req) {
            local->xattr_req = dict_new();
            if (!local->xattr_req) {
                ret = -ENOMEM;
                goto out;
            }
        }

        ret = dict_set_uint32(local->xattr_req, GF_PROTECT_FROM_EXTERNAL_WRITES,
                              1);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, DHT_MSG_DICT_SET_FAILED, 0,
                   "Failed to set key %s in dictionary",
                   GF_PROTECT_FROM_EXTERNAL_WRITES);
            errno = -ENOMEM;
            goto out;
        }
    }
out:
    return ret;
}

int
dht_inode_write_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *prev = NULL;
    int ret = -1;
    xlator_t *src_subvol = NULL;
    xlator_t *dst_subvol = NULL;
    inode_t *inode = NULL;

    local = frame->local;
    prev = cookie;

    /* Needs to be checked only for ftruncate.
     * ftruncate fails with EBADF/EINVAL if dht has not yet opened the fd
     * on the cached subvol. This could happen if the file was migrated
     * and a lookup updated the cached subvol in the inode ctx.
     * We only check once as this could actually be a valid error.
     */

    if ((local->fd) &&
        dht_check_remote_fd_failed_error(local, op_ret, op_errno)) {
        ret = dht_check_and_open_fd_on_subvol(this, frame);
        if (ret)
            goto out;
        return 0;
    }

    if ((op_ret == -1) && !dht_inode_missing(op_errno)) {
        local->op_errno = op_errno;
        local->op_ret = -1;
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                     prev->name);
        goto out;
    }

    if (local->call_cnt != 1) {
        if (local->stbuf.ia_blocks) {
            dht_iatt_merge(this, postbuf, &local->stbuf);
            dht_iatt_merge(this, prebuf, &local->prebuf);
        }
        goto out;
    }

    local->rebalance.target_op_fn = dht_inode_write2;
    local->op_ret = op_ret;
    local->op_errno = op_errno;

    /* We might need to pass the stbuf information to the higher DHT
     * layer for appropriate handling.
     */

    dht_set_local_rebalance(this, local, NULL, prebuf, postbuf, xdata);

    /* Phase 2 of migration */
    if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2(postbuf)) {
        ret = dht_rebalance_complete_check(this, frame);
        if (!ret)
            return 0;
    }

    /* Check if the rebalance phase1 is true.
       At the end of the migration process, whatever 'attr' we
       have on source file will be migrated to destination file
       in one shot, hence we don't need to check for in progress
       state here (ie, PHASE1) for [f]setattr*/
    if ((local->fop != GF_FOP_SETATTR) && (local->fop != GF_FOP_SETATTR) &&
        IS_DHT_MIGRATION_PHASE1(postbuf)) {
        ret = dht_set_protect_from_external_writes(frame);
        if (ret < 0) {
            op_ret = -1;
            op_errno = -ret;
            goto out;
        }

        dht_iatt_merge(this, &local->stbuf, postbuf);
        dht_iatt_merge(this, &local->prebuf, prebuf);

        inode = (local->fd) ? local->fd->inode : local->loc.inode;

        dht_inode_ctx_get_mig_info(this, inode, &src_subvol, &dst_subvol);
        if (!dht_mig_info_is_invalid(local->cached_subvol, src_subvol,
                                     dst_subvol)) {
            if ((!local->fd) ||
                ((local->fd) &&
                 dht_fd_open_on_dst(this, local->fd, dst_subvol))) {
                dht_inode_write2(this, dst_subvol, frame, 0);
                return 0;
            }
        }
        ret = dht_rebalance_in_progress_check(this, frame);
        if (!ret)
            return 0;
    }

out:
    DHT_STRIP_PHASE1_FLAGS(postbuf);
    DHT_STRIP_PHASE1_FLAGS(prebuf);

    dht_inode_write_unwind(local->fop, frame, op_ret, op_errno, prebuf, postbuf,
                           xdata);
    return 0;
}

void
dht_inode_write_wind(call_frame_t *frame, xlator_t *wind_subvol)
{
    dht_local_t *local = frame->local;

    switch (local->fop) {
        case GF_FOP_WRITE:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->writev, local->fd,
                              local->rebalance.vector, local->rebalance.count,
                              local->rebalance.offset, local->rebalance.flags,
                              local->rebalance.iobref, local->xattr_req);
            break;

        case GF_FOP_TRUNCATE:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->truncate,
                              &local->loc, local->rebalance.offset,
                              local->xattr_req);
            break;

        case GF_FOP_FTRUNCATE:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->ftruncate,
                              local->fd, local->rebalance.offset,
                              local->xattr_req);
            break;

        case GF_FOP_ZEROFILL:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->zerofill,
                              local->fd, local->rebalance.offset,
                              local->rebalance.size, local->xattr_req);
            break;

        case GF_FOP_DISCARD:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->discard,
                              local->fd, local->rebalance.offset,
                              local->rebalance.size, local->xattr_req);
            break;

        case GF_FOP_FALLOCATE:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->fallocate,
                              local->fd, local->rebalance.flags,
                              local->rebalance.offset, local->rebalance.size,
                              local->xattr_req);
            break;
            /*TODO: too many cbk for dirs is different, cbk for mds is
             * different*/
        case GF_FOP_FSETATTR:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->fsetattr,
                              local->fd, &local->rebalance.stbuf,
                              local->rebalance.flags, local->xattr_req);
            break;

        case GF_FOP_SETATTR:
            STACK_WIND_COOKIE(frame, dht_inode_write_cbk, wind_subvol,
                              wind_subvol, wind_subvol->fops->setattr,
                              &local->loc, &local->rebalance.stbuf,
                              local->rebalance.flags, local->xattr_req);
            break;
        default:
            GF_ASSERT(0);
            break;
    }
}

int
dht_inode_write2(xlator_t *this, xlator_t *subvol, call_frame_t *frame, int ret)
{
    dht_local_t *local = NULL;
    int32_t op_errno = EINVAL;

    local = frame->local;
    op_errno = local->op_errno;

    if (we_are_not_migrating(ret)) {
        /* This dht xlator is not migrating the file. Unwind and
         * pass on the original mode bits so the higher DHT layer
         * can handle this.
         */
        dht_inode_write_unwind(local->fop, frame, local->op_ret,
                               local->op_errno, &local->rebalance.prebuf,
                               &local->rebalance.postbuf,
                               local->rebalance.xdata);
        return 0;
    }

    if (subvol == NULL)
        goto out;

    local->call_cnt = 2; /* This is the second attempt */

    dht_inode_write_wind(frame, subvol);

    return 0;

out:
    dht_inode_write_unwind(local->fop, frame, -1, op_errno, NULL, NULL, NULL);
    return 0;
}

int
dht_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int count, off_t off, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    local = dht_local_init(frame, NULL, fd, GF_FOP_WRITE);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    local->rebalance.vector = iov_dup(vector, count);
    local->rebalance.offset = off;
    local->rebalance.count = count;
    local->rebalance.flags = flags;
    local->rebalance.iobref = iobref_ref(iobref);
    local->call_cnt = 1;

    dht_inode_write_wind(frame, subvol);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    dht_inode_write_unwind(GF_FOP_WRITE, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

int
dht_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    local = dht_local_init(frame, loc, NULL, GF_FOP_TRUNCATE);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->rebalance.offset = offset;
    local->call_cnt = 1;
    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for gfid=%s",
                     uuid_utoa(loc->inode->gfid));
        op_errno = EINVAL;
        goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    dht_inode_write_wind(frame, subvol);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    dht_inode_write_unwind(GF_FOP_TRUNCATE, frame, -1, op_errno, NULL, NULL,
                           NULL);

    return 0;
}

int
dht_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    local = dht_local_init(frame, NULL, fd, GF_FOP_FTRUNCATE);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->rebalance.offset = offset;
    local->call_cnt = 1;
    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    dht_inode_write_wind(frame, subvol);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    dht_inode_write_unwind(GF_FOP_FTRUNCATE, frame, -1, op_errno, NULL, NULL,
                           NULL);

    return 0;
}

int
dht_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
              off_t offset, size_t len, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    local = dht_local_init(frame, NULL, fd, GF_FOP_FALLOCATE);
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
        gf_msg_debug(this->name, 0, "no cached subvolume for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    dht_inode_write_wind(frame, subvol);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    dht_inode_write_unwind(GF_FOP_FALLOCATE, frame, -1, op_errno, NULL, NULL,
                           NULL);

    return 0;
}

int
dht_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    local = dht_local_init(frame, NULL, fd, GF_FOP_DISCARD);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->rebalance.offset = offset;
    local->rebalance.size = len;

    local->call_cnt = 1;
    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    dht_inode_write_wind(frame, subvol);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    dht_inode_write_unwind(GF_FOP_DISCARD, frame, -1, op_errno, NULL, NULL,
                           NULL);

    return 0;
}

int
dht_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    int op_errno = -1;
    dht_local_t *local = NULL;

    local = dht_local_init(frame, NULL, fd, GF_FOP_ZEROFILL);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    local->rebalance.offset = offset;
    local->rebalance.size = len;

    local->call_cnt = 1;
    subvol = local->cached_subvol;
    if (!subvol) {
        gf_msg_debug(this->name, 0, "no cached subvolume for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    dht_inode_write_wind(frame, subvol);

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    dht_inode_write_unwind(GF_FOP_ZEROFILL, frame, -1, op_errno, NULL, NULL,
                           NULL);

    return 0;
}

/* Keep the existing code same for all the cases other than regular file */
int
dht_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, struct iatt *statpre, struct iatt *statpost,
                dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;

    local = frame->local;
    prev = cookie;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                         prev->name);
            goto post_unlock;
        }

        dht_iatt_merge(this, &local->prebuf, statpre);
        dht_iatt_merge(this, &local->stbuf, statpost);

        local->op_ret = 0;
        local->op_errno = 0;
    }
    UNLOCK(&frame->lock);
post_unlock:
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        if (local->op_ret == 0)
            dht_inode_ctx_time_set(local->loc.inode, this, &local->stbuf);
        DHT_STACK_UNWIND(setattr, frame, local->op_ret, local->op_errno,
                         &local->prebuf, &local->stbuf, xdata);
    }

    return 0;
}

/* Keep the existing code same for all the cases other than regular file */
int
dht_non_mds_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, struct iatt *statpre,
                        struct iatt *statpost, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;

    local = frame->local;
    prev = cookie;

    if (op_ret == -1) {
        gf_msg(this->name, op_errno, 0, 0, "subvolume %s returned -1",
               prev->name);
        goto post_unlock;
    }

    LOCK(&frame->lock);
    {
        dht_iatt_merge(this, &local->prebuf, statpre);
        dht_iatt_merge(this, &local->stbuf, statpost);

        local->op_ret = 0;
        local->op_errno = 0;
    }
    UNLOCK(&frame->lock);
post_unlock:
    this_call_cnt = dht_frame_return(frame);
    if (is_last_call(this_call_cnt)) {
        dht_inode_ctx_time_set(local->loc.inode, this, &local->stbuf);
        DHT_STACK_UNWIND(setattr, frame, 0, 0, &local->prebuf, &local->stbuf,
                         xdata);
    }

    return 0;
}

int
dht_mds_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, struct iatt *statpre,
                    struct iatt *statpost, dict_t *xdata)

{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    xlator_t *prev = NULL;
    xlator_t *mds_subvol = NULL;
    struct iatt loc_stbuf = {
        0,
    };
    int i = 0;

    local = frame->local;
    prev = cookie;
    conf = this->private;
    mds_subvol = local->mds_subvol;

    if (op_ret == -1) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        gf_msg_debug(this->name, op_errno, "subvolume %s returned -1",
                     prev->name);
        goto out;
    }

    local->op_ret = 0;
    loc_stbuf = local->stbuf;
    dht_iatt_merge(this, &local->prebuf, statpre);
    dht_iatt_merge(this, &local->stbuf, statpost);

    local->call_cnt = conf->subvolume_cnt - 1;
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (mds_subvol == conf->subvolumes[i])
            continue;
        STACK_WIND_COOKIE(frame, dht_non_mds_setattr_cbk, conf->subvolumes[i],
                          conf->subvolumes[i],
                          conf->subvolumes[i]->fops->setattr, &local->loc,
                          &loc_stbuf, local->valid, local->xattr_req);
    }

    return 0;
out:
    DHT_STACK_UNWIND(setattr, frame, local->op_ret, local->op_errno,
                     &local->prebuf, &local->stbuf, xdata);

    return 0;
}

int
dht_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
            int32_t valid, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    xlator_t *mds_subvol = NULL;
    dht_layout_t *layout = NULL;
    dht_local_t *local = NULL;
    int op_errno = -1;
    int i = -1;
    int ret = -1;
    int call_cnt = 0;
    dht_conf_t *conf = NULL;

    conf = this->private;
    local = dht_local_init(frame, loc, NULL, GF_FOP_SETATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg_debug(this->name, 0, "no layout for path=%s", loc->path);
        op_errno = EINVAL;
        goto err;
    }

    if (!layout_is_sane(layout)) {
        gf_msg_debug(this->name, 0, "layout is not sane for path=%s",
                     loc->path);
        op_errno = EINVAL;
        goto err;
    }
    if (xdata)
        local->xattr_req = dict_ref(xdata);

    if (IA_ISREG(loc->inode->ia_type)) {
        /* in the regular file _cbk(), we need to check for
           migration possibilities */
        local->rebalance.stbuf = *stbuf;
        local->rebalance.flags = valid;
        local->call_cnt = 1;
        subvol = local->cached_subvol;

        dht_inode_write_wind(frame, subvol);

        return 0;
    }

    local->call_cnt = call_cnt = layout->cnt;

    if (IA_ISDIR(loc->inode->ia_type) && !__is_root_gfid(loc->inode->gfid) &&
        call_cnt != 1) {
        ret = dht_inode_ctx_mdsvol_get(loc->inode, this, &mds_subvol);
        if (ret || !mds_subvol) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   DHT_MSG_HASHED_SUBVOL_GET_FAILED,
                   "Failed to get mds subvol for path %s", local->loc.path);
            op_errno = EINVAL;
            goto err;
        }

        local->mds_subvol = mds_subvol;
        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->subvolumes[i] == mds_subvol) {
                if (!conf->subvolume_status[i]) {
                    gf_msg(this->name, GF_LOG_WARNING, layout->list[i].err,
                           DHT_MSG_HASHED_SUBVOL_DOWN,
                           "MDS subvol is down for path "
                           " %s Unable to set attr ",
                           local->loc.path);
                    op_errno = ENOTCONN;
                    goto err;
                }
            }
        }
        local->valid = valid;
        local->stbuf = *stbuf;

        STACK_WIND_COOKIE(frame, dht_mds_setattr_cbk, local->mds_subvol,
                          local->mds_subvol, local->mds_subvol->fops->setattr,
                          loc, stbuf, valid, xdata);
        return 0;
    } else {
        for (i = 0; i < call_cnt; i++) {
            STACK_WIND_COOKIE(frame, dht_setattr_cbk, layout->list[i].xlator,
                              layout->list[i].xlator,
                              layout->list[i].xlator->fops->setattr, loc, stbuf,
                              valid, xdata);
        }
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(setattr, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

int
dht_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
             int32_t valid, dict_t *xdata)
{
    xlator_t *subvol = NULL;
    dht_layout_t *layout = NULL;
    dht_local_t *local = NULL;
    int op_errno = -1;
    int i = -1;
    int call_cnt = 0;

    local = dht_local_init(frame, NULL, fd, GF_FOP_FSETATTR);
    if (!local) {
        op_errno = ENOMEM;
        goto err;
    }

    layout = local->layout;
    if (!layout) {
        gf_msg_debug(this->name, 0, "no layout for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }

    if (!layout_is_sane(layout)) {
        gf_msg_debug(this->name, 0, "layout is not sane for fd=%p", fd);
        op_errno = EINVAL;
        goto err;
    }
    if (xdata)
        local->xattr_req = dict_ref(xdata);

    if (IA_ISREG(fd->inode->ia_type)) {
        /* in the regular file _cbk(), we need to check for
           migration possibilities */
        local->rebalance.stbuf = *stbuf;
        local->rebalance.flags = valid;
        local->call_cnt = 1;
        subvol = local->cached_subvol;

        dht_inode_write_wind(frame, subvol);
        return 0;
    }

    local->call_cnt = call_cnt = layout->cnt;

    for (i = 0; i < call_cnt; i++) {
        STACK_WIND_COOKIE(frame, dht_setattr_cbk, layout->list[i].xlator,
                          layout->list[i].xlator,
                          layout->list[i].xlator->fops->fsetattr, fd, stbuf,
                          valid, xdata);
    }

    return 0;

err:
    op_errno = (op_errno == -1) ? errno : op_errno;
    DHT_STACK_UNWIND(fsetattr, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}
