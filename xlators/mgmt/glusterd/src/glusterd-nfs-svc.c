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
#include "glusterd-nfs-svc.h"

char *nfs_svc_name = "nfs";

static gf_boolean_t
glusterd_nfssvc_need_start ()
{
        glusterd_conf_t    *priv    = NULL;
        gf_boolean_t       start   = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;

        priv = THIS->private;

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                if (!glusterd_is_volume_started (volinfo))
                        continue;

                if (dict_get_str_boolean (volinfo->dict, "nfs.disable", 0))
                        continue;
                start = _gf_true;
                break;
        }

        return start;
}

int
glusterd_nfssvc_init (glusterd_svc_t *svc)
{
        return glusterd_svc_init (svc, nfs_svc_name,
                                  glusterd_nfssvc_manager,
                                  glusterd_nfssvc_start,
                                  glusterd_nfssvc_stop);
}

static int
glusterd_nfssvc_create_volfile ()
{
        char            filepath[PATH_MAX] = {0,};
        glusterd_conf_t *conf = THIS->private;

        glusterd_svc_build_volfile_path (nfs_svc_name, conf->workdir,
                                         filepath, sizeof (filepath));
        return glusterd_create_global_volfile (build_nfs_graph,
                                               filepath, NULL);
}

static int
glusterd_nfssvc_check_volfile_identical (gf_boolean_t *identical)
{
        char            nfsvol[PATH_MAX]        = {0,};
        char            tmpnfsvol[PATH_MAX]     = {0,};
        glusterd_conf_t *conf                   = NULL;
        xlator_t        *this                   = NULL;
        int             ret                     = -1;
        int             need_unlink             = 0;
        int             tmp_fd                  = -1;

        this = THIS;

        GF_ASSERT (this);
        GF_ASSERT (identical);
        conf = this->private;

        glusterd_svc_build_volfile_path (nfs_svc_name, conf->workdir,
                                         nfsvol, sizeof (nfsvol));

        snprintf (tmpnfsvol, sizeof (tmpnfsvol), "/tmp/gnfs-XXXXXX");

        tmp_fd = mkstemp (tmpnfsvol);
        if (tmp_fd < 0) {
                gf_log (this->name, GF_LOG_WARNING, "Unable to create temp file"
                        " %s:(%s)", tmpnfsvol, strerror (errno));
                goto out;
        }

        need_unlink = 1;

        ret = glusterd_create_global_volfile (build_nfs_graph,
                                              tmpnfsvol, NULL);
        if (ret)
                goto out;

        ret = glusterd_check_files_identical (nfsvol, tmpnfsvol,
                                              identical);
        if (ret)
                goto out;

out:
        if (need_unlink)
                unlink (tmpnfsvol);

        if (tmp_fd >= 0)
                close (tmp_fd);

        return ret;
}

static int
glusterd_nfssvc_check_topology_identical (gf_boolean_t *identical)
{
        char            nfsvol[PATH_MAX]        = {0,};
        char            tmpnfsvol[PATH_MAX]     = {0,};
        glusterd_conf_t *conf                   = NULL;
        xlator_t        *this                   = THIS;
        int             ret                     = -1;
        int             tmpclean                = 0;
        int             tmpfd                   = -1;

        if ((!identical) || (!this) || (!this->private))
                goto out;

        conf = (glusterd_conf_t *) this->private;
        GF_ASSERT (conf);

        /* Fetch the original NFS volfile */
        glusterd_svc_build_volfile_path (conf->nfs_svc.name, conf->workdir,
                                         nfsvol, sizeof (nfsvol));

        /* Create the temporary NFS volfile */
        snprintf (tmpnfsvol, sizeof (tmpnfsvol), "/tmp/gnfs-XXXXXX");
        tmpfd = mkstemp (tmpnfsvol);
        if (tmpfd < 0) {
                gf_log (this->name, GF_LOG_WARNING, "Unable to create temp file"
                        " %s: (%s)", tmpnfsvol, strerror (errno));
                goto out;
        }

        tmpclean = 1; /* SET the flag to unlink() tmpfile */

        ret = glusterd_create_global_volfile (build_nfs_graph,
                                              tmpnfsvol, NULL);
        if (ret)
                goto out;

        /* Compare the topology of volfiles */
        ret = glusterd_check_topology_identical (nfsvol, tmpnfsvol,
                                                 identical);
out:
        if (tmpfd >= 0)
                close (tmpfd);
        if (tmpclean)
                unlink (tmpnfsvol);
        return ret;
}

int
glusterd_nfssvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = -1;

        if (!glusterd_nfssvc_need_start ()) {
                ret = svc->stop (svc, SIGKILL);

        } else {
                ret = glusterd_nfssvc_create_volfile ();
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
glusterd_nfssvc_start (glusterd_svc_t *svc, int flags)
{
        if (glusterd_nfssvc_need_start ())
                return glusterd_svc_start (svc, flags, NULL);

        return 0;
}

int
glusterd_nfssvc_stop (glusterd_svc_t *svc, int sig)
{
        int                    ret        = -1;
        gf_boolean_t           deregister = _gf_false;

        if (glusterd_proc_is_running (&(svc->proc)))
                deregister = _gf_true;

        ret = glusterd_svc_stop (svc, sig);
        if (ret)
                goto out;
        if (deregister)
                glusterd_nfs_pmap_deregister ();

out:
        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
glusterd_nfssvc_reconfigure ()
{
        int              ret             = -1;
        xlator_t        *this            = NULL;
        glusterd_conf_t *priv            = NULL;
        gf_boolean_t     identical       = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */
        ret = glusterd_nfssvc_check_volfile_identical (&identical);
        if (ret)
                goto out;

        if (identical) {
                ret = 0;
                goto out;
        }

        /*
         * They are not identical. Find out if the topology is changed
         * OR just the volume options. If just the options which got
         * changed, then inform the xlator to reconfigure the options.
         */
        identical = _gf_false; /* RESET the FLAG */
        ret = glusterd_nfssvc_check_topology_identical (&identical);
        if (ret)
                goto out;

        /* Topology is not changed, but just the options. But write the
         * options to NFS volfile, so that NFS will be reconfigured.
         */
        if (identical) {
                ret = glusterd_nfssvc_create_volfile();
                if (ret == 0) {/* Only if above PASSES */
                        ret = glusterd_fetchspec_notify (THIS);
                }
                goto out;
        }

        /*
         * NFS volfile's topology has been changed. NFS server needs
         * to be RESTARTED to ACT on the changed volfile.
         */
        ret = priv->nfs_svc.manager (&(priv->nfs_svc), NULL,
                                     PROC_START_NO_WAIT);

out:
        return ret;

}
