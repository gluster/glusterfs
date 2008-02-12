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

#ifndef _RR_H
#define _RR_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "scheduler.h"
#include <stdint.h>
#include <sys/time.h>

struct rr_sched_struct {
  xlator_t *xl;
  struct timeval last_stat_fetch;
  int64_t free_disk;
  int32_t refresh_interval;
  unsigned char eligible;
};

struct rr_struct {
  struct rr_sched_struct *array;
  struct timeval last_stat_fetch;
  int32_t refresh_interval;
  int64_t min_free_disk;
  char first_time;

  pthread_mutex_t rr_mutex;
  int32_t child_count;
  int32_t sched_index;  
};

#endif /* _RR_H */
