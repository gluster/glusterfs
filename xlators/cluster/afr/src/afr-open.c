/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#include <glusterfs/dict.h>
#include <glusterfs/logging.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/compat.h>
#include <glusterfs/statedump.h>

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "protocol-common.h"

static gf_boolean_t
afr_is_fd_fixable(fd_t *fd)
{
    if (!fd || !fd->inode)
        return _gf_false;
    else if (fd_is_anonymous(fd))
        return _gf_false;
    else if (gf_uuid_is_null(fd->inode->gfid))
        return _gf_false;

    return _gf_true;
}

static int
afr_open_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
    afr_local_t *local = frame->local;

    AFR_STACK_UNWIND(open, frame, local->op_ret, local->op_errno,
                     local->cont.open.fd, xdata);
    return 0;
}

static int
afr_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    afr_local_t *local = NULL;
    int call_count = -1;
    int child_index = (long)cookie;
    afr_fd_ctx_t *fd_ctx = NULL;

    local = frame->local;
    fd_ctx = local->fd_ctx;

    local->replies[child_index].valid = 1;
    local->replies[child_index].op_ret = op_ret;
    local->replies[child_index].op_errno = op_errno;

    LOCK(&frame->lock);
    {
        if (op_ret == -1) {
            local->op_errno = op_errno;
            fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
        } else {
            local->op_ret = op_ret;
            fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
            if (!local->xdata_rsp && xdata)
                local->xdata_rsp = dict_ref(xdata);
        }
        call_count = --local->call_count;
    }
    UNLOCK(&frame->lock);

    if (call_count == 0) {
        afr_handle_replies_quorum(frame, this);
        if (local->op_ret == -1) {
            AFR_STACK_UNWIND(open, frame, local->op_ret, local->op_errno, NULL,
                             NULL);
        } else if (fd_ctx->flags & O_TRUNC) {
            STACK_WIND(frame, afr_open_ftruncate_cbk, this,
                       this->fops->ftruncate, fd, 0, NULL);
        } else {
            AFR_STACK_UNWIND(open, frame, local->op_ret, local->op_errno,
                             local->cont.open.fd, local->xdata_rsp);
        }
    }

    return 0;
}

static int
afr_open_continue(call_frame_t *frame, xlator_t *this, int err)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    int call_count = 0;
    int i = 0;

    local = frame->local;
    priv = this->private;

    if (err) {
        AFR_STACK_UNWIND(open, frame, -1, err, NULL, NULL);
    } else {
        local->call_count = AFR_COUNT(local->child_up, priv->child_count);
        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
            if (local->child_up[i]) {
                STACK_WIND_COOKIE(frame, afr_open_cbk, (void *)(long)i,
                                  priv->children[i],
                                  priv->children[i]->fops->open, &local->loc,
                                  (local->cont.open.flags & ~O_TRUNC),
                                  local->cont.open.fd, local->xdata_req);
                if (!--call_count)
                    break;
            }
        }
    }
    return 0;
}

int
afr_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    int spb_subvol = 0;
    int event_generation = 0;
    int ret = 0;
    int32_t op_errno = 0;
    afr_fd_ctx_t *fd_ctx = NULL;

    // We can't let truncation to happen outside transaction.

    priv = this->private;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    local->op = GF_FOP_OPEN;
    fd_ctx = afr_fd_ctx_get(fd, this);
    if (!fd_ctx) {
        op_errno = ENOMEM;
        goto out;
    }

    if (priv->quorum_count && !afr_has_quorum(local->child_up, this, NULL)) {
        op_errno = afr_quorum_errno(priv);
        goto out;
    }

    if (!afr_is_consistent_io_possible(local, priv, &op_errno))
        goto out;

    local->inode = inode_ref(loc->inode);
    loc_copy(&local->loc, loc);
    local->fd_ctx = fd_ctx;
    fd_ctx->flags = flags;
    if (xdata)
        local->xdata_req = dict_ref(xdata);

    local->cont.open.flags = flags;
    local->cont.open.fd = fd_ref(fd);

    ret = afr_inode_get_readable(frame, local->inode, this, NULL,
                                 &event_generation, AFR_DATA_TRANSACTION);
    if ((ret < 0) &&
        (afr_split_brain_read_subvol_get(local->inode, this, NULL,
                                         &spb_subvol) == 0) &&
        spb_subvol < 0) {
        afr_inode_refresh(frame, this, local->inode, local->inode->gfid,
                          afr_open_continue);
    } else {
        afr_open_continue(frame, this, 0);
    }

    return 0;
