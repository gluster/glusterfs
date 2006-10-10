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
#include <sys/time.h>
#include "random.h"

static int32_t
random_init (struct xlator *xl)
{
  struct random_struct *random_buf = calloc (1, sizeof (struct random_struct));
  struct xlator *trav_xl = xl->first_child;
  
  {
    /* Set the seed for the 'random' function */
    srandom ((uint32_t) time (NULL));
  }

  int32_t index = 0;

  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next_sibling;
  }
  random_buf->child_count = index;
  random_buf->array = calloc (index, sizeof (struct random_sched_struct));
  trav_xl = xl->first_child;
  index = 0;

  while (trav_xl) {
    random_buf->array[index].xl = trav_xl;
    random_buf->array[index].eligible = 1;
    trav_xl = trav_xl->next_sibling;
    index++;
  }
  
  *((long *)xl->private) = (long)random_buf; // put it at the proper place
  return 0;
}

static void
random_fini (struct xlator *xl)
{
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);
  free (random_buf->array);
  free (random_buf);
}

static struct xlator *
random_schedule (struct xlator *xl, int32_t size)
{
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);
  int32_t rand = random () % random_buf->child_count;
  while (!random_buf->array[rand].eligible) {
    rand = random () % random_buf->child_count;
  }
  return random_buf->array[rand].xl;
}

struct sched_ops sched = {
  .init     = random_init,
  .fini     = random_fini,
  .schedule = random_schedule
};
