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

/*****************************************************************************
 *                                   fops
 *****************************************************************************/


int
ha_lookup_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *stbuf,
	       dict_t *xattr)
{
	ha_local_t *local = NULL;
	int32_t   active_idx = -1, child_idx = -1;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0) ||
	    HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 inode, child_idx,
						 &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_lookup_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->lookup,
			   &local->args.lookup.loc,
			   local->args.lookup.need_xattr);

	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND(frame,
		     op_ret, op_errno,
		     inode, stbuf, xattr);

	if (local) {
		loc_wipe (&local->args.lookup.loc);

		FREE(local);
	}

	return 0;
}


int
ha_lookup (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t need_xattr)
{
	ha_local_t *local = NULL;
	char *state = NULL;
	int32_t ret = -1;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.lookup.loc, loc);
	local->args.lookup.need_xattr = need_xattr;

	ret = dict_get_ptr (loc->inode->ctx,
			    this->name, (void *)&state);
	if (ret < 0) {
		ret = ha_set_state (loc->inode->ctx, this);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"failed to set state to inode->ctx for %s",
				loc->path);
			goto err;
		}
	} 

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);
	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_lookup_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->lookup,
			   loc, need_xattr);

	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, NULL, NULL, NULL);

	if (local) {
		loc_wipe (&local->args.lookup.loc);

		FREE(local);
	}
	
	return 0;
}


int
ha_stat_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     struct stat *stbuf)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}
	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.stat.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_stat_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->stat,
			   &local->args.stat.loc);

	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND(frame, op_ret, op_errno, stbuf);

	if (local) {
		loc_wipe (&local->args.stat.loc);

		FREE(local);
	}

	return 0;
}


int
ha_stat (call_frame_t *frame,
	 xlator_t *this,
	 loc_t *loc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOTCONN;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.stat.loc, loc);

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);
	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_stat_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->stat,
			   loc);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.stat.loc);

		FREE(local);
	}

	return 0;
}


int
ha_chmod_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.chmod.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_chmod_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->chmod,
			   &local->args.chmod.loc,
			   local->args.chmod.mode);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND(frame, op_ret, op_errno, stbuf);

	if (local) {
		loc_wipe (&local->args.chmod.loc);

		FREE(local);
	}

	return 0;
}


int
ha_chmod (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  mode_t mode)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOTCONN;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.chmod.loc, loc);
	local->args.chmod.mode = mode;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE(frame, ha_chmod_cbk,
			  (void *) (long) active_idx,
			  active, active->fops->chmod,
			  loc, mode);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.chmod.loc);

		FREE(local);
	}

	return 0;
}


int
ha_fchmod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.fchmod.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_fchmod_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fchmod,
			   local->args.fchmod.fd,
			   local->args.fchmod.mode);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		if (local->args.fchmod.fd)
			fd_unref (local->args.fchmod.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fchmod (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   mode_t mode)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.fchmod.fd = fd_ref (fd);
	local->args.fchmod.mode = mode;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_fchmod_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fchmod,
			   fd, mode);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		if (local->args.fchmod.fd)
			fd_unref (local->args.fchmod.fd);

		FREE(local);
	}

	return 0;
}


int
ha_chown_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.chown.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_chown_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->chown,
			   &local->args.chown.loc,
			   local->args.chown.uid,
			   local->args.chown.gid);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		loc_wipe (&local->args.chown.loc);

		FREE(local);
	}

	return 0;
}


int
ha_chown (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  uid_t uid,
	  gid_t gid)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.chown.loc, loc);
	local->args.chown.uid = uid;
	local->args.chown.gid = gid;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_chown_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->chown,
			   loc, uid, gid);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.chown.loc);

		FREE(local);
	}

	return 0;
}


int
ha_fchown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.fchown.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_fchown_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fchown,
			   local->args.fchown.fd,
			   local->args.fchown.uid,
			   local->args.fchown.gid);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		if (local->args.fchown.fd)
			fd_unref (local->args.fchown.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fchown (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   uid_t uid,
	   gid_t gid)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.fchown.fd = fd_ref (fd);
	local->args.fchown.uid = uid;
	local->args.fchown.gid = gid;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_fchown_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fchown,
			   fd, uid, gid);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		if (local->args.fchown.fd)
			fd_unref (local->args.fchown.fd);

		FREE(local);
	}

	return 0;
}


int
ha_truncate_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.truncate.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_truncate_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->truncate,
			   &local->args.truncate.loc,
			   local->args.truncate.off);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		loc_wipe (&local->args.truncate.loc);

		FREE(local);
	}

	return 0;
}


int
ha_truncate (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     off_t offset)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.truncate.loc, loc);
	local->args.truncate.off = offset;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_truncate_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->truncate,
			   loc, offset);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.truncate.loc);

		FREE(local);
	}

	return 0;
}


int
ha_ftruncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.ftruncate.fd,
					      child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_ftruncate_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->ftruncate,
			   local->args.ftruncate.fd,
			   local->args.ftruncate.off);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		if (local->args.ftruncate.fd)
			fd_unref (local->args.ftruncate.fd);

		FREE(local);
	}

	return 0;
}


