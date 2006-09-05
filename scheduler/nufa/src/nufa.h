#ifndef _NUFA_H
#define _NUFA_H

#include "scheduler.h"

struct nufa_struct {
  struct xlator *sched_xl;
  int child_count;
};

#endif /* _NUFA_H */
