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

void
__dir_entry_fop_common_cbk (call_frame_t *frame, int child_index,
                            xlator_t *this, int32_t op_ret,
                            int32_t op_errno, inode_t *inode,
                            struct iatt *buf, struct iatt *preparent,
                            struct iatt *postparent, struct iatt *prenewparent,
                            struct iatt *postnewparent)
{
        afr_local_t     *local          = NULL;

        local = frame->local;

        if (afr_fop_failed (op_ret, op_errno))
                afr_transaction_fop_failed (frame, this, child_index);

        if (op_ret > -1) {
                local->op_ret = op_ret;

                if ((local->success_count == 0) ||
                    (child_index == local->read_child_index)) {
                        local->cont.dir_fop.preparent      = *preparent;
                        local->cont.dir_fop.postparent     = *postparent;
                        if (buf)
                                local->cont.dir_fop.buf            = *buf;
                        if (prenewparent)
                             local->cont.dir_fop.prenewparent  = *prenewparent;
                        if (postnewparent)
                             local->cont.dir_fop.postnewparent = *postnewparent;
                }

                local->cont.dir_fop.inode = inode;

                local->fresh_children[local->success_count] = child_index;
                local->success_count++;
                local->child_errno[child_index] = 0;
        } else {
                local->child_errno[child_index] = op_errno;
        }

        local->op_errno = op_errno;
}

int
afr_mark_new_entry_changelog_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  dict_t *xattr, dict_t *xdata)
{
        int     call_count = 0;

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                AFR_STACK_DESTROY (frame);
        }
        return 0;
}

void
afr_mark_new_entry_changelog (call_frame_t *frame, xlator_t *this)
{
        call_frame_t  *new_frame  = NULL;
        afr_local_t   *local      = NULL;
        afr_local_t   *new_local  = NULL;
        afr_private_t *priv       = NULL;
        dict_t        **xattr     = NULL;
        int32_t       **changelog = NULL;
        int           i           = 0;
        GF_UNUSED int op_errno    = 0;

        local = frame->local;
        priv = this->private;

        new_frame = copy_frame (frame);
        if (!new_frame) {
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (new_frame->local, out);
        new_local = new_frame->local;
        changelog = afr_matrix_create (priv->child_count, AFR_NUM_CHANGE_LOGS);
        if (!changelog)
                goto out;

        xattr = GF_CALLOC (priv->child_count, sizeof (*xattr),
                           gf_afr_mt_dict_t);
        if (!xattr)
                goto out;
        for (i = 0; i < priv->child_count; i++) {
                if (local->child_errno[i])
                        continue;
                xattr[i] = dict_new ();
                if (!xattr[i])
                        goto out;
        }

        afr_prepare_new_entry_pending_matrix (changelog,
                                              afr_is_errno_set,
                                              local->child_errno,
                                              &local->cont.dir_fop.buf,
                                              priv->child_count);

        new_local->pending = changelog;
        uuid_copy (new_local->loc.gfid, local->cont.dir_fop.buf.ia_gfid);
        new_local->loc.inode = inode_ref (local->cont.dir_fop.inode);
        new_local->call_count = local->success_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_errno[i])
                        continue;

                afr_set_pending_dict (priv, xattr[i], changelog, i, LOCAL_LAST);
                STACK_WIND_COOKIE (new_frame, afr_mark_new_entry_changelog_cbk,
                                   (void *) (long) i, priv->children[i],
                                   priv->children[i]->fops->xattrop,
                                   &new_local->loc, GF_XATTROP_ADD_ARRAY,
                                   xattr[i], NULL);
        }
        new_frame = NULL;
out:
        if (new_frame)
                AFR_STACK_DESTROY (new_frame);
        afr_xattr_array_destroy (xattr, priv->child_count);
        return;
}

gf_boolean_t
afr_is_new_entry_changelog_needed (glusterfs_fop_t fop)
{
        glusterfs_fop_t fops[]   = {GF_FOP_CREATE, GF_FOP_MKNOD, GF_FOP_NULL};
        int             i        = 0;

        for (i = 0; fops[i] != GF_FOP_NULL; i++) {
                if (fop == fops[i])
                        return _gf_true;
        }
        return _gf_false;
}

