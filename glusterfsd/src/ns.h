#ifndef _NS_H
#define _NS_H

#include "hashfn.h"

#define LOCK_HASH 1024

typedef struct _ns_inner {
  struct _ns_inner *next;
  const char *path;
  const char *ns;
} ns_inner_t;

ns_inner_t * global_ns[LOCK_HASH];

char * ns_lookup (const char *path);

int ns_update (const char *path, const char *ns);

#endif /* _NS_H */
