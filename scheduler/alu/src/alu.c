#include "alu.h"

static int
get_stats_disk_usage (struct xlator_stats *this)
{
  return this->disk_usage;
}

static int
get_stats_write_usage (struct xlator_stats *this)
{
  return this->write_usage;
}

static int
get_stats_read_usage (struct xlator_stats *this)
{
  return this->read_usage;
}

static int
get_stats_disk_speed (struct xlator_stats *this)
{
  return this->disk_speed;
}

static int
get_stats_file_usage (struct xlator_stats *this)
{
  (void) &get_stats_file_usage;    /* Avoid warning "defined but not used" */

  return this->nr_files;
}

static int
get_stats_num_client (struct xlator_stats *this)
{
  (void) &get_stats_num_client;    /* Avoid warning "defined but not used" */
  return this->nr_clients;
}

static int
get_stats_free_disk (struct xlator_stats *this)
{
  (void) &get_stats_free_disk;    /* Avoid warning "defined but not used" */
  return this->free_disk;
}

static int
get_max_diff_write_usage (struct alu_sched *alu)
{
  return (alu->max_limit.write_usage - alu->min_limit.write_usage);
}

static int
get_max_diff_read_usage (struct alu_sched *alu)
{
  return (alu->max_limit.read_usage - alu->min_limit.read_usage);
}

static int
get_max_diff_disk_usage (struct alu_sched *alu)
{
  return (alu->max_limit.disk_usage - alu->min_limit.disk_usage);
}

static int
get_max_diff_disk_speed (struct alu_sched *alu)
{
  return (alu->max_limit.disk_speed - alu->min_limit.disk_speed);
}

static int
get_max_diff_file_usage (struct alu_sched *alu)
{
  return (alu->max_limit.nr_files - alu->min_limit.nr_files);
}

static int
get_max_diff_num_client (struct alu_sched *alu)
{
  (void) &get_max_diff_num_client;    /* Avoid warning "defined but not used" */
  return (alu->max_limit.nr_clients - alu->min_limit.nr_clients);
}

static int
get_max_diff_free_disk (struct alu_sched *alu)
{
  (void) &get_max_diff_free_disk;    /* Avoid warning "defined but not used" */        
  return (alu->max_limit.free_disk - alu->min_limit.free_disk);
}


