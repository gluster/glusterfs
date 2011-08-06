/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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

                        fd_ctx->opened_on[child_index] = 1;
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


int
afr_openfd_sh_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_local_t         *local       = NULL;
        afr_private_t       *priv        = NULL;
        afr_fd_ctx_t        *fd_ctx      = NULL;
        uint64_t             ctx         = 0;
        int                  ret         = 0;
        int                  call_count  = 0;
        int                  child_index = (long) cookie;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        LOCK (&frame->lock);
        {
                if (op_ret >= 0) {
                        ret = fd_ctx_get (fd, this, &ctx);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to get fd context, %p", fd);
                                goto out;
                        }

                        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                        fd_ctx->opened_on[child_index] = 1;

                        gf_log (this->name, GF_LOG_TRACE,
                                "fd for %s opened successfully on subvolume %s",
                                local->loc.path, priv->children[child_index]->name);
                }
        }
out:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                int_lock->lock_cbk = local->transaction.done;
                local->transaction.resume (frame, this);
        }

        return 0;
}


static int
__unopened_count (int child_count, unsigned int *opened_on, unsigned char *child_up)
{
        int i = 0;
        int count = 0;

        for (i = 0; i < child_count; i++) {
                if (!opened_on[i] && child_up[i])
                        count++;
        }

        return count;
}


int
afr_openfd_sh_unwind (call_frame_t *frame, xlator_t *this, int32_t op_ret,
                      int32_t op_errno)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        uint64_t       ctx        = 0;
        afr_fd_ctx_t  *fd_ctx     = NULL;
        int            abandon    = 0;
        int            ret        = 0;
        int            i          = 0;
        int            call_count = 0;

        priv  = this->private;
        local = frame->local;

        /*
         * Some subvolumes might have come up on which we never
         * opened this fd in the first place. Re-open fd's on those
         * subvolumes now.
         */

        ret = fd_ctx_get (local->fd, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to get fd context %p (%s)",
                        local->fd, local->loc.path);
                abandon = 1;
                goto out;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        LOCK (&local->fd->lock);
        {
                call_count = __unopened_count (priv->child_count,
                                               fd_ctx->opened_on,
                                               local->child_up);
                for (i = 0; i < priv->child_count; i++) {
                        fd_ctx->pre_op_done[i] = 0;
                        fd_ctx->pre_op_piggyback[i] = 0;
                }
        }
        UNLOCK (&local->fd->lock);

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "fd not open on any subvolume %p (%s)",
                        local->fd, local->loc.path);
                abandon = 1;
                goto out;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (!fd_ctx->opened_on[i] && local->child_up[i]) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "opening fd for %s on subvolume %s",
                                local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_sh_open_cbk,
                                           (void *)(long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           &local->loc, fd_ctx->flags, local->fd,
                                           fd_ctx->wbflags);

                        if (!--call_count)
                                break;
                }
        }

out:
        if (abandon)
                local->transaction.resume (frame, this);

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


int
afr_openfd_sh (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh = NULL;
        char            sh_type_str[256] = {0,};

        local = frame->local;
        sh    = &local->self_heal;

        GF_ASSERT (local->loc.path);
        /* forcibly trigger missing-entries self-heal */

        sh->need_missing_entry_self_heal = _gf_true;
        sh->need_gfid_self_heal = _gf_true;
        sh->data_lock_held      = _gf_true;
        sh->need_data_self_heal = _gf_true;
        sh->type                = local->fd->inode->ia_type;
        sh->background          = _gf_false;
        sh->unwind              = afr_openfd_sh_unwind;

        afr_self_heal_type_str_get(&local->self_heal,
                                   sh_type_str,
                                   sizeof(sh_type_str));
        gf_log (this->name, GF_LOG_INFO, "%s self-heal triggered. "
                "path: %s, reason: Replicate up down flush, data lock is held",
                sh_type_str, local->loc.path);

        afr_self_heal (frame, this, local->fd->inode);

        return 0;
}


