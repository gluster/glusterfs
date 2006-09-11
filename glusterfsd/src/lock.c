#include "lock.h"
#include <stdio.h>
#include <errno.h>
#include "hashfn.h"
#include <string.h>
#include <stdlib.h>

static lock_inner_t *global_lock[LOCK_HASH];

int
lock_try_acquire (const char *path)
{
  unsigned int hashval = SuperFastHash ((char *)path, strlen (path));
  lock_inner_t *trav;

  hashval = hashval % LOCK_HASH;

  trav = global_lock[hashval];

  while (trav) {
    if (!strcmp (trav->path, path))
      break;
    trav = trav->next;
  }

  if (!trav) {
    trav = calloc (1, sizeof (lock_inner_t));
    trav->path = (path);

    trav->next = global_lock[hashval];
    global_lock[hashval] = trav;
    return 0;
  }

  errno = EEXIST;
  return -1;
}


int
lock_release (const char *path)
{
  unsigned int hashval = SuperFastHash ((char *)path, strlen (path));
  lock_inner_t *trav, *prev;

  hashval = hashval % LOCK_HASH;

  trav = global_lock[hashval];
  prev = NULL;

  while (trav) {
    if (!strcmp (trav->path, path))
      break;
    prev = trav;
    trav = trav->next;
  }

  if (trav) {
    free ((void *)trav->path);

    if (prev)
      prev->next = trav->next;
    else
      global_lock[hashval] = trav->next;

    free ((void *)trav);
    return 0;
  }

  errno = ENOENT;
  return -1;
}
