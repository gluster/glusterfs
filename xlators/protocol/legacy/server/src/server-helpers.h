/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __SERVER_HELPERS_H__
#define __SERVER_HELPERS_H__

#define CALL_STATE(frame)   ((server_state_t *)frame->root->state)

#define BOUND_XL(frame)     ((xlator_t *) CALL_STATE(frame)->bound_xl)

#define TRANSPORT_FROM_FRAME(frame) ((transport_t *) CALL_STATE(frame)->trans)

#define SERVER_CONNECTION(frame)  \
	((server_connection_t *) TRANSPORT_FROM_FRAME(frame)->xl_private)

#define SERVER_CONF(frame) \
	((server_conf_t *)TRANSPORT_FROM_FRAME(frame)->xl->private)

#define TRANSPORT_FROM_XLATOR(this) ((((server_conf_t *)this->private))->trans)

#define INODE_LRU_LIMIT(this)						\
	(((server_conf_t *)(this->private))->inode_lru_limit)

#define IS_ROOT_INODE(inode) (inode == inode->table->root)

#define IS_NOT_ROOT(pathlen) ((pathlen > 2)? 1 : 0)

void free_old_server_state (server_state_t *state);

void old_server_loc_wipe (loc_t *loc);

#endif /* __SERVER_HELPERS_H__ */
