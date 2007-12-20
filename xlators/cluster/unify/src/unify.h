/*
   Copyright (c) 2006 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _UNIFY_H
#define _UNIFY_H

#include "scheduler.h"
#include "list.h"

#define MAX_DIR_ENTRY_STRING     (32 * 1024)

#define NS(xl)          (((unify_private_t *)xl->private)->namespace)


/* This is used to allocate memory for local structure */
#define INIT_LOCAL(fr, loc)                   \
do {                                          \
  loc = calloc (1, sizeof (unify_local_t));   \
  if (!loc) {                                 \
    STACK_UNWIND (fr, -1, ENOMEM);            \
    return 0;                                 \
  }                                           \
  fr->local = loc;                            \
  loc->op_ret = -1;                           \
  loc->op_errno = ENOENT;                     \
} while (0)



struct unify_openfd_list {
  struct unify_openfd_list *next;
  xlator_t *xl;
};
typedef struct unify_openfd_list unify_openfd_t;

struct unify_private {
  /* Update this structure depending on requirement */
  void *scheduler;               /* THIS SHOULD BE THE FIRST VARIABLE, 
				    if xlator is using scheduler */
  struct sched_ops *sched_ops;   /* Scheduler options  */
  xlator_t *namespace;           /* ptr to namespace xlator */
  xlator_t **xl_array;
  int16_t child_count;
  int16_t num_child_up;
  int16_t self_heal;
  uint64_t inode_generation;
  uint8_t is_up;
  gf_lock_t lock;
};
typedef struct unify_private unify_private_t;

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
  dir_entry_t *ns_entry;   /* Namespace entries */
  dir_entry_t *entry;
  dir_entry_t *last;
  int32_t count;    // dir_entry_t count;
  int32_t ns_count;    // dir_entry_t count for namespace entry;
  fd_t *fd;
  struct stat stbuf;
  struct statvfs statvfs_buf;
  struct timespec tv[2];
  char *path;
  char *name;
  inode_t *inode;
  inode_t *new_inode; /* Only used in case of rename */
  int32_t revalidate;

  off_t st_size;
  time_t mtime;
  blkcnt_t st_blocks;
  nlink_t st_nlink;
  
  dict_t *dict;

  int16_t *list;
  int16_t index;

  unify_openfd_t *openfd;
  int32_t failed;

  uint8_t dir_checksum[4096];
  uint8_t ns_dir_checksum[4096];
  uint8_t file_checksum[4096];
  uint8_t ns_file_checksum[4096];
};
typedef struct _unify_local_t unify_local_t;

int32_t unify_getdents_self_heal (call_frame_t *frame,
				 xlator_t *this,
				 fd_t *fd,
				 unify_local_t *local);

int32_t gf_unify_self_heal (call_frame_t *frame,
			    xlator_t *this,
			    unify_local_t *local);

#endif /* _UNIFY_H */
