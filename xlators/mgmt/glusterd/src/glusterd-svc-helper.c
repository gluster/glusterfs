/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/globals.h>
#include <glusterfs/run.h>
#include "glusterd.h"
#include <glusterfs/glusterfs.h>
#include "glusterd-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-shd-svc.h"
#include "glusterd-quotad-svc.h"
#ifdef BUILD_GNFS
#include "glusterd-nfs-svc.h"
#endif
#include "glusterd-bitd-svc.h"
#include "glusterd-scrub-svc.h"
#include "glusterd-svc-helper.h"
#include <glusterfs/syscall.h>

int
glusterd_svcs_reconfigure()
{
    int ret = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    char *svc_name = NULL;

    conf = this->private;
    GF_ASSERT(conf);

#ifdef BUILD_GNFS
    svc_name = "nfs";
    ret = glusterd_nfssvc_reconfigure();
    if (ret)
        goto out;
#endif
    svc_name = "self-heald";
    ret = glusterd_shdsvc_reconfigure();
    if (ret)
        goto out;

    if (conf->op_version == GD_OP_VERSION_MIN)
        goto out;

    svc_name = "quotad";
    ret = glusterd_quotadsvc_reconfigure();
    if (ret)
        goto out;

    svc_name = "bitd";
    ret = glusterd_bitdsvc_reconfigure();
    if (ret)
        goto out;

    svc_name = "scrubber";
    ret = glusterd_scrubsvc_reconfigure();
out:
    if (ret && svc_name)
        gf_event(EVENT_SVC_RECONFIGURE_FAILED, "svc_name=%s", svc_name);
    return ret;
}

int
glusterd_svcs_stop()
{
    int ret = 0;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

#ifdef BUILD_GNFS
    ret = priv->nfs_svc.stop(&(priv->nfs_svc), SIGKILL);
    if (ret)
        goto out;
#endif

    ret = priv->shd_svc.stop(&(priv->shd_svc), SIGTERM);
    if (ret)
        goto out;

    ret = priv->quotad_svc.stop(&(priv->quotad_svc), SIGTERM);
    if (ret)
        goto out;

    ret = priv->bitd_svc.stop(&(priv->bitd_svc), SIGTERM);
    if (ret)
        goto out;
    ret = priv->scrub_svc.stop(&(priv->scrub_svc), SIGTERM);
out:
    return ret;
}

int
glusterd_svcs_manager(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    glusterd_conf_t *conf = NULL;

    conf = THIS->private;
    GF_ASSERT(conf);

    if (volinfo && volinfo->is_snap_volume)
        return 0;

#if BUILD_GNFS
    ret = conf->nfs_svc.manager(&(conf->nfs_svc), NULL, PROC_START_NO_WAIT);
    if (ret)
        goto out;
#endif

    ret = conf->shd_svc.manager(&(conf->shd_svc), volinfo, PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
    if (ret)
        goto out;

    if (conf->op_version == GD_OP_VERSION_MIN)
        goto out;

    ret = conf->quotad_svc.manager(&(conf->quotad_svc), volinfo,
                                   PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
    if (ret)
        goto out;

    ret = conf->bitd_svc.manager(&(conf->bitd_svc), NULL, PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
    if (ret)
        goto out;

    ret = conf->scrub_svc.manager(&(conf->scrub_svc), NULL, PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
out:
    return ret;
}

int
glusterd_svc_check_volfile_identical(char *svc_name,
                                     glusterd_graph_builder_t builder,
                                     gf_boolean_t *identical)
{
    char orgvol[PATH_MAX] = {
        0,
    };
    char *tmpvol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    int need_unlink = 0;
    int tmp_fd = -1;

    GF_ASSERT(identical);
    conf = this->private;

    glusterd_svc_build_volfile_path(svc_name, conf->workdir, orgvol,
                                    sizeof(orgvol));

    ret = gf_asprintf(&tmpvol, "/tmp/g%s-XXXXXX", svc_name);
    if (ret < 0) {
        goto out;
    }

    /* coverity[SECURE_TEMP] mkstemp uses 0600 as the mode and is safe */
    tmp_fd = mkstemp(tmpvol);
    if (tmp_fd < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to create temp file"
               " %s:(%s)",
               tmpvol, strerror(errno));
        ret = -1;
        goto out;
    }

    need_unlink = 1;

    ret = glusterd_create_global_volfile(builder, tmpvol, NULL);
    if (ret)
        goto out;

    ret = glusterd_check_files_identical(orgvol, tmpvol, identical);
out:
    if (need_unlink)
        sys_unlink(tmpvol);

    if (tmpvol != NULL)
        GF_FREE(tmpvol);

    if (tmp_fd >= 0)
        sys_close(tmp_fd);

    return ret;
}

int
glusterd_svc_check_topology_identical(char *svc_name,
                                      glusterd_graph_builder_t builder,
                                      gf_boolean_t *identical)
{
    char orgvol[PATH_MAX] = {
        0,
    };
    char *tmpvol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    int tmpclean = 0;
    int tmpfd = -1;

    if ((!identical) || (!this->private)) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    /* Fetch the original volfile */
    glusterd_svc_build_volfile_path(svc_name, conf->workdir, orgvol,
                                    sizeof(orgvol));

    /* Create the temporary volfile */
    ret = gf_asprintf(&tmpvol, "/tmp/g%s-XXXXXX", svc_name);
    if (ret < 0) {
        goto out;
    }

    /* coverity[SECURE_TEMP] mkstemp uses 0600 as the mode and is safe */
    tmpfd = mkstemp(tmpvol);
    if (tmpfd < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to create temp file"
               " %s:(%s)",
               tmpvol, strerror(errno));
        ret = -1;
        goto out;
    }

    tmpclean = 1; /* SET the flag to unlink() tmpfile */

    ret = glusterd_create_global_volfile(builder, tmpvol, NULL);
    if (ret)
        goto out;

    /* Compare the topology of volfiles */
    ret = glusterd_check_topology_identical(orgvol, tmpvol, identical);
out:
    if (tmpfd >= 0)
        sys_close(tmpfd);
    if (tmpclean)
        sys_unlink(tmpvol);
    if (tmpvol != NULL)
        GF_FREE(tmpvol);
    return ret;
}
