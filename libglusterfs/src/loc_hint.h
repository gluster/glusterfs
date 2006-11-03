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

#ifndef __LOC_HINT_H__
#define __LOC_HINT_H__

#include <pthread.h>
#include "xlator.h"

typedef struct _loc_hint {
  const char *path;
  struct xlator *xlator;
  int32_t valid;
  int32_t refcount;
  struct _loc_hint *hash_next;  /* for the hash table */
  struct _loc_hint *next;       /* for the unused node and used node lists */
  struct _loc_hint *prev;
} loc_hint;

typedef struct {
  loc_hint **table;
  int32_t table_size;

  loc_hint *used_entries;
  loc_hint *used_entries_last;

  loc_hint *unused_entries;
  loc_hint *unused_entries_initial;  /* the initial pointer; used later for destroying */

  pthread_mutex_t lock;
} loc_hint_table;

loc_hint_table *loc_hint_table_new (int32_t nr_entries);
void loc_hint_table_destroy (loc_hint_table *hints);

struct xlator *loc_hint_lookup (loc_hint_table *hints, const char *path);
void loc_hint_insert (loc_hint_table *hints, const char *path, struct xlator *xlator);

void loc_hint_ref (loc_hint_table *hints, const char *path);
void loc_hint_unref (loc_hint_table *hints, const char *path);

void loc_hint_invalidate (loc_hint_table *hints, const char *path);

#endif /* __LOC_HINT_H__ */
