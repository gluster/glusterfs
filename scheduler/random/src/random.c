  index = 0;

  while (trav_xl) {
    random_buf->array[index].xl = trav_xl;
    random_buf->array[index].eligible = 1;
    trav_xl = trav_xl->next_sibling;
    index++;
  }
  
  *((int *)xl->private) = random_buf; // put it at the proper place
  return 0;
}

static void
random_fini (struct xlator *xl)
{
  struct random_struct *random_buf = *((int *)xl->private);
  free (random_buf->array);
  free (random_buf);
}

static struct xlator *
random_schedule (struct xlator *xl, int size)
{
  struct random_struct *random_buf = *((int *)xl->private);
  int rand = random () % random_buf->child_count;
  while (!random_buf->array[rand].eligible) {
    rand = random () % random_buf->child_count;
  }
  return random_buf->array[rand].xl;
}

struct sched_ops sched = {
  .init     = random_init,
  .fini     = random_fini,
  .schedule = random_schedule
};
