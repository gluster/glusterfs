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

struct rr_subvolume
{
  xlator_t  *xl;
  uint8_t   free_disk_status;
  uint8_t   status;
};
typedef struct rr_subvolume rr_subvolume_t;

struct rr
{
  rr_options_t    options;
  rr_subvolume_t  *subvolume_list;
  uint64_t        subvolume_count;
  uint64_t        schedule_index;
  struct timeval  last_stat_fetched_time;
  pthread_mutex_t mutex;
  char            first_time;
};
typedef struct rr rr_t;

int rr_init (xlator_t *this_xl);
void rr_fini (xlator_t *this_xl);
xlator_t *rr_schedule (xlator_t *this_xl, void *path);
void rr_update (xlator_t *this_xl);
int rr_update_cbk (call_frame_t *frame, 
		   void *cookie, 
		   xlator_t *this_xl, 
		   int32_t op_ret, 
		   int32_t op_errno, 
		   struct xlator_stats *stats);
void rr_notify (xlator_t *this_xl, int32_t event, void *data);
int rr_notify_cbk (call_frame_t *frame, 
		   void *cookie, 
		   xlator_t *this_xl, 
		   int32_t op_ret, 
		   int32_t op_errno);

#endif /* _RR_H */
