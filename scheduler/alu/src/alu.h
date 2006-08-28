#ifndef _ALU_H
#define _ALU_H

#include "scheduler.h"

struct alu_sched_struct {
  struct xlator *xl;
  struct xlator_stats stats;
  unsigned char eligible;
};

struct alu_limits {
  struct alu_limits *next;
  char *name;
  int limit;
};

struct alu_priority {
  struct alu_priority *next;
  char *name;
  int diff_value;
};

struct alu_sched {
  struct alu_limits *limits;
  struct alu_priority *priority;
  struct alu_sched_struct *array;
  int client_count;
};

#endif /* _ALU_H */