out:
    AFR_STACK_UNWIND(open, frame, -1, op_errno, fd, NULL);

    return 0;
}

static int
afr_openfd_fix_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, fd_t *fd,
                        dict_t *xdata)
{
    afr_local_t *local = NULL;
    afr_private_t *priv = NULL;
    afr_fd_ctx_t *fd_ctx = NULL;
    int call_count = 0;
    int child_index = (long)cookie;

    priv = this->private;
    local = frame->local;

    if (op_ret >= 0) {
        gf_msg_debug(this->name, 0,
                     "fd for %s opened "
                     "successfully on subvolume %s",
                     local->loc.path, priv->children[child_index]->name);
    } else {
        gf_smsg(this->name, fop_log_level(GF_FOP_OPEN, op_errno), op_errno,
                AFR_MSG_OPEN_FAIL, "path=%s", local->loc.path, "subvolume=%s",
                priv->children[child_index]->name, NULL);
    }

    fd_ctx = local->fd_ctx;

    LOCK(&local->fd->lock);
    {
        if (op_ret >= 0) {
            fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
        } else {
            fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
        }
    }
    UNLOCK(&local->fd->lock);

    call_count = afr_frame_return(frame);
    if (call_count == 0)
        AFR_STACK_DESTROY(frame);
    return 0;
}

static void
afr_fd_ctx_reset_need_open(fd_t *fd, xlator_t *this, unsigned char *need_open)
{
    afr_fd_ctx_t *fd_ctx = NULL;
    afr_private_t *priv = NULL;
    int i = 0;

    priv = this->private;
    fd_ctx = afr_fd_ctx_get(fd, this);
    if (!fd_ctx)
        return;

    LOCK(&fd->lock);
    {
        for (i = 0; i < priv->child_count; i++) {
            if (fd_ctx->opened_on[i] == AFR_FD_OPENING && need_open[i]) {
                fd_ctx->opened_on[i] = AFR_FD_NOT_OPENED;
                need_open[i] = 0;
            }
        }
    }
    UNLOCK(&fd->lock);
}

static int
afr_fd_ctx_set_need_open(fd_t *fd, xlator_t *this, unsigned char *need_open)
{
    afr_fd_ctx_t *fd_ctx = NULL;
    afr_private_t *priv = NULL;
    int i = 0;
    int count = 0;

    priv = this->private;
    fd_ctx = afr_fd_ctx_get(fd, this);
    if (!fd_ctx)
        return 0;

    LOCK(&fd->lock);
    {
        for (i = 0; i < priv->child_count; i++) {
            if (fd_ctx->opened_on[i] == AFR_FD_NOT_OPENED &&
                priv->child_up[i]) {
                fd_ctx->opened_on[i] = AFR_FD_OPENING;
                need_open[i] = 1;
                count++;
            } else {
                need_open[i] = 0;
            }
        }
    }
    UNLOCK(&fd->lock);

    return count;
}

