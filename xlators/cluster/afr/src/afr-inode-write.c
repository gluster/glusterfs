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
#include "afr-self-heal-common.h"

void
__inode_write_fop_cbk (call_frame_t *frame, int child_index, int read_child,
                       xlator_t *this, int32_t *op_ret, int32_t *op_errno,
                       struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t     *local = NULL;

        local = frame->local;

        if (afr_fop_failed (*op_ret, *op_errno)) {
                local->child_errno[child_index] = *op_errno;

                switch (local->op) {
                case GF_FOP_TRUNCATE:
                case GF_FOP_FTRUNCATE:
                        if (*op_errno != EFBIG)
                                afr_transaction_fop_failed (frame, this,
                                                            child_index);
                break;
                default:
                        afr_transaction_fop_failed (frame, this, child_index);
                break;
                }
                local->op_errno = *op_errno;
                goto out;
        }

        if ((local->success_count == 0) || (read_child == child_index)) {
                local->op_ret              = *op_ret;
                if (prebuf)
                        local->cont.inode_wfop.prebuf  = *prebuf;
                if (postbuf)
                        local->cont.inode_wfop.postbuf = *postbuf;
        }

        local->success_count++;
out:
        return;
}

/* {{{ writev */

void
afr_writev_copy_outvars (call_frame_t *src_frame, call_frame_t *dst_frame)
{
        afr_local_t *src_local = NULL;
        afr_local_t *dst_local = NULL;

        src_local = src_frame->local;
        dst_local = dst_frame->local;

        dst_local->op_ret = src_local->op_ret;
        dst_local->op_errno = src_local->op_errno;
        dst_local->cont.inode_wfop.prebuf = src_local->cont.inode_wfop.prebuf;
        dst_local->cont.inode_wfop.postbuf = src_local->cont.inode_wfop.postbuf;
}

void
afr_writev_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        local = frame->local;

        AFR_STACK_UNWIND (writev, frame,
                          local->op_ret, local->op_errno,
                          &local->cont.inode_wfop.prebuf,
                          &local->cont.inode_wfop.postbuf,
                          NULL);
}

call_frame_t*
afr_transaction_detach_fop_frame (call_frame_t *frame)
{
        afr_local_t *   local = NULL;
        call_frame_t   *fop_frame = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                fop_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        return fop_frame;
}

int
afr_transaction_writev_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *fop_frame = NULL;

        fop_frame = afr_transaction_detach_fop_frame (frame);

        if (fop_frame) {
                afr_writev_copy_outvars (frame, fop_frame);
                afr_writev_unwind (fop_frame, this);
        }
        return 0;
}

static void
afr_writev_handle_short_writes (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;
        int           i      = 0;

        local = frame->local;
        priv = this->private;
        /*
         * We already have the best case result of the writev calls staged
         * as the return value. Any writev that returns some value less
         * than the best case is now out of sync, so mark the fop as
         * failed. Note that fops that have returned with errors have
         * already been marked as failed.
         */
        for (i = 0; i < priv->child_count; i++) {
                if ((!local->replies[i].valid) ||
                    (local->replies[i].op_ret == -1))
                        continue;

                if (local->replies[i].op_ret < local->op_ret)
                        afr_transaction_fop_failed(frame, this, i);
        }
}

