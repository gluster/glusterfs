#include <stdlib.h>
#include <string.h>
#include <math.h>

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
  loc_hint_table *hints = malloc (sizeof (loc_hint_table));
  hints->table_size = closest_power_of_two (nr_entries);
  hints->table = malloc (sizeof (loc_hint) * hints->table_size);

  int i;
  
  hints->unused_entries = (loc_hint *) calloc (nr_entries, sizeof (loc_hint));
  hints->unused_entries_initial = hints->unused_entries;
  
  hints->unused_entries[0].prev = NULL;
  hints->unused_entries[0].next = &hints->unused_entries[1];

  for (i = 1; i < nr_entries-1; i++) {
    hints->unused_entries[i].next = &hints->unused_entries[i+1];
    hints->unused_entries[i].prev = &hints->unused_entries[i-1];
  }

  hints->unused_entries[nr_entries-1].prev = &hints->unused_entries[nr_entries-2];
  hints->unused_entries[nr_entries-1].next = NULL;

  pthread_mutex_init (&hints->lock, NULL);
  return hints;
}

void 
loc_hint_table_destroy (loc_hint_table *hints)
{
  pthread_mutex_lock (&hints->lock);
  free (hints->unused_entries_initial);
  free (hints->table);
  pthread_mutex_unlock (&hints->lock);
  free (hints);
}

static loc_hint *
hint_lookup (loc_hint_table *hints, const char *path)
{
  int hashval = SuperFastHash (path, strlen (path)) % hints->table_size;
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
  pthread_mutex_lock (&hints->lock);
  loc_hint *hint = hint_lookup (hints, path);
  if (hint && hint->valid) {
    /* bring this entry to the front */
    if (hint->prev)
      hint->prev->next = hint->next;
    if (hint->next)
      hint->next->prev = hint->prev;

    hints->used_entries->prev = hint;
    hint->next = hints->used_entries;
    hint->prev = NULL;
    hints->used_entries = hint;

    pthread_mutex_unlock (&hints->lock);
    /* return with reference (_getref) */
    return hint->xlator;
  }
  else {
    pthread_mutex_unlock (&hints->lock);
    return NULL;
  }
}

void 
loc_hint_insert (loc_hint_table *hints, const char *path, struct xlator *xlator)
{
  pthread_mutex_lock (&hints->lock);
  loc_hint *hint = hint_lookup (hints, path);

  /* Simply set the value if an entry already exists */
  if (hint) {
    /* getref */
    hint->xlator = xlator;
    pthread_mutex_unlock (&hints->lock);
    return;
  }

  /*
    If we have unused entries, take one from it and insert it into
    the used entries list
  */
  if (hints->unused_entries) {
    hint = hints->unused_entries;
    hints->unused_entries = hint->next;
    
    if (hints->unused_entries)
      hints->unused_entries->prev = NULL;
    hint->next = hints->used_entries;
    
    if (hints->used_entries)
      hints->used_entries->prev = hint;
    hints->used_entries = hint;

    if (hints->used_entries_last == NULL)
      hints->used_entries_last = hint;

    hint->path = strdup (path);
    hint->xlator = xlator;
    hint->valid = 1;

    int hashval = SuperFastHash (path, strlen (path)) % hints->table_size;
    hint->hash_next = hints->table[hashval];
    hints->table[hashval] = hint;

    pthread_mutex_unlock (&hints->lock);
    return;
  }

  /* 
     All entries are in use. Take the last entry from the used list
     (the "oldest") and use it
  */

  hint = hints->used_entries_last;
  while (hint->refcount > 0) {
    hint = hint->prev;
    if (!hint) {
      /*
	uh-oh. We've reached the beginning of the list without finding any free
	node. Silently return.
      */
      return;
    }
  }

  /* Remove that node from the used list and the hash table */
  /* TBD: unref() on the xlator, and lose reference to it 
     before fixing next xlator, and get a reference to it
     with getref () */
  if (hint == hints->used_entries_last)
    hints->used_entries_last = hint->prev;
  
  if (hint->prev)
    hint->prev->next = hint->next;
  
  if (hint->next)
    hint->next->prev = hint->prev;

  int hashval = SuperFastHash (path, strlen (path)) % hints->table_size;
  loc_hint *h = hints->table[hashval];
  loc_hint *hp = NULL;
  while (h != NULL) {
    if (h == hint) {
      if (hp)
	hp->next = h->next;
      else
	hints->table[hashval] = h->next;
    }
    hp = h;
    h = h->next;
  }
  
  free ((void *)hint->path);
  hint->path = strdup (path);
  hint->xlator = xlator;
  hint->valid = 1;

  hint->next = hints->used_entries;
  hint->prev = NULL;
  hints->used_entries = hint;

  hashval = SuperFastHash (path, strlen (path)) % hints->table_size;
  hint->hash_next = hints->table[hashval];
  hints->table[hashval] = hint;

  pthread_mutex_unlock (&hints->lock);
}

void loc_hint_invalidate (loc_hint_table *hints, const char *path)
{
  pthread_mutex_lock (&hints->lock);
  loc_hint *hint = hint_lookup (hints, path);
  if (hint) 
    hint->valid = 0;
  pthread_mutex_unlock (&hints->lock);
}

void loc_hint_ref (loc_hint_table *hints, const char *path)
{
  pthread_mutex_lock (&hints->lock);
  loc_hint *hint = hint_lookup (hints, path);
  if (hint)
    hint->refcount++;
  pthread_mutex_unlock (&hints->lock);
}

void loc_hint_unref (loc_hint_table *hints, const char *path)
{
  pthread_mutex_lock (&hints->lock);
  loc_hint *hint = hint_lookup (hints, path);
  if (hint && hint->refcount > 0)
    hint->refcount--;
  pthread_mutex_unlock (&hints->lock);
}

#ifdef LOC_HINT_TEST

int main (void)
{
  int n = 42;
  int *foo = &n;

  loc_hint_table *hints = loc_hint_table_new (2);
  loc_hint_insert (hints, "/home/avati", foo);
  loc_hint_insert (hints, "/home/avati", foo);
  loc_hint_ref (hints, "/home/avati");
  
  loc_hint_insert (hints, "/home/amar", foo);
  loc_hint_ref (hints, "/home/amar");

  loc_hint_unref (hints,  "/home/avati");
  loc_hint_insert (hints, "/home/vikas", foo);
  loc_hint_ref (hints, "/home/vikas");
  loc_hint_insert (hints, "/home/bala", foo);
  
  printf ("%d\n", *(int *)loc_hint_lookup (hints, "/home/vikas"));
  printf ("%d\n", *(int *)loc_hint_lookup (hints, "/home/amar"));

  loc_hint_invalidate (hints, "/home/vikas");
  loc_hint_table_destroy (hints);
}

#endif
