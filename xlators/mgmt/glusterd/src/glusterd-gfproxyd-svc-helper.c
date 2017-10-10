/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-gfproxyd-svc-helper.h"
#include "glusterd-messages.h"
#include "syscall.h"
#include "glusterd-volgen.h"

void
glusterd_svc_build_gfproxyd_rundir (glusterd_volinfo_t *volinfo,
                                    char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_VOLUME_PID_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s", workdir);
}

void
glusterd_svc_build_gfproxyd_socket_filepath (glusterd_volinfo_t *volinfo,
                                          char *path, int path_len)
{
        char                    sockfilepath[PATH_MAX] = {0,};
        char                    rundir[PATH_MAX]       = {0,};

        glusterd_svc_build_gfproxyd_rundir (volinfo, rundir, sizeof (rundir));
        snprintf (sockfilepath, sizeof (sockfilepath), "%s/run-%s",
                  rundir, uuid_utoa (MY_UUID));

        glusterd_set_socket_filepath (sockfilepath, path, path_len);
}

void
glusterd_svc_build_gfproxyd_pidfile (glusterd_volinfo_t *volinfo,
                                     char *path, int path_len)
{
        char                    rundir[PATH_MAX]      = {0,};

        glusterd_svc_build_gfproxyd_rundir (volinfo, rundir, sizeof (rundir));

        snprintf (path, path_len, "%s/%s.gfproxyd.pid", rundir, volinfo->volname);
}

void
glusterd_svc_build_gfproxyd_volfile_path (glusterd_volinfo_t *volinfo,
                                          char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/%s.gfproxyd.vol", workdir,
                  volinfo->volname);
}

void
glusterd_svc_build_gfproxyd_logdir (char *logdir, char *volname, size_t len)
{
        snprintf (logdir, len, "%s/gfproxy/%s", DEFAULT_LOG_FILE_DIRECTORY,
                  volname);
}

void
glusterd_svc_build_gfproxyd_logfile (char *logfile, char *logdir, size_t len)
{
        snprintf (logfile, len, "%s/gfproxyd.log", logdir);
}

int
glusterd_is_gfproxyd_enabled (glusterd_volinfo_t *volinfo)
{
        return glusterd_volinfo_get_boolean (volinfo, VKEY_CONFIG_GFPROXY);
}

static int
glusterd_svc_get_gfproxyd_volfile (glusterd_volinfo_t *volinfo, char *svc_name,
                                   char *orgvol, char *tmpvol, int path_len)
{
        int             tmp_fd                  = -1;
        int             ret                     = -1;
        int             need_unlink             = 0;

        glusterd_svc_build_gfproxyd_volfile_path (volinfo, orgvol,
                                                  path_len);

        snprintf (tmpvol, path_len, "/tmp/g%s-XXXXXX", svc_name);

        tmp_fd = mkstemp (tmpvol);
        if (tmp_fd < 0) {
                gf_msg ("glusterd", GF_LOG_WARNING, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to create temp file"
                        " %s:(%s)", tmpvol, strerror (errno));
                goto out;
        }

        need_unlink = 1;
        ret = glusterd_build_gfproxyd_volfile (volinfo, tmpvol);

out:
        if (need_unlink && ret < 0)
                sys_unlink (tmpvol);

        if (tmp_fd >= 0)
                sys_close (tmp_fd);

        return ret;
}

int
glusterd_svc_check_gfproxyd_volfile_identical (char *svc_name,
                                               glusterd_volinfo_t *volinfo,
                                               gf_boolean_t *identical)
{
        char            orgvol[PATH_MAX]        = {0,};
        char            tmpvol[PATH_MAX]        = {0,};
        int             ret                     = -1;
        int             need_unlink             = 0;

        GF_VALIDATE_OR_GOTO ("glusterd", identical, out);

        ret = glusterd_svc_get_gfproxyd_volfile (volinfo, svc_name, orgvol,
                                                 tmpvol, PATH_MAX);
        if (ret)
                goto out;

        need_unlink = 1;
        ret = glusterd_check_files_identical (orgvol, tmpvol,
                                              identical);
        if (ret)
                goto out;

out:
        if (need_unlink)
                sys_unlink (tmpvol);

        return ret;
}

int
glusterd_svc_check_gfproxyd_topology_identical (char *svc_name,
                                                glusterd_volinfo_t *volinfo,
                                                gf_boolean_t *identical)
{
        char            orgvol[PATH_MAX]        = {0,};
        char            tmpvol[PATH_MAX]        = {0,};
        int             ret                     = -1;
        int             tmpclean                = 0;

        GF_VALIDATE_OR_GOTO ("glusterd", identical, out);

        ret = glusterd_svc_get_gfproxyd_volfile (volinfo, svc_name, orgvol,
                                                 tmpvol, PATH_MAX);
        if (ret)
                goto out;

        tmpclean = 1; /* SET the flag to unlink() tmpfile */

        /* Compare the topology of volfiles */
        ret = glusterd_check_topology_identical (orgvol, tmpvol,
                                                 identical);
out:
        if (tmpclean)
                sys_unlink (tmpvol);
        return ret;
}

glusterd_volinfo_t *
glusterd_gfproxyd_volinfo_from_svc (glusterd_svc_t *svc)
{
        glusterd_volinfo_t     *volinfo                    = NULL;
        glusterd_gfproxydsvc_t *gfproxyd                   = NULL;

        /* Get volinfo->gfproxyd from svc object */
        gfproxyd = cds_list_entry (svc, glusterd_gfproxydsvc_t, svc);
        if (!gfproxyd) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_OBJ_GET_FAIL, "Failed to get gfproxyd "
                        "object from gfproxyd service");
                goto out;
        }

        /* Get volinfo from gfproxyd */
        volinfo = cds_list_entry (gfproxyd, glusterd_volinfo_t, gfproxyd);
        if (!volinfo) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "Failed to get volinfo from "
                        "from gfproxyd");
                goto out;
        }
out:
        return volinfo;
}
