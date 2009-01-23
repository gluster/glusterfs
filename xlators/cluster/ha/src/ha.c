/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/* generate errors randomly, code is simple now, better alogorithm
 * can be written to decide what error to be returned and when
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "dict.h"
#include "compat-errno.h"
#include "ha.h"

/*
 * TODO:
 * - dbench fails if ha over server side afr
 * - lock calls - lock on all subvols.
 * - support preferred-subvolume option. code already there.
 * - do not alloc the call-stub in case only one subvol is up.
 */

int32_t 
ha_lookup_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *buf,
	       dict_t *dict)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int child_count = 0, i = 0, callcnt = 0;
	data_t *state_data = NULL;
	char *state = NULL;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++) {
		if (pvt->children[i] == prev_frame->this)
			break;
	}
	if ((op_ret == -1) && (op_errno != ENOENT)) {
		gf_log (this->name, GF_LOG_ERROR, "(child=%s) (op_ret=%d op_errno=%s)", 
			  children[i]->name, op_ret, strerror (op_errno));
	}
	state_data = dict_get (local->inode->ctx, this->name);
	state = data_to_ptr (state_data);

	LOCK (&frame->lock);
	if (local->revalidate == 1) {
		if ((!op_ret) != state[i]) {
			local->revalidate_error = 1;
			gf_log (this->name, GF_LOG_DEBUG, "revalidate error on %s", 
				pvt->children[i]->name);
		}
	} else {
		if (op_ret == 0) {
			state[i] = 1;
		}
	}
	if (local->op_ret == -1 && op_ret == 0) {
		local->op_ret = 0;
		local->buf = *buf;
		if (dict)
			local->dict = dict_ref (dict);
	}
	if (op_ret == -1 && op_ret != ENOTCONN)
		local->op_errno = op_errno;
	callcnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (callcnt == 0) {
		dict_t *ctx = local->dict;
		inode_t *inode = local->inode;
		if (local->revalidate_error == 1) {
			local->op_ret = -1;
			local->op_errno = EIO;
			gf_log (this->name, GF_LOG_DEBUG, "revalidate error, returning EIO");
		}
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      inode,
			      &local->buf,
			      ctx);
		if (inode)
			inode_unref (inode);
		if (ctx)
			dict_unref (ctx);
	}
	return 0;
}

int32_t
ha_lookup (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t need_xattr)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int child_count = 0, i = 0;
	data_t *state_data = NULL;
	char *state = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	children = pvt->children;

	frame->local = local = CALLOC (1, sizeof (*local));
	child_count = pvt->child_count;
	local->inode = inode_ref (loc->inode);
	state_data = dict_get (loc->inode->ctx, this->name);
	if (state_data == NULL) {
		state = CALLOC (1, child_count);
		dict_set (loc->inode->ctx, this->name, 
			  data_from_dynptr (state, child_count));
	} else
		local->revalidate = 1;
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->call_count = child_count;

	for (i = 0; i < child_count; i++) {
		STACK_WIND (frame,
			    ha_lookup_cbk,
			    children[i],
			    children[i]->fops->lookup,
			    loc,
			    need_xattr);
	}
	return 0;
}

 int32_t
ha_stat_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_stat (call_frame_t *frame,
	 xlator_t *this,
	 loc_t *loc)
{
	ha_local_t *local = NULL;
	int op_errno = ENOTCONN;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_stat_stub (frame, ha_stat, loc);

	STACK_WIND_COOKIE (frame,
			   ha_stat_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->stat,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;	
}

 int32_t
ha_chmod_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_chmod (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  mode_t mode)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_chmod_stub (frame, ha_chmod, loc, mode);

	STACK_WIND_COOKIE (frame,
			   ha_chmod_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->chmod,
			   loc,
			   mode);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

 int32_t
ha_fchmod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t 
ha_fchmod (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   mode_t mode)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_fchmod_stub (frame, ha_fchmod, fd, mode);

	STACK_WIND_COOKIE (frame,
			   ha_fchmod_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->fchmod,
			   fd,
			   mode);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

 int32_t
ha_chown_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_chown (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  uid_t uid,
	  gid_t gid)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_chown_stub (frame, ha_chown, loc, uid, gid);

	STACK_WIND_COOKIE (frame,	      
			   ha_chown_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->chown,
			   loc,
			   uid,
			   gid);
	return 0;
err:
	STACK_UNWIND (frame, -1, ENOTCONN, NULL);
	return 0;
}

 int32_t
ha_fchown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t 
ha_fchown (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   uid_t uid,
	   gid_t gid)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_fchown_stub (frame, ha_fchown, fd, uid, gid);

	STACK_WIND_COOKIE (frame,	      
			   ha_fchown_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->fchown,
			   fd,
			   uid,
			   gid);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

 int32_t
ha_truncate_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_truncate (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     off_t offset)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_truncate_stub (frame, ha_truncate, loc, offset);

	STACK_WIND_COOKIE (frame,
			   ha_truncate_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->truncate,
			   loc,
			   offset);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

 int32_t
ha_ftruncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_ftruncate (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      off_t offset)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_ftruncate_stub (frame, ha_ftruncate, fd, offset);

	STACK_WIND_COOKIE (frame,
			   ha_ftruncate_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->ftruncate,
			   fd,
			   offset);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

int32_t 
ha_utimens_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t 
ha_utimens (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    struct timespec tv[2])
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_utimens_stub (frame, ha_utimens, loc, tv);

	STACK_WIND_COOKIE (frame,
			   ha_utimens_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->utimens,
			   loc,
			   tv);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

int32_t
ha_access_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_access (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t mask)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_access_stub (frame, ha_access, loc, mask);

	STACK_WIND_COOKIE (frame,
			   ha_access_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->access,
			   loc,
			   mask);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}


 int32_t
ha_readlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 const char *path)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      path);
	}
	return 0;
}

