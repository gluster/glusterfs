#ifndef _RR_H
#define _RR_H

#include "scheduler.h"

struct rr_sched_struct {
  struct xlator *xl;
  unsigned char eligible;
};

struct rr_struct {
  struct rr_sched_struct *array;
  pthread_mutex_t rr_mutex;
  int child_count;
  int sched_index;  
};

#endif /* _RR_H */
