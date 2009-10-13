/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#include "dict.h"
#include "byte-order.h"

#include "afr.h"
#include "afr-transaction.h"

#include <signal.h>
#include <alloca.h>


static void
afr_pid_save (call_frame_t *frame)
{
        afr_local_t * local = NULL;

        local = frame->local;

        local->saved_pid = frame->root->pid;
}


static void
afr_pid_restore (call_frame_t *frame)
{
        afr_local_t * local = NULL;

        local = frame->local;

        frame->root->pid = local->saved_pid;
}


static void
__mark_all_pending (int32_t *pending[], int child_count,
                    afr_transaction_type type)
{	
	int i;
        int j;

        for (i = 0; i < child_count; i++) {
                j = afr_index_for_transaction_type (type);
		pending[i][j] = hton32 (1);
        }
}


static void
__mark_child_dead (int32_t *pending[], int child_count, int child,
                   afr_transaction_type type)
{
        int j;

        j = afr_index_for_transaction_type (type);
        
	pending[child][j] = 0;
}


static void
__mark_fop_failed_on_fd (fd_t *fd, xlator_t *this,
                         int child_index)
{
        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        int ret = 0;

        ret = fd_ctx_get (fd, this, &ctx);

        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        fd_ctx->child_failed[child_index] = 1;
out:
        return;
}


static void
__mark_failed_children (int32_t *pending[], int child_count, 
                        xlator_t *this, fd_t *fd, afr_transaction_type type)
{
        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        int ret = 0;
        int i   = 0;
        int j   = 0;

        ret = fd_ctx_get (fd, this, &ctx);

        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        for (i = 0; i < child_count; i++) {
                j = afr_index_for_transaction_type (type);

                if (fd_ctx->child_failed[i])
                        pending[i][j] = 0;
        }
        
out:
        return;
}


static void
__mark_down_children (int32_t *pending[], int child_count, 
                      unsigned char *child_up, afr_transaction_type type)
{
	int i;
	int j;

	for (i = 0; i < child_count; i++) {
                j = afr_index_for_transaction_type (type);

		if (!child_up[i])
			pending[i][j] = 0;
        }
}


static void
__mark_all_success (int32_t *pending[], int child_count,
                    afr_transaction_type type)
{
	int i;
        int j;

	for (i = 0; i < child_count; i++) {
                j = afr_index_for_transaction_type (type);
		pending[i][j] = hton32 (-1);
        }
}


static int
__is_first_write_on_fd (xlator_t *this, fd_t *fd)
{
        int op_ret     = 0;
        int _ret       = -1;

        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        LOCK (&fd->lock);
        {
                _ret = __fd_ctx_get (fd, this, &ctx);
                
                if (_ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "could not get fd ctx on fd=%p",
                                fd);
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                if (fd_ctx->pre_op_done == 0) {
                        fd_ctx->pre_op_done = 1;
                        op_ret = 1;
                }
        }
out:
        UNLOCK (&fd->lock);

        return op_ret;
}


static int
__if_fd_pre_op_done (xlator_t *this, fd_t *fd)
{
        int op_ret = 0;
        int _ret   = -1;

        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        LOCK (&fd->lock);
        {
                _ret = __fd_ctx_get (fd, this, &ctx);

                if (_ret < 0) {
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                if (fd_ctx->pre_op_done) {
                        fd_ctx->pre_op_done = 0;
                        op_ret = 1;
                }
        }
out:
        UNLOCK (&fd->lock);

        return op_ret;
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
		
	case AFR_FLUSH_TRANSACTION:
		ret = 1;
	}

	return ret;
}


static int
__changelog_needed_pre_op (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	fd_t *          fd    = NULL;

	int op_ret   = 0;

	priv  = this->private;
	local = frame->local;
	
	if (__changelog_enabled (priv, local->transaction.type)) {
		switch (local->op) {

		case GF_FOP_WRITE:
		case GF_FOP_FTRUNCATE:
			/* 
			   if it's a data transaction, we write the changelog
			   only on the first write on an fd 
			*/
			
			fd = local->fd;
			if (!fd || __is_first_write_on_fd (this, fd))
				op_ret = 1;

			break;

		case GF_FOP_FLUSH:
			/* only do post-op on flush() */

			op_ret = 0;
			break;

		default:
			op_ret = 1;
		}
	}

	return op_ret;
}


