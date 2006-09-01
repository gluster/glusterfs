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
  long long (*max_value) (struct xlator_stats *); /* Max limit, specified by the user */
  long long (*min_value) (struct xlator_stats *); /* Min limit, specified by the user */
  long long (*cur_value) (struct xlator_stats *); /* Current values of variables got from stats call */
};

struct alu_threshold {
  struct alu_threshold *next;
  long long (*diff_value) (struct xlator_stats *max, struct xlator_stats *min); /* Diff b/w max and min */
  long long (*entry_value) (struct xlator_stats *); /* Limit specified user */
  long long (*exit_value) (struct xlator_stats *); /* Exit point for the limit */
  long long (*sched_value) (struct xlator_stats *); /* This will return the index of the child area */
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
  struct alu_threshold *sched_method;
  struct xlator_stats max_limit;
  struct xlator_stats min_limit;
  struct xlator_stats entry_limit;
  struct xlator_stats exit_limit;
  struct xlator_stats spec_limit;     /* User given limit */
  
  struct timeval last_stat_fetch;
  int refresh_interval;      /* in seconds */
  int refresh_create_count;  /* num-file-create */

  int sched_nodes_pending;
  int child_count;
};

#endif /* _ALU_H */
