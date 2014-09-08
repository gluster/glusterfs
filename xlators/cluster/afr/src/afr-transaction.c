/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dict.h"
#include "byte-order.h"
#include "common-utils.h"
#include "timer.h"

#include "afr.h"
#include "afr-transaction.h"

#include <signal.h>

gf_boolean_t
afr_changelog_pre_op_uninherit (call_frame_t *frame, xlator_t *this);

gf_boolean_t
afr_changelog_pre_op_update (call_frame_t *frame, xlator_t *this);

int
afr_changelog_do (call_frame_t *frame, xlator_t *this, dict_t *xattr,
		  afr_changelog_resume_t changelog_resume);


int
__afr_txn_write_fop (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int i = 0;

        local = frame->local;
        priv = this->private;

        call_count = AFR_COUNT (local->transaction.pre_op, priv->child_count);

        if (call_count == 0) {
                local->transaction.resume (frame, this);
                return 0;
        }

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i]) {
			local->transaction.wind (frame, this, i);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int
__afr_txn_write_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

        return 0;
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


static void
afr_save_lk_owner (call_frame_t *frame)
{
        afr_local_t * local = NULL;

        local = frame->local;

        local->saved_lk_owner = frame->root->lk_owner;
}


static void
afr_restore_lk_owner (call_frame_t *frame)
{
        afr_local_t * local = NULL;

        local = frame->local;

        frame->root->lk_owner = local->saved_lk_owner;
}

void
__mark_all_success (call_frame_t *frame, xlator_t *this)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;
	int i;

	local = frame->local;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		local->transaction.failed_subvols[i] = 0;
	}
}


int
afr_transaction_perform_fop (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        fd_t            *fd   = NULL;

        local = frame->local;
        fd    = local->fd;

        /*  Perform fops with the lk-owner from top xlator.
         *  Eg: lk-owner of posix-lk and flush should be same,
         *  flush cant clear the  posix-lks without that lk-owner.
         */
        afr_save_lk_owner (frame);
        frame->root->lk_owner =
                local->transaction.main_frame->root->lk_owner;

	if (local->pre_op_compat)
		/* old mode, pre-op was done as afr_changelog_do()
		   just now, before OP */
		afr_changelog_pre_op_update (frame, this);

        /* The wake up needs to happen independent of
           what type of fop arrives here. If it was
           a write, then it has already inherited the
           lock and changelog. If it was not a write,
           then the presumption of the optimization (of
           optimizing for successive write operations)
           fails.
        */
        if (fd)
                afr_delayed_changelog_wake_up (this, fd);
        local->transaction.fop (frame, this);

	return 0;
}


static int
__changelog_enabled (afr_private_t *priv, afr_transaction_type type)
{
        int ret = 0;

        switch (type) {
        case AFR_DATA_TRANSACTION:
                if (priv->data_change_log)
                        ret = 1;

                break;

        case AFR_METADATA_TRANSACTION:
                if (priv->metadata_change_log)
                        ret = 1;

                break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
                if (priv->entry_change_log)
                        ret = 1;

                break;
        }

        return ret;
}


static int
__fop_changelog_needed (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv  = NULL;
        afr_local_t   * local = NULL;
        int op_ret = 0;
        afr_transaction_type type = -1;

        priv  = this->private;
        local = frame->local;
        type  = local->transaction.type;

        if (__changelog_enabled (priv, type)) {
                switch (local->op) {

                case GF_FOP_WRITE:
                case GF_FOP_FTRUNCATE:
                        op_ret = 1;
                        break;

                case GF_FOP_FLUSH:
                        op_ret = 0;
                        break;

                default:
                        op_ret = 1;
                }
        }

        return op_ret;
}


int
afr_set_pending_dict (afr_private_t *priv, dict_t *xattr, int **pending)
{
        int i = 0;
        int ret = 0;

        for (i = 0; i < priv->child_count; i++) {

                ret = dict_set_static_bin (xattr, priv->pending_key[i],
					   pending[i],
					   AFR_NUM_CHANGE_LOGS * sizeof (int));
                /* 3 = data+metadata+entry */

                if (ret)
                        break;
        }

        return ret;
}

int
afr_lock_server_count (afr_private_t *priv, afr_transaction_type type)
{
        int ret = 0;

        switch (type) {
        case AFR_DATA_TRANSACTION:
                ret = priv->child_count;
                break;

        case AFR_METADATA_TRANSACTION:
                ret = priv->child_count;
                break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
                ret = priv->child_count;
                break;
        }

        return ret;
}

/* {{{ pending */


int
afr_changelog_post_op_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
        afr_internal_lock_t *int_lock = NULL;

	local = frame->local;
	priv = this->private;
        int_lock = &local->internal_lock;

	if (local->transaction.resume_stub) {
		call_resume (local->transaction.resume_stub);
		local->transaction.resume_stub = NULL;
	}

	if (afr_lock_server_count (priv, local->transaction.type) == 0) {
		local->transaction.done (frame, this);
	} else {
		int_lock->lock_cbk = local->transaction.done;
		afr_unlock (frame, this);
	}

	return 0;
}


