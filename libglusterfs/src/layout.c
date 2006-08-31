
#include "layout.h"
#include <pthread.h>


void
layout_destroy (layout_t *lay)
{
  chunk_t *chunk, *prev;

  pthread_mutex_destroy (&lay->count_lock);
  chunk = prev = lay->chunks.next;

  while (prev) {
    chunk = prev->next;
    if (prev->path_dyn)
      free (prev->path);
    free (prev);
    prev = chunk;
  }

  //  free (lay);
}

void
layout_unref (layout_t *lay)
{
  pthread_mutex_lock (&lay->count_lock);
  lay->refcount--;
  pthread_mutex_unlock (&lay->count_lock);

  if (!lay->refcount) {
    layout_destroy (lay);
  }
}


layout_t *
layout_getref (layout_t *lay)
{
  pthread_mutex_lock (&lay->count_lock);
  lay->refcount++;
  pthread_mutex_unlock (&lay->count_lock);

  return NULL;
}


layout_t *
layout_new ()
{
  layout_t *newlayout = (void *) calloc (1, sizeof (layout_t));
  pthread_mutex_init (&newlayout->count_lock, NULL);
  return newlayout;
}