int
afr_writev_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *   local = NULL;
        afr_private_t  *priv  = NULL;
        call_frame_t    *fop_frame = NULL;
        int child_index = (long) cookie;
        int call_count  = -1;
        int read_child  = 0;
        int      ret = 0;
        uint32_t open_fd_count = 0;
        uint32_t write_is_append = 0;

        local = frame->local;
        priv  = this->private;

        read_child = afr_inode_get_read_ctx (this, local->fd->inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, prebuf, postbuf,
                                       xdata);

		local->replies[child_index].valid = 1;
		local->replies[child_index].op_ret = op_ret;
		local->replies[child_index].op_errno = op_errno;


		/* stage the best case return value for unwind */
                if ((local->success_count == 0) || (op_ret > local->op_ret)) {
                        local->op_ret              = op_ret;
			local->op_errno		   = op_errno;
		}

		if (op_ret != -1) {
                        if (xdata) {
                                ret = dict_get_uint32 (xdata,
                                                       GLUSTERFS_OPEN_FD_COUNT,
                                                       &open_fd_count);
                                if ((ret == 0) &&
                                    (open_fd_count > local->open_fd_count)) {
                                        local->open_fd_count = open_fd_count;
                                        local->update_open_fd_count = _gf_true;
                                }

				write_is_append = 0;
                                ret = dict_get_uint32 (xdata,
                                                       GLUSTERFS_WRITE_IS_APPEND,
                                                       &write_is_append);
                                if (ret || !write_is_append)
					local->append_write = _gf_false;
                        }

		}
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {

                if (local->update_open_fd_count)
                        afr_handle_open_fd_count (frame, this);

                if (!local->stable_write && !local->append_write)
			/* An appended write removes the necessity to
			   fsync() the file. This is because self-heal
			   has the logic to check for larger file when
			   the xattrs are not reliably pointing at
			   a stale file.
			*/
                        afr_fd_report_unstable_write (this, local->fd);

                afr_writev_handle_short_writes (frame, this);
                if (afr_any_fops_failed (local, priv)) {
                        //Don't unwind until post-op is complete
                        local->transaction.resume (frame, this);
                } else {
                /*
                 * Generally inode-write fops do transaction.unwind then
                 * transaction.resume, but writev needs to make sure that
                 * delayed post-op frame is placed in fdctx before unwind
                 * happens. This prevents the race of flush doing the
                 * changelog wakeup first in fuse thread and then this
                 * writev placing its delayed post-op frame in fdctx.
                 * This helps flush make sure all the delayed post-ops are
                 * completed.
                 */

                        fop_frame = afr_transaction_detach_fop_frame (frame);
                        afr_writev_copy_outvars (frame, fop_frame);
                        local->transaction.resume (frame, this);
                        afr_writev_unwind (fop_frame, this);
                }
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
        dict_t *xdata = NULL;
        GF_UNUSED int     ret = 0;

        local = frame->local;
        priv = this->private;

        call_count = afr_pre_op_done_children_count (local->transaction.pre_op,
                                                     priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;
	local->replies = GF_CALLOC(priv->child_count, sizeof(*local->replies),
				   gf_afr_mt_reply_t);
	if (!local->replies) {
		local->op_ret = -1;
		local->op_errno = ENOMEM;
		local->transaction.unwind(frame, this);
		local->transaction.resume(frame, this);
		return 0;
	}

        xdata = dict_new ();
        if (xdata) {
                ret = dict_set_uint32 (xdata, GLUSTERFS_OPEN_FD_COUNT,
                                       sizeof (uint32_t));
		ret = dict_set_uint32 (xdata, GLUSTERFS_WRITE_IS_APPEND,
				       0);
		/* Set append_write to be true speculatively. If on any
		   server it turns not be true, we unset it in the
		   callback.
		*/
		local->append_write = _gf_true;
        }

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
                                           local->cont.writev.flags,
                                           local->cont.writev.iobref,
                                           xdata);

                        if (!--call_count)
                                break;
                }
        }

        if (xdata)
                dict_unref (xdata);

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
        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);

        local->op = GF_FOP_WRITE;

        local->success_count      = 0;

        local->transaction.fop    = afr_writev_wind;
        local->transaction.done   = afr_writev_done;
        local->transaction.unwind = afr_transaction_writev_unwind;

        local->transaction.main_frame = frame;
        if (local->fd->flags & O_APPEND) {
               /*
                * Backend vfs ignores the 'offset' for append mode fd so
                * locking just the region provided for the writev does not
                * give consistency gurantee. The actual write may happen at a
                * completely different range than the one provided by the
                * offset, len in the fop. So lock the entire file.
                */
                local->transaction.start   = 0;
                local->transaction.len     = 0;
        } else {
                local->transaction.start   = local->cont.writev.offset;
                local->transaction.len     = iov_length (local->cont.writev.vector,
                                                         local->cont.writev.count);
        }

        op_ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (op_ret < 0) {
            op_errno = -op_ret;
            goto out;
        }

        op_ret = 0;
out:
        if (op_ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (writev, frame, op_ret, op_errno, NULL, NULL, NULL);
        }

        return 0;
}

