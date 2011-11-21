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

void
afr_build_parent_loc (loc_t *parent, loc_t *child)
{
        char *tmp = NULL;

        if (!child->parent) {
                //this should never be called with root as the child
                GF_ASSERT (0);
                loc_copy (parent, child);
                return;
        }

        tmp = gf_strdup (child->path);
        parent->path   = gf_strdup (dirname (tmp));
        GF_FREE (tmp);

        parent->name   = strrchr (parent->path, '/');
        if (parent->name)
                parent->name++;

        parent->inode  = inode_ref (child->parent);
        parent->parent = inode_parent (parent->inode, 0, NULL);

        if (!uuid_is_null (child->pargfid))
                uuid_copy (parent->gfid, child->pargfid);
}

/* {{{ create */

int
afr_create_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;
        struct iatt  *unwind_buf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                if (local->cont.create.read_child_buf.ia_ino) {
                        unwind_buf = &local->cont.create.read_child_buf;
                } else {
                        unwind_buf = &local->cont.create.buf;
                }

                AFR_STACK_UNWIND (create, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.create.fd,
                                  local->cont.create.inode,
                                  unwind_buf, &local->cont.create.preparent,
                                  &local->cont.create.postparent);
        }

        return 0;
}


int
afr_create_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     fd_t *fd, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent)
{
        afr_local_t     *local = NULL;
        afr_private_t   *priv  = NULL;
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx = NULL;
        int             ret = 0;
        int             call_count = -1;
        int             child_index = -1;
        int32_t         *fresh_children = NULL;

        local = frame->local;
        priv  = this->private;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        local->op_ret = op_ret;

                        ret = afr_fd_ctx_set (this, fd);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not set ctx on fd=%p", fd);

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
                        fd_ctx->flags                  = local->cont.create.flags;

                        if (local->success_count == 0)
                                local->cont.create.buf        = *buf;

                        if (child_index == local->read_child_index) {
                                local->cont.create.read_child_buf = *buf;
                                local->cont.create.preparent      = *preparent;
                                local->cont.create.postparent     = *postparent;
                        }

                        local->cont.create.inode = inode;

                        fresh_children = local->fresh_children;
                        fresh_children[local->success_count] = child_index;
                        local->success_count++;
                }

                local->op_errno = op_errno;
        }

unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_set_read_ctx_from_policy (this, inode,
                                              local->fresh_children,
                                              local->read_child_index,
                                              priv->read_child);
                local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_create_wind (call_frame_t *frame, xlator_t *this)
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
                        STACK_WIND_COOKIE (frame, afr_create_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->create,
                                           &local->loc,
                                           local->cont.create.flags,
                                           local->cont.create.mode,
                                           local->cont.create.fd,
                                           local->cont.create.params);
                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_create_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_create (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, mode_t mode,
            fd_t *fd, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(create,out);

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

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.create.flags = flags;
        local->cont.create.mode  = mode;
        local->cont.create.fd    = fd_ref (fd);
        if (params)
                local->cont.create.params = dict_ref (params);

        local->transaction.fop    = afr_create_wind;
        local->transaction.done   = afr_create_done;
        local->transaction.unwind = afr_create_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (create, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ mknod */

int
afr_mknod_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;
        struct iatt *unwind_buf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                if (local->cont.mknod.read_child_buf.ia_ino) {
                        unwind_buf = &local->cont.mknod.read_child_buf;
                } else {
                        unwind_buf = &local->cont.mknod.buf;
                }

                AFR_STACK_UNWIND (mknod, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.mknod.inode,
                                  unwind_buf, &local->cont.mknod.preparent,
                                  &local->cont.mknod.postparent);
        }

        return 0;
}


