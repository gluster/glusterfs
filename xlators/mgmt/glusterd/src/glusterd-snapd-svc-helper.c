/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-snapd-svc-helper.h"

void
glusterd_svc_build_snapd_rundir (glusterd_volinfo_t *volinfo,
                                 char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/run", workdir);
}

void
glusterd_svc_build_snapd_socket_filepath (glusterd_volinfo_t *volinfo,
                                          char *path, int path_len)
{
        char                    sockfilepath[PATH_MAX] = {0,};
        char                    rundir[PATH_MAX]       = {0,};

        glusterd_svc_build_snapd_rundir (volinfo, rundir, sizeof (rundir));
        snprintf (sockfilepath, sizeof (sockfilepath), "%s/run-%s",
                  rundir, uuid_utoa (MY_UUID));

        glusterd_set_socket_filepath (sockfilepath, path, path_len);
}

void
glusterd_svc_build_snapd_pidfile (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len)
{
        char            rundir[PATH_MAX]      = {0,};

        glusterd_svc_build_snapd_rundir (volinfo, rundir, sizeof (rundir));

        snprintf (path, path_len, "%s/%s-snapd.pid", rundir, volinfo->volname);
}

void
glusterd_svc_build_snapd_volfile (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/%s-snapd.vol", workdir,
                  volinfo->volname);
}
