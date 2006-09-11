
#include "nufa.h"

static int
nufa_init (struct xlator *xl)
{
  struct nufa_struct *nufa_buf = calloc (1, sizeof (struct nufa_struct));
  struct xlator *trav_xl = xl->first_child;
  int index = 0;

  data_t *local_name = dict_get (xl->options, "nufa.local-volume-name");
  if (!local_name) {
    /* Error */
    gf_log ("nufa", GF_LOG_CRITICAL, "No 'local-volume-name' option given in spec file\n");
    exit (1);
  }
  while (trav_xl) {
    index++;
    if (strcmp (trav_xl->name, local_name->data) == 0)
      nufa_buf->sched_xl = trav_xl;
    trav_xl = trav_xl->next_sibling;
  }
  nufa_buf->child_count = index;
  trav_xl = xl->first_child;
  index = 0;
  
  *((int *)xl->private) = (int)nufa_buf; // put it at the proper place
  return 0;
}

static void
nufa_fini (struct xlator *xl)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((int *)xl->private);
  free (nufa_buf);
}

static struct xlator *
nufa_schedule (struct xlator *xl, int size)
{
  struct nufa_struct *nufa_buf = (struct nufa_struct *)*((int *)xl->private);
  return nufa_buf->sched_xl;
}

struct sched_ops sched = {
  .init     = nufa_init,
  .fini     = nufa_fini,
  .schedule = nufa_schedule
};
