#ifndef _UNIFY_H
#define _UNIFY_H

#include "scheduler.h"

#define MAX_DIR_ENTRY_STRING     (32 * 1024)

struct cement_private {
  /* Update this structure depending on requirement */
  void *scheduler; /* THIS SHOULD BE THE FIRST VARIABLE */
  struct sched_ops *sched_ops;
  int childnode_cnt;
  unsigned char is_debug;
};

#endif /* _UNIFY_H */
