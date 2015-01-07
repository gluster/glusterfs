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
#include "glusterd-bitdsvc.h"

char *bitd_svc_name = "bitd";

int
glusterd_bitdsvc_init (glusterd_svc_t *svc)
{
        return glusterd_svc_init (svc, bitd_svc_name,
                                  glusterd_bitdsvc_manager,
                                  glusterd_bitdsvc_start,
                                  glusterd_bitdsvc_stop);
}

static int
glusterd_bitdsvc_create_volfile ()
{
        char              filepath[PATH_MAX] = {0,};
        int               ret                = -1;
        glusterd_conf_t   *conf              = NULL;

        conf = THIS->private;
        GF_ASSERT (conf);


        glusterd_svc_build_volfile_path (bitd_svc_name, conf->workdir,
                                         filepath, sizeof (filepath));

        ret = glusterd_create_global_volfile (build_bitd_graph,
                                              filepath, NULL);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to create volfile");
                goto out;
        }
out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_bitdsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int          ret    = -1;

        if (glusterd_are_all_volumes_stopped ()) {
                ret = svc->stop (svc, SIGKILL);
        } else {
                ret = glusterd_bitdsvc_create_volfile ();
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
glusterd_bitdsvc_start (glusterd_svc_t *svc, int flags)
{
        return glusterd_svc_start (svc, flags, NULL);
}

int
glusterd_bitd_stop (glusterd_svc_t *svc, int sig)
{
        return glusterd_svc_stop (svc, sig);
}

int
glusterd_bitdsvc_reconfigure ()
{
        return glusterd_svc_reconfigure (glusterd_bitdsvc_create_volfile);
}

void
glusterd_svc_build_bitd_volfile (glusterd_volinfo_t *volinfo,
                                 char *path, int path_len)
{
        char            workdir[PATH_MAX] = {0,};
        glusterd_conf_t *priv             = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/%s-bitd.vol", workdir,
                  volinfo->volname);
}
