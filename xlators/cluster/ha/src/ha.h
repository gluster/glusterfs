/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
	loc_t loc;
	struct stat buf;
	fd_t *fd;
	inode_t *inode;
	int32_t flags;
	int32_t first_success;
} ha_local_t;

typedef struct {
	char *state;
	xlator_t **children;
	int child_count, pref_subvol;
} ha_private_t;

typedef struct {
	char *fdstate;
	char *path;
	gf_lock_t lock;
	int active;
} hafd_t;

#define HA_ACTIVE_CHILD(this, local) (((ha_private_t *)this->private)->children[local->active])

extern int ha_alloc_init_fd (call_frame_t *frame, fd_t *fd);

extern int ha_handle_cbk (call_frame_t *frame, void *cookie, int op_ret, int op_errno) ;

extern int ha_alloc_init_inode (call_frame_t *frame, inode_t *inode);

#endif
