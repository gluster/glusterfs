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
  int (*max_value) (struct xlator_stats *); /* Max limit, specified by the user */
  int (*cur_value) (struct xlator_stats *); /* Current values of variables got from stats call */
};

struct alu_threshold {
  struct alu_threshold *next;
  int (*diff_value) (struct xlator_stats *max, struct xlator_stats *min); /* Diff b/w max and min */
  int (*entry_value) (struct xlator_stats *); /* Limit specified user */
  int (*exit_value) (struct xlator_stats *); /* Exit point for the limit */
  int (*sched_value) (struct xlator_stats *); /* This will return the index of the child area */
};

struct alu_sched_node {
  struct alu_sched_node *next;
  int index;
};

struct alu_sched {
  struct alu_limits *limits_fn;
  struct alu_threshold *threshold_fn;
  struct alu_sched_struct *array;
  struct alu_sched_node *sched_node;
  struct xlator_stats max_limit;
  struct xlator_stats min_limit;
  struct xlator_stats entry_limit;
  struct xlator_stats exit_limit;
  struct xlator_stats spec_limit;     /* User given limit */
  int sched_nodes_pending;
  int child_count;
};

#endif /* _ALU_H */
