/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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


#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"

#include "afr.h"
#include "afr-transaction.h"
#include "afr-self-heal-common.h"

/* {{{ writev */

int
afr_writev_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (writev, main_frame,
                                  local->op_ret, local->op_errno,
                                  &local->cont.writev.prebuf,
                                  &local->cont.writev.postbuf);
        }
        return 0;
}


int
afr_writev_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
        afr_local_t *   local = NULL;
        int child_index = (long) cookie;
        int call_count  = -1;
        int read_child  = 0;

        local = frame->local;

        read_child = afr_inode_get_read_ctx (this, local->fd->inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret              = op_ret;
                                local->cont.writev.prebuf  = *prebuf;
                                local->cont.writev.postbuf = *postbuf;
                        }

                        if (child_index == read_child) {
                                local->cont.writev.prebuf  = *prebuf;
                                local->cont.writev.postbuf = *postbuf;
                        }
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }
        return 0;
}

int
afr_writev_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int i = 0;
        int call_count = -1;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_writev_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->writev,
                                           local->fd,
                                           local->cont.writev.vector,
                                           local->cont.writev.count,
                                           local->cont.writev.offset,
                                           local->cont.writev.iobref);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_writev_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        iobref_unref (local->cont.writev.iobref);
        local->cont.writev.iobref = NULL;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_do_writev (call_frame_t *frame, xlator_t *this)
{
        call_frame_t    *transaction_frame = NULL;
        afr_local_t     *local             = NULL;
        int             op_ret   = -1;
        int             op_errno = 0;

        local = frame->local;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        transaction_frame->local = local;
        frame->local = NULL;

        local->op = GF_FOP_WRITE;

        local->success_count      = 0;

        local->transaction.fop    = afr_writev_wind;
        local->transaction.done   = afr_writev_done;
        local->transaction.unwind = afr_writev_unwind;

        local->transaction.main_frame = frame;
        if (local->fd->flags & O_APPEND) {
                local->transaction.start   = 0;
                local->transaction.len     = 0;
        } else {
                local->transaction.start   = local->cont.writev.offset;
                local->transaction.len     = iov_length (local->cont.writev.vector,
                                                         local->cont.writev.count);
        }

        afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (writev, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}

static int
afr_prepare_loc (call_frame_t *frame, fd_t *fd)
{
        afr_local_t    *local = NULL;
        char           *name = NULL;
        char           *path = NULL;
        int             ret = 0;

        if ((!fd) || (!fd->inode))
                return -1;

        local = frame->local;
        ret = inode_path (fd->inode, NULL, (char **)&path);
        if (ret <= 0) {
                gf_log (frame->this->name, GF_LOG_DEBUG,
                        "Unable to get path for gfid: %s",
                        uuid_utoa (fd->inode->gfid));
                return -1;
        }

        if (local->loc.path) {
                if (strcmp (path, local->loc.path))
                        gf_log (frame->this->name, GF_LOG_DEBUG,
                                "overwriting old loc->path %s with %s",
                                local->loc.path, path);
                GF_FREE ((char *)local->loc.path);
        }
        local->loc.path = path;

        name = strrchr (local->loc.path, '/');
        if (name)
                name++;
        local->loc.name = name;

        if (local->loc.inode) {
                inode_unref (local->loc.inode);
        }
        local->loc.inode = inode_ref (fd->inode);

        if (local->loc.parent) {
                inode_unref (local->loc.parent);
        }

        local->loc.parent = inode_parent (local->loc.inode, 0, NULL);

        return 0;
}

afr_fd_paused_call_t*
afr_paused_call_create (call_frame_t *frame)
{
        afr_local_t             *local = NULL;
        afr_fd_paused_call_t   *paused_call = NULL;

        local = frame->local;
        GF_ASSERT (local->fop_call_continue);

        paused_call = GF_CALLOC (1, sizeof (*paused_call),
                                  gf_afr_fd_paused_call_t);
        if (paused_call) {
                INIT_LIST_HEAD (&paused_call->call_list);
                paused_call->frame = frame;
        }

        return paused_call;
}

static int
afr_pause_fd_fop (call_frame_t *frame, xlator_t *this, afr_fd_ctx_t *fd_ctx)
{
        afr_fd_paused_call_t *paused_call = NULL;
        int                    ret = 0;

        paused_call = afr_paused_call_create (frame);
        if (paused_call)
                list_add (&paused_call->call_list, &fd_ctx->paused_calls);
        else
                ret = -ENOMEM;

        return ret;
}

static void
afr_trigger_open_fd_self_heal (call_frame_t *frame, xlator_t *this)
{
        afr_local_t             *local = NULL;
        afr_self_heal_t         *sh = NULL;
        inode_t                 *inode = NULL;
        char                    *reason = NULL;

        local = frame->local;
        sh    = &local->self_heal;
        inode = local->fd->inode;

        sh->do_missing_entry_self_heal = _gf_true;
        sh->do_gfid_self_heal = _gf_true;
        sh->do_data_self_heal = _gf_true;

        reason = "subvolume came online";
        afr_launch_self_heal (frame, this, inode, _gf_true, inode->ia_type,
                              reason, NULL, NULL);
}

int
afr_open_fd_fix (call_frame_t *frame, xlator_t *this, gf_boolean_t pause_fop)
{
        int                     ret = 0;
        int                     i   = 0;
        afr_fd_ctx_t            *fd_ctx = NULL;
        gf_boolean_t            need_self_heal = _gf_false;
        int                     *need_open = NULL;
        int                     need_open_count = 0;
        afr_local_t             *local = NULL;
        afr_private_t           *priv = NULL;
        gf_boolean_t            fop_continue = _gf_true;

        local = frame->local;
        priv  = this->private;

        GF_ASSERT (local->fd);
        if (pause_fop)
                GF_ASSERT (local->fop_call_continue);

        ret = afr_prepare_loc (frame, local->fd);
        if (ret < 0) {
                //File does not exist we cant open it.
                ret = 0;
                goto out;
        }

        fd_ctx = afr_fd_ctx_get (local->fd, this);
        if (!fd_ctx) {
                ret = -EINVAL;
                goto unlock;
        }

        LOCK (&local->fd->lock);
        {
                if (fd_ctx->up_count < priv->up_count) {
                        need_self_heal = _gf_true;
                        fd_ctx->up_count   = priv->up_count;
                        fd_ctx->down_count = priv->down_count;
                }

                for (i = 0; i < priv->child_count; i++) {
                        if ((fd_ctx->opened_on[i] == AFR_FD_NOT_OPENED) &&
                            local->child_up[i]) {
                                fd_ctx->opened_on[i] = AFR_FD_OPENING;
                                if (!need_open)
                                        need_open = GF_CALLOC (priv->child_count,
                                                               sizeof (*need_open),
                                                               gf_afr_mt_int32_t);
                                need_open[i] = 1;
                                need_open_count++;
                        } else if (pause_fop && local->child_up[i] &&
                                   (fd_ctx->opened_on[i] == AFR_FD_OPENING)) {
                                local->fop_paused = _gf_true;
                        }
                }

                if (local->fop_paused) {
                        GF_ASSERT (pause_fop);
                        gf_log (this->name, GF_LOG_INFO, "Pause fd %p",
                                local->fd);
                        ret = afr_pause_fd_fop (frame, this, fd_ctx);
                        if (ret)
                                goto unlock;
                        fop_continue = _gf_false;
                }
        }
unlock:
        UNLOCK (&local->fd->lock);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to fix fd for %s",
                        local->loc.path);
                fop_continue = _gf_false;
                goto out;
        }

        if (need_self_heal)
                afr_trigger_open_fd_self_heal (frame, this);

        if (!need_open_count)
                goto out;

        gf_log (this->name, GF_LOG_INFO, "Opening fd %p", local->fd);
        afr_fix_open (frame, this, fd_ctx, need_open_count, need_open);
        fop_continue = _gf_false;
