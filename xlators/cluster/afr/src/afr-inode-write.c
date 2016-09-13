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
#include "protocol-common.h"
#include "byte-order.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-messages.h"

static void
__afr_inode_write_finalize (call_frame_t *frame, xlator_t *this)
{
	int                       i               = 0;
        int                       ret             = 0;
	int                       read_subvol     = 0;
        struct iatt              *stbuf           = NULL;
	afr_local_t              *local           = NULL;
	afr_private_t            *priv            = NULL;
        afr_read_subvol_args_t    args            = {0,};

	local = frame->local;
	priv = this->private;

        /*This code needs to stay till DHT sends fops on linked
         * inodes*/
        if (local->inode && !inode_is_linked (local->inode)) {
                for (i = 0; i < priv->child_count; i++) {
                        if (!local->replies[i].valid)
                                continue;
                        if (local->replies[i].op_ret == -1)
                                continue;
                        if (!gf_uuid_is_null
                                        (local->replies[i].poststat.ia_gfid)) {
                                gf_uuid_copy (args.gfid,
                                            local->replies[i].poststat.ia_gfid);
                                args.ia_type =
                                        local->replies[i].poststat.ia_type;
                                break;
                        } else {
                                ret = dict_get_bin (local->replies[i].xdata,
                                                    DHT_IATT_IN_XDATA_KEY,
                                                    (void **) &stbuf);
                                if (ret)
                                        continue;
                                gf_uuid_copy (args.gfid, stbuf->ia_gfid);
                                args.ia_type = stbuf->ia_type;
                                break;
                        }
                }
        }

	if (local->inode) {
		if (local->transaction.type == AFR_METADATA_TRANSACTION)
                        read_subvol = afr_metadata_subvol_get (local->inode,
                                      this, NULL, local->readable, NULL, &args);
		else
			read_subvol = afr_data_subvol_get (local->inode, this,
                                            NULL, local->readable, NULL, &args);
	}

	local->op_ret = -1;
	local->op_errno = afr_final_errno (local, priv);
        afr_pick_error_xdata (local, priv, local->inode, local->readable, NULL,
                              NULL);

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].valid)
			continue;
		if (local->replies[i].op_ret < 0)
			continue;

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
			if (local->replies[i].xattr) {
				if (local->xattr_rsp)
					dict_unref (local->xattr_rsp);
				local->xattr_rsp =
					dict_ref (local->replies[i].xattr);
			}
		}
	}

        afr_txn_arbitrate_fop_cbk (frame, this);
        afr_set_in_flight_sb_status (this, local, local->inode);
}


static void
__afr_inode_write_fill (call_frame_t *frame, xlator_t *this, int child_index,
			int op_ret, int op_errno,
			struct iatt *prebuf, struct iatt *postbuf,
			dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	local->replies[child_index].valid = 1;

        if (AFR_IS_ARBITER_BRICK(priv, child_index) && op_ret == 1)
                op_ret = iov_length (local->cont.writev.vector,
                                     local->cont.writev.count);

	local->replies[child_index].op_ret = op_ret;
	local->replies[child_index].op_errno = op_errno;
        if (xdata)
                local->replies[child_index].xdata = dict_ref (xdata);

	if (op_ret >= 0) {
		if (prebuf)
			local->replies[child_index].prestat = *prebuf;
		if (postbuf)
			local->replies[child_index].poststat = *postbuf;
		if (xattr)
			local->replies[child_index].xattr = dict_ref (xattr);
	} else {
		afr_transaction_fop_failed (frame, this, child_index);
	}

        return;
}


static int
__afr_inode_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int child_index = (long) cookie;
        int call_count = -1;
        afr_private_t *priv = NULL;

        priv = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                __afr_inode_write_fill (frame, this, child_index, op_ret,
					op_errno, prebuf, postbuf, xattr,
					xdata);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		__afr_inode_write_finalize (frame, this);

		if (afr_txn_nothing_failed (frame, this)) {
                        /*if it did pre-op, it will do post-op changing ctime*/
                        if (priv->consistent_metadata &&
                            afr_needs_changelog_update (local))
                                afr_zero_fill_stat (local);
                        local->transaction.unwind (frame, this);
                }

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
        afr_private_t *priv = this->private;

        local = frame->local;

       if (priv->consistent_metadata)
               afr_zero_fill_stat (local);

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

