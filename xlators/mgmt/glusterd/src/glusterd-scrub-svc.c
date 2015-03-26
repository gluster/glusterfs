/*
  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "globals.h"
#include "run.h"
#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-scrub-svc.h"

char *scrub_svc_name = "scrub";

int
glusterd_scrubsvc_init (glusterd_svc_t *svc)
{
        return glusterd_svc_init (svc, scrub_svc_name,
                                  glusterd_scrubsvc_manager,
                                  glusterd_scrubsvc_start,
                                  glusterd_scrubsvc_stop);
}

static int
glusterd_scrubsvc_create_volfile ()
{
        char              filepath[PATH_MAX] = {0,};
        int               ret                = -1;
        glusterd_conf_t   *conf              = NULL;
        dict_t            *mod_dict          = NULL;
        xlator_t          *this              = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);


        mod_dict = dict_new ();
        if (!mod_dict) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate new "
                        "dict");
                goto out;
        }

        ret = dict_set_uint32 (mod_dict, "trusted-client", GF_CLIENT_TRUSTED);
        if (ret)
                goto free_dict;

        glusterd_svc_build_volfile_path (scrub_svc_name, conf->workdir,
                                         filepath, sizeof (filepath));

        ret = glusterd_create_global_volfile (build_scrub_graph,
                                              filepath, mod_dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create volfile");
                goto free_dict;
        }

free_dict:
        dict_unref (mod_dict);
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_scrubsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int          ret    = -EINVAL;

        if (glusterd_all_volumes_with_bitrot_stopped ()) {
                ret = svc->stop (svc, SIGTERM);
        } else {
                ret = glusterd_scrubsvc_create_volfile ();
                if (ret)
                        goto out;

                ret = svc->stop (svc, SIGKILL);
                if (ret)
                        goto out;

                ret = svc->start (svc, flags);
                if (ret)
                        goto out;

                ret = glusterd_conn_connect (&(svc->conn));
                if (ret)
                        goto out;
        }

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_scrubsvc_start (glusterd_svc_t *svc, int flags)
{
        return glusterd_svc_start (svc, flags, NULL);
}

int
glusterd_scrubsvc_stop (glusterd_svc_t *svc, int sig)
{
        return glusterd_svc_stop (svc, sig);
}

int
glusterd_scrubsvc_reconfigure ()
{
        return glusterd_svc_reconfigure (glusterd_scrubsvc_create_volfile);
}
