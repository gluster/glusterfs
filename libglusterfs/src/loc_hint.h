#ifndef __LOC_HINT_H__
#define __LOC_HINT_H__

#include <pthread.h>
#include "xlator.h"

typedef struct _loc_hint {
  const char *path;
  struct xlator *xlator;
  int valid;
  int refcount;
  struct _loc_hint *hash_next;  /* for the hash table */
  struct _loc_hint *next;       /* for the unused node and used node lists */
  struct _loc_hint *prev;
} loc_hint;

typedef struct {
  loc_hint **table;
  int table_size;

  loc_hint *used_entries;
  loc_hint *used_entries_last;

  loc_hint *unused_entries;
  pthread_mutex_t lock;
} loc_hint_table;

loc_hint_table *loc_hint_table_new (int nr_entries);
void loc_hint_table_destroy (loc_hint_table *hints);

struct xlator *loc_hint_lookup (loc_hint_table *hints, const char *path);
void loc_hint_insert (loc_hint_table *hints, const char *path, struct xlator *xlator);

void loc_hint_ref (loc_hint_table *hints, const char *path);
void loc_hint_unref (loc_hint_table *hints, const char *path);

void loc_hint_invalidate (loc_hint_table *hints, const char *path);

#endif /* __LOC_HINT_H__ */
