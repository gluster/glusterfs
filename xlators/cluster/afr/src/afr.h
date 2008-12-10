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


#ifndef __AFR_H__
#define __AFR_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "scheduler.h"
#include "call-stub.h"
#include "compat-errno.h"


#define VOID(ptr) ((void **)((void *)ptr))

typedef struct _afr_private {
	gf_lock_t lock;               /* to guard access to child_count, etc */
	unsigned int child_count;     /* total number of children   */

	xlator_t **children;

	unsigned char *child_up;

	unsigned int data_self_heal;       /* on/off */
	unsigned int metadata_self_heal;   /* on/off */
	unsigned int entry_self_heal;      /* on/off */


	unsigned int data_change_log;       /* on/off */
	unsigned int metadata_change_log;   /* on/off */
	unsigned int entry_change_log;      /* on/off */

	unsigned int read_child;      /* read-subvolume */
	unsigned int favorite_child;  /* subvolume to be preferred in resolving
					 split-brain cases */

	unsigned int data_lock_server_count;
	unsigned int metadata_lock_server_count;
	unsigned int entry_lock_server_count;

	unsigned int wait_count;      /* # of servers to wait for success */
} afr_private_t;

typedef struct {
	/* array of stat's, one for each child */
	struct stat *buf;

	/* array of xattr's, one for each child */
	dict_t **xattr;

	/* array of errno's, one for each child */
	int *child_errno;

	int32_t **pending_matrix;
	int32_t **delta_matrix;

	int *sources;
	int source;
	int active_source;
	int active_sinks;
	int *success;

	fd_t *healing_fd;
	int   op_failed;

	blksize_t block_size;
	off_t file_size;
	off_t offset;

	loc_t parent_loc;
	int (*completion_cbk) (call_frame_t *frame, xlator_t *this);
	call_frame_t *sh_frame;
} afr_self_heal_t;


typedef enum {
	AFR_DATA_TRANSACTION,          /* truncate, write, ... */
	AFR_METADATA_TRANSACTION,      /* chmod, chown, ... */
	AFR_ENTRY_TRANSACTION,         /* create, rmdir, ... */
	AFR_ENTRY_RENAME_TRANSACTION,  /* rename */
} afr_transaction_type;

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

	int32_t *pending_array;

	loc_t loc;
	loc_t newloc;

	fd_t *fd;

	glusterfs_fop_t fop;

	unsigned char *child_up; 
	int            child_count;

	int32_t *child_errno;

	/* 
	   This struct contains the arguments for the "continuation"
	   (scheme-like) of fops
	*/
	   
	struct {
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
			ino_t ino;
		} stat;

		struct {
			int last_tried;
			ino_t ino;
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
			mode_t mode;
			struct stat buf;
		} fchmod;

		struct {
			ino_t ino;
			uid_t uid;
			gid_t gid;
			struct stat buf;
		} chown;

		struct {
			ino_t ino;
			uid_t uid;
			gid_t gid;
			struct stat buf;
		} fchown;
		
		struct {
			ino_t ino;
			struct stat buf;

			int32_t op_ret;

			struct iovec *vector;
			dict_t *refs;
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
			off_t offset;
			struct stat buf;
		} ftruncate;

		struct {
			ino_t ino;
			struct timespec tv[2];
			struct stat buf;
		} utimens;

		struct {
			dict_t *dict;
			int32_t flags;
		} setxattr;

		struct {
			const char *name;
		} removexattr;

		/* dir write */
		
		struct {
			ino_t ino;
			fd_t *fd;
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
			struct stat buf;
		} rename;

		struct {
			ino_t ino;
			inode_t *inode;
			struct stat buf;
		} link;

		struct {
			ino_t ino;
			inode_t *inode;
			struct stat buf;
			char *linkpath;
		} symlink;

		struct {
			int32_t flags;
			dir_entry_t *entries;
			int32_t count;
		} setdents;
	} cont;
	
	struct {
		off_t start, len;

		unsigned char *locked_nodes;
		int lock_count;

		const char *basename;
		const char *new_basename;

		char *pending;

		loc_t parent_loc;
		loc_t new_parent_loc;

		afr_transaction_type type;

		int success_count;
		int erase_pending;
		int failure_count;

		int last_tried;
		int32_t *child_errno;

		call_frame_t *main_frame;

		int (*fop) (call_frame_t *frame, xlator_t *this);

		int (*done) (call_frame_t *frame, xlator_t *this);

		int (*resume) (call_frame_t *frame, xlator_t *this);

		int (*unwind) (call_frame_t *frame, xlator_t *this);
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
afr_build_parent_loc (loc_t *parent, loc_t *child);

int
afr_up_children_count (int child_count, unsigned char *child_up);

int
afr_first_up_child (afr_private_t *priv);

ino64_t
afr_itransform (ino64_t ino, int child_count, int child_index);

int
afr_deitransform (ino64_t ino, int child_count);

void
afr_local_cleanup (afr_local_t *local, xlator_t *this);

int
afr_frame_return (call_frame_t *frame);

#define AFR_STACK_UNWIND(frame, params ...)		\
	do {						\
		afr_local_t *__local = NULL;		\
		xlator_t    *__this = NULL;		\
		__local = frame->local;			\
		__this = frame->this;			\
		frame->local = NULL;                    \
		STACK_UNWIND (frame, params);		\
		afr_local_cleanup (__local, __this);	\
		free (__local);				\
} while (0);					

#define AFR_STACK_DESTROY(frame)			\
	do {						\
		afr_local_t *__local = NULL;		\
		xlator_t    *__this = NULL;		\
		__local = frame->local;			\
		__this = frame->this;			\
		frame->local = NULL;                    \
		STACK_DESTROY (frame->root);		\
		afr_local_cleanup (__local, __this);	\
		free (__local);				\
} while (0);					

/* allocate and return a string that is the basename of argument */
static inline char * 
AFR_BASENAME (const char *str)						
{
	char *__tmp_str = NULL;				
	char *__basename_str = NULL;			
	__tmp_str = strdup (str);			
	__basename_str = strdup (basename (__tmp_str));	
	FREE (__tmp_str);
	return __basename_str;
}

/* initialize local_t */
static inline int
AFR_LOCAL_INIT (afr_local_t *local, afr_private_t *priv)
{
	local->child_errno = calloc (sizeof (*local->child_errno),
				     priv->child_count);
	if (!local->child_errno) {
		return -ENOMEM;
	}

	local->pending_array = calloc (sizeof (*local->pending_array),
				       priv->child_count);
	if (!local->pending_array) {
		return -ENOMEM;
	}

	local->child_up = calloc (sizeof (*local->child_up),
				  priv->child_count);
	if (!local->child_up) {
		return -ENOMEM;
	}

	memcpy (local->child_up, priv->child_up, 
		sizeof (*local->child_up) * priv->child_count);

	local->transaction.locked_nodes = calloc (sizeof (*local->transaction.locked_nodes),
						  priv->child_count);

	local->transaction.child_errno = calloc (sizeof (*local->transaction.child_errno),
						  priv->child_count);
	local->call_count = afr_up_children_count (priv->child_count, local->child_up);
	if (local->call_count == 0)
		return -ENOTCONN;

	local->transaction.erase_pending = 1;

	local->op_ret = -1;
	local->op_errno = EUCLEAN;

	return 0;
}

#endif /* __AFR_H__ */
