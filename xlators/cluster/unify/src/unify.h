/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _UNIFY_H
#define _UNIFY_H

#include "scheduler.h"
#include "list.h"

#define MAX_DIR_ENTRY_STRING     (32 * 1024)

#define ZR_UNIFY_SELF_HEAL_OFF 0
#define ZR_UNIFY_FG_SELF_HEAL  1
#define ZR_UNIFY_BG_SELF_HEAL  2

/* Sometimes one should use completely random numbers.. its good :p */
#define UNIFY_SELF_HEAL_GETDENTS_COUNT 2345 

#define NS(xl)          (((unify_private_t *)xl->private)->namespace)

/* This is used to allocate memory for local structure */
#define INIT_LOCAL(fr, loc)                   \
do {                                          \
  loc = calloc (1, sizeof (unify_local_t));   \
  ERR_ABORT (loc);			      \
  if (!loc) {                                 \
    STACK_UNWIND (fr, -1, ENOMEM);            \
    return 0;                                 \
  }                                           \
  fr->local = loc;                            \
  loc->op_ret = -1;                           \
  loc->op_errno = ENOENT;                     \
} while (0)



struct unify_private {
	/* Update this structure depending on requirement */
	void *scheduler;               /* THIS SHOULD BE THE FIRST VARIABLE, 
					  if xlator is using scheduler */
	struct sched_ops *sched_ops;   /* Scheduler options  */
	xlator_t *namespace;           /* ptr to namespace xlator */
	xlator_t **xl_array;
	gf_boolean_t optimist;
	int16_t child_count;
	int16_t num_child_up;
	uint8_t self_heal;
	uint8_t is_up;
	uint64_t inode_generation;
	gf_lock_t lock;
};
typedef struct unify_private unify_private_t;

struct unify_self_heal_struct {
	uint8_t dir_checksum[ZR_FILENAME_MAX];
	uint8_t ns_dir_checksum[ZR_FILENAME_MAX];
	uint8_t file_checksum[ZR_FILENAME_MAX];
	uint8_t ns_file_checksum[ZR_FILENAME_MAX];
	off_t *offset_list;
	int   *count_list;
	dir_entry_t **entry_list;
};


struct _unify_local_t {
	int32_t call_count;
	int32_t op_ret;
	int32_t op_errno;
	mode_t mode;
	off_t offset;
	dev_t dev;
	uid_t uid;
	gid_t gid;
	int32_t flags;
	int32_t entry_count;
	int32_t count;    // dir_entry_t count;
	fd_t *fd;
	struct stat stbuf;
	struct statvfs statvfs_buf;
	struct timespec tv[2];
	char *name;
	int32_t revalidate;

	ino_t st_ino;
	nlink_t st_nlink;
  
	dict_t *dict;

	int16_t *list;
	int16_t *new_list; /* Used only in case of rename */
	int16_t index;

	int32_t failed;
	int32_t return_eio;  /* Used in case of different st-mode 
				present for a given path */

	int64_t inode_generation; /* used to store the per directory 
				   * inode_generation. Got from inode->ctx 
				   * of directory inodes
				   */

	struct unify_self_heal_struct *sh_struct;
	loc_t loc1, loc2;
};
typedef struct _unify_local_t unify_local_t;

int32_t zr_unify_self_heal (call_frame_t *frame,
			    xlator_t *this,
			    unify_local_t *local);

#endif /* _UNIFY_H */
