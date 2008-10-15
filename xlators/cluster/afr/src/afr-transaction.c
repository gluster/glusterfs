/*
  Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "afr.h"
#include "afr-transaction.h"

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
		loc_wipe (&local->transaction.loc);
		FREE (local->transaction.child_up);

		local->transaction.success (frame, op_ret, op_errno);
	}
	
	return 0;
}


int
afr_unlock_inode (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			

	int i = 0;				
	int call_count = 0;		     

	afr_local_t *local = NULL;
	afr_private_t * priv = this->private;

	local = frame->local;
	call_count = up_children_count (priv->child_count, local->transaction.child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = local->transaction.start;			
		flock.l_len   = local->transaction.len;
		flock.l_type  = F_UNLCK;			

		if (local->transaction.child_up[i]) {
			STACK_WIND (frame, afr_unlock_common_cbk,	
				    priv->children[i], 
				    priv->children[i]->fops->gf_file_lk, 
				    &local->transaction.loc, 
				    local->transaction.fd, F_SETLK, &flock); 
		}
	}

	return 0;
}


int
afr_unlock_dir (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     
	afr_local_t *local = NULL;
	afr_private_t * priv = this->private;

	local = frame->local;

	call_count = up_children_count (priv->child_count, local->transaction.child_up); 

	local->call_count = call_count;		

	if (local->transaction.type == AFR_DIR_LINK_TRANSACTION) 
		local->call_count *= 2;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->transaction.child_up[i]) {
			STACK_WIND (frame, afr_unlock_common_cbk,	
				    priv->children[i], 
				    priv->children[i]->fops->gf_dir_lk, 
				    &local->transaction.loc, local->transaction.basename,
				    GF_DIR_LK_UNLOCK, GF_DIR_LK_WRLCK);

			if (local->transaction.type == AFR_DIR_LINK_TRANSACTION) {

				STACK_WIND (frame, afr_unlock_common_cbk,	
					    priv->children[i], 
					    priv->children[i]->fops->gf_dir_lk, 
					    &local->transaction.loc, 
					    local->transaction.new_basename,
					    GF_DIR_LK_UNLOCK, GF_DIR_LK_WRLCK);
			}
		}
	}

	return 0;
}


/* }}} */


/* {{{ pending */

int32_t
afr_write_pending_post_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			       int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_local_t *   local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		switch (local->transaction.type) {
		case AFR_INODE_TRANSACTION:
			afr_unlock_inode (frame, this);
			break;
		case AFR_DIR_TRANSACTION:
		case AFR_DIR_LINK_TRANSACTION:
			afr_unlock_dir (frame, this);
			break;
		}
	}

	return 0;	
}


int 
afr_write_pending_post_op (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = this->private;

	int i = 0;				
	int call_count = 0;		     
	dict_t *xattr = get_new_dict ();

	afr_local_t *local = NULL;

	local = frame->local;

	dict_set_static_bin (xattr, local->transaction.pending, priv->pending_dec_array, 
			     priv->child_count * sizeof (int32_t));

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {					
		if (local->transaction.child_up[i]) {
			STACK_WIND (frame, afr_write_pending_post_op_cbk,
				    priv->children[i], 
				    priv->children[i]->fops->xattrop,
				    local->transaction.fd, local->transaction.loc.path, 
				    GF_XATTROP_ADD_ARRAY, xattr);
		}
	}
	
	dict_unref (xattr);
	return 0;
}


int32_t
afr_write_pending_pre_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			      int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;
	loc_t       *   loc   = NULL;

	int call_count  = -1;
	int child_index = (int) cookie;

	local = frame->local;
	loc   = &local->transaction.loc;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->transaction.child_up[child_index] = 0;

			if (!child_went_down (op_ret, op_errno)) {
				gf_log (this->name, GF_LOG_ERROR,
					"xattrop failed on child %s: %s",
					priv->children[child_index]->name, 
					strerror (op_errno));
			}
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		local->transaction.fop (frame, this);
	}

	return 0;	
}


