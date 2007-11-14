/*
   Copyright (c) 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#ifndef _AFR_H
#define _AFR_H

#include "scheduler.h"
#include "call-stub.h"

typedef struct _afrfd {
  char *fdstate;
  char *fdsuccess;
  int32_t write;
  int32_t create;
  int32_t rchild;
  char *path;
} afrfd_t;

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
  dir_entry_t *entry;
} afr_selfheal_t;

typedef struct _afr_local {
  int32_t call_count;
  int32_t op_ret;
  int32_t op_errno;
  int32_t size;
  int32_t flags;
  int32_t latest;
  int32_t stat_child;
  int32_t rmelem_status;
  uid_t uid, gid;
  ino_t ino;
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
  afr_selfheal_t *source, *ashptr;
  struct stat *statptr;
  int32_t shcalled;
  call_frame_t *orig_frame;
  loc_t *loc, *loc2;
  dir_entry_t *entry, *last;;
  int32_t count;
  xlator_t *lock_node;
  int32_t sh_return_error;
  afrfd_t *afrfdp;
} afr_local_t;

typedef struct _afr_statfs_local {
  struct statvfs statvfs;
  int32_t op_ret, op_errno;
  int32_t call_count;
  int32_t stat_child;
} afr_statfs_local_t;

typedef struct _pattern_info {
  char *pattern;
  int copies;
} pattern_info_t;

typedef struct _afr_private {
  xlator_t *lock_node;
  int32_t child_count;
  int32_t debug;
  int32_t self_heal;
  int32_t read_schedule;
  xlator_t **children;
  char *state;
} afr_private_t;

#define AFR_VERSION "trusted.afr.version"
#define AFR_CREATETIME "trusted.afr.createtime"

#endif
