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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "nufa.h"
#include <sys/time.h>

static int32_t
nufa_init (xlator_t *xl)
{
  int32_t index = 0;
  data_t *local_name = NULL;
  data_t *data = NULL;
  xlator_list_t *trav_xl = xl->children;
  struct nufa_struct *nufa_buf = calloc (1, sizeof (struct nufa_struct));

  data = dict_get (xl->options, "nufa.limits.min-free-disk");
  if (data) {
    nufa_buf->min_free_disk = gf_str_to_long_long (data->data);
    if (nufa_buf->min_free_disk >= 100) {
      gf_log ("nufa", GF_LOG_ERROR,
	      "check the \"option nufa.limits.min-free-disk\", it should be percentage value");
      return -1;
    }
  } else {
    gf_log ("nufa", GF_LOG_WARNING, 
	    "No option for limit min-free-disk given, defaulting it to 15%");
    nufa_buf->min_free_disk = gf_str_to_long_long ("15"); /* 15% free-disk */
  }
  data = dict_get (xl->options, "nufa.refresh-interval");
  if (data) {
    nufa_buf->refresh_interval = (int32_t)gf_str_to_long_long (data->data);
  } else {
    gf_log ("nufa", GF_LOG_WARNING, 
	    "No option for nufa.refresh-interval given, defaulting it to 30");
    nufa_buf->refresh_interval = 30; /* 30 Seconds */
  }

  /* Get the array built */
  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next;
  }
  nufa_buf->child_count = index;
  nufa_buf->sched_index = 0;
  nufa_buf->array = calloc (index, sizeof (struct nufa_sched_struct));
  nufa_buf->local_array = calloc (index, sizeof (int32_t));
  trav_xl = xl->children;

  local_name = dict_get (xl->options, "nufa.local-volume-name");
  if (!local_name) {
    /* Error */
    gf_log ("nufa", GF_LOG_ERROR, 
	    "No 'local-volume-name' option given in spec file\n");
    freee (nufa_buf->array);
    freee (nufa_buf->local_array);
    freee (nufa_buf);
    return -1;
  }

  /* Get the array properly */
  index = 0;
  trav_xl = xl->children;
  while (trav_xl) {
    nufa_buf->array[index].xl = trav_xl->xlator;
    nufa_buf->array[index].eligible = 1;
    nufa_buf->array[index].free_disk = nufa_buf->min_free_disk;
    nufa_buf->array[index].refresh_interval = nufa_buf->refresh_interval;
    trav_xl = trav_xl->next;
    index++;
  }
  
  { 
    int32_t array_index = 0;
    char *child = NULL;
    char *tmp = NULL;
    char *childs_data = strdup (local_name->data);
    
    child = strtok_r (childs_data, ",", &tmp);
    while (child) {
      /* Check if the local_volume specified is proper subvolume of unify */
      trav_xl = xl->children;
      index=0;
      while (trav_xl) {
	if (strcmp (child, trav_xl->xlator->name) == 0)
	  break;
	trav_xl = trav_xl->next;
	index++;
      }

      if (!trav_xl) {
	/* entry for 'local-volume-name' is wrong, not present in subvolumes */
	gf_log ("nufa", GF_LOG_ERROR, 
		"option 'nufa.local-volume-name' is wrong\n");
	freee (nufa_buf->array);
	freee (nufa_buf->local_array);
	freee (nufa_buf);
	return -1;
      } else {
	nufa_buf->local_array[array_index++] = index;
	nufa_buf->local_xl_count++;
      }
      child = strtok_r (NULL, ",", &tmp);
    }
    free (childs_data);
  }

  LOCK_INIT (&nufa_buf->nufa_lock);
  *((long *)xl->private) = (long)nufa_buf; // put it at the proper place
  return 0;
}

static void
nufa_fini (xlator_t *xl)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  LOCK_DESTROY (&nufa_buf->nufa_lock);
  freee (nufa_buf->local_array);
  freee (nufa_buf->array);
  freee (nufa_buf);
}

static int32_t 
update_stat_array_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct xlator_stats *trav_stats)
{
  struct nufa_struct *nufa_struct = NULL; 
  int32_t idx = 0;
  int32_t percent = 0;

  nufa_struct = (struct nufa_struct *)*((long *)xl->private);
  LOCK (&nufa_struct->nufa_lock);
  for (idx = 0; idx < nufa_struct->child_count; idx++) {
    if (nufa_struct->array[idx].xl->name == (char *)cookie)
      break;
  }
  UNLOCK (&nufa_struct->nufa_lock);

  if (op_ret == 0) {
    percent = (trav_stats->free_disk * 100) / trav_stats->total_disk_size;
    if (nufa_struct->array[idx].free_disk > percent) {
      if (nufa_struct->array[idx].eligible)
	gf_log ("nufa", GF_LOG_CRITICAL,
		"node \"%s\" is _almost_ (%d %%) full", 
		nufa_struct->array[idx].xl->name, 100 - percent);
      nufa_struct->array[idx].eligible = 0;
    } else {
      nufa_struct->array[idx].eligible = 1;
    }
  } else {
    nufa_struct->array[idx].eligible = 0;
  }

  STACK_DESTROY (frame->root);
  return 0;
}

