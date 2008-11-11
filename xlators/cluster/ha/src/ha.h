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

#ifndef __HA_H_
#define __HA_H_

typedef struct {
	call_stub_t *stub;
	int32_t op_ret, op_errno;
	int32_t active, tries, revalidate, revalidate_error;
	int32_t call_count;
	char *state, *pattern;
	dict_t *dict;
	loc_t *loc;
	struct stat buf;
	fd_t *fd;
	inode_t *inode;
	int32_t flags;
	int32_t first_success;
} ha_local_t;

typedef struct {
	char *state;
	xlator_t **children;
	int child_count, active, write_load_balance;
} ha_private_t;

typedef struct {
	char *fdsuccess;
	char *fdstate;
	char *path;
	gf_lock_t lock;
	int active;
} hafd_t;

#define HA_ACTIVE_CHILD(this, local) (((ha_private_t *)this->private)->children[local->active])

#define HA_CALL_CODE_FD(this, frame, local, hafdp) do {			\
		if (local == NULL) {					\
			int i;						\
			ha_private_t *pvt = this->private;		\
			int child_count = pvt->child_count;		\
			local = frame->local = calloc (1, sizeof (*local)); \
			local->active = pvt->active;			\
			local->state = calloc (1, child_count);		\
			LOCK (&hafdp->lock);				\
			memcpy (local->state, hafdp->fdstate, child_count); \
			UNLOCK (&hafdp->lock);				\
			if (local->active != -1 && local->state[local->active] == 0) \
				local->active = -1;			\
			for (i = 0; i < pvt->child_count; i++) {	\
				if (local->state[i]) {			\
					if (local->active == -1)	\
						local->active = i;	\
					local->tries++;			\
				}					\
			}						\
		}							\
	} while(0);

#define HA_CALL_CBK_CODE_FD(this, cookie, op_ret, op_errno, local, hafdp) do { \
		int i;							\
		ha_private_t *pvt = this->private;			\
		xlator_t **children = pvt->children;			\
		call_frame_t *prev_frame = cookie;			\
		GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%s)",	\
			  prev_frame->this->name, op_ret, strerror (op_errno)); \
		if (op_ret == -1) {					\
			GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%s)", \
				  prev_frame->this->name, op_ret, strerror (op_errno)); \
		}							\
		if (op_ret == -1 && (op_errno == ENOTCONN)) {		\
			while (1) {					\
				for (i = 0; i < pvt->child_count; i++){ \
					if (prev_frame->this == children[i]) \
						break;			\
				}					\
				LOCK(&hafdp->lock);			\
				hafdp->fdstate[i] = 0;			\
				UNLOCK(&hafdp->lock);			\
				local->active = (local->active + 1) % pvt->child_count;	\
				local->tries--;				\
				if (local->tries == 0)			\
					break;				\
				if (local->state[local->active])	\
					break;				\
			}						\
			if (local->tries != 0) {			\
				call_resume (local->stub);		\
				return 0;				\
			}						\
		}							\
		FREE (local->state);					\
		call_stub_destroy (local->stub);			\
	} while(0);


#define HA_CALL_CODE(frame, local, state) do {				\
		if (local == NULL) {					\
			int i;						\
			ha_private_t *pvt = this->private;		\
			local = frame->local = calloc (1, sizeof (*local)); \
			local->active = pvt->active;			\
			local->state = state;				\
			if (local->active != -1 && local->state[local->active] == 0) \
				local->active = -1;			\
			for (i = 0; i < pvt->child_count; i++) {	\
				if (local->state[i]) {			\
					if (local->active == -1)	\
						local->active = i;	\
					local->tries++;			\
				}					\
			}						\
		}							\
	} while(0);

#define HA_CALL_CBK_CODE(this, cookie, local, op_ret, op_errno) do {	\
		call_frame_t *prev_frame = cookie;			\
		ha_private_t *pvt = this->private;			\
		GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%s)",	\
			  prev_frame->this->name, op_ret, strerror (op_errno));	\
		if (op_ret == -1) {					\
			GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%s)", \
				  prev_frame->this->name, op_ret, strerror (op_errno));	\
		}							\
		if (op_ret == -1 && (op_errno == ENOTCONN)) {		\
			while (1) {					\
				local->active = (local->active + 1) % pvt->child_count;	\
				local->tries--;				\
				if (local->tries == 0)			\
					break;				\
				if (local->state[local->active])	\
					break;				\
			}						\
			if (local->tries != 0) {			\
				call_resume (local->stub);		\
				return 0;				\
			}						\
		}							\
		call_stub_destroy (local->stub);			\
	} while(0);

#endif