afr_inodelk_t*
afr_get_inodelk (afr_internal_lock_t *int_lock, char *dom)
{
        afr_inodelk_t *inodelk = NULL;
        int           i = 0;

        for (i = 0; int_lock->inodelk[i].domain; i++) {
                inodelk = &int_lock->inodelk[i];
                if (strcmp (dom, inodelk->domain) == 0)
                        return inodelk;
        }
        return NULL;
}

unsigned char*
afr_locked_nodes_get (afr_transaction_type type, afr_internal_lock_t *int_lock)
{
        unsigned char *locked_nodes = NULL;
        afr_inodelk_t *inodelk = NULL;
        switch (type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                inodelk = afr_get_inodelk (int_lock, int_lock->domain);
                locked_nodes = inodelk->locked_nodes;
        break;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
                /*Because same set of subvols participate in all lockee
                 * entities*/
                locked_nodes = int_lock->lockee[0].locked_nodes;
        break;
        }
        return locked_nodes;
}


int
afr_changelog_call_count (afr_transaction_type type,
			  unsigned char *pre_op_subvols,
			  unsigned int child_count)
{
        int call_count = 0;

	call_count = AFR_COUNT(pre_op_subvols, child_count);

        if (type == AFR_ENTRY_RENAME_TRANSACTION)
                call_count *= 2;

        return call_count;
}


gf_boolean_t
afr_txn_nothing_failed (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int i = 0;

        local = frame->local;
	priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.failed_subvols[i])
                        return _gf_false;
        }

        return _gf_true;
}


void
afr_handle_symmetric_errors (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int op_errno = 0;
	int i_errno = 0;
	gf_boolean_t matching_errors = _gf_true;
	int i = 0;

	priv = this->private;
	local = frame->local;

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].valid)
			continue;
		if (local->replies[i].op_ret != -1) {
			/* Operation succeeded on at least on subvol,
			   so it is not a failed-everywhere situation.
			*/
			matching_errors = _gf_false;
			break;
		}
		i_errno = local->replies[i].op_errno;

		if (i_errno == ENOTCONN) {
			/* ENOTCONN is not a symmetric error. We do not
			   know if the operation was performed on the
			   backend or not.
			*/
			matching_errors = _gf_false;
			break;
		}

		if (!op_errno) {
			op_errno = i_errno;
		} else if (op_errno != i_errno) {
			/* Mismatching op_errno's */
			matching_errors = _gf_false;
			break;
		}
	}

	if (matching_errors)
		__mark_all_success (frame, this);
}

gf_boolean_t
afr_has_quorum (unsigned char *subvols, xlator_t *this)
{
        unsigned int  quorum_count = 0;
        afr_private_t *priv  = NULL;
        unsigned int  up_children_count = 0;

        priv = this->private;
        up_children_count = AFR_COUNT (subvols, priv->child_count);

        if (priv->quorum_count == AFR_QUORUM_AUTO) {
        /*
         * Special case for even numbers of nodes in auto-quorum:
         * if we have exactly half children up
         * and that includes the first ("senior-most") node, then that counts
         * as quorum even if it wouldn't otherwise.  This supports e.g. N=2
         * while preserving the critical property that there can only be one
         * such group.
         */
                if ((priv->child_count % 2 == 0) &&
                    (up_children_count == (priv->child_count/2)))
                        return subvols[0];
        }

        if (priv->quorum_count == AFR_QUORUM_AUTO) {
                quorum_count = priv->child_count/2 + 1;
        } else {
                quorum_count = priv->quorum_count;
        }

        if (up_children_count >= quorum_count)
                return _gf_true;

        return _gf_false;
}

static gf_boolean_t
afr_has_fop_quorum (call_frame_t *frame)
{
        xlator_t        *this = frame->this;
        afr_local_t     *local = frame->local;
        unsigned char *locked_nodes = NULL;

        locked_nodes = afr_locked_nodes_get (local->transaction.type,
                                             &local->internal_lock);
        return afr_has_quorum (locked_nodes, this);
}

static gf_boolean_t
afr_has_fop_cbk_quorum (call_frame_t *frame)
{
        afr_local_t   *local   = frame->local;
        xlator_t      *this    = frame->this;
        afr_private_t *priv    = this->private;
        unsigned char *success = alloca0(priv->child_count);
        int           i        = 0;

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i])
                        if (!local->transaction.failed_subvols[i])
                                success[i] = 1;
        }

        return afr_has_quorum (success, this);
}