int32_t
ha_readlink (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     size_t size)
{
	ha_local_t *local = frame->local;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_readlink_stub (frame, ha_readlink, loc, size);

	STACK_WIND_COOKIE (frame,
			   ha_readlink_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->readlink,
			   loc,
			   size);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

int
ha_mknod_lookup_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf,
		     dict_t *dict)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0, ret = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.mknod.loc.path, op_ret, op_errno);
	}
	ret = dict_get_ptr (local->stub->args.mknod.loc.inode->ctx, this->name, (void *)&stateino);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "unwind(-1), dict_get_ptr() error");
		/* It is difficult to handle this error at this stage
		 * as we still expect more cbks, we can't return as
		 * of now
		 */
	} else if (op_ret == 0) {
		stateino[i] = 1;
	}
	LOCK (&frame->lock);
	cnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (cnt == 0) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      local->stub->args.mknod.loc.inode,
			      &local->buf);
		call_stub_destroy (stub);
	}
	return 0;
}

int32_t
ha_mknod_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *buf)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0, ret = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		local->op_errno = op_errno;
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.mknod.loc.path, op_ret, op_errno);
	}

	ret = dict_get_ptr (local->stub->args.mknod.loc.inode->ctx, this->name, (void *)&stateino);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_get_ptr() error");
		/* FIXME: handle the case */
	}
	if (op_ret == 0) {
		stateino[i] = 1;
		local->op_ret = 0;
		local->first_success = 1;
		local->buf = *buf;
	}
	cnt = --local->call_count;
	for (i = local->active + 1; i < child_count; i++) {
		if (local->state[i])
			break;
	}

	if (cnt == 0 || i == child_count) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		stub = local->stub;
		STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stub->args.mknod.loc.inode, &local->buf);
		call_stub_destroy (stub);
		return 0;
	}

	local->active = i;

	if (local->first_success == 0) {
		STACK_WIND (frame,
			    ha_mknod_cbk,
			    children[i],
			    children[i]->fops->mknod,
			    &local->stub->args.mknod.loc,
			    local->stub->args.mknod.mode,
			    local->stub->args.mknod.rdev);
		return 0;
	}
	cnt = local->call_count;

	for (; i < child_count; i++) {
		if (local->state[i]) {
			STACK_WIND (frame,
				    ha_mknod_lookup_cbk,
				    children[i],
				    children[i]->fops->lookup,
				    &local->stub->args.mknod.loc,
				    0);
			if (--cnt == 0)
				break;
		}
	}
	return 0;
}

int32_t
ha_mknod (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  mode_t mode,
	  dev_t rdev)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int child_count = 0, i = 0;
	char *stateino = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;

	frame->local = local = CALLOC (1, sizeof (*local));
	local->stub = fop_mknod_stub (frame, ha_mknod, loc, mode, rdev);
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->state = CALLOC (1, child_count);
	memcpy (local->state, pvt->state, child_count);
	local->active = -1;

	stateino = CALLOC (1, child_count);
	dict_set (loc->inode->ctx, this->name, data_from_dynptr (stateino, child_count));

	for (i = 0; i < child_count; i++) {
		if (local->state[i]) {
			local->call_count++;
			if (local->active == -1) 
				local->active = i;
		}
	}

	STACK_WIND (frame,
		    ha_mknod_cbk,
		    HA_ACTIVE_CHILD(this, local),
		    HA_ACTIVE_CHILD(this, local)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}


int
ha_mkdir_lookup_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf,
		     dict_t *dict)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.mkdir.loc.path, op_ret, op_errno);
	}
	stateino = data_to_ptr (dict_get (local->stub->args.mkdir.loc.inode->ctx, this->name));  

	if (op_ret == 0)
		stateino[i] = 1;

	LOCK (&frame->lock);
	cnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (cnt == 0) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      local->stub->args.mkdir.loc.inode,
			      &local->buf);
		call_stub_destroy (stub);
	}
	return 0;
}

int32_t
ha_mkdir_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *buf)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;
	
	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		local->op_errno = op_errno;
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.mkdir.loc.path, op_ret, op_errno);
	}
	stateino = data_to_ptr (dict_get (local->stub->args.mkdir.loc.inode->ctx, this->name));

	if (op_ret == 0) {
		stateino[i] = 1;
		local->op_ret = 0;
		local->first_success = 1;
		local->buf = *buf;
	}
	cnt = --local->call_count;
	for (i = local->active + 1; i < child_count; i++) {
		if (local->state[i])
			break;
	}

	if (cnt == 0 || i == child_count) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		stub = local->stub;
		STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stub->args.mkdir.loc.inode, &local->buf);
		call_stub_destroy (stub);
		return 0;
	}

	local->active = i;

	if (local->first_success == 0) {
		STACK_WIND (frame,
			    ha_mkdir_cbk,
			    children[i],
			    children[i]->fops->mkdir,
			    &local->stub->args.mkdir.loc,
			    local->stub->args.mkdir.mode);
		return 0;
	}
	cnt = local->call_count;

	for (; i < child_count; i++) {
		if (local->state[i]) {
			STACK_WIND (frame,
				    ha_mkdir_lookup_cbk,
				    children[i],
				    children[i]->fops->lookup,
				    &local->stub->args.mkdir.loc,
				    0);
			if (--cnt == 0)
				break;
		}
	}
	return 0;
}

int32_t
ha_mkdir (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  mode_t mode)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int child_count = 0, i = 0;
	char *stateino = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;

	frame->local = local = CALLOC (1, sizeof (*local));
	local->stub = fop_mkdir_stub (frame, ha_mkdir, loc, mode);
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->state = CALLOC (1, child_count);
	memcpy (local->state, pvt->state, child_count);
	local->active = -1;

	stateino = CALLOC (1, child_count);
	dict_set (loc->inode->ctx, this->name, data_from_dynptr (stateino, child_count));

	for (i = 0; i < child_count; i++) {
		if (local->state[i]) {
			local->call_count++;
			if (local->active == -1)
				local->active = i;
		}
	}

	STACK_WIND (frame,
		    ha_mkdir_cbk,
		    HA_ACTIVE_CHILD(this, local),
		    HA_ACTIVE_CHILD(this, local)->fops->mkdir,
		    loc, mode);
	return 0;
}

 int32_t
ha_unlink_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame, op_ret, op_errno);
	}
	return 0;
}

