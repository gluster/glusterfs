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
afr_stale_child_up (afr_local_t *local, xlator_t *this)
{
        int             i = 0;
        afr_private_t   *priv = NULL;
        int             up = -1;

        priv = this->private;

        if (!local->fresh_children)
                local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children)
                goto out;

        afr_inode_get_read_ctx (this, local->fd->inode, local->fresh_children);
        if (priv->child_count == afr_get_children_count (local->fresh_children,
                                                         priv->child_count))
                goto out;

        for (i = 0; i < priv->child_count; i++) {
                if (!local->child_up[i])
                        continue;
                if (afr_is_child_present (local->fresh_children,
                                          priv->child_count, i))
                        continue;
                up = i;
                break;
        }
out:
        return up;
}

void
afr_perform_data_self_heal (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        inode_t         *inode = NULL;
        int             st_child = -1;
        char            reason[64] = {0};

        local = frame->local;
        sh = &local->self_heal;
        inode = local->fd->inode;

        if (!IA_ISREG (inode->ia_type))
                goto out;

        st_child = afr_stale_child_up (local, this);
        if (st_child < 0)
                goto out;

        sh->do_data_self_heal          = _gf_true;
        sh->do_metadata_self_heal      = _gf_true;
        sh->do_gfid_self_heal          = _gf_true;
        sh->do_missing_entry_self_heal = _gf_true;

        snprintf (reason, sizeof (reason), "stale subvolume %d detected",
                  st_child);
        afr_launch_self_heal (frame, this, inode, _gf_true, inode->ia_type,
                              reason, NULL, NULL);
out:
        return;
}

int
afr_open_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf)
{
        afr_local_t * local = frame->local;
        afr_private_t *priv = NULL;

        priv = this->private;
        if (afr_open_only_data_self_heal (priv->data_self_heal))
                afr_perform_data_self_heal (frame, this);
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
        int            ret         = 0;
        int            call_count  = -1;
        int            child_index = (long) cookie;
        afr_private_t *priv        = NULL;

        priv = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        local->success_count++;

                        ret = afr_child_fd_ctx_set (this, fd, child_index,
                                                    local->cont.open.flags,
                                                    local->cont.open.wbflags);
                        if (ret) {
                                local->op_ret = -1;
                                local->op_errno = -ret;
                                goto unlock;
                        }
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
                        if (afr_open_only_data_self_heal (priv->data_self_heal))
                                afr_perform_data_self_heal (frame, this);
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
        int32_t         op_errno   = 0;
        int32_t         wind_flags = flags & (~O_TRUNC);
        //We can't let truncation to happen outside transaction.

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        if (flags & (O_CREAT|O_TRUNC)) {
                QUORUM_CHECK(open,out);
        }

        if (afr_is_split_brain (this, loc->inode)) {
                /* self-heal failed */
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to open as split brain seen, returning EIO");
                op_errno = EIO;
                goto out;
        }

        ALLOC_OR_GOTO (frame->local, afr_local_t, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

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

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (open, frame, -1, op_errno, fd);

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
        gf_boolean_t           fop_paused = _gf_false;

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

        fop_paused = local->fop_paused;
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
                //If the fop is paused then resume_calls will continue the fop
                if (fop_paused)
                        goto done;

                if (local->fop_call_continue)
                        local->fop_call_continue (frame, this);
                else
                        AFR_STACK_DESTROY (frame);
        }

done:
        return 0;
}

int
afr_fix_open (call_frame_t *frame, xlator_t *this, afr_fd_ctx_t *fd_ctx,
              int need_open_count, int *need_open)
{
        afr_local_t       *local = NULL;
        afr_private_t     *priv  = NULL;
        int               i      = 0;
        call_frame_t      *open_frame = NULL;
        afr_local_t      *open_local = NULL;
        int               ret    = -1;
        ia_type_t         ia_type = IA_INVAL;
        int32_t           op_errno = 0;

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
                ALLOC_OR_GOTO (open_frame->local, afr_local_t, out);
                open_local = open_frame->local;
                ret = afr_local_init (open_local, priv, &op_errno);
                if (ret < 0)
                        goto out;
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

        ia_type = open_local->fd->inode->ia_type;
        GF_ASSERT (ia_type != IA_INVAL);
        for (i = 0; i < priv->child_count; i++) {
                if (!need_open[i])
                        continue;
                if (IA_IFDIR == ia_type) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "opening fd for dir %s on subvolume %s",
                                local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_fix_open_cbk,
                                           (void*) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->opendir,
                                           &open_local->loc, open_local->fd);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "opening fd for file %s on subvolume %s",
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
        if (op_errno)
                ret = -op_errno;
        if (ret && open_frame)
                AFR_STACK_DESTROY (open_frame);
        return ret;
}