static void
afr_trigger_open_fd_self_heal (fd_t *fd, xlator_t *this)
{
        call_frame_t    *frame   = NULL;
        afr_local_t     *local   = NULL;
        afr_self_heal_t *sh      = NULL;
        char            *reason  = NULL;
        int32_t         op_errno = 0;
        int             ret      = 0;

        if (!fd || !fd->inode || uuid_is_null (fd->inode->gfid)) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Invalid args: "
                                  "fd: %p, inode: %p", fd,
                                  fd ? fd->inode : NULL);
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;
        ret = afr_local_init (local, this->private, &op_errno);
        if (ret < 0)
                goto out;

        local->loc.inode = inode_ref (fd->inode);
        ret = loc_path (&local->loc, NULL);
        if (ret < 0)
                goto out;

        sh    = &local->self_heal;
        sh->do_metadata_self_heal = _gf_true;
        if (fd->inode->ia_type == IA_IFREG)
                sh->do_data_self_heal = _gf_true;
        else if (fd->inode->ia_type == IA_IFDIR)
                sh->do_entry_self_heal = _gf_true;

        reason = "subvolume came online";
        afr_launch_self_heal (frame, this, fd->inode, _gf_true,
                              fd->inode->ia_type, reason, NULL, NULL);
        return;
out:
        AFR_STACK_DESTROY (frame);
}

void
afr_open_fd_fix (fd_t *fd, xlator_t *this)
{
        int           ret             = 0;
        int           i               = 0;
        afr_fd_ctx_t  *fd_ctx         = NULL;
        gf_boolean_t  need_self_heal  = _gf_false;
        int           *need_open      = NULL;
        size_t        need_open_count = 0;
        afr_private_t *priv           = NULL;

        priv  = this->private;

        if (!afr_is_fd_fixable (fd))
                goto out;

        fd_ctx = afr_fd_ctx_get (fd, this);
        if (!fd_ctx)
                goto out;

        LOCK (&fd->lock);
        {
                if (fd_ctx->up_count < priv->up_count) {
                        need_self_heal = _gf_true;
                        fd_ctx->up_count   = priv->up_count;
                        fd_ctx->down_count = priv->down_count;
                }

                need_open = alloca (priv->child_count * sizeof (*need_open));
                for (i = 0; i < priv->child_count; i++) {
                        need_open[i] = 0;
                        if (fd_ctx->opened_on[i] != AFR_FD_NOT_OPENED)
                                continue;

                        if (!priv->child_up[i])
                                continue;

                        fd_ctx->opened_on[i] = AFR_FD_OPENING;

                        need_open[i] = 1;
                        need_open_count++;
                }
        }
        UNLOCK (&fd->lock);
        if (ret)
                goto out;

        if (need_self_heal)
                afr_trigger_open_fd_self_heal (fd, this);

        if (!need_open_count)
                goto out;

        afr_fix_open (this, fd, need_open_count, need_open);
out:
        return;
}

int
afr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->cont.writev.vector     = iov_dup (vector, count);
        local->cont.writev.count      = count;
        local->cont.writev.offset     = offset;
        local->cont.writev.flags      = flags;
        local->cont.writev.iobref     = iobref_ref (iobref);

        local->fd                = fd_ref (fd);

	/* detect here, but set it in writev_wind_cbk *after* the unstable
	   write is performed
	*/
	local->stable_write = !!((fd->flags|flags)&(O_SYNC|O_DSYNC));

        afr_open_fd_fix (fd, this);

        afr_do_writev (frame, this);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);

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
                                  &local->cont.inode_wfop.prebuf,
                                  &local->cont.inode_wfop.postbuf,
                                  NULL);
        }

        return 0;
}


int
afr_truncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *   local = NULL;
        int child_index = (long) cookie;
        int read_child  = 0;
        int call_count  = -1;

        local = frame->local;

        read_child = afr_inode_get_read_ctx (this, local->loc.inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (op_ret != -1) {
			if (prebuf->ia_size != postbuf->ia_size)
				local->stable_write = _gf_false;
                }
                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, prebuf, postbuf,
                                       xdata);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		if (local->stable_write && afr_txn_nothing_failed (frame, this))
			local->transaction.unwind (frame, this);

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
	local->stable_write = _gf_true;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_truncate_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->truncate,
                                           &local->loc,
                                           local->cont.truncate.offset,
                                           NULL);

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
              loc_t *loc, off_t offset, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
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

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->op = GF_FOP_TRUNCATE;
        local->cont.truncate.offset  = offset;

        local->transaction.fop    = afr_truncate_wind;
        local->transaction.done   = afr_truncate_done;
        local->transaction.unwind = afr_truncate_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = offset;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);
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
                                  &local->cont.inode_wfop.prebuf,
                                  &local->cont.inode_wfop.postbuf,
                                  NULL);
        }
        return 0;
}