int 
afr_write_pending_pre_op (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = this->private;

	int i = 0;				
	int call_count = 0;		     
	dict_t *xattr = get_new_dict ();
	afr_local_t *local = NULL;

	local = frame->local;

	call_count = up_children_count (priv->child_count, local->transaction.child_up); 

	if (call_count == 0) {
		/* no child is up */

		/* free the dict */
		dict_unref (xattr);

		loc_wipe (&local->transaction.loc);
		FREE (local->transaction.child_up);
		local->transaction.error (frame, this, -1, ENOTCONN);
	}

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {					
		if (local->transaction.child_up[i]) {
			dict_set_static_bin (xattr, local->transaction.pending, 
					     priv->pending_inc_array, 
					     priv->child_count * sizeof (int32_t));

			STACK_WIND_COOKIE (frame, afr_write_pending_pre_op_cbk, (void *) i,
					   priv->children[i], priv->children[i]->fops->xattrop,
					   local->transaction.fd, local->transaction.loc.path, 
					   GF_XATTROP_ADD_ARRAY, xattr);
		}
	}

	dict_unref (xattr);
	return 0;
}

/* }}} */

/* {{{ lock */


static
void retry_lock_serially (call_frame_t *frame, xlator_t *this, int child_index);

int32_t
afr_retry_serial_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv = NULL;
	
	int child_index = (int) cookie;
	int call_count  = -1;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->transaction.child_up[child_index] = 0;
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	retry_lock_serially (frame, this, child_index + 1);

	if (call_count == 0) {
		afr_write_pending_pre_op (frame, this);
	}

	return 0;
}


static
void retry_lock_serially (call_frame_t *frame, xlator_t *this, int child_index)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	struct flock flock;

	local = frame->local;
	priv  = this->private;

	flock.l_start = local->transaction.start;
	flock.l_len   = local->transaction.len;
	flock.l_type  = F_WRLCK;

	STACK_WIND_COOKIE (frame, afr_retry_serial_cbk, (void *) child_index,
			   priv->children[child_index], 
			   priv->children[child_index]->fops->gf_file_lk,
			   &local->transaction.loc, 
			   local->transaction.fd, F_SETLKW, &flock);

}


int32_t
afr_retry_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = 0;
	int child_index = (int) cookie;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->transaction.child_up[child_index] = 0;
		}

		call_count = --local->call_count;
	}

	if (call_count == 0) {
		retry_lock_serially (frame, this, 0);
	}

	return 0;
}


static
void acquire_lock_serially (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int i = 0;
	int child_count = 0;

	local = frame->local;
	priv  = this->private;

	child_count = priv->child_count;

	local->call_count = 0;

	/* First let's unlock on the nodes we did get a lock */

	for (i = 0; i < child_count; i++) {
		if (local->transaction.child_errno == 0) {
			struct flock flock;

			flock.l_start = local->transaction.start;
			flock.l_len   = local->transaction.len;
			flock.l_type  = F_UNLCK;

			local->call_count++;

			STACK_WIND_COOKIE (frame, afr_retry_unlock_cbk, (void *) i,
					   priv->children[i], priv->children[i]->fops->gf_file_lk,
					   &local->transaction.loc, 
					   local->transaction.fd, F_SETLK, &flock);
		}
	}

}


int32_t
afr_lock_common_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_index = (int) cookie;

	int call_count = -1;

	priv = this->private;
	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_errno == EAGAIN) {
			/* lock acquisition failed, unlock all and try serially */
			if (local->transaction.type == AFR_INODE_TRANSACTION)
				local->transaction.failure_count++;
		}

		local->transaction.child_errno[child_index] = op_errno;
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (local->transaction.failure_count != 0) {
			acquire_lock_serially (frame, this);
		} else {
			afr_write_pending_pre_op (frame, this);
		}
	}

	return 0;
}


