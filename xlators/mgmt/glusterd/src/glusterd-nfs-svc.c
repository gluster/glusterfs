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
#include "glusterd-messages.h"
#include "glusterd-svc-helper.h"

static char *nfs_svc_name = "nfs";

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

                if (dict_get_str_boolean (volinfo->dict, NFS_DISABLE_MAP_KEY, 1))
                        continue;
                start = _gf_true;
                break;
        }

        return start;
}

int
glusterd_nfssvc_init (glusterd_svc_t *svc)
{
        return glusterd_svc_init (svc, nfs_svc_name);
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
glusterd_nfssvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = -1;

        if (!svc->inited) {
                ret = glusterd_nfssvc_init (svc);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_FAILED_INIT_NFSSVC, "Failed to init nfs "
                                "service");
                        goto out;
                } else {
                        svc->inited = _gf_true;
                        gf_msg_debug (THIS->name, 0, "nfs service initialized");
                }
        }

        ret = svc->stop (svc, SIGKILL);
        if (ret)
                goto out;

        ret = glusterd_nfssvc_create_volfile ();
        if (ret)
                goto out;

        if (glusterd_nfssvc_need_start ()) {
                ret = svc->start (svc, flags);
                if (ret)
                        goto out;

                ret = glusterd_conn_connect (&(svc->conn));
                if (ret)
                        goto out;
        }
out:
        if (ret)
                gf_event (EVENT_SVC_MANAGER_FAILED, "svc_name=%s", svc->name);

        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

static int
glusterd_nfssvc_start (glusterd_svc_t *svc, int flags)
{
        return glusterd_svc_start (svc, flags, NULL);
}

static int
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
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

void
glusterd_nfssvc_build (glusterd_svc_t *svc)
{
        svc->manager = glusterd_nfssvc_manager;
        svc->start = glusterd_nfssvc_start;
        svc->stop = glusterd_nfssvc_stop;
}

int
glusterd_nfssvc_reconfigure ()
{
        int              ret             = -1;
        xlator_t        *this            = NULL;
        glusterd_conf_t *priv            = NULL;
        gf_boolean_t     identical       = _gf_false;
        gf_boolean_t     vol_started     = _gf_false;
        glusterd_volinfo_t *volinfo      = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (this->name, this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                        vol_started = _gf_true;
                        break;
                }
        }
        if (!vol_started) {
                ret = 0;
                goto out;
        }

        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */

        ret = glusterd_svc_check_volfile_identical (priv->nfs_svc.name,
                                                    build_nfs_graph,
                                                    &identical);
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
        ret = glusterd_svc_check_topology_identical (priv->nfs_svc.name,
                                                     build_nfs_graph,
                                                     &identical);
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
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;

}
