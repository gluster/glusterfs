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

#include <sys/time.h>
#include <stdlib.h>
#include "rr.h"


static int64_t 
str_to_long_long (const char *number)
{
  int64_t unit = 1;
  int64_t ret = 0;
  char *endptr = NULL ;
  ret = strtoll (number, &endptr, 0);

  if (endptr) {
    switch (*endptr) {
    case 'G':
      if (* (endptr + 1) == 'B')
	unit = 1024 * 1024 * 1024;
      break;
    case 'M':
      if (* (endptr + 1) == 'B')
	unit = 1024 * 1024;
      break;
    case 'K':
      if (* (endptr + 1) == 'B')
	unit = 1024;
      break;
    case '%':
      unit = 1;
      break;
    default:
      unit = 1;
      break;
    }
  }
  return ret * unit;
}

static int32_t
rr_init (struct xlator *xl)
{
  struct rr_struct *rr_buf = calloc (1, sizeof (struct rr_struct));
  xlator_list_t *trav_xl = xl->children;
  data_t *data = dict_get (xl->options, "rr.limits.min-free-disk");
  if (data) {
    rr_buf->min_free_disk = str_to_long_long (data->data);
  } else {
    rr_buf->min_free_disk = str_to_long_long ("10GB"); /* 10 GB */
  }
  rr_buf->refresh_interval = 10; /* 10 Seconds */

  int32_t index = 0;
  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next;
  }
  rr_buf->child_count = index;
  rr_buf->sched_index = 0;
  rr_buf->array = calloc (index, sizeof (struct rr_sched_struct));
  trav_xl = xl->children;
  index = 0;

  while (trav_xl) {
    rr_buf->array[index].xl = trav_xl->xlator;
    rr_buf->array[index].eligible = 1;
    trav_xl = trav_xl->next;
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

static int32_t 
update_stat_array_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t ret,
		       int32_t op_errno,
		       struct xlator_stats *trav_stats)
{
  struct rr_struct *rr_struct = (struct rr_struct *)*((long *)xl->private);
  int32_t idx = 0;
  
  // LOCK
  for (idx = 0; idx < rr_struct->child_count; idx++) {
    if (strcmp (rr_struct->array[idx].xl->name, prev_frame->this->name) == 0)
      break;
  }
  // UNLOCK
  
  if (rr_struct->array[idx].free_disk < trav_stats->free_disk)
    rr_struct->array[idx].eligible = 0;

  return 0;
}

static void 
update_stat_array (xlator_t *xl)
{
  /* This function schedules the file in one of the child nodes */
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  int32_t idx = 0;

  for (idx = 0 ; idx < rr_buf->child_count; idx++) {
    call_ctx_t *cctx = calloc (1, sizeof (*cctx));
    cctx->frames.root  = cctx;
    cctx->frames.this  = xl;    

    STACK_WIND ((&cctx->frames), 
		update_stat_array_cbk, 
		rr_buf->array[idx].xl, 
		(rr_buf->array[idx].xl)->mops->stats,
		0); //flag
  }
  return;
}

static struct xlator *
rr_schedule (struct xlator *xl, int32_t size)
{
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  int32_t rr;
  struct timeval tv;
  gettimeofday (&tv, NULL);
  if (tv.tv_sec > (rr_buf->refresh_interval + rr_buf->last_stat_fetch.tv_sec)) {
  /* Update the stats from all the server */
    update_stat_array (xl);
    rr_buf->last_stat_fetch.tv_sec = tv.tv_sec;
  }
  
  while (1) {
    pthread_mutex_lock (&rr_buf->rr_mutex);
    rr = rr_buf->sched_index++;
    rr_buf->sched_index = rr_buf->sched_index % rr_buf->child_count;
    pthread_mutex_unlock (&rr_buf->rr_mutex);
    if (rr_buf->array[rr].eligible)
      break;
  }
  return rr_buf->array[rr].xl;
}

struct sched_ops sched = {
  .init     = rr_init,
  .fini     = rr_fini,
  .schedule = rr_schedule
};