static int
alu_init (struct xlator *xl)
{
  //  gf_log ("ALU Scheduler", GLUSTER_DEBUG, "Initializing..\n");

  struct alu_sched *alu_sched = calloc (1, sizeof (struct alu_sched));

  {
    data_t *order = dict_get (xl->options, "alu.order");
    if (!order) {
      fprintf (stderr, "ALU: Scheduler order not specified\n");
      exit (1);
    }
    struct alu_threshold *_threshold_fn;
    struct alu_threshold *tmp_threshold;
    data_t *entry_fn = NULL;
    data_t *exit_fn = NULL;
    char *tmp_str;
    char *order_str = strtok_r (order->data, ":", &tmp_str);
    while (order_str) {
      printf ("%s\n", order_str);
      if (strcmp (order_str, "disk-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_disk_usage;
	_threshold_fn->sched_value = get_stats_disk_usage;
	entry_fn = dict_get (xl->options, "alu.disk-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.disk_usage = 2*1024;
	} else {
	  alu_sched->entry_limit.disk_usage = strtol (entry_fn->data, NULL, 0);
	}
	_threshold_fn->entry_value = get_stats_disk_usage;
	exit_fn = dict_get (xl->options, "alu.disk-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.disk_usage = 512;
	} else {
	  alu_sched->exit_limit.disk_usage = strtol (exit_fn->data, NULL, 0);
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

      } else if (strcmp (order_str, "write-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_write_usage;
	_threshold_fn->sched_value = get_stats_write_usage;
	entry_fn = dict_get (xl->options, "alu.write-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.write_usage = 2*1024*1024;
	} else {
	  alu_sched->entry_limit.write_usage = strtol (entry_fn->data, NULL, 0);
	}
	_threshold_fn->entry_value = get_stats_write_usage;
	exit_fn = dict_get (xl->options, "alu.write-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.write_usage = 512*1024;
	} else {
	  alu_sched->exit_limit.write_usage = strtol (exit_fn->data, NULL, 0);
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

      } else if (strcmp (order_str, "read-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_read_usage;
	_threshold_fn->sched_value = get_stats_read_usage;
	entry_fn = dict_get (xl->options, "alu.read-usage.entry-threshold");
	if (!entry_fn) {
	  alu_sched->entry_limit.read_usage = 2*1024*1024;
	} else {
	  alu_sched->entry_limit.read_usage = strtol (entry_fn->data, NULL, 0);
	}
	_threshold_fn->entry_value = get_stats_read_usage;
	exit_fn = dict_get (xl->options, "alu.read-usage.exit-threshold");
	if (!exit_fn) {
	  alu_sched->exit_limit.read_usage = 512*1024;
	} else {
	  alu_sched->exit_limit.read_usage = strtol (exit_fn->data, NULL, 0);
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
	exit_fn = dict_get (xl->options, "alu.disk-usage.exit-threshold");
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

      } else if (strcmp (order_str, "disk-speed-usage") == 0) {
	_threshold_fn = calloc (1, sizeof (struct alu_threshold));
	_threshold_fn->diff_value = get_max_diff_disk_speed;
	_threshold_fn->sched_value = get_stats_disk_speed;
	entry_fn = dict_get (xl->options, "alu.disk-speed-usage.entry-threshold");
	if (entry_fn) {
	  printf ("Warning: entry-threshold is given for disk-speed, which is constant\n");
	}
	_threshold_fn->entry_value = NULL;
	exit_fn = dict_get (xl->options, "alu.disk-speed-usage.exit-threshold");
	if (exit_fn) {
	  printf ("Warning: exit-threshold is given for disk-speed, which is constant\n");
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
	printf ("alu: %s, unknown option provided to scheduler\n", order_str);
      }
      order_str = strtok_r (NULL, ":", &tmp_str);
    }
  }

  {
    /* Get the limits */
    struct alu_limits *_limit_fn = NULL;
    struct alu_limits *tmp_limits = NULL;
    data_t *limits = NULL;

    limits = dict_get (xl->options, "scheduler.limits.min-disk-free");
    if (limits) {
	_limit_fn = calloc (1, sizeof (struct alu_limits));
	_limit_fn->max_value = get_stats_free_disk;
	_limit_fn->cur_value = get_stats_free_disk;
	tmp_limits = alu_sched->limits_fn->next ;
	_limit_fn->next = tmp_limits;
	alu_sched->limits_fn = _limit_fn;
	alu_sched->spec_limit.disk_usage = strtol (limits->data, NULL, 0);
    }

    limits = dict_get (xl->options, "scheduler.limits.max-open-files");
    if (limits) {
	// Update alu_sched->priority properly
	_limit_fn = calloc (1, sizeof (struct alu_limits));
	_limit_fn->max_value = get_stats_file_usage;
	_limit_fn->cur_value = get_stats_file_usage;
	tmp_limits = alu_sched->limits_fn->next ;
	_limit_fn->next = tmp_limits;
	alu_sched->limits_fn = _limit_fn;
	alu_sched->spec_limit.nr_files = strtol (limits->data, NULL, 0);
    }
  }

  {
    /* Build an array of child_nodes */
    struct alu_sched_struct *sched_array = NULL;
    struct xlator *trav_xl = xl->first_child;
    int index = 0;
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
  *((int *)xl->private) = alu_sched;

  /* Initialize all the alu_sched structure's elements */
  {
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
  struct alu_sched *alu_sched = *((int *)xl->private);
  struct alu_limits *limit = alu_sched->limits_fn;
  struct alu_threshold *threshold = alu_sched->threshold_fn;
  void *tmp = NULL;
  free (alu_sched->array);
  while (limit) {
    tmp = limit;
    threshold = threshold->next;
    free (tmp);
  }
  while (threshold) {
    tmp = threshold;
    threshold = threshold->next;
    free (tmp);
  }
  free (alu_sched);
}

static struct xlator *
alu_scheduler (struct xlator *xl, int size)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched *alu_sched = *((int *)xl->private);
  struct alu_limits *limits_fn = alu_sched->limits_fn;
  struct xlator_stats trav_stats = {0,};
  int idx = 0;
  int sched_index =0;

  for (idx = 0 ; idx < alu_sched->child_count; idx++) {
    /* Get stats from all the child node */
    (alu_sched->array[idx].xl)->mgmt_ops->stats (alu_sched->array[idx].xl, &trav_stats);
    {
      alu_sched->array[idx].stats.nr_files   = trav_stats.nr_files;
      alu_sched->array[idx].stats.free_disk  = trav_stats.free_disk;
      alu_sched->array[idx].stats.disk_usage = trav_stats.disk_usage;
      alu_sched->array[idx].stats.disk_speed = trav_stats.disk_speed;
      alu_sched->array[idx].stats.write_usage = trav_stats.write_usage;
      alu_sched->array[idx].stats.read_usage = trav_stats.read_usage;
      alu_sched->array[idx].stats.nr_clients = trav_stats.nr_clients;

      // others follow
      limits_fn = alu_sched->limits_fn;
      while (limits_fn){
	if (limits_fn->cur_value (&trav_stats) > limits_fn->max_value (&(alu_sched->spec_limit)))
	  alu_sched->array[idx].eligible = 0;
	limits_fn = limits_fn->next;
      }
    }

    /* Select minimum and maximum disk_usage */
    if (trav_stats.disk_usage > alu_sched->max_limit.disk_usage) {
      alu_sched->max_limit.disk_usage = trav_stats.disk_usage;
    }
    if (trav_stats.disk_usage < alu_sched->min_limit.disk_usage) {
      alu_sched->min_limit.disk_usage = trav_stats.disk_usage;
      alu_sched->sched_node_idx.disk_usage = idx;
    }

    /* Select minimum and maximum disk_speed */
    if (trav_stats.disk_speed > alu_sched->max_limit.disk_speed) {
      alu_sched->max_limit.disk_speed = trav_stats.disk_speed;
      alu_sched->sched_node_idx.disk_speed = idx;
    }
    if (trav_stats.disk_speed < alu_sched->min_limit.disk_speed) {
      alu_sched->min_limit.disk_speed = trav_stats.disk_speed;
    }

    /* Select minimum and maximum number of open files */
    if (trav_stats.nr_files > alu_sched->max_limit.nr_files) {
      alu_sched->max_limit.nr_files = trav_stats.nr_files;
    }
    if (trav_stats.nr_files < alu_sched->min_limit.nr_files) {
      alu_sched->min_limit.nr_files = trav_stats.nr_files;
      alu_sched->sched_node_idx.nr_files = idx;
    }

    /* Select minimum and maximum write-usage */
    if (trav_stats.write_usage > alu_sched->max_limit.write_usage) {
      alu_sched->max_limit.write_usage = trav_stats.write_usage;
    }
    if (trav_stats.write_usage < alu_sched->min_limit.write_usage) {
      alu_sched->min_limit.write_usage = trav_stats.write_usage;
      alu_sched->sched_node_idx.write_usage = idx;
    }

    /* Select minimum and maximum read-usage */
    if (trav_stats.read_usage > alu_sched->max_limit.read_usage) {
      alu_sched->max_limit.read_usage = trav_stats.read_usage;
    }
    if (trav_stats.read_usage < alu_sched->min_limit.read_usage) {
      alu_sched->min_limit.read_usage = trav_stats.read_usage;
      alu_sched->sched_node_idx.read_usage = idx;
    }

    /* Select minimum and maximum free-disk */
    if (trav_stats.free_disk > alu_sched->max_limit.free_disk) {
      alu_sched->max_limit.free_disk = trav_stats.free_disk;
    }
    if (trav_stats.free_disk < alu_sched->min_limit.free_disk) {
      alu_sched->min_limit.free_disk = trav_stats.free_disk;
      alu_sched->sched_node_idx.free_disk = idx;
    }
  }

  /* Now check each threshold one by one if some nodes are classified */
  {
    struct alu_threshold *threshold = alu_sched->threshold_fn;
    struct alu_threshold *tmp_threshold = threshold;
    
    /* FIXME: As of now exit_value is not called */
    while (tmp_threshold) {
      if (tmp_threshold->entry_value) {
	if (tmp_threshold->diff_value (alu_sched) > 
	    tmp_threshold->entry_value (&(alu_sched->entry_limit))) {
	  sched_index = tmp_threshold->sched_value (&(alu_sched->sched_node_idx));
	  if (alu_sched->array[sched_index].eligible)
	    break;
	}
      } else {
	sched_index = tmp_threshold->sched_value (&(alu_sched->sched_node_idx));
      }
      tmp_threshold = tmp_threshold->next;
    }
  }
  return alu_sched->array[sched_index].xl;
}

struct sched_ops sched = {
  .init     = alu_init,
  .fini     = alu_fini,
  .schedule = alu_scheduler
};