int
ha_ftruncate (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      off_t offset)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.ftruncate.fd = fd_ref (fd);
	local->args.ftruncate.off = offset;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_ftruncate_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->ftruncate,
			   fd, offset);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		if (local->args.ftruncate.fd)
			fd_unref (local->args.ftruncate.fd);

		FREE(local);
	}

	return 0;
}


int
ha_utimens_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.utimens.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_utimens_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->utimens,
			   &local->args.utimens.loc,
			   local->args.utimens.tv);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		loc_wipe (&local->args.utimens.loc);

		FREE(local);
	}

	return 0;
}


int
ha_utimens (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    struct timespec tv[2])
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx  = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.utimens.loc, loc);
	local->args.utimens.tv[0] = tv[0];
	local->args.utimens.tv[1] = tv[1];

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_utimens_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->utimens,
			   loc, tv);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.utimens.loc);

		FREE(local);
	}

	return 0;
}


int
ha_access_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.access.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_access_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->access,
			   &local->args.access.loc,
			   local->args.access.mask);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		loc_wipe (&local->args.access.loc);

		FREE(local);
	}

	return 0;
}


int
ha_access (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t mask)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.access.loc, loc);
	local->args.access.mask = mask;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_access_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->access,
			   loc, mask);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		loc_wipe (&local->args.access.loc);

		FREE(local);
	}

	return 0;
}


int
ha_readlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 const char *buf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.readlink.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_readlink_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->readlink,
			   &local->args.readlink.loc,
			   local->args.readlink.size);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, buf);

	if (local) {
		loc_wipe (&local->args.readlink.loc);

		FREE(local);
	}

	return 0;
}


int
ha_readlink (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     size_t size)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = ENOMEM;
	xlator_t  *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.readlink.loc, loc);
	local->args.readlink.size = size;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_readlink_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->readlink,
			   loc, size);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.readlink.loc);

		FREE(local);
	}

	return 0;
}


int
ha_mknod_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.mknod.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_mknod_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->mknod,
			   &local->args.mknod.loc,
			   local->args.mknod.mode,
			   local->args.mknod.rdev);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND(frame, op_ret, op_errno, inode, stbuf);

	if (local) {
		loc_wipe (&local->args.mknod.loc);

		FREE(local);
	}

	return 0;

}


int
ha_mknod (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  mode_t mode,
	  dev_t rdev)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.mknod.loc, loc);
	local->args.mknod.mode = mode;
	local->args.mknod.rdev = rdev;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_mknod_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->mknod,
			   loc, mode, rdev);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, NULL, NULL);

	if (local) {
		loc_wipe (&local->args.mknod.loc);

		FREE(local);
	}

	return 0;
}


int
ha_mkdir_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.mkdir.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_mkdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->mkdir,
			   &local->args.mkdir.loc,
			   local->args.mkdir.mode);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND(frame, op_ret, op_errno, inode, stbuf);

	if (local) {
		loc_wipe (&local->args.mkdir.loc);

		FREE(local);
	}

	return 0;

}


int
ha_mkdir (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  mode_t mode)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0, ret = -1;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.mkdir.loc, loc);
	local->args.mkdir.mode = mode;

	ret = ha_set_state (loc->inode->ctx, this);
	if (ret < 0) {
		op_errno = -ret;
		gf_log (this->name, GF_LOG_ERROR,
			"failed to set inode state for %s",
			loc->path);
		goto err;
	}

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_mkdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->mkdir,
			   loc, mode);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, NULL, NULL);

	if (local) {
		loc_wipe (&local->args.mkdir.loc);

		FREE(local);
	}

	return 0;

}


int
ha_unlink_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.unlink.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_unlink_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->unlink,
			   &local->args.unlink.loc);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		loc_wipe (&local->args.unlink.loc);

		FREE(local);
	}

	return 0;
}


int
ha_unlink (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.unlink.loc, loc);

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_unlink_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->unlink,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		loc_wipe (&local->args.unlink.loc);

		FREE(local);
	}

	return 0;
}


int
ha_rmdir_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.rmdir.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_rmdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->rmdir,
			   &local->args.rmdir.loc);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		loc_wipe (&local->args.rmdir.loc);

		FREE(local);
	}

	return 0;
}


int
ha_rmdir (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.rmdir.loc, loc);

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_rmdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->rmdir,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		loc_wipe (&local->args.rmdir.loc);

		FREE(local);
	}

	return 0;
}


int
ha_symlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.symlink.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_symlink_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->symlink,
			   local->args.symlink.linkname,
			   &local->args.symlink.loc);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND(frame, op_ret, op_errno, inode, stbuf);

	if (local) {
		loc_wipe (&local->args.symlink.loc);

		FREE(local);
	}

	return 0;
}


