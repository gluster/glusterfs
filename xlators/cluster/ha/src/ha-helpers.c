/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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
#include "call-stub.h"
#include "defaults.h"
#include "dict.h"
#include "compat-errno.h"
#include "ha.h"

#define HA_TRANSPORT_NOTCONN(_ret, _errno, _fd) \
	((_ret == -1) && (_fd ? (_errno == EBADFD):(_errno == ENOTCONN)))

int ha_alloc_init_fd (call_frame_t *frame, fd_t *fd)
{
	ha_local_t *local = NULL;
	int i = -1;
	ha_private_t *pvt = NULL;
	int child_count = 0;
	int ret = -1;
	hafd_t *hafdp = NULL;
	xlator_t *this = NULL;
	uint64_t tmp_hafdp = 0;

	this = frame->this;
	local = frame->local;
	pvt = this->private;
	child_count = pvt->child_count;

	if (local == NULL) {
		ret = fd_ctx_get (fd, this, &tmp_hafdp);
		if (ret < 0) {
			goto out;
		}
		hafdp = (hafd_t *)(long)tmp_hafdp;
		local = frame->local = CALLOC (1, sizeof (*local));
		if (local == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		local->state = CALLOC (1, child_count);
		if (local->state == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		/* take care of the preferred subvolume */
		if (pvt->pref_subvol == -1)
			local->active = hafdp->active;
		else
			local->active = pvt->pref_subvol;

		LOCK (&hafdp->lock);
		memcpy (local->state, hafdp->fdstate, child_count);
		UNLOCK (&hafdp->lock);

		/* in case the preferred subvolume is down */
		if ((local->active != -1) && (local->state[local->active] == 0))
			local->active = -1;

		for (i = 0; i < child_count; i++) {
			if (local->state[i]) {
				if (local->active == -1)
					local->active = i;
				local->tries++;
			}
		}
		if (local->active == -1) {
			ret = -ENOTCONN;
			goto out;
		}
		local->fd = fd_ref (fd);
	}
	ret = 0;
out:
	return ret;
}

int ha_handle_cbk (call_frame_t *frame, void *cookie, int op_ret, int op_errno) 
{
	xlator_t *xl = NULL;
	ha_private_t *pvt = NULL;
	xlator_t **children = NULL;
	int prev_child = -1;
	hafd_t *hafdp = NULL;
	int ret = -1;
	call_stub_t *stub = NULL;
	ha_local_t *local = NULL;
	uint64_t tmp_hafdp = 0;

	xl = frame->this;
	pvt = xl->private;
	children = pvt->children;
	prev_child = (long) cookie;
	local = frame->local;

	if (op_ret == -1) {
		gf_log (xl->name, GF_LOG_ERROR ,"(child=%s) (op_ret=%d op_errno=%s)",
			children[prev_child]->name, op_ret, strerror (op_errno));
	}

	if (HA_TRANSPORT_NOTCONN (op_ret, op_errno, (local->fd))) {
		ret = 0;
		if (local->fd) {
			ret = fd_ctx_get (local->fd, xl, &tmp_hafdp);
		}
		hafdp = (hafd_t *)(long)tmp_hafdp;		
		if (ret == 0) {
			if (local->fd) {
				LOCK(&hafdp->lock);
				hafdp->fdstate[prev_child] = 0;
				UNLOCK(&hafdp->lock);
			}
			local->tries--;
			if (local->tries != 0) {
				while (1) {
					local->active = (local->active + 1) % pvt->child_count;
					if (local->state[local->active])
						break;
				}
				stub = local->stub;
				local->stub = NULL;
				call_resume (stub);
				return -1;
			}
		}
	}
	if (local->stub)
		call_stub_destroy (local->stub);
	if (local->fd) {
		FREE (local->state);
		fd_unref (local->fd);
	}
	return 0;
}

int ha_alloc_init_inode (call_frame_t *frame, inode_t *inode)
{
	int i = -1;
	ha_private_t *pvt = NULL;
	xlator_t *xl = NULL;
	int ret = -1;
	ha_local_t *local = NULL;
	uint64_t tmp_state = 0;

	xl = frame->this;
	pvt = xl->private;
	local = frame->local;

	if (local == NULL) {
		local = frame->local = CALLOC (1, sizeof (*local));
		if (local == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		local->active = pvt->pref_subvol;
		ret = inode_ctx_get (inode, xl, &tmp_state);
		if (ret < 0) {
			goto out;
		}
		local->state = (char *)(long)tmp_state;
		if (local->active != -1 && local->state[local->active] == 0)
			local->active = -1;
		for (i = 0; i < pvt->child_count; i++) {
			if (local->state[i]) {
				if (local->active == -1)
					local->active = i;
				local->tries++;
			}
		}
		if (local->active == -1) {
			ret = -ENOTCONN;
			goto out;
		}
	}
	ret = 0;
out:
	return ret;
}
