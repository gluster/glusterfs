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
#include "transaction.h"


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

	if (local->call_count == 0) {
//		loc_wipe (&local->transaction.loc);
		local->transaction.success (frame, op_ret, op_errno);
	}
	
	return 0;
}


int
afr_unlock_inode (call_frame_t *frame, afr_private_t *priv)
{
	struct flock flock;			
	afr_local_t *local = NULL;
	int i = 0;				
	int call_count = 0;		     

	call_count = up_children_count (priv->child_count, priv->child_up); 
	local = frame->local;

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = local->transaction.start;			
		flock.l_len   = local->transaction.len;
		flock.l_type  = F_UNLCK;			

		if (priv->child_up[i]) {
		    STACK_WIND (frame, afr_unlock_common_cbk,	
				priv->children[i], 
				priv->children[i]->fops->gf_file_lk, 
				&local->transaction.loc, F_SETLK, &flock); 
		}
	}

	return 0;
}


int
afr_unlock_dir (call_frame_t *frame, afr_private_t *priv)
{
	int i = 0;				
	int call_count = 0;		     
	afr_local_t *local = NULL;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local = frame->local;
	local->call_count = call_count;		

	if (local->transaction.type == AFR_DIR_LINK_TRANSACTION) 
		local->call_count *= 2;


	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
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
afr_erase_pending_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (!child_went_down (op_ret, op_errno)) {
			local->transaction.success_count++;
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	priv = this->private;

	if (local->call_count == 0) {
		if (local->transaction.success_count == 0) {
			local->transaction.error (frame, this, -1, ENOTCONN);
			goto out;
		}

		switch (local->transaction.type) {
		case AFR_INODE_TRANSACTION: 		
			afr_unlock_inode (frame, priv);
			break;
		case AFR_DIR_TRANSACTION:
			afr_unlock_dir (frame, priv);
			break;
		case AFR_DIR_LINK_TRANSACTION:
			afr_unlock_dir (frame, priv);
		}

	}

out:	
	return 0;	
}


int 
afr_erase_pending (call_frame_t *frame, afr_private_t *priv)
{
	int i = 0;				
	int call_count = 0;		     
	dict_t *xattr = get_new_dict ();
	
	afr_local_t *local = NULL;

	local = frame->local;

	dict_set_bin (xattr, local->transaction.pending, priv->pending_dec_array, 
		      priv->child_count * sizeof (int32_t));

	call_count = up_children_count (priv->child_count, priv->child_up); 

	if (call_count != priv->child_count) {
		/* some node is down, so we shouldn't erase the pending
		   entries */

		switch (local->transaction.type) {
		case AFR_INODE_TRANSACTION: 		
			afr_unlock_inode (frame, priv);
			break;
		case AFR_DIR_TRANSACTION:
			afr_unlock_dir (frame, priv);
			break;
		case AFR_DIR_LINK_TRANSACTION:
			afr_unlock_dir (frame, priv);
		}

		goto out;
	}

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {					
		if (priv->child_up[i]) {
			STACK_WIND (frame, afr_erase_pending_cbk,
				    priv->children[i], 
				    priv->children[i]->fops->xattrop,
				    local->transaction.fd, local->transaction.loc.path, 
				    GF_XATTROP_ADD_ARRAY, xattr);
		}
	}
	
out:
	return 0;
}


int32_t
afr_write_pending_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
	afr_local_t * local = NULL;
	loc_t       * loc   = NULL;

	int call_count = -1;

	local = frame->local;
	loc   = &local->transaction.loc;

	LOCK (&frame->lock);
	{
		if (!child_went_down (op_ret, op_errno)) {
			local->transaction.success_count++;
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (local->transaction.success_count == 0) {
			local->transaction.error (frame, this, -1, ENOTCONN);
			goto out;
		}

		local->transaction.fop (frame, this, loc);
	}

out:	
	return 0;	
}


int 
afr_write_pending (call_frame_t *frame, afr_private_t *priv)
{
	int i = 0;				
	int call_count = 0;		     
	dict_t *xattr = get_new_dict ();
	afr_local_t *local = NULL;

	local = frame->local;

	dict_set_bin (xattr, local->transaction.pending, priv->pending_inc_array, 
		      priv->child_count * sizeof (int32_t));

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {					
		if (priv->child_up[i]) {
			STACK_WIND (frame, afr_write_pending_cbk,
				    priv->children[i], priv->children[i]->fops->xattrop,
				    local->transaction.fd, local->transaction.loc.path, 
				    GF_XATTROP_ADD_ARRAY, xattr);
		}
	}

	return 0;
}


/* }}} */

/* {{{ lock */


static
void retry_lock_serially (call_frame_t *frame, xlator_t *this);

int32_t
afr_retry_serial_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv = NULL;
	
	int child_i = (int) cookie;
	int call_count = -1;

	local = frame->local;
	priv  = this->private;

	/* TODO: check for error */

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		local->transaction.lock_state[child_i] = 1;
	}
	UNLOCK (&frame->lock);

	retry_lock_serially (frame, this);

	if (call_count == 0) {
		afr_write_pending (frame, priv);
	}

	return 0;
}


