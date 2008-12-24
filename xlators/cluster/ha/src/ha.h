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

#ifndef __HA_H__
#define __HA_H__

typedef struct {
	int32_t      op_ret; 
	int32_t      op_errno;
	int32_t      call_count;
	struct stat  stbuf;

	union {
		/* lookup */
		struct {
			loc_t loc;
			int32_t need_xattr;
		} lookup;

		/* stat */
		struct {
			loc_t loc;
		} stat;

		/* fstat */
		struct {
			fd_t *fd;
		} fstat;

		/* chmod */
		struct {
			loc_t loc;
			mode_t mode;
		} chmod;

		/* fchmod */
		struct {
			fd_t *fd;
			mode_t mode;
		} fchmod;

		/* chown */
		struct {
			loc_t loc;
			uid_t uid;
			gid_t gid;
		} chown;

		/* fchown */
		struct {
			fd_t *fd;
			uid_t uid;
			gid_t gid;
		} fchown;

		/* truncate */
		struct {
			loc_t loc;
			off_t off;
		} truncate;

		/* ftruncate */
		struct {
			fd_t *fd;
			off_t off;
		} ftruncate;

		/* utimens */
		struct {
			loc_t loc;
			struct timespec tv[2];
		} utimens;

		/* access */
		struct {
			loc_t loc;
			int32_t mask;
		} access;

		/* readlink */
		struct {
			loc_t loc;
			size_t size;
		} readlink;

		/* mknod */
		struct {
			loc_t loc;
			mode_t mode;
			dev_t rdev;
		} mknod;

		/* mkdir */
		struct {
			loc_t loc;
			mode_t mode;
		} mkdir;

		/* unlink */
		struct {
			loc_t loc;
		} unlink;

		/* rmdir */
		struct {
			loc_t loc;
		} rmdir;

		/* symlink */
		struct {
			const char *linkname;
			loc_t loc;
		} symlink;

		/* rename */
		struct {
			loc_t old;
			loc_t new;
		} rename;

		/* link */
		struct {
			loc_t oldloc;
			loc_t newloc;
		} link;

		/* create */
		struct {
			loc_t loc;
			int32_t flags;
			mode_t mode;
			fd_t *fd;
		} create;

		/* open */
		struct {
			loc_t loc;
			int32_t flags;
			fd_t *fd;
		} open;

		/* readv */
		struct {
			fd_t *fd;
			size_t size;
			off_t off;
		} readv;

		/* writev */
		struct {
			fd_t *fd;
			struct iovec *vector;
			int32_t count;
			off_t off;
			dict_t *req_refs;
		} writev;

		/* flush */
		struct {
			fd_t *fd;
		} flush;

		/* fsync */
		struct {
			fd_t *fd;
			int32_t datasync;
		} fsync;

		/* opendir */
		struct {
			loc_t loc;
			fd_t *fd;
		} opendir;

		/* getdents */
		struct {
			fd_t *fd;
			size_t size;
			off_t off;
			int32_t flag;
		} getdents;

		/* setdents */
		struct {
			fd_t *fd;
			int32_t flags;
			dir_entry_t entries;
			int32_t count;
		} setdents;

		/* fsyncdir */
		struct {
			fd_t *fd;
			int32_t datasync;
		} fsyncdir;

		/* statfs */
		struct {
			loc_t loc;
		} statfs;

		/* setxattr */
		struct {
			loc_t loc;
			dict_t *dict;
			int32_t flags;
		} setxattr;

		/* getxattr */
		struct {
			loc_t loc;
			const char *name;
		} getxattr;

		/* removexattr */
		struct {
			loc_t loc;
			const char *name;
		} removexattr;

		/* lk */
		struct {
			fd_t *fd;
			int32_t cmd;
			struct flock lock;
		} lk;

		/* inodelk */
		struct {
			loc_t loc;
			int32_t cmd;
			struct flock lock;
		} inodelk;

		/* finodelk */
		struct {
			fd_t *fd;
			int32_t cmd;
			struct flock lock;
		} finodelk;

		/* entrylk */
		struct {
			loc_t loc;
			const char *name;
			entrylk_cmd cmd;
			entrylk_type type;
		} entrylk;

		/* fentrylk */
		struct {
			fd_t *fd;
			const char *name;
			entrylk_cmd cmd;
			entrylk_type type;
		} fentrylk;

		/* readdir */
		struct {
			fd_t *fd;
			size_t size;
			off_t off;
		} readdir;

		/* checksum */
		struct {
			loc_t loc;
			int32_t flags;
		} checksum;

		/* xattrop */
		struct {
			loc_t loc;
			gf_xattrop_flags_t optype;
			dict_t *xattr;
		} xattrop;

		/* fxattrop */
		struct {
			fd_t *fd;
			gf_xattrop_flags_t optype;
			dict_t *xattr;
		} fxattrop;
		
		struct {
			int32_t flags;
		} stats;
		
		struct {
			const char *key;
			int32_t flags;
		} getspec;
	} args;
} ha_local_t;

typedef struct {
	char      *state;
	xlator_t **children;
	int32_t    child_count;
	int32_t    active;
	gf_lock_t  lock;
} ha_private_t;

#define HA_CHILDREN(_this) (((ha_private_t *)_this->private)->children)

#define HA_CHILDREN_COUNT(_this) (((ha_private_t *)_this->private)->child_count)

#define HA_NOT_TRANSPORT_ERROR(_op_ret, _op_errno) \
	((op_ret == -1) && (!((_op_errno == ENOTCONN) || (_op_errno == EBADFD))))

#define HA_NONE (-1)

ha_local_t *
ha_local_init (call_frame_t *frame);

int32_t
ha_copy_state_to_fd (xlator_t *this,
		     fd_t *fd,
		     inode_t *inode);

int32_t
ha_first_active_child_index (xlator_t *this);

int32_t
ha_mark_child_down_for_inode (xlator_t *this,
			      inode_t *inode,
			      int32_t child_idx);

xlator_t *
ha_next_active_child_for_inode (xlator_t *this,
				inode_t *inode,
				int32_t child_idx,
				int32_t *ret_active_idx);

xlator_t *
ha_next_active_child_for_fd (xlator_t *this,
			     fd_t *fd,
			     int32_t child_idx,
			     int32_t *ret_active_idx);

int32_t
ha_set_state (dict_t *ctx, xlator_t *this);

int32_t
ha_next_active_child_index (xlator_t *this, 
			    int32_t discard);

xlator_t *
ha_child_for_index (xlator_t *this, int32_t idx);


#endif /* __HA_H__ */