int
ha_symlink (call_frame_t *frame,
	    xlator_t *this,
	    const char *linkname,
	    loc_t *loc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;
	int32_t ret = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.symlink.loc, loc);

	ret = ha_set_state (loc->inode->ctx, this);
	if (ret < 0) {
		op_errno = -ret;
		gf_log (this->name, GF_LOG_ERROR,
			"failed to set inode state for %s",
			loc->path);
		goto err;
	}

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_symlink_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->symlink,
			   linkname, loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	if (local) {
		loc_wipe (&local->args.symlink.loc);

		FREE(local);
	}

	return 0;
}


int
ha_rename_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.rename.old.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_rename_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->rename,
			   &local->args.rename.old,
			   &local->args.rename.new);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, buf);

	if (local) {
		loc_wipe (&local->args.rename.old);

		loc_wipe (&local->args.rename.new);

		FREE(local);
	}

	return 0;
}


int
ha_rename (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *oldloc,
	   loc_t *newloc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.rename.old, oldloc);
	loc_copy (&local->args.rename.new, newloc);

	active = ha_next_active_child_for_inode (this,
						 oldloc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_rename_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->rename,
			   oldloc, newloc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.rename.old);

		loc_wipe (&local->args.rename.new);

		FREE(local);
	}

	return 0;
}


int
ha_link_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     inode_t *inode,
	     struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.link.oldloc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_link_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->link,
			   &local->args.link.oldloc,
			   &local->args.link.newloc);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

	if (local) {
		loc_wipe (&local->args.link.oldloc);

		loc_wipe (&local->args.link.newloc);

		FREE(local);
	}

	return 0;
}


int
ha_link (call_frame_t *frame,
	 xlator_t *this,
	 loc_t *oldloc,
	 loc_t *newloc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.link.oldloc, oldloc);
	loc_copy (&local->args.link.newloc, newloc);

	active = ha_next_active_child_for_inode (this,
						 oldloc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_link_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->link,
			   oldloc, newloc);
	return 0;
err:
	STACK_UNWIND(frame, -1, ENOTCONN, NULL, NULL);

	if (local) {
		loc_wipe (&local->args.link.oldloc);

		loc_wipe (&local->args.link.newloc);

		FREE(local);
	}

	return 0;
}


int
ha_create_open_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd,
		    inode_t *inode,
		    struct stat *stbuf)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0;
	int32_t ret = -1, call_count = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto success;
	}

	child_idx = (long) cookie;

	ha_mark_child_down_for_inode (this, inode, child_idx);

success:
	LOCK(&frame->lock);
	{
		call_count = --local->call_count;
		if (local->op_ret == -1) {
			local->op_ret = op_ret;
			local->op_errno = op_errno;
		}
	}
	UNLOCK(&frame->lock);

	if (call_count != 0) {
		goto out;
	}

	if (local->op_ret == 0) {
		ret = ha_copy_state_to_fd (this, fd, inode);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"failed to set state for fd %p(path=%s)",
				fd, local->args.open.loc.path);
			op_ret = -1;
			op_errno = EINVAL;
		}
	}

	frame->local = NULL;

	STACK_UNWIND (frame, local->op_ret, local->op_errno,
		      fd, inode, &local->stbuf);

	if (local) {
		if (local->args.create.fd)
			fd_unref (local->args.create.fd);

		loc_wipe (&local->args.create.loc);

		FREE(local);
	}

out:
	return 0;
}


int
ha_create_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       fd_t *fd,
	       inode_t *inode,
	       struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL, **children = NULL;
	int32_t child_idx = 0, active_idx = 0;
	int32_t ret = -1;
	int32_t idx = 0;
	int32_t call_count = 0;
	
	local = frame->local;

	child_idx = (long) cookie;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto cont;
	}

	active = ha_next_active_child_for_inode (this,
						 local->args.create.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_create_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->create,
			   &local->args.create.loc,
			   local->args.create.flags,
			   local->args.create.mode,
			   local->args.create.fd);
	return 0;
cont:
	if (op_ret == 0) {
		local->op_ret = op_ret;
		local->op_errno = op_errno;
		local->stbuf  = *stbuf;

		local->args.create.flags = local->args.create.flags & ~O_EXCL;
			
		call_count = HA_CHILDREN_COUNT(this);
		children   = HA_CHILDREN(this);

		local->call_count = (--call_count);

		for (idx = 0; idx <= call_count; idx++) {
			if (idx != child_idx) {
				STACK_WIND_COOKIE (frame, ha_create_open_cbk,
						   (void *) (long) idx,
						   children[idx],
						   children[idx]->fops->create,
						   &local->args.create.loc, 
						   local->args.create.flags, 
						   local->args.create.mode, 
						   local->args.create.fd);
			}
		}

		goto out;
	}
unwind:
	if (op_ret == 0) {
		ret = ha_copy_state_to_fd (this, fd, inode);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"failed to set state for fd %p(ino=%"PRId64")",
				fd, inode->ino);
			op_ret = -1;
			op_errno = EINVAL;
		} 
	}

	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);

	if (local) {
		if (local->args.create.fd)
			fd_unref (local->args.create.fd);

		loc_wipe (&local->args.create.loc);

		FREE(local);
	}
