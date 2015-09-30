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


static void
__afr_inode_write_finalize (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int read_subvol = 0;
	int i = 0;

	local = frame->local;
	priv = this->private;

	if (local->inode) {
		if (local->transaction.type == AFR_METADATA_TRANSACTION)
			read_subvol = afr_metadata_subvol_get (local->inode, this,
							       NULL, NULL);
		else
			read_subvol = afr_data_subvol_get (local->inode, this,
							   NULL, NULL);
	}

	local->op_ret = -1;
	local->op_errno = afr_final_errno (local, priv);

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].valid)
			continue;
		if (local->replies[i].op_ret < 0) {
			afr_inode_read_subvol_reset (local->inode, this);
			continue;
		}

		/* Order of checks in the compound conditional
		   below is important.

		   - Highest precedence: largest op_ret
		   - Next precendence: if all op_rets are equal, read subvol
		   - Least precedence: any succeeded subvol
		*/
		if ((local->op_ret < local->replies[i].op_ret) ||
		    ((local->op_ret == local->replies[i].op_ret) &&
		     (i == read_subvol))) {

			local->op_ret = local->replies[i].op_ret;
			local->op_errno = local->replies[i].op_errno;

			local->cont.inode_wfop.prebuf =
				local->replies[i].prestat;
			local->cont.inode_wfop.postbuf =
				local->replies[i].poststat;

			if (local->replies[i].xdata) {
				if (local->xdata_rsp)
					dict_unref (local->xdata_rsp);
				local->xdata_rsp =
					dict_ref (local->replies[i].xdata);
			}
		}
	}
}


static void
__afr_inode_write_fill (call_frame_t *frame, xlator_t *this, int child_index,
			int op_ret, int op_errno,
			struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *local = NULL;

        local = frame->local;

	local->replies[child_index].valid = 1;
	local->replies[child_index].op_ret = op_ret;
	local->replies[child_index].op_errno = op_errno;

	if (op_ret >= 0) {
		if (prebuf)
			local->replies[child_index].prestat = *prebuf;
		if (postbuf)
			local->replies[child_index].poststat = *postbuf;
		if (xdata)
			local->replies[child_index].xdata = dict_ref (xdata);
	} else {
		afr_transaction_fop_failed (frame, this, child_index);
	}

        return;
}


static int
__afr_inode_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int child_index = (long) cookie;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                __afr_inode_write_fill (frame, this, child_index, op_ret,
					op_errno, prebuf, postbuf, xdata);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		__afr_inode_write_finalize (frame, this);

		if (afr_txn_nothing_failed (frame, this))
			local->transaction.unwind (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
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
	if (src_local->xdata_rsp)
		dst_local->xdata_rsp = dict_ref (src_local->xdata_rsp);
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
                          local->xdata_rsp);
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
                        afr_transaction_fop_failed (frame, this, i);
        }
}

int
afr_writev_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *   local = NULL;
        call_frame_t    *fop_frame = NULL;
        int child_index = (long) cookie;
        int call_count  = -1;
        int ret = 0;
        uint32_t open_fd_count = 0;
        uint32_t write_is_append = 0;

        local = frame->local;

        LOCK (&frame->lock);
        {
                __afr_inode_write_fill (frame, this, child_index, op_ret,
					op_errno, prebuf, postbuf, xdata);
		if (op_ret == -1 || !xdata)
			goto unlock;

		write_is_append = 0;
		ret = dict_get_uint32 (xdata, GLUSTERFS_WRITE_IS_APPEND,
				       &write_is_append);
		if (ret || !write_is_append)
			local->append_write = _gf_false;

		ret = dict_get_uint32 (xdata, GLUSTERFS_OPEN_FD_COUNT,
				       &open_fd_count);
		if (ret == -1)
			goto unlock;
		if ((open_fd_count > local->open_fd_count)) {
			local->open_fd_count = open_fd_count;
			local->update_open_fd_count = _gf_true;
		}
        }
unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		if (!local->stable_write && !local->append_write)
			/* An appended write removes the necessity to
			   fsync() the file. This is because self-heal
			   has the logic to check for larger file when
			   the xattrs are not reliably pointing at
			   a stale file.
			*/
			afr_fd_report_unstable_write (this, local->fd);

		__afr_inode_write_finalize (frame, this);

                afr_writev_handle_short_writes (frame, this);

                if (local->update_open_fd_count)
                        afr_handle_open_fd_count (frame, this);

                if (!afr_txn_nothing_failed (frame, this)) {
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
afr_writev_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_writev_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->writev,
			   local->fd, local->cont.writev.vector,
			   local->cont.writev.count, local->cont.writev.offset,
			   local->cont.writev.flags, local->cont.writev.iobref,
			   local->xdata_req);
        return 0;
}


