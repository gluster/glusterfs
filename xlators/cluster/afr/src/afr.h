/*
 * (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */


#ifndef _AFR_H
#define _AFR_H

#include "scheduler.h"
#include "call-stub.h"

typedef struct gf_inode_child_ {
  struct list_head clist;
  xlator_t *xl;
  inode_t *inode;
  int32_t op_errno;
  struct stat stat;
  int32_t repair;
} gf_inode_child_t;

typedef struct _afr_selfheal {
  struct list_head clist;
  xlator_t *xl;
  inode_t *inode;
  struct stat stat;
  int32_t repair;
  uint32_t version;
  uint32_t ctime;
  int32_t op_errno;
  dict_t *dict;
} afr_selfheal_t;

typedef struct _afr_local {
  int32_t call_count;
  int32_t op_ret;
  int32_t op_errno;
  int32_t size;
  int32_t flags;
  uid_t uid, gid;
  off_t offset;
  char *path, *name;
  inode_t *inode;
  fd_t *fd;
  struct list_head *list;
  dict_t *ctx;
  xlator_list_t *xlnodeptr;
  struct timespec *tspec;
  struct stat stbuf;
  struct flock lock;
  call_stub_t *stub;
  afr_selfheal_t *source;
  int32_t shcalled;
  call_frame_t *orig_frame;
  loc_t *loc;
  dir_entry_t *entry, *last;;
  int32_t count;
  xlator_t *lock_node;
  int32_t sh_return_error;
} afr_local_t;

typedef struct _pattern_info {
  char *pattern;
  int copies;
} pattern_info_t;

typedef struct afr_child_state {
  struct list_head clist;
  int32_t state;
  xlator_t *xl;
} afr_child_state_t;

typedef struct _afr_private {
  xlator_t *lock_node;
  int32_t child_count;
  int32_t pil_num;
  int32_t debug;
  pattern_info_t *pattern_info_list;
  int32_t self_heal;
  struct list_head *children;
} afr_private_t;

typedef struct _afr_inode_private {
  struct list_head *giclist;
} afr_inode_private_t;

#endif