out:
	return 0;
}


int
ha_create (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t flags,
	   mode_t mode, fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0, ret = -1;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.create.loc, loc);
	local->args.create.fd = fd_ref (fd);
	local->args.create.flags = flags;
	local->args.create.mode = mode;

	ret = ha_set_state (loc->inode->ctx, this);
	if (ret < 0) {
		op_errno = -ret;
		gf_log (this->name, GF_LOG_ERROR,
			"failed to set inode state for %s",
			loc->path);
		goto err;
	}

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_create_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->create,
			   loc, flags, mode, fd);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, fd, NULL, NULL);

	if (local) {
		if (local->args.create.fd)
			fd_unref (local->args.create.fd);

		loc_wipe (&local->args.create.loc);

		FREE(local);
	}

	return 0;
}


int
ha_open_cbk (call_frame_t *frame,
	     void *cookie,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0;
	int32_t ret = -1, call_count = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto success;
	}

	child_idx = (long) cookie;

	ha_mark_child_down_for_inode (this, local->args.open.loc.inode,
				      child_idx);

success:
	LOCK(&frame->lock);
	{
		call_count = --local->call_count;
		if (local->op_ret == -1) {
			local->op_ret = op_ret;
			local->op_errno = op_errno;
		}
	}
	UNLOCK(&frame->lock);

	if (call_count != 0) {
		goto out;
	}

	if (local->op_ret == 0) {
		ret = ha_copy_state_to_fd (this, fd,
					   local->args.open.loc.inode);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"failed to set state for fd %p(path=%s)",
				fd, local->args.open.loc.path);
			op_ret = -1;
			op_errno = EINVAL;
		}
	}

	frame->local = NULL;

	STACK_UNWIND(frame, local->op_ret, local->op_errno, fd);

	if (local) {
		loc_wipe (&local->args.open.loc);

		FREE(local);
	}

out:
	return 0;
}


int
ha_open (call_frame_t *frame,
	 xlator_t *this,
	 loc_t *loc,
	 int32_t flags, fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0;
	xlator_t **children = NULL;
	int32_t call_count = 0, idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.open.loc, loc);

	local->call_count = HA_CHILDREN_COUNT(this);
	call_count = HA_CHILDREN_COUNT(this);
	children   = HA_CHILDREN(this);

	frame->local = local;

	for (idx = 0; idx < call_count; idx++)
		STACK_WIND_COOKIE (frame, ha_open_cbk,
				   (void *) (long) idx,
				   children[idx], children[idx]->fops->open,
				   loc, flags, fd);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, fd);

	if (local) {
		loc_wipe (&local->args.open.loc);

		FREE(local);
	}

	return 0;
}


int
ha_readv_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct iovec *vector,
	      int32_t count,
	      struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret >= 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.readv.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_readv_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->readv,
			   local->args.readv.fd,
			   local->args.readv.size,
			   local->args.readv.off);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno,
		      vector, count,
		      stbuf);

	if (local) {
		if (local->args.readv.fd)
			fd_unref (local->args.readv.fd);

		FREE(local);
	}

	return 0;
}


int
ha_readv (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  size_t size,
	  off_t offset)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.readv.fd = fd_ref (fd);
	local->args.readv.size = size;
	local->args.readv.off = offset;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_readv_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->readv,
			   fd, size, offset);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL, 0, NULL);

	if (local) {
		if (local->args.readv.fd)
			fd_unref (local->args.readv.fd);

		FREE(local);
	}

	return 0;
}


int
ha_writev_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret >= 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.writev.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_writev_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->writev,
			   local->args.writev.fd,
			   local->args.writev.vector,
			   local->args.writev.count,
			   local->args.writev.off);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		if (local->args.writev.vector)
			FREE(local->args.writev.vector);

		if (local->args.writev.fd)
			fd_unref (local->args.writev.fd);
		
		if (local->args.writev.req_refs)
			dict_unref (local->args.writev.req_refs);

		FREE(local);
	}

	return 0;
}


int
ha_writev (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   struct iovec *vector,
	   int32_t count,
	   off_t off)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.writev.fd = fd_ref (fd);
	local->args.writev.vector = iov_dup (vector, count);
	local->args.writev.count = count;
	local->args.writev.off = off;
	local->args.writev.req_refs = dict_ref (frame->root->req_refs);

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_writev_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->writev,
			   fd, vector, count, off);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		if (local->args.writev.vector)
			FREE(local->args.writev.vector);

		if (local->args.writev.fd)
			fd_unref (local->args.writev.fd);
		
		if (local->args.writev.req_refs)
			dict_unref (local->args.writev.req_refs);

		FREE(local);
	}

	return 0;
}


int
ha_flush_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.flush.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_flush_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->flush,
			   local->args.flush.fd);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		if (local->args.flush.fd)
			fd_unref (local->args.flush.fd);

		FREE(local);
	}

	return 0;
}