void
afr_inode_write_fill (call_frame_t *frame, xlator_t *this, int child_index,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        int ret = 0;
        afr_local_t *local = frame->local;
        uint32_t open_fd_count = 0;
        uint32_t write_is_append = 0;

        LOCK (&frame->lock);
        {
                __afr_inode_write_fill (frame, this, child_index, op_ret,
					op_errno, prebuf, postbuf, NULL, xdata);
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
		if (open_fd_count > local->open_fd_count) {
                        local->open_fd_count = open_fd_count;
                        local->update_open_fd_count = _gf_true;
		}
        }
unlock:
        UNLOCK (&frame->lock);
}

void
afr_process_post_writev (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;

        local = frame->local;

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

}

int
afr_writev_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t     *local = NULL;
        call_frame_t    *fop_frame = NULL;
        int child_index = (long) cookie;
        int call_count  = -1;

        local = frame->local;

        afr_inode_write_fill (frame, this, child_index, op_ret, op_errno,
                              prebuf, postbuf, xdata);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_process_post_writev (frame, this);

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

static int
afr_arbiter_writev_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = frame->local;
        afr_private_t *priv = this->private;
        static char byte = 0xFF;
        static struct iovec vector = {&byte, 1};
        int32_t count = 1;

        STACK_WIND_COOKIE (frame, afr_writev_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->writev,
			   local->fd, &vector, count, local->cont.writev.offset,
			   local->cont.writev.flags, local->cont.writev.iobref,
			   local->xdata_req);

        return 0;
}

int
afr_writev_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

        if (AFR_IS_ARBITER_BRICK(priv, subvol)) {
                afr_arbiter_writev_wind (frame, this, subvol);
                return 0;
        }

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
                * give consistency guarantee. The actual write may happen at a
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
				      prebuf, postbuf, NULL, xdata);
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
				      prebuf, postbuf, NULL, xdata);
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
	if (!transaction_frame)
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
				      preop, postop, NULL, xdata);
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
				      preop, postop, NULL, xdata);
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
				      NULL, NULL, NULL, xdata);
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
afr_emptyb_set_pending_changelog_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this, int op_ret, int op_errno,
                                  dict_t *xattr, dict_t *xdata)

{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int i, ret = 0;
        char *op_type = NULL;

        local = frame->local;
        priv = this->private;
        i = (long) cookie;

        local->replies[i].valid = 1;
        local->replies[i].op_ret = op_ret;
        local->replies[i].op_errno = op_errno;

        ret = dict_get_str (local->xdata_req, "replicate-brick-op", &op_type);
        if (ret)
                goto out;

        gf_msg (this->name, op_ret ? GF_LOG_ERROR : GF_LOG_INFO,
                op_ret ? op_errno : 0,
                afr_get_msg_id (op_type),
                "Set of pending xattr %s on"
                " %s.", op_ret ? "failed" : "succeeded",
                priv->children[i]->name);

out:
        syncbarrier_wake (&local->barrier);
        return 0;
}

int
afr_emptyb_set_pending_changelog (call_frame_t *frame, xlator_t *this,
                                unsigned char *locked_nodes)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int ret = 0, i = 0;

        local = frame->local;
        priv = this->private;

        AFR_ONLIST (locked_nodes, frame, afr_emptyb_set_pending_changelog_cbk,
                    xattrop, &local->loc, GF_XATTROP_ADD_ARRAY,
                    local->xattr_req, NULL);

        /* It is sufficient if xattrop was successful on one child */
        for (i = 0; i < priv->child_count; i++) {
                if (!local->replies[i].valid)
                        continue;

                if (local->replies[i].op_ret == 0) {
                        ret = 0;
                        goto out;
                } else {
                        ret = afr_higher_errno (ret,
                                                local->replies[i].op_errno);
                }
        }