int32_t
ha_unlink (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);

	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_unlink_stub (frame, ha_unlink, loc);

	STACK_WIND_COOKIE (frame,
			   ha_unlink_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->unlink,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

 int32_t
ha_rmdir_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_rmdir (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
	ha_local_t *local = frame->local;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_rmdir_stub (frame, ha_rmdir, loc);

	STACK_WIND_COOKIE (frame,
			   ha_rmdir_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->rmdir,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}


int
ha_symlink_lookup_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode,
		       struct stat *buf,
		       dict_t *dict)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.symlink.loc.path, op_ret, op_errno);
	}
	stateino = data_to_ptr (dict_get (local->stub->args.symlink.loc.inode->ctx, this->name));  

	if (op_ret == 0)
		stateino[i] = 1;

	LOCK (&frame->lock);
	cnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (cnt == 0) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      local->stub->args.symlink.loc.inode,
			      &local->buf);
		call_stub_destroy (stub);
	}
	return 0;
}

int32_t
ha_symlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		local->op_errno = op_errno;
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.symlink.loc.path, op_ret, op_errno);
	}
	stateino = data_to_ptr (dict_get (local->stub->args.symlink.loc.inode->ctx, this->name));

	if (op_ret == 0) {
		stateino[i] = 1;
		local->op_ret = 0;
		local->first_success = 1;
		local->buf = *buf;
	}
	cnt = --local->call_count;
	for (i = local->active + 1; i < child_count; i++) {
		if (local->state[i])
			break;
	}

	if (cnt == 0 || i == child_count) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		stub = local->stub;
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local->stub->args.symlink.loc.inode, &local->buf);
		call_stub_destroy (stub);
		return 0;
	}

	local->active = i;

	if (local->first_success == 0) {
		STACK_WIND (frame,
			    ha_symlink_cbk,
			    children[i],
			    children[i]->fops->symlink,
			    local->stub->args.symlink.linkname,
			    &local->stub->args.symlink.loc);
		return 0;
	}
	cnt = local->call_count;

	for (; i < child_count; i++) {
		if (local->state[i]) {
			STACK_WIND (frame,
				    ha_symlink_lookup_cbk,
				    children[i],
				    children[i]->fops->lookup,
				    &local->stub->args.symlink.loc,
				    0);
			if (--cnt == 0)
				break;
		}
	}
	return 0;
}

int32_t
ha_symlink (call_frame_t *frame,
	    xlator_t *this,
	    const char *linkname,
	    loc_t *loc)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int child_count = 0, i = 0;
	char *stateino = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;

	frame->local = local = CALLOC (1, sizeof (*local));
	local->stub = fop_symlink_stub (frame, ha_symlink, linkname, loc);
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->state = CALLOC (1, child_count);
	memcpy (local->state, pvt->state, child_count);
	local->active = -1;

	stateino = CALLOC (1, child_count);
	dict_set (loc->inode->ctx, this->name, data_from_dynptr (stateino, child_count));

	for (i = 0; i < child_count; i++) {
		if (local->state[i]) {
			local->call_count++;
			if (local->active == -1) {
				local->active = i;
			}
		}
	}

	STACK_WIND (frame,
		    ha_symlink_cbk,
		    HA_ACTIVE_CHILD(this, local),
		    HA_ACTIVE_CHILD(this, local)->fops->symlink,
		    linkname, loc);
	return 0;
}

 int32_t
ha_rename_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame, op_ret, op_errno, buf);
	}
	return 0;
}

int32_t
ha_rename (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *oldloc,
	   loc_t *newloc)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, oldloc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_rename_stub (frame, ha_rename, oldloc, newloc);
	STACK_WIND_COOKIE (frame,
			   ha_rename_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->rename,
			   oldloc, newloc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

int
ha_link_lookup_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf,
		    dict_t *dict)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;


	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.link.newloc.path, op_ret, op_errno);
	}
	stateino = data_to_ptr (dict_get (local->stub->args.link.newloc.inode->ctx, this->name));  

	if (op_ret == 0)
		stateino[i] = 1;

	LOCK (&frame->lock);
	cnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (cnt == 0) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      local->stub->args.link.oldloc.inode,
			      &local->buf);
		call_stub_destroy (stub);
	}
	return 0;
}

int32_t
ha_link_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     inode_t *inode,
	     struct stat *buf)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	char *stateino = NULL;
	int child_count = 0, i = 0, cnt = 0;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	for (i = 0; i < child_count; i++)
		if (prev_frame->this == children[i])
			break;

	if (op_ret == -1) {
		local->op_errno = op_errno;
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.link.newloc.path, op_ret, op_errno);
	}
	stateino = data_to_ptr (dict_get (local->stub->args.link.newloc.inode->ctx, this->name));

	if (op_ret == 0) {
		stateino[i] = 1;
		local->op_ret = 0;
		local->first_success = 1;
		local->buf = *buf;
	}
	cnt = --local->call_count;
	for (i = local->active + 1; i < child_count; i++) {
		if (local->state[i])
			break;
	}

	if (cnt == 0 || i == child_count) {
		call_stub_t *stub = local->stub;
		FREE (local->state);
		stub = local->stub;
		STACK_UNWIND (frame, local->op_ret, local->op_errno, local->stub->args.link.oldloc.inode, &local->buf);
		call_stub_destroy (stub);
		return 0;
	}

	local->active = i;

	if (local->first_success == 0) {
		STACK_WIND (frame,
			    ha_link_cbk,
			    children[i],
			    children[i]->fops->link,
			    &local->stub->args.link.oldloc,
			    &local->stub->args.link.newloc);
		return 0;
	}
	cnt = local->call_count;

	for (; i < child_count; i++) {
		if (local->state[i]) {
			STACK_WIND (frame,
				    ha_link_lookup_cbk,
				    children[i],
				    children[i]->fops->lookup,
				    &local->stub->args.link.newloc,
				    0);
			if (--cnt == 0)
				break;
		}
	}
	return 0;
}

int32_t
ha_link (call_frame_t *frame,
	 xlator_t *this,
	 loc_t *oldloc,
	 loc_t *newloc)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int child_count = 0, i = 0;
	char *stateino = NULL;
	int32_t ret = 0;

	ret = dict_get_ptr (newloc->inode->ctx, this->name, (void *) &stateino);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_ptr_error()");
	}

	if (stateino == NULL) {
		gf_log (this->name, GF_LOG_ERROR, "newloc->inode->ctx is NULL, returning EINVAL");
		STACK_UNWIND (frame, -1, EINVAL, oldloc->inode, NULL);
		return 0;
	}

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;

	frame->local = local = CALLOC (1, sizeof (*local));
	local->stub = fop_link_stub (frame, ha_link, oldloc, newloc);
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->state = CALLOC (1, child_count);
	memcpy (local->state, pvt->state, child_count);
	local->active = -1;

	for (i = 0; i < child_count; i++) {
		if (local->state[i]) {
			local->call_count++;
			if (local->active == -1)
				local->active = i;
		}
	}

	STACK_WIND (frame,
		    ha_link_cbk,
		    HA_ACTIVE_CHILD(this, local),
		    HA_ACTIVE_CHILD(this, local)->fops->link,
		    oldloc,
		    newloc);
	return 0;
}

