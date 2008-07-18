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

#ifndef _NUFA_H
#define _NUFA_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "scheduler.h"
#include "common-utils.h"

struct nufa_sched_struct {
  xlator_t *xl;
  struct timeval last_stat_fetch;
  int64_t free_disk;
  int32_t refresh_interval;
  unsigned char eligible;
};

struct nufa_struct {
  struct nufa_sched_struct *array;
  struct timeval last_stat_fetch;

  int32_t *local_array; /* Used to keep the index of the local xlators */
  int32_t local_xl_index; /* index in the above array */
  int32_t local_xl_count; /* Count of the local subvolumes */

  uint32_t refresh_interval;
  uint64_t min_free_disk;
  
  gf_lock_t nufa_lock;
  int32_t child_count;
  int32_t sched_index;  
};

#endif /* _NUFA_H */
