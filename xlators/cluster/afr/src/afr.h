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


#ifndef __AFR_H__
#define __AFR_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "scheduler.h"
#include "call-stub.h"
#include "compat-errno.h"


typedef struct _afr_private {
	gf_lock_t lock;               /* to guard access to child_count, etc */
	unsigned int child_count;     /* total number of children   */

	xlator_t **children;

	unsigned char *child_up;
	int32_t *pending_inc_array;
	int32_t *pending_dec_array;

	unsigned int read_child;      /* read-subvolume */
	unsigned int favorite_child;  /* subvolume to be preferred in resolving
					 split-brain cases */
} afr_private_t;

typedef struct {
	/* array of stat's, one for each child */
	struct stat *buf;  

	/* array of xattr's, one for each child */
	dict_t **xattr;

	int32_t **pending_matrix;

	int *sources;
	int source;

	fd_t *healing_fd;

	blksize_t block_size;
	off_t file_size;
	off_t offset;

	int (*completion_cbk) (call_frame_t *frame, xlator_t *this);
} afr_self_heal_t;


typedef struct _afr_local {
	unsigned int call_count;
	unsigned int success_count;
	unsigned int enoent_count;

	unsigned int need_metadata_self_heal;
	unsigned int need_entry_self_heal;
	unsigned int need_data_self_heal;
	unsigned int govinda_gOvinda;

	unsigned int reval_child_index;
	int32_t op_ret;
	int32_t op_errno;

	loc_t loc;
	fd_t *fd;

	unsigned char *child_up; 

	/* 
	   This struct contains the arguments for the "continuation"
	   (scheme-like) of fops
	*/
	   
	union {
		struct {
			unsigned char buf_set;
			struct statvfs buf;
		} statfs;

		struct {
			inode_t *inode;
			struct stat buf;
			dict_t *xattr;
		} lookup;

		struct {
			int32_t flags;
		} open;

		struct {
			int32_t cmd;
			struct flock flock;
		} lk;

		/* inode read */

		struct {
			int32_t mask;
			int last_tried;  /* index of the child we tried previously */
		} access;

		struct {
			int last_tried;
		} stat;

		struct {
			int last_tried;
		} fstat;

		struct {
			size_t size;
			int last_tried;
		} readlink;

		struct {
			const char *name;
			int last_tried;
		} getxattr;

		struct {
			size_t size;
			off_t offset;
			int last_tried;
		} readv;

		/* dir read */

		struct {
			int success_count;
			int32_t op_ret;
			int32_t op_errno;
		} opendir;

		struct {
			int32_t op_ret;
			int32_t op_errno;
			size_t size;
			off_t offset;

			int last_tried;
		} readdir;

		struct {
			int32_t op_ret;
			int32_t op_errno;

			size_t size;
			off_t offset;
			int32_t flag;

			int last_tried;
		} getdents;

		/* inode write */

		struct {
			ino_t ino;
			mode_t mode;
			struct stat buf;
		} chmod;

		struct {
			ino_t ino;
			uid_t uid;
			gid_t gid;
			struct stat buf;
		} chown;
		
		struct {
			ino_t ino;
			struct stat buf;

			int32_t op_ret;

			struct iovec *vector;
			int32_t count;
			off_t offset;
		} writev;

		struct {
			ino_t ino;
			off_t offset;
			struct stat buf;
		} truncate;

		struct {
			ino_t ino;
			struct timespec tv[2];
			struct stat buf;
		} utimens;

		/* dir write */
		
		struct {
			ino_t ino;
			int32_t flags;
			mode_t mode;
			inode_t *inode;
			struct stat buf;
		} create;

		struct {
			ino_t ino;
			dev_t dev;
			mode_t mode;
			inode_t *inode;
			struct stat buf;
		} mknod;

		struct {
			ino_t ino;
			int32_t mode;
			inode_t *inode;
			struct stat buf;
		} mkdir;

		struct {
			int32_t op_ret;
			int32_t op_errno;
		} unlink;

		struct {
			int32_t op_ret;
			int32_t op_errno;
		} rmdir;

		struct {
			ino_t ino;
			loc_t oldloc;
			loc_t newloc;
			struct stat buf;
		} rename;

		struct {
			ino_t ino;
			loc_t oldloc;
			loc_t newloc;
			inode_t *inode;
			struct stat buf;
		} link;

		struct {
			ino_t ino;
			inode_t *inode;
			struct stat buf;
		} symlink;

	} cont;
	
	struct {
		off_t start, len;
		const char *basename;
		
		const char *new_basename;

		char *pending;

		loc_t parent_loc;

		enum {AFR_INODE_TRANSACTION,     /* chmod, write, ... */
		      AFR_DIR_TRANSACTION,       /* create, rmdir, ... */
		      AFR_DIR_LINK_TRANSACTION,  /* link, rename */
		} type;

		int success_count;
		int failure_count;

		int last_tried;
		int32_t child_errno[1024];

		int (*fop) (call_frame_t *frame, xlator_t *this);

		int (*success) (call_frame_t *frame, 
				int32_t op_ret, int32_t op_errno);

		int (*error) (call_frame_t *frame, xlator_t *this, 
			      int32_t op_ret, int32_t op_errno);

		int (*resume) (call_frame_t *frame, xlator_t *this);
	} transaction;

	afr_self_heal_t self_heal;
} afr_local_t;

/* try alloc and if it fails, goto label */
#define ALLOC_OR_GOTO(var, type, label) do {			\
		var = calloc (sizeof (type), 1);		\
		if (!var) {					\
			gf_log (this->name, GF_LOG_ERROR,	\
				"out of memory :(");		\
			op_errno = ENOMEM;			\
			goto label;				\
		}						\
	} while (0);


/* did a call fail due to a child failing? */
#define child_went_down(op_ret, op_errno) (((op_ret) < 0) &&	      \
					   ((op_errno == ENOTCONN) || \
					    (op_errno == EBADFD)))

/* have we tried all children? */
#define all_tried(i, count)  ((i) == (count) - 1)

void
loc_wipe (loc_t *loc);
/*
void
loc_copy (loc_t *dest, loc_t *src);
*/
int
up_children_count (int child_count, unsigned char *child_up);

int
first_up_child (afr_private_t *priv);

ino64_t
afr_itransform (ino64_t ino, int child_count, int child_index);

int
afr_deitransform (ino64_t ino, int child_count);

void
afr_local_cleanup (call_frame_t *frame);

#define AFR_STACK_UNWIND(frame, params ...)		\
	do {						\
		afr_local_cleanup (frame);		\
		STACK_UNWIND (frame, params);		\
} while (0);					

#endif /* __AFR_H__ */
