/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include "defaults.h"

#define SET_DEFAULT_FOP(fn) do {        \
    if (!xl->fops->fn)              \
       xl->fops->fn = default_##fn; \
} while (0)

#define SET_DEFAULT_MGMT_OP(fn) do {        \
    if (!xl->mgmt_ops->fn)              \
       xl->mgmt_ops->fn = default_##fn; \
} while (0)

static void
fill_defaults (struct xlator *xl)
{
  SET_DEFAULT_FOP (open);
  SET_DEFAULT_FOP (getattr);
  SET_DEFAULT_FOP (readlink);
  SET_DEFAULT_FOP (mknod);
  SET_DEFAULT_FOP (mkdir);
  SET_DEFAULT_FOP (unlink);
  SET_DEFAULT_FOP (rmdir);
  SET_DEFAULT_FOP (symlink);
  SET_DEFAULT_FOP (rename);
  SET_DEFAULT_FOP (link);
  SET_DEFAULT_FOP (chmod);
  SET_DEFAULT_FOP (chown);
  SET_DEFAULT_FOP (truncate);
  SET_DEFAULT_FOP (utime);
  SET_DEFAULT_FOP (read);
  SET_DEFAULT_FOP (write);
  SET_DEFAULT_FOP (statfs);
  SET_DEFAULT_FOP (flush);
  SET_DEFAULT_FOP (release);
  SET_DEFAULT_FOP (fsync);
  SET_DEFAULT_FOP (setxattr);
  SET_DEFAULT_FOP (getxattr);
  SET_DEFAULT_FOP (listxattr);
  SET_DEFAULT_FOP (removexattr);
  SET_DEFAULT_FOP (opendir);
  SET_DEFAULT_FOP (readdir);
  SET_DEFAULT_FOP (releasedir);
  SET_DEFAULT_FOP (fsyncdir);
  SET_DEFAULT_FOP (access);
  SET_DEFAULT_FOP (ftruncate);
  SET_DEFAULT_FOP (fgetattr);
  SET_DEFAULT_FOP (bulk_getattr);

  SET_DEFAULT_MGMT_OP (stats);
  SET_DEFAULT_MGMT_OP (lock);
  SET_DEFAULT_MGMT_OP (unlock);
  SET_DEFAULT_MGMT_OP (nslookup);
  SET_DEFAULT_MGMT_OP (nsupdate);

  return;
}

void
xlator_set_type (struct xlator *xl, 
		 const char *type)
{
  char *name = NULL;
  void *handle = NULL;

  gf_log ("libglusterfs", GF_LOG_DEBUG, "xlator.c->xlator_set_type: attempt to load type %s\n", type);
  asprintf (&name, "%s/%s.so", XLATORDIR, type);
  gf_log ("libglusterfs", GF_LOG_DEBUG, "xlator.c->xlator_set_type: attempt to load file %s\n", name);


  handle = dlopen (name, RTLD_LAZY);

  if (!handle) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "xlator.c->xlator_set_type: dlopen(%s): %s\n", 
	    name, dlerror ());
    exit (1);
  }

  if (!(xl->fops = dlsym (handle, "fops"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym(fops) on %s\n", dlerror ());
    exit (1);
  }
  if (!(xl->mgmt_ops = dlsym (handle, "mgmt_ops"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym(mgmt_ops) on %s\n", dlerror ());
    exit (1);
  }

  if (!(xl->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym(init) on %s\n", dlerror ());
    exit (1);
  }

  if (!(xl->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym(fini) on %s\n", dlerror ());
    exit (1);
  }

  if (!(xl->getlayout = dlsym (handle, "getlayout"))) {
    gf_log ("libglusterfs", GF_LOG_NORMAL, "type=%s getlayout=default_getlayout", xl->name);
    xl->getlayout = default_getlayout;
  }

  if (!(xl->setlayout = dlsym (handle, "setlayout"))) {
    gf_log ("libglusterfs", GF_LOG_NORMAL, "type=%s setlayout=default_setlayout", xl->name);
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

static void
_foreach_dfs (struct xlator *this,
	      void (*fn)(struct xlator *each))
{
  struct xlator *child = this->first_child;

  while (child) {
    _foreach_dfs (child, fn);
    child = child->next_sibling;
  }

  fn (this);
}

void
foreach_xlator (struct xlator *this,
		void (*fn)(struct xlator *each))
{
  struct xlator *top;

  top = this;

  while (top->parent)
    top = top->parent;

  _foreach_dfs (top, fn);
}
