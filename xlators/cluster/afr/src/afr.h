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

typedef struct _afr_local {
  int32_t call_count;
  int32_t op_ret;
  int32_t op_errno;
  int32_t size;
  off_t offset;
  const char *path, *name;
  dict_t *ctx;
  xlator_list_t *xlnodeptr;
  struct timespec *tspec;
  struct stat stbuf;
} afr_local_t;

typedef struct _afr_private {
  xlator_t *afr_node;
  int32_t child_count;
} afr_private_t;

typedef struct pattern_info {
  char *pattern;
  int copies;
} pattern_info_t;

#endif