void
afr_handle_quorum (call_frame_t *frame)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;
        int           i      = 0;
        const char    *file  = NULL;
        uuid_t        gfid   = {0};

        local = frame->local;
        priv  = frame->this->private;

        if (priv->quorum_count == 0)
                return;

        /*
         * Network split may happen just after the fops are unwound, so check
         * if the fop succeeded in a way it still follows quorum. If it doesn't,
         * mark the fop as failure, mark the changelogs so it reflects that
         * failure.
         *
         * Scenario:
         * There are 3 mounts on 3 machines(node1, node2, node3) all writing to
         * single file. Network split happened in a way that node1 can't see
         * node2, node3. Node2, node3 both of them can't see node1. Now at the
         * time of sending write all the bricks are up. Just after write fop is
         * wound on node1, network split happens. Node1 thinks write fop failed
         * on node2, node3 so marks pending changelog for those 2 extended
         * attributes on node1. Node2, node3 thinks writes failed on node1 so
         * they mark pending changelog for node1. When the network is stable
         * again the file already is in split-brain. These checks prevent
         * marking pending changelog on other subvolumes if the fop doesn't
         * succeed in a way it is still following quorum. So with this fix what
         * is happening is, node1 will have all pending changelog(FOOL) because
         * the write succeeded only on node1 but failed on node2, node3 so
         * instead of marking pending changelogs on node2, node3 it just treats
         * the fop as failure and goes into DIRTY state. Where as node2, node3
         * say they are sources and have pending changelog to node1 so there is
         * no split-brain with the fix. The problem is eliminated completely.
         */

        if (afr_has_fop_cbk_quorum (frame))
                return;

        if (local->fd) {
                uuid_copy (gfid, local->fd->inode->gfid);
                file = uuid_utoa (gfid);
        } else {
                loc_path (&local->loc, local->loc.name);
                file = local->loc.path;
        }

        gf_msg (frame->this->name, GF_LOG_WARNING, 0, AFR_MSG_QUORUM_FAIL,
                "%s: Failing %s as quorum is not met",
                file, gf_fop_list[local->op]);

        for (i = 0; i < priv->child_count; i++) {
                if (local->transaction.pre_op[i])
                        afr_transaction_fop_failed (frame, frame->this, i);
        }

        local->op_ret = -1;
        local->op_errno = EROFS;
}

int
afr_changelog_post_op_now (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv = this->private;
        int i          = 0;
	int ret = 0;
	int idx = 0;
        afr_local_t *  local = NULL;
        dict_t        *xattr = NULL;
        int            nothing_failed = 1;
	gf_boolean_t   need_undirty = _gf_false;

        afr_handle_quorum (frame);
        local = frame->local;
	idx = afr_index_for_transaction_type (local->transaction.type);

        nothing_failed = afr_txn_nothing_failed (frame, this);

	if (afr_changelog_pre_op_uninherit (frame, this))
		need_undirty = _gf_false;
	else
		need_undirty = _gf_true;

        //If the fop fails on all the subvols then pending markers are placed
        //for every subvol on all subvolumes. Which is nothing but split-brain.
        //Avoid this by not doing post-op in case of failures.
        if (local->op_ret < 0) {
                afr_changelog_post_op_done (frame, this);
                goto out;
        }

	if (nothing_failed && !need_undirty) {
		afr_changelog_post_op_done (frame, this);
                goto out;
	}

	xattr = dict_new ();
	if (!xattr) {
		local->op_ret = -1;
		local->op_errno = ENOMEM;
		afr_changelog_post_op_done (frame, this);
		goto out;
	}

	if (need_undirty)
		local->dirty[idx] = hton32(-1);
	else
		local->dirty[idx] = hton32(0);

	ret = dict_set_static_bin (xattr, AFR_DIRTY, local->dirty,
				   sizeof(int) * AFR_NUM_CHANGE_LOGS);
	if (ret) {
		local->op_ret = -1;
		local->op_errno = ENOMEM;
		afr_changelog_post_op_done (frame, this);
		goto out;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (local->transaction.failed_subvols[i])
			local->pending[i][idx] = hton32(1);
	}

	ret = afr_set_pending_dict (priv, xattr, local->pending);
	if (ret < 0) {
		local->op_ret = -1;
		local->op_errno = ENOMEM;
		afr_changelog_post_op_done (frame, this);
		goto out;
	}

	afr_changelog_do (frame, this, xattr, afr_changelog_post_op_done);
out:
	if (xattr)
                dict_unref (xattr);

        return 0;
}


gf_boolean_t
afr_changelog_pre_op_uninherit (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	fd_t *fd = NULL;
	int i = 0;
	gf_boolean_t ret = _gf_false;
	afr_fd_ctx_t *fd_ctx = NULL;
	int type = 0;

	local = frame->local;
	priv = this->private;
	fd = local->fd;

	type = afr_index_for_transaction_type (local->transaction.type);
	if (type != AFR_DATA_TRANSACTION)
		return !local->transaction.dirtied;

	if (!fd)
		return !local->transaction.dirtied;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
		return _gf_false;

	if (local->transaction.no_uninherit)
		return _gf_false;

	/* This function must be idempotent. So check if we
	   were called before and return the same answer again.

	   It is important to keep this function idempotent for
	   the call in afr_changelog_post_op_safe() to not have
	   side effects on the call from afr_changelog_post_op_now()
	*/
	if (local->transaction.uninherit_done)
		return local->transaction.uninherit_value;

	LOCK(&fd->lock);
	{
		for (i = 0; i < priv->child_count; i++) {
			if (local->transaction.pre_op[i] !=
			    fd_ctx->pre_op_done[type][i]) {
				ret = !local->transaction.dirtied;
				goto unlock;
			}
		}

		if (fd_ctx->inherited[type]) {
			ret = _gf_true;
			fd_ctx->inherited[type]--;
		} else if (fd_ctx->on_disk[type]) {
			ret = _gf_false;
			fd_ctx->on_disk[type]--;
		} else {
			/* ASSERT */
			ret = _gf_false;
		}

		if (!fd_ctx->inherited[type] && !fd_ctx->on_disk[type]) {
			for (i = 0; i < priv->child_count; i++)
				fd_ctx->pre_op_done[type][i] = 0;
		}
	}
unlock:
	UNLOCK(&fd->lock);

	local->transaction.uninherit_done = _gf_true;
	local->transaction.uninherit_value = ret;

	return ret;
}