int
ha_flush (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.flush.fd = fd_ref (fd);

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_flush_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->flush,
			   fd);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		if (local->args.flush.fd)
			fd_unref (local->args.flush.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fsync_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.fsync.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_fsync_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fsync,
			   local->args.fsync.fd,
			   local->args.fsync.datasync);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		if (local->args.fsync.fd)
			fd_unref (local->args.fsync.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fsync (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t datasync)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.fsync.fd = fd_ref (fd);
	local->args.fsync.datasync = datasync;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_fsync_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fsync,
			   fd, datasync);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		if (local->args.fsync.fd)
			fd_unref (local->args.fsync.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fstat_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stbuf)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.fstat.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_fstat_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fstat,
			   local->args.fstat.fd);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		if (local->args.fstat.fd)
			fd_unref (local->args.fstat.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fstat (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.fstat.fd = fd_ref (fd);

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_fstat_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fstat,
			   fd);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		if (local->args.fstat.fd)
			fd_unref (local->args.fstat.fd);

		FREE(local);
	}

	return 0;
}


int
ha_opendir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0;
	int32_t ret = -1, call_count = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto success;
	}

	child_idx = (long) cookie;

	ha_mark_child_down_for_inode (this, local->args.opendir.loc.inode,
				      child_idx);

success:
	LOCK(&frame->lock);
	{
		call_count = --local->call_count;
		if (local->op_ret == -1) {
			local->op_ret = op_ret;
			local->op_errno = op_errno;
		}
	}
	UNLOCK(&frame->lock);

	if (call_count != 0) {
		goto out;
	}

	if (local->op_ret == 0) {
		ret = ha_copy_state_to_fd (this, fd,
					   local->args.opendir.loc.inode);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"failed to set state for fd %p(path=%s)",
				fd, local->args.opendir.loc.path);
			op_ret = -1;
			op_errno = EINVAL;
		}
	}

	frame->local = NULL;

	STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);

	if (local) {
		loc_wipe (&local->args.opendir.loc);

		FREE(local);
	}

out:
	return 0;
}


int
ha_opendir (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc, fd_t *fd)
{
	ha_local_t *local = NULL;
	int32_t op_errno = 0;
	xlator_t **children = NULL;
	int32_t call_count = 0, idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.opendir.loc, loc);

	local->call_count = HA_CHILDREN_COUNT(this);
	call_count = HA_CHILDREN_COUNT(this);
	children   = HA_CHILDREN(this);

	frame->local = local;

	for (idx = 0; idx < call_count; idx++)
		STACK_WIND_COOKIE (frame, ha_opendir_cbk,
				   (void *) (long) idx,
				   children[idx], children[idx]->fops->opendir,
				   loc, fd);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, fd);

	if (local) {
		loc_wipe (&local->args.opendir.loc);

		FREE(local);
	}

	return 0;
}


int
ha_getdents_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dir_entry_t *entries,
		 int32_t count)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.getdents.fd,
					      child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_getdents_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->getdents,
			   local->args.getdents.fd,
			   local->args.getdents.size,
			   local->args.getdents.off,
			   local->args.getdents.flag);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, entries, count);

	if (local) {
		if (local->args.getdents.fd)
			fd_unref (local->args.getdents.fd);

		FREE(local);
	}

	return 0;
}


int
ha_getdents (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset,
	     int32_t flag)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t  *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.getdents.fd = fd_ref (fd);
	local->args.getdents.size = size;
	local->args.getdents.off = offset;
	local->args.getdents.flag = flag;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_getdents_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->getdents,
			   fd, size, offset, flag);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL, 0);

	if (local) {
		if (local->args.getdents.fd)
			fd_unref (local->args.getdents.fd);

		FREE(local);
	}

	return 0;
}


int
ha_setdents_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.setdents.fd,
					      child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_setdents_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->setdents,
			   local->args.setdents.fd,
			   local->args.setdents.flags,
			   &local->args.setdents.entries,
			   local->args.setdents.count);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		if (local->args.setdents.fd)
			fd_unref (local->args.setdents.fd);

		FREE(local);
	}

	return 0;
}


int
ha_setdents (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags,
	     dir_entry_t *entries,
	     int32_t count)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t  *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.setdents.fd = fd_ref(fd);
	if (entries) {
		local->args.setdents.entries.next = entries->next;
	}
	local->args.setdents.count = count;
	local->args.setdents.flags = flags;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_setdents_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->setdents,
			   fd, flags, entries, count);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		if (local->args.setdents.fd)
			fd_unref (local->args.setdents.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fsyncdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.fsyncdir.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_fsyncdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fsyncdir,
			   local->args.fsyncdir.fd,
			   local->args.fsyncdir.datasync);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		if (local->args.fsyncdir.fd)
			fd_unref (local->args.fsyncdir.fd);

		FREE(local);
	}

	return 0;
}


int
ha_fsyncdir (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t  *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.fsyncdir.fd = fd_ref (fd);
	local->args.fsyncdir.datasync = flags;

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_fsyncdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fsyncdir,
			   fd, flags);

	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		if (local->args.fsyncdir.fd)
			fd_unref (local->args.fsyncdir.fd);

		FREE(local);
	}

	return 0;
}


