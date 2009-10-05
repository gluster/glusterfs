/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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
#include "xlator.h"
#include "scheduler.h"
#include "list.h"

struct sched_ops *
get_scheduler (xlator_t *xl, const char *name)
{
	struct sched_ops  *tmp_sched = NULL;
	volume_opt_list_t *vol_opt   = NULL;
	char *sched_file             = NULL;
	void *handle                 = NULL;
        int   ret                    = 0;

	if (name == NULL) {
		gf_log ("scheduler", GF_LOG_ERROR, 
			"'name' not specified, EINVAL");
		return NULL;
	}
  
	ret = asprintf (&sched_file, "%s/%s.so", SCHEDULERDIR, name);
        if (-1 == ret) {
                gf_log ("scheduler", GF_LOG_ERROR, "asprintf failed");
                return NULL;
        }

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
  
	vol_opt = CALLOC (1, sizeof (volume_opt_list_t));
	vol_opt->given_opt = dlsym (handle, "options");
	if (vol_opt->given_opt == NULL) {
		gf_log ("scheduler", GF_LOG_DEBUG,
			"volume option validation not specified");
	} else {
		list_add_tail (&vol_opt->list, &xl->volume_options);
		if (validate_xlator_volume_options (xl, vol_opt->given_opt)
		    == -1) {
			gf_log ("scheduler", GF_LOG_ERROR,
				"volume option validation failed");
			return NULL;
		}
	}
	
	return tmp_sched;
}