int
afr_do_writev (call_frame_t *frame, xlator_t *this)
{
        call_frame_t    *transaction_frame = NULL;
        afr_local_t     *local             = NULL;
        int             ret   = -1;
        int             op_errno = ENOMEM;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

        local = frame->local;
        transaction_frame->local = local;
	frame->local = NULL;

	if (!AFR_FRAME_INIT (frame, op_errno))
		goto out;

        local->op = GF_FOP_WRITE;

        local->transaction.wind   = afr_writev_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
            op_errno = -ret;
            goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int
afr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int op_errno = ENOMEM;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        local->cont.writev.vector = iov_dup (vector, count);
	if (!local->cont.writev.vector)
		goto out;
        local->cont.writev.count      = count;
        local->cont.writev.offset     = offset;
        local->cont.writev.flags      = flags;
        local->cont.writev.iobref     = iobref_ref (iobref);

	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	if (dict_set_uint32 (local->xdata_req, GLUSTERFS_OPEN_FD_COUNT, 4)) {
		op_errno = ENOMEM;
		goto out;
	}

	if (dict_set_uint32 (local->xdata_req, GLUSTERFS_WRITE_IS_APPEND, 4)) {
		op_errno = ENOMEM;
		goto out;
	}

	/* Set append_write to be true speculatively. If on any
	   server it turns not be true, we unset it in the
	   callback.
	*/
	local->append_write = _gf_true;

	/* detect here, but set it in writev_wind_cbk *after* the unstable
	   write is performed
	*/
	local->stable_write = !!((fd->flags|flags)&(O_SYNC|O_DSYNC));

        afr_fix_open (fd, this);

        afr_do_writev (frame, this);

	return 0;
out:
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

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (truncate, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf, local->xdata_rsp);
        return 0;
}


int
afr_truncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (op_ret == 0 && prebuf->ia_size != postbuf->ia_size)
		local->stable_write = _gf_false;

	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      prebuf, postbuf, xdata);
}


int
afr_truncate_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_truncate_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->truncate,
			   &local->loc, local->cont.truncate.offset,
			   local->xdata_req);
        return 0;
}


int
afr_truncate (call_frame_t *frame, xlator_t *this,
              loc_t *loc, off_t offset, dict_t *xdata)
{
        afr_local_t   * local = NULL;
        call_frame_t   *transaction_frame = NULL;
	int ret = -1;
        int op_errno = ENOMEM;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.truncate.offset  = offset;
	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

	local->transaction.wind   = afr_truncate_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_truncate_unwind;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);

        local->op = GF_FOP_TRUNCATE;

        local->transaction.main_frame = frame;
        local->transaction.start   = offset;
        local->transaction.len     = 0;

	/* Set it true speculatively, will get reset in afr_truncate_wind_cbk
	   if truncate was not a NOP */
	local->stable_write = _gf_true;

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (ftruncate, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf, local->xdata_rsp);
        return 0;
}


int
afr_ftruncate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (op_ret == 0 && prebuf->ia_size != postbuf->ia_size)
		local->stable_write = _gf_false;

	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      prebuf, postbuf, xdata);
}


int
afr_ftruncate_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

	local = frame->local;
	priv = this->private;

	STACK_WIND_COOKIE (frame, afr_ftruncate_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->ftruncate,
			   local->fd, local->cont.ftruncate.offset,
			   local->xdata_req);
        return 0;
}


int
afr_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	       dict_t *xdata)
{
        afr_local_t *local = NULL;
	call_frame_t *transaction_frame = NULL;
	int ret = -1;
        int op_errno = ENOMEM;

	transaction_frame = copy_frame (frame);
	if (!frame)
		goto out;

        local = AFR_FRAME_INIT (transaction_frame, op_errno);
        if (!local)
		goto out;

        local->cont.ftruncate.offset  = offset;
	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

        local->op = GF_FOP_FTRUNCATE;

	local->transaction.wind   = afr_ftruncate_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_ftruncate_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.ftruncate.offset;
        local->transaction.len     = 0;

        afr_fix_open (fd, this);

	/* Set it true speculatively, will get reset in afr_ftruncate_wind_cbk
	   if truncate was not a NOP */
	local->stable_write = _gf_true;

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	AFR_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

/* }}} */

/* {{{ setattr */

int
afr_setattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        call_frame_t *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (setattr, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf,
			  local->xdata_rsp);
        return 0;
}


int
afr_setattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno,
                      struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      preop, postop, xdata);
}