int
afr_mknod_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
        afr_local_t     *local          = NULL;
        afr_private_t   *priv           = NULL;
        int             call_count      = -1;
        int             child_index     = -1;
        int32_t         *fresh_children = NULL;

        local = frame->local;
        priv = this->private;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        local->op_ret = op_ret;

                        if (local->success_count == 0)
                                local->cont.mknod.buf   = *buf;

                        if (child_index == local->read_child_index) {
                                local->cont.mknod.read_child_buf = *buf;
                                local->cont.mknod.preparent      = *preparent;
                                local->cont.mknod.postparent     = *postparent;
                        }

                        local->cont.mknod.inode = inode;

                        fresh_children = local->fresh_children;
                        fresh_children[local->success_count] = child_index;
                        local->success_count++;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_set_read_ctx_from_policy (this, inode,
                                              local->fresh_children,
                                              local->read_child_index,
                                              priv->read_child);
                local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
}


int32_t
afr_mknod_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_mknod_wind_cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->mknod,
                                           &local->loc, local->cont.mknod.mode,
                                           local->cont.mknod.dev,
                                           local->cont.mknod.params);
                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_mknod_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);
        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_mknod (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, dev_t dev, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(mknod,out);

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

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.mknod.mode  = mode;
        local->cont.mknod.dev   = dev;
        if (params)
                local->cont.mknod.params = dict_ref (params);

        local->transaction.fop    = afr_mknod_wind;
        local->transaction.done   = afr_mknod_done;
        local->transaction.unwind = afr_mknod_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (mknod, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ mkdir */


int
afr_mkdir_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;
        struct iatt *unwind_buf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                if (local->cont.mkdir.read_child_buf.ia_ino) {
                        unwind_buf = &local->cont.mkdir.read_child_buf;
                } else {
                        unwind_buf = &local->cont.mkdir.buf;
                }

                AFR_STACK_UNWIND (mkdir, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.mkdir.inode,
                                  unwind_buf, &local->cont.mkdir.preparent,
                                  &local->cont.mkdir.postparent);
        }

        return 0;
}


int
afr_mkdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
        afr_local_t     *local          = NULL;
        afr_private_t   *priv           = NULL;
        int             call_count      = -1;
        int             child_index     = -1;
        int32_t         *fresh_children = NULL;

        local = frame->local;
        priv = this->private;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        local->op_ret           = op_ret;

                        if (local->success_count == 0)
                                local->cont.mkdir.buf   = *buf;

                        if (child_index == local->read_child_index) {
                                local->cont.mkdir.read_child_buf = *buf;
                                local->cont.mkdir.preparent      = *preparent;
                                local->cont.mkdir.postparent     = *postparent;
                        }

                        local->cont.mkdir.inode = inode;

                        fresh_children = local->fresh_children;
                        fresh_children[local->success_count] = child_index;
                        local->success_count++;
                }

                local->op_errno         = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_set_read_ctx_from_policy (this, inode,
                                              local->fresh_children,
                                              local->read_child_index,
                                              priv->read_child);
                local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_mkdir_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_mkdir_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->mkdir,
                                           &local->loc, local->cont.mkdir.mode,
                                           local->cont.mkdir.params);
                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_mkdir_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_mkdir (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(mkdir,out);

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

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.mkdir.mode  = mode;
        if (params)
                local->cont.mkdir.params = dict_ref (params);

        local->transaction.fop    = afr_mkdir_wind;
        local->transaction.done   = afr_mkdir_done;
        local->transaction.unwind = afr_mkdir_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);

                AFR_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ link */


int
afr_link_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;
        struct iatt *unwind_buf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                if (local->cont.link.read_child_buf.ia_ino) {
                        unwind_buf = &local->cont.link.read_child_buf;
                } else {
                        unwind_buf = &local->cont.link.buf;
                }

                AFR_STACK_UNWIND (link, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.link.inode,
                                  unwind_buf, &local->cont.link.preparent,
                                  &local->cont.link.postparent);
        }

        return 0;
}


