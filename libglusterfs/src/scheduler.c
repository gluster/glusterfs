/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <netdb.h>
#include "scheduler.h"

struct sched_ops *
get_scheduler (const char *name)
{
  struct sched_ops *tmp_sched = NULL;
  char *sched_file = NULL;
  void *handle = NULL;
  
  if (name == NULL)
    {
      gf_log ("scheduler", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }
  
  asprintf (&sched_file, "%s/%s.so", SCHEDULERDIR, name);

  gf_log ("scheduler", GF_LOG_DEBUG,
	  "attempt to load file %s.so", name);

  handle = dlopen (sched_file, RTLD_LAZY);
  if (!handle) {
    gf_log ("scheduler", GF_LOG_ERROR,
	    "dlopen(%s): %s", sched_file, dlerror ());
    return NULL;
  }

  tmp_sched = dlsym (handle, "sched");
  if (!tmp_sched) {
    gf_log ("scheduler", GF_LOG_ERROR,
	    "dlsym(sched) on %s", dlerror ());
    return NULL;
  }
  
  return tmp_sched;
}
