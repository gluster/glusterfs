/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _GUTS_TABLES_H_
#define _GUTS_TABLES_H_


int32_t
guts_attr_cmp (const struct stat *attr,
	       const struct stat *old_attr);

int32_t
guts_statvfs_cmp (const struct statvfs *stbuf,
		  const struct statvfs *old_stbuf);

int32_t
guts_inode_update (guts_replay_ctx_t *ctx,
		   fuse_ino_t old_ino,
		   fuse_ino_t new_ino);

fuse_ino_t
guts_inode_search (guts_replay_ctx_t *ctx,
		   fuse_ino_t old_ino);

int32_t 
guts_add_request (guts_replay_ctx_t *,
		  guts_req_t *);

guts_req_t *
guts_get_request (guts_replay_ctx_t *ctx);

guts_req_t *
guts_lookup_request (guts_replay_ctx_t *ctx,
		     uint64_t unique);

guts_reply_t *
guts_lookup_reply (guts_replay_ctx_t *ctx,
		   uint64_t unique);

int32_t
guts_add_reply (guts_replay_ctx_t *ctx,
		guts_reply_t *reply);

int32_t
guts_flock_cmp (struct flock *lock,
		struct flock *old_lock);

fd_t *
guts_fd_search (guts_replay_ctx_t *ctx,
		unsigned long old_fd);

int32_t
guts_delete_fd (guts_replay_ctx_t *,
		unsigned long);

int32_t 
guts_get_opcode (guts_replay_ctx_t *ctx,
		 uint64_t unique);
int32_t
guts_fd_add (guts_replay_ctx_t *ctx,
	     unsigned long old_fd,
	     fd_t *new_fd);

#endif
