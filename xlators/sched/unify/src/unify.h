#ifndef _UNIFY_H
#define _UNIFY_H

#include "scheduler.h"

struct cement_private {
  /* Update this structure depending on requirement */
  void *scheduler;
  struct sched_ops *sched_ops;
  int childnode_cnt;
  unsigned char is_debug;
};

#endif /* _UNIFY_H */