gf_boolean_t
afr_changelog_pre_op_inherit (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	fd_t *fd = NULL;
	int i = 0;
	gf_boolean_t ret = _gf_false;
	afr_fd_ctx_t *fd_ctx = NULL;
	int type = 0;

	local = frame->local;
	priv = this->private;
	fd = local->fd;

	if (local->transaction.type != AFR_DATA_TRANSACTION)
		return _gf_false;

	type = afr_index_for_transaction_type (local->transaction.type);

	if (!fd)
		return _gf_false;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
		return _gf_false;

	LOCK(&fd->lock);
	{
		if (!fd_ctx->on_disk[type]) {
			/* nothing to inherit yet */
			ret = _gf_false;
			goto unlock;
		}

		for (i = 0; i < priv->child_count; i++) {
			if (local->transaction.pre_op[i] !=
			    fd_ctx->pre_op_done[type][i]) {
				/* either inherit exactly, or don't */
				ret = _gf_false;
				goto unlock;
			}
		}

		fd_ctx->inherited[type]++;

		ret = _gf_true;

		local->transaction.inherited = _gf_true;
	}
unlock:
	UNLOCK(&fd->lock);

	return ret;
}


gf_boolean_t
afr_changelog_pre_op_update (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	fd_t *fd = NULL;
	afr_fd_ctx_t *fd_ctx = NULL;
	int i = 0;
	gf_boolean_t ret = _gf_false;
	int type = 0;

	local = frame->local;
	priv = this->private;
	fd = local->fd;

	if (!fd)
		return _gf_false;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
		return _gf_false;

	if (local->transaction.inherited)
		/* was already inherited in afr_changelog_pre_op */
		return _gf_false;

	if (!local->transaction.dirtied)
		return _gf_false;

        if (!afr_txn_nothing_failed (frame, this))
		return _gf_false;

	type = afr_index_for_transaction_type (local->transaction.type);

	ret = _gf_false;

	LOCK(&fd->lock);
	{
		if (!fd_ctx->on_disk[type]) {
			for (i = 0; i < priv->child_count; i++)
				fd_ctx->pre_op_done[type][i] =
					local->transaction.pre_op[i];
		} else {
			for (i = 0; i < priv->child_count; i++)
				if (fd_ctx->pre_op_done[type][i] !=
				    local->transaction.pre_op[i]) {
					local->transaction.no_uninherit = 1;
					goto unlock;
				}
		}
		fd_ctx->on_disk[type]++;

		ret = _gf_true;
	}
unlock:
	UNLOCK(&fd->lock);

	return ret;
}


int
afr_changelog_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		   int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

	if (op_ret == -1)
		afr_transaction_fop_failed (frame, this, (long) cookie);

	call_count = afr_frame_return (frame);

        if (call_count == 0)
		local->transaction.changelog_resume (frame, this);

        return 0;
}


int
afr_changelog_do (call_frame_t *frame, xlator_t *this, dict_t *xattr,
		  afr_changelog_resume_t changelog_resume)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

        call_count = afr_changelog_call_count (local->transaction.type,
					       local->transaction.pre_op,
					       priv->child_count);

	if (call_count == 0) {
		changelog_resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	local->transaction.changelog_resume = changelog_resume;

        for (i = 0; i < priv->child_count; i++) {
                if (!local->transaction.pre_op[i])
                        continue;

                switch (local->transaction.type) {
                case AFR_DATA_TRANSACTION:
                case AFR_METADATA_TRANSACTION:
                        if (!local->fd) {
                                STACK_WIND_COOKIE (frame, afr_changelog_cbk,
						   (void *) (long) i,
						   priv->children[i],
						   priv->children[i]->fops->xattrop,
						   &local->loc,
						   GF_XATTROP_ADD_ARRAY, xattr,
						   NULL);
                        } else {
                                STACK_WIND_COOKIE (frame, afr_changelog_cbk,
						   (void *) (long) i,
						   priv->children[i],
						   priv->children[i]->fops->fxattrop,
						   local->fd,
						   GF_XATTROP_ADD_ARRAY, xattr,
						   NULL);
                        }
			break;
                case AFR_ENTRY_RENAME_TRANSACTION:

			STACK_WIND_COOKIE (frame, afr_changelog_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->xattrop,
					   &local->transaction.new_parent_loc,
					   GF_XATTROP_ADD_ARRAY, xattr,
					   NULL);
                        call_count--;

                /* fall through */

                case AFR_ENTRY_TRANSACTION:
                        if (local->fd)
                                STACK_WIND_COOKIE (frame, afr_changelog_cbk,
						   (void *) (long) i,
						   priv->children[i],
						   priv->children[i]->fops->fxattrop,
						   local->fd,
						   GF_XATTROP_ADD_ARRAY, xattr,
						   NULL);
                        else
                                STACK_WIND_COOKIE (frame, afr_changelog_cbk,
						   (void *) (long) i,
						   priv->children[i],
						   priv->children[i]->fops->xattrop,
						   &local->transaction.parent_loc,
						   GF_XATTROP_ADD_ARRAY, xattr,
						   NULL);
			break;
		}

                if (!--call_count)
                        break;
        }

	return 0;
}


