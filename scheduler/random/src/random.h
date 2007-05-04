/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef _RANDOM_H
#define _RANDOM_H

#include <sys/time.h>
#include "scheduler.h"

struct random_sched_struct {
  struct xlator *xl;
  unsigned char eligible;
};

struct random_struct {
  int32_t child_count;
  int32_t refresh_interval;
  int64_t min_free_disk;
  struct timeval last_stat_entry;
  struct random_sched_struct *array;
  pthread_mutex_t random_mutex;
};

#endif /* _RANDOM_H */
