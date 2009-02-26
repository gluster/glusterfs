/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

	size_t        size;
} map_local_t;

xlator_t *map_subvol_next (xlator_t *this, xlator_t *prev);
int map_subvol_cnt (xlator_t *this, xlator_t *subvol);

int map_itransform (xlator_t *this, xlator_t *subvol, 
		    uint64_t x, uint64_t *y_p);
int map_deitransform (xlator_t *this, uint64_t y, 
		      xlator_t **subvol_p, uint64_t *x_p);


xlator_t *get_mapping_subvol_from_path (xlator_t *this, const char *path);
xlator_t *get_mapping_subvol_from_ctx (xlator_t *this, inode_t *inode);

int check_multiple_volume_entry (xlator_t *this, xlator_t *subvol);
int verify_dir_and_assign_subvol (xlator_t *this, 
				  const char *directory, const char *subvol);
int assign_default_subvol (xlator_t *this, const char *default_xl);
void verify_if_all_subvolumes_got_used (xlator_t *this);


#endif /* __MAP_H__ */