out:
        return -ret;
}

int
_afr_handle_empty_brick_type (xlator_t *this, call_frame_t *frame,
                            loc_t *loc, int empty_index,
                            afr_transaction_type type,
                            char *op_type)
{
        int              count            = 0;
        int              ret              = -ENOMEM;
        int              idx              = -1;
        int              d_idx            = -1;
        unsigned char   *locked_nodes     = NULL;
        afr_local_t     *local            = NULL;
        afr_private_t   *priv             = NULL;

        priv = this->private;
        local = frame->local;

        locked_nodes = alloca0 (priv->child_count);

        idx = afr_index_for_transaction_type (type);
        d_idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);

        local->pending = afr_matrix_create (priv->child_count,
                                            AFR_NUM_CHANGE_LOGS);
        if (!local->pending)
                goto out;

        local->pending[empty_index][idx] = hton32 (1);

        if ((priv->esh_granular) && (type == AFR_ENTRY_TRANSACTION))
                        local->pending[empty_index][d_idx] = hton32 (1);

        local->xdata_req = dict_new ();
        if (!local->xdata_req)
                goto out;

        ret = dict_set_str (local->xdata_req, "replicate-brick-op", op_type);
        if (ret)
                goto out;

        local->xattr_req = dict_new ();
        if (!local->xattr_req)
                goto out;

        ret = afr_set_pending_dict (priv, local->xattr_req, local->pending);
        if (ret < 0)
                goto out;

        if (AFR_ENTRY_TRANSACTION == type) {
                count = afr_selfheal_entrylk (frame, this, loc->inode,
                                              this->name, NULL, locked_nodes);
        } else {
                count = afr_selfheal_inodelk (frame, this, loc->inode,
                                              this->name, LLONG_MAX - 1, 0,
                                              locked_nodes);
        }

        if (!count) {
                gf_msg (this->name, GF_LOG_ERROR, EAGAIN,
                        AFR_MSG_REPLACE_BRICK_STATUS, "Couldn't acquire lock on"
                        " any child.");
                ret = -EAGAIN;
                goto unlock;
        }

        ret = afr_emptyb_set_pending_changelog (frame, this, locked_nodes);
        if (ret)
                goto unlock;
        ret = 0;
unlock:
        if (AFR_ENTRY_TRANSACTION == type) {
                afr_selfheal_unentrylk (frame, this, loc->inode, this->name,
                                        NULL, locked_nodes, NULL);
        } else {
                afr_selfheal_uninodelk (frame, this, loc->inode, this->name,
                                        LLONG_MAX - 1, 0, locked_nodes);
        }
out:
        return ret;
}

void
afr_brick_args_cleanup (void *opaque)
{
        afr_empty_brick_args_t *data = NULL;

        data = opaque;
        loc_wipe (&data->loc);
        GF_FREE (data);
}

int
_afr_handle_empty_brick_cbk (int ret, call_frame_t *frame, void *opaque)
{
        afr_brick_args_cleanup (opaque);
        return 0;
}

int
_afr_handle_empty_brick (void *opaque)
{

        afr_local_t     *local          = NULL;
        afr_private_t   *priv           = NULL;
        int              empty_index       = -1;
        int              ret            = -1;
        int              op_errno       = ENOMEM;
        call_frame_t    *frame          = NULL;
        xlator_t        *this           = NULL;
        char            *op_type        = NULL;
        afr_empty_brick_args_t *data  = NULL;

        data = opaque;
        frame = data->frame;
        empty_index = data->empty_index;
        op_type = data->op_type;
        this = frame->this;
        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local)
                goto out;

        loc_copy (&local->loc, &data->loc);

        gf_msg (this->name, GF_LOG_INFO, 0, 0, "New brick is : %s",
                priv->children[empty_index]->name);

        ret = _afr_handle_empty_brick_type (this, frame, &local->loc, empty_index,
                                          AFR_METADATA_TRANSACTION, op_type);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }

        dict_unref (local->xdata_req);
        dict_unref (local->xattr_req);
        afr_matrix_cleanup (local->pending, priv->child_count);
        local->pending = NULL;
        local->xattr_req = NULL;
        local->xdata_req = NULL;

        ret = _afr_handle_empty_brick_type (this, frame, &local->loc, empty_index,
                                          AFR_ENTRY_TRANSACTION, op_type);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        AFR_STACK_UNWIND (setxattr, frame, ret, op_errno, NULL);
        return 0;
}


