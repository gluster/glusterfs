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
#include "glusterfs.h"
#include "glusterd-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-shd-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-bitd-svc.h"

int
glusterd_svcs_reconfigure (glusterd_volinfo_t *volinfo)
{
        int              ret  = 0;
        xlator_t        *this = THIS;
        glusterd_conf_t *conf = NULL;

        GF_ASSERT (this);

        conf = this->private;
        GF_ASSERT (conf);

        ret = glusterd_nfssvc_reconfigure ();
        if (ret)
                goto out;

        if (volinfo && !glusterd_is_shd_compatible_volume (volinfo)) {
                ; /* Do nothing */
        } else {
                ret = glusterd_shdsvc_reconfigure ();
                if (ret)
                        goto out;
        }
        if (conf->op_version == GD_OP_VERSION_MIN)
                goto out;

        if (volinfo && !glusterd_is_volume_quota_enabled (volinfo))
                goto out;

        ret = glusterd_quotadsvc_reconfigure ();
        if (ret)
                goto out;

        ret = glusterd_bitdsvc_reconfigure ();
        if (ret)
                goto out;
out:
        return ret;
}

int
glusterd_svcs_stop ()
{
        int              ret  = 0;
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_svc_stop (&(priv->nfs_svc), SIGKILL);
        if (ret)
                goto out;

        ret = glusterd_svc_stop (&(priv->shd_svc), SIGTERM);
        if (ret)
                goto out;

        ret = glusterd_svc_stop (&(priv->quotad_svc), SIGTERM);
        if (ret)
                goto out;
out:
        return ret;
}

int
glusterd_svcs_manager (glusterd_volinfo_t *volinfo)
{
        int              ret  = 0;
        xlator_t        *this = THIS;
        glusterd_conf_t *conf = NULL;

        GF_ASSERT (this);

        conf = this->private;
        GF_ASSERT (conf);

        if (volinfo && volinfo->is_snap_volume)
                return 0;

        ret = conf->nfs_svc.manager (&(conf->nfs_svc), NULL,
                                     PROC_START_NO_WAIT);
        if (ret)
                goto out;

        ret = conf->shd_svc.manager (&(conf->shd_svc), volinfo,
                                     PROC_START_NO_WAIT);
        if (ret == -EINVAL)
                ret = 0;
        if (ret)
                goto out;

        if (conf->op_version == GD_OP_VERSION_MIN)
                goto out;

        ret = conf->quotad_svc.manager (&(conf->quotad_svc), volinfo,
                                        PROC_START_NO_WAIT);
        if (ret == -EINVAL)
                ret = 0;
        if (ret)
                goto out;

        ret = conf->bitd_svc.manager (&(conf->bitd_svc), volinfo,
                                        PROC_START_NO_WAIT);
        if (ret == -EINVAL)
                ret = 0;
        if (ret)
                goto out;
out:
        return ret;
}