int
afr_lock_inode (call_frame_t *frame, xlator_t *this)
{
	struct flock flock;			
	int i = 0;				
	int call_count = 0;		     

	afr_local_t *   local = NULL;
	afr_private_t * priv  = this->private;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local = frame->local;
	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = local->transaction.start;			
		flock.l_len   = local->transaction.len;
		flock.l_type  = F_WRLCK;			

		if (local->transaction.child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_lock_common_cbk, (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->gf_file_lk, 
					   &local->transaction.loc, 
					   local->transaction.fd, F_SETLK, &flock); 
		}
	}

	return 0;
}


int32_t
afr_lock_dir (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     
	
	afr_local_t * local  = NULL;
	afr_private_t * priv = this->private;
	
	call_count = up_children_count (priv->child_count, priv->child_up); 

	local = frame->local;
	local->call_count = call_count;
		
	if (local->transaction.type == AFR_DIR_LINK_TRANSACTION)
		local->call_count *= 2;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->transaction.child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_lock_common_cbk, (void *) i,	
					   priv->children[i], 
					   priv->children[i]->fops->gf_dir_lk, 
					   &local->transaction.loc, local->transaction.basename,
					   GF_DIR_LK_LOCK, GF_DIR_LK_WRLCK);

			if (local->transaction.type == AFR_DIR_LINK_TRANSACTION) {

				STACK_WIND_COOKIE (frame, afr_lock_common_cbk, (void *) i,	
						   priv->children[i], 
						   priv->children[i]->fops->gf_dir_lk, 
						   &local->transaction.loc, 
						   local->transaction.new_basename,
						   GF_DIR_LK_LOCK, GF_DIR_LK_WRLCK);
			}
		}
	}

	return 0;
}

/* }}} */

int32_t
transaction_resume (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	
	local = frame->local;
	
	afr_write_pending_post_op (frame, this);

	return 0;
}


int32_t
afr_inode_transaction (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	local = frame->local;
	priv  = this->private;

	local->transaction.resume = transaction_resume;
	local->transaction.type   = AFR_INODE_TRANSACTION;
	local->transaction.child_up = calloc (sizeof (*local->transaction.child_up), 
					      priv->child_count);
	if (!local->transaction.child_up) {
		loc_wipe (&local->transaction.loc);
		local->transaction.error (frame, this, -1, ENOMEM);
	}

	memcpy (local->transaction.child_up, priv->child_up,
		sizeof (*local->transaction.child_up) * priv->child_count);

	afr_lock_inode (frame, this);

	return 0;
}


int32_t
afr_dir_transaction (call_frame_t *frame, xlator_t *this)

{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	local = frame->local;
	priv  = this->private;

	local->transaction.resume = transaction_resume;
	local->transaction.type   = AFR_DIR_TRANSACTION;

	local->transaction.child_up = calloc (sizeof (*local->transaction.child_up), 
					      priv->child_count);
	if (!local->transaction.child_up) {
		loc_wipe (&local->transaction.loc);
		local->transaction.error (frame, this, -1, ENOMEM);
	}

	memcpy (local->transaction.child_up, priv->child_up,
		sizeof (*local->transaction.child_up) * priv->child_count);

	afr_lock_dir (frame, this);

	return 0;
}


int32_t
afr_dir_link_transaction (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	local = frame->local;
	priv  = this->private;

	local->transaction.resume = transaction_resume;
	local->transaction.type   = AFR_DIR_LINK_TRANSACTION;
	local->transaction.child_up = calloc (sizeof (*local->transaction.child_up), 
					      priv->child_count);
	if (!local->transaction.child_up) {
		loc_wipe (&local->transaction.loc);
		local->transaction.error (frame, this, -1, ENOMEM);
	}

	memcpy (local->transaction.child_up, priv->child_up,
		sizeof (*local->transaction.child_up) * priv->child_count);

	afr_lock_dir (frame, this);

	return 0;
}