static
void retry_lock_serially (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	unsigned char *lock_state;
	int i = 0;

	struct flock flock;

	local = frame->local;
	priv  = this->private;

	lock_state = local->transaction.lock_state;

	while (lock_state[i]) {
		i++;
	}

	flock.l_start = local->transaction.start;
	flock.l_len   = local->transaction.len;
	flock.l_type  = F_WRLCK;

	STACK_WIND_COOKIE (frame, afr_retry_serial_cbk, (void *) i,
			   priv->children[i], 
			   priv->children[i]->fops->gf_file_lk,
			   &local->transaction.loc, F_SETLKW, &flock);

}


int32_t
afr_retry_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count = 0;

	unsigned char *lock_state = NULL;

	int i = 0;

	local = frame->local;
	priv  = this->private;

	lock_state = local->transaction.lock_state;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}

	if (call_count == 0) {
		for (i = 0; i < priv->child_count; i++)
			lock_state[i] = 0;

		retry_lock_serially (frame, this);
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

	unsigned char *lock_state;

	local = frame->local;
	priv  = this->private;

	child_count = priv->child_count;
	lock_state = local->transaction.lock_state;

	local->call_count = 0;

	/* First let's unlock on the nodes we did get a lock */

	for (i = 0; i < child_count; i++) {
		if (!lock_state[i]) {
			struct flock flock;

			flock.l_start = local->transaction.start;
			flock.l_len   = local->transaction.len;
			flock.l_type  = F_UNLCK;

			local->call_count++;

			STACK_WIND (frame, afr_retry_unlock_cbk,
				    priv->children[i], priv->children[i]->fops->gf_file_lk,
				    &local->transaction.loc, F_SETLK, &flock);
		}
	}

}


int32_t
afr_lock_common_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int child_i = (int) cookie;

	unsigned char *lock_state;

	int call_count = -1;

	priv = this->private;
	local = frame->local;

	lock_state = local->transaction.lock_state;

	LOCK (&frame->lock);
	{
		if ((op_ret == 0) ||
		    child_went_down (op_ret, op_errno)) {

			lock_state[child_i] = 1;
			local->transaction.success_count++;
		}

		if (op_errno == EAGAIN) {
			/* lock acquisition failed, unlock all and try serially */
			
			lock_state[child_i] = 0;
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (local->transaction.success_count != priv->child_count) {
//			acquire_lock_serially (frame, this);
		}

		afr_write_pending (frame, priv);
	}

	return 0;
}


int
afr_lock_inode (call_frame_t *frame, afr_private_t *priv)
{
	struct flock flock;			
	int i = 0;				
	int call_count = 0;		     
	afr_local_t *local = NULL;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local = frame->local;
	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {				
		flock.l_start = local->transaction.start;			
		flock.l_len   = local->transaction.len;
		flock.l_type  = F_WRLCK;			

		if (priv->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_lock_common_cbk, (void *) i,
					   priv->children[i], 
					   priv->children[i]->fops->gf_file_lk, 
					   &local->transaction.loc, F_SETLK, &flock); 
		}
	}

	return 0;
}


int32_t
afr_lock_dir (call_frame_t *frame, afr_private_t *priv)
{
	int i = 0;				
	int call_count = 0;		     
	afr_local_t *local = NULL;

	call_count = up_children_count (priv->child_count, priv->child_up); 

	local = frame->local;
	local->call_count = call_count;
		
	if (local->transaction.type == AFR_DIR_LINK_TRANSACTION)
		local->call_count *= 2;

	for (i = 0; i < priv->child_count; i++) {				
		if (priv->child_up[i]) {
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
transaction_resume (call_frame_t *frame, afr_private_t *priv)
{
	afr_local_t *local = NULL;
	loc_t *loc = NULL;
	
	local = frame->local;
	loc = &local->transaction.loc;

	afr_erase_pending (frame, priv);

	return 0;
}


void
build_loc_from_fd (loc_t *loc, fd_t *fd)
{
	loc->path   = strdup ("");
	loc->name   = "avati's fault";
	loc->parent = inode_parent (fd->inode, 0, NULL);
	loc->inode  = inode_ref (fd->inode);
	loc->ino    = fd->inode->ino;
}


int32_t
afr_inode_transaction (call_frame_t *frame, afr_private_t *priv)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.resume = transaction_resume;
	local->transaction.type   = AFR_INODE_TRANSACTION;

	afr_lock_inode (frame, priv);

	return 0;
}


int32_t
afr_dir_transaction (call_frame_t *frame, afr_private_t *priv)

{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.resume = transaction_resume;
	local->transaction.type   = AFR_DIR_TRANSACTION;

	afr_lock_dir (frame, priv);

	return 0;
}


int32_t
afr_dir_link_transaction (call_frame_t *frame, afr_private_t *priv)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.resume = transaction_resume;
	local->transaction.type   = AFR_DIR_LINK_TRANSACTION;

	afr_lock_dir (frame, priv);

	return 0;
}
