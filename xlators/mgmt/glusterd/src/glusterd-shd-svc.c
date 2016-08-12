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
#include "glusterd-svc-mgmt.h"
#include "glusterd-shd-svc.h"
#include "glusterd-svc-helper.h"

char *shd_svc_name = "glustershd";

void
glusterd_shdsvc_build (glusterd_svc_t *svc)
{
        svc->manager = glusterd_shdsvc_manager;
        svc->start = glusterd_shdsvc_start;
        svc->stop = glusterd_svc_stop;
}

int
glusterd_shdsvc_init (glusterd_svc_t *svc)
{
        return glusterd_svc_init (svc, shd_svc_name);
}

static int
glusterd_shdsvc_create_volfile ()
{
        char            filepath[PATH_MAX] = {0,};
        int             ret = -1;
        glusterd_conf_t *conf = THIS->private;
        dict_t          *mod_dict = NULL;

        mod_dict = dict_new ();
        if (!mod_dict)
                goto out;

        ret = dict_set_uint32 (mod_dict, "cluster.background-self-heal-count",
                               0);
        if (ret)
                goto out;

        ret = dict_set_str (mod_dict, "cluster.data-self-heal", "on");
        if (ret)
                goto out;

        ret = dict_set_str (mod_dict, "cluster.metadata-self-heal", "on");
        if (ret)
                goto out;

        ret = dict_set_str (mod_dict, "cluster.entry-self-heal", "on");
        if (ret)
                goto out;

        glusterd_svc_build_volfile_path (shd_svc_name, conf->workdir,
                                         filepath, sizeof (filepath));
        ret = glusterd_create_global_volfile (build_shd_graph, filepath,
                                              mod_dict);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "Failed to create volfile");
                goto out;
        }

out:
        if (mod_dict)
                dict_unref (mod_dict);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_shdsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = 0;
        glusterd_volinfo_t *volinfo = NULL;

        if (!svc->inited) {
                ret = glusterd_shdsvc_init (svc);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_FAILED_INIT_SHDSVC, "Failed to init shd "
                                "service");
                        goto out;
                } else {
                        svc->inited = _gf_true;
                        gf_msg_debug (THIS->name, 0, "shd service initialized");
                }
        }

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
            glusterd_all_shd_compatible_volumes_stopped ()) {
                if (!(volinfo &&
                      !glusterd_is_shd_compatible_volume (volinfo))) {
                        ret = svc->stop (svc, SIGTERM);
                }
        } else {
                if (!(volinfo &&
                      !glusterd_is_shd_compatible_volume (volinfo))) {
                        ret = glusterd_shdsvc_create_volfile ();
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
        if (ret)
                gf_event (EVENT_SVC_MANAGER_FAILED, "svc_name=%s", svc->name);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_shdsvc_start (glusterd_svc_t *svc, int flags)
{
        int              ret                            = -1;
        char             glusterd_uuid_option[PATH_MAX] = {0};
        dict_t          *cmdline                        = NULL;

        cmdline = dict_new ();
        if (!cmdline)
                goto out;

        ret = snprintf (glusterd_uuid_option, sizeof (glusterd_uuid_option),
                        "*replicate*.node-uuid=%s", uuid_utoa (MY_UUID));
        if (ret < 0)
                goto out;

        /* Pass cmdline arguments as key-value pair. The key is merely
         * a carrier and is not used. Since dictionary follows LIFO the value
         * should be put in reverse order*/
        ret = dict_set_str (cmdline, "arg2", glusterd_uuid_option);
        if (ret)
                goto out;

        ret = dict_set_str (cmdline, "arg1", "--xlator-option");
        if (ret)
                goto out;

        ret = glusterd_svc_start (svc, flags, cmdline);

out:
        if (cmdline)
                dict_unref (cmdline);

        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}


int
glusterd_shdsvc_reconfigure ()
{
        int              ret             = -1;
        xlator_t        *this            = NULL;
        glusterd_conf_t *priv            = NULL;
        gf_boolean_t     identical       = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO (this->name, this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        if (glusterd_all_shd_compatible_volumes_stopped ())
                goto manager;

        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */
        ret = glusterd_svc_check_volfile_identical (priv->shd_svc.name,
                                                    build_shd_graph,
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
        ret = glusterd_svc_check_topology_identical (priv->shd_svc.name,
                                                     build_shd_graph,
                                                     &identical);
        if (ret)
                goto out;

        /* Topology is not changed, but just the options. But write the
         * options to shd volfile, so that shd will be reconfigured.
         */
        if (identical) {
                ret = glusterd_shdsvc_create_volfile ();
                if (ret == 0) {/* Only if above PASSES */
                        ret = glusterd_fetchspec_notify (THIS);
                }
                goto out;
        }
manager:
        /*
         * shd volfile's topology has been changed. shd server needs
         * to be RESTARTED to ACT on the changed volfile.
         */
        ret = priv->shd_svc.manager (&(priv->shd_svc), NULL,
                                     PROC_START_NO_WAIT);

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;

}