static int
__changelog_needed_post_op (call_frame_t *frame, xlator_t *this)
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
                        op_ret = 0;
                        break;

                case GF_FOP_FLUSH:
                        op_ret = __if_fd_pre_op_done (this, local->fd);
                        break;

                default:
                        op_ret = 1;
                }
        }

	return op_ret;
}


static int
afr_set_pending_dict (afr_private_t *priv, dict_t *xattr, int32_t **pending)
{
        int i;
        int ret = 0;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_set_static_bin (xattr, priv->pending_key[i],
                                           pending[i], 3 * sizeof (int32_t));
                /* 3 = data+metadata+entry */
                
                if (ret < 0)
                        goto out;
        }

out:
        return ret;
}


static int
afr_lock_server_count (afr_private_t *priv, afr_transaction_type type)
{
	int ret = 0;

	switch (type) {
	case AFR_FLUSH_TRANSACTION:
	case AFR_DATA_TRANSACTION:
		ret = priv->data_lock_server_count;
		break;

	case AFR_METADATA_TRANSACTION:
		ret = priv->metadata_lock_server_count;
		break;

	case AFR_ENTRY_TRANSACTION:
	case AFR_ENTRY_RENAME_TRANSACTION:
		ret = priv->entry_lock_server_count;
		break;
	}

	return ret;
}


/* {{{ unlock */

int32_t
afr_unlock_common_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local;
	int call_count = 0;

	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.done (frame, this);
	}
	
	return 0;
}


int
afr_unlock (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			

	int i = 0;				
	int call_count = 0;		     

	afr_local_t *local = NULL;
	afr_private_t * priv = this->private;

	local = frame->local;

        /*
          pid has been restored to saved_pid in the fop,
          so set it back to frame->root 
        */

        frame->root->pid = (long) frame->root;

	call_count = afr_locked_nodes_count (local->transaction.locked_nodes, 
					     priv->child_count);
	
	if (call_count == 0) {
		local->transaction.done (frame, this);
		return 0;
	}

	if (local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION) 
		call_count *= 2;

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = local->transaction.start;			
		flock.l_len   = local->transaction.len;
		flock.l_type  = F_UNLCK;			

		if (local->transaction.locked_nodes[i]) {
			switch (local->transaction.type) {
			case AFR_DATA_TRANSACTION:
			case AFR_METADATA_TRANSACTION:
			case AFR_FLUSH_TRANSACTION:

				if (local->fd) {
					STACK_WIND (frame, afr_unlock_common_cbk,	
						    priv->children[i], 
						    priv->children[i]->fops->finodelk, 
						    this->name, local->fd, 
                                                    F_SETLK, &flock); 
				} else {
					STACK_WIND (frame, afr_unlock_common_cbk,	
						    priv->children[i], 
						    priv->children[i]->fops->inodelk, 
						    this->name, &local->loc,
                                                    F_SETLK, &flock); 
				}
				
				break;

			case AFR_ENTRY_RENAME_TRANSACTION:
				
				STACK_WIND (frame, afr_unlock_common_cbk,	
					    priv->children[i], 
					    priv->children[i]->fops->entrylk, 
                                            this->name,
					    &local->transaction.new_parent_loc, 
					    local->transaction.new_basename,
					    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);

				call_count--;

				/* fall through */

			case AFR_ENTRY_TRANSACTION:
				if (local->fd) {
					STACK_WIND (frame, afr_unlock_common_cbk,	
						    priv->children[i], 
						    priv->children[i]->fops->fentrylk, 
						    this->name, local->fd, 
						    local->transaction.basename,
						    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
				} else {
					STACK_WIND (frame, afr_unlock_common_cbk,	
						    priv->children[i], 
						    priv->children[i]->fops->entrylk, 
						    this->name,
                                                    &local->transaction.parent_loc, 
						    local->transaction.basename,
						    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);

				}
				break;
			}
			
			if (!--call_count)
				break;
		}
	}

	return 0;
}

/* }}} */


/* {{{ pending */

int32_t
afr_changelog_post_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_private_t * priv  = NULL;
	afr_local_t *   local = NULL;
	
	int call_count = -1;

	priv  = this->private;
	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (afr_lock_server_count (priv, local->transaction.type) == 0) {
			local->transaction.done (frame, this);
		} else {
			afr_unlock (frame, this);
		}
	}

	return 0;	
}


