#include "alu.h"

static int
alu_init (struct xlator *xl)
{
  //  gluster_log ("ALU Scheduler", GLUSTER_DEBUG, "Initializing..\n");

  data_t *priority = dict_get (xl->options, "Scheduler.priority");
  data_t *limits = dict_get (xl->options, "Scheduler.limits");

  if (!priority) {
    fprintf (stderr, "ALU: Scheduler Priority not specified\n");
    exit (1);
  }
  
  if (!limits) {
    fprintf (stderr, "ALU: Scheduler limits not specified in the spec file\n");
    exit (1);
  }

  printf ("Priority: %s\n", priority->data);
  printf ("Limits: %s\n", limits->data);
  
  /* Build an array of child_nodes */
  {
    struct alu_sched_struct *sched_array = NULL;
    struct xlator *trav_xl = xl->first_child;
    int index = 0;
    while (trav_xl) {
      index++;
      trav_xl = trav_xl->next_sibling;
    }
    sched_array = calloc (index, sizeof (struct alu_sched_struct));
    trav_xl = xl->first_child;
    index = 0;
    while (trav_xl) {
      sched_array[index].xl = trav_xl;
      sched_array[index].eligible = 1;
      index++;
      trav_xl = trav_xl->next_sibling;
    }
    *((int *)xl->private) = sched_array;
  }

  return 0;
}

static void
alu_fini (struct xlator *xl)
{
  free (*((int *)xl->private));
}

static struct xlator *
alu_scheduler (struct xlator *xl, int size)
{
  /* This function schedules the file in one of the child nodes */
  struct alu_sched_struct *alu_sched = *((int *)xl->private);
  
  struct xlator *trav_xl = NULL;
  struct xlator *sched_xl = NULL;
  struct xlator_stats trav_stats = {0,};
  struct xlator_stats diff_stats = {0,};

  int min_du = 0xFFFFFFFF;
  int max_du = 0;
  int i = 0;
  int sched_index =0;

  trav_xl = xl->first_child;
  while (trav_xl) {
    /* Get stats from all the child node */
    trav_xl->mgmt_ops->stats (trav_xl, &trav_stats);
    {
      alu_sched[i].xl = trav_xl;
      alu_sched[i].stats.nr_files   = trav_stats.nr_files;
      alu_sched[i].stats.free_disk  = trav_stats.free_disk;
      alu_sched[i].stats.disk_usage = trav_stats.disk_usage;
      alu_sched[i].stats.disk_speed   = trav_stats.disk_speed;
      //alu_sched[i].stats.nr_files   = trav_stats.nr_files;
      //alu_sched[i].stats.nr_files   = trav_stats.nr_files;

      // others follow

      /*if (trav_stats.free_disk < option->free_disk)
	alu_sched[i].eligible = 0 */
    }

    if (trav_stats.disk_usage > max_du)
      max_du = trav_stats.disk_usage;
    if (trav_stats.disk_usage < min_du) {
      min_du = trav_stats.disk_usage;
      sched_index = i;
    }
    trav_xl = trav_xl->next_sibling;
    i++;
  }

  return alu_sched[sched_index].xl;
}

struct sched_struct sched = {
  .init     = alu_init,
  .fini     = alu_fini,
  .schedule = alu_scheduler
};