int
afr_changelog_pre_op (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv = this->private;
        int i = 0;
        int ret = 0;
        int call_count = 0;
	int op_errno = 0;
        afr_local_t *local = NULL;
        afr_internal_lock_t *int_lock = NULL;
        unsigned char       *locked_nodes = NULL;
	unsigned char       *pending_subvols = NULL;
	int idx = -1;
	gf_boolean_t pre_nop = _gf_true;
	dict_t *xdata_req = NULL;

        local = frame->local;
        int_lock = &local->internal_lock;
	idx = afr_index_for_transaction_type (local->transaction.type);

        locked_nodes = afr_locked_nodes_get (local->transaction.type, int_lock);

	pending_subvols = alloca0 (priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		if (locked_nodes[i]) {
			local->transaction.pre_op[i] = 1;
			call_count++;
		} else {
			pending_subvols[i] = 1;
		}
	}

        if (priv->quorum_count && !afr_has_fop_quorum (frame)) {
                op_errno = EROFS;
                goto err;
        }

        if (call_count == 0) {
		op_errno = ENOTCONN;
		goto err;
	}

	xdata_req = dict_new();
	if (!xdata_req) {
		op_errno = ENOMEM;
		goto err;
	}

	pre_nop = _gf_true;

	if (afr_changelog_pre_op_inherit (frame, this))
		goto next;

	if (call_count < priv->child_count) {
		/* For subvols we are not performing operation on,
		   mark them as pending up-front along with the FOP
		   so that we can safely defer unmarking dirty until
		   later.
		*/
		for (i = 0; i < priv->child_count; i++) {
			if (pending_subvols[i])
				local->pending[i][idx] = hton32(1);
		}
		ret = afr_set_pending_dict (priv, xdata_req,
					    local->pending);
		if (ret < 0) {
			op_errno = ENOMEM;
			goto err;
		}
		pre_nop = _gf_false;
	}

	if (call_count > 1 &&
	    (local->transaction.type == AFR_DATA_TRANSACTION ||
	     !local->optimistic_change_log)) {

		/* If we are performing change on only one subvol, no
		   need to mark dirty, because we are setting the pending
		   counts already anyways
		*/
		local->dirty[idx] = hton32(1);

		ret = dict_set_static_bin (xdata_req, AFR_DIRTY, local->dirty,
					   sizeof(int) * AFR_NUM_CHANGE_LOGS);
		if (ret) {
			op_errno = ENOMEM;
			goto err;
		}

		pre_nop = _gf_false;
		local->transaction.dirtied = 1;
	}

	if (pre_nop)
		goto next;

	if (!local->pre_op_compat) {
		dict_copy (xdata_req, local->xdata_req);
		goto next;
	}

	afr_changelog_do (frame, this, xdata_req, afr_transaction_perform_fop);

	if (xdata_req)
		dict_unref (xdata_req);

	return 0;
next:
	afr_transaction_perform_fop (frame, this);

	if (xdata_req)
		dict_unref (xdata_req);

        return 0;
err:
	local->internal_lock.lock_cbk = local->transaction.done;
	local->op_ret = -1;
	local->op_errno = op_errno;

	afr_unlock (frame, this);

	if (xdata_req)
		dict_unref (xdata_req);

	return 0;
}


int
afr_post_blocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Blocking inodelks failed.");
                local->transaction.done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Blocking inodelks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_nonblocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        /* Initiate blocking locks if non-blocking has failed */
        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking inodelks failed. Proceeding to blocking");
                int_lock->lock_cbk = afr_post_blocking_inodelk_cbk;
                afr_blocking_lock (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking inodelks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_blocking_entrylk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Blocking entrylks failed.");
                local->transaction.done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Blocking entrylks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_nonblocking_entrylk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local = frame->local;
        int_lock = &local->internal_lock;

        /* Initiate blocking locks if non-blocking has failed */
        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking entrylks failed. Proceeding to blocking");
                int_lock->lock_cbk = afr_post_blocking_entrylk_cbk;
                afr_blocking_lock (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Non blocking entrylks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }

        return 0;
}


int
afr_post_blocking_rename_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "Blocking entrylks failed.");
                local->transaction.done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Blocking entrylks done. Proceeding to FOP");
                afr_internal_lock_finish (frame, this);
        }
        return 0;
}

int
afr_post_lower_unlock_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        GF_ASSERT (!int_lock->higher_locked);

        int_lock->lock_cbk = afr_post_blocking_rename_cbk;
        afr_blocking_lock (frame, this);

        return 0;
}


int
afr_set_transaction_flock (afr_local_t *local)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_inodelk_t       *inodelk  = NULL;

        int_lock = &local->internal_lock;
        inodelk = afr_get_inodelk (int_lock, int_lock->domain);

        inodelk->flock.l_len   = local->transaction.len;
        inodelk->flock.l_start = local->transaction.start;
        inodelk->flock.l_type  = F_WRLCK;

        return 0;
}

