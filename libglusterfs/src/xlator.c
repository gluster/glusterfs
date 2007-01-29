/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#define SET_DEFAULT_FOP(fn) do {    \
    if (!xl->fops->fn)              \
       xl->fops->fn = default_##fn; \
} while (0)

#define SET_DEFAULT_MOP(fn) do {        \
    if (!xl->mops->fn)                  \
       xl->mops->fn = default_##fn;     \
} while (0)

static void
fill_defaults (struct xlator *xl)
{
  SET_DEFAULT_FOP (create);
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
  SET_DEFAULT_FOP (utimes);
  SET_DEFAULT_FOP (read);
  SET_DEFAULT_FOP (writev);
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
  SET_DEFAULT_FOP (lk);

  SET_DEFAULT_MOP (stats);
  SET_DEFAULT_MOP (lock);
  SET_DEFAULT_MOP (unlock);
  SET_DEFAULT_MOP (listlocks);
  SET_DEFAULT_MOP (nslookup);
  SET_DEFAULT_MOP (nsupdate);

  return;
}

void
xlator_set_type (struct xlator *xl, 
		 const char *type)
{
  char *name = NULL;
  void *handle = NULL;

  gf_log ("libglusterfs/xlator",
	  GF_LOG_DEBUG,
	  "xlator_set_type: attempt to load type %s",
	  type);

  asprintf (&name, "%s/%s.so", XLATORDIR, type);

  gf_log ("libglusterfs/xlator",
	  GF_LOG_DEBUG,
	  " xlator_set_type: attempt to load file %s",
	  name);

  handle = dlopen (name, RTLD_LAZY);

  if (!handle) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "xlator_set_type: dlopen(%s): %s", 
	    name, dlerror ());
    exit (1);
  }

  if (!(xl->fops = dlsym (handle, "fops"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "xlator_set_type: dlsym(fops) on %s",
	    dlerror ());
    exit (1);
  }
  if (!(xl->mops = dlsym (handle, "mops"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "xlator_set_type: dlsym(mops) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "xlator_set_type: dlsym(init) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "xlator_set_type: dlsym(fini) on %s",
	    dlerror ());
    exit (1);
  }

  fill_defaults (xl);

  free (name);
  return ;
}

static void
_foreach_dfs (struct xlator *this,
	      void (*fn)(struct xlator *each,
			 void *data),
	      void *data)
{
  xlator_list_t *child = this->children;

  while (child) {
    _foreach_dfs (child->xlator, fn, data);
    child = child->next;
  }

  fn (this, data);
}

void
xlator_foreach (struct xlator *this,
		void (*fn)(struct xlator *each,
			   void *data),
		void *data)
{
  struct xlator *first;

  first = this;

  while (first->prev)
    first = first->prev;

  while (first) {
    fn (first, data);
    first = first->next;
  }
}
