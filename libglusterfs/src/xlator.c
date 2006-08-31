
#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include "defaults.h"

#define SET_DEFAULT(fn) do {        \
    if (!xl->fops->fn)              \
       xl->fops->fn = default_##fn; \
} while (0)

static void
fill_defaults (struct xlator *xl)
{
  if (!xl->getlayout)
    xl->getlayout = default_getlayout;

  if (!xl->setlayout)
    xl->setlayout = default_setlayout;

  SET_DEFAULT (open);
  SET_DEFAULT (getattr);
  SET_DEFAULT (readlink);
  SET_DEFAULT (mknod);
  SET_DEFAULT (mkdir);
  SET_DEFAULT (unlink);
  SET_DEFAULT (rmdir);
  SET_DEFAULT (symlink);
  SET_DEFAULT (rename);
  SET_DEFAULT (link);
  SET_DEFAULT (chmod);
  SET_DEFAULT (chown);
  SET_DEFAULT (truncate);
  SET_DEFAULT (utime);
  SET_DEFAULT (read);
  SET_DEFAULT (write);
  SET_DEFAULT (statfs);
  SET_DEFAULT (flush);
  SET_DEFAULT (release);
  SET_DEFAULT (fsync);
  SET_DEFAULT (setxattr);
  SET_DEFAULT (getxattr);
  SET_DEFAULT (listxattr);
  SET_DEFAULT (removexattr);
  SET_DEFAULT (opendir);
  SET_DEFAULT (readdir);
  SET_DEFAULT (releasedir);
  SET_DEFAULT (fsyncdir);
  SET_DEFAULT (access);
  SET_DEFAULT (ftruncate);
  SET_DEFAULT (fgetattr);
  SET_DEFAULT (bulk_getattr);
}

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
    fprintf (stderr, "dlsym(fops) on %s\n", dlerror ());
    exit (1);
  }
  if (!(xl->mgmt_ops = dlsym (handle, "mgmt_ops"))) {
    fprintf (stderr, "dlsym(mgmt_ops) on %s\n", dlerror ());
    exit (1);
  }

  if (!(xl->init = dlsym (handle, "init"))) {
    fprintf (stderr, "dlsym(init) on %s\n", dlerror ());
    exit (1);
  }

  if (!(xl->fini = dlsym (handle, "fini"))) {
    fprintf (stderr, "dlsym(fini) on %s\n", dlerror ());
    exit (1);
  }

  if (!(xl->getlayout = dlsym (handle, "getlayout"))) {
    xl->getlayout = default_getlayout;
  }

  if (!(xl->getlayout = dlsym (handle, "setlayout"))) {
    xl->setlayout = default_setlayout;
  }

  fill_defaults (xl);

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