int
afr_setattr_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_setattr_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->setattr,
			   &local->loc, &local->cont.setattr.in_buf,
			   local->cont.setattr.valid, local->xdata_req);
        return 0;
}


int
afr_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *buf,
	     int32_t valid, dict_t *xdata)
{
        afr_local_t *local = NULL;
        call_frame_t *transaction_frame = NULL;
        int ret = -1;
        int op_errno = ENOMEM;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.setattr.in_buf = *buf;
        local->cont.setattr.valid  = valid;
	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_setattr_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_setattr_unwind;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);

	local->op = GF_FOP_SETATTR;

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

/* {{{ fsetattr */

int
afr_fsetattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (fsetattr, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf, local->xdata_rsp);
        return 0;
}


int
afr_fsetattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      preop, postop, xdata);
}


int
afr_fsetattr_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_fsetattr_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->fsetattr,
			   local->fd, &local->cont.fsetattr.in_buf,
			   local->cont.fsetattr.valid, local->xdata_req);
        return 0;
}


int
afr_fsetattr (call_frame_t *frame, xlator_t *this,
              fd_t *fd, struct iatt *buf, int32_t valid, dict_t *xdata)
{
        afr_local_t *local = NULL;
        call_frame_t *transaction_frame = NULL;
        int ret = -1;
        int op_errno = ENOMEM;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.fsetattr.in_buf = *buf;
        local->cont.fsetattr.valid  = valid;
	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_fsetattr_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_fsetattr_unwind;

        local->fd                 = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	local->op = GF_FOP_FSETATTR;

        afr_fix_open (fd, this);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


/* {{{ setxattr */


int
afr_setxattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (setxattr, main_frame, local->op_ret, local->op_errno,
			  local->xdata_rsp);
        return 0;
}


int
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      NULL, NULL, xdata);
}


int
afr_setxattr_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t      *local         = NULL;
        afr_private_t    *priv          = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_setxattr_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->setxattr,
			   &local->loc, local->cont.setxattr.dict,
			   local->cont.setxattr.flags, local->xdata_req);
        return 0;
}


int
afr_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
	      int32_t flags, dict_t *xdata)
{
        afr_local_t    *local             = NULL;
        call_frame_t   *transaction_frame = NULL;
        int             ret               = -1;
        int             op_errno          = EINVAL;

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.afr.*", dict,
                                   op_errno, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.afr.*", dict,
                                   op_errno, out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
                goto out;

        local->cont.setxattr.dict  = dict_ref (dict);
        local->cont.setxattr.flags = flags;
	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_setxattr_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_setxattr_unwind;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

	local->op = GF_FOP_SETXATTR;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        return 0;
}

/* {{{ fsetxattr */


int
afr_fsetxattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t    *local         = NULL;
        call_frame_t   *main_frame    = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (fsetxattr, main_frame, local->op_ret, local->op_errno,
			  local->xdata_rsp);
        return 0;
}


int
afr_fsetxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      NULL, NULL, xdata);
}


int
afr_fsetxattr_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t        *local       = NULL;
        afr_private_t      *priv        = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_fsetxattr_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->fsetxattr,
			   local->fd, local->cont.fsetxattr.dict,
			   local->cont.fsetxattr.flags, local->xdata_req);
        return 0;
}


int
afr_fsetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata)
{
        afr_local_t      *local             = NULL;
        call_frame_t     *transaction_frame = NULL;
        int               ret               = -1;
        int               op_errno          = ENOMEM;

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.afr.*", dict,
                                   op_errno, out);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.afr.*", dict,
                                   op_errno, out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.fsetxattr.dict  = dict_ref (dict);
        local->cont.fsetxattr.flags = flags;

	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_fsetxattr_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_fsetxattr_unwind;

        local->fd                 = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	local->op = GF_FOP_FSETXATTR;

        local->transaction.main_frame = frame;
        local->transaction.start  = LLONG_MAX - 1;
        local->transaction.len    = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (removexattr, main_frame, local->op_ret, local->op_errno,
			  local->xdata_rsp);
        return 0;
}


int
afr_removexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      NULL, NULL, xdata);
}


int
afr_removexattr_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

	local = frame->local;
	priv = this->private;

	STACK_WIND_COOKIE (frame, afr_removexattr_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->removexattr,
			   &local->loc, local->cont.removexattr.name,
			   local->xdata_req);
        return 0;
}


