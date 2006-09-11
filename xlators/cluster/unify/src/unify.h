#ifndef _UNIFY_H
#define _UNIFY_H

#include "scheduler.h"

#define MAX_DIR_ENTRY_STRING     (32 * 1024)

struct cement_private {
  /* Update this structure depending on requirement */
  void *scheduler; /* THIS SHOULD BE THE FIRST VARIABLE, if xlator is using scheduler */
  struct sched_ops *sched_ops; /* Scheduler options */
  struct xlator **array; /* Child node array */
  int child_count;
  int is_debug;
};

#endif /* _UNIFY_H */