static int
afr_do_fix_open(call_frame_t *frame, xlator_t *this)
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = NULL;
    int i = 0;
    int need_open_count = 0;

    priv = this->private;

    need_open_count = AFR_COUNT(local->need_open, priv->child_count);
    if (!need_open_count) {
        goto out;
    }
    gf_msg_debug(this->name, 0, "need open count: %d", need_open_count);
    local->call_count = need_open_count;

    for (i = 0; i < priv->child_count; i++) {
        if (!local->need_open[i])
            continue;

        if (IA_IFDIR == local->fd->inode->ia_type) {
            gf_msg_debug(this->name, 0, "opening fd for dir %s on subvolume %s",
                         local->loc.path, priv->children[i]->name);
            STACK_WIND_COOKIE(frame, afr_openfd_fix_open_cbk, (void *)(long)i,
                              priv->children[i],
                              priv->children[i]->fops->opendir, &local->loc,
                              local->fd, NULL);
        } else {
            gf_msg_debug(this->name, 0,
                         "opening fd for file %s on subvolume %s",
                         local->loc.path, priv->children[i]->name);

            STACK_WIND_COOKIE(
                frame, afr_openfd_fix_open_cbk, (void *)(long)i,
                priv->children[i], priv->children[i]->fops->open, &local->loc,
                local->fd_ctx->flags & ~(O_CREAT | O_EXCL | O_TRUNC), local->fd,
                NULL);
        }
        if (!--need_open_count)
            break;
    }
    return 0;

out:
    afr_fd_ctx_reset_need_open(local->fd, this, local->need_open);
    AFR_STACK_DESTROY(frame);
    return 0;
}

static int
afr_is_reopen_allowed_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct gf_flock *lock, dict_t *xdata)
{
    afr_local_t *local = frame->local;
    afr_private_t *priv = NULL;
    int ret = -1;
    int call_count = 0;
    int i = (long)cookie;
    int32_t fd_reopen_status = -1;
    int32_t final_reopen_status = -1;

    priv = this->private;
    local->replies[i].valid = 1;
    local->replies[i].op_ret = op_ret;
    local->replies[i].op_errno = op_errno;
    if (op_ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, AFR_MSG_DICT_GET_FAILED,
               "Failed getlk for %s", uuid_utoa(local->fd->inode->gfid));
    }

    if (xdata)
        local->replies[i].xdata = dict_ref(xdata);

    call_count = afr_frame_return(frame);

    if (call_count)
        return 0;

    /* Currently we get 3 values from the lower layer (protocol/client) in the
     * getlk_cbk.
     *  FD_REOPEN_ALLOWED : No conflicting locks are held and reopen is allowed
     *  FD_REOPEN_NOT_ALLOWED : Conflicting locks are held and reopen is not
     *                          allowed
     *  FD_BAD : FD is not valid
     *
     * - If we get FD_REOPEN_NOT_ALLOWED from any of the bricks, will block the
     *   reopen taking this as high priority.
     * - If we get FD_BAD from all the replies, we will not reopen since we do
     *   not know the correct status.
     * - If we get FD_BAD from few brick and FD_REOPEN_NOT_ALLOWED from one or
     *   more bricks, then we will block reopen.
     * - If we get FD_BAD from few bricks and FD_REOPEN_ALLOWED from one or
     *   more bricks, then we will allow the reopen.
     *
     *   We will update the final_reopen_status only when the value returned
     *   from lower layer is >= FD_REOPEN_ALLOWED and < FD_BAD. We will not set
     *   FD_BAD in final_reopen_status, since it can lead to unexpected
     *   behaviours.
     *
     *   At the end of this loop, if we still have final_reopen_status as -1
     *   i.e., the init value, it means we failed to get the fd status from any
     *   of the bricks or we do not have a valid fd on any of the bricks. We
     *   will not reopen the fd in this case as well.
     */

    for (i = 0; i < priv->child_count; i++) {
        if (final_reopen_status != FD_REOPEN_NOT_ALLOWED &&
            local->replies[i].xdata) {
            ret = dict_get_int32(xdata, "fd-reopen-status", &fd_reopen_status);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
                       "Failed to get whether reopen is allowed or not on fd "
                       "for file %s on subvolume %s.",
                       local->loc.path, priv->children[i]->name);
            } else if (fd_reopen_status >= FD_REOPEN_ALLOWED &&
                       fd_reopen_status < FD_BAD) {
                final_reopen_status = fd_reopen_status;
            }
        }

        if (final_reopen_status == FD_REOPEN_NOT_ALLOWED)
            break;
    }

    if (final_reopen_status == FD_REOPEN_NOT_ALLOWED) {
        gf_log(this->name, GF_LOG_INFO,
               "Conflicting locks held on file %s. FD reopen is not allowed.",
               local->loc.path);
    } else if (final_reopen_status == -1) {
        gf_log(this->name, GF_LOG_INFO,
               "Failed to get the lock information "
               "on file %s. FD reopen is not allowed.",
               local->loc.path);
    } else {
        afr_local_replies_wipe(local, priv);
        afr_do_fix_open(frame, this);
        return 0;
    }

    afr_fd_ctx_reset_need_open(local->fd, this, local->need_open);
    AFR_STACK_DESTROY(frame);
    return 0;
}