int 
afr_changelog_post_op (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = this->private;

	int ret        = 0;
	int i          = 0;				
	int call_count = 0;
	
	afr_local_t *  local = NULL;	
	dict_t        **xattr = NULL;

	local = frame->local;

	__mark_down_children (local->pending, priv->child_count, 
                              local->child_up, local->transaction.type);
        
        if (local->op == GF_FOP_FLUSH) {
                __mark_failed_children (local->pending, priv->child_count,
                                        this, local->fd,
                                        local->transaction.type);
        }

        xattr = alloca (priv->child_count * sizeof (*xattr));
        memset (xattr, 0, (priv->child_count * sizeof (*xattr)));
	for (i = 0; i < priv->child_count; i++) {
                xattr[i] = get_new_dict ();
                dict_ref (xattr[i]);
        }

	call_count = afr_up_children_count (priv->child_count, local->child_up); 

	if (local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION) {
		call_count *= 2;
	}

	local->call_count = call_count;		

	if (call_count == 0) {
		/* no child is up */
                for (i = 0; i < priv->child_count; i++) {
                        dict_unref (xattr[i]);
                }
                
		afr_unlock (frame, this);
		return 0;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
                        ret = afr_set_pending_dict (priv, xattr[i], 
                                                    local->pending);

			if (ret < 0)
				gf_log (this->name, GF_LOG_DEBUG,
					"failed to set pending entry");


			switch (local->transaction.type) {
			case AFR_DATA_TRANSACTION:
			case AFR_METADATA_TRANSACTION:
			case AFR_FLUSH_TRANSACTION:
			{
				if (local->fd)
					STACK_WIND (frame, afr_changelog_post_op_cbk,
						    priv->children[i], 
						    priv->children[i]->fops->fxattrop,
						    local->fd, 
						    GF_XATTROP_ADD_ARRAY, xattr[i]);
				else 
					STACK_WIND (frame, afr_changelog_post_op_cbk,
						    priv->children[i], 
						    priv->children[i]->fops->xattrop,
						    &local->loc, 
						    GF_XATTROP_ADD_ARRAY, xattr[i]);
			}
			break;

			case AFR_ENTRY_RENAME_TRANSACTION:
			{
				STACK_WIND_COOKIE (frame, afr_changelog_post_op_cbk,
						   (void *) (long) i,
						   priv->children[i],
						   priv->children[i]->fops->xattrop,
						   &local->transaction.new_parent_loc,
						   GF_XATTROP_ADD_ARRAY, xattr[i]);
				
				call_count--;
			}

			/* 
			   set it again because previous stack_wind
			   might have already returned (think of case
			   where subvolume is posix) and would have
			   used the dict as placeholder for return
			   value
			*/
                        
			ret = afr_set_pending_dict (priv, xattr[i], 
                                                    local->pending);

			if (ret < 0)
				gf_log (this->name, GF_LOG_DEBUG,
					"failed to set pending entry");

			/* fall through */

			case AFR_ENTRY_TRANSACTION:
			{
				if (local->fd)
					STACK_WIND (frame, afr_changelog_post_op_cbk,
						    priv->children[i], 
						    priv->children[i]->fops->fxattrop,
						    local->fd, 
						    GF_XATTROP_ADD_ARRAY, xattr[i]);
				else 
					STACK_WIND (frame, afr_changelog_post_op_cbk,
						    priv->children[i], 
						    priv->children[i]->fops->xattrop,
						    &local->transaction.parent_loc, 
						    GF_XATTROP_ADD_ARRAY, xattr[i]);
			}
			break;
			}

			if (!--call_count)
				break;
		}
	}

        for (i = 0; i < priv->child_count; i++) {
                dict_unref (xattr[i]);
        }

	return 0;
}