int32_t
ha_create_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       fd_t *fd,
	       inode_t *inode,
	       struct stat *buf)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int i, child_count = 0, cnt = 0, ret = 0;
	char *stateino = NULL;
	hafd_t *hafdp = NULL;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	prev_frame = cookie;
	children = pvt->children;

	ret = dict_get_ptr (local->stub->args.create.loc.inode->ctx, this->name, (void *) &stateino);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_to_ptr() error");
		/* FIXME: handle */
	}
	ret = dict_get_ptr (local->stub->args.create.fd->ctx, this->name, (void *) &hafdp);
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_to_ptr() error");
		/* FIXME: handle */
	}

	for (i = 0; i < child_count; i++) {
		if (prev_frame->this == children[i])
			break;
	}

	if (op_ret == -1) {
		local->op_errno = op_errno;
		gf_log (this->name, GF_LOG_ERROR, "(path=%s) (op_ret=%d op_errno=%d)", local->stub->args.create.loc.path, op_ret, op_errno);
	}
	if (op_ret != -1) {
		stateino[i] = 1;
		hafdp->fdstate[i] = 1;
		if (local->op_ret == -1) {
			local->op_ret = 0;
			local->buf = *buf;
			local->first_success = 1;
		}
		local->stub->args.create.flags &= (~O_EXCL);
	}
	LOCK (&frame->lock);
	cnt = --local->call_count;
	UNLOCK (&frame->lock);

	for (i = local->active + 1; i < child_count; i++) {
		if (local->state[i])
			break;
	}

	if (cnt == 0 || i == child_count) {
		char *state = local->state;
		call_stub_t *stub = local->stub;
		STACK_UNWIND (frame, local->op_ret, local->op_errno,
			      stub->args.create.fd,
			      stub->args.create.loc.inode, &local->buf);
		FREE (state);
		call_stub_destroy (stub);
		return 0;
	}
	local->active = i;
	cnt = local->call_count;
	for (; i < child_count; i++) {
		if (local->state[i]) {
			STACK_WIND (frame,
				    ha_create_cbk,
				    children[i],
				    children[i]->fops->create,
				    &local->stub->args.create.loc,
				    local->stub->args.create.flags,
				    local->stub->args.create.mode,
				    local->stub->args.create.fd);
			if ((local->first_success == 0) || (cnt == 0))
				break;
		}
	}
	return 0;
}

int32_t
ha_create (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t flags,
	   mode_t mode, fd_t *fd)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	int i, child_count = 0;
	char *stateino = NULL;
	xlator_t **children = NULL;
	hafd_t *hafdp = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	children = pvt->children;

	if (local == NULL) {
		local = frame->local = CALLOC (1, sizeof (*local));
		local->stub = fop_create_stub (frame, ha_create, loc, flags, mode, fd);
		local->state = CALLOC (1, child_count);
		local->active = -1;
		local->op_ret = -1;
		local->op_errno = ENOTCONN;
		memcpy (local->state, pvt->state, child_count);

		for (i = 0; i < pvt->child_count; i++) {
			if (local->state[i]) {
				local->call_count++;
				if (local->active == -1)
					local->active = i;
			}
		}
		/* FIXME handle active -1 */
		stateino = CALLOC (1, child_count);
		hafdp = CALLOC (1, sizeof (*hafdp));
		hafdp->fdstate = CALLOC (1, child_count);
		hafdp->path = strdup(loc->path);
		LOCK_INIT (&hafdp->lock);
		dict_set (fd->ctx, this->name, data_from_dynptr (hafdp, sizeof (*hafdp)));
		dict_set (loc->inode->ctx, this->name, data_from_dynptr (stateino, child_count));
	}

	STACK_WIND (frame,
		    ha_create_cbk,
		    children[local->active],
		    children[local->active]->fops->create,
		    loc, flags, mode, fd);
	return 0;
}

 int32_t
ha_open_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     fd_t *fd)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	xlator_t **children = NULL;
	int i = 0, child_count = 0, callcnt = 0, ret = 0;
	call_frame_t *prev_frame = NULL;
	hafd_t *hafdp = NULL;

	local = frame->local;
	pvt = this->private;
	children = pvt->children;
	child_count = pvt->child_count;
	prev_frame = cookie;

	ret = dict_get_ptr (local->fd->ctx, this->name, (void *) &hafdp);

	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_ptr_error()");
	}

	for (i = 0; i < child_count; i++)
		if (children[i] == prev_frame->this)
			break;
	LOCK (&frame->lock);
	if (op_ret != -1) {
		hafdp->fdstate[i] = 1;
		local->op_ret = 0;
	}
	if (op_ret == -1 && op_errno != ENOTCONN)
		local->op_errno = op_errno;
	callcnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (callcnt == 0) {
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      local->fd);
	}
	return 0;
}

int32_t
ha_open (call_frame_t *frame,
	 xlator_t *this,
	 loc_t *loc,
	 int32_t flags, fd_t *fd)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	dict_t *ctx = NULL;
	char *stateino = NULL;
	xlator_t **children = NULL;
	int cnt = 0, i, child_count = 0, ret = 0;
	hafd_t *hafdp = NULL;

	local = frame->local;
	pvt = this->private;
	ctx = fd->ctx;
	children = pvt->children;
	child_count = pvt->child_count;


	local = frame->local = CALLOC (1, sizeof (*local));
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->fd = fd;

	hafdp = CALLOC (1, sizeof (*hafdp));
	hafdp->fdstate = CALLOC (1, child_count);
	hafdp->path = strdup (loc->path);
	hafdp->active = -1;
	if (pvt->pref_subvol == -1) {
		hafdp->active = fd->inode->ino % child_count;
	}

	LOCK_INIT (&hafdp->lock);
	dict_set (ctx, this->name, data_from_dynptr (hafdp, sizeof (*hafdp)));
	ret = dict_get_ptr (loc->inode->ctx, this->name, (void *) &stateino);

	for (i = 0; i < child_count; i++)
		if (stateino[i])
			cnt++;
	local->call_count = cnt;
	for (i = 0; i < child_count; i++) {
		if (stateino[i]) {
			STACK_WIND (frame,
				    ha_open_cbk,
				    children[i],
				    children[i]->fops->open,
				    loc, flags, fd);
			if (--cnt == 0)
				break;
		}
	}
	return 0;
}

 int32_t