int
afr_ftruncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
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

                if (op_ret != -1) {
			if (prebuf->ia_size != postbuf->ia_size)
				local->stable_write = _gf_false;
                }
                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, prebuf, postbuf,
                                       xdata);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		if (local->stable_write && afr_txn_nothing_failed (frame, this))
			local->transaction.unwind (frame, this);

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
	local->stable_write = _gf_true;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
                        STACK_WIND_COOKIE (frame, afr_ftruncate_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->ftruncate,
                                           local->fd,
                                           local->cont.ftruncate.offset,
                                           NULL);

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

        op_ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (op_ret < 0) {
            op_errno = -op_ret;
            goto out;
        }

        op_ret = 0;
out:
        if (op_ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, NULL,
                                  NULL, NULL);
        }

        return 0;
}


int
afr_ftruncate (call_frame_t *frame, xlator_t *this,
               fd_t *fd, off_t offset, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }
        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->cont.ftruncate.offset  = offset;

        local->fd = fd_ref (fd);

        afr_open_fd_fix (fd, this);

        afr_do_ftruncate (frame, this);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
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
                                  &local->cont.inode_wfop.prebuf,
                                  &local->cont.inode_wfop.postbuf,
                                  NULL);
        }

        return 0;
}


int
afr_setattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *preop, struct iatt *postop, dict_t *xdata)
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

                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, preop, postop,
                                       xdata);

                if ((local->success_count >= priv->wait_count)
                    && local->read_child_returned) {
                        need_unwind = 1;
                }
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
                                           local->cont.setattr.valid,
                                           NULL);

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
             loc_t *loc, struct iatt *buf, int32_t valid, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
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

        AFR_LOCAL_ALLOC_OR_GOTO (transaction_frame->local, out);
        local = transaction_frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->op = GF_FOP_SETATTR;
        local->cont.setattr.in_buf = *buf;
        local->cont.setattr.valid  = valid;

        local->transaction.fop    = afr_setattr_wind;
        local->transaction.done   = afr_setattr_done;
        local->transaction.unwind = afr_setattr_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);
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
                                  &local->cont.inode_wfop.prebuf,
                                  &local->cont.inode_wfop.postbuf,
                                  NULL);
        }

        return 0;
}


int
afr_fsetattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop, dict_t *xdata)
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

                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, preop, postop,
                                       xdata);

                if ((local->success_count >= priv->wait_count)
                    && local->read_child_returned) {
                        need_unwind = 1;
                }
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
                                           local->cont.fsetattr.valid,
                                           NULL);

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
              fd_t *fd, struct iatt *buf, int32_t valid, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }

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

        local->op = GF_FOP_FSETATTR;
        local->cont.fsetattr.in_buf = *buf;
        local->cont.fsetattr.valid  = valid;

        local->transaction.fop    = afr_fsetattr_wind;
        local->transaction.done   = afr_fsetattr_done;
        local->transaction.unwind = afr_fsetattr_unwind;

        local->fd                 = fd_ref (fd);

        afr_open_fd_fix (fd, this);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
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
                                  local->op_ret, local->op_errno,
                                  NULL);
        }
        return 0;
}


int
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        int           call_count  = -1;
        int           need_unwind = 0;
        int           child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                __inode_write_fop_cbk (frame, child_index, -1, this,
                                       &op_ret, &op_errno, NULL, NULL,
                                       xdata);
                if (local->success_count == priv->child_count) {
                        need_unwind = 1;
                }
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
        afr_local_t      *local         = NULL;
        afr_private_t    *priv          = NULL;
        int               call_count    = -1;
        int               i             = 0;

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
                                           local->cont.setxattr.flags,
                                           NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_setxattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local    = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

int
afr_setxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *dict, int32_t flags, dict_t *xdata)
{
        afr_private_t  *priv              = NULL;
        afr_local_t    *local             = NULL;
        call_frame_t   *transaction_frame = NULL;
        int             ret               = -1;
        int             op_errno          = EINVAL;

        VALIDATE_OR_GOTO (this, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.afr.*", dict,
                                   op_errno, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.afr.*", dict,
                                   op_errno, out);

        VALIDATE_OR_GOTO (frame, out);
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

        local->op = GF_FOP_SETXATTR;
        local->cont.setxattr.dict  = dict_ref (dict);
        local->cont.setxattr.flags = flags;

        local->transaction.fop    = afr_setxattr_wind;
        local->transaction.done   = afr_setxattr_done;
        local->transaction.unwind = afr_setxattr_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);
        }

        return 0;
}