int
afr_link_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        afr_local_t     *local          = NULL;
        afr_private_t   *priv           = NULL;
        int             call_count      = -1;
        int             child_index     = -1;
        int32_t         *fresh_children = NULL;

        local = frame->local;
        priv = this->private;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        local->op_ret   = op_ret;

                        if (local->success_count == 0) {
                                local->cont.link.buf        = *buf;
                        }

                        if (child_index == local->read_child_index) {
                                local->cont.link.read_child_buf = *buf;
                                local->cont.link.preparent      = *preparent;
                                local->cont.link.postparent     = *postparent;
                        }

                        local->cont.link.inode    = inode;

                        fresh_children = local->fresh_children;
                        fresh_children[local->success_count] = child_index;
                        local->success_count++;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_set_read_ctx_from_policy (this, inode,
                                              local->fresh_children,
                                              local->read_child_index,
                                              priv->read_child);
                local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_link_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_link_wind_cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->link,
                                           &local->loc,
                                           &local->newloc);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_link_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(link,out);

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

        loc_copy (&local->loc,    oldloc);
        loc_copy (&local->newloc, newloc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->transaction.fop    = afr_link_wind;
        local->transaction.done   = afr_link_done;
        local->transaction.unwind = afr_link_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, oldloc);

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (oldloc->path);
        local->transaction.new_basename = AFR_BASENAME (newloc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (link, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ symlink */


int
afr_symlink_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;
        struct iatt *unwind_buf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                if (local->cont.symlink.read_child_buf.ia_ino) {
                        unwind_buf = &local->cont.symlink.read_child_buf;
                } else {
                        unwind_buf = &local->cont.symlink.buf;
                }

                AFR_STACK_UNWIND (symlink, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.symlink.inode,
                                  unwind_buf, &local->cont.symlink.preparent,
                                  &local->cont.symlink.postparent);
        }

        return 0;
}


int
afr_symlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, inode_t *inode,
                      struct iatt *buf, struct iatt *preparent,
                      struct iatt *postparent)
{
        afr_local_t     *local          = NULL;
        afr_private_t   *priv           = NULL;
        int             call_count      = -1;
        int             child_index     = -1;
        int32_t         *fresh_children = NULL;

        local = frame->local;
        priv = this->private;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        local->op_ret   = op_ret;

                        if (local->success_count == 0)
                                local->cont.symlink.buf        = *buf;

                        if (child_index == local->read_child_index) {
                                local->cont.symlink.read_child_buf = *buf;
                                local->cont.symlink.preparent      = *preparent;
                                local->cont.symlink.postparent     = *postparent;
                        }

                        local->cont.symlink.inode    = inode;

                        fresh_children = local->fresh_children;
                        fresh_children[local->success_count] = child_index;
                        local->success_count++;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_set_read_ctx_from_policy (this, inode,
                                              local->fresh_children,
                                              local->read_child_index,
                                              priv->read_child);
                local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_symlink_wind (call_frame_t *frame, xlator_t *this)
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
                        STACK_WIND_COOKIE (frame, afr_symlink_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->symlink,
                                           local->cont.symlink.linkpath,
                                           &local->loc,
                                           local->cont.symlink.params);

                        if (!--call_count)
                                break;

                }
        }

        return 0;
}


int
afr_symlink_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_symlink (call_frame_t *frame, xlator_t *this,
             const char *linkpath, loc_t *loc, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(symlink,out);

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

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.symlink.linkpath = gf_strdup (linkpath);
        if (params)
                local->cont.symlink.params = dict_ref (params);

        local->transaction.fop    = afr_symlink_wind;
        local->transaction.done   = afr_symlink_done;
        local->transaction.unwind = afr_symlink_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, loc);

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (symlink, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ rename */

int
afr_rename_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;
        struct iatt *unwind_buf = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                if (local->cont.rename.read_child_buf.ia_ino) {
                        unwind_buf = &local->cont.rename.read_child_buf;
                } else {
                        unwind_buf = &local->cont.rename.buf;
                }

                AFR_STACK_UNWIND (rename, main_frame,
                                  local->op_ret, local->op_errno,
                                  unwind_buf,
                                  &local->cont.rename.preoldparent,
                                  &local->cont.rename.postoldparent,
                                  &local->cont.rename.prenewparent,
                                  &local->cont.rename.postnewparent);
        }

        return 0;
}