int32_t
afr_changelog_pre_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			      int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;
	loc_t       *   loc   = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;

	local = frame->local;
	loc   = &local->loc;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->child_up[child_index] = 0;
			
			if (op_errno == ENOTSUP) {
				gf_log (this->name, GF_LOG_ERROR,
					"xattrop not supported by %s",
					priv->children[child_index]->name);
				local->op_ret = -1;
                                
			} else if (!child_went_down (op_ret, op_errno)) {
				gf_log (this->name, GF_LOG_ERROR,
					"xattrop failed on child %s: %s",
					priv->children[child_index]->name, 
					strerror (op_errno));
			}
			local->op_errno = op_errno;
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if ((local->op_ret == -1) && 
		    (local->op_errno == ENOTSUP)) {
			local->transaction.resume (frame, this);
		} else {
                        __mark_all_success (local->pending, priv->child_count,
                                            local->transaction.type);

                        afr_pid_restore (frame);

			local->transaction.fop (frame, this);
		}
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
	dict_t **xattr = NULL;

	afr_local_t *local = NULL;

	local = frame->local;
        
        xattr = alloca (priv->child_count * sizeof (*xattr));
        memset (xattr, 0, (priv->child_count * sizeof (*xattr)));

	for (i = 0; i < priv->child_count; i++) {
                xattr[i] = get_new_dict ();
                dict_ref (xattr[i]);
        }

	call_count = afr_up_children_count (priv->child_count, 
					    local->child_up); 

	if (local->transaction.type == AFR_ENTRY_RENAME_TRANSACTION) {
		call_count *= 2;
	}

	if (call_count == 0) {
		/* no child is up */
                for (i = 0; i < priv->child_count; i++) {
                        dict_unref (xattr[i]);
                }
                
		afr_unlock (frame, this);
		return 0;
	}

	local->call_count = call_count;		

	__mark_all_pending (local->pending, priv->child_count,
                            local->transaction.type);

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			ret = afr_set_pending_dict (priv, xattr[i], 
                                                    local->pending);

			if (ret < 0)
				gf_log (this->name, GF_LOG_DEBUG,
					"failed to set pending entry");


			switch (local->transaction.type) {
			case AFR_DATA_TRANSACTION:
			case AFR_METADATA_TRANSACTION:
			case AFR_FLUSH_TRANSACTION:
			{
				if (local->fd)
					STACK_WIND_COOKIE (frame, 
							   afr_changelog_pre_op_cbk,
							   (void *) (long) i,
							   priv->children[i], 
							   priv->children[i]->fops->fxattrop,
							   local->fd,
							   GF_XATTROP_ADD_ARRAY, xattr[i]);
				else
					STACK_WIND_COOKIE (frame, 
							   afr_changelog_pre_op_cbk,
							   (void *) (long) i,
							   priv->children[i], 
							   priv->children[i]->fops->xattrop,
							   &(local->loc), 
							   GF_XATTROP_ADD_ARRAY, xattr[i]);
			}
			break;
				
			case AFR_ENTRY_RENAME_TRANSACTION: 
			{
				STACK_WIND_COOKIE (frame, 
						   afr_changelog_pre_op_cbk,
						   (void *) (long) i,
						   priv->children[i], 
						   priv->children[i]->fops->xattrop,
						   &local->transaction.new_parent_loc, 
						   GF_XATTROP_ADD_ARRAY, xattr[i]);

				call_count--;
			}


			/* 
			   set it again because previous stack_wind
			   might have already returned (think of case
			   where subvolume is posix) and would have
			   used the dict as placeholder for return
			   value
			*/

			ret = afr_set_pending_dict (priv, xattr[i], 
                                                    local->pending);

			if (ret < 0)
				gf_log (this->name, GF_LOG_DEBUG,
					"failed to set pending entry");

			/* fall through */
				
			case AFR_ENTRY_TRANSACTION:
			{
				if (local->fd)
					STACK_WIND_COOKIE (frame, 
							   afr_changelog_pre_op_cbk,
							   (void *) (long) i,
							   priv->children[i], 
							   priv->children[i]->fops->fxattrop,
							   local->fd, 
							   GF_XATTROP_ADD_ARRAY, xattr[i]);
				else
					STACK_WIND_COOKIE (frame, 
							   afr_changelog_pre_op_cbk,
							   (void *) (long) i,
							   priv->children[i], 
							   priv->children[i]->fops->xattrop,
							   &local->transaction.parent_loc, 
							   GF_XATTROP_ADD_ARRAY, xattr[i]);
			}

			break;
			}

			if (!--call_count)
				break;
		}
	}
        
        for (i = 0; i < priv->child_count; i++) {
                dict_unref (xattr[i]);
        }
        
	return 0;
}

/* }}} */

/* {{{ lock */

static
int afr_lock_rec (call_frame_t *frame, xlator_t *this, int child_index);

