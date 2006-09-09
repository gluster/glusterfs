#include "ns.h"
#include <stdio.h>
#include <errno.h>
#include "hashfn.h"
#include <string.h>
#include <stdlib.h>

char *
ns_lookup (const char *path)
{
  int hashval = SuperFastHash ((char *)path, strlen (path));
  ns_inner_t *trav;

  hashval = hashval % LOCK_HASH;

  trav = global_ns[hashval];

  
  while (trav) {
    if (!strcmp (trav->path, path))
      break;
    trav = trav->next;
  }

  if (trav)
    return (char *)trav->ns;

  return NULL;
}


int
ns_update (const char *path, const char *ns)
{
  int hashval = SuperFastHash ((char *)path, strlen (path));
  ns_inner_t *trav, *prev;

  hashval = hashval % LOCK_HASH;

  trav = global_ns[hashval];
  prev = NULL;

  
  while (trav) {
    if (!strcmp (trav->path, path))
      break;
    prev = trav;
    trav = trav->next;
  }

  if (trav) {
    free ((char *)trav->ns);
    trav->ns = ns;
  } else {
    trav = calloc (1, sizeof (ns_inner_t));
    trav->path = path;
    trav->ns = ns;
    if (prev)
      prev->next = trav;
    else
      global_ns[hashval] = trav;
  }
  return 0;
}
