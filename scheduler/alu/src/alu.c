#include "alu.h"

static int
alu_init (struct xlator *xl)
{
  //  gluster_log ("ALU Scheduler", GLUSTER_DEBUG, "Initializing..\n");

  struct alu_sched *alu_sched = calloc (1, sizeof (struct alu_sched));
  data_t *priority = dict_get (xl->options, "scheduler.priority");
  data_t *limits = dict_get (xl->options, "scheduler.limits");

  if (!priority) {
    fprintf (stderr, "ALU: Scheduler Priority not specified\n");
    exit (1);
  }
  
  if (!limits) {
    fprintf (stderr, "ALU: Scheduler limits not specified in the spec file\n");
    exit (1);
  }

  {
    /* Get the priority */
    printf ("Priority: %s\n", priority->data);
  }

  {
    /* Get the limits */
    printf ("Limits: %s\n", limits->data);
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

  return 0;
}

static void
alu_fini (struct xlator *xl)
{
  struct alu_sched *alu_sched = *((int *)xl->private);
  free(alu_sched->array);
  free(alu_sched);
}

static struct xlator *
alu_scheduler (struct xlator *xl, int size)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched *alu_sched = *((int *)xl->private);
  
  struct xlator *trav_xl = NULL;
  struct xlator *sched_xl = NULL;
  struct xlator_stats trav_stats = {0,};
  struct xlator_stats diff_stats = {0,};

  int min_du = 0xFFFFFFFF;
  int max_du = 0;
  int idx = 0;
  int sched_index =0;

  for (idx = 0 ; idx < alu_sched->child_count; idx++) {
    /* Get stats from all the child node */
    (alu_sched->array[idx].xl)->mgmt_ops->stats (alu_sched->array[idx].xl, &trav_stats);
    {
      alu_sched->array[idx].stats.nr_files   = trav_stats.nr_files;
      alu_sched->array[idx].stats.free_disk  = trav_stats.free_disk;
      alu_sched->array[idx].stats.disk_usage = trav_stats.disk_usage;
      alu_sched->array[idx].stats.disk_speed   = trav_stats.disk_speed;
      // others follow

      /*if (trav_stats.free_disk < option->free_disk)
	alu_sched->array[idx].eligible = 0 */
    }

    if (trav_stats.disk_usage > max_du)
      max_du = trav_stats.disk_usage;
    if (trav_stats.disk_usage < min_du) {
      min_du = trav_stats.disk_usage;
      sched_index = idx;
    }
  }

  return alu_sched->array[sched_index].xl;
}

struct sched_ops sched = {
  .init     = alu_init,
  .fini     = alu_fini,
  .schedule = alu_scheduler
};