void
afr_dir_fop_mark_entry_pending_changelog (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;

        local = frame->local;
        priv  = this->private;

        if (local->op_ret < 0)
                goto out;

        if (local->success_count == priv->child_count)
                goto out;

        if (!afr_is_new_entry_changelog_needed (local->op))
                goto out;

        afr_mark_new_entry_changelog (frame, this);

out:
        return;
}

void
afr_dir_fop_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local          = NULL;
        afr_private_t   *priv           = NULL;

        local = frame->local;
        priv  = this->private;

        if (local->cont.dir_fop.inode == NULL)
                goto done;
        afr_set_read_ctx_from_policy (this, local->cont.dir_fop.inode,
                                      local->fresh_children,
                                      local->read_child_index,
                                      priv->read_child,
                                      local->cont.dir_fop.buf.ia_gfid);
done:
        local->transaction.unwind (frame, this);
        afr_dir_fop_mark_entry_pending_changelog (frame, this);
        local->transaction.resume (frame, this);
}

/* {{{ create */

int
afr_create_unwind (call_frame_t *frame, xlator_t *this)
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
                AFR_STACK_UNWIND (create, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.create.fd,
                                  local->cont.dir_fop.inode,
                                  &local->cont.dir_fop.buf,
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx = NULL;
        int             ret = 0;
        int             call_count = -1;
        int             child_index = -1;

        local = frame->local;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (op_ret > -1) {
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
				if (xdata)
					local->xdata_rsp = dict_ref(xdata);
			}
                }
                __dir_entry_fop_common_cbk (frame, child_index, this,
                                            op_ret, op_errno, inode, buf,
                                            preparent, postparent, NULL, NULL);
        }

unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op                = GF_FOP_CREATE;
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
        int_lock = &local->internal_lock;

        int_lock->lockee_count = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[0], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        int_lock->lockee_count++;
        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
                AFR_STACK_UNWIND (mknod, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.dir_fop.inode,
                                  &local->cont.dir_fop.buf,
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
        int             call_count      = -1;
        int             child_index     = -1;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                __dir_entry_fop_common_cbk (frame, child_index, this,
                                            op_ret, op_errno, inode, buf,
                                            preparent, postparent, NULL, NULL);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op               = GF_FOP_MKNOD;
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
        int_lock = &local->internal_lock;

        int_lock->lockee_count = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[0], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        int_lock->lockee_count++;
        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
                AFR_STACK_UNWIND (mkdir, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.dir_fop.inode,
                                  &local->cont.dir_fop.buf,
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
        int             call_count      = -1;
        int             child_index     = -1;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                __dir_entry_fop_common_cbk (frame, child_index, this,
                                            op_ret, op_errno, inode, buf,
                                            preparent, postparent, NULL, NULL);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op = GF_FOP_MKDIR;
        local->transaction.fop    = afr_mkdir_wind;
        local->transaction.done   = afr_mkdir_done;
        local->transaction.unwind = afr_mkdir_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);
        int_lock = &local->internal_lock;

        int_lock->lockee_count = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[0], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        int_lock->lockee_count++;
        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
                AFR_STACK_UNWIND (link, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.dir_fop.inode,
                                  &local->cont.dir_fop.buf,
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
        int             call_count      = -1;
        int             child_index     = -1;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                __dir_entry_fop_common_cbk (frame, child_index, this,
                                            op_ret, op_errno, inode, buf,
                                            preparent, postparent, NULL, NULL);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
                        STACK_WIND_COOKIE (frame, afr_link_wind_cbk,
                                           (void *) (long) i,
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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op = GF_FOP_LINK;
        local->transaction.fop    = afr_link_wind;
        local->transaction.done   = afr_link_done;
        local->transaction.unwind = afr_link_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, newloc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (newloc->path);
        int_lock = &local->internal_lock;

        int_lock->lockee_count = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[0], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        int_lock->lockee_count++;
        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }
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
                AFR_STACK_UNWIND (symlink, main_frame,
                                  local->op_ret, local->op_errno,
                                  local->cont.dir_fop.inode,
                                  &local->cont.dir_fop.buf,
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
        int             call_count      = -1;
        int             child_index     = -1;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                __dir_entry_fop_common_cbk (frame, child_index, this,
                                            op_ret, op_errno, inode, buf,
                                            preparent, postparent, NULL, NULL);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op = GF_FOP_SYMLINK;
        local->transaction.fop    = afr_symlink_wind;
        local->transaction.done   = afr_symlink_done;
        local->transaction.unwind = afr_symlink_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame   = frame;
        local->transaction.basename     = AFR_BASENAME (loc->path);
        int_lock = &local->internal_lock;

        int_lock->lockee_count = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[0], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        int_lock->lockee_count++;
        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
                AFR_STACK_UNWIND (rename, main_frame,
                                  local->op_ret, local->op_errno,
                                  &local->cont.dir_fop.buf,
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
                                  &local->cont.dir_fop.prenewparent,
                                  &local->cont.dir_fop.postnewparent,
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
                local->op_errno = op_errno;
                local->child_errno[child_index] = op_errno;

                if (op_ret > -1)
                        __dir_entry_fop_common_cbk (frame, child_index, this,
                                                   op_ret, op_errno, NULL, buf,
                                                   preoldparent, postoldparent,
                                                   prenewparent, postnewparent);

        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;
        int                     nlockee                 = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op = GF_FOP_RENAME;
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
        int_lock = &local->internal_lock;

        int_lock->lockee_count = nlockee = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[nlockee], local,
                                     &local->transaction.new_parent_loc,
                                     local->transaction.new_basename,
                                     priv->child_count);
        if (ret)
                goto out;

        nlockee++;
        ret = afr_init_entry_lockee (&int_lock->lockee[nlockee], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        nlockee++;
        if (local->newloc.inode && IA_ISDIR (local->newloc.inode->ia_type)) {
                ret = afr_init_entry_lockee (&int_lock->lockee[nlockee], local,
                                             &local->newloc,
                                             NULL,
                                             priv->child_count);
                if (ret)
                        goto out;

                nlockee++;
        }
        qsort (int_lock->lockee, nlockee, sizeof (*int_lock->lockee),
               afr_entry_lockee_cmp);
        int_lock->lockee_count = nlockee;

        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_RENAME_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
                __dir_entry_fop_common_cbk (frame, child_index, this,
                                            op_ret, op_errno, NULL, NULL,
                                            preparent, postparent, NULL, NULL);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);
        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op = GF_FOP_UNLINK;
        local->transaction.fop    = afr_unlink_wind;
        local->transaction.done   = afr_unlink_done;
        local->transaction.unwind = afr_unlink_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);
        int_lock = &local->internal_lock;

        int_lock->lockee_count = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[0], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        int_lock->lockee_count++;
        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
                                  &local->cont.dir_fop.preparent,
                                  &local->cont.dir_fop.postparent,
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
                local->op_errno = op_errno;
                local->child_errno[child_index] = op_errno;
                if (op_ret > -1)
                        __dir_entry_fop_common_cbk (frame, child_index, this,
                                                   op_ret, op_errno, NULL, NULL,
                                                   preparent, postparent, NULL,
                                                   NULL);

        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);
        if (call_count == 0)
                afr_dir_fop_done (frame, this);

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
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = 0;
        int                     nlockee                 = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

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

        local->op = GF_FOP_RMDIR;
        local->transaction.fop    = afr_rmdir_wind;
        local->transaction.done   = afr_rmdir_done;
        local->transaction.unwind = afr_rmdir_unwind;

        ret = afr_build_parent_loc (&local->transaction.parent_loc, loc,
                                    &op_errno);
        if (ret)
                goto out;

        local->transaction.main_frame = frame;
        local->transaction.basename = AFR_BASENAME (loc->path);
        int_lock = &local->internal_lock;

        int_lock->lockee_count = nlockee = 0;
        ret = afr_init_entry_lockee (&int_lock->lockee[nlockee], local,
                                     &local->transaction.parent_loc,
                                     local->transaction.basename,
                                     priv->child_count);
        if (ret)
                goto out;

        nlockee++;
        ret = afr_init_entry_lockee (&int_lock->lockee[nlockee], local,
                                     &local->loc,
                                     NULL,
                                     priv->child_count);
        if (ret)
                goto out;

        nlockee++;
        qsort (int_lock->lockee, nlockee, sizeof (*int_lock->lockee),
               afr_entry_lockee_cmp);
        int_lock->lockee_count = nlockee;

        ret = afr_transaction (transaction_frame, this, AFR_ENTRY_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

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