out:
        if (need_open)
                GF_FREE (need_open);
        if (fop_continue && local->fop_call_continue)
                local->fop_call_continue (frame, this);
        return ret;
}

int
afr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            struct iobref *iobref)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        frame->local = local;

        local->cont.writev.vector     = iov_dup (vector, count);
        local->cont.writev.count      = count;
        local->cont.writev.offset     = offset;
        local->cont.writev.iobref     = iobref_ref (iobref);

        local->fd                = fd_ref (fd);
        local->fop_call_continue = afr_do_writev;

        ret = afr_open_fd_fix (frame, this, _gf_true);
        if (ret) {
                op_errno = -ret;
                goto out;
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (writev, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}


/* }}} */

/* {{{ truncate */

int
afr_truncate_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (truncate, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.truncate.prebuf,
                                  &local->cont.truncate.postbuf);
        }

        return 0;
}


int
afr_truncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int child_index = (long) cookie;
        int read_child  = 0;
        int call_count  = -1;
        int need_unwind = 0;

        local = frame->local;
        priv  = this->private;

        read_child = afr_inode_get_read_ctx (this, local->loc.inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno) && op_errno != EFBIG)
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                                local->cont.truncate.prebuf  = *prebuf;
                                local->cont.truncate.postbuf = *postbuf;
                        }

                        if (child_index == read_child) {
                                local->cont.truncate.prebuf  = *prebuf;
                                local->cont.truncate.postbuf = *postbuf;
                        }

                        local->success_count++;

                        if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
                                need_unwind = 1;
                        }
                }
                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                local->transaction.unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int32_t