/* {{{ fsetxattr */


int
afr_fsetxattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t    *local         = NULL;
        call_frame_t   *main_frame    = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (local->transaction.main_frame)
                        main_frame = local->transaction.main_frame;
                local->transaction.main_frame = NULL;
        }
        UNLOCK (&frame->lock);

        if (main_frame) {
                AFR_STACK_UNWIND (fsetxattr, main_frame,
                                  local->op_ret, local->op_errno,
                                  NULL);
        }
        return 0;
}


int
afr_fsetxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        int           call_count  = -1;
        int           need_unwind = 0;
        int           child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {

                __inode_write_fop_cbk (frame, child_index, -1, this,
                                       &op_ret, &op_errno, NULL, NULL,
                                       xdata);
                if (local->success_count == priv->child_count) {
                        need_unwind = 1;
                }
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
afr_fsetxattr_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t        *local       = NULL;
        afr_private_t      *priv        = NULL;
        int                 call_count  = -1;
        int                 i           = 0;

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
                        STACK_WIND_COOKIE (frame, afr_fsetxattr_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fsetxattr,
                                           local->fd,
                                           local->cont.fsetxattr.dict,
                                           local->cont.fsetxattr.flags,
                                           NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_fsetxattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local   = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

int
afr_fsetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata)
{
        afr_private_t    *priv              = NULL;
        afr_local_t      *local             = NULL;
        call_frame_t     *transaction_frame = NULL;
        int               ret               = -1;
        int               op_errno          = EINVAL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.afr.*", dict,
                                   op_errno, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.afr.*", dict,
                                   op_errno, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (local, out);

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        transaction_frame->local = local;

        local->op_ret = -1;

        local->op = GF_FOP_FSETXATTR;
        local->cont.fsetxattr.dict  = dict_ref (dict);
        local->cont.fsetxattr.flags = flags;

        local->transaction.fop    = afr_fsetxattr_wind;
        local->transaction.done   = afr_fsetxattr_done;
        local->transaction.unwind = afr_fsetxattr_unwind;

        local->fd                 = fd_ref (fd);

        local->transaction.main_frame = frame;
        local->transaction.start  = LLONG_MAX - 1;
        local->transaction.len    = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
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
                                  local->op_ret, local->op_errno,
                                  NULL);
        }
        return 0;
}


int
afr_removexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        int           call_count  = -1;
        int           need_unwind = 0;
        int           child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                __inode_write_fop_cbk (frame, child_index, -1, this,
                                       &op_ret, &op_errno, NULL, NULL,
                                       xdata);
                if (local->success_count == priv->wait_count) {
                        need_unwind = 1;
                }
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
                                           local->cont.removexattr.name,
                                           NULL);

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
                 loc_t *loc, const char *name, dict_t *xdata)
{
        afr_private_t   *priv              = NULL;
        afr_local_t     *local             = NULL;
        call_frame_t    *transaction_frame = NULL;
        int              ret               = -1;
        int              op_errno          = 0;

        VALIDATE_OR_GOTO (this, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.afr.*",
                                 name, op_errno, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.afr.*",
                                 name, op_errno, out);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

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

        local->op = GF_FOP_REMOVEXATTR;
        local->cont.removexattr.name = gf_strdup (name);

        local->transaction.fop    = afr_removexattr_wind;
        local->transaction.done   = afr_removexattr_done;
        local->transaction.unwind = afr_removexattr_unwind;

        loc_copy (&local->loc, loc);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (removexattr, frame, -1, op_errno, NULL);
        }

        return 0;
}

/* ffremovexattr */
int
afr_fremovexattr_unwind (call_frame_t *frame, xlator_t *this)
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
                AFR_STACK_UNWIND (fremovexattr, main_frame,
                                  local->op_ret, local->op_errno,
                                  NULL);
        }
        return 0;
}


