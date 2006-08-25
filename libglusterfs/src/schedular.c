#include <dlfcn.h>
#include <netdb.h>
#include "schedular.h"

struct sched_struct *
get_schedular (const char *name)
{
  struct sched_struct *tmp_sched = NULL;
  char *sched_file = NULL;
  void *handle = NULL;

  asprintf (&sched_file, "%s/%s.so", SCHEDULARDIR, name);
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
