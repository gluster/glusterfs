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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>

#include "logging.h"
#include "transport.h"

struct transport *
transport_new (dict_t *options)
{
  struct transport *trans = calloc (1, sizeof (struct transport));

  char *type = dict_get (options, "type"); // transport type, e.g., "tcp"
  char *name = NULL;
  void *handle = NULL;

  gf_log ("libglusterfs", GF_LOG_DEBUG, "transport.c: transport_new: attempt to load type %s", type);
  asprintf (&name, "%s/%s.so", TRANSPORTDIR, type);
  gf_log ("libglusterfs", GF_LOG_DEBUG, "transport.c: transport_new: attempt to load file %s", name);

  handle = dlopen (name, RTLD_LAZY);

  if (!handle) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "transport.c: transport_new: dlopen (%s): %s",
	    name, dlerror ());
    exit (1);
  };

  if (!(trans->transport_ops = dlsym (handle, "transport_ops"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym (transport_ops) on %s", dlerror ());
    exit (1);
  }

  if (!(trans->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym (init) on %s", dlerror ());
    exit (1);
  }

  if (!(trans->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dlsym (fini) on %s", dlerror ());
    exit (1);
  }

  if (trans->init (trans) != 0) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "transport '%s' initialization failed", type);
    exit (1);
  }

  free (name);
  return trans;
}
