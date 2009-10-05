/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _RR_OPTIONS_H
#define _RR_OPTIONS_H

struct rr_options
{
  uint32_t min_free_disk;
  uint32_t refresh_interval;
  char     **read_only_subvolume_list;
  uint64_t read_only_subvolume_count;
};
typedef struct rr_options rr_options_t;

int rr_options_validate (dict_t *options, rr_options_t *rr_options);

#endif