int
afr_split_brain_resolve_do (call_frame_t *frame, xlator_t *this, loc_t *loc,
                            char *data)
{
        afr_local_t    *local             = NULL;
        int     ret                       = -1;
        int     op_errno                  = EINVAL;

        local = frame->local;
        local->xdata_req = dict_new ();

        if (!local->xdata_req) {
                op_errno = ENOMEM;
                goto out;
        }

        ret = dict_set_int32 (local->xdata_req, "heal-op",
                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }
        ret = dict_set_str (local->xdata_req, "child-name", data);
        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }
        /* set spb choice to -1 whether heal succeeds or not:
         * If heal succeeds : spb-choice should be set to -1 as
         *                    it is no longer valid; file is not
         *                    in split-brain anymore.
         * If heal doesn't succeed:
         *                    spb-choice should be set to -1
         *                    otherwise reads will be served
         *                    from spb-choice which is misleading.
         */
        ret = afr_inode_split_brain_choice_set (loc->inode, this, -1);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR, "Failed to set"
                        "split-brain choice to -1");
        afr_heal_splitbrain_file (frame, this, loc);
        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);
        return 0;
}

int
afr_get_split_brain_child_index (xlator_t *this, void *value, size_t len)
{
        int             spb_child_index   = -1;
        char           *spb_child_str     = NULL;

        spb_child_str =  alloca0 (len + 1);
        memcpy (spb_child_str, value, len);

        if (!strcmp (spb_child_str, "none"))
                return -2;

        spb_child_index = afr_get_child_index_from_name (this,
                                                         spb_child_str);
        if (spb_child_index < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        AFR_MSG_INVALID_SUBVOL, "Invalid subvol: %s",
                        spb_child_str);
        }
        return spb_child_index;
}

int
afr_can_set_split_brain_choice (void *opaque)
{
        afr_spbc_timeout_t        *data         = opaque;
        call_frame_t              *frame        = NULL;
        xlator_t                  *this         = NULL;
        loc_t                     *loc          = NULL;
        int                        ret          = -1;

        frame = data->frame;
        loc = data->loc;
        this = frame->this;

        ret = afr_is_split_brain (frame, this, loc->inode, loc->gfid,
                                  &data->d_spb, &data->m_spb);

        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
                        "Failed to determine if %s"
                        " is in split-brain. "
                        "Aborting split-brain-choice set.",
                        uuid_utoa (loc->gfid));
        return ret;
}