int
afr_removexattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, const char *name, dict_t *xdata)
{
        afr_local_t     *local             = NULL;
        call_frame_t    *transaction_frame = NULL;
        int              ret               = -1;
        int              op_errno          = ENOMEM;

        GF_IF_NATIVE_XATTR_GOTO ("trusted.afr.*",
                                 name, op_errno, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.afr.*",
                                 name, op_errno, out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.removexattr.name = gf_strdup (name);

	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_removexattr_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_removexattr_unwind;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);

	local->op = GF_FOP_REMOVEXATTR;

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (removexattr, frame, -1, op_errno, NULL);
        return 0;
}

/* ffremovexattr */
int
afr_fremovexattr_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (fremovexattr, main_frame, local->op_ret, local->op_errno,
			  local->xdata_rsp);
        return 0;
}


int
afr_fremovexattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      NULL, NULL, xdata);
}


int
afr_fremovexattr_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_fremovexattr_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->fremovexattr,
			   local->fd, local->cont.removexattr.name,
			   local->xdata_req);
        return 0;
}


int
afr_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  const char *name, dict_t *xdata)
{
        afr_local_t *local = NULL;
        call_frame_t *transaction_frame = NULL;
        int ret = -1;
        int op_errno = ENOMEM;

        GF_IF_NATIVE_XATTR_GOTO ("trusted.afr.*",
                                 name, op_errno, out);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.afr.*",
                                 name, op_errno, out);

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
                goto out;

        local->cont.removexattr.name = gf_strdup (name);
	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_fremovexattr_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_fremovexattr_unwind;

        local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	local->op = GF_FOP_FREMOVEXATTR;

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (fremovexattr, frame, -1, op_errno, NULL);

        return 0;
}


int
afr_fallocate_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (fallocate, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf, local->xdata_rsp);
        return 0;
}


int
afr_fallocate_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
        return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      prebuf, postbuf, xdata);
}


int
afr_fallocate_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_fallocate_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->fallocate,
			   local->fd, local->cont.fallocate.mode,
			   local->cont.fallocate.offset,
			   local->cont.fallocate.len, local->xdata_req);
        return 0;
}


int
afr_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
               off_t offset, size_t len, dict_t *xdata)
{
        call_frame_t *transaction_frame = NULL;
        afr_local_t *local = NULL;
        int ret = -1;
        int op_errno = ENOMEM;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.fallocate.mode = mode;
        local->cont.fallocate.offset  = offset;
        local->cont.fallocate.len = len;

        local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_FALLOCATE;

        local->transaction.wind   = afr_fallocate_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_fallocate_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.fallocate.offset;
        local->transaction.len     = 0;

        afr_fix_open (fd, this);

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


/* }}} */

/* {{{ discard */

int
afr_discard_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (discard, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf, local->xdata_rsp);
        return 0;
}


int
afr_discard_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      prebuf, postbuf, xdata);
}


int
afr_discard_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_discard_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->discard,
			   local->fd, local->cont.discard.offset,
			   local->cont.discard.len, local->xdata_req);
        return 0;
}


int
afr_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             size_t len, dict_t *xdata)
{
        afr_local_t *local = NULL;
        call_frame_t *transaction_frame = NULL;
        int ret = -1;
        int op_errno = ENOMEM;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame)
		goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.discard.offset  = offset;
        local->cont.discard.len = len;

        local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_DISCARD;

        local->transaction.wind   = afr_discard_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_discard_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.discard.offset;
        local->transaction.len     = 0;

        afr_fix_open (fd, this);

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (discard, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


/* {{{ zerofill */

int
afr_zerofill_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        call_frame_t   *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (discard, main_frame, local->op_ret, local->op_errno,
			  &local->cont.inode_wfop.prebuf,
			  &local->cont.inode_wfop.postbuf, local->xdata_rsp);
        return 0;
}


int
afr_zerofill_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      prebuf, postbuf, xdata);
}


int
afr_zerofill_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_zerofill_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->zerofill,
			   local->fd, local->cont.zerofill.offset,
			   local->cont.zerofill.len, local->xdata_req);
        return 0;
}

int
afr_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             size_t len, dict_t *xdata)
{
        afr_local_t *local = NULL;
        call_frame_t *transaction_frame = NULL;
        int ret = -1;
        int op_errno = ENOMEM;

	transaction_frame = copy_frame (frame);
	if (!transaction_frame)
		goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        local->cont.zerofill.offset  = offset;
        local->cont.zerofill.len = len;

        local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	if (xdata)
		local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_ZEROFILL;

        local->transaction.wind   = afr_zerofill_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_zerofill_unwind;

        local->transaction.main_frame = frame;

        local->transaction.start   = local->cont.discard.offset;
        local->transaction.len     = len;

        afr_fix_open (fd, this);

        ret = afr_transaction (transaction_frame, this, AFR_DATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (zerofill, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

/* }}} */