int
afr_lock_rec (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->transaction_lk_type = AFR_TRANSACTION_LK;
        int_lock->domain = this->name;

        switch (local->transaction.type) {
        case AFR_DATA_TRANSACTION:
        case AFR_METADATA_TRANSACTION:
                afr_set_transaction_flock (local);

                int_lock->lock_cbk = afr_post_nonblocking_inodelk_cbk;

                afr_nonblocking_inodelk (frame, this);
                break;

        case AFR_ENTRY_RENAME_TRANSACTION:

                int_lock->lock_cbk = afr_post_nonblocking_entrylk_cbk;
                afr_nonblocking_entrylk (frame, this);
                break;

        case AFR_ENTRY_TRANSACTION:
                int_lock->lk_basename = local->transaction.basename;
                if (&local->transaction.parent_loc)
                        int_lock->lk_loc = &local->transaction.parent_loc;
                else
                        GF_ASSERT (local->fd);

                int_lock->lock_cbk = afr_post_nonblocking_entrylk_cbk;
                afr_nonblocking_entrylk (frame, this);
                break;
        }

        return 0;
}


int
afr_lock (call_frame_t *frame, xlator_t *this)
{
        afr_set_lock_number (frame, this);

        return afr_lock_rec (frame, this);
}


/* }}} */

int
afr_internal_lock_finish (call_frame_t *frame, xlator_t *this)
{
        if (__fop_changelog_needed (frame, this)) {
                afr_changelog_pre_op (frame, this);
        } else {
                afr_transaction_perform_fop (frame, this);
        }

        return 0;
}


void
afr_set_delayed_post_op (call_frame_t *frame, xlator_t *this)
{
        afr_local_t    *local = NULL;
        afr_private_t  *priv = NULL;

        /* call this function from any of the related optimizations
           which benefit from delaying post op are enabled, namely:

           - changelog piggybacking
           - eager locking
        */

        priv = this->private;
        if (!priv)
                return;

        if (!priv->post_op_delay_secs)
                return;

        local = frame->local;
        if (!local->transaction.eager_lock_on)
                return;

        if (!local)
                return;

        if (!local->fd)
                return;

        if (local->op == GF_FOP_WRITE)
                local->delayed_post_op = _gf_true;
}

gf_boolean_t
afr_are_multiple_fds_opened (fd_t *fd, xlator_t *this)
{
        afr_fd_ctx_t *fd_ctx = NULL;

        if (!fd) {
                /* If false is returned, it may keep on taking eager-lock
                 * which may lead to starvation, so return true to avoid that.
                 */
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Invalid fd");
                return _gf_true;
        }
        /* Lets say mount1 has eager-lock(full-lock) and after the eager-lock
         * is taken mount2 opened the same file, it won't be able to
         * perform any data operations until mount1 releases eager-lock.
         * To avoid such scenario do not enable eager-lock for this transaction
         * if open-fd-count is > 1
         */

        fd_ctx = afr_fd_ctx_get (fd, this);
        if (!fd_ctx)
                return _gf_true;

        if (fd_ctx->open_fd_count > 1)
                return _gf_true;

        return _gf_false;
}


gf_boolean_t
is_afr_delayed_changelog_post_op_needed (call_frame_t *frame, xlator_t *this)
{
        afr_local_t      *local = NULL;
        gf_boolean_t      res = _gf_false;

        local = frame->local;
        if (!local)
                goto out;

        if (!local->delayed_post_op)
                goto out;

        //Mark pending changelog ASAP
        if (!afr_txn_nothing_failed (frame, this))
                goto out;

        if (local->fd && afr_are_multiple_fds_opened (local->fd, this))
                goto out;

        res = _gf_true;
out:
        return res;
}


void
afr_delayed_changelog_post_op (xlator_t *this, call_frame_t *frame, fd_t *fd,
                               call_stub_t *stub);

void
afr_delayed_changelog_wake_up_cbk (void *data)
{
        fd_t           *fd = NULL;

        fd = data;

        afr_delayed_changelog_wake_up (THIS, fd);
}


/* SET operation */
int
afr_fd_report_unstable_write (xlator_t *this, fd_t *fd)
{
        afr_fd_ctx_t *fdctx = NULL;

        fdctx = afr_fd_ctx_get (fd, this);

        LOCK(&fd->lock);
        {
                fdctx->witnessed_unstable_write = _gf_true;
        }
        UNLOCK(&fd->lock);

        return 0;
}

/* TEST and CLEAR operation */
gf_boolean_t
afr_fd_has_witnessed_unstable_write (xlator_t *this, fd_t *fd)
{
        afr_fd_ctx_t *fdctx = NULL;
        gf_boolean_t witness = _gf_false;

        fdctx = afr_fd_ctx_get (fd, this);
        if (!fdctx)
                return _gf_true;

        LOCK(&fd->lock);
        {
                if (fdctx->witnessed_unstable_write) {
                        witness = _gf_true;
                        fdctx->witnessed_unstable_write = _gf_false;
                }
        }
        UNLOCK (&fd->lock);

        return witness;
}


int
afr_changelog_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, struct iatt *pre,
                         struct iatt *post, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        int child_index = (long) cookie;
        int call_count = -1;
        afr_local_t *local = NULL;

        priv = this->private;
        local = frame->local;

        if (op_ret != 0) {
                /* Failure of fsync() is as good as failure of previous
                   write(). So treat it like one.
		*/
                gf_log (this->name, GF_LOG_WARNING,
                        "fsync(%s) failed on subvolume %s. Transaction was %s",
                        uuid_utoa (local->fd->inode->gfid),
                        priv->children[child_index]->name,
                        gf_fop_list[local->op]);

                afr_transaction_fop_failed (frame, this, child_index);
	}

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_changelog_post_op_now (frame, this);

	return 0;
}


