    if (strcmp (trav_xl->name, local_name->data) == 0)
      nufa_buf->sched_xl = trav_xl;
    trav_xl = trav_xl->next_sibling;
  }
  nufa_buf->child_count = index;
  trav_xl = xl->first_child;
  index = 0;
  
  *((int *)xl->private) = nufa_buf; // put it at the proper place
  return 0;
}

static void
nufa_fini (struct xlator *xl)
{
  struct random_struct *nufa_buf = *((int *)xl->private);
  free (nufa_buf);
}

static struct xlator *
nufa_schedule (struct xlator *xl, int size)
{
  struct nufa_struct *nufa_buf = *((int *)xl->private);
  return nufa_buf->sched_xl;
}

struct sched_ops sched = {
  .init     = nufa_init,
  .fini     = nufa_fini,
  .schedule = nufa_schedule
};
