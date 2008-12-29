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

#include "xlator.h"
#include "dict.h"
#include "ha.h"

/**********************************************************************
 *                               helper functions
 *********************************************************************/
int32_t
ha_set_state (dict_t *ctx, xlator_t *this)
{
	ha_private_t *private = NULL;
	char *state = NULL;
	int32_t ret = -1;
	int32_t child_count = 0;

	private = this->private;
	LOCK(&private->lock);
	{
		child_count = private->child_count;

		state = CALLOC(1, child_count);
		GF_VALIDATE_OR_GOTO(this->name, state, out);

		memcpy (state, private->state, child_count);
	}
	UNLOCK(&private->lock);

	ret = dict_set_dynptr (ctx, this->name, state, child_count);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to set state to context dictionary");
		goto out;
	}
	ret = 0;
out:
	return ret;
}

int32_t
ha_copy_state_to_fd (xlator_t *this,
		     fd_t *fd,
		     inode_t *inode)
{
	int32_t ret = -1;
	char *state = NULL, *fdstate = NULL;

	ret = dict_get_ptr (inode->ctx, this->name, (void *)&state);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to get state from inode");
		goto out;
	}

	fdstate = CALLOC(1, HA_CHILDREN_COUNT(this));
	memcpy (fdstate, state, HA_CHILDREN_COUNT(this));

	ret = dict_set_dynptr (fd->ctx, this->name,
			       fdstate, HA_CHILDREN_COUNT(this));
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to set state to context dictionary");
		goto out;
	}
	ret = 0;
out:
	return ret;
}

static int32_t
ha_mark_child_down (char *state,
		    int32_t child_idx)
{
	state[child_idx] = 0;

	return 0;
}

int32_t
ha_mark_child_down_for_inode (xlator_t *this,
			      inode_t *inode,
			      int32_t child_idx)
{
	char *state = NULL;
	int32_t ret = -1;

	ret = dict_get_ptr (inode->ctx, this->name, (void *)&state);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to get subvolumes' state from inode");
		goto out;
	}

	state[child_idx] = 0;
out:
	return ret;
}

int32_t
ha_next_active_child_index (xlator_t *this, 
			    int32_t discard)
{
	ha_private_t *private = NULL;
	int32_t idx = 0, ret = -1;
	int32_t child_count = 0;
	int32_t active_idx = 0;

	private = this->private;
	child_count = private->child_count;
	
	LOCK(&private->lock);
	{
		if (private->active != discard) {
			ret = 0;
			active_idx = private->active;
			goto unlock;
		}

		for (idx = 0; idx < child_count; idx++) {
			if (private->state[idx] 
			    && (idx != discard)) {
				ret = idx;
				break;
			}
		}
	}
unlock:
	UNLOCK(&private->lock);
	
	return ret;
}

static int32_t
ha_next_active_child_for_state (xlator_t *this, char *state)
{
	ha_private_t *private = NULL;
	int32_t idx = 0, ret = -1;
	int32_t child_count = 0;

	private = this->private;
	child_count = private->child_count;

	for (idx = 0; idx < child_count; idx++) {
		if (state[idx]) {
			ret = idx;
			break;
		}
	}

	if (ret < 0) {
		/* all the subvolumes have gone down in the course of this
		 * inode's life. but the distributed subvolume state stored in
		 * state is not updated by notify(). we need to update state at
		 * this point */
		LOCK(&private->lock);
		{
			memcpy (state, private->state, child_count);
		}
		UNLOCK(&private->lock);

		for (idx = 0; idx < child_count; idx++) {
			if (state[idx]) {
				ret = idx;
				break;
			}
		}

	}

	return ret;
}

int32_t
ha_first_active_child_index (xlator_t *this)
{
	ha_private_t *private = NULL;

	private = this->private;

	return private->active;
}

ha_local_t *
ha_local_init (call_frame_t *frame)
{
	ha_local_t *local = NULL;

	local = calloc (1, sizeof (ha_local_t));
	GF_VALIDATE_OR_GOTO (frame->this->name, local, out);

	local->op_ret     = -1;
	local->op_errno   = ENOTCONN;
out:
	return local;
}

xlator_t *
ha_child_for_index (xlator_t *this, int32_t idx)
{
	ha_private_t *private = NULL;
	xlator_t *active = NULL;

	private = this->private;

	if (idx != -1)
		active =  private->children[idx];

	return active;
}

static xlator_t *
_ha_next_active_child_for_ctx (xlator_t *this,
			       dict_t *ctx,
			       int32_t child_idx,
			       int32_t *ret_active_idx)
{
	xlator_t *active = NULL;
	char *state = NULL;
	int32_t ret = -1;
	int32_t active_idx = -1;

	ret = dict_get_ptr (ctx, this->name, (void *)&state);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to get the state from inode->ctx");
		goto err;
	}

	if (child_idx != HA_NONE)
		ha_mark_child_down (state, child_idx);

	active_idx = ha_next_active_child_for_state (this, state);
	if (active_idx == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected");
		errno = ENOTCONN;
		goto err;
	}

	if (ret_active_idx)
		*ret_active_idx = active_idx;

	active = ha_child_for_index (this, active_idx);

	if (active_idx == child_idx) {
		gf_log (this->name, GF_LOG_ERROR,
			"none of the children are connected other than %s",
			active->name);
		active = NULL;
	}

err:
	return active;
}

xlator_t *
ha_next_active_child_for_fd (xlator_t *this,
			     fd_t *fd,
			     int32_t child_idx,
			     int32_t *ret_active_idx)
{
	return _ha_next_active_child_for_ctx (this, fd->ctx, child_idx, ret_active_idx);
}

xlator_t *
ha_next_active_child_for_inode (xlator_t *this,
				inode_t *inode,
				int32_t child_idx,
				int32_t *ret_active_idx)
{
	return _ha_next_active_child_for_ctx (this, inode->ctx, child_idx, ret_active_idx);
}

