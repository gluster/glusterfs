/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
                        struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t * local = frame->local;
        afr_private_t *priv = NULL;

        priv = this->private;
        if (afr_open_only_data_self_heal (priv->data_self_heal))
                afr_perform_data_self_heal (frame, this);
        AFR_STACK_UNWIND (open, frame, local->op_ret, local->op_errno,
                          local->fd, xdata);
        return 0;
}


int
afr_open_cbk (call_frame_t *frame, void *cookie,
              xlator_t *this, int32_t op_ret, int32_t op_errno,
              fd_t *fd, dict_t *xdata)
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
                                                    local->cont.open.flags);
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
                                    fd, 0, NULL);
                } else {
                        if (afr_open_only_data_self_heal (priv->data_self_heal))
                                afr_perform_data_self_heal (frame, this);
                        AFR_STACK_UNWIND (open, frame, local->op_ret,
                                          local->op_errno, local->fd, xdata);
                }
        }

        return 0;
}

int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
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

        if (afr_is_split_brain (this, loc->inode)) {
                /* self-heal failed */
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to open as split brain seen, returning EIO");
                op_errno = EIO;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count   = local->call_count;
        loc_copy (&local->loc, loc);

        local->cont.open.flags   = flags;

        local->fd = fd_ref (fd);

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_open_cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           loc, wind_flags, fd, xdata);

                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (open, frame, -1, op_errno, fd, xdata);

        return 0;
}

int
afr_openfd_fix_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, fd_t *fd,
                         dict_t *xdata)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        afr_fd_ctx_t  *fd_ctx     = NULL;
        int           call_count  = 0;
        int           child_index = (long) cookie;

        priv     = this->private;
        local    = frame->local;

        if (op_ret >= 0) {
                gf_log (this->name, GF_LOG_DEBUG, "fd for %s opened "
                        "successfully on subvolume %s", local->loc.path,
                        priv->children[child_index]->name);
        } else {
                gf_log (this->name, GF_LOG_ERROR, "Failed to open %s "
                        "on subvolume %s", local->loc.path,
                        priv->children[child_index]->name);
        }

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
                } else {
                        fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
                }
        }
        UNLOCK (&local->fd->lock);
out:
        call_count = afr_frame_return (frame);
        if (call_count == 0)
                AFR_STACK_DESTROY (frame);

        return 0;
}

void
afr_fix_open (xlator_t *this, fd_t *fd, size_t need_open_count, int *need_open)
{
        afr_private_t *priv    = NULL;
        int           i        = 0;
        call_frame_t  *frame   = NULL;
        afr_local_t   *local   = NULL;
        int           ret      = -1;
        int32_t       op_errno = 0;
        afr_fd_ctx_t  *fd_ctx  = NULL;

        priv  = this->private;

        if (!afr_is_fd_fixable (fd) || !need_open || !need_open_count)
                goto out;

        fd_ctx = afr_fd_ctx_get (fd, this);
        if (!fd_ctx) {
                ret = -1;
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;
        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->loc.inode = inode_ref (fd->inode);
        ret = loc_path (&local->loc, NULL);
        if (ret < 0)
                goto out;

        local->fd = fd_ref (fd);
        local->call_count = need_open_count;

        gf_log (this->name, GF_LOG_DEBUG, "need open count: %zd",
                need_open_count);

        for (i = 0; i < priv->child_count; i++) {
                if (!need_open[i])
                        continue;

                if (IA_IFDIR == fd->inode->ia_type) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "opening fd for dir %s on subvolume %s",
                                local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_fix_open_cbk,
                                           (void*) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->opendir,
                                           &local->loc, local->fd,
                                           NULL);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "opening fd for file %s on subvolume %s",
                                local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_fix_open_cbk,
                                           (void *)(long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           &local->loc,
                                           fd_ctx->flags & (~O_TRUNC),
                                           local->fd, NULL);
                }

        }
        op_errno = 0;
        ret = 0;
out:
        if (op_errno)
                ret = -1; //For handling ALLOC_OR_GOTO
        if (ret && frame)
                AFR_STACK_DESTROY (frame);
}
