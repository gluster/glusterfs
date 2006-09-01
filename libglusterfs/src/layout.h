#ifndef _LAYOUT_H
#define _LAYOUT_H

#include <pthread.h>
#include "xlator.h"

typedef struct _chunk_t {
  struct _chunk_t *next;
  char *path;
  char path_dyn;
  long long int begin;
  long long int end;
  struct xlator *child;
} chunk_t;

typedef struct _layout_t {
  pthread_mutex_t count_lock;
  char *path;
  char path_dyn;
  int refcount;
  int chunk_count;
  chunk_t chunks;
} layout_t;

/* layout_str -
   <len>:path:<chunk_count>[:<start>:<end>:<len>:path:<len>:child]
 */
#define LAYOUT_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, NULL, 1, 0, NULL }

void
layout_unref (layout_t *lay);

layout_t *
layout_getref (layout_t *lay);

layout_t *
layout_new ();

char *layout_to_str (layout_t *lay);
int str_to_layout (char *str, layout_t *lay);

#endif /* _LAYOUT_H */
