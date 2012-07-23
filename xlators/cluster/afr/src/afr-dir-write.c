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

#include "afr.h"
#include "afr-transaction.h"

int
afr_build_parent_loc (loc_t *parent, loc_t *child, int32_t *op_errno)
{
        int     ret = -1;
        char    *child_path = NULL;

        if (!child->parent) {
                if (op_errno)
                        *op_errno = EINVAL;
                goto out;
        }

        child_path = gf_strdup (child->path);
        if (!child_path) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }
        parent->path = gf_strdup( dirname (child_path) );
	if (!parent->path) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }
        parent->inode  = inode_ref (child->parent);
        uuid_copy (parent->gfid, child->pargfid);

        ret = 0;
out:
	GF_FREE(child_path);

        return ret;
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
                                  &local->cont.create.postparent,
                                  local->xdata_rsp);
        }

        return 0;
}


int
afr_create_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     fd_t *fd, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
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

                        if (local->success_count == 0) {
                                local->cont.create.buf        = *buf;
				if (xdata)
					local->xdata_rsp = dict_ref(xdata);
			}

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
                                              priv->read_child,
                                              local->cont.create.buf.ia_gfid);
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
                                           local->umask,
                                           local->cont.create.fd,
                                           local->xdata_req);
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
            mode_t umask, fd_t *fd, dict_t *params)
{
        afr_private_t  *priv  = NULL;
        afr_local_t    *local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int             ret = -1;
        int             op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(create,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

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
        local->umask  = umask;
        if (params)
                local->xdata_req = dict_ref (params);

        local->transaction.fop    = afr_create_wind;
        local->transaction.done   = afr_create_done;
        local->transaction.unwind = afr_create_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (create, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL, NULL, NULL);
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
                                  &local->cont.mknod.postparent,
                                  NULL);
        }

        return 0;
}


int
afr_mknod_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
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
                                              priv->read_child,
                                              local->cont.mknod.buf.ia_gfid);
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
                                           local->umask,
                                           local->xdata_req);
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
afr_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t dev, mode_t umask, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(mknod,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.mknod.mode  = mode;
        local->cont.mknod.dev   = dev;
        local->umask = umask;
        if (params)
                local->xdata_req = dict_ref (params);

        local->transaction.fop    = afr_mknod_wind;
        local->transaction.done   = afr_mknod_done;
        local->transaction.unwind = afr_mknod_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (mknod, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
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
                                  &local->cont.mkdir.postparent,
                                  NULL);
        }

        return 0;
}


int
afr_mkdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
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
                                              priv->read_child,
                                              local->cont.mkdir.buf.ia_gfid);
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
                                           local->umask,
                                           local->xdata_req);
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
           loc_t *loc, mode_t mode, mode_t umask, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(mkdir,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.mkdir.mode  = mode;
        local->umask = umask;
        if (params)
                local->xdata_req = dict_ref (params);

        local->transaction.fop    = afr_mkdir_wind;
        local->transaction.done   = afr_mkdir_done;
        local->transaction.unwind = afr_mkdir_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);

                AFR_STACK_UNWIND (mkdir, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
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
                                  &local->cont.link.postparent,
                                  NULL);
        }

        return 0;
}


int
afr_link_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
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
                                              priv->read_child,
                                              local->cont.link.buf.ia_gfid);
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
                                           &local->newloc, local->xdata_req);

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
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(link,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc,    oldloc);
        loc_copy (&local->newloc, newloc);
        if (xdata)
                local->xdata_req = dict_ref (xdata);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->transaction.fop    = afr_link_wind;
        local->transaction.done   = afr_link_done;
        local->transaction.unwind = afr_link_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, newloc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (newloc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (link, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
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
                                  &local->cont.symlink.postparent,
                                  NULL);
        }

        return 0;
}


int
afr_symlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, inode_t *inode,
                      struct iatt *buf, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
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
                                              priv->read_child,
                                              local->cont.symlink.buf.ia_gfid);
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
                                           local->umask,
                                           local->xdata_req);

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
             const char *linkpath, loc_t *loc, mode_t umask, dict_t *params)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(symlink,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc, loc);

        LOCK (&priv->read_child_lock);
        {
                local->read_child_index = (++priv->read_child_rr)
                        % (priv->child_count);
        }
        UNLOCK (&priv->read_child_lock);

        local->cont.symlink.linkpath = gf_strdup (linkpath);
        local->umask = umask;
        if (params)
                local->xdata_req = dict_ref (params);

        local->transaction.fop    = afr_symlink_wind;
        local->transaction.done   = afr_symlink_done;
        local->transaction.unwind = afr_symlink_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret  < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (symlink, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
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
                                  &local->cont.rename.postnewparent,
                                  NULL);
        }

        return 0;
}


int
afr_rename_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
                     dict_t *xdata)
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
                                           &local->newloc, NULL);
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
            loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(rename,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc,    oldloc);
        loc_copy (&local->newloc, newloc);

        local->read_child_index = afr_inode_get_read_ctx (this, oldloc->inode, NULL);

        local->transaction.fop    = afr_rename_wind;
        local->transaction.done   = afr_rename_done;
        local->transaction.unwind = afr_rename_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, oldloc,
                                    &op_errno);
        if (ret)
                goto out;
        ret = afr_build_parent_loc (&local->transaction.new_parent_loc, newloc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (oldloc->path);
        local->transaction.new_basename = AFR_BASENAME (newloc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_RENAME_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);

                AFR_STACK_UNWIND (rename, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL, NULL, NULL);
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
                                  &local->cont.unlink.postparent,
                                  NULL);
        }

        return 0;
}


int
afr_unlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
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
                                           &local->loc, local->xflag,
                                           local->xdata_req);

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
            loc_t *loc, int xflag, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(unlink,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc, loc);
        local->xflag = xflag;
        if (xdata)
                local->xdata_req = dict_ref (xdata);

        local->transaction.fop    = afr_unlink_wind;
        local->transaction.done   = afr_unlink_done;
        local->transaction.unwind = afr_unlink_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (unlink, frame, -1, op_errno,
                                  NULL, NULL, NULL);
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
                                  &local->cont.rmdir.postparent,
                                  NULL);
        }

        return 0;
}


int
afr_rmdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
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
                                           &local->loc, local->cont.rmdir.flags,
                                           NULL);

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
           loc_t *loc, int flags, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t  * transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        QUORUM_CHECK(rmdir,out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->cont.rmdir.flags = flags;
        loc_copy (&local->loc, loc);

        local->transaction.fop    = afr_rmdir_wind;
        local->transaction.done   = afr_rmdir_done;
        local->transaction.unwind = afr_rmdir_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);

        afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */
