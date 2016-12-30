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
#include "byte-order.h"

#include "afr.h"
#include "afr-transaction.h"

void
afr_mark_entry_pending_changelog (call_frame_t *frame, xlator_t *this);

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

        parent->path = gf_strdup (dirname (child_path));
        if (!parent->path) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }

        parent->inode = inode_ref (child->parent);
	gf_uuid_copy (parent->gfid, child->pargfid);

        ret = 0;
out:
        GF_FREE (child_path);

        return ret;
}


static void
__afr_dir_write_finalize (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int inode_read_subvol = -1;
	int parent_read_subvol = -1;
	int parent2_read_subvol = -1;
	int i = 0;
        afr_read_subvol_args_t args = {0,};

	local = frame->local;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
	        if (!local->replies[i].valid)
	                continue;
	        if (local->replies[i].op_ret == -1)
	                continue;
                gf_uuid_copy (args.gfid, local->replies[i].poststat.ia_gfid);
                args.ia_type = local->replies[i].poststat.ia_type;
                break;
        }

	if (local->inode) {
		afr_replies_interpret (frame, this, local->inode, NULL);
		inode_read_subvol = afr_data_subvol_get (local->inode, this,
                                                       NULL, NULL, NULL, &args);
	}

	if (local->parent)
		parent_read_subvol = afr_data_subvol_get (local->parent, this,
                                             NULL, local->readable, NULL, NULL);

	if (local->parent2)
		parent2_read_subvol = afr_data_subvol_get (local->parent2, this,
                                            NULL, local->readable2, NULL, NULL);

	local->op_ret = -1;
	local->op_errno = afr_final_errno (local, priv);
        afr_pick_error_xdata (local, priv, local->parent, local->readable,
                              local->parent2, local->readable2);

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].valid)
			continue;
		if (local->replies[i].op_ret < 0) {
			if (local->inode)
				afr_inode_event_gen_reset (local->inode, this);
			if (local->parent)
				afr_inode_event_gen_reset (local->parent,
							     this);
			if (local->parent2)
				afr_inode_event_gen_reset (local->parent2,
							     this);
			continue;
		}

		if (local->op_ret == -1) {
			local->op_ret = local->replies[i].op_ret;
			local->op_errno = local->replies[i].op_errno;

			local->cont.dir_fop.buf =
				local->replies[i].poststat;
			local->cont.dir_fop.preparent =
				local->replies[i].preparent;
			local->cont.dir_fop.postparent =
				local->replies[i].postparent;
			local->cont.dir_fop.prenewparent =
				local->replies[i].preparent2;
			local->cont.dir_fop.postnewparent =
				local->replies[i].postparent2;
                        if (local->xdata_rsp) {
                                dict_unref (local->xdata_rsp);
                                local->xdata_rsp = NULL;
                        }

			if (local->replies[i].xdata)
				local->xdata_rsp =
					dict_ref (local->replies[i].xdata);
			continue;
		}

		if (i == inode_read_subvol) {
			local->cont.dir_fop.buf =
				local->replies[i].poststat;
			if (local->replies[i].xdata) {
				if (local->xdata_rsp)
					dict_unref (local->xdata_rsp);
				local->xdata_rsp =
					dict_ref (local->replies[i].xdata);
			}
		}

		if (i == parent_read_subvol) {
			local->cont.dir_fop.preparent =
				local->replies[i].preparent;
			local->cont.dir_fop.postparent =
				local->replies[i].postparent;
		}

		if (i == parent2_read_subvol) {
			local->cont.dir_fop.prenewparent =
				local->replies[i].preparent2;
			local->cont.dir_fop.postnewparent =
				local->replies[i].postparent2;
		}
	}

        afr_txn_arbitrate_fop_cbk (frame, this);
}


static void
__afr_dir_write_fill (call_frame_t *frame, xlator_t *this, int child_index,
		      int op_ret, int op_errno, struct iatt *poststat,
		      struct iatt *preparent, struct iatt *postparent,
		      struct iatt *preparent2, struct iatt *postparent2,
		      dict_t *xdata)
{
        afr_local_t *local = NULL;
	afr_fd_ctx_t *fd_ctx = NULL;

        local = frame->local;
	fd_ctx = local->fd_ctx;

	local->replies[child_index].valid = 1;
	local->replies[child_index].op_ret = op_ret;
	local->replies[child_index].op_errno = op_errno;
        if (xdata)
                local->replies[child_index].xdata = dict_ref (xdata);


	if (op_ret >= 0) {
		if (poststat)
			local->replies[child_index].poststat = *poststat;
		if (preparent)
			local->replies[child_index].preparent = *preparent;
		if (postparent)
			local->replies[child_index].postparent = *postparent;
		if (preparent2)
			local->replies[child_index].preparent2 = *preparent2;
		if (postparent2)
			local->replies[child_index].postparent2 = *postparent2;
		if (fd_ctx)
			fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
	} else {
		if (op_errno != ENOTEMPTY)
			afr_transaction_fop_failed (frame, this, child_index);
		if (fd_ctx)
			fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
	}

        return;
}


static int
__afr_dir_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int op_ret, int op_errno, struct iatt *buf,
		     struct iatt *preparent, struct iatt *postparent,
		     struct iatt *preparent2, struct iatt *postparent2,
                     dict_t *xdata)
{
        afr_local_t *local = NULL;
        int child_index = (long) cookie;
        int call_count = -1;
        afr_private_t *priv = NULL;

        priv  = this->private;
        local = frame->local;

	LOCK (&frame->lock);
	{
		__afr_dir_write_fill (frame, this, child_index, op_ret,
				      op_errno, buf, preparent, postparent,
				      preparent2, postparent2, xdata);
	}
	UNLOCK (&frame->lock);
        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		__afr_dir_write_finalize (frame, this);

		if (afr_txn_nothing_failed (frame, this)) {
                        /*if it did pre-op, it will do post-op changing ctime*/
                        if (priv->consistent_metadata &&
                            afr_needs_changelog_update (local))
                                afr_zero_fill_stat (local);
                        local->transaction.unwind (frame, this);
                }

		afr_mark_entry_pending_changelog (frame, this);

                local->transaction.resume (frame, this);
        }

        return 0;
}


int
afr_mark_new_entry_changelog_cbk (call_frame_t *frame, void *cookie,
				  xlator_t *this, int op_ret, int op_errno,
                                  dict_t *xattr, dict_t *xdata)
{
        int call_count = 0;

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_DESTROY (frame);

        return 0;
}


void
afr_mark_new_entry_changelog (call_frame_t *frame, xlator_t *this)
{
        call_frame_t  *new_frame  = NULL;
        afr_local_t   *local      = NULL;
        afr_local_t   *new_local  = NULL;
        afr_private_t *priv       = NULL;
        dict_t        *xattr      = NULL;
        int32_t       **changelog = NULL;
        int           i           = 0;
        int           op_errno    = ENOMEM;
	unsigned char *pending    = NULL;
	int           call_count   = 0;

        local = frame->local;
        priv = this->private;

        new_frame = copy_frame (frame);
        if (!new_frame)
                goto out;

	new_local = AFR_FRAME_INIT (new_frame, op_errno);
	if (!new_local)
		goto out;

        xattr = dict_new ();
	if (!xattr)
		goto out;

	pending = alloca0 (priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		if (local->transaction.pre_op[i] &&
		    !local->transaction.failed_subvols[i]) {
			call_count ++;
			continue;
		}
		pending[i] = 1;
	}

        changelog = afr_mark_pending_changelog (priv, pending, xattr,
                                            local->cont.dir_fop.buf.ia_type);
        if (!changelog)
                goto out;

        new_local->pending = changelog;
        gf_uuid_copy (new_local->loc.gfid, local->cont.dir_fop.buf.ia_gfid);
        new_local->loc.inode = inode_ref (local->inode);

        new_local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
		if (pending[i])
                        continue;

                STACK_WIND_COOKIE (new_frame, afr_mark_new_entry_changelog_cbk,
                                   (void *) (long) i, priv->children[i],
                                   priv->children[i]->fops->xattrop,
                                   &new_local->loc, GF_XATTROP_ADD_ARRAY,
                                   xattr, NULL);
		if (!--call_count)
			break;
        }

        new_frame = NULL;
out:
        if (new_frame)
                AFR_STACK_DESTROY (new_frame);
	if (xattr)
		dict_unref (xattr);
        return;
}


void
afr_mark_entry_pending_changelog (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
	int pre_op_count = 0;
	int failed_count = 0;

        local = frame->local;
        priv  = this->private;

        if (local->op_ret < 0)
		return;

	if (local->op != GF_FOP_CREATE && local->op != GF_FOP_MKNOD &&
            local->op != GF_FOP_MKDIR)
		return;

	pre_op_count = AFR_COUNT (local->transaction.pre_op, priv->child_count);
	failed_count = AFR_COUNT (local->transaction.failed_subvols,
				  priv->child_count);

	if (pre_op_count == priv->child_count && !failed_count)
		return;

        afr_mark_new_entry_changelog (frame, this);

        return;
}


/* {{{ create */

int
afr_create_unwind (call_frame_t *frame, xlator_t *this)
{
        call_frame_t *main_frame = NULL;
        afr_local_t  *local = NULL;

        local = frame->local;

	main_frame = afr_transaction_detach_fop_frame (frame);

        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (create, main_frame, local->op_ret, local->op_errno,
			  local->cont.create.fd, local->inode,
			  &local->cont.dir_fop.buf,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_create_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     fd_t *fd, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
                     dict_t *xdata)
{
        return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, buf,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_create_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_create_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->create,
			   &local->loc, local->cont.create.flags,
			   local->cont.create.mode, local->umask,
			   local->cont.create.fd, local->xdata_req);
        return 0;
}


int
afr_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
	    mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc, loc);

	local->fd_ctx = afr_fd_ctx_get (fd, this);
	if (!local->fd_ctx)
		goto out;

	local->inode = inode_ref (loc->inode);
	local->parent = inode_ref (loc->parent);

        local->op                = GF_FOP_CREATE;
        local->cont.create.flags = flags;
        local->fd_ctx->flags     = flags;
        local->cont.create.mode  = mode;
        local->cont.create.fd    = fd_ref (fd);
        local->umask  = umask;

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_create_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL, NULL,
			  NULL, NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (mknod, main_frame, local->op_ret, local->op_errno,
			  local->inode, &local->cont.dir_fop.buf,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_mknod_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
	return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, buf,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_mknod_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv  = this->private;

	STACK_WIND_COOKIE (frame, afr_mknod_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->mknod,
			   &local->loc, local->cont.mknod.mode,
			   local->cont.mknod.dev, local->umask,
			   local->xdata_req);
        return 0;
}

int
afr_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t dev, mode_t umask, dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);
	local->parent = inode_ref (loc->parent);

        local->op               = GF_FOP_MKNOD;
        local->cont.mknod.mode  = mode;
        local->cont.mknod.dev   = dev;
        local->umask = umask;

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->transaction.wind   = afr_mknod_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL,
			  NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (mkdir, main_frame, local->op_ret, local->op_errno,
			  local->inode, &local->cont.dir_fop.buf,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_mkdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
	return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, buf,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_mkdir_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv  = this->private;

	STACK_WIND_COOKIE (frame, afr_mkdir_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->mkdir, &local->loc,
			   local->cont.mkdir.mode, local->umask,
			   local->xdata_req);
        return 0;
}


int
afr_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
	   mode_t umask, dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);
	local->parent = inode_ref (loc->parent);

        local->cont.mkdir.mode  = mode;
        local->umask = umask;

        if (!xdata || !dict_get (xdata, "gfid-req")) {
                op_errno = EPERM;
                gf_msg_callingfn (this->name, GF_LOG_WARNING, op_errno,
                                  AFR_MSG_GFID_NULL, "mkdir: %s is received "
                                  "without gfid-req %p", loc->path, xdata);
	        goto out;
        }

        local->xdata_req = dict_copy_with_ref (xdata, NULL);
        if (!local->xdata_req) {
                op_errno = ENOMEM;
                goto out;
        }

        local->op = GF_FOP_MKDIR;
        local->transaction.wind   = afr_mkdir_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL,
			  NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (link, main_frame, local->op_ret, local->op_errno,
			  local->inode, &local->cont.dir_fop.buf,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_link_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, buf,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_link_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv  = this->private;

	STACK_WIND_COOKIE (frame, afr_link_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->link,
			   &local->loc, &local->newloc, local->xdata_req);
        return 0;
}


int
afr_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
	  dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc,    oldloc);
        loc_copy (&local->newloc, newloc);

	local->inode = inode_ref (oldloc->inode);
	local->parent = inode_ref (newloc->parent);

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_LINK;

        local->transaction.wind   = afr_link_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL,
			  NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (symlink, main_frame, local->op_ret, local->op_errno,
			  local->inode, &local->cont.dir_fop.buf,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_symlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, inode_t *inode,
                      struct iatt *buf, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
        return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, buf,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_symlink_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;

	STACK_WIND_COOKIE (frame, afr_symlink_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->symlink,
			   local->cont.symlink.linkpath, &local->loc,
			   local->umask, local->xdata_req);
        return 0;
}


int
afr_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
	     loc_t *loc, mode_t umask, dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);
	local->parent = inode_ref (loc->parent);

        local->cont.symlink.linkpath = gf_strdup (linkpath);
        local->umask = umask;

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_SYMLINK;
        local->transaction.wind   = afr_symlink_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (symlink, frame, -1, op_errno, NULL, NULL, NULL,
			  NULL, NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (rename, main_frame, local->op_ret, local->op_errno,
			  &local->cont.dir_fop.buf,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent,
			  &local->cont.dir_fop.prenewparent,
			  &local->cont.dir_fop.postnewparent, local->xdata_rsp);
        return 0;
}


int
afr_rename_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
                     dict_t *xdata)
{
        return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, buf,
				    preoldparent, postoldparent, prenewparent,
				    postnewparent, xdata);
}


int
afr_rename_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

	local = frame->local;
	priv = this->private;

	STACK_WIND_COOKIE (frame, afr_rename_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->rename,
			   &local->loc, &local->newloc, local->xdata_req);
        return 0;
}


int
afr_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
	    dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;
        int                     nlockee                 = 0;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                goto out;
        }

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc,    oldloc);
        loc_copy (&local->newloc, newloc);

	local->inode = inode_ref (oldloc->inode);
	local->parent = inode_ref (oldloc->parent);
	local->parent2 = inode_ref (newloc->parent);

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_RENAME;
        local->transaction.wind   = afr_rename_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
			  NULL, NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
        if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (unlink, main_frame, local->op_ret, local->op_errno,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_unlink_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, NULL,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_unlink_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv  = this->private;

	STACK_WIND_COOKIE (frame, afr_unlink_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->unlink,
			   &local->loc, local->xflag, local->xdata_req);
        return 0;
}


int
afr_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
	    dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;

        loc_copy (&local->loc, loc);
        local->xflag = xflag;

	local->inode = inode_ref (loc->inode);
	local->parent = inode_ref (loc->parent);

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_UNLINK;
        local->transaction.wind   = afr_unlink_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
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

	main_frame = afr_transaction_detach_fop_frame (frame);
	if (!main_frame)
		return 0;

	AFR_STACK_UNWIND (rmdir, main_frame, local->op_ret, local->op_errno,
			  &local->cont.dir_fop.preparent,
			  &local->cont.dir_fop.postparent, local->xdata_rsp);
        return 0;
}


int
afr_rmdir_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        return __afr_dir_write_cbk (frame, cookie, this, op_ret, op_errno, NULL,
				    preparent, postparent, NULL, NULL, xdata);
}


int
afr_rmdir_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv  = this->private;

	STACK_WIND_COOKIE (frame, afr_rmdir_wind_cbk, (void *) (long) subvol,
			   priv->children[subvol],
			   priv->children[subvol]->fops->rmdir,
			   &local->loc, local->cont.rmdir.flags, local->xdata_req);
        return 0;
}


int
afr_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
	   dict_t *xdata)
{
        afr_private_t           *priv                   = NULL;
        afr_local_t             *local                  = NULL;
        afr_internal_lock_t     *int_lock               = NULL;
        call_frame_t            *transaction_frame      = NULL;
        int                     ret                     = -1;
        int                     op_errno                = ENOMEM;
        int                     nlockee                 = 0;

        priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame)
                goto out;

	local = AFR_FRAME_INIT (transaction_frame, op_errno);
	if (!local)
		goto out;


        loc_copy (&local->loc, loc);
	local->inode = inode_ref (loc->inode);
	local->parent = inode_ref (loc->parent);

        local->cont.rmdir.flags = flags;

        if (xdata)
                local->xdata_req = dict_copy_with_ref (xdata, NULL);
	else
		local->xdata_req = dict_new ();

	if (!local->xdata_req)
		goto out;

        local->op = GF_FOP_RMDIR;
        local->transaction.wind   = afr_rmdir_wind;
        local->transaction.fop    = __afr_txn_write_fop;
        local->transaction.done   = __afr_txn_write_done;
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

	return 0;
out:
	if (transaction_frame)
		AFR_STACK_DESTROY (transaction_frame);

	AFR_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

/* }}} */
