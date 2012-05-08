/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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

        ret = gf_asprintf (&sched_file, "%s/%s.so", SCHEDULERDIR, name);
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
                GF_FREE(sched_file);
                return NULL;
        }

        tmp_sched = dlsym (handle, "sched");
        if (!tmp_sched) {
                gf_log ("scheduler", GF_LOG_ERROR,
                        "dlsym(sched) on %s", dlerror ());
                GF_FREE(sched_file);
                return NULL;
        }

        vol_opt = GF_CALLOC (1, sizeof (volume_opt_list_t),
                             gf_common_mt_volume_opt_list_t);
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
                        GF_FREE(sched_file);
                        return NULL;
                }
        }
        GF_FREE(sched_file);
        GF_FREE (vol_opt);

        return tmp_sched;
}
