#include <stdlib.h>
#include <string.h>

#include "loc_hint.h"
#include "hashfn.h"

static int
closest_power_of_two (int n)
{
  return (int)pow (2, (int)(log (n)/log (2)));
}

loc_hint_table
*loc_hint_table_new (int nr_entries)
{
  loc_hint_table *hints = malloc (sizeof (loc_hint));
  hints->table_size = closest_power_of_two (nr_entries);
  hints->table = malloc (sizeof (loc_hint) * hints->table_size);

  int i;
  
  hints->unused_entries = (loc_hint *) calloc (nr_entries, sizeof (loc_hint));
  
  hints->unused_entries[0].prev = NULL;
  hints->unused_entries[0].next = &hints->unused_entries[1];

  for (i = 1; i < nr_entries-1; i++) {
    hints->unused_entries[i].next = &hints->unused_entries[i+1];
    hints->unused_entries[i].prev = &hints->unused_entries[i-1];
  }

  hints->unused_entries[nr_entries].prev = &hints->unused_entries[nr_entries-1];
  hints->unused_entries[nr_entries].next = NULL;
  return hints;
}

void 
loc_hint_table_destroy (loc_hint_table *hints)
{
  loc_hint *h = hints->used_entries;
  while (h) {
    loc_hint *tmp = h->next;
    free (h);
    h = tmp;
  }

  h = hints->unused_entries;
  while (h) {
    loc_hint *tmp = h->next;
    free (h);
    h = tmp;
  }

  free (hints->table);
  free (hints);
}

static loc_hint *
hint_lookup (loc_hint_table *hints, const char *path)
{
  int hashval = SuperFastHash (path, hints->table_size);
  loc_hint *h;

  for (h = hints->table[hashval]; h != NULL; h = h->hash_next) {
    if (!strcmp (h->path, path))
      return h;
  }
  
  return NULL;
}

struct xlator *
loc_hint_lookup (loc_hint_table *hints, const char *path)
{
  loc_hint *hint = hint_lookup (hints, path);
  if (hint && hint->valid) {
    /* bring this entry to the front */
    hint->prev->next = hint->next;
    hint->next->prev = hint->prev;
    hints->used_entries->prev = hint;
    hint->next = hints->used_entries;
    hint->prev = NULL;
    hints->used_entries = hint;

    return hint->xlator;
  }
  else
    return NULL;
}

void 
loc_hint_insert (loc_hint_table *hints, const char *path, struct xlator *xlator)
{
  loc_hint *hint = hint_lookup (hints, path);

  /* Simply set the value if an entry already exists */
  if (hint) {
    hint->xlator = xlator;
    return;
  }

  /* If we have unused entries, take one from it and insert it into the used entries list */
  if (hints->unused_entries) {
    hint = hints->unused_entries;
    hints->unused_entries = hint->next;
    hints->unused_entries->prev = NULL;
    hint->next = hints->used_entries;
    hints->used_entries->prev = hint;
    hints->used_entries = hint;

    if (hints->used_entries_last == NULL)
      hints->used_entries_last = hint;

    hint->xlator = xlator;
    hint->valid = 1;

    int hashval = SuperFastHash (path, hints->table_size);
    hint->hash_next = hints->table[hashval];
    hints->table[hashval] = hint;

    return;
  }

  /* 
     All entries are in use. Take the last entry from the used list
     (the "oldest") and use it
  */

  hint = hints->used_entries_last;
  hint->prev->next = NULL;
  hints->used_entries_last = hint->prev;

  hint->xlator = xlator;
  hint->valid = 1;

  hint->next = hints->used_entries;
  hint->prev = NULL;
  hints->used_entries = hint;
}

void loc_hint_invalidate (loc_hint_table *hints, const char *path)
{
  loc_hint *hint = hint_lookup (hints, path);
  if (hint) 
    hint->valid = 0;
}
