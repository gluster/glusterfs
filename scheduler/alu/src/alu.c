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



/* ALU code needs a complete re-write. This is one of the most important part of 
 * GlusterFS and so needs more and more reviews and testing 
 */
#include <sys/time.h>
#include <stdint.h>
#include "stack.h"
#include "alu.h"

static int64_t 
get_stats_disk_usage (struct xlator_stats *this)
{
  return this->disk_usage;
}

static int64_t 
get_stats_write_usage (struct xlator_stats *this)
{
  return this->write_usage;
}

static int64_t 
get_stats_read_usage (struct xlator_stats *this)
{
  return this->read_usage;
}

static int64_t 
get_stats_disk_speed (struct xlator_stats *this)
{
  return this->disk_speed;
}

static int64_t 
get_stats_file_usage (struct xlator_stats *this)
{
  (void) &get_stats_file_usage;    /* Avoid warning "defined but not used" */

  return this->nr_files;
}

static int64_t 
get_stats_num_client (struct xlator_stats *this)
{
  (void) &get_stats_num_client;    /* Avoid warning "defined but not used" */
  return this->nr_clients;
}

static int64_t 
get_stats_free_disk (struct xlator_stats *this)
{
  (void) &get_stats_free_disk;    /* Avoid warning "defined but not used" */
  return this->free_disk;
}

static int64_t 
get_max_diff_write_usage (struct xlator_stats *max, struct xlator_stats *min)
{
  return (max->write_usage - min->write_usage);
}

static int64_t 
get_max_diff_read_usage (struct xlator_stats *max, struct xlator_stats *min)
{
  return (max->read_usage - min->read_usage);
}

static int64_t 
get_max_diff_disk_usage (struct xlator_stats *max, struct xlator_stats *min)
{
  return (max->disk_usage - min->disk_usage);
}

static int64_t 
get_max_diff_disk_speed (struct xlator_stats *max, struct xlator_stats *min)
{
  return (max->disk_speed - min->disk_speed);
}

static int64_t 
get_max_diff_file_usage (struct xlator_stats *max, struct xlator_stats *min)
{
  return (max->nr_files - min->nr_files);
}

static int64_t 
get_max_diff_num_client (struct xlator_stats *max, struct xlator_stats *min)
{
  (void) &get_max_diff_num_client;    /* Avoid warning "defined but not used" */
  return (max->nr_clients - min->nr_clients);
}

static int64_t 
get_max_diff_free_disk (struct xlator_stats *max, struct xlator_stats *min)
{
  (void) &get_max_diff_free_disk;    /* Avoid warning "defined but not used" */        
  return (max->free_disk - min->free_disk);
}