int
afr_openfd_flush_done (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;

        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        int _ret = -1;

        priv  = this->private;
        local = frame->local;

        LOCK (&local->fd->lock);
        {
                _ret = __fd_ctx_get (local->fd, this, &ctx);
                if (_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to get fd context %p (%s)",
                                local->fd, local->loc.path);
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                fd_ctx->down_count = priv->down_count;
                fd_ctx->up_count   = priv->up_count;
        }
out:
        UNLOCK (&local->fd->lock);

        afr_local_transaction_cleanup (local, this);

        gf_log (this->name, GF_LOG_TRACE,
                "The up/down flush is over");

        fd_unref (local->fd);
        local->openfd_flush_cbk (frame, this);

        return 0;
}



int
afr_openfd_xaction (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        afr_local_t   * local = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        local = frame->local;

        local->op = GF_FOP_FLUSH;

        local->transaction.fop    = afr_openfd_sh;
        local->transaction.done   = afr_openfd_flush_done;

        local->transaction.start  = 0;
        local->transaction.len    = 0;

        gf_log (this->name, GF_LOG_TRACE,
                "doing up/down flush on fd=%p", fd);

        afr_transaction (frame, this, AFR_DATA_TRANSACTION);

out:
        return 0;
}



int
afr_openfd_xaction_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        afr_internal_lock_t *int_lock    = NULL;
        afr_local_t         *local       = NULL;
        afr_private_t       *priv        = NULL;
        int                  ret         = 0;
        uint64_t             ctx         = 0;
        afr_fd_ctx_t        *fd_ctx      = NULL;
        int                  call_count  = 0;
        int                  child_index = (long) cookie;

        priv     = this->private;
        local    = frame->local;
        int_lock = &local->internal_lock;

        LOCK (&frame->lock);
        {
                if (op_ret >= 0) {
                        ret = fd_ctx_get (fd, this, &ctx);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to get fd context %p (%s)",
                                        fd, local->loc.path);
                                goto out;
                        }

                        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                        fd_ctx->opened_on[child_index] = 1;

                        gf_log (this->name, GF_LOG_TRACE,
                                "fd for %s opened successfully on subvolume %s",
                                local->loc.path, priv->children[child_index]->name);
                }
        }
out:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_openfd_xaction (frame, this, local->fd);
        }

        return 0;
}


int
afr_openfd_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        uint64_t       ctx        = 0;
        afr_fd_ctx_t  *fd_ctx     = NULL;
        int            no_open    = 0;
        int            ret        = 0;
        int            i          = 0;
        int            call_count = 0;

        priv  = this->private;
        local = frame->local;

        /*
         * If the file is already deleted while the fd is open, no need to
         * perform the openfd flush, call the flush_cbk and get out.
         */
        ret = afr_prepare_loc (frame, fd);
        if (ret < 0) {
                local->openfd_flush_cbk (frame, this);
                goto out;
        }

        /*
         * Some subvolumes might have come up on which we never
         * opened this fd in the first place. Re-open fd's on those
         * subvolumes now.
         */

        local->fd = fd_ref (fd);

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to get fd context %p (%s)",
                        fd, local->loc.path);
                no_open = 1;
                goto out;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        LOCK (&local->fd->lock);
        {
                call_count = __unopened_count (priv->child_count,
                                               fd_ctx->opened_on,
                                               local->child_up);
        }
        UNLOCK (&local->fd->lock);

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "fd not open on any subvolume %p (%s)",
                        fd, local->loc.path);
                no_open = 1;
                goto out;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (!fd_ctx->opened_on[i] && local->child_up[i]) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "opening fd for %s on subvolume %s",
                                local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_xaction_open_cbk,
                                           (void *)(long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           &local->loc, fd_ctx->flags, fd,
                                           fd_ctx->wbflags);

                        if (!--call_count)
                                break;
                }
        }

out:
        if (no_open)
                afr_openfd_xaction (frame, this, fd);

        return 0;
}