ha_readv_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct iovec *vector,
	      int32_t count,
	      struct stat *stbuf)
{
	int ret = 0;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      vector,
			      count,
			      stbuf);
	}
	return 0;
}

int32_t
ha_readv (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  size_t size,
	  off_t offset)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_readv_stub (frame, ha_readv, fd, size, offset);

	STACK_WIND_COOKIE (frame,
			   ha_readv_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->readv,
			   fd,
			   size,
			   offset);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

 int32_t
ha_writev_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
	int ret = 0;
	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      stbuf);
	}
	return 0;
}

int32_t
ha_writev (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   struct iovec *vector,
	   int32_t count,
	   off_t off)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_writev_stub (frame, ha_writev, fd, vector, count, off);

	STACK_WIND_COOKIE (frame,
			   ha_writev_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->writev,
			   fd,
			   vector,
			   count,
			   off);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;	
}

 int32_t
ha_flush_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
	int ret = 0;
	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_flush (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_flush_stub (frame, ha_flush, fd);
	STACK_WIND_COOKIE (frame,
			   ha_flush_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->flush,
			   fd);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}


 int32_t
ha_fsync_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
	int ret = 0;
	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_fsync (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t flags)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_fsync_stub (frame, ha_fsync, fd, flags);
	STACK_WIND_COOKIE (frame,
			   ha_fsync_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->fsync,
			   fd,
			   flags);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

 int32_t
ha_fstat_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *buf)
{
	int ret = 0;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_fstat (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);

	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_fstat_stub (frame, ha_fstat, fd);
	STACK_WIND_COOKIE (frame,
			   ha_fstat_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->fstat,
			   fd);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

int32_t
ha_opendir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	xlator_t **children = NULL;
	int i = 0, child_count = 0, callcnt = 0, ret = 0;
	call_frame_t *prev_frame = NULL;
	hafd_t *hafdp = NULL;

	local = frame->local;
	pvt = this->private;
	children = pvt->children;
	child_count = pvt->child_count;
	prev_frame = cookie;

	ret = dict_get_ptr (local->fd->ctx, this->name, (void *) &hafdp);

	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_ptr_error()");
	}

	for (i = 0; i < child_count; i++)
		if (children[i] == prev_frame->this)
			break;
	LOCK (&frame->lock);
	if (op_ret != -1) {
		hafdp->fdstate[i] = 1;
		local->op_ret = 0;
	}
	if (op_ret == -1 && op_errno != ENOTCONN)
		local->op_errno = op_errno;
	callcnt = --local->call_count;
	UNLOCK (&frame->lock);

	if (callcnt == 0) {
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      local->fd);
	}
	return 0;
}

int32_t
ha_opendir (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc, fd_t *fd)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	dict_t *ctx = NULL;
	char *stateino = NULL;
	xlator_t **children = NULL;
	int cnt = 0, i, child_count = 0, ret = 0;
	hafd_t *hafdp = NULL;

	local = frame->local;
	pvt = this->private;
	ctx = fd->ctx;
	children = pvt->children;
	child_count = pvt->child_count;

	local = frame->local = CALLOC (1, sizeof (*local));
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->fd = fd;

	hafdp = CALLOC (1, sizeof (*hafdp));
	hafdp->fdstate = CALLOC (1, child_count);
	hafdp->path = strdup (loc->path);
	LOCK_INIT (&hafdp->lock);
	dict_set (ctx, this->name, data_from_dynptr (hafdp, sizeof (*hafdp)));
	ret = dict_get_ptr (loc->inode->ctx, this->name, (void *) &stateino);
	
	if (ret != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_get_ptr() error");
	}
	for (i = 0; i < child_count; i++)
		if (stateino[i])
			cnt++;
	local->call_count = cnt;
	for (i = 0; i < child_count; i++) {
		if (stateino[i]) {
			STACK_WIND (frame,
				    ha_opendir_cbk,
				    children[i],
				    children[i]->fops->opendir,
				    loc, fd);
			if (--cnt == 0)
				break;
		}
	}
	return 0;
}

 int32_t
ha_getdents_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dir_entry_t *entries,
		 int32_t count)
{
	int ret = 0;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      entries,
			      count);
	}
	return 0;
}

int32_t
ha_getdents (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset,
	     int32_t flag)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_getdents_stub (frame, ha_getdents, fd, size, offset, flag);
	STACK_WIND_COOKIE (frame,
			   ha_getdents_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->getdents,
			   fd,
			   size,
			   offset,
			   flag);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL, 0);
	return 0;
}

 int32_t
ha_setdents_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	int ret = 0;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_setdents (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags,
	     dir_entry_t *entries,
	     int32_t count)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;

	local->stub = fop_setdents_stub (frame, ha_setdents, fd, flags, entries, count);

	STACK_WIND_COOKIE (frame,
			   ha_setdents_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->setdents,
			   fd,
			   flags,
			   entries,
			   count);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

 int32_t
ha_fsyncdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	int ret = 0;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_fsyncdir (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_fsyncdir_stub (frame, ha_fsyncdir, fd, flags);
	STACK_WIND_COOKIE (frame,
			   ha_fsyncdir_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->fsyncdir,
			   fd,
			   flags);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}


 int32_t
ha_statfs_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct statvfs *buf)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      buf);
	}
	return 0;
}

int32_t
ha_statfs (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;

	local->stub = fop_statfs_stub (frame, ha_statfs, loc);
	STACK_WIND_COOKIE (frame,
			   ha_statfs_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->statfs,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

 int32_t
ha_setxattr_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_setxattr (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     dict_t *dict,
	     int32_t flags)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_setxattr_stub (frame, ha_setxattr, loc, dict, flags);
	STACK_WIND_COOKIE (frame,
			   ha_setxattr_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->setxattr,
			   loc,
			   dict,
			   flags);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

 int32_t
ha_getxattr_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *dict)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);

	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      dict);
	}
	return 0;
}

