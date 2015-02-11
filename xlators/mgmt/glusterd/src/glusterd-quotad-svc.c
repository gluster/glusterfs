/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
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
#include "glusterd-quotad-svc.h"

char *quotad_svc_name = "quotad";

int glusterd_quotadsvc_init (glusterd_svc_t *svc)
{
        int              ret                = -1;
        char             volfile[PATH_MAX]  = {0,};
        glusterd_conf_t *conf               = THIS->private;

        ret = glusterd_svc_init (svc, quotad_svc_name,
                                 glusterd_quotadsvc_manager,
                                 glusterd_quotadsvc_start,
                                 glusterd_svc_stop);
        if (ret)
                goto out;

        /* glusterd_svc_build_volfile_path () doesn't put correct quotad volfile
         * path in proc object at service initialization. Re-initialize
         * the correct path
         */
        glusterd_quotadsvc_build_volfile_path (quotad_svc_name, conf->workdir,
                                               volfile, sizeof (volfile));
        snprintf (svc->proc.volfile, sizeof (svc->proc.volfile), "%s", volfile);
out:
        return ret;
}

static int
glusterd_quotadsvc_create_volfile ()
{
        char             filepath[PATH_MAX] = {0,};
        glusterd_conf_t *conf               = THIS->private;

        glusterd_quotadsvc_build_volfile_path (quotad_svc_name, conf->workdir,
                                               filepath, sizeof (filepath));
        return glusterd_create_global_volfile (build_quotad_graph,
                                               filepath, NULL);
}

int
glusterd_quotadsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = 0;
        glusterd_volinfo_t *volinfo = NULL;

        volinfo = data;

        /* If all the volumes are stopped or all shd compatible volumes
         * are stopped then stop the service if:
         * - volinfo is NULL or
         * - volinfo is present and volume is shd compatible
         * Otherwise create volfile and restart service if:
         * - volinfo is NULL or
         * - volinfo is present and volume is shd compatible
         */
        if (glusterd_are_all_volumes_stopped () ||
            glusterd_all_volumes_with_quota_stopped ()) {
                if (!(volinfo && !glusterd_is_volume_quota_enabled (volinfo))) {
                        ret = svc->stop (svc, SIGTERM);
                }
        } else {
                if (!(volinfo && !glusterd_is_volume_quota_enabled (volinfo))) {
                        ret = glusterd_quotadsvc_create_volfile ();
                        if (ret)
                                goto out;

                        ret = svc->stop (svc, SIGTERM);
                        if (ret)
                                goto out;

                        ret = svc->start (svc, flags);
                        if (ret)
                                goto out;

                        ret = glusterd_conn_connect (&(svc->conn));
                        if (ret)
                                goto out;
                }
        }
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_quotadsvc_start (glusterd_svc_t *svc, int flags)
{
        int              i         = 0;
        int              ret       = -1;
        dict_t          *cmdline   = NULL;
        char             key[16]   = {0};
        char            *options[] = {
                                      "*replicate*.entry-self-heal=off",
                                      "--xlator-option",
                                      "*replicate*.metadata-self-heal=off",
                                      "--xlator-option",
                                      "*replicate*.data-self-heal=off",
                                      "--xlator-option",
                                      NULL
                                      };

        cmdline = dict_new ();
        if (!cmdline)
                goto out;

        for (i = 0; options[i]; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "arg%d", i);
                ret = dict_set_str (cmdline, key, options[i]);
                if (ret)
                        goto out;
        }

        ret = glusterd_svc_start (svc, flags, cmdline);

out:
        if (cmdline)
                dict_unref (cmdline);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_quotadsvc_reconfigure ()
{
        return glusterd_svc_reconfigure (glusterd_quotadsvc_create_volfile);
}

void
glusterd_quotadsvc_build_volfile_path (char *server, char *workdir,
                                       char *volfile, size_t len)
{
        char  dir[PATH_MAX] = {0,};

        GF_ASSERT (len == PATH_MAX);

        glusterd_svc_build_svcdir (server, workdir, dir, sizeof (dir));
        snprintf (volfile, len, "%s/%s.vol", dir, server);
}