int
ha_statfs_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct statvfs *stbuf)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active_idx = ha_next_active_child_index (this, child_idx);

	if (active_idx == -1) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}
	active = ha_child_for_index (this, active_idx);
	STACK_WIND_COOKIE (frame, ha_statfs_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->statfs,
			   &local->args.statfs.loc);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, stbuf);

	if (local) {
		loc_wipe (&local->args.statfs.loc);

		FREE(local);
	}

	return 0;
}


int
ha_statfs (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.statfs.loc, loc);

	active_idx = ha_first_active_child_index (this);

	if (active_idx == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected");
		op_errno = ENOTCONN;
		goto err;
	}
	frame->local = local;

	active = ha_child_for_index (this, active_idx);

	STACK_WIND_COOKIE (frame, ha_statfs_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->statfs,
			   loc);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.statfs.loc);

		FREE(local);
	}

	return 0;
}


int
ha_setxattr_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.setxattr.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_setxattr_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->setxattr,
			   &local->args.setxattr.loc,
			   local->args.setxattr.dict,
			   local->args.setxattr.flags);
	return 0;
unwind:

	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		loc_wipe (&local->args.setxattr.loc);

		if (local->args.setxattr.dict)
			dict_unref (local->args.setxattr.dict);

		FREE(local);
	}

	return 0;
}


int
ha_setxattr (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     dict_t *dict,
	     int32_t flags)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.setxattr.loc, loc);
	local->args.setxattr.dict = dict_ref (dict);
	local->args.setxattr.flags = flags;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_setxattr_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->setxattr,
			   loc, dict, flags);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		loc_wipe (&local->args.setxattr.loc);

		if (local->args.setxattr.dict)
			dict_unref (local->args.setxattr.dict);

		FREE(local);
	}

	return 0;
}


int
ha_getxattr_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *dict)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.getxattr.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_getxattr_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->getxattr,
			   &local->args.getxattr.loc,
			   local->args.getxattr.name);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, dict);

	if (local) {
		loc_wipe (&local->args.getxattr.loc);

		if (local->args.getxattr.name)
			FREE(local->args.getxattr.name);

		FREE(local);
	}

	return 0;
}


int
ha_getxattr (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     const char *name)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.getxattr.loc, loc);
	if (name)
		local->args.getxattr.name = strdup (name);

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_getxattr_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->getxattr,
			   loc, name);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);

	if (local) {
		loc_wipe (&local->args.getxattr.loc);

		if (local->args.getxattr.name)
			FREE(local->args.getxattr.name);

		FREE(local);
	}

	return 0;
}


int
ha_xattrop_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *dict)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.xattrop.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_xattrop_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->xattrop,
			   &local->args.xattrop.loc,
			   local->args.xattrop.optype,
			   local->args.xattrop.xattr);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, dict);

	if (local) {
		loc_wipe (&local->args.xattrop.loc);

		if (local->args.xattrop.xattr)
			dict_unref (local->args.xattrop.xattr);

		FREE(local);
	}

	return 0;
}


int
ha_xattrop (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    gf_xattrop_flags_t flags,
	    dict_t *dict)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.xattrop.loc, loc);
	local->args.xattrop.optype = flags;
	local->args.xattrop.xattr = dict_ref (dict);

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_xattrop_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->xattrop,
			   loc, flags, dict);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, dict);

	if (local) {
		loc_wipe (&local->args.xattrop.loc);

		if (local->args.xattrop.xattr)
			dict_unref (local->args.xattrop.xattr);

		FREE(local);
	}

	return 0;
}


int
ha_fxattrop_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dict_t *dict)
{
	ha_local_t *local = NULL;
	int32_t child_idx = 0, active_idx = 0;
	xlator_t *active = NULL;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.fxattrop.fd,
					      child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_fxattrop_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fxattrop,
			   local->args.fxattrop.fd,
			   local->args.fxattrop.optype,
			   local->args.fxattrop.xattr);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, dict);

	if (local) {
		if (local->args.fxattrop.fd)
			fd_unref (local->args.fxattrop.fd);

		if (local->args.fxattrop.xattr)
			dict_unref (local->args.fxattrop.xattr);

		FREE(local);
	}

	return 0;
}


int
ha_fxattrop (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     gf_xattrop_flags_t flags,
	     dict_t *dict)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.fxattrop.fd = fd_ref (fd);
	local->args.fxattrop.optype = flags;
	local->args.fxattrop.xattr = dict_ref (dict);

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_fxattrop_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->fxattrop,
			   fd, flags, dict);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno, dict);

	if (local) {
		if (local->args.fxattrop.fd)
			fd_unref (local->args.fxattrop.fd);

		if (local->args.fxattrop.xattr)
			dict_unref (local->args.fxattrop.xattr);

		FREE(local);
	}

	return 0;
}


