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
#include "glusterd-bitd-svc.h"

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
        xlator_t          *this              = NULL;

        this = THIS;
        conf = this->private;
        GF_ASSERT (conf);


        glusterd_svc_build_volfile_path (bitd_svc_name, conf->workdir,
                                         filepath, sizeof (filepath));

        ret = glusterd_create_global_volfile (build_bitd_graph,
                                              filepath, NULL);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "Failed to create volfile");
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_bitdsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                      ret           = 0;
        xlator_t                *this          = NULL;
        glusterd_brickinfo_t    *brickinfo     = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (glusterd_should_i_stop_bitd ()) {
                ret = svc->stop (svc, SIGTERM);
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
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int
glusterd_bitdsvc_start (glusterd_svc_t *svc, int flags)
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
glusterd_bitdsvc_stop (glusterd_svc_t *svc, int sig)
{
        return glusterd_svc_stop (svc, sig);
}

int
glusterd_bitdsvc_reconfigure ()
{
        return glusterd_svc_reconfigure (glusterd_bitdsvc_create_volfile);
}