static void
afr_is_reopen_allowed(xlator_t *this, call_frame_t *frame)
{
    afr_private_t *priv = NULL;
    afr_local_t *local = NULL;
    dict_t *xdata = NULL;
    int i = 0;
    int call_count = 0;
    struct gf_flock flock = {
        0,
    };

    local = frame->local;
    priv = this->private;

    flock.l_type = F_WRLCK;
    afr_set_lk_owner(frame, this, frame->root);
    lk_owner_copy(&flock.l_owner, &frame->root->lk_owner);

    call_count = AFR_COUNT(local->child_up, priv->child_count);
    if (!call_count)
        goto out;
    local->call_count = call_count;

    xdata = dict_new();
    if (xdata == NULL)
        goto out;

    if (dict_set_int32(xdata, "fd-reopen-status", -1))
        goto out;

    for (i = 0; i < priv->child_count; i++) {
        if (local->child_up[i]) {
            STACK_WIND_COOKIE(frame, afr_is_reopen_allowed_cbk, (void *)(long)i,
                              priv->children[i], priv->children[i]->fops->lk,
                              local->fd, F_GETLK, &flock, xdata);
        } else {
            continue;
        }

        if (!--call_count)
            break;
    }

    dict_unref(xdata);
    return;

out:
    if (xdata)
        dict_unref(xdata);
    afr_fd_ctx_reset_need_open(local->fd, this, local->need_open);
    AFR_STACK_DESTROY(frame);
    return;
}

void
afr_fix_open(fd_t *fd, xlator_t *this)
{
    call_frame_t *frame = NULL;
    afr_local_t *local = NULL;
    int ret = -1;
    int32_t op_errno = 0;
    afr_fd_ctx_t *fd_ctx = NULL;
    int call_count = 0;

    if (!afr_is_fd_fixable(fd))
        goto out;

    fd_ctx = afr_fd_ctx_get(fd, this);
    if (!fd_ctx)
        goto out;

    frame = create_frame(this, this->ctx->pool);
    if (!frame)
        goto out;

    local = AFR_FRAME_INIT(frame, op_errno);
    if (!local)
        goto out;

    call_count = afr_fd_ctx_set_need_open(fd, this, local->need_open);
    if (!call_count)
        goto out;

    local->loc.inode = inode_ref(fd->inode);
    ret = loc_path(&local->loc, NULL);
    if (ret < 0)
        goto out;
    local->fd = fd_ref(fd);
    local->fd_ctx = fd_ctx;

    afr_is_reopen_allowed(this, frame);
    return;

out:
    if (call_count)
        afr_fd_ctx_reset_need_open(fd, this, local->need_open);
    if (frame)
        AFR_STACK_DESTROY(frame);
    return;
}