static int32_t
alu_init (struct xlator *xl)
{
  struct alu_sched *alu_sched = calloc (1, sizeof (struct alu_sched));

  {
    data_t *order = dict_get (xl->options, "alu.order");
    if (!order) {
      gf_log ("scheduler/alu",
	      GF_LOG_ERROR,
	      "alu_init: order not specified");
      exit (1);
    }
    struct alu_threshold *_threshold_fn;
    struct alu_threshold *tmp_threshold;
    data_t *entry_fn = NULL;
    data_t *exit_fn = NULL;
    char *tmp_str;
    char *order_str = strtok_r (order->data, ":", &tmp_str);
    /* Get the scheduling priority order, specified by the user. */
    while (order_str) {
      gf_log ("scheduler/alu",
	      GF_LOG_DEBUG,
	      "alu_init: order string: %s",
	      order_str);
      if (strcmp (order_str, "disk-usage") == 0) {
	/* Disk usage */
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_disk_usage;
	_threshold_fn->sched_value = get_stats_disk_usage;
	entry_fn = dict_get (xl->options, "alu.disk-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.disk_usage = 1024 * 1024 * 1024; /* Byte Unit */
	} else {
	  alu_sched->entry_limit.disk_usage = gf_str_to_long_long (entry_fn->data);
	}
	_threshold_fn->entry_value = get_stats_disk_usage;
	exit_fn = dict_get (xl->options, "alu.disk-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.disk_usage = 512 * 1024 * 1024;
	} else {
	  alu_sched->exit_limit.disk_usage = gf_str_to_long_long (exit_fn->data);
	}
	_threshold_fn->exit_value = get_stats_disk_usage;
	tmp_threshold = alu_sched->threshold_fn;
	if (!tmp_threshold) {
	  alu_sched->threshold_fn = _threshold_fn;
	} else {
	  while (tmp_threshold->next) {
	    tmp_threshold = tmp_threshold->next;
	  }
	  tmp_threshold->next = _threshold_fn;
	}
	gf_log ("scheduler/alu",
		GF_LOG_DEBUG, "alu_init: = %lld,%lld", 
		alu_sched->entry_limit.disk_usage, 
		alu_sched->exit_limit.disk_usage);

      } else if (strcmp (order_str, "write-usage") == 0) {
	/* Handle "write-usage" */

	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_write_usage;
	_threshold_fn->sched_value = get_stats_write_usage;
	entry_fn = dict_get (xl->options, "alu.write-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.write_usage = 25;
	} else {
	  alu_sched->entry_limit.write_usage = (long)gf_str_to_long_long (entry_fn->data);
	}
	_threshold_fn->entry_value = get_stats_write_usage;
	exit_fn = dict_get (xl->options, "alu.write-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.write_usage = 5;
	} else {
	  alu_sched->exit_limit.write_usage = (long)gf_str_to_long_long (exit_fn->data);
	}
	_threshold_fn->exit_value = get_stats_write_usage;
	tmp_threshold = alu_sched->threshold_fn;
	if (!tmp_threshold) {
	  alu_sched->threshold_fn = _threshold_fn;
	}
	else {
	  while (tmp_threshold->next) {
	    tmp_threshold = tmp_threshold->next;
	  }
	  tmp_threshold->next = _threshold_fn;
	}
	gf_log ("scheduler/alu",
		GF_LOG_DEBUG, "alu_init: = %lld,%lld", 
		alu_sched->entry_limit.write_usage, 
		alu_sched->exit_limit.write_usage);

      } else if (strcmp (order_str, "read-usage") == 0) {
	/* Read usage */

	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_read_usage;
	_threshold_fn->sched_value = get_stats_read_usage;
	entry_fn = dict_get (xl->options, "alu.read-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.read_usage = 25;
	} else {
	  alu_sched->entry_limit.read_usage = (long)gf_str_to_long_long (entry_fn->data);
	}
	_threshold_fn->entry_value = get_stats_read_usage;
	exit_fn = dict_get (xl->options, "alu.read-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.read_usage = 5;
	} else {
	  alu_sched->exit_limit.read_usage = (long)gf_str_to_long_long (exit_fn->data);
	}
	_threshold_fn->exit_value = get_stats_read_usage;
	tmp_threshold = alu_sched->threshold_fn;
	if (!tmp_threshold) {
	  alu_sched->threshold_fn = _threshold_fn;
	}
	else {
	  while (tmp_threshold->next) {
	    tmp_threshold = tmp_threshold->next;
	  }
	  tmp_threshold->next = _threshold_fn;
	}
	gf_log ("scheduler/alu",
		GF_LOG_DEBUG, "alu_init: = %lld,%lld", 
		alu_sched->entry_limit.read_usage, 
		alu_sched->exit_limit.read_usage);

      } else if (strcmp (order_str, "open-files-usage") == 0) {
	/* Open files counter */
	
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_file_usage;
	_threshold_fn->sched_value = get_stats_file_usage;
	entry_fn = dict_get (xl->options, "alu.open-files-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.nr_files = 1000;
	} else {
	  alu_sched->entry_limit.nr_files = strtol (entry_fn->data, NULL, 0);
	}
	_threshold_fn->entry_value = get_stats_file_usage;
	exit_fn = dict_get (xl->options, "alu.open-files-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.nr_files = 100;
	} else {
	  alu_sched->exit_limit.nr_files = strtol (exit_fn->data, NULL, 0);
	}
	_threshold_fn->exit_value = get_stats_file_usage;
	tmp_threshold = alu_sched->threshold_fn;
	if (!tmp_threshold) {
	  alu_sched->threshold_fn = _threshold_fn;
	}
	else {
	  while (tmp_threshold->next) {
	    tmp_threshold = tmp_threshold->next;
	  }
	  tmp_threshold->next = _threshold_fn;
	}
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: = %ld,%ld\n", alu_sched->entry_limit.nr_files, 
		alu_sched->exit_limit.nr_files);

      } else if (strcmp (order_str, "disk-speed-usage") == 0) {
	/* Disk speed */

	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_disk_speed;
	_threshold_fn->sched_value = get_stats_disk_speed;
	entry_fn = dict_get (xl->options, "alu.disk-speed-usage.entry-threshold");
	if (entry_fn) {
	  gf_log ("scheduler/alu",
		  GF_LOG_DEBUG,
		  "alu_init: entry-threshold is given for disk-speed, \
which is constant");
	}
	_threshold_fn->entry_value = NULL;
	exit_fn = dict_get (xl->options, "alu.disk-speed-usage.exit-threshold");
	if (exit_fn) {
	  gf_log ("scheduler/alu",
		  GF_LOG_DEBUG,
		  "alu_init: exit-threshold is given for disk-speed, \
which is constant");
	}
	_threshold_fn->exit_value = NULL;
	tmp_threshold = alu_sched->threshold_fn;
	if (!tmp_threshold) {
	  alu_sched->threshold_fn = _threshold_fn;
	}
	else {
	  while (tmp_threshold->next) {
	    tmp_threshold = tmp_threshold->next;
	  }
	  tmp_threshold->next = _threshold_fn;
	}
	
      } else {
	gf_log ("scheduler/alu",
		GF_LOG_DEBUG,
		"alu_init: %s, unknown option provided to scheduler",
		order_str);
      }
      order_str = strtok_r (NULL, ":", &tmp_str);
    }
  }

  {
    /* Get the limits */
    struct alu_limits *_limit_fn = NULL;
    struct alu_limits *tmp_limits = NULL;
    data_t *limits = NULL;

    limits = dict_get (xl->options, "alu.limits.min-free-disk");
    if (limits) {
	_limit_fn = calloc (1, sizeof (struct alu_limits));
	_limit_fn->min_value = get_stats_free_disk;
	_limit_fn->cur_value = get_stats_free_disk;
	tmp_limits = alu_sched->limits_fn ;
	_limit_fn->next = tmp_limits;
	alu_sched->limits_fn = _limit_fn;
	alu_sched->spec_limit.free_disk = gf_str_to_long_long (limits->data);
	gf_log ("scheduler/alu",
		GF_LOG_DEBUG,
		"alu_init: limit.min-disk-free = %lld", 
		_limit_fn->cur_value (&(alu_sched->spec_limit)));
    }
    limits = dict_get (xl->options, "alu.limits.max-open-files");
    if (limits) {
	// Update alu_sched->priority properly
	_limit_fn = calloc (1, sizeof (struct alu_limits));
	_limit_fn->max_value = get_stats_file_usage;
	_limit_fn->cur_value = get_stats_file_usage;
	tmp_limits = alu_sched->limits_fn ;
	_limit_fn->next = tmp_limits;
	alu_sched->limits_fn = _limit_fn;
	alu_sched->spec_limit.nr_files = gf_str_to_long_long (limits->data);
	gf_log ("scheduler/alu",
		GF_LOG_DEBUG,
		"alu_init: limit.max-open-files = %lld",
		_limit_fn->cur_value (&(alu_sched->spec_limit)));
    }
  }

  {
    /* Stats refresh options */
    data_t *stats_refresh = dict_get (xl->options, "alu.stat-refresh.interval");
    if (stats_refresh) {
      alu_sched->refresh_interval = (int)gf_str_to_long_long (stats_refresh->data);  
    } else {
      alu_sched->refresh_interval = 5; // set to the default value
    }
    gettimeofday (&(alu_sched->last_stat_fetch), NULL);
    

    stats_refresh = dict_get (xl->options, "alu.stat-refresh.num-file-create");
    if (stats_refresh) {
      alu_sched->refresh_create_count = (int)gf_str_to_long_long (stats_refresh->data);
    } else {
      alu_sched->refresh_create_count = 5; // set to the default value
    }
  }

  {
    /* Build an array of child_nodes */
    struct alu_sched_struct *sched_array = NULL;
    xlator_list_t *trav_xl = xl->children;
    int32_t index = 0;
    while (trav_xl) {
      index++;
      trav_xl = trav_xl->next;
    }
    alu_sched->child_count = index;
    sched_array = calloc (index, sizeof (struct alu_sched_struct));

    trav_xl = xl->children;
    index = 0;
    while (trav_xl) {
      sched_array[index].xl = trav_xl->xlator;
      sched_array[index].eligible = 1;
      index++;
      trav_xl = trav_xl->next;
    }
    alu_sched->array = sched_array;
  }
  *((long *)xl->private) = (long)alu_sched;

  /* Initialize all the alu_sched structure's elements */
  {
    alu_sched->sched_nodes_pending = 0;

    alu_sched->min_limit.free_disk = 0xFFFFFFFF;
    alu_sched->min_limit.disk_usage = 0xFFFFFFFF;
    alu_sched->min_limit.disk_speed = 0xFFFFFFFF;
    alu_sched->min_limit.write_usage = 0xFFFFFFFF;
    alu_sched->min_limit.read_usage = 0xFFFFFFFF;
    alu_sched->min_limit.nr_files = 0xFFFFFFFF;
    alu_sched->min_limit.nr_clients = 0xFFFFFFFF;
  }

  pthread_mutex_init (&alu_sched->alu_mutex, NULL);
  return 0;
}

