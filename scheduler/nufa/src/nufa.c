/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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


#include "nufa.h"
#include <sys/time.h>

static int32_t
nufa_init (struct xlator *xl)
{
  int32_t index = 0;
  data_t *local_name;
  struct nufa_struct *nufa_buf = calloc (1, sizeof (struct nufa_struct));
  xlator_list_t *trav_xl = xl->children;
  data_t *data = dict_get (xl->options, "nufa.limits.min-free-disk");

  if (data) {
    nufa_buf->min_free_disk = gf_str_to_long_long (data->data);
  } else {
    nufa_buf->min_free_disk = gf_str_to_long_long ("10GB"); /* 10 GB */
  }
  data = dict_get (xl->options, "nufa.refresh-interval");
  if (data) {
    nufa_buf->refresh_interval = (int32_t)gf_str_to_long_long (data->data);
  } else {
    nufa_buf->refresh_interval = 10; /* 10 Seconds */
  }
  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next;
  }
  nufa_buf->child_count = index;
  nufa_buf->sched_index = 0;
  nufa_buf->array = calloc (index, sizeof (struct nufa_sched_struct));
  trav_xl = xl->children;
  index = 0;

  local_name = dict_get (xl->options, "nufa.local-volume-name");
  if (!local_name) {
    /* Error */
    gf_log ("nufa", GF_LOG_ERROR, "No 'local-volume-name' option given in spec file\n");
    exit (1);
  }

  while (trav_xl) {
    nufa_buf->array[index].xl = trav_xl->xlator;
    nufa_buf->array[index].eligible = 1;
    nufa_buf->array[index].free_disk = nufa_buf->min_free_disk;
    nufa_buf->array[index].refresh_interval = nufa_buf->refresh_interval;
    if (strcmp (trav_xl->xlator->name, local_name->data) == 0)
      nufa_buf->local_xl_idx = index;
    trav_xl = trav_xl->next;
    index++;
  }
  pthread_mutex_init (&nufa_buf->nufa_mutex, NULL);
  *((long *)xl->private) = (long)nufa_buf; // put it at the proper place
  return 0;
}

static void
nufa_fini (struct xlator *xl)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);
  pthread_mutex_destroy (&nufa_buf->nufa_mutex);
  free (nufa_buf->array);
  free (nufa_buf);
}

static int32_t 
update_stat_array_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct xlator_stats *trav_stats)
{
  struct nufa_struct *nufa_struct = (struct nufa_struct *)*((long *)xl->private);
  int32_t idx = 0;
  
  pthread_mutex_lock (&nufa_struct->nufa_mutex);
  for (idx = 0; idx < nufa_struct->child_count; idx++) {
    if (strcmp (nufa_struct->array[idx].xl->name, (char *)cookie) == 0)
      break;
  }
  pthread_mutex_unlock (&nufa_struct->nufa_mutex);

  if (op_ret == 0) {
    if (nufa_struct->array[idx].free_disk > trav_stats->free_disk) {
      if (nufa_struct->array[idx].eligible)
	gf_log ("nufa", GF_LOG_CRITICAL, 
		"node \"%s\" is full", 
		nufa_struct->array[idx].xl->name);
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
    cctx = calloc (1, sizeof (*cctx));
    cctx->frames.root  = cctx;
    cctx->frames.this  = xl;    
    
    _STACK_WIND ((&cctx->frames), 
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
  if (tv.tv_sec > (nufa_buf->array[nufa_buf->local_xl_idx].refresh_interval 
		   + nufa_buf->array[nufa_buf->local_xl_idx].last_stat_fetch.tv_sec)) {
  /* Update the stats from all the server */
    update_stat_array (xl);
    nufa_buf->array[nufa_buf->local_xl_idx].last_stat_fetch.tv_sec = tv.tv_sec;
  }
}

static xlator_t *
nufa_schedule (xlator_t *xl, int32_t size)
{
  int32_t rr;
  int32_t nufa_orig;  
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((long *)xl->private);

  //TODO: Do i need to do this here?

  nufa_update (xl);

  if (nufa_buf->array[nufa_buf->local_xl_idx].eligible) {
    return nufa_buf->array[nufa_buf->local_xl_idx].xl;
  }

  nufa_orig = nufa_buf->sched_index;
  
  while (1) {
    pthread_mutex_lock (&nufa_buf->nufa_mutex);
    rr = nufa_buf->sched_index++;
    nufa_buf->sched_index = nufa_buf->sched_index % nufa_buf->child_count;
    pthread_mutex_unlock (&nufa_buf->nufa_mutex);

    /* if 'eligible' or there are _no_ eligible nodes */
    if (nufa_buf->array[rr].eligible) {
      break;
    }
    if ((rr + 1) % nufa_buf->child_count == nufa_orig) {
      gf_log ("nufa", 
	      GF_LOG_CRITICAL, 
	      "free space not available on any server");
      pthread_mutex_lock (&nufa_buf->nufa_mutex);
      nufa_buf->sched_index++;
      nufa_buf->sched_index = nufa_buf->sched_index % nufa_buf->child_count;
      pthread_mutex_unlock (&nufa_buf->nufa_mutex);
      break;
    }
  }
  return nufa_buf->array[rr].xl;
}


struct sched_ops sched = {
  .init     = nufa_init,
  .fini     = nufa_fini,
  .update   = nufa_update,
  .schedule = nufa_schedule
};
