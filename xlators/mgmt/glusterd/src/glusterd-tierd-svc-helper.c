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
#include "glusterd-tierd-svc-helper.h"
#include "glusterd-messages.h"
#include "syscall.h"
#include "glusterd-volgen.h"


void
glusterd_svc_build_tierd_rundir (glusterd_volinfo_t *volinfo,
                                 char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_TIER_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/run", workdir);
}

void
glusterd_svc_build_tierd_socket_filepath (glusterd_volinfo_t *volinfo,
                                          char *path, int path_len)
{
        char                    sockfilepath[PATH_MAX] = {0,};
        char                    rundir[PATH_MAX]       = {0,};

        glusterd_svc_build_tierd_rundir (volinfo, rundir, sizeof (rundir));
        snprintf (sockfilepath, sizeof (sockfilepath), "%s/run-%s",
                  rundir, uuid_utoa (MY_UUID));

        glusterd_set_socket_filepath (sockfilepath, path, path_len);
}

void
glusterd_svc_build_tierd_pidfile (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len)
{
        char                    rundir[PATH_MAX]      = {0,};

        glusterd_svc_build_tierd_rundir (volinfo, rundir, sizeof (rundir));

        snprintf (path, path_len, "%s/%s-tierd.pid", rundir, volinfo->volname);
}

void
glusterd_svc_build_tierd_volfile_path (glusterd_volinfo_t *volinfo,
                                  char *path, int path_len)
{
        char                    workdir[PATH_MAX]      = {0,};
        glusterd_conf_t        *priv                   = THIS->private;

        GLUSTERD_GET_VOLUME_DIR (workdir, volinfo, priv);

        snprintf (path, path_len, "%s/%s-tierd.vol", workdir,
                  volinfo->volname);
}

void
glusterd_svc_build_tierd_logdir (char *logdir, char *volname, size_t len)
{
        snprintf (logdir, len, "%s/tier/%s", DEFAULT_LOG_FILE_DIRECTORY,
                  volname);
}

void
glusterd_svc_build_tierd_logfile (char *logfile, char *logdir, size_t len)
{
        snprintf (logfile, len, "%s/tierd.log", logdir);
}

int
glusterd_svc_check_tier_volfile_identical (char *svc_name,
                                           glusterd_volinfo_t *volinfo,
                                           gf_boolean_t *identical)
{
        char            orgvol[PATH_MAX]        = {0,};
        char            tmpvol[PATH_MAX]        = {0,};
        xlator_t        *this                   = NULL;
        int             ret                     = -1;
        int             need_unlink             = 0;
        int             tmp_fd                  = -1;

        this = THIS;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, identical, out);

        glusterd_svc_build_tierd_volfile_path (volinfo, orgvol,
                        sizeof (orgvol));

        snprintf (tmpvol, sizeof (tmpvol), "/tmp/g%s-XXXXXX", svc_name);

        tmp_fd = mkstemp (tmpvol);
        if (tmp_fd < 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to create temp file"
                        " %s:(%s)", tmpvol, strerror (errno));
                goto out;
        }

        need_unlink = 1;
        ret = build_rebalance_volfile (volinfo, tmpvol, NULL);
        if (ret)
                goto out;

        ret = glusterd_check_files_identical (orgvol, tmpvol,
                                              identical);
        if (ret)
                goto out;

out:
        if (need_unlink)
                sys_unlink (tmpvol);

        if (tmp_fd >= 0)
                sys_close (tmp_fd);

        return ret;
}

int
glusterd_svc_check_tier_topology_identical (char *svc_name,
                                       glusterd_volinfo_t *volinfo,
                                       gf_boolean_t *identical)
{
        char            orgvol[PATH_MAX]        = {0,};
        char            tmpvol[PATH_MAX]        = {0,};
        glusterd_conf_t *conf                   = NULL;
        xlator_t        *this                   = THIS;
        int             ret                     = -1;
        int             tmpclean                = 0;
        int             tmpfd                   = -1;

        if ((!identical) || (!this) || (!this->private))
                goto out;

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);


        glusterd_svc_build_tierd_volfile_path (volinfo, orgvol,
                        sizeof (orgvol));

        snprintf (tmpvol, sizeof (tmpvol), "/tmp/g%s-XXXXXX", svc_name);

        tmpfd = mkstemp (tmpvol);
        if (tmpfd < 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        GD_MSG_FILE_OP_FAILED, "Unable to create temp file"
                        " %s:(%s)", tmpvol, strerror (errno));
                goto out;
        }

        tmpclean = 1; /* SET the flag to unlink() tmpfile */
        ret = build_rebalance_volfile (volinfo, tmpvol, NULL);
        if (ret)
                goto out;

        /* Compare the topology of volfiles */
        ret = glusterd_check_topology_identical (orgvol, tmpvol,
                                                 identical);
out:
        if (tmpfd >= 0)
                sys_close (tmpfd);
        if (tmpclean)
                sys_unlink (tmpvol);
        return ret;
}