static void 
update_stat_array (xlator_t *xl)
{
  /* This function schedules the file in one of the child nodes */
  int32_t idx;
  call_ctx_t *cctx;
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);

  for (idx = 0; idx < nufa_buf->child_count; idx++) {
    call_pool_t *pool = xl->ctx->pool;
    cctx = calloc (1, sizeof (*cctx));
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
		 nufa_buf->array[idx].xl->name,
		 nufa_buf->array[idx].xl, 
		 (nufa_buf->array[idx].xl)->mops->stats,
		 0); //flag
  }

  return;
}

static void 
nufa_update (xlator_t *xl)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  struct timeval tv;
  gettimeofday (&tv, NULL);
  if (tv.tv_sec > (nufa_buf->refresh_interval 
		   + nufa_buf->last_stat_fetch.tv_sec)) {
    /* Update the stats from all the server */
    update_stat_array (xl);
    nufa_buf->last_stat_fetch.tv_sec = tv.tv_sec;
  }
}

static xlator_t *
nufa_schedule (xlator_t *xl, void *path)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  int32_t nufa_orig = nufa_buf->local_xl_index;  
  int32_t rr;
  
  nufa_update (xl);
  
  while (1) {
    LOCK (&nufa_buf->nufa_lock);
    rr = nufa_buf->local_xl_index++;
    nufa_buf->local_xl_index %= nufa_buf->local_xl_count;
    UNLOCK (&nufa_buf->nufa_lock);
    
    /* if 'eligible' or there are _no_ eligible nodes */
    if (nufa_buf->array[nufa_buf->local_array[rr]].eligible) {
      /* Return the local node */
      return nufa_buf->array[nufa_buf->local_array[rr]].xl;
    }
    if ((rr + 1) % nufa_buf->local_xl_count == nufa_orig) {
      gf_log ("nufa", 
	      GF_LOG_CRITICAL, 
	      "No free space available on any local volumes, using RR scheduler");
      LOCK (&nufa_buf->nufa_lock);
      nufa_buf->local_xl_index++;
      nufa_buf->local_xl_index %= nufa_buf->local_xl_count;
      UNLOCK (&nufa_buf->nufa_lock);
      break;
    }
  }

  nufa_orig = nufa_buf->sched_index;  
  while (1) {
    LOCK (&nufa_buf->nufa_lock);
    rr = nufa_buf->sched_index++;
    nufa_buf->sched_index = nufa_buf->sched_index % nufa_buf->child_count;
    UNLOCK (&nufa_buf->nufa_lock);
    
    /* if 'eligible' or there are _no_ eligible nodes */
    if (nufa_buf->array[rr].eligible) {
      break;
    }
    if ((rr + 1) % nufa_buf->child_count == nufa_orig) {
      gf_log ("nufa", 
	      GF_LOG_CRITICAL, 
	      "No free space available on any server, using RR scheduler.");
      LOCK (&nufa_buf->nufa_lock);
      nufa_buf->sched_index++;
      nufa_buf->sched_index = nufa_buf->sched_index % nufa_buf->child_count;
      UNLOCK (&nufa_buf->nufa_lock);
      break;
    }
  }
  return nufa_buf->array[rr].xl;
}


/**
 * notify
 */
void
nufa_notify (xlator_t *xl, int32_t event, void *data)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  int32_t idx = 0;
  
  if (!nufa_buf)
    return;

  for (idx = 0; idx < nufa_buf->child_count; idx++) {
    if (strcmp (nufa_buf->array[idx].xl->name, ((xlator_t *)data)->name) == 0)
      break;
  }

  switch (event)
    {
    case GF_EVENT_CHILD_UP:
      {
	//nufa_buf->array[idx].eligible = 1;
      }
      break;
    case GF_EVENT_CHILD_DOWN:
      {
	nufa_buf->array[idx].eligible = 0;
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
  .init     = nufa_init,
  .fini     = nufa_fini,
  .update   = nufa_update,
  .schedule = nufa_schedule,
  .notify   = nufa_notify
};
