#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "xlator.h"

struct sched_ops {
  int (*init) (struct xlator *this);
  void (*fini) (struct xlator *this);
  struct xlator *(*schedule) (struct xlator *this, int size);
};

extern struct sched_ops *get_scheduler (const char *name);

#endif /* _SCHEDULER_H */
