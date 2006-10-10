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

#include <stdlib.h>
#include "rr.h"

static int32_t
rr_init (struct xlator *xl)
{
  struct rr_struct *rr_buf = calloc (1, sizeof (struct rr_struct));
  struct xlator *trav_xl = xl->first_child;
  
  int32_t index = 0;

  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next_sibling;
  }
  rr_buf->child_count = index;
  rr_buf->sched_index = 0;
  rr_buf->array = calloc (index, sizeof (struct rr_sched_struct));
  trav_xl = xl->first_child;
  index = 0;

  while (trav_xl) {
    rr_buf->array[index].xl = trav_xl;
    rr_buf->array[index].eligible = 1;
    trav_xl = trav_xl->next_sibling;
    index++;
  }
  pthread_mutex_init (&rr_buf->rr_mutex, NULL);
  *((long *)xl->private) = (long)rr_buf; // put it at the proper place
  return 0;
}

static void
rr_fini (struct xlator *xl)
{
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  free (rr_buf->array);
  free (rr_buf);
}

static struct xlator *
rr_schedule (struct xlator *xl, int32_t size)
{
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  int32_t rr;
  
  pthread_mutex_lock (&rr_buf->rr_mutex);
  rr = rr_buf->sched_index++;
  rr_buf->sched_index = rr_buf->sched_index % rr_buf->child_count;
  pthread_mutex_unlock (&rr_buf->rr_mutex);
  return rr_buf->array[rr].xl;
}

struct sched_ops sched = {
  .init     = rr_init,
  .fini     = rr_fini,
  .schedule = rr_schedule
};
