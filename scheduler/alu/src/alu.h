/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License aint64_t with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef _ALU_H
#define _ALU_H

#include "scheduler.h"

struct alu_sched;

struct alu_sched_struct {
  struct xlator *xl;
  struct xlator_stats stats;
  unsigned char eligible;
};

// Write better name for these functions
struct alu_limits {
  struct alu_limits *next;
  int64_t (*max_value) (struct xlator_stats *); /* Max limit, specified by the user */
  int64_t (*min_value) (struct xlator_stats *); /* Min limit, specified by the user */
  int64_t (*cur_value) (struct xlator_stats *); /* Current values of variables got from stats call */
};

struct alu_threshold {
  struct alu_threshold *next;
  int64_t (*diff_value) (struct xlator_stats *max, struct xlator_stats *min); /* Diff b/w max and min */
  int64_t (*entry_value) (struct xlator_stats *); /* Limit specified user */
  int64_t (*exit_value) (struct xlator_stats *); /* Exit point32_t for the limit */
  int64_t (*sched_value) (struct xlator_stats *); /* This will return the index of the child area */
};

struct alu_sched_node {
  struct alu_sched_node *next;
  int32_t index;
};

struct alu_sched {
  struct alu_limits *limits_fn;
  struct alu_threshold *threshold_fn;
  struct alu_sched_struct *array;
  struct alu_sched_node *sched_node;
  struct alu_threshold *sched_method;
  struct xlator_stats max_limit;
  struct xlator_stats min_limit;
  struct xlator_stats entry_limit;
  struct xlator_stats exit_limit;
  struct xlator_stats spec_limit;     /* User given limit */
  
  struct timeval last_stat_fetch;
  int32_t refresh_interval;      /* in seconds */
  int32_t refresh_create_count;  /* num-file-create */

  int32_t sched_nodes_pending;
  int32_t child_count;
};

#endif /* _ALU_H */
