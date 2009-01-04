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

#ifndef __MAP_H__
#define __MAP_H__

#include "xlator.h"

struct map_pattern {
	struct map_pattern *next;
	xlator_t           *xl;
	char               *directory;
	int                 dir_len;
};

struct map_xlator_array {
	xlator_t *xl;
	int       mapped; /* yes/no */
};

typedef struct {
	struct map_pattern      *map;
	xlator_t                *default_xl;
	struct map_xlator_array *xlarray;
	int                      child_count;
} map_private_t;

typedef struct {
	int32_t        op_ret;
	int32_t        op_errno;
	int            call_count;
	struct statvfs statvfs;
	struct stat    stbuf;
	inode_t       *inode;
	dict_t        *dict;
	fd_t          *fd;

	gf_dirent_t   entries;
	int           count;
} map_local_t;

#endif /* __MAP_H__ */
