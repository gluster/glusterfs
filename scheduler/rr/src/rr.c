#include <stdlib.h>
#include "rr.h"

static int
rr_init (struct xlator *xl)
{
  struct rr_struct *rr_buf = calloc (1, sizeof (struct rr_struct));
  struct xlator *trav_xl = xl->first_child;
  
  int index = 0;

  while (trav_xl) {
    index++;
    trav_xl = trav_xl->next_sibling;
  }
  rr_buf->child_count = index;
  rr_buf->sched_index = 0;
  rr_buf->array = calloc (index, sizeof (struct rr_sched_struct));
  trav_xl = xl->first_child;
  index = 0;

  while (trav_xl) {
    rr_buf->array[index].xl = trav_xl;
    rr_buf->array[index].eligible = 1;
    trav_xl = trav_xl->next_sibling;
    index++;
  }
  pthread_mutex_init (&rr_buf->rr_mutex, NULL);
  *((int *)xl->private) = (int)rr_buf; // put it at the proper place
  return 0;
}

static void
rr_fini (struct xlator *xl)
{
  struct rr_struct *rr_buf = (struct rr_struct *)*((int *)xl->private);
  free (rr_buf->array);
  free (rr_buf);
}

static struct xlator *
rr_schedule (struct xlator *xl, int size)
{
  struct rr_struct *rr_buf = (struct rr_struct *)*((int *)xl->private);
  int rr;
  
  pthread_mutex_lock (&rr_buf->rr_mutex);
  rr = rr_buf->sched_index++;
  rr_buf->sched_index = rr_buf->sched_index % rr_buf->child_count;
  pthread_mutex_unlock (&rr_buf->rr_mutex);
  return rr_buf->array[rr].xl;
}

struct sched_ops sched = {
  .init     = rr_init,
  .fini     = rr_fini,
  .schedule = rr_schedule
};