int32_t
afr_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv = NULL;
	int done = 0;
	int child_index = (long) cookie;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			if (op_errno == ENOSYS) {
				/* return ENOTSUP */
				gf_log (this->name, GF_LOG_ERROR,
					"subvolume does not support locking. "
					"please load features/posix-locks xlator on server");
				local->op_ret   = op_ret;
				done = 1;
			}

			local->child_up[child_index] = 0;
			local->op_errno = op_errno;
		}
	}
	UNLOCK (&frame->lock);
	
        if ((op_ret == -1) &&
            (op_errno == ENOSYS)) {
                afr_unlock (frame, this);
        } else {
                if (op_ret == 0) {
                        local->transaction.locked_nodes[child_index] = 1;
                        local->transaction.lock_count++;
                }
                afr_lock_rec (frame, this, child_index + 1);
        }

	return 0;
}


static loc_t *
lower_path (loc_t *l1, const char *b1, loc_t *l2, const char *b2)
{
	int ret = 0;

	ret = strcmp (l1->path, l2->path);
	
	if (ret == 0) 
		ret = strcmp (b1, b2);

	if (ret <= 0)
		return l1;
	else
		return l2;
}


int32_t
afr_lock_lower_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        afr_private_t *priv  = NULL;
        afr_local_t   *local = NULL;

        int child_index = (long) cookie;

	loc_t * lower  = NULL;
	loc_t * higher = NULL;

	const char *lower_name  = NULL;
	const char *higher_name = NULL;

        priv  = this->private;
        local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			if (op_errno == ENOSYS) {
				/* return ENOTSUP */

				gf_log (this->name, GF_LOG_ERROR,
					"subvolume does not support locking. "
					"please load features/posix-locks xlator on server");

				local->op_ret   = op_ret;
			}

			local->child_up[child_index] = 0;
			local->op_errno = op_errno;
		}
	}
	UNLOCK (&frame->lock);

        if (op_ret != 0) {
                afr_unlock (frame, this);
                goto out;
        } else {
                local->transaction.locked_nodes[child_index] = 1;
        }

        /* The lower path has been locked. Now lock the higher path */

        lower = lower_path (&local->transaction.parent_loc,
                            local->transaction.basename,
                            &local->transaction.new_parent_loc,
                            local->transaction.new_basename);

        lower_name = (lower == &local->transaction.parent_loc ?
                      local->transaction.basename :
                      local->transaction.new_basename);

        higher = (lower == &local->transaction.parent_loc ?
                  &local->transaction.new_parent_loc :
                  &local->transaction.parent_loc);

        higher_name = (higher == &local->transaction.parent_loc ?
                       local->transaction.basename :
                       local->transaction.new_basename);

        STACK_WIND_COOKIE (frame, afr_lock_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->entrylk,
                           this->name, higher, higher_name,
                           ENTRYLK_LOCK, ENTRYLK_WRLCK);

out:
        return 0;
}