int
afr_fremovexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *   local       = NULL;
        afr_private_t * priv        = NULL;
        int             call_count  = -1;
        int             need_unwind = 0;
        int             child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                __inode_write_fop_cbk (frame, child_index, -1, this,
                                       &op_ret, &op_errno, NULL, NULL,
                                       xdata);

                if (local->success_count == priv->wait_count) {
                        need_unwind = 1;
                }
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
afr_fremovexattr_wind (call_frame_t *frame, xlator_t *this)
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
                        STACK_WIND_COOKIE (frame, afr_fremovexattr_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fremovexattr,
                                           local->fd,
                                           local->cont.removexattr.name,
                                           NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
afr_fremovexattr_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t * local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}


int
afr_fremovexattr (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, const char *name, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_ret   = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (this, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.afr.*",
                                 name, op_errno, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.afr.*",
                                 name, op_errno, out);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;
        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (local, out);

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        transaction_frame->local = local;

        local->op_ret = -1;

        local->op = GF_FOP_FREMOVEXATTR;
        local->cont.removexattr.name = gf_strdup (name);

        local->transaction.fop    = afr_fremovexattr_wind;
        local->transaction.done   = afr_fremovexattr_done;
        local->transaction.unwind = afr_fremovexattr_unwind;

        local->fd = fd_ref (fd);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        op_ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (op_ret < 0) {
            op_errno = -op_ret;
            goto out;
        }

        op_ret = 0;
out:
        if (op_ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, NULL);
        }

        return 0;
}

static int
afr_fallocate_unwind (call_frame_t *frame, xlator_t *this)
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
                AFR_STACK_UNWIND (fallocate, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.inode_wfop.prebuf,
                                  &local->cont.inode_wfop.postbuf,
                                  NULL);
        }
        return 0;
}

static int
afr_fallocate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
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

                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, prebuf, postbuf,
                                       xdata);

                if ((local->success_count >= priv->wait_count)
                    && local->read_child_returned) {
                        need_unwind = 1;
                }
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

static int
afr_fallocate_wind (call_frame_t *frame, xlator_t *this)
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
                        STACK_WIND_COOKIE (frame, afr_fallocate_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fallocate,
                                           local->fd,
                                           local->cont.fallocate.mode,
                                           local->cont.fallocate.offset,
                                           local->cont.fallocate.len,
                                           NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}

static int
afr_fallocate_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

static int
afr_do_fallocate (call_frame_t *frame, xlator_t *this)
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

        local->op = GF_FOP_FALLOCATE;

        local->transaction.fop    = afr_fallocate_wind;
        local->transaction.done   = afr_fallocate_done;
        local->transaction.unwind = afr_fallocate_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.fallocate.offset;
        local->transaction.len     = 0;

        /* fallocate can modify the file size */
        op_ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (op_ret < 0) {
            op_errno = -op_ret;
            goto out;
        }

        op_ret = 0;
out:
        if (op_ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (fallocate, frame, op_ret, op_errno, NULL,
                                  NULL, NULL);
        }

        return 0;
}

int
afr_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
               off_t offset, size_t len, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }
        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->cont.fallocate.mode = mode;
        local->cont.fallocate.offset  = offset;
        local->cont.fallocate.len = len;

        local->fd = fd_ref (fd);

        afr_open_fd_fix (fd, this);

        afr_do_fallocate (frame, this);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
        }

        return 0;
}

/* }}} */

/* {{{ discard */

static int
afr_discard_unwind (call_frame_t *frame, xlator_t *this)
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
                AFR_STACK_UNWIND (discard, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.inode_wfop.prebuf,
                                  &local->cont.inode_wfop.postbuf,
                                  NULL);
        }
        return 0;
}

static int
afr_discard_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
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

                __inode_write_fop_cbk (frame, child_index, read_child, this,
                                       &op_ret, &op_errno, prebuf, postbuf,
                                       xdata);

                if ((local->success_count >= priv->wait_count)
                    && local->read_child_returned) {
                        need_unwind = 1;
                }
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

static int
afr_discard_wind (call_frame_t *frame, xlator_t *this)
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
                        STACK_WIND_COOKIE (frame, afr_discard_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->discard,
                                           local->fd,
                                           local->cont.discard.offset,
                                           local->cont.discard.len,
                                           NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}

static int
afr_discard_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

static int
afr_do_discard (call_frame_t *frame, xlator_t *this)
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

        local->op = GF_FOP_DISCARD;

        local->transaction.fop    = afr_discard_wind;
        local->transaction.done   = afr_discard_done;
        local->transaction.unwind = afr_discard_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.discard.offset;
        local->transaction.len     = 0;

        op_ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (op_ret < 0) {
            op_errno = -op_ret;
            goto out;
        }

        op_ret = 0;
