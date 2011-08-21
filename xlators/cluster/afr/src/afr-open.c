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
#include "byte-order.h"
#include "statedump.h"

#include "fd.h"

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"


int
afr_open_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf)
{
        afr_local_t * local = frame->local;

        AFR_STACK_UNWIND (open, frame, local->op_ret, local->op_errno,
                          local->fd);
        return 0;
}


int
afr_open_cbk (call_frame_t *frame, void *cookie,
              xlator_t *this, int32_t op_ret, int32_t op_errno,
              fd_t *fd)
{
        afr_local_t *  local       = NULL;
        uint64_t       ctx         = 0;
        afr_fd_ctx_t  *fd_ctx      = NULL;
        int            ret         = 0;
        int            call_count  = -1;
        int            child_index = (long) cookie;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        local->success_count++;

                        ret = afr_fd_ctx_set (this, fd);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not set fd ctx for fd=%p", fd);

                                local->op_ret   = -1;
                                local->op_errno = -ret;
                                goto unlock;
                        }

                        ret = fd_ctx_get (fd, this, &ctx);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not get fd ctx for fd=%p", fd);
                                local->op_ret   = -1;
                                local->op_errno = -ret;
                                goto unlock;
                        }

                        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                        fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
                        fd_ctx->flags                  = local->cont.open.flags;
                        fd_ctx->wbflags                = local->cont.open.wbflags;
                }
        }
unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if ((local->cont.open.flags & O_TRUNC)
                    && (local->op_ret >= 0)) {
                        STACK_WIND (frame, afr_open_ftruncate_cbk,
                                    this, this->fops->ftruncate,
                                    fd, 0);
                } else {
                        AFR_STACK_UNWIND (open, frame, local->op_ret,
                                          local->op_errno, local->fd);
                }
        }

        return 0;
}

int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, int32_t wbflags)
{
        afr_private_t * priv       = NULL;
        afr_local_t *   local      = NULL;
        int             i          = 0;
        int             ret        = -1;
        int32_t         call_count = 0;
        int32_t         op_ret     = -1;
        int32_t         op_errno   = 0;
        int32_t         wind_flags = flags & (~O_TRUNC);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        if (afr_is_split_brain (this, loc->inode)) {
                /* self-heal failed */
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to open as split brain seen, returning EIO");
                op_errno = EIO;
                goto out;
        }

        ALLOC_OR_GOTO (local, afr_local_t, out);

        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        frame->local = local;
        call_count   = local->call_count;
        loc_copy (&local->loc, loc);

        local->cont.open.flags   = flags;
        local->cont.open.wbflags = wbflags;

        local->fd = fd_ref (fd);

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_open_cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           loc, wind_flags, fd, wbflags);

                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (open, frame, op_ret, op_errno, fd);
        }

        return 0;
}

//NOTE: this function should be called with holding the lock on
//fd to which fd_ctx belongs
void
afr_get_resumable_calls (xlator_t *this, afr_fd_ctx_t *fd_ctx,
                         struct list_head *list)
{
        afr_fd_paused_call_t *paused_call = NULL;
        afr_fd_paused_call_t *tmp = NULL;
        afr_local_t           *call_local  = NULL;
        afr_private_t         *priv        = NULL;
        int                    i = 0;
        gf_boolean_t           call = _gf_false;

        priv = this->private;
        list_for_each_entry_safe (paused_call, tmp, &fd_ctx->paused_calls,
                                  call_list) {
                call = _gf_true;
                call_local = paused_call->frame->local;
                for (i = 0; i < priv->child_count; i++) {
                        if (call_local->child_up[i] &&
                            (fd_ctx->opened_on[i] == AFR_FD_OPENING))
                                call = _gf_false;
                }

                if (call) {
                        list_del_init (&paused_call->call_list);
                        list_add (&paused_call->call_list, list);
                }
        }
}

void
afr_resume_calls (xlator_t *this, struct list_head *list)
{
        afr_fd_paused_call_t *paused_call = NULL;
        afr_fd_paused_call_t *tmp = NULL;
        afr_local_t           *call_local  = NULL;

        list_for_each_entry_safe (paused_call, tmp, list, call_list) {
                list_del_init (&paused_call->call_list);
                call_local = paused_call->frame->local;
                call_local->fop_call_continue (paused_call->frame, this);
                GF_FREE (paused_call);
        }
}

int
afr_openfd_fix_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        afr_local_t           *local       = NULL;
        afr_private_t         *priv        = NULL;
        afr_fd_ctx_t          *fd_ctx      = NULL;
        int                    call_count  = 0;
        int                    child_index = (long) cookie;
        struct list_head       paused_calls = {0};

        priv     = this->private;
        local    = frame->local;

        call_count = afr_frame_return (frame);

        //Note: No frame locking needed for this block of code
        fd_ctx = afr_fd_ctx_get (local->fd, this);
        if (!fd_ctx) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to get fd context, %p", local->fd);
                goto out;
        }

        LOCK (&local->fd->lock);
        {
                if (op_ret >= 0) {
                        fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
                        gf_log (this->name, GF_LOG_INFO, "fd for %s opened "
                                "successfully on subvolume %s", local->loc.path,
                                priv->children[child_index]->name);
                } else {
                        //Change open status from OPENING to NOT OPENED.
                        fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
                }
                if (call_count == 0) {
                        INIT_LIST_HEAD (&paused_calls);
                        afr_get_resumable_calls (this, fd_ctx, &paused_calls);
                }
        }
        UNLOCK (&local->fd->lock);
out:
        if (call_count == 0) {
                afr_resume_calls (this, &paused_calls);
                if (local->fop_call_continue)
                        local->fop_call_continue (frame, this);
                else
                        AFR_STACK_DESTROY (frame);
        }

        return 0;
}

int
afr_fix_open (call_frame_t *frame, xlator_t *this, afr_fd_ctx_t *fd_ctx,
              int need_open_count, int *need_open)
{
        afr_local_t     *local = NULL;
        afr_private_t   *priv  = NULL;
        int             i      = 0;
        call_frame_t    *open_frame = NULL;
        afr_local_t    *open_local = NULL;
        int             ret    = -1;
        int32_t         op_errno = 0;

        GF_ASSERT (fd_ctx);
        GF_ASSERT (need_open_count > 0);
        GF_ASSERT (need_open);

        local = frame->local;
        priv  = this->private;
        if (!local->fop_call_continue) {
                open_frame = copy_frame (frame);
                if (!open_frame) {
                        ret = -ENOMEM;
                        goto out;
                }
                ALLOC_OR_GOTO (open_local, afr_local_t, out);
                open_frame->local = open_local;
                ret = AFR_LOCAL_INIT (open_local, priv);
                if (ret < 0) {
                        op_errno = -ret;
                        goto out;
                }
                loc_copy (&open_local->loc, &local->loc);
                open_local->fd = fd_ref (local->fd);
        } else {
                ret = 0;
                open_frame = frame;
                open_local = local;
        }

        open_local->call_count = need_open_count;

        gf_log (this->name, GF_LOG_DEBUG, "need open count: %d",
                need_open_count);

        for (i = 0; i < priv->child_count; i++) {
                if (need_open[i]) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "opening fd for %s on subvolume %s",
                                local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (open_frame, afr_openfd_fix_open_cbk,
                                           (void *)(long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           &open_local->loc, fd_ctx->flags,
                                           open_local->fd, fd_ctx->wbflags);

                }
        }
out:
        if (ret && open_frame)
                AFR_STACK_DESTROY (open_frame);
        return ret;
}