static
int afr_lock_rec (call_frame_t *frame, xlator_t *this, int child_index)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	struct flock flock;

	loc_t * lower  = NULL;
	loc_t * higher = NULL;

	const char *lower_name  = NULL;
	const char *higher_name = NULL;

	local = frame->local;
	priv  = this->private;

	flock.l_start = local->transaction.start;
	flock.l_len   = local->transaction.len;
	flock.l_type  = F_WRLCK;

	/* skip over children that are down */
	while ((child_index < priv->child_count)
	       && !local->child_up[child_index])
		child_index++;

	if ((child_index == priv->child_count) &&
	    local->transaction.lock_count == 0) {

		gf_log (this->name, GF_LOG_DEBUG,
			"unable to lock on even one child");

		local->op_ret   = -1;
		local->op_errno = EAGAIN;

		local->transaction.done (frame, this);
		
		return 0;

	}

	if ((child_index == priv->child_count) 
	    || (local->transaction.lock_count == 
		afr_lock_server_count (priv, local->transaction.type))) {

		/* we're done locking */

		if (__changelog_needed_pre_op (frame, this)) {
			afr_changelog_pre_op (frame, this);
		} else {
                        __mark_all_success (local->pending, priv->child_count,
                                            local->transaction.type);

                        afr_pid_restore (frame);

			local->transaction.fop (frame, this);
		}

		return 0;
	}

	switch (local->transaction.type) {
	case AFR_DATA_TRANSACTION:		
	case AFR_METADATA_TRANSACTION:
	case AFR_FLUSH_TRANSACTION:

		if (local->fd) {
			STACK_WIND_COOKIE (frame, afr_lock_cbk,
					   (void *) (long) child_index,
					   priv->children[child_index], 
					   priv->children[child_index]->fops->finodelk,
					   this->name, local->fd, 
                                           F_SETLKW, &flock);
			
		} else {
			STACK_WIND_COOKIE (frame, afr_lock_cbk,
					   (void *) (long) child_index,
					   priv->children[child_index], 
					   priv->children[child_index]->fops->inodelk,
					   this->name, &local->loc, 
                                           F_SETLKW, &flock);
		}
		
		break;
		
	case AFR_ENTRY_RENAME_TRANSACTION:
	{
		lower = lower_path (&local->transaction.parent_loc, 
				    local->transaction.basename,
				    &local->transaction.new_parent_loc,
				    local->transaction.new_basename);
		
		lower_name = (lower == &local->transaction.parent_loc ? 
			      local->transaction.basename :
			      local->transaction.new_basename);

		higher = (lower == &local->transaction.parent_loc ? 
			  &local->transaction.new_parent_loc :
			  &local->transaction.parent_loc);

		higher_name = (higher == &local->transaction.parent_loc ? 
			       local->transaction.basename :
			       local->transaction.new_basename);

		STACK_WIND_COOKIE (frame, afr_lock_lower_cbk,
				   (void *) (long) child_index,
				   priv->children[child_index], 
				   priv->children[child_index]->fops->entrylk, 
				   this->name, lower, lower_name,
				   ENTRYLK_LOCK, ENTRYLK_WRLCK);

		break;
	}
		
	case AFR_ENTRY_TRANSACTION:
		if (local->fd) {
			STACK_WIND_COOKIE (frame, afr_lock_cbk,
					   (void *) (long) child_index,	
					   priv->children[child_index], 
					   priv->children[child_index]->fops->fentrylk, 
					   this->name, local->fd, 
					   local->transaction.basename,
					   ENTRYLK_LOCK, ENTRYLK_WRLCK);
		} else {
			STACK_WIND_COOKIE (frame, afr_lock_cbk,
					   (void *) (long) child_index,	
					   priv->children[child_index], 
					   priv->children[child_index]->fops->entrylk, 
					   this->name, 
                                           &local->transaction.parent_loc, 
					   local->transaction.basename,
					   ENTRYLK_LOCK, ENTRYLK_WRLCK);
		}

		break;
	}

	return 0;
}


int32_t afr_lock (call_frame_t *frame, xlator_t *this)
{
        afr_pid_save (frame);

        frame->root->pid = (long) frame->root;

	return afr_lock_rec (frame, this, 0);
}


/* }}} */

int32_t
afr_transaction_resume (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	local = frame->local;
	priv  = this->private;

	if (__changelog_needed_post_op (frame, this)) {
		afr_changelog_post_op (frame, this);
	} else {
		if (afr_lock_server_count (priv, local->transaction.type) == 0) {
			local->transaction.done (frame, this);
		} else {
			afr_unlock (frame, this);
		}
	}

	return 0;
}


/**
 * afr_transaction_fop_failed - inform that an fop failed
 */

void
afr_transaction_fop_failed (call_frame_t *frame, xlator_t *this, int child_index)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	local = frame->local;
	priv  = this->private;

        switch (local->op) {
        case GF_FOP_WRITE:
                __mark_fop_failed_on_fd (local->fd, this, child_index);
                break;
        default:
                __mark_child_dead (local->pending, priv->child_count,
                                   child_index, local->transaction.type);
                break;
        }
}


int32_t
afr_transaction (call_frame_t *frame, xlator_t *this, afr_transaction_type type)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	local = frame->local;
	priv  = this->private;

	afr_transaction_local_init (local, priv);

	local->transaction.resume = afr_transaction_resume;
	local->transaction.type   = type;

	if (afr_lock_server_count (priv, local->transaction.type) == 0) {
		if (__changelog_needed_pre_op (frame, this)) {
			afr_changelog_pre_op (frame, this);
		} else {
                        __mark_all_success (local->pending, priv->child_count,
                                            local->transaction.type);

                        afr_pid_restore (frame);

			local->transaction.fop (frame, this);
		}
	} else {
		afr_lock (frame, this);
	}

	return 0;
}