static void
alu_fini (struct xlator *xl)
{
  if (!xl)
    return;
  struct alu_sched *alu_sched = (struct alu_sched *)*((long *)xl->private);
  struct alu_limits *limit = alu_sched->limits_fn;
  struct alu_threshold *threshold = alu_sched->threshold_fn;
  void *tmp = NULL;
  pthread_mutex_destroy (&alu_sched->alu_mutex);
  free (alu_sched->array);
  while (limit) {
    tmp = limit;
    limit = limit->next;
    free (tmp);
  }
  while (threshold) {
    tmp = threshold;
    threshold = threshold->next;
    free (tmp);
  }
  free (alu_sched);
}

static int32_t 
update_stat_array_cbk (call_frame_t *frame,
		       call_frame_t *prev_frame,
		       xlator_t *xl,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct xlator_stats *trav_stats)
{
  struct alu_sched *alu_sched = (struct alu_sched *)*((long *)xl->private);
  struct alu_limits *limits_fn = alu_sched->limits_fn;
  int32_t idx = 0;
  
  // LOCK
  for (idx = 0; idx < alu_sched->child_count; idx++) {
    if (strcmp (alu_sched->array[idx].xl->name, prev_frame->this->name) == 0)
      break;
  }
  // UNLOCK
  if (op_ret == -1) {
    alu_sched->array[idx].eligible = 0;
  } else {
    memcpy (&(alu_sched->array[idx].stats), trav_stats, sizeof (struct xlator_stats));
    
    /* Get stats from all the child node */
    /* Here check the limits specified by the user to 
       consider the nodes to be used by scheduler */
    alu_sched->array[idx].eligible = 1;
    limits_fn = alu_sched->limits_fn;
    while (limits_fn){
      if (limits_fn->max_value && 
	  limits_fn->cur_value (trav_stats) > 
	  limits_fn->max_value (&(alu_sched->spec_limit))) {
	alu_sched->array[idx].eligible = 0;
      }
      if (limits_fn->min_value && 
	  limits_fn->cur_value (trav_stats) < 
	  limits_fn->min_value (&(alu_sched->spec_limit))) {
	alu_sched->array[idx].eligible = 0;
      }
      limits_fn = limits_fn->next;
    }

    /* Select minimum and maximum disk_usage */
    if (trav_stats->disk_usage > alu_sched->max_limit.disk_usage) {
      alu_sched->max_limit.disk_usage = trav_stats->disk_usage;
    }
    if (trav_stats->disk_usage < alu_sched->min_limit.disk_usage) {
      alu_sched->min_limit.disk_usage = trav_stats->disk_usage;
    }

    /* Select minimum and maximum disk_speed */
    if (trav_stats->disk_speed > alu_sched->max_limit.disk_speed) {
      alu_sched->max_limit.disk_speed = trav_stats->disk_speed;
    }
    if (trav_stats->disk_speed < alu_sched->min_limit.disk_speed) {
      alu_sched->min_limit.disk_speed = trav_stats->disk_speed;
    }

    /* Select minimum and maximum number of open files */
    if (trav_stats->nr_files > alu_sched->max_limit.nr_files) {
      alu_sched->max_limit.nr_files = trav_stats->nr_files;
    }
    if (trav_stats->nr_files < alu_sched->min_limit.nr_files) {
      alu_sched->min_limit.nr_files = trav_stats->nr_files;
    }

    /* Select minimum and maximum write-usage */
    if (trav_stats->write_usage > alu_sched->max_limit.write_usage) {
      alu_sched->max_limit.write_usage = trav_stats->write_usage;
    }
    if (trav_stats->write_usage < alu_sched->min_limit.write_usage) {
      alu_sched->min_limit.write_usage = trav_stats->write_usage;
    }

    /* Select minimum and maximum read-usage */
    if (trav_stats->read_usage > alu_sched->max_limit.read_usage) {
      alu_sched->max_limit.read_usage = trav_stats->read_usage;
    }
    if (trav_stats->read_usage < alu_sched->min_limit.read_usage) {
      alu_sched->min_limit.read_usage = trav_stats->read_usage;
    }

    /* Select minimum and maximum free-disk */
    if (trav_stats->free_disk > alu_sched->max_limit.free_disk) {
      alu_sched->max_limit.free_disk = trav_stats->free_disk;
    }
    if (trav_stats->free_disk < alu_sched->min_limit.free_disk) {
      alu_sched->min_limit.free_disk = trav_stats->free_disk;
    }
  }

  STACK_DESTROY (frame->root);

  return 0;
}