afr_truncate_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_truncate_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->truncate,
                                           &local->loc,
                                           local->cont.truncate.offset);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_truncate_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_truncate (call_frame_t *frame, xlator_t *this,
              loc_t *loc, off_t offset)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        transaction_frame->local = local;

        local->op_ret = -1;

        local->cont.truncate.offset  = offset;

        local->transaction.fop    = afr_truncate_wind;
        local->transaction.done   = afr_truncate_done;
        local->transaction.unwind = afr_truncate_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = offset;
        local->transaction.len     = 0;

        afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (truncate, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}


/* }}} */

/* {{{ ftruncate */


int
afr_ftruncate_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (ftruncate, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.ftruncate.prebuf,
                                  &local->cont.ftruncate.postbuf);
        }
        return 0;
}


int
afr_ftruncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int child_index = (long) cookie;
        int call_count  = -1;
        int need_unwind = 0;
        int read_child  = 0;

        local = frame->local;
        priv  = this->private;

        read_child = afr_inode_get_read_ctx (this, local->fd->inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                                local->cont.ftruncate.prebuf  = *prebuf;
                                local->cont.ftruncate.postbuf = *postbuf;
                        }

                        if (child_index == read_child) {
                                local->cont.ftruncate.prebuf  = *prebuf;
                                local->cont.ftruncate.postbuf = *postbuf;
                        }

                        local->success_count++;

                        if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
                                need_unwind = 1;
                        }
                }
                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                local->transaction.unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_ftruncate_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_ftruncate_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->ftruncate,
                                           local->fd, local->cont.ftruncate.offset);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_ftruncate_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_do_ftruncate (call_frame_t *frame, xlator_t *this)
{
        call_frame_t * transaction_frame = NULL;
        afr_local_t *  local             = NULL;
        int op_ret   = -1;
        int op_errno = 0;

        local = frame->local;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        transaction_frame->local = local;
        frame->local = NULL;

        local->op = GF_FOP_FTRUNCATE;

        local->transaction.fop    = afr_ftruncate_wind;
        local->transaction.done   = afr_ftruncate_done;
        local->transaction.unwind = afr_ftruncate_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.ftruncate.offset;
        local->transaction.len     = 0;

        afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}


int
afr_ftruncate (call_frame_t *frame, xlator_t *this,
               fd_t *fd, off_t offset)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);
        ret = AFR_LOCAL_INIT (local, priv);

        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        frame->local = local;

        local->cont.ftruncate.offset  = offset;

        local->fd = fd_ref (fd);
        local->fop_call_continue = afr_do_ftruncate;

        ret = afr_open_fd_fix (frame, this, _gf_true);
        if (ret) {
                op_errno = -ret;
                goto out;
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ setattr */

int
afr_setattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (setattr, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.setattr.preop_buf,
                                  &local->cont.setattr.postop_buf);
        }

        return 0;
}


int
afr_setattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *preop, struct iatt *postop)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int child_index = (long) cookie;
        int read_child  = 0;
        int call_count  = -1;
        int need_unwind = 0;

        local = frame->local;
        priv  = this->private;

        read_child = afr_inode_get_read_ctx (this, local->loc.inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                                local->cont.setattr.preop_buf  = *preop;
                                local->cont.setattr.postop_buf = *postop;
                        }

                        if (child_index == read_child) {
                                local->cont.setattr.preop_buf  = *preop;
                                local->cont.setattr.postop_buf = *postop;
                        }

                        local->success_count++;

                        if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
                                need_unwind = 1;
                        }
                }
                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                local->transaction.unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int32_t
