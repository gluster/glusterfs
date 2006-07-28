
#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>

void
xlator_set_type (struct xlator *xl, 
		 const char *type)
{
  char *name = NULL;
  void *handle = NULL;

  printf ("Attempt to load type %s\n", type);
  asprintf (&name, "%s/%s.so", XLATORDIR, type);
  printf ("Attempt to load file %s\n", name);


  handle = dlopen (name, RTLD_LAZY);
  if (!handle) {
    fprintf (stderr, "dlopen(%s): %s\n", name, dlerror ());
    exit (1);
  }

  if (!(xl->fops = dlsym (handle, "fops"))) {
    fprintf (stderr, "dlsym(fops) on %s: %s\n", dlerror (), name);
    exit (1);
  }
  if (!(xl->init = dlsym (handle, "init"))) {
    fprintf (stderr, "dlsym(init) on %s: %s\n", dlerror (), name);
    exit (1);
  }
  if (!(xl->fini = dlsym (handle, "fini"))) {
    fprintf (stderr, "dlsym(fini) on %s: %s\n", dlerror (), name);
    exit (1);
  }
  free (name);
  return ;
}

in_addr_t
resolve_ip (const char *hostname)
{
  in_addr_t addr;
  struct hostent *h = gethostbyname (hostname);
  if (!h)
    return INADDR_NONE;
  memcpy (&addr, h->h_addr, h->h_length);

  return addr;
}
