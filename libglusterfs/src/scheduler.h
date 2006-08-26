#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "xlator.h"

struct sched_struct {
  
  struct xlator *(*schedule) (struct xlator *this, int size);
};

extern struct sched_struct *get_scheduler (const char *name);

#endif /* _SCHEDULER_H */