int
ha_removexattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.removexattr.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_removexattr_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->removexattr,
			   &local->args.removexattr.loc,
			   local->args.removexattr.name);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno);

	if (local) {
		loc_wipe (&local->args.removexattr.loc);

		FREE(local);
	}

	return 0;
}


int
ha_removexattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.removexattr.loc, loc);
	local->args.removexattr.name = strdup (name);

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_removexattr_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->removexattr,
			   loc, name);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno);

	if (local) {
		loc_wipe (&local->args.removexattr.loc);

		FREE(local);
	}

	return 0;
}


int
ha_checksum_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 uint8_t *file_checksum,
		 uint8_t *dir_checksum)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	active = ha_next_active_child_for_inode (this,
						 local->args.checksum.loc.inode,
						 child_idx, &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_checksum_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->checksum,
			   &local->args.checksum.loc,
			   local->args.checksum.flags);
	return 0;
unwind:
	frame->local = NULL;

	STACK_UNWIND(frame, op_ret, op_errno,
		     file_checksum, dir_checksum);

	if (local) {
		loc_wipe (&local->args.checksum.loc);

		FREE(local);
	}

	return 0;
}


int
ha_checksum (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flag)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	loc_copy (&local->args.checksum.loc, loc);
	local->args.checksum.flags = flag;

	active = ha_next_active_child_for_inode (this,
						 loc->inode, HA_NONE,
						 &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_checksum_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->checksum,
			   loc, flag);
	return 0;
err:
	STACK_UNWIND(frame, -1, op_errno,
		     NULL, NULL);

	if (local) {
		loc_wipe (&local->args.checksum.loc);

		FREE(local);
	}

	return 0;
}


int
ha_readdir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		gf_dirent_t *entries)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret >= 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno))
		goto unwind;

	child_idx = (long) cookie;

	active = ha_next_active_child_for_fd (this,
					      local->args.readv.fd, child_idx,
					      &active_idx);
	if (active == NULL) {
		op_ret = -1;
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto unwind;
	}

	STACK_WIND_COOKIE (frame, ha_readdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->readdir,
			   local->args.readdir.fd,
			   local->args.readdir.size,
			   local->args.readdir.off);
	return 0;

unwind:
	frame->local = NULL;

	STACK_UNWIND (frame, op_ret, op_errno, entries);

	if (local) {
		if (local->args.readdir.fd)
			fd_unref (local->args.readdir.fd);

		FREE(local);
	}

	return 0;
}


int
ha_readdir (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    size_t size,
	    off_t off)
{
	ha_local_t *local = NULL;
	int32_t     op_errno = 0;
	int32_t     active_idx = -1;
	xlator_t   *active = NULL;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	local->args.readdir.fd = fd_ref (fd);

	active = ha_next_active_child_for_fd (this,
					      fd, HA_NONE,
					      &active_idx);

	if (active == NULL) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no active subvolume");
		goto err;
	}

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_readdir_cbk,
			   (void *) (long) active_idx,
			   active, active->fops->readdir,
			   fd, size, off);
	return 0;
err:
	STACK_UNWIND (frame, -1, ENOTCONN, NULL);

	if (local) {
		if (local->args.readdir.fd)
			fd_unref (local->args.readdir.fd);

		FREE(local);
	}

	return 0;
}


/* Management operations */
int
ha_stats_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct xlator_stats *stats)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t active_idx = 0;
	int32_t child_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	/* forward call to the next child */
	active_idx = ha_next_active_child_index (this, child_idx);

	if (active_idx == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected");
		op_ret = -1;
		op_errno = ENOTCONN;
		goto unwind;
	}

	active = ha_child_for_index (this, active_idx);

	STACK_WIND_COOKIE (frame, ha_stats_cbk,
			   (void *) (long) active_idx,
			   active, active->mops->stats,
			   local->args.stats.flags);
	return 0;
unwind:
	STACK_UNWIND (frame, op_ret, op_errno, stats);
	return 0;
}


int
ha_stats (call_frame_t *frame,
	  xlator_t *this,
	  int32_t flags)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	xlator_t *active = NULL;
	int32_t active_idx = 0;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	active_idx = ha_first_active_child_index (this);

	if (active_idx == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected");
		op_errno = ENOTCONN;
		goto err;
	}

	active = ha_child_for_index (this, active_idx);

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_stats_cbk,
			   (void *) (long) active_idx,
			   active, active->mops->stats,
			   flags);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}


int
ha_getspec_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		char *spec_data)
{
	ha_local_t *local = NULL;
	xlator_t *active = NULL;
	int32_t child_idx = 0, active_idx = 0;

	local = frame->local;

	if ((op_ret == 0)
	    || HA_NOT_TRANSPORT_ERROR(op_ret, op_errno)) {
		goto unwind;
	}

	child_idx = (long) cookie;

	/* forward call to the next child */
	active_idx = ha_next_active_child_index (this, child_idx);

	if (active_idx == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected");
		op_ret = -1;
		op_errno = ENOTCONN;
		goto unwind;
	}

	active = ha_child_for_index (this, active_idx);

	STACK_WIND_COOKIE (frame, ha_getspec_cbk,
			   (void *) (long) active_idx,
			   active, active->mops->getspec,
			   local->args.getspec.key, 
			   local->args.getspec.flags);
	return 0;

unwind:
	STACK_UNWIND (frame, op_ret, op_errno, spec_data);
	return 0;
}


