/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
random_init (xlator_t *xl)
{
  struct random_struct *random_buf = calloc (1, sizeof (struct random_struct));
  xlator_list_t *trav_xl = xl->children;
  int32_t index = 0;

  /* Set the seed for the 'random' function */
  srandom ((uint32_t) time (NULL));

  data_t *limit = dict_get (xl->options, "random.limits.min-free-disk");
  if (limit) {
    random_buf->min_free_disk = gf_str_to_long_long (limit->data);
  } else {
    gf_log ("random", 
	    GF_LOG_WARNING, 
	    "No option for limit min-free-disk given, defaulting it to 1GB");
    random_buf->min_free_disk = gf_str_to_long_long ("1GB");
  }

  limit = dict_get (xl->options, "random.refresh-interval");
  if (limit) {
    random_buf->refresh_interval = (int32_t)gf_str_to_long_long (limit->data);
  } else {
    random_buf->refresh_interval = 10; /* 10 Seconds */
  }

  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next;
  }
  random_buf->child_count = index;
  random_buf->array = calloc (index, sizeof (struct random_sched_struct));
  trav_xl = xl->children;
  index = 0;

  while (trav_xl) {
    random_buf->array[index].xl = trav_xl->xlator;
    random_buf->array[index].eligible = 1;
    trav_xl = trav_xl->next;
    index++;
  }
  pthread_mutex_init (&random_buf->random_mutex, NULL);
  
  *((long *)xl->private) = (long)random_buf; // put it at the proper place
  return 0;
}

static void
random_fini (xlator_t *xl)
{
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);
  pthread_mutex_destroy (&random_buf->random_mutex);
  free (random_buf->array);
  free (random_buf);
}


static int32_t 
update_stat_array_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct xlator_stats *trav_stats)
{
  int32_t idx;
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);

  pthread_mutex_lock (&random_buf->random_mutex);
  for (idx = 0; idx < random_buf->child_count; idx++) {
    if (strcmp (random_buf->array[idx].xl->name, (char *)cookie) == 0)
      break;
  }
  pthread_mutex_unlock (&random_buf->random_mutex);

  if (op_ret == 0) {
    if (random_buf->min_free_disk > trav_stats->free_disk) {
      random_buf->array[idx].eligible = 0;
    } else {
      random_buf->array[idx].eligible = 1;
    }
  } else {
    random_buf->array[idx].eligible = 0;
  }    

  STACK_DESTROY (frame->root);
  return 0;
}

static void 
update_stat_array (xlator_t *xl)
{
  int32_t idx;
  call_ctx_t *cctx;
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);

  for (idx = 0; idx < random_buf->child_count; idx++) {
    cctx = calloc (1, sizeof (*cctx));
    cctx->frames.root = cctx;
    cctx->frames.this = xl;
    
    _STACK_WIND ((&cctx->frames),
		 update_stat_array_cbk,
		 random_buf->array[idx].xl->name,
		 random_buf->array[idx].xl,
		 (random_buf->array[idx].xl)->mops->stats,
		 0);
  }
  return ;
}

static void 
random_update (xlator_t *xl)
{
  struct timeval tv;
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);

  gettimeofday(&tv, NULL);
  if (tv.tv_sec > (random_buf->refresh_interval + 
		   random_buf->last_stat_entry.tv_sec)) {
    update_stat_array (xl);
    random_buf->last_stat_entry.tv_sec = tv.tv_sec;
  }
}

static xlator_t *
random_schedule (xlator_t *xl, int32_t size)
{
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);
  int32_t rand = random () % random_buf->child_count;
  int32_t try = 0;

  //TODO: Do I need to do this here?
  random_update (xl);

  while (!random_buf->array[rand].eligible) {
    if (try++ > 100)
      break; //there is a chance of this becoming a infinite loop otherwise.
    rand = random () % random_buf->child_count;
  }
  return random_buf->array[rand].xl;
}

struct sched_ops sched = {
  .init     = random_init,
  .fini     = random_fini,
  .update   = random_update,
  .schedule = random_schedule
};
