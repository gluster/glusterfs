#ifndef _SCHEDULAR_H
#define _SCHEDULAR_H

#include "xlator.h"

struct sched_struct {
  
  struct xlator *(*schedule) (struct xlator *this, int size);
};

extern struct sched_struct *get_schedular (const char *name);

#endif /* _SCHEDULAR_H */
