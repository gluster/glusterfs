/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _RANDOM_H
#define _RANDOM_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include <sys/time.h>
#include "scheduler.h"

struct random_sched_struct {
  xlator_t *xl;
  unsigned char eligible;
};

struct random_struct {
  int32_t child_count;
  uint32_t refresh_interval;
  uint32_t min_free_disk;
  struct timeval last_stat_entry;
  struct random_sched_struct *array;
  pthread_mutex_t random_mutex;
};

#endif /* _RANDOM_H */