int
afr_handle_split_brain_commands (xlator_t *this, call_frame_t *frame,
                                loc_t *loc, dict_t *dict)
{
        void           *value             = NULL;
        afr_private_t  *priv              = NULL;
        afr_local_t    *local             = NULL;
        afr_spbc_timeout_t *data          = NULL;
        int             len               = 0;
        int             spb_child_index   = -1;
        int             ret               = -1;
        int             op_errno          = EINVAL;

        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local) {
                ret = 1;
                goto out;
        }

        local->op = GF_FOP_SETXATTR;

        ret =  dict_get_ptr_and_len (dict, GF_AFR_SBRAIN_CHOICE, &value,
                                     &len);
        if (value) {
                spb_child_index = afr_get_split_brain_child_index (this, value,
                                                                   len);
                if (spb_child_index < 0) {
                        /* Case where value was "none" */
                        if (spb_child_index == -2)
                                spb_child_index = -1;
                        else {
                                ret = 1;
                                op_errno = EINVAL;
                                goto out;
                        }
                }

                data = GF_CALLOC (1, sizeof (*data), gf_afr_mt_spbc_timeout_t);
                if (!data) {
                        ret = 1;
                        goto out;
                }
                data->spb_child_index = spb_child_index;
                data->frame = frame;
                loc_copy (&local->loc, loc);
                data->loc = &local->loc;
                ret = synctask_new (this->ctx->env,
                                    afr_can_set_split_brain_choice,
                                    afr_set_split_brain_choice, NULL, data);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
                                "Failed to create"
                                " synctask. Aborting split-brain choice set"
                                " for %s", loc->name);
                        ret = 1;
                        op_errno = ENOMEM;
                        goto out;
                }
                ret = 0;
                goto out;
        }

        ret = dict_get_ptr_and_len (dict, GF_AFR_SBRAIN_RESOLVE, &value, &len);
        if (value) {
                spb_child_index = afr_get_split_brain_child_index (this, value,
                                                                   len);
                if (spb_child_index < 0) {
                        ret = 1;
                        goto out;
                }

                afr_split_brain_resolve_do (frame, this, loc,
                                            priv->children[spb_child_index]->name);
                ret = 0;
        }
out:
        /* key was correct but value was invalid when ret == 1 */
        if (ret == 1) {
                AFR_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);
                if (data)
                        GF_FREE (data);
                ret = 0;
        }
        return ret;
}

int
afr_handle_spb_choice_timeout (xlator_t *this, call_frame_t *frame,
                               dict_t *dict)
{
        int             ret               = -1;
        int             op_errno          = 0;
        uint64_t        timeout           = 0;
        afr_private_t  *priv              = NULL;

        priv = this->private;

        ret = dict_get_uint64 (dict, GF_AFR_SPB_CHOICE_TIMEOUT, &timeout);
        if (!ret) {
                priv->spb_choice_timeout = timeout * 60;
                AFR_STACK_UNWIND (setxattr, frame, ret, op_errno, NULL);
        }

        return ret;
}

int
afr_handle_empty_brick (xlator_t *this, call_frame_t *frame, loc_t *loc,
                        dict_t *dict)
{
        int             ret               = -1;
        int             ab_ret            = -1;
        int             empty_index        = -1;
        int             op_errno          = EPERM;
        char           *empty_brick         = NULL;
        char           *op_type           = NULL;
        afr_empty_brick_args_t *data        = NULL;

        ret =  dict_get_str (dict, GF_AFR_REPLACE_BRICK, &empty_brick);
        if (!ret)
                op_type = GF_AFR_REPLACE_BRICK;

        ab_ret = dict_get_str (dict, GF_AFR_ADD_BRICK, &empty_brick);
        if (!ab_ret)
                op_type = GF_AFR_ADD_BRICK;

        if (ret && ab_ret)
                goto out;

        if (frame->root->pid != GF_CLIENT_PID_SELF_HEALD) {
                gf_msg (this->name, GF_LOG_ERROR, EPERM,
                        afr_get_msg_id (op_type),
                        "'%s' is an internal extended attribute.",
                        op_type);
                ret = 1;
                goto out;
        }
        empty_index = afr_get_child_index_from_name (this, empty_brick);

        if (empty_index < 0) {
                 /* Didn't belong to this replica pair
                  * Just do a no-op
                  */
                AFR_STACK_UNWIND (setxattr, frame, 0, 0, NULL);
                return 0;
        } else {
                data = GF_CALLOC (1, sizeof (*data),
                                  gf_afr_mt_empty_brick_t);
                if (!data) {
                        ret = 1;
                        op_errno = ENOMEM;
                        goto out;
                }
                data->frame = frame;
                loc_copy (&data->loc, loc);
                data->empty_index = empty_index;
                data->op_type = op_type;
                ret = synctask_new (this->ctx->env,
                                    _afr_handle_empty_brick,
                                    _afr_handle_empty_brick_cbk,
                                    NULL, data);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                afr_get_msg_id (op_type),
                                "Failed to create synctask.");
                        ret = 1;
                        op_errno = ENOMEM;
                        afr_brick_args_cleanup (data);
                        goto out;
                }
        }
        ret = 0;
out:
        if (ret == 1) {
                AFR_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);
                ret = 0;
        }
        return ret;
}

static int
afr_handle_special_xattr (xlator_t *this, call_frame_t *frame, loc_t *loc,
                          dict_t *dict)
{
        int     ret     = -1;

        ret = afr_handle_split_brain_commands (this, frame, loc, dict);
        if (ret == 0)
                goto out;

        ret = afr_handle_spb_choice_timeout (this, frame, dict);
        if (ret == 0)
                goto out;

        /* Applicable for replace-brick and add-brick commands */
        ret = afr_handle_empty_brick (this, frame, loc, dict);
out:
        return ret;
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

        ret = afr_handle_special_xattr (this, frame, loc, dict);
        if (ret == 0)
                return 0;

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
				      NULL, NULL, NULL, xdata);
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
				      NULL, NULL, NULL, xdata);
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
				      NULL, NULL, NULL, xdata);
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
				      prebuf, postbuf, NULL, xdata);
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
				      prebuf, postbuf, NULL, xdata);
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
				      prebuf, postbuf, NULL, xdata);
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

int32_t
afr_xattrop_wind_cbk (call_frame_t *frame, void *cookie,
                      xlator_t *this, int32_t op_ret, int32_t op_errno,
                      dict_t *xattr, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      NULL, NULL, xattr, xdata);
}

int
afr_xattrop_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_xattrop_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->xattrop,
			   &local->loc, local->cont.xattrop.optype,
			   local->cont.xattrop.xattr, local->xdata_req);
        return 0;
}

int
afr_xattrop_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        call_frame_t *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (xattrop, main_frame, local->op_ret, local->op_errno,
			  local->xattr_rsp, local->xdata_rsp);
        return 0;
}

int32_t
afr_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
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

        local->cont.xattrop.xattr = dict_ref (xattr);
        local->cont.xattrop.optype = optype;
	if (xdata)
		local->xdata_req = dict_ref (xdata);

        local->transaction.wind   = afr_xattrop_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_xattrop_unwind;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);

	local->op = GF_FOP_XATTROP;

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

	AFR_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
afr_fxattrop_wind_cbk (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       dict_t *xattr, dict_t *xdata)
{
	return __afr_inode_write_cbk (frame, cookie, this, op_ret, op_errno,
				      NULL, NULL, xattr, xdata);
}

int
afr_fxattrop_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_fxattrop_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->fxattrop,
			   local->fd, local->cont.xattrop.optype,
			   local->cont.xattrop.xattr, local->xdata_req);
        return 0;
}

int
afr_fxattrop_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        call_frame_t *main_frame = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (fxattrop, main_frame, local->op_ret, local->op_errno,
			  local->xattr_rsp, local->xdata_rsp);
        return 0;
}

int32_t
afr_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
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

        local->cont.xattrop.xattr = dict_ref (xattr);
        local->cont.xattrop.optype = optype;
	if (xdata)
		local->xdata_req = dict_ref (xdata);

        local->transaction.wind   = afr_fxattrop_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
        local->transaction.unwind = afr_fxattrop_unwind;

	local->fd = fd_ref (fd);
	local->inode = inode_ref (fd->inode);

	local->op = GF_FOP_FXATTROP;

        local->transaction.main_frame = frame;
        local->transaction.start   = LLONG_MAX - 1;
        local->transaction.len     = 0;

        ret = afr_transaction (transaction_frame, this,
                               AFR_METADATA_TRANSACTION);
        if (ret < 0) {
		op_errno = -ret;
		goto out;
        }

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (fxattrop, frame, -1, op_errno, NULL, NULL);
        return 0;
}
