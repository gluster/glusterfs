
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

char *
layout_to_str (layout_t *lay)
{
  int tot_len = 0;
  chunk_t * chunks = &lay->chunks;
  int i;
  char *str = NULL;
  char *cur_ptr;

  tot_len += 4; // strlen (lay->path)
  tot_len++; //       :
  tot_len += strlen (lay->path); // lay->path
  tot_len++; // :
  tot_len += 4; // lay->chunk_count
  tot_len++; // :

  for (i=0; i<lay->chunk_count; i++) {
    tot_len += 16; // chunks->begin
    tot_len++;     // :
    tot_len += 16; // chunks->end
    tot_len++;     // :
    tot_len += 4;  // strlen (chunks->path)
    tot_len++;     // :
    tot_len += strlen (chunks->path); // chunks->path;
    tot_len++;     // :
    tot_len += 4;  // strlen (chunks->child->name);
    tot_len++;     // :
    tot_len += strlen (chunks->child->name); // chunks->child->name
    tot_len++;     // :

    chunks = chunks->next;
  }
  cur_ptr = str = calloc (tot_len + 1, 1);
  cur_ptr += sprintf (cur_ptr,
		      "%04d:%s:%04d:",
		      strlen (lay->path),
		      lay->path,
		      lay->chunk_count);

  for (i = 0 ; i < lay->chunk_count ; i++) {
    cur_ptr += sprintf (cur_ptr,
			"%016lld:%016lld:%04d:%s:%04d:%s:",
			chunks->begin,
			chunks->end,
			strlen (chunks->path),
			chunks->path,
			strlen (chunks->child->path),
			chunks->child->path);
    chunks = chunks->next;
  }

  return str;
}

int
str_to_layout (char *str,
	       layout_t *lay)
{
  char *cur_ptr = str;
  chunk_t *chunk &lay->chunks;
  int i;

  if (cur_ptr[4] != ':')
    return -1;

  sscanf (cur_ptr, "%d:", &i);
  cur_ptr += 4;
  cur_ptr ++;

  if (cur_ptr[i] != ':')
    return -1;

  lay->path_dyn = 1;
  lay->path = strndup (cur_ptr, i);

  cur_ptr += i;
  cur_ptr ++;

  if (cur_ptr[4] != ':')
    return -1;

  sscanf (cur_ptr, "%d:", &lay->chunk_count);
  cur_ptr += 4;
  cur_ptr ++;

  if (lay->chunk_count > 0) {
    sscanf (cur_ptr,
	    "%lld:%lld:%d:", 
	    &chunk->begin,
	    &chunk->end,
	    &i);
    cur_ptr += (16 + 1 + 16 + 1 + 4 + 1);

    chunk->path = strndup (cur_ptr, i);
    chunk->path_dyn = 1;

    cur_ptr += i;
    cur_ptr ++;

    sscanf (cur_ptr,
	    "%d", &i);
  }
  for (i = 1; i < lay->chunk_count; i++) {

  }

}
