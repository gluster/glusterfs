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
#include "glusterd-shd-svc-helper.h"
#include "glusterd-messages.h"
#include "glusterd-volgen.h"

void
glusterd_svc_build_shd_socket_filepath(glusterd_volinfo_t *volinfo, char *path,
                                       int path_len)
{
    char sockfilepath[PATH_MAX] = {
        0,
    };
    char rundir[PATH_MAX] = {
        0,
    };
    int32_t len = 0;
    glusterd_conf_t *priv = THIS->private;

    if (!priv)
        return;

    GLUSTERD_GET_SHD_RUNDIR(rundir, volinfo, priv);
    len = snprintf(sockfilepath, sizeof(sockfilepath), "%s/run-%s", rundir,
                   uuid_utoa(MY_UUID));
    if ((len < 0) || (len >= sizeof(sockfilepath))) {
        sockfilepath[0] = 0;
    }

    glusterd_set_socket_filepath(sockfilepath, path, path_len);
}

void
glusterd_svc_build_shd_pidfile(glusterd_volinfo_t *volinfo, char *path,
                               int path_len)
{
    char rundir[PATH_MAX] = {
        0,
    };
    glusterd_conf_t *priv = THIS->private;

    if (!priv)
        return;

    GLUSTERD_GET_SHD_RUNDIR(rundir, volinfo, priv);

    snprintf(path, path_len, "%s/%s-shd.pid", rundir, volinfo->volname);
}

void
glusterd_svc_build_shd_volfile_path(glusterd_volinfo_t *volinfo, char *path,
                                    int path_len)
{
    char workdir[PATH_MAX] = {
        0,
    };
    glusterd_conf_t *priv = THIS->private;

    if (!priv)
        return;

    GLUSTERD_GET_VOLUME_DIR(workdir, volinfo, priv);

    snprintf(path, path_len, "%s/%s-shd.vol", workdir, volinfo->volname);
}

void
glusterd_shd_svcproc_cleanup(glusterd_shdsvc_t *shd)
{
    glusterd_svc_proc_t *svc_proc = NULL;
    glusterd_svc_t *svc = NULL;
    glusterd_conf_t *conf = NULL;
    gf_boolean_t need_unref = _gf_false;
    rpc_clnt_t *rpc = NULL;

    conf = THIS->private;
    if (!conf)
        return;

    GF_VALIDATE_OR_GOTO(THIS->name, conf, out);
    GF_VALIDATE_OR_GOTO(THIS->name, shd, out);

    svc = &shd->svc;
    shd->attached = _gf_false;

    if (svc->conn.rpc) {
        rpc_clnt_unref(svc->conn.rpc);
        svc->conn.rpc = NULL;
    }

    pthread_mutex_lock(&conf->attach_lock);
    {
        svc_proc = svc->svc_proc;
        svc->svc_proc = NULL;
        svc->inited = _gf_false;
        cds_list_del_init(&svc->mux_svc);
        glusterd_unlink_file(svc->proc.pidfile);

        if (svc_proc && cds_list_empty(&svc_proc->svcs)) {
            cds_list_del_init(&svc_proc->svc_proc_list);
            /* We cannot free svc_proc list from here. Because
             * if there are pending events on the rpc, it will
             * try to access the corresponding svc_proc, so unrefing
             * rpc request and then cleaning up the memory is carried
             * from the notify function upon RPC_CLNT_DESTROY destroy.
             */
            need_unref = _gf_true;
            rpc = svc_proc->rpc;
            svc_proc->rpc = NULL;
        }
    }
    pthread_mutex_unlock(&conf->attach_lock);
    /*rpc unref has to be performed outside the lock*/
    if (need_unref && rpc)
        rpc_clnt_unref(rpc);
out:
    return;
}

int
glusterd_svc_set_shd_pidfile(glusterd_volinfo_t *volinfo, dict_t *dict)
{
    int ret = -1;
    glusterd_svc_t *svc = NULL;
    xlator_t *this = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    GF_VALIDATE_OR_GOTO(this->name, volinfo, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);

    svc = &(volinfo->shd.svc);

    ret = dict_set_dynstr_with_alloc(dict, "pidfile", svc->proc.pidfile);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set pidfile %s in dict", svc->proc.pidfile);
        goto out;
    }
    ret = 0;
out:
    return ret;
}
