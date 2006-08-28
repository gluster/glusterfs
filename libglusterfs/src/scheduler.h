#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "xlator.h"

struct sched_struct {
  int (*init) (struct xlator *this);
  void (*fini) (struct xlator *this);
  struct xlator *(*schedule) (struct xlator *this, int size);
};

extern struct sched_struct *get_scheduler (const char *name);

#endif /* _SCHEDULER_H */
