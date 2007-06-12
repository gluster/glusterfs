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

typedef struct _afr_local {
  int32_t call_count;
  int32_t op_ret;
  int32_t op_errno;
  int32_t size;
  int32_t flags;
  off_t offset;
  const char *path, *name;
  inode_t *inode;
  fd_t *fd;
  struct list_head *list;
  dict_t *ctx;
  xlator_list_t *xlnodeptr;
  struct timespec *tspec;
  struct stat stbuf;
  struct flock lock;
  call_stub_t *stub;
  gf_inode_child_t *latest;
  int32_t shcalled;
  call_frame_t *orig_frame;
} afr_local_t;

typedef struct _pattern_info {
  char *pattern;
  int copies;
} pattern_info_t;

typedef struct _afr_private {
  xlator_t *lock_node;
  int32_t child_count;
  int32_t pil_num;
  int32_t debug;
  pattern_info_t *pattern_info_list;
} afr_private_t;

#endif
