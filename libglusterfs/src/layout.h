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

#ifndef _LAYOUT_H
#define _LAYOUT_H

#include <pthread.h>
#include "xlator.h"

typedef struct _chunk_t {
  struct _chunk_t *next;
  int8_t *path;
  int8_t path_dyn;
  int64_t begin;
  int64_t end;
  struct xlator *child;
  int8_t *child_name;
  int8_t child_name_dyn;
} chunk_t;

typedef struct _layout_t {
  pthread_mutex_t count_lock;
  int8_t *path;
  int8_t path_dyn;
  int32_t refcount;
  int32_t chunk_count;
  chunk_t chunks;
} layout_t;

/* layout_str -
   <len>:path:<chunk_count>[:<start>:<end>:<len>:path:<len>:child]
 */
#define LAYOUT_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, NULL, 1, 0, NULL }

void layout_destroy (layout_t *lay);
void layout_unref (layout_t *lay);
layout_t *layout_getref (layout_t *lay);
layout_t *layout_new ();

int8_t *layout_to_str (layout_t *lay);
int32_t str_to_layout (int8_t *str, layout_t *lay);
void layout_setchildren (layout_t *lay, struct xlator *this);
void layout_destroy (layout_t *lay);

#endif /* _LAYOUT_H */
