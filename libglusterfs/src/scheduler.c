#include <dlfcn.h>
#include <netdb.h>
#include "scheduler.h"

struct sched_ops *
get_scheduler (const char *name)
{
  struct sched_ops *tmp_sched = NULL;
  char *sched_file = NULL;
  void *handle = NULL;

  asprintf (&sched_file, "%s/%s.so", SCHEDULERDIR, name);
  printf ("Attempt to load file %s.so\n", name);

  handle = dlopen (sched_file, RTLD_LAZY);
  if (!handle) {
    fprintf (stderr, "dlopen(%s): %s\n", sched_file, dlerror ());
    exit (1);
  }

  tmp_sched = dlsym (handle, "sched");
  if (!tmp_sched) {
    fprintf (stderr, "dlsym(sched) on %s\n", dlerror ());
    exit (1);
  }
  
  return tmp_sched;
}
