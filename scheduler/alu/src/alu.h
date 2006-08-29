#ifndef _ALU_H
#define _ALU_H

#include "scheduler.h"

struct alu_sched_struct {
  struct xlator *xl;
  struct xlator_stats stats;
  unsigned char eligible;
};

// Write better name for these functions
struct alu_limits {
  struct alu_limits *next;
  int (*max_limit) (void *); /* Max limit, specified by the user */
  int (*cur_value) (void *); /* Current values of variables got from stats call */
};

struct alu_priority {
  struct alu_priority *next;
  int (*diff_limit) (void *); /* Limit specified user */
  int (*diff_value) (void *); /* Diff b/w max and min */
};

struct alu_sched {
  struct alu_limits *limits;
  struct alu_priority *priority;
  struct alu_sched_struct *array;
  struct xlator_stats max_limit;      /* User given limit */
  struct xlator_stats max_difference; /* User given priority value */
  int child_count;
};

#endif /* _ALU_H */
