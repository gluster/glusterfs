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
  License aint64_t with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#include "nufa.h"

static int
nufa_init (struct xlator *xl)
{
  struct nufa_struct *nufa_buf = calloc (1, sizeof (struct nufa_struct));
  struct xlator *trav_xl = xl->first_child;
  int32_t index = 0;

  data_t *local_name = dict_get (xl->options, "nufa.local-volume-name");
  if (!local_name) {
    /* Error */
    gf_log ("nufa", GF_LOG_ERROR, "No 'local-volume-name' option given in spec file\n");
    exit (1);
  }
  while (trav_xl) {
    index++;
    if (strcmp (trav_xl->name, local_name->data) == 0)
      nufa_buf->sched_xl = trav_xl;
    trav_xl = trav_xl->next_sibling;
  }
  nufa_buf->child_count = index;
  trav_xl = xl->first_child;
  index = 0;
  
  *((long *)xl->private) = (long)nufa_buf; // put it at the proper place
  return 0;
}

static void
nufa_fini (struct xlator *xl)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  free (nufa_buf);
}

static struct xlator *
nufa_schedule (struct xlator *xl, int32_t size)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  return nufa_buf->sched_xl;
}

struct sched_ops sched = {
  .init     = nufa_init,
  .fini     = nufa_fini,
  .schedule = nufa_schedule
};
