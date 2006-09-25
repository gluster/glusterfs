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

#include "alu.h"
#include <sys/time.h>

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

static int64_t
str_to_long_int64_t (const char *number)
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
alu_init (struct xlator *xl)
{
  //  gf_log ("ALU Scheduler", GLUSTER_DEBUG, "Initializing..\n");

  struct alu_sched *alu_sched = calloc (1, sizeof (struct alu_sched));

  {
    /* Set the seed for the 'random' function */
    srandom ((uint32_t) time (NULL));
  }

  {
    data_t *order = dict_get (xl->options, "alu.order");
    if (!order) {
      gf_log ("alu", GF_LOG_ERROR, "alu.c->alu_init: order not specified\n");
      exit (1);
    }
    struct alu_threshold *_threshold_fn;
    struct alu_threshold *tmp_threshold;
    data_t *entry_fn = NULL;
    data_t *exit_fn = NULL;
    char *tmp_str;
    char *order_str = strtok_r (order->data, ":", &tmp_str);
    while (order_str) {
      gf_log ("alu", GF_LOG_ERROR, "alu.c->alu_init: order string: %s", order_str);
      if (strcmp (order_str, "disk-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_disk_usage;
	_threshold_fn->sched_value = get_stats_disk_usage;
	entry_fn = dict_get (xl->options, "alu.disk-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.disk_usage = 1024 * 1024 * 1024; /* Byte Unit */
	} else {
	  alu_sched->entry_limit.disk_usage = str_to_long_int64_t (entry_fn->data);
	}
	_threshold_fn->entry_value = get_stats_disk_usage;
	exit_fn = dict_get (xl->options, "alu.disk-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.disk_usage = 512 * 1024 * 1024;
	} else {
	  alu_sched->exit_limit.disk_usage = str_to_long_int64_t (exit_fn->data);
	}
	_threshold_fn->exit_value = get_stats_disk_usage;
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
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: = %lld,%lld\n", 
		alu_sched->entry_limit.disk_usage, 
		alu_sched->exit_limit.disk_usage);

      } else if (strcmp (order_str, "write-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_write_usage;
	_threshold_fn->sched_value = get_stats_write_usage;
	entry_fn = dict_get (xl->options, "alu.write-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.write_usage = 25;
	} else {
	  alu_sched->entry_limit.write_usage = str_to_long_int64_t (entry_fn->data);
	}
	_threshold_fn->entry_value = get_stats_write_usage;
	exit_fn = dict_get (xl->options, "alu.write-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.write_usage = 5;
	} else {
	  alu_sched->exit_limit.write_usage = str_to_long_int64_t (exit_fn->data);
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
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: = %lld,%lld\n", 
		alu_sched->entry_limit.write_usage, 
		alu_sched->exit_limit.write_usage);

      } else if (strcmp (order_str, "read-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_read_usage;
	_threshold_fn->sched_value = get_stats_read_usage;
	entry_fn = dict_get (xl->options, "alu.read-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.read_usage = 25;
	} else {
	  alu_sched->entry_limit.read_usage = str_to_long_int64_t (entry_fn->data);
	}
	_threshold_fn->entry_value = get_stats_read_usage;
	exit_fn = dict_get (xl->options, "alu.read-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.read_usage = 5;
	} else {
	  alu_sched->exit_limit.read_usage = str_to_long_int64_t (exit_fn->data);
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
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: = %lld,%lld\n", 
		alu_sched->entry_limit.read_usage, 
		alu_sched->exit_limit.read_usage);

      } else if (strcmp (order_str, "open-files-usage") == 0) {
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
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_disk_speed;
	_threshold_fn->sched_value = get_stats_disk_speed;
	entry_fn = dict_get (xl->options, "alu.disk-speed-usage.entry-threshold");
	if (entry_fn) {
	  gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: entry-threshold is given for disk-speed, \
which is constant\n");
	}
	_threshold_fn->entry_value = NULL;
	exit_fn = dict_get (xl->options, "alu.disk-speed-usage.exit-threshold");
	if (exit_fn) {
	  gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: exit-threshold is given for disk-speed, \
which is constant\n");
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
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: %s, unknown option provided to scheduler\n", 
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
	alu_sched->spec_limit.free_disk = str_to_long_int64_t (limits->data);
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: limit.min-disk-free = %lld\n", 
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
	alu_sched->spec_limit.nr_files = str_to_long_int64_t (limits->data);
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_init: limit.max-open-files = %lld\n", 
		_limit_fn->cur_value (&(alu_sched->spec_limit)));
    }
  }

  {
    /* Stats refresh options */
    data_t *stats_refresh = dict_get (xl->options, "alu.stat-refresh.interval");
    if (stats_refresh) {
      alu_sched->refresh_interval = (int)str_to_long_int64_t (stats_refresh->data);  
    } else {
      alu_sched->refresh_interval = 5; // set to the default value
    }
    gettimeofday (&(alu_sched->last_stat_fetch), NULL);
    

    stats_refresh = dict_get (xl->options, "alu.stat-refresh.num-file-create");
    if (stats_refresh) {
      alu_sched->refresh_create_count = (int)str_to_long_int64_t (stats_refresh->data);
    } else {
      alu_sched->refresh_create_count = 5; // set to the default value
    }
  }

  {
    /* Build an array of child_nodes */
    struct alu_sched_struct *sched_array = NULL;
    struct xlator *trav_xl = xl->first_child;
    int32_t index = 0;
    while (trav_xl) {
      index++;
      trav_xl = trav_xl->next_sibling;
    }
    alu_sched->child_count = index;
    sched_array = calloc (index, sizeof (struct alu_sched_struct));

    trav_xl = xl->first_child;
    index = 0;
    while (trav_xl) {
      sched_array[index].xl = trav_xl;
      sched_array[index].eligible = 1;
      index++;
      trav_xl = trav_xl->next_sibling;
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

static void 
update_stat_array (struct xlator *xl)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched *alu_sched = (struct alu_sched *)*((long *)xl->private);
  struct alu_limits *limits_fn = alu_sched->limits_fn;
  struct xlator_stats *trav_stats;
  int32_t idx = 0;

  for (idx = 0 ; idx < alu_sched->child_count; idx++) {
    /* Get stats from all the child node */
    trav_stats = &(alu_sched->array[idx]).stats;
    (alu_sched->array[idx].xl)->mgmt_ops->stats (alu_sched->array[idx].xl, trav_stats);
    {
      /* Here check the limits specified by the user to 
	 consider the file to be used by scheduler */
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
  return;
}

static struct xlator *
alu_scheduler (struct xlator *xl, int32_t size)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched *alu_sched = (struct alu_sched *)*((long *)xl->private);
  int32_t sched_index =0;
  int32_t idx = 0;

  struct timeval tv;
  gettimeofday (&tv, NULL);
  if (tv.tv_sec > (alu_sched->refresh_interval + alu_sched->last_stat_fetch.tv_sec)) {
  /* Update the stats from all the server */
    update_stat_array (xl);
  }

  /* Now check each threshold one by one if some nodes are classified */
  {
    struct alu_threshold *trav_threshold = alu_sched->threshold_fn;
    struct alu_threshold *tmp_threshold = alu_sched->sched_method;
    struct alu_sched_node *tmp_sched_node;   

    /* This pointer 'trav_threshold' contains function pointers according to spec file
       give by user, */
    while (trav_threshold) {
      if (alu_sched->sched_nodes_pending) {
	/* There are some node in this criteria to be scheduled, no need 
	 * to sort and check other methods 
	 */
	int32_t _index = random () % alu_sched->sched_nodes_pending;
	struct alu_sched_node *trav_sched_node = alu_sched->sched_node;
	tmp_sched_node = trav_sched_node;
	while (_index) {
	  /* this is to get the _index'th item */
	  trav_sched_node = trav_sched_node->next;
	  _index--;
	}
	sched_index = trav_sched_node->index; // this is the actual scheduled node
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_scheduler: scheduled to %d\n", sched_index);
	/*gf_log ("alu", GF_LOG_NORMAL, "File scheduled to %s sub-volume\n", 
	  alu_sched->array[sched_index].xl->name );
	  gf_log ("alu", GF_LOG_DEBUG, "stats max = %d, sched = %d\n", 
	  tmp_threshold->exit_value (&(alu_sched->max_limit)), 
	  tmp_threshold->exit_value (&(alu_sched->array[sched_index].stats))); */
	if (tmp_threshold && tmp_threshold->exit_value) {
	  /* verify the exit value */
	  if (tmp_threshold->diff_value (&(alu_sched->max_limit),
					 &(alu_sched->array[sched_index].stats)) >
	      tmp_threshold->exit_value (&(alu_sched->exit_limit))) {
	    tmp_sched_node = trav_sched_node; // used for free
	    trav_sched_node = tmp_sched_node->next;
	    free (tmp_sched_node);
	    alu_sched->sched_nodes_pending--;
	  }
	} else {
	  tmp_sched_node = trav_sched_node; // used for free
	  trav_sched_node = tmp_sched_node->next;
	  free (tmp_sched_node);
	  alu_sched->sched_nodes_pending--;
	}
	alu_sched->sched_method = tmp_threshold; /* this is the method used for selecting */
	return alu_sched->array[sched_index].xl;
      }
      
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
	gf_log ("alu", GF_LOG_DEBUG, "alu.c->alu_schedule: scheduling some nodes-> %d\n", idx);
	tmp_sched_node = calloc (1, sizeof (struct alu_sched_node *));
	tmp_sched_node->index = idx;
	if (!alu_sched->sched_node) {
	  alu_sched->sched_node = tmp_sched_node;
	} else {
	  tmp_sched_node->next = alu_sched->sched_node;
	  alu_sched->sched_node = tmp_sched_node;
	}
	alu_sched->sched_nodes_pending++;
      }
      tmp_threshold = trav_threshold;
      trav_threshold = trav_threshold->next;
    }
  }
  sched_index = random () % alu_sched->child_count;
  alu_sched->sched_method = NULL;
  return alu_sched->array[sched_index].xl;
}

struct sched_ops sched = {
  .init     = alu_init,
  .fini     = alu_fini,
  .schedule = alu_scheduler
};

