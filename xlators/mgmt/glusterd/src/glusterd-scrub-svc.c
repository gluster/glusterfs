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
#include "glusterd-svc-helper.h"

char *scrub_svc_name = "scrub";

void
glusterd_scrubsvc_build (glusterd_svc_t *svc)
{
        svc->manager = glusterd_scrubsvc_manager;
        svc->start = glusterd_scrubsvc_start;
        svc->stop = glusterd_scrubsvc_stop;
}

int
glusterd_scrubsvc_init (glusterd_svc_t *svc)
{
        return glusterd_svc_init (svc, scrub_svc_name);
}

static int
glusterd_scrubsvc_create_volfile ()
{
        char              filepath[PATH_MAX] = {0,};
        int               ret                = -1;
        glusterd_conf_t   *conf              = NULL;
        xlator_t          *this              = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);

        glusterd_svc_build_volfile_path (scrub_svc_name, conf->workdir,
                                         filepath, sizeof (filepath));

        ret = glusterd_create_global_volfile (build_scrub_graph,
                                              filepath, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "Failed to create volfile");
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_scrubsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int          ret    = -EINVAL;

        if (!svc->inited) {
                ret = glusterd_scrubsvc_init (svc);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_SCRUB_INIT_FAIL, "Failed to init "
                                "scrub service");
                        goto out;
                } else {
                        svc->inited = _gf_true;
                        gf_msg_debug (THIS->name, 0, "scrub service "
                                      "initialized");
                }
        }

        if (glusterd_should_i_stop_bitd ()) {
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
        if (ret)
                gf_event (EVENT_SVC_MANAGER_FAILED, "svc_name=%s", svc->name);
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_scrubsvc_start (glusterd_svc_t *svc, int flags)
{
        int ret = -1;
        dict_t *cmdict = NULL;

        cmdict = dict_new ();
        if (!cmdict)
                goto error_return;

        ret = dict_set_str (cmdict, "cmdarg0", "--global-timer-wheel");
        if (ret)
                goto dealloc_dict;

        ret = glusterd_svc_start (svc, flags, cmdict);

 dealloc_dict:
        dict_unref (cmdict);
 error_return:
        return ret;
}

int
glusterd_scrubsvc_stop (glusterd_svc_t *svc, int sig)
{
        return glusterd_svc_stop (svc, sig);
}

int
glusterd_scrubsvc_reconfigure ()
{
        int              ret             = -1;
        xlator_t        *this            = NULL;
        glusterd_conf_t *priv            = NULL;
        gf_boolean_t     identical       = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO (this->name, this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        if (glusterd_should_i_stop_bitd ())
                goto  manager;


        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */
        ret = glusterd_svc_check_volfile_identical (priv->scrub_svc.name,
                                                    build_scrub_graph,
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
        ret = glusterd_svc_check_topology_identical (priv->scrub_svc.name,
                                                     build_scrub_graph,
                                                     &identical);
        if (ret)
                goto out;

        /* Topology is not changed, but just the options. But write the
         * options to scrub volfile, so that scrub will be reconfigured.
         */
        if (identical) {
                ret = glusterd_scrubsvc_create_volfile ();
                if (ret == 0) {/* Only if above PASSES */
                        ret = glusterd_fetchspec_notify (THIS);
                }
                goto out;
        }

manager:
        /*
         * scrub volfile's topology has been changed. scrub server needs
         * to be RESTARTED to ACT on the changed volfile.
         */
        ret = priv->scrub_svc.manager (&(priv->scrub_svc),
                                       NULL,
                                       PROC_START_NO_WAIT);

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;

}