int32_t
ha_getxattr (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     const char *name)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_getxattr_stub (frame, ha_getxattr, loc, name);
	STACK_WIND_COOKIE (frame,
			   ha_getxattr_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->getxattr,
			   loc,
			   name);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}

int32_t
ha_xattrop_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *dict)
{
	int ret = -1;
	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame, op_ret, op_errno, dict);
	}
	return 0;
}


int32_t
ha_xattrop (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    gf_xattrop_flags_t flags,
	    dict_t *dict)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;

	local->stub = fop_xattrop_stub (frame, ha_xattrop, loc, flags, dict);

	STACK_WIND_COOKIE (frame,
			   ha_xattrop_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->xattrop,
			   loc,
			   flags,
			   dict);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, dict);
	return 0;
}

int32_t
ha_fxattrop_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *dict)
{
	int ret = -1;
	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0)
		STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
ha_fxattrop (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     gf_xattrop_flags_t flags,
	     dict_t *dict)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_fxattrop_stub (frame, ha_fxattrop, fd, flags, dict);

	STACK_WIND_COOKIE (frame,
			   ha_fxattrop_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->fxattrop,
			   fd,
			   flags,
			   dict);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, dict);
	return 0;
}

 int32_t
ha_removexattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	int ret = -1;
	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_removexattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	
	local->stub = fop_removexattr_stub (frame, ha_removexattr, loc, name);

	STACK_WIND_COOKIE (frame,
			   ha_removexattr_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->removexattr,
			   loc,
			   name);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

int32_t
ha_lk_setlk_unlck_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct flock *lock)
{
	ha_local_t *local = NULL;
	int cnt = 0;
	call_stub_t *stub = NULL;

	local = frame->local;

	LOCK (&frame->lock);
	cnt = --local->call_count;
	if (op_ret == 0)
		local->op_ret = 0;
	UNLOCK (&frame->lock);

	if (cnt == 0) {
		stub = local->stub;
		FREE (local->state);
		if (stub->args.lk.lock.l_type == F_UNLCK) {
			STACK_UNWIND (frame, local->op_ret, local->op_errno, &stub->args.lk.lock);
		} else {
			STACK_UNWIND (frame, -1, EIO, NULL);
		}
		call_stub_destroy (stub);
	}
	return 0;
}

int32_t
ha_lk_setlk_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct flock *lock)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	xlator_t **children = NULL;
	int i = 0, cnt = 0, j = 0;
	int child_count = 0;
	call_frame_t *prev_frame = NULL;
	char *state = NULL;

	local = frame->local;
	pvt = this->private;
	children = pvt->children;
	child_count = pvt->child_count;
	prev_frame = cookie;
	state = local->state;

	if (op_ret == 0)
		local->op_ret = 0;

	if ((op_ret == 0) || (op_ret == -1 && op_errno == ENOTCONN)) {
		for (i = 0; i < child_count; i++) {
			if (prev_frame->this == cookie)
				break;
		}
		i++;
		for (; i < child_count; i++) {
			if (local->state[i])
				break;
		}
		if (i == child_count) {
			call_stub_t *stub = local->stub;
			FREE (local->state);
			STACK_UNWIND (frame, 0, op_errno, &stub->args.lk.lock);
			call_stub_destroy (stub);
			return 0;
		}
		STACK_WIND (frame,
			    ha_lk_setlk_cbk,
			    children[i],
			    children[i]->fops->lk,
			    local->stub->args.lk.fd,
			    local->stub->args.lk.cmd,
			    &local->stub->args.lk.lock);
		return 0;
	} else {
		for (i = 0; i < child_count; i++) {
			if (prev_frame->this == cookie)
				break;
		}
		cnt = 0;
		for (j = 0; j < i; j++) {
			if (state[i])
				cnt++;
		}
		if (cnt) {
			struct flock lock;
			lock = local->stub->args.lk.lock;
			for (i = 0; i < child_count; i++) {
				if (state[i]) {
					STACK_WIND (frame,
						    ha_lk_setlk_unlck_cbk,
						    children[i],
						    children[i]->fops->lk,
						    local->stub->args.lk.fd,
						    local->stub->args.lk.cmd,
						    &lock);
					if (--cnt == 0)
						break;
				}
			}
			return 0;
		} else {
			FREE (local->state);
			call_stub_destroy (local->stub);
			STACK_UNWIND (frame,
				      op_ret,
				      op_errno,
				      lock);
			return 0;
		}
	}
}

int32_t
ha_lk_getlk_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct flock *lock)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	fd_t *fd = NULL;
	int child_count = 0, i = 0;
	xlator_t **children = NULL;
	call_frame_t *prev_frame = NULL;

	local = frame->local;
	pvt = this->private;
	fd = local->stub->args.lk.fd;
	child_count = pvt->child_count;
	children = pvt->children;
	prev_frame = cookie;

	if (op_ret == 0) {
		FREE (local->state);
		call_stub_destroy (local->stub);
		STACK_UNWIND (frame, 0, 0, lock);
		return 0;
	}

	for (i = 0; i < child_count; i++) {
		if (prev_frame->this == children[i])
			break;
	}

	for (; i < child_count; i++) {
		if (local->state[i])
			break;
	}

	if (i == child_count) {
		FREE (local->state);
		call_stub_destroy (local->stub);
		STACK_UNWIND (frame, op_ret, op_errno, lock);
		return 0;
	}

	STACK_WIND (frame,
		    ha_lk_getlk_cbk,
		    children[i],
		    children[i]->fops->lk,
		    fd,
		    local->stub->args.lk.cmd,
		    &local->stub->args.lk.lock);
	return 0;
}

