
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
#include <netdb.h>
#include "scheduler.h"

struct sched_ops *
get_scheduler (const char *name)
{
  struct sched_ops *tmp_sched = NULL;
  char *sched_file = NULL;
  void *handle = NULL;

  asprintf (&sched_file, "%s/%s.so", SCHEDULERDIR, name);
  gf_log ("libglusterfs", LOG_DEBUG, "scheduler.c->get_scheduler: attempt to load file %s.so\n",
	  name);

  handle = dlopen (sched_file, RTLD_LAZY);
  if (!handle) {
    gf_log ("libglusterfs", LOG_CRITICAL, "scheduler.c->get_scheduler: dlopen(%s): %s\n", 
	    sched_file, dlerror ());
    exit (1);
  }

  tmp_sched = dlsym (handle, "sched");
  if (!tmp_sched) {
    gf_log ("libglusterfs", LOG_CRITICAL,  "scheduler.c->get_scheduler: dlsym(sched) on %s\n", 
	    dlerror ());
    exit (1);
  }
  
  return tmp_sched;
}
