/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#include "layout.h"
#include "xlator.h"
#include <pthread.h>


void
layout_destroy (layout_t *lay)
{
  chunk_t *chunk, *prev;

  pthread_mutex_destroy (&lay->count_lock);
  chunk = prev = lay->chunks.next;
  
  if (lay->path_dyn)
    free (lay->path);

  if (lay->chunks.path_dyn)
    free (lay->chunks.path);

  while (prev) {
    chunk = prev->next;
    if (prev->path_dyn)
      free (prev->path);
    if (prev->child_name_dyn)
      free (prev->child_name);
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
    tot_len += 20; // chunks->begin
    tot_len++;     // :
    tot_len += 20; // chunks->end
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

  chunks = &lay->chunks;
  for (i = 0 ; i < lay->chunk_count ; i++) {
    cur_ptr += sprintf (cur_ptr,
			"%020lld:%020lld:%04d:%s:%04d:%s:",
			chunks->begin,
			chunks->end,
			strlen (chunks->path),
			chunks->path,
			strlen (chunks->child->name),
			chunks->child->name);
    chunks = chunks->next;
  }

  return str;
}

int
str_to_layout (char *str,
	       layout_t *lay)
{
  char *cur_ptr = str;
  chunk_t *chunk = &lay->chunks;
  int i;

  memset (lay, 0, sizeof (*lay));
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
    cur_ptr += (20 + 1 + 20 + 1 + 4 + 1);

    chunk->path = strndup (cur_ptr, i);
    chunk->path_dyn = 1;

    cur_ptr += i;
    cur_ptr ++;

    sscanf (cur_ptr,
	    "%d:", &i);

    cur_ptr += (4 + 1);

    chunk->child_name = strndup (cur_ptr, i);
    chunk->child_name_dyn = 1;

    cur_ptr += i;
    cur_ptr ++;
  }

  for (i = 1; i < lay->chunk_count; i++) {
    chunk->next = calloc (1, sizeof (chunk_t));
    chunk = chunk->next;

    sscanf (cur_ptr,
	    "%lld:%lld:%d:", 
	    &chunk->begin,
	    &chunk->end,
	    &i);
    cur_ptr += (20 + 1 + 20 + 1 + 4 + 1);

    chunk->path = strndup (cur_ptr, i);
    chunk->path_dyn = 1;

    cur_ptr += i;
    cur_ptr ++;

    sscanf (cur_ptr,
	    "%d:", &i);

    cur_ptr += (4 + 1);

    chunk->child_name = strndup (cur_ptr, i);
    chunk->child_name_dyn = 1;
    //    chunk->child = xlator_lookup (chunk->child_name);

    cur_ptr += i;
    cur_ptr ++;
  }

  return 0;
}

void
layout_setchildren (layout_t *lay, struct xlator *this)
{
  struct xlator *children = this->first_child;
  chunk_t *chunk = &lay->chunks;

  while (chunk) {
    if (chunk->child_name && !chunk->child) {
      struct xlator *trav = children;
      while (trav) {
	if (!strcmp (trav->name, chunk->child_name)) {
	  chunk->child = trav;
	  break;
	}
	trav = trav->next_sibling;
      }
    }
    chunk = chunk->next;
  }
}