static void 
update_stat_array (xlator_t *xl)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched *alu_sched = (struct alu_sched *)*((long *)xl->private);
  int32_t idx = 0;

  for (idx = 0 ; idx < alu_sched->child_count; idx++) {
    call_ctx_t *cctx = calloc (1, sizeof (*cctx));
    cctx->frames.root  = cctx;
    cctx->frames.this  = xl;    
    
    STACK_WIND ((&cctx->frames), 
		update_stat_array_cbk, 
		alu_sched->array[idx].xl, 
		(alu_sched->array[idx].xl)->mops->stats,
		0); //flag
  }
  return;
}


static struct xlator *
alu_scheduler (struct xlator *xl, int32_t size)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched *alu_sched = (struct alu_sched *)*((long *)xl->private);
  int32_t sched_index = 0;
  int32_t sched_index_orig = 0;
  int32_t idx = 0;

  struct timeval tv;
  gettimeofday (&tv, NULL);
  if (tv.tv_sec > (alu_sched->refresh_interval + alu_sched->last_stat_fetch.tv_sec)) {
    /* Update the stats from all the server */
    update_stat_array (xl);
    alu_sched->last_stat_fetch.tv_sec = tv.tv_sec;
  }
  
  /* Now check each threshold one by one if some nodes are classified */
  {
    struct alu_threshold *trav_threshold = alu_sched->threshold_fn;
    struct alu_threshold *tmp_threshold = alu_sched->sched_method;
    struct alu_sched_node *tmp_sched_node;   

    /* This pointer 'trav_threshold' contains function pointers according to spec file
       give by user, */
    while (trav_threshold) {
      /* This check is needed for seeing if already there are nodes in this criteria 
         to be scheduled */
      if (!alu_sched->sched_nodes_pending) {
	for (idx = 0; idx < alu_sched->child_count; idx++) {
	  if (!alu_sched->array[idx].eligible) {
	    continue;
	  }
	  if (trav_threshold->entry_value) {
	    if (trav_threshold->diff_value (&(alu_sched->max_limit),
					    &(alu_sched->array[idx].stats)) <
		trav_threshold->entry_value (&(alu_sched->entry_limit))) {
	      continue;
	    }
	  }
	  tmp_sched_node = calloc (1, sizeof (struct alu_sched_node *));
	  tmp_sched_node->index = idx;
	  if (!alu_sched->sched_node) {
	    alu_sched->sched_node = tmp_sched_node;
	  } else {
	    pthread_mutex_lock (&alu_sched->alu_mutex);
	    tmp_sched_node->next = alu_sched->sched_node;
	    alu_sched->sched_node = tmp_sched_node;
	    pthread_mutex_unlock (&alu_sched->alu_mutex);
	  }
	  alu_sched->sched_nodes_pending++;
	}
      } /* end of if (sched_nodes_pending) */

      /* This loop is required to check the eligible nodes */
      struct alu_sched_node *trav_sched_node;
      while (alu_sched->sched_nodes_pending) {
	trav_sched_node = alu_sched->sched_node;
	sched_index = trav_sched_node->index;
	if (alu_sched->array[sched_index].eligible)
	  break;
	alu_sched->sched_node = trav_sched_node->next;
	free (trav_sched_node);
	alu_sched->sched_nodes_pending--;
      }
      if (alu_sched->sched_nodes_pending) {
	/* There are some node in this criteria to be scheduled, no need 
	 * to sort and check other methods 
	 */
	if (tmp_threshold && tmp_threshold->exit_value) {
	  /* verify the exit value && whether node is eligible or not */
	  if (tmp_threshold->diff_value (&(alu_sched->max_limit),
					  &(alu_sched->array[sched_index].stats)) >
	       tmp_threshold->exit_value (&(alu_sched->exit_limit))) {
	    /* Free the allocated info for the node :) */
	    pthread_mutex_lock (&alu_sched->alu_mutex);
	    alu_sched->sched_node = trav_sched_node->next;
	    free (trav_sched_node);
	    trav_sched_node = alu_sched->sched_node;
	    alu_sched->sched_nodes_pending--;
	    pthread_mutex_unlock (&alu_sched->alu_mutex);
	  }
	} else {
	  /* if there is no exit value, then exit after scheduling once */
	  pthread_mutex_lock (&alu_sched->alu_mutex);
	  alu_sched->sched_node = trav_sched_node->next;
	  free (trav_sched_node);
	  trav_sched_node = alu_sched->sched_node;
	  alu_sched->sched_nodes_pending--;
	  pthread_mutex_unlock (&alu_sched->alu_mutex);
	}
	
	alu_sched->sched_method = tmp_threshold; /* this is the method used for selecting */

	/* */
	if (trav_sched_node) {
	  tmp_sched_node = trav_sched_node;
	  while (trav_sched_node->next) {
	    trav_sched_node = trav_sched_node->next;
	  }
	  if (tmp_sched_node->next) {
	    pthread_mutex_lock (&alu_sched->alu_mutex);
	    alu_sched->sched_node = tmp_sched_node->next;
	    tmp_sched_node->next = NULL;
	    trav_sched_node->next = tmp_sched_node;
	    pthread_mutex_unlock (&alu_sched->alu_mutex);
	  }
	}
	/* return the scheduled node */
	return alu_sched->array[sched_index].xl;
      } /* end of if (pending_nodes) */
      
      tmp_threshold = trav_threshold;
      trav_threshold = trav_threshold->next;
    }
  }
  
  /* This is used only when there is everything seems ok, or no eligible nodes */
  sched_index_orig = alu_sched->sched_index;
  alu_sched->sched_method = NULL;
  while (1) {
    //lock
    pthread_mutex_lock (&alu_sched->alu_mutex);
    sched_index = alu_sched->sched_index++;
    alu_sched->sched_index = alu_sched->sched_index % alu_sched->child_count;
    pthread_mutex_unlock (&alu_sched->alu_mutex);
    //unlock
    if (alu_sched->array[sched_index].eligible)
      break;
    if (sched_index_orig == (sched_index + 1) % alu_sched->child_count) {
      gf_log ("alu", GF_LOG_WARNING, "No node is eligible to schedule");
      //lock
      pthread_mutex_lock (&alu_sched->alu_mutex);
      alu_sched->sched_index++;
      alu_sched->sched_index = alu_sched->sched_index % alu_sched->child_count;
      pthread_mutex_unlock (&alu_sched->alu_mutex);
      //unlock
      break;
    }
  }
  return alu_sched->array[sched_index].xl;
}

struct sched_ops sched = {
  .init     = alu_init,
  .fini     = alu_fini,
  .schedule = alu_scheduler
};