int32_t
ha_lk (call_frame_t *frame,
       xlator_t *this,
       fd_t *fd,
       int32_t cmd,
       struct flock *lock)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	hafd_t *hafdp = NULL;
	char *state = NULL;
	int child_count = 0, i = 0, cnt = 0, ret = 0;
	xlator_t **children = NULL;

	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;
	children = pvt->children;
	ret = dict_get_ptr (fd->ctx, this->name, (void *)&hafdp);
	if (ret < 0)
		gf_log (this->name, GF_LOG_ERROR, "dict_get failed on fd ctx");

	if (local == NULL) {
		local = frame->local = CALLOC (1, sizeof (*local));
		local->active = -1;
		local->op_ret = -1;
		local->op_errno = ENOTCONN;
	}

	if (local->active == -1) {
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	local->stub = fop_lk_stub (frame, ha_lk, fd, cmd, lock);
	local->state = CALLOC (1, child_count);
	state = hafdp->fdstate;
	LOCK (&hafdp->lock);
	memcpy (local->state, state, child_count);
	UNLOCK (&hafdp->lock);
	if (cmd == F_GETLK) {
		for (i = 0; i < child_count; i++) {
			if (local->state[i])
				break;
		}
		STACK_WIND (frame,
			    ha_lk_getlk_cbk,
			    children[i],
			    children[i]->fops->lk,
			    fd,
			    cmd,
			    lock);
	} else if (cmd == F_SETLK && lock->l_type == F_UNLCK) {
		for (i = 0; i < child_count; i++) {
			if (local->state[i])
				local->call_count++;
		}
		cnt = local->call_count;
		for (i = 0; i < child_count; i++) {
			if (local->state[i]) {
				STACK_WIND (frame,
					    ha_lk_setlk_unlck_cbk,
					    children[i],
					    children[i]->fops->lk,
					    fd, cmd, lock);
				if (--cnt == 0)
					break;
			}
		}
	} else {
		for (i = 0; i < child_count; i++) {
			if (local->state[i])
				break;
		}
		STACK_WIND (frame,
			    ha_lk_setlk_cbk,
			    children[i],
			    children[i]->fops->lk,
			    fd,
			    cmd,
			    lock);
	}
	return 0;
}

 int32_t
ha_inode_entry_lk_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno);
	}
	return 0;
}

int32_t
ha_inodelk (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t cmd,
	    struct flock *lock)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_inodelk_stub (frame, ha_inodelk, loc, cmd, lock);
	STACK_WIND_COOKIE (frame,
			   ha_inode_entry_lk_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->inodelk,
			   loc,
			   cmd,
			   lock);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

int32_t
ha_entrylk (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    const char *basename,
	    entrylk_cmd cmd,
	    entrylk_type type)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_entrylk_stub (frame, ha_entrylk, loc, basename, cmd, type);
	STACK_WIND_COOKIE (frame,
			   ha_inode_entry_lk_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->entrylk,
			   loc, basename, cmd, type);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);
	return 0;
}

 int32_t
ha_checksum_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 uint8_t *file_checksum,
		 uint8_t *dir_checksum)
{
	int ret = -1;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0) {
		STACK_UNWIND (frame,
			      op_ret,
			      op_errno,
			      file_checksum,
			      dir_checksum);
	}
	return 0;
}

int32_t
ha_checksum (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flag)
{
	int op_errno = 0;
	ha_local_t *local = NULL;

	op_errno = ha_alloc_init_inode (frame, loc->inode);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_checksum_stub (frame, ha_checksum, loc, flag);

	STACK_WIND_COOKIE (frame,
			   ha_checksum_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->checksum,
			   loc,
			   flag);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
	return 0;
}

int32_t
ha_readdir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		gf_dirent_t *entries)
{
	int ret = 0;

	ret = ha_handle_cbk (frame, cookie, op_ret, op_errno);
	if (ret == 0)
		STACK_UNWIND (frame, op_ret, op_errno, entries);
	return 0;
}

int32_t
ha_readdir (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    size_t size,
	    off_t off)
{
	ha_local_t *local = NULL;
	int op_errno = 0;

	op_errno = ha_alloc_init_fd (frame, fd);
	if (op_errno < 0) {
		op_errno = -op_errno;
		goto err;
	}
	local = frame->local;
	local->stub = fop_readdir_stub (frame, ha_readdir, fd, size, off);
	STACK_WIND_COOKIE (frame,
			   ha_readdir_cbk,
			   (void *)local->active,
			   HA_ACTIVE_CHILD(this, local),
			   HA_ACTIVE_CHILD(this, local)->fops->readdir,
			   fd, size, off);
	return 0;
err:
	STACK_UNWIND (frame, -1, ENOTCONN, NULL);
	return 0;
}

/* Management operations */

 int32_t
ha_stats_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct xlator_stats *stats)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;
	int i = 0;

	local = frame->local;
	pvt = this->private;
	prev_frame = cookie;
	children = pvt->children;

	if (op_ret == -1 && op_errno == ENOTCONN) {
		for (i = 0; i < pvt->child_count; i++) {
			if (prev_frame->this == children[i])
				break;
		}
		i++;
		for (; i < pvt->child_count; i++) {
			if (pvt->state[i])
				break;
		}

		if (i == pvt->child_count) {
			STACK_UNWIND (frame, -1, ENOTCONN, NULL);
			return 0;
		}
		STACK_WIND (frame,
			    ha_stats_cbk,
			    children[i],
			    children[i]->mops->stats,
			    local->flags);
		return 0;
	}

	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      stats);
	return 0;
}

int32_t
ha_stats (call_frame_t *frame,
	  xlator_t *this,
	  int32_t flags)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	xlator_t **children = NULL;
	int i = 0;

	local = frame->local = CALLOC (1, sizeof (*local));
	pvt = this->private;
	children = pvt->children;
	for (i = 0; i < pvt->child_count; i++) {
		if (pvt->state[i])
			break;
	}

	if (i == pvt->child_count) {
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}
	local->flags = flags;

	STACK_WIND (frame,
		    ha_stats_cbk,
		    children[i],
		    children[i]->mops->stats,
		    flags);
	return 0;
}


int32_t
ha_getspec_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		char *spec_data)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	call_frame_t *prev_frame = NULL;
	xlator_t **children = NULL;
	int i = 0;

	local = frame->local;
	pvt = this->private;
	prev_frame = cookie;
	children = pvt->children;

	if (op_ret == -1 && op_errno == ENOTCONN) {
		for (i = 0; i < pvt->child_count; i++) {
			if (prev_frame->this == children[i])
				break;
		}
		i++;
		for (; i < pvt->child_count; i++) {
			if (pvt->state[i])
				break;
		}

		if (i == pvt->child_count) {
			STACK_UNWIND (frame, -1, ENOTCONN, NULL);
			return 0;
		}
		STACK_WIND (frame,
			    ha_getspec_cbk,
			    children[i],
			    children[i]->mops->getspec,
			    local->pattern,
			    local->flags);
		return 0;
	}

	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      spec_data);
	return 0;
}