out:
        if (op_ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (discard, frame, op_ret, op_errno, NULL,
                                  NULL, NULL);
        }

        return 0;
}

int
afr_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             size_t len, dict_t *xdata)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
        int ret = -1;
        int op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }
        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->cont.discard.offset  = offset;
        local->cont.discard.len = len;

        local->fd = fd_ref (fd);

        afr_open_fd_fix (fd, this);

        afr_do_discard(frame, this);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);
                AFR_STACK_UNWIND (discard, frame, -1, op_errno, NULL, NULL, NULL);
        }

        return 0;
}


/* {{{ zerofill */

static int
afr_zerofill_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local            = NULL;
        call_frame_t    *main_frame       = NULL;

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
                AFR_STACK_UNWIND (zerofill, main_frame, local->op_ret,
                                  local->op_errno,
                                  &local->cont.zerofill.prebuf,
                                  &local->cont.zerofill.postbuf,
                                  NULL);
        }
        return 0;
}

static int
afr_zerofill_wind_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t       *local             = NULL;
        afr_private_t     *priv              = NULL;
        int                child_index       = (long) cookie;
        int                call_count        = -1;
        int                need_unwind       = 0;
        int                read_child        = 0;

        local = frame->local;
        priv  = this->private;

        read_child = afr_inode_get_read_ctx (this, local->fd->inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (afr_fop_failed (op_ret, op_errno)) {
                        afr_transaction_fop_failed (frame, this, child_index);
                }

                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                                local->cont.zerofill.prebuf  = *prebuf;
                                local->cont.zerofill.postbuf = *postbuf;
                        }

                        if (child_index == read_child) {
                                local->cont.zerofill.prebuf  = *prebuf;
                                local->cont.zerofill.postbuf = *postbuf;
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

        if (need_unwind) {
                local->transaction.unwind (frame, this);
        }
        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
        }

        return 0;
}

static int
afr_zerofill_wind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t    *local         = NULL;
        afr_private_t  *priv          = NULL;
        int             call_count    = -1;
        int             i             = 0;

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
                        STACK_WIND_COOKIE (frame, afr_zerofill_wind_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->zerofill,
                                           local->fd,
                                           local->cont.zerofill.offset,
                                           local->cont.zerofill.len,
                                           NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}

static int
afr_zerofill_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
}

static int
afr_do_zerofill(call_frame_t *frame, xlator_t *this)
{
        call_frame_t  *transaction_frame = NULL;
        afr_local_t   *local             = NULL;
        int            op_ret            = -1;
        int            op_errno          = 0;

        local = frame->local;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                goto out;
        }

        transaction_frame->local = local;
        frame->local = NULL;

        local->op = GF_FOP_ZEROFILL;

        local->transaction.fop    = afr_zerofill_wind;
        local->transaction.done   = afr_zerofill_done;
        local->transaction.unwind = afr_zerofill_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.zerofill.offset;
        local->transaction.len     = 0;

        op_ret = afr_transaction (transaction_frame, this,
                                  AFR_DATA_TRANSACTION);
        if (op_ret < 0) {
                op_errno = -op_ret;
                goto out;
        }

        op_ret = 0;
out:
        if (op_ret < 0) {
                if (transaction_frame) {
                        AFR_STACK_DESTROY (transaction_frame);
                }
                AFR_STACK_UNWIND (zerofill, frame, op_ret, op_errno, NULL,
                                  NULL, NULL);
        }

        return 0;
}

int
afr_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              off_t len, dict_t *xdata)
{
        afr_private_t   *priv               = NULL;
        afr_local_t     *local              = NULL;
        call_frame_t    *transaction_frame  = NULL;
        int              ret                = -1;
        int              op_errno           = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        if (afr_is_split_brain (this, fd->inode)) {
                op_errno = EIO;
                goto out;
        }
        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0) {
                goto out;
        }
        local->cont.zerofill.offset  = offset;
        local->cont.zerofill.len = len;

        local->fd = fd_ref (fd);

        afr_open_fd_fix (fd, this);

        afr_do_zerofill(frame, this);

        ret = 0;
out:
        if (ret < 0) {
                if (transaction_frame) {
                        AFR_STACK_DESTROY (transaction_frame);
                }
                AFR_STACK_UNWIND (zerofill, frame, -1, op_errno, NULL,
                                  NULL, NULL);
        }

        return 0;
}

/* }}} */


