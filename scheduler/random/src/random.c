/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <sys/time.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "random.h"

#define RANDOM_LIMITS_MIN_FREE_DISK_DEFAULT    15
#define RANDOM_REFRESH_INTERVAL_DEFAULT        10


static int32_t
random_init (xlator_t *xl)
{
  struct random_struct *random_buf = NULL;
  xlator_list_t *trav_xl = xl->children;
  int32_t index = 0;

  random_buf = calloc (1, sizeof (struct random_struct));
  ERR_ABORT (random_buf);
  
  /* Set the seed for the 'random' function */
  srandom ((uint32_t) time (NULL));
  
  data_t *limit = dict_get (xl->options, "random.limits.min-free-disk");
  if (limit)
    {
      if (gf_string2percent (data_to_str (limit),
			     &random_buf->min_free_disk) != 0)
	{
	  gf_log ("random", 
		  GF_LOG_ERROR, 
		  "invalid number format \"%s\" of \"option random.limits.min-free-disk\"", 
		  limit);
	  return -1;
	}
      if (random_buf->min_free_disk >= 100)
	{
	  gf_log ("random", GF_LOG_ERROR,
		  "check the \"option random.limits.min-free-disk\", it should be percentage value");
	  return -1;
	}
      
    }
  else
    {
      gf_log ("random", 
	      GF_LOG_WARNING, 
	      "No option for limit min-free-disk given, defaulting it to 5%");
      random_buf->min_free_disk = RANDOM_LIMITS_MIN_FREE_DISK_DEFAULT;
    }
  
  limit = dict_get (xl->options, "random.refresh-interval");
  if (limit)
    {
      if (gf_string2time (data_to_str (limit),
			  &random_buf->refresh_interval) != 0)
	{
	  gf_log ("random", 
		  GF_LOG_ERROR, 
		  "invalid number format \"%s\" of \"option random.refresh-interval\"", 
		  limit);
	  return -1;
	}
    }
  else
    {
      random_buf->refresh_interval = RANDOM_REFRESH_INTERVAL_DEFAULT;
    }
  
  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next;
  }
  random_buf->child_count = index;
  random_buf->array = calloc (index, sizeof (struct random_sched_struct));
  ERR_ABORT (random_buf->array);
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
  int32_t idx = 0;
  int32_t percent = 0;
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);

  pthread_mutex_lock (&random_buf->random_mutex);
  for (idx = 0; idx < random_buf->child_count; idx++) {
    if (strcmp (random_buf->array[idx].xl->name, (char *)cookie) == 0)
      break;
  }
  pthread_mutex_unlock (&random_buf->random_mutex);

  if (op_ret == 0) {
    percent = (trav_stats->free_disk *100) / trav_stats->total_disk_size;
    if (random_buf->min_free_disk > percent) {
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
    call_pool_t *pool = xl->ctx->pool;
    cctx = calloc (1, sizeof (*cctx));
    ERR_ABORT (cctx);
    cctx->frames.root  = cctx;
    cctx->frames.this  = xl;    
    cctx->pool = pool;
    LOCK (&pool->lock);
    {
      list_add (&cctx->all_frames, &pool->all_frames);
    }
    UNLOCK (&pool->lock);

    STACK_WIND_COOKIE ((&cctx->frames),
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
random_schedule (xlator_t *xl, void *path)
{
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);
  int32_t rand = (int32_t) (1.0*random_buf->child_count * (random() / (RAND_MAX + 1.0)));
  int32_t try = 0;

  random_update (xl);

  while (!random_buf->array[rand].eligible) {
    if (try++ > 100)
      break; //there is a chance of this becoming a infinite loop otherwise.
    rand = (int32_t) (1.0*random_buf->child_count * (random() / (RAND_MAX + 1.0)));
  }
  return random_buf->array[rand].xl;
}


/**
 * notify
 */
void
random_notify (xlator_t *xl, int32_t event, void *data)
{
  struct random_struct *random_buf = (struct random_struct *)*((long *)xl->private);
  int32_t idx = 0;
  
  if (!random_buf)
    return;

  for (idx = 0; idx < random_buf->child_count; idx++) {
    if (random_buf->array[idx].xl == (xlator_t *)data)
      break;
  }

  switch (event)
    {
    case GF_EVENT_CHILD_UP:
      {
	//random_buf->array[idx].eligible = 1;
      }
      break;
    case GF_EVENT_CHILD_DOWN:
      {
	random_buf->array[idx].eligible = 0;
      }
      break;
    default:
      {
	;
      }
      break;
    }

}

struct sched_ops sched = {
  .init     = random_init,
  .fini     = random_fini,
  .update   = random_update,
  .schedule = random_schedule,
  .notify   = random_notify
};