int32_t
ha_getspec (call_frame_t *frame,
	    xlator_t *this,
	    const char *key,
	    int32_t flags)
{
	ha_local_t *local = NULL;
	ha_private_t *pvt = NULL;
	xlator_t **children = NULL;
	int i = 0;

	local = frame->local = CALLOC (1, sizeof (*local));
	pvt = this->private;
	children = pvt->children;

	local = frame->local = CALLOC (1, sizeof (*local));
	for (i = 0; i < pvt->child_count; i++) {
		if (pvt->state[i])
			break;
	}

	if (i == pvt->child_count) {
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}
	local->flags = flags;
	local->pattern = (char *)key;

	STACK_WIND (frame,
		    ha_getspec_cbk,
		    children[i],
		    children[i]->mops->getspec,
		    key, flags);
	return 0;
}

int32_t
ha_closedir (xlator_t *this,
	     fd_t *fd)
{
	hafd_t *hafdp = NULL;
	int op_errno = 0;

	op_errno = dict_get_ptr (fd->ctx, this->name, (void *) &hafdp);
	if (op_errno != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_get_ptr() error");
		return 0;
	}
	FREE (hafdp->fdstate);
	FREE (hafdp->path);
	LOCK_DESTROY (&hafdp->lock);
	return 0;
}

int32_t
ha_close (xlator_t *this,
	  fd_t *fd)
{
	hafd_t *hafdp = NULL;
	int op_errno = 0;

	op_errno = dict_get_ptr (fd->ctx, this->name, (void *)&hafdp);
	if (op_errno != 0) {
		gf_log (this->name, GF_LOG_ERROR, "dict_get_ptr() error");
		return 0;
	}

	FREE (hafdp->fdstate);
	FREE (hafdp->path);
	LOCK_DESTROY (&hafdp->lock);
	return 0;
}

/* notify */
int32_t
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
	ha_private_t *pvt = NULL;
	int32_t i = 0, upcnt = 0;

	pvt = this->private;
	if (pvt == NULL) {
		gf_log (this->name, GF_LOG_DEBUG, "got notify before init()");
		return 0;
	}

	switch (event)
	{
	case GF_EVENT_CHILD_DOWN:
	{
		for (i = 0; i < pvt->child_count; i++) {
			if (data == pvt->children[i])
				break;
		}
	        gf_log (this->name, GF_LOG_DEBUG, "GF_EVENT_CHILD_DOWN from %s", pvt->children[i]->name);
		pvt->state[i] = 0;
		for (i = 0; i < pvt->child_count; i++) {
			if (pvt->state[i])
				break;
		}
		if (i == pvt->child_count) {
			default_notify (this, event, data);
		}
	}
	break;
	case GF_EVENT_CHILD_UP:
	{
		for (i = 0; i < pvt->child_count; i++) {
			if (data == pvt->children[i])
				break;
		}

		gf_log (this->name, GF_LOG_DEBUG, "GF_EVENT_CHILD_UP from %s", pvt->children[i]->name);

		pvt->state[i] = 1;

		for (i = 0; i < pvt->child_count; i++) {
			if (pvt->state[i])
				upcnt++;
		}

		if (upcnt == 1) {
			default_notify (this, event, data);
		}
	}
	break;

	default:
	{
		default_notify (this, event, data);
	}
	}

	return 0;
}

int
init (xlator_t *this)
{
	ha_private_t *pvt = NULL;
	xlator_list_t *trav = NULL;
	int count = 0, ret = 0;

	if (!this->children) {
		gf_log (this->name,GF_LOG_ERROR, 
			"FATAL: ha should have one or more child defined");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}
  
	trav = this->children;
	pvt = CALLOC (1, sizeof (ha_private_t));

	ret = dict_get_int32 (this->options, "preferred-subvolume", &pvt->pref_subvol);
	if (ret < 0) {
		pvt->pref_subvol = -1;
	}

	trav = this->children;
	while (trav) {
		count++;
		trav = trav->next;
	}

	pvt->child_count = count;
	pvt->children = CALLOC (count, sizeof (xlator_t*));

	trav = this->children;
	count = 0;
	while (trav) {
		pvt->children[count] = trav->xlator;
		count++;
		trav = trav->next;
	}

	pvt->state = CALLOC (1, count);
	this->private = pvt;
	return 0;
}

void
fini (xlator_t *this)
{
	ha_private_t *priv = NULL;
	priv = this->private;
	FREE (priv);
	return;
}


struct xlator_fops fops = {
	.lookup      = ha_lookup,
	.stat        = ha_stat,
	.readlink    = ha_readlink,
	.mknod       = ha_mknod,
	.mkdir       = ha_mkdir,
	.unlink      = ha_unlink,
	.rmdir       = ha_rmdir,
	.symlink     = ha_symlink,
	.rename      = ha_rename,
	.link        = ha_link,
	.chmod       = ha_chmod,
	.chown       = ha_chown,
	.truncate    = ha_truncate,
	.utimens     = ha_utimens,
	.create      = ha_create,
	.open        = ha_open,
	.readv       = ha_readv,
	.writev      = ha_writev,
	.statfs      = ha_statfs,
	.flush       = ha_flush,
	.fsync       = ha_fsync,
	.setxattr    = ha_setxattr,
	.getxattr    = ha_getxattr,
	.removexattr = ha_removexattr,
	.opendir     = ha_opendir,
	.readdir     = ha_readdir,
	.getdents    = ha_getdents,
	.fsyncdir    = ha_fsyncdir,
	.access      = ha_access,
	.ftruncate   = ha_ftruncate,
	.fstat       = ha_fstat,
	.lk          = ha_lk,
	.fchmod      = ha_fchmod,
	.fchown      = ha_fchown,
	.setdents    = ha_setdents,
	.lookup_cbk  = ha_lookup_cbk,
	.checksum    = ha_checksum,
	.xattrop     = ha_xattrop,
	.fxattrop    = ha_fxattrop
};

struct xlator_mops mops = {
	.stats = ha_stats,
	.getspec = ha_getspec,
};

struct xlator_cbks cbks = {
	.release = ha_close,
	.releasedir = ha_closedir,
};