afr_setattr_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_setattr_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->setattr,
                                           &local->loc,
                                           &local->cont.setattr.in_buf,
                                           local->cont.setattr.valid);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_setattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_setattr (call_frame_t *frame, xlator_t *this,
             loc_t *loc, struct iatt *buf, int32_t valid)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        transaction_frame->local = local;

        local->op_ret = -1;

        local->cont.setattr.in_buf = *buf;
        local->cont.setattr.valid  = valid;

        local->transaction.fop    = afr_setattr_wind;
        local->transaction.done   = afr_setattr_done;
        local->transaction.unwind = afr_setattr_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (setattr, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}

/* {{{ fsetattr */

int
afr_fsetattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (fsetattr, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.fsetattr.preop_buf,
                                  &local->cont.fsetattr.postop_buf);
        }

        return 0;
}


int
afr_fsetattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int child_index = (long) cookie;
        int read_child  = 0;
        int call_count  = -1;
        int need_unwind = 0;

        local = frame->local;
        priv  = this->private;

        read_child = afr_inode_get_read_ctx (this, local->fd->inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                                local->cont.fsetattr.preop_buf  = *preop;
                                local->cont.fsetattr.postop_buf = *postop;
                        }

                        if (child_index == read_child) {
                                local->cont.fsetattr.preop_buf  = *preop;
                                local->cont.fsetattr.postop_buf = *postop;
                        }

                        local->success_count++;

                        if ((local->success_count >= priv->wait_count)
                            && local->read_child_returned) {
                                need_unwind = 1;
                        }
                }
                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                local->transaction.unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int32_t
afr_fsetattr_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_fsetattr_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fsetattr,
                                           local->fd,
                                           &local->cont.fsetattr.in_buf,
                                           local->cont.fsetattr.valid);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_fsetattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

int
afr_fsetattr (call_frame_t *frame, xlator_t *this,
              fd_t *fd, struct iatt *buf, int32_t valid)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        ALLOC_OR_GOTO (local, afr_local_t, out);
        transaction_frame->local = local;

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        local->op_ret = -1;

        local->cont.fsetattr.in_buf = *buf;
        local->cont.fsetattr.valid  = valid;

        local->transaction.fop    = afr_fsetattr_wind;
        local->transaction.done   = afr_fsetattr_done;
        local->transaction.unwind = afr_fsetattr_unwind;

        local->fd                 = fd_ref (fd);

        op_ret = afr_open_fd_fix (transaction_frame, this, _gf_false);
        if (ret) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, NULL, NULL);
        }

        return 0;
}


/* {{{ setxattr */


int
afr_setxattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (setxattr, main_frame,
                                  local->op_ret, local->op_errno)
                        }
        return 0;
}


int
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int call_count  = -1;
        int need_unwind = 0;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                        }
                        local->success_count++;

                        if (local->success_count == priv->child_count) {
                                need_unwind = 1;
                        }
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                local->transaction.unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_setxattr_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_setxattr_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->setxattr,
                                           &local->loc,
                                           local->cont.setxattr.dict,
                                           local->cont.setxattr.flags);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_setxattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

int
afr_setxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *dict, int32_t flags)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        transaction_frame->local = local;

        local->op_ret = -1;

        local->cont.setxattr.dict  = dict_ref (dict);
        local->cont.setxattr.flags = flags;

        local->transaction.fop    = afr_setxattr_wind;
        local->transaction.done   = afr_setxattr_done;
        local->transaction.unwind = afr_setxattr_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (setxattr, frame, op_ret, op_errno);
        }

        return 0;
}

/* }}} */

/* {{{ removexattr */


int
afr_removexattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (removexattr, main_frame,
                                  local->op_ret, local->op_errno)
                        }
        return 0;
}


int
afr_removexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        int call_count  = -1;
        int need_unwind = 0;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                        }
                        local->success_count++;

                        if (local->success_count == priv->wait_count) {
                                need_unwind = 1;
                        }
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        if (need_unwind)
                local->transaction.unwind (frame, this);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}


int32_t
afr_removexattr_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_removexattr_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->removexattr,
                                           &local->loc,
                                           local->cont.removexattr.name);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_removexattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_removexattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, const char *name)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        transaction_frame->local = local;

        local->op_ret = -1;

        local->cont.removexattr.name = gf_strdup (name);

        local->transaction.fop    = afr_removexattr_wind;
        local->transaction.done   = afr_removexattr_done;
        local->transaction.unwind = afr_removexattr_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (removexattr, frame, op_ret, op_errno);
        }

        return 0;
}
