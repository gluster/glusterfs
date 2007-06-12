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


static int32_t
rr_init (xlator_t *xl)
{
  int32_t index = 0;
  struct rr_struct *rr_buf = calloc (1, sizeof (struct rr_struct));
  xlator_list_t *trav_xl = xl->children;
  data_t *data = dict_get (xl->options, "rr.limits.min-free-disk");

  if (data) {
    rr_buf->min_free_disk = gf_str_to_long_long (data->data);
  } else {
    gf_log (xl->name,
	    GF_LOG_DEBUG,
	    "'option rr.limits.min-free-disk' not specified, defaulting to 5%");
    rr_buf->min_free_disk = gf_str_to_long_long ("5"); /* 5% free space */
  }
  data = dict_get (xl->options, "rr.refresh-interval");
  if (data) {
    rr_buf->refresh_interval = (int32_t)gf_str_to_long_long (data->data);
  } else {
    rr_buf->refresh_interval = 10; /* 10 Seconds */
  }
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
    rr_buf->array[index].free_disk = rr_buf->min_free_disk;
    rr_buf->array[index].refresh_interval = rr_buf->refresh_interval;
    trav_xl = trav_xl->next;
    index++;
  }
  pthread_mutex_init (&rr_buf->rr_mutex, NULL);
  *((long *)xl->private) = (long)rr_buf; // put it at the proper place
  return 0;
}

static void
rr_fini (xlator_t *xl)
{
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  pthread_mutex_destroy (&rr_buf->rr_mutex);
  free (rr_buf->array);
  free (rr_buf);
}

static int32_t 
update_stat_array_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct xlator_stats *trav_stats)
{
  struct rr_struct *rr_struct = (struct rr_struct *)*((long *)xl->private);
  int32_t percent = 0;
  int32_t idx = 0;
  
  pthread_mutex_lock (&rr_struct->rr_mutex);
  for (idx = 0; idx < rr_struct->child_count; idx++) {
    if (strcmp (rr_struct->array[idx].xl->name, (char *)cookie) == 0)
      break;
  }
  pthread_mutex_unlock (&rr_struct->rr_mutex);

  if (op_ret == 0) {
    percent = (trav_stats->free_disk *100) / trav_stats->total_disk_size;
    if ((rr_struct->array[idx].free_disk > percent)) {
      if (rr_struct->array[idx].eligible)
	gf_log ("rr", GF_LOG_CRITICAL, 
		"node \"%s\" is _almost_ full", 
		rr_struct->array[idx].xl->name);
      rr_struct->array[idx].eligible = 0;
    } else 
      rr_struct->array[idx].eligible = 1;      
  } else {
    rr_struct->array[idx].eligible = 0;
  }
  STACK_DESTROY (frame->root);
  return 0;
}

static void 
update_stat_array (xlator_t *xl)
{
  /* This function schedules the file in one of the child nodes */
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  call_ctx_t *cctx;
  int32_t idx;

  for (idx = 0; idx < rr_buf->child_count; idx++) {
    cctx = calloc (1, sizeof (*cctx));
    cctx->frames.root  = cctx;
    cctx->frames.this  = xl;    
    
    _STACK_WIND ((&cctx->frames), 
		 update_stat_array_cbk,
		 rr_buf->array[idx].xl->name, //cookie
		 rr_buf->array[idx].xl, 
		 (rr_buf->array[idx].xl)->mops->stats,
		 0); //flag
  }
  return;
}

static void 
rr_update (xlator_t *xl)
{
  struct timeval tv;
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);

  gettimeofday (&tv, NULL);
  if (tv.tv_sec > (rr_buf->refresh_interval 
		   + rr_buf->last_stat_fetch.tv_sec)) {
      /* Update the stats from all the server */
    update_stat_array (xl);
    rr_buf->last_stat_fetch.tv_sec = tv.tv_sec;
  }  
}

static xlator_t *
rr_schedule (xlator_t *xl, int32_t size)
{
  int32_t rr;
  struct rr_struct *rr_buf = (struct rr_struct *)*((long *)xl->private);
  int32_t rr_orig = rr_buf->sched_index;
  
  rr_update (xl);
  while (1) {
    pthread_mutex_lock (&rr_buf->rr_mutex);
    rr = rr_buf->sched_index++;
    rr_buf->sched_index = rr_buf->sched_index % rr_buf->child_count;
    pthread_mutex_unlock (&rr_buf->rr_mutex);
    
    /* if 'eligible' or there are _no_ eligible nodes */
    if (rr_buf->array[rr].eligible) {
      break;
    }
    if ((rr + 1) % rr_buf->child_count == rr_orig) {
      gf_log ("rr", 
	      GF_LOG_CRITICAL, 
	      "free space not available on any server");
      pthread_mutex_lock (&rr_buf->rr_mutex);
      rr_buf->sched_index++;
      rr_buf->sched_index = rr_buf->sched_index % rr_buf->child_count;
      pthread_mutex_unlock (&rr_buf->rr_mutex);
      break;
    }
    rr_update (xl);
  }
  return rr_buf->array[rr].xl;
}

struct sched_ops sched = {
  .init     = rr_init,
  .fini     = rr_fini,
  .update   = rr_update,
  .schedule = rr_schedule
};
