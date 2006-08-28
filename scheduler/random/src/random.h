#ifndef _RANDOM_H
#define _RANDOM_H

#include "scheduler.h"

struct random_sched_struct {
  struct xlator *xl;
  unsigned char eligible;
};

struct random_struct {
  int child_count;
  struct random_sched_struct *array;
};

#endif /* _RANDOM_H */