int
afr_rename_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent)
{
        afr_local_t *   local = NULL;
        int call_count = -1;
        int child_index = -1;

        local = frame->local;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (afr_fop_failed (op_ret, op_errno) && op_errno != ENOTEMPTY)
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;

                                if (buf) {
                                        local->cont.rename.buf = *buf;
                                }

                                local->success_count++;
                        }

                        if (child_index == local->read_child_index) {
                                local->cont.rename.read_child_buf = *buf;

                                local->cont.rename.preoldparent  = *preoldparent;
                                local->cont.rename.postoldparent = *postoldparent;
                                local->cont.rename.prenewparent  = *prenewparent;
                                local->cont.rename.postnewparent = *postnewparent;
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


int32_t
afr_rename_wind (call_frame_t *frame, xlator_t *this)
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
                        STACK_WIND_COOKIE (frame, afr_rename_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->rename,
                                           &local->loc,
                                           &local->newloc);
                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_rename_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_rename (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(rename,out);

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

        loc_copy (&local->loc,    oldloc);
        loc_copy (&local->newloc, newloc);

        local->read_child_index = afr_inode_get_read_ctx (this, oldloc->inode, NULL);

        local->transaction.fop    = afr_rename_wind;
        local->transaction.done   = afr_rename_done;
        local->transaction.unwind = afr_rename_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, oldloc);
        afr_build_parent_loc (&local->transaction.new_parent_loc, newloc);

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (oldloc->path);
        local->transaction.new_basename = AFR_BASENAME (newloc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_RENAME_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);

                AFR_STACK_UNWIND (rename, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ unlink */

int
afr_unlink_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (unlink, main_frame,
                                  local->op_ret, local->op_errno,
                                  &local->cont.unlink.preparent,
                                  &local->cont.unlink.postparent);
        }

        return 0;
}


int
afr_unlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                     struct iatt *postparent)
{
        afr_local_t *   local = NULL;
        int call_count  = -1;
        int child_index = (long) cookie;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (child_index == local->read_child_index) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret   = op_ret;
                                local->cont.unlink.preparent  = *preparent;
                                local->cont.unlink.postparent = *postparent;
                        }

                        if (child_index == local->read_child_index) {
                                local->cont.unlink.preparent  = *preparent;
                                local->cont.unlink.postparent = *postparent;
                        }

                        local->success_count++;
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


int32_t
afr_unlink_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_unlink_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->unlink,
                                           &local->loc);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int32_t
afr_unlink_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int32_t
afr_unlink (call_frame_t *frame, xlator_t *this,
            loc_t *loc)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(unlink,out);

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

        loc_copy (&local->loc, loc);

        local->transaction.fop    = afr_unlink_wind;
        local->transaction.done   = afr_unlink_done;
        local->transaction.unwind = afr_unlink_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                                  NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ rmdir */



int
afr_rmdir_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame) {
                        main_frame = local->transaction.main_frame;
                }
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (rmdir, main_frame,
                                  local->op_ret, local->op_errno,
                                  &local->cont.rmdir.preparent,
                                  &local->cont.rmdir.postparent);
        }

        return 0;
}


int
afr_rmdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent)
{
        afr_local_t *   local = NULL;
        int call_count  = -1;
        int child_index = (long) cookie;
        int read_child  = 0;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno) && (op_errno != ENOTEMPTY))
                        afr_transaction_fop_failed (frame, this, child_index);

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                                local->cont.rmdir.preparent  = *preparent;
                                local->cont.rmdir.postparent = *postparent;

                        }

                        if (child_index == read_child) {
                                local->cont.rmdir.preparent  = *preparent;
                                local->cont.rmdir.postparent = *postparent;
                        }

                        local->success_count++;
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
afr_rmdir_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_rmdir_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->rmdir,
                                           &local->loc, local->cont.rmdir.flags);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_rmdir_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_rmdir (call_frame_t *frame, xlator_t *this,
           loc_t *loc, int flags)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(rmdir,out);

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

        local->cont.rmdir.flags = flags;
        loc_copy (&local->loc, loc);

        local->transaction.fop    = afr_rmdir_wind;
        local->transaction.done   = afr_rmdir_done;
        local->transaction.unwind = afr_rmdir_unwind;

        afr_build_parent_loc (&local->transaction.parent_loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        op_ret = 0;
out:
        if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                                  NULL, NULL);
        }

        return 0;
}

/* }}} */