int
ha_getspec (call_frame_t *frame,
	    xlator_t *this,
	    const char *key,
	    int32_t flags)
{
	ha_local_t *local = NULL;
	int32_t op_errno = ENOMEM;
	int32_t active_idx = 0;
	xlator_t *active = NULL;

	local = ha_local_init (frame);
	GF_VALIDATE_OR_GOTO(this->name, local, err);

	active_idx = ha_first_active_child_index (this);

	if (active_idx == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected");
		op_errno = ENOTCONN;
		goto err;
	}
	
	active = ha_child_for_index (this, active_idx);

	frame->local = local;

	STACK_WIND_COOKIE (frame, ha_getspec_cbk,
			   (void *) (long)active_idx,
			   active, active->mops->getspec,
			   key, flags);
	return 0;
err:
	STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}


/* notify */
int
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
	ha_private_t *private = NULL;
	int32_t idx = 0, j = 0;
	int32_t i_am_down = 0, i_came_up = 0;

	private = this->private;
	if (private == NULL) {
		gf_log (this->name, GF_LOG_ERROR,
			"got notify before init()");
		return 0;
	}

	switch (event){
	case GF_EVENT_CHILD_DOWN:
	{
		/* NOTE: private->child_count & private->children are _constants_, we
		 *       don't require locking
		 */
		for (idx = 0; idx < private->child_count; idx++) {
			if (data == private->children[idx])
				break;
		}

		gf_log (this->name, GF_LOG_DEBUG,
			"GF_EVENT_CHILD_DOWN from %s",
			private->children[idx]->name);

		LOCK(&private->lock);
		{
			private->state[idx] = 0;

			if (private->active == idx) {
				private->active = -1;
				for (j = 0; j < private->child_count; j++) {
					if (private->state[j]) {
						private->active = j;
						break;
					}
				}
				if (private->active == -1) {
					gf_log (this->name, GF_LOG_DEBUG,
						"none of the subvols are up, "
						"switching \"active\" from %s to -1",
						private->children[idx]->name);
					i_am_down = 1;
				}
			}
		}
		UNLOCK(&private->lock);

		if (i_am_down)
			default_notify (this, event, data);
	}
	break;
	case GF_EVENT_CHILD_UP:
	{
		/* NOTE: private->child_count & private->children are _constants_, we
		 *       don't require locking
		 */
		for (idx = 0; idx < private->child_count; idx++) {
			if (data == private->children[idx])
				break;
		}

		gf_log (this->name, GF_LOG_DEBUG,
			"GF_EVENT_CHILD_UP from %s",
			private->children[idx]->name);

		LOCK(&private->lock);
		{
			private->state[idx] = 1;

			if (private->active == -1) {
				gf_log (this->name, GF_LOG_DEBUG,
					"switching \"active\" from -1 to %s",
					private->children[idx]->name);
				private->active = idx;
				i_came_up = 1;
			}
		}
		UNLOCK(&private->lock);
		
		if (i_came_up)
			default_notify (this, event, data);
	}
	break;

	default:
		default_notify (this, event, data);
	}

	return 0;
}


int
init (xlator_t *this)
{
	ha_private_t *private = NULL;
	xlator_list_t *trav = NULL;
	int count = 0;

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
	private = CALLOC (1, sizeof (ha_private_t));
	LOCK_INIT(&private->lock);
	private->active = -1;

	trav = this->children;
	while (trav) {
		count++;
		trav = trav->next;
	}

	private->child_count = count;
	private->children = CALLOC(count, sizeof (xlator_t*));

	trav = this->children;
	count = 0;
	while (trav) {
		private->children[count] = trav->xlator;
		count++;
		trav = trav->next;
	}

	private->state = CALLOC(1, count);
	this->private = private;
	return 0;
}


void
fini (xlator_t *this)
{
	ha_private_t *private = NULL;
	private = this->private;
	FREE(private);
	return;
}


struct xlator_fops fops = {
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
	.getdents    = ha_getdents,
	.fsyncdir    = ha_fsyncdir,
	.access      = ha_access,
	.create      = ha_create,
	.ftruncate   = ha_ftruncate,
	.fstat       = ha_fstat,
	.utimens     = ha_utimens,
	.fchmod      = ha_fchmod,
	.fchown      = ha_fchown,
	.lookup      = ha_lookup,
	.setdents    = ha_setdents,
	.readdir     = ha_readdir,
	.setdents    = ha_setdents,
	.checksum    = ha_checksum,
	.xattrop     = ha_xattrop,
	.fxattrop    = ha_fxattrop
};


struct xlator_mops mops = {
	.stats = ha_stats,
	.getspec = ha_getspec,
};


struct xlator_cbks cbks = {
};
