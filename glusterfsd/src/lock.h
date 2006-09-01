#ifndef _LOCK_H
#define _LOCK_H

#include "hashfn.h"

#define LOCK_HASH 1024

typedef struct _lock_inner {
  struct _lock_inner *next;
  const char *path;
} lock_inner_t;

lock_inner_t * global_lock[LOCK_HASH];

int lock_try_acquire (const char *path);

int lock_release (const char *path);

#endif /* _LOCK_H */