int
afr_changelog_fsync (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        int i = 0;
        int call_count = 0;
        afr_private_t *priv = NULL;
        dict_t *xdata = NULL;
        GF_UNUSED int ret = -1;

        local = frame->local;
        priv = this->private;

        call_count = AFR_COUNT (local->transaction.pre_op, priv->child_count);

        if (!call_count) {
                /* will go straight to unlock */
                afr_changelog_post_op_now (frame, this);
                return 0;
        }

        local->call_count = call_count;

        xdata = dict_new();
        if (xdata)
                ret = dict_set_int32 (xdata, "batch-fsync", 1);

        for (i = 0; i < priv->child_count; i++) {
                if (!local->transaction.pre_op[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_changelog_fsync_cbk,
				   (void *) (long) i, priv->children[i],
				   priv->children[i]->fops->fsync, local->fd,
				   1, xdata);
                if (!--call_count)
                        break;
        }

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
afr_changelog_post_op_safe (call_frame_t *frame, xlator_t *this)
{
	afr_local_t    *local = NULL;
        afr_private_t  *priv = NULL;

	local = frame->local;
        priv = this->private;

        if (!local->fd || local->transaction.type != AFR_DATA_TRANSACTION) {
                afr_changelog_post_op_now (frame, this);
                return 0;
        }

        if (afr_changelog_pre_op_uninherit (frame, this) &&
	    afr_txn_nothing_failed (frame, this)) {
                /* just detected that this post-op is about to
                   be optimized away as a new write() has
                   already piggybacked on this frame's changelog.
                   */
                afr_changelog_post_op_now (frame, this);
                return 0;
        }

        /* Calling afr_changelog_post_op_now() now will result in
           issuing ->[f]xattrop().

           Performing a hard POST-OP (->[f]xattrop() FOP) is a more
           responsible operation that what it might appear on the surface.

           The changelog of a file (in the xattr of the file on the server)
           stores information (pending count) about the state of the file
           on the OTHER server. This changelog is blindly trusted, and must
           therefore be updated in such a way it remains trustworthy. This
           implies that decrementing the pending count (essentially "clearing
           the dirty flag") must be done STRICTLY after we are sure that the
           operation on the other server has reached stable storage.

           While the backend filesystem on that server will eventually flush
           it to stable storage, we (being in userspace) have no mechanism
           to get notified when the write became "stable".

           This means we need take matter into our own hands and issue an
           fsync() EVEN IF THE APPLICATION WAS PERFORMING UNSTABLE WRITES,
           and get an acknowledgement for it. And we need to wait for the
           fsync() acknowledgement before initiating the hard POST-OP.

           However if the FD itself was opened in O_SYNC or O_DSYNC then
           we are already guaranteed that the writes were made stable as
           part of the FOP itself. The same holds true for NFS stable
           writes which happen on an anonymous FD with O_DSYNC or O_SYNC
           flag set in the writev() @flags param. For all other write types,
           mark a flag in the fdctx whenever an unstable write is witnessed.
           */

        if (!afr_fd_has_witnessed_unstable_write (this, local->fd)) {
                afr_changelog_post_op_now (frame, this);
                return 0;
        }

        /* Check whether users want durability and perform fsync/post-op
         * accordingly.
         */
        if (priv->ensure_durability) {
                /* Time to fsync() */
                afr_changelog_fsync (frame, this);
        } else {
                afr_changelog_post_op_now (frame, this);
        }

        return 0;
}


void
afr_delayed_changelog_post_op (xlator_t *this, call_frame_t *frame, fd_t *fd,
                               call_stub_t *stub)
{
	afr_fd_ctx_t      *fd_ctx = NULL;
	call_frame_t      *prev_frame = NULL;
	struct timespec    delta = {0, };
	afr_private_t     *priv = NULL;
	afr_local_t       *local = NULL;

	priv = this->private;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
                goto out;

	delta.tv_sec = priv->post_op_delay_secs;
	delta.tv_nsec = 0;

	pthread_mutex_lock (&fd_ctx->delay_lock);
	{
		prev_frame = fd_ctx->delay_frame;
		fd_ctx->delay_frame = NULL;
		if (fd_ctx->delay_timer)
			gf_timer_call_cancel (this->ctx, fd_ctx->delay_timer);
		fd_ctx->delay_timer = NULL;
		if (!frame)
			goto unlock;
		fd_ctx->delay_timer = gf_timer_call_after (this->ctx, delta,
							   afr_delayed_changelog_wake_up_cbk,
							   fd);
		fd_ctx->delay_frame = frame;
	}
unlock:
	pthread_mutex_unlock (&fd_ctx->delay_lock);

out:
	if (prev_frame) {
		local = prev_frame->local;
		local->transaction.resume_stub = stub;
		afr_changelog_post_op_now (prev_frame, this);
	} else if (stub) {
		call_resume (stub);
	}
}


void
afr_changelog_post_op (call_frame_t *frame, xlator_t *this)
{
        afr_local_t  *local = NULL;

        local = frame->local;

        if (is_afr_delayed_changelog_post_op_needed (frame, this))
                afr_delayed_changelog_post_op (this, frame, local->fd, NULL);
        else
                afr_changelog_post_op_safe (frame, this);
}



/* Wake up the sleeping/delayed post-op, and also register
   a stub to have it resumed after this transaction
   completely finishes.

   The @stub gets saved in @local and gets resumed in
   afr_local_cleanup()
   */
void
afr_delayed_changelog_wake_resume (xlator_t *this, fd_t *fd, call_stub_t *stub)
{
        afr_delayed_changelog_post_op (this, NULL, fd, stub);
}


void
afr_delayed_changelog_wake_up (xlator_t *this, fd_t *fd)
{
        afr_delayed_changelog_post_op (this, NULL, fd, NULL);
}


int
afr_transaction_resume (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;

        local    = frame->local;

        if (local->transaction.eager_lock_on) {
                /* We don't need to retain "local" in the
                   fd list anymore, writes to all subvols
                   are finished by now */
                afr_remove_eager_lock_stub (local);
        }

        afr_restore_lk_owner (frame);

        afr_handle_symmetric_errors (frame, this);

	if (!local->pre_op_compat)
		/* new mode, pre-op was done along
		   with OP */
		afr_changelog_pre_op_update (frame, this);

        if (__fop_changelog_needed (frame, this)) {
                afr_changelog_post_op (frame, this);
        } else {
		afr_changelog_post_op_done (frame, this);
        }

        return 0;
}


/**
 * afr_transaction_fop_failed - inform that an fop failed
 */

void
afr_transaction_fop_failed (call_frame_t *frame, xlator_t *this,
                            int child_index)
{
        afr_local_t *   local = NULL;

        local = frame->local;

	local->transaction.failed_subvols[child_index] = 1;
}



        static gf_boolean_t
afr_locals_overlap (afr_local_t *local1, afr_local_t *local2)
{
        uint64_t start1 = local1->transaction.start;
        uint64_t start2 = local2->transaction.start;
        uint64_t end1 = 0;
        uint64_t end2 = 0;

        if (local1->transaction.len)
                end1 = start1 + local1->transaction.len - 1;
        else
                end1 = ULLONG_MAX;

        if (local2->transaction.len)
                end2 = start2 + local2->transaction.len - 1;
        else
                end2 = ULLONG_MAX;

        return ((end1 >= start2) && (end2 >= start1));
}

void
afr_transaction_eager_lock_init (afr_local_t *local, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_fd_ctx_t  *fdctx = NULL;
        afr_local_t   *each = NULL;

        priv = this->private;

        if (!local->fd)
                return;

        if (local->transaction.type != AFR_DATA_TRANSACTION)
                return;

        if (!priv->eager_lock)
                return;

        fdctx = afr_fd_ctx_get (local->fd, this);
        if (!fdctx)
                return;

        if (afr_are_multiple_fds_opened (local->fd, this))
                return;
        /*
         * Once full file lock is acquired in eager-lock phase, overlapping
         * writes do not compete for inode-locks, instead are transferred to the
         * next writes. Because of this overlapping writes are not ordered.
         * This can cause inconsistencies in replication.
         * Example:
         * Two overlapping writes w1, w2 are sent in parallel on same fd
         * in two threads t1, t2.
         * Both threads can execute afr_writev_wind in the following manner.
         * t1 winds w1 on brick-0
         * t2 winds w2 on brick-0
         * t2 winds w2 on brick-1
         * t1 winds w1 on brick-1
         *
         * This check makes sure the locks are not transferred for
         * overlapping writes.
         */
        LOCK (&local->fd->lock);
        {
                list_for_each_entry (each, &fdctx->eager_locked,
                                     transaction.eager_locked) {
                        if (afr_locals_overlap (each, local)) {
                                local->transaction.eager_lock_on = _gf_false;
                                goto unlock;
                        }
                }

                local->transaction.eager_lock_on = _gf_true;
                list_add_tail (&local->transaction.eager_locked,
                               &fdctx->eager_locked);
        }
unlock:
        UNLOCK (&local->fd->lock);
}


int
afr_transaction (call_frame_t *frame, xlator_t *this, afr_transaction_type type)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        fd_t            *fd   = NULL;
        int             ret   = -1;

        local = frame->local;
        priv  = this->private;

        local->transaction.resume = afr_transaction_resume;
        local->transaction.type   = type;

        ret = afr_transaction_local_init (local, this);
        if (ret < 0)
            goto out;

        afr_transaction_eager_lock_init (local, this);

        if (local->fd && local->transaction.eager_lock_on)
                afr_set_lk_owner (frame, this, local->fd);
        else
                afr_set_lk_owner (frame, this, frame->root);

        if (!local->transaction.eager_lock_on && local->loc.inode) {
                fd = fd_lookup (local->loc.inode, frame->root->pid);
                if (fd == NULL)
                        fd = fd_lookup_anonymous (local->loc.inode);

                if (fd) {
                        afr_delayed_changelog_wake_up (this, fd);
                        fd_unref (fd);
                }
        }

        if (afr_lock_server_count (priv, local->transaction.type) == 0) {
                afr_internal_lock_finish (frame, this);
        } else {
                afr_lock (frame, this);
        }
        ret = 0;
out:
        return ret;
}
