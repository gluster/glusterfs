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
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-shd-svc.h"
#include "glusterd-shd-svc-helper.h"
#include "glusterd-svc-helper.h"
#include "glusterd-store.h"

#define GD_SHD_PROCESS_NAME "--process-name"
char *shd_svc_name = "glustershd";

void
glusterd_shdsvc_build(glusterd_svc_t *svc)
{
    int ret = -1;
    ret = snprintf(svc->name, sizeof(svc->name), "%s", shd_svc_name);
    if (ret < 0)
        return;

    CDS_INIT_LIST_HEAD(&svc->mux_svc);
    svc->manager = glusterd_shdsvc_manager;
    svc->start = glusterd_shdsvc_start;
    svc->stop = glusterd_shdsvc_stop;
    svc->reconfigure = glusterd_shdsvc_reconfigure;
}

int
glusterd_shdsvc_init(void *data, glusterd_conn_t *mux_conn,
                     glusterd_svc_proc_t *mux_svc)
{
    int ret = -1;
    char rundir[PATH_MAX] = {
        0,
    };
    char sockpath[PATH_MAX] = {
        0,
    };
    char pidfile[PATH_MAX] = {
        0,
    };
    char volfile[PATH_MAX] = {
        0,
    };
    char logdir[PATH_MAX] = {
        0,
    };
    char logfile[PATH_MAX] = {
        0,
    };
    char volfileid[256] = {0};
    glusterd_svc_t *svc = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_conf_t *priv = NULL;
    glusterd_muxsvc_conn_notify_t notify = NULL;
    xlator_t *this = NULL;
    char *volfileserver = NULL;
    int32_t len = 0;

    this = THIS;
    GF_VALIDATE_OR_GOTO(THIS->name, this, out);

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    volinfo = data;
    GF_VALIDATE_OR_GOTO(this->name, data, out);
    GF_VALIDATE_OR_GOTO(this->name, mux_svc, out);

    svc = &(volinfo->shd.svc);

    ret = snprintf(svc->name, sizeof(svc->name), "%s", shd_svc_name);
    if (ret < 0)
        goto out;

    notify = glusterd_muxsvc_common_rpc_notify;
    glusterd_store_perform_node_state_store(volinfo);

    GLUSTERD_GET_SHD_RUNDIR(rundir, volinfo, priv);
    glusterd_svc_create_rundir(rundir);

    glusterd_svc_build_logfile_path(shd_svc_name, priv->logdir, logfile,
                                    sizeof(logfile));

    /* Initialize the connection mgmt */
    if (mux_conn && mux_svc->rpc) {
        /* multiplexed svc */
        svc->conn.frame_timeout = mux_conn->frame_timeout;
        /* This will be unrefed from glusterd_shd_svcproc_cleanup*/
        svc->conn.rpc = rpc_clnt_ref(mux_svc->rpc);
        ret = snprintf(svc->conn.sockpath, sizeof(svc->conn.sockpath), "%s",
                       mux_conn->sockpath);
        if (ret < 0)
            goto out;
    } else {
        ret = mkdir_p(priv->logdir, 0755, _gf_true);
        if ((ret == -1) && (EEXIST != errno)) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_CREATE_DIR_FAILED,
                   "Unable to create logdir %s", logdir);
            goto out;
        }

        glusterd_svc_build_shd_socket_filepath(volinfo, sockpath,
                                               sizeof(sockpath));
        ret = glusterd_muxsvc_conn_init(&(svc->conn), mux_svc, sockpath, 600,
                                        notify);
        if (ret)
            goto out;
        /* This will be unrefed when the last svcs is detached from the list */
        if (!mux_svc->rpc)
            mux_svc->rpc = rpc_clnt_ref(svc->conn.rpc);
    }

    /* Initialize the process mgmt */
    glusterd_svc_build_shd_pidfile(volinfo, pidfile, sizeof(pidfile));
    glusterd_svc_build_shd_volfile_path(volinfo, volfile, PATH_MAX);
    len = snprintf(volfileid, sizeof(volfileid), "shd/%s", volinfo->volname);
    if ((len < 0) || (len >= sizeof(volfileid))) {
        ret = -1;
        goto out;
    }

    if (dict_get_strn(this->options, "transport.socket.bind-address",
                      SLEN("transport.socket.bind-address"),
                      &volfileserver) != 0) {
        volfileserver = "localhost";
    }
    ret = glusterd_proc_init(&(svc->proc), shd_svc_name, pidfile, logdir,
                             logfile, volfile, volfileid, volfileserver);
    if (ret)
        goto out;

out:
    gf_msg_debug(this ? this->name : "glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_shdsvc_create_volfile(glusterd_volinfo_t *volinfo)
{
    char filepath[PATH_MAX] = {
        0,
    };

    int ret = -1;
    dict_t *mod_dict = NULL;
    xlator_t *this = THIS;
    GF_ASSERT(this);

    glusterd_svc_build_shd_volfile_path(volinfo, filepath, PATH_MAX);
    if (!glusterd_is_shd_compatible_volume(volinfo)) {
        /* If volfile exist, delete it. This case happens when we
         * change from replica/ec to distribute.
         */
        (void)glusterd_unlink_file(filepath);
        ret = 0;
        goto out;
    }
    mod_dict = dict_new();
    if (!mod_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }

    ret = dict_set_uint32(mod_dict, "cluster.background-self-heal-count", 0);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.background-self-heal-count", NULL);
        goto out;
    }

    ret = dict_set_str(mod_dict, "cluster.data-self-heal", "on");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.data-self-heal", NULL);
        goto out;
    }

    ret = dict_set_str(mod_dict, "cluster.metadata-self-heal", "on");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.metadata-self-heal", NULL);
        goto out;
    }

    ret = dict_set_str(mod_dict, "cluster.entry-self-heal", "on");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.entry-self-heal", NULL);
        goto out;
    }

    ret = glusterd_shdsvc_generate_volfile(volinfo, filepath, mod_dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Failed to create volfile");
        goto out;
    }

out:
    if (mod_dict)
        dict_unref(mod_dict);
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

gf_boolean_t
glusterd_svcs_shd_compatible_volumes_stopped(glusterd_svc_t *svc)
{
    glusterd_svc_proc_t *svc_proc = NULL;
    glusterd_shdsvc_t *shd = NULL;
    glusterd_svc_t *temp_svc = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    gf_boolean_t comp = _gf_false;
    glusterd_conf_t *conf = THIS->private;

    GF_VALIDATE_OR_GOTO("glusterd", conf, out);
    GF_VALIDATE_OR_GOTO("glusterd", svc, out);
    pthread_mutex_lock(&conf->attach_lock);
    {
        svc_proc = svc->svc_proc;
        if (!svc_proc)
            goto unlock;
        cds_list_for_each_entry(temp_svc, &svc_proc->svcs, mux_svc)
        {
            /* Get volinfo->shd from svc object */
            shd = cds_list_entry(svc, glusterd_shdsvc_t, svc);
            if (!shd) {
                gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SHD_OBJ_GET_FAIL,
                       "Failed to get shd object "
                       "from shd service");
                goto unlock;
            }

            /* Get volinfo from shd */
            volinfo = cds_list_entry(shd, glusterd_volinfo_t, shd);
            if (!volinfo) {
                gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
                       "Failed to get volinfo from "
                       "from shd");
                goto unlock;
            }
            if (!glusterd_is_shd_compatible_volume(volinfo))
                continue;
            if (volinfo->status == GLUSTERD_STATUS_STARTED)
                goto unlock;
        }
        comp = _gf_true;
    }
unlock:
    pthread_mutex_unlock(&conf->attach_lock);
out:
    return comp;
}

int
glusterd_shdsvc_manager(glusterd_svc_t *svc, void *data, int flags)
{
    int ret = -1;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_conf_t *conf = NULL;
    gf_boolean_t shd_restart = _gf_false;

    conf = THIS->private;
    GF_VALIDATE_OR_GOTO("glusterd", conf, out);
    GF_VALIDATE_OR_GOTO("glusterd", svc, out);
    volinfo = data;
    GF_VALIDATE_OR_GOTO("glusterd", volinfo, out);

    if (volinfo->is_snap_volume) {
        /* healing of a snap volume is not supported yet*/
        ret = 0;
        goto out;
    }

    while (conf->restart_shd) {
        synccond_wait(&conf->cond_restart_shd, &conf->big_lock);
    }
    conf->restart_shd = _gf_true;
    shd_restart = _gf_true;

    if (volinfo)
        glusterd_volinfo_ref(volinfo);

    if (!glusterd_is_shd_compatible_volume(volinfo)) {
        ret = 0;
        if (svc->inited) {
            /* This means glusterd was running for this volume and now
             * it was converted to a non-shd volume. So just stop the shd
             */
            ret = svc->stop(svc, SIGTERM);
        }
        goto out;
    }
    ret = glusterd_shdsvc_create_volfile(volinfo);
    if (ret)
        goto out;

    ret = glusterd_shd_svc_mux_init(volinfo, svc);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_FAILED_INIT_SHDSVC,
               "Failed to init shd service");
        goto out;
    }

    /* If all the volumes are stopped or all shd compatible volumes
     * are stopped then stop the service if:
     * - volinfo is NULL or
     * - volinfo is present and volume is shd compatible
     * Otherwise create volfile and restart service if:
     * - volinfo is NULL or
     * - volinfo is present and volume is shd compatible
     */
    if (glusterd_svcs_shd_compatible_volumes_stopped(svc)) {
        /* TODO
         * Take a lock and detach all svc's to stop the process
         * also reset the init flag
         */
        ret = svc->stop(svc, SIGTERM);
    } else if (volinfo) {
        if (volinfo->status != GLUSTERD_STATUS_STARTED) {
            ret = svc->stop(svc, SIGTERM);
            if (ret)
                goto out;
        }
        if (volinfo->status == GLUSTERD_STATUS_STARTED) {
            ret = svc->start(svc, flags);
            if (ret)
                goto out;
        }
    }
out:
    if (shd_restart) {
        conf->restart_shd = _gf_false;
        synccond_broadcast(&conf->cond_restart_shd);
    }
    if (volinfo)
        glusterd_volinfo_unref(volinfo);
    if (ret)
        gf_event(EVENT_SVC_MANAGER_FAILED, "svc_name=%s", svc->name);
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

int
glusterd_new_shd_svc_start(glusterd_svc_t *svc, int flags)
{
    int ret = -1;
    char glusterd_uuid_option[PATH_MAX] = {0};
    char client_pid[32] = {0};
    dict_t *cmdline = NULL;
    xlator_t *this = THIS;
    GF_ASSERT(this);

    cmdline = dict_new();
    if (!cmdline)
        goto out;

    ret = snprintf(glusterd_uuid_option, sizeof(glusterd_uuid_option),
                   "*replicate*.node-uuid=%s", uuid_utoa(MY_UUID));
    if (ret < 0)
        goto out;

    ret = snprintf(client_pid, sizeof(client_pid), "--client-pid=%d",
                   GF_CLIENT_PID_SELF_HEALD);
    if (ret < 0)
        goto out;

    ret = dict_set_str(cmdline, "arg", client_pid);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=arg", NULL);
        goto out;
    }

    /* Pass cmdline arguments as key-value pair. The key is merely
     * a carrier and is not used. Since dictionary follows LIFO the value
     * should be put in reverse order*/
    ret = dict_set_str(cmdline, "arg4", svc->name);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=arg4", NULL);
        goto out;
    }

    ret = dict_set_str(cmdline, "arg3", GD_SHD_PROCESS_NAME);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=arg3", NULL);
        goto out;
    }

    ret = dict_set_str(cmdline, "arg2", glusterd_uuid_option);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=arg2", NULL);
        goto out;
    }

    ret = dict_set_str(cmdline, "arg1", "--xlator-option");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=arg1", NULL);
        goto out;
    }

    ret = glusterd_svc_start(svc, flags, cmdline);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno,
                GD_MSG_GLUSTER_SERVICE_START_FAIL, NULL);
        goto out;
    }

    ret = glusterd_conn_connect(&(svc->conn));
out:
    if (cmdline)
        dict_unref(cmdline);
    return ret;
}

int
glusterd_recover_shd_attach_failure(glusterd_volinfo_t *volinfo,
                                    glusterd_svc_t *svc, int flags)
{
    int ret = -1;
    glusterd_svc_proc_t *mux_proc = NULL;
    glusterd_conf_t *conf = NULL;

    conf = THIS->private;

    if (!conf || !volinfo || !svc)
        return -1;
    glusterd_shd_svcproc_cleanup(&volinfo->shd);
    mux_proc = glusterd_svcprocess_new();
    if (!mux_proc) {
        return -1;
    }
    ret = glusterd_shdsvc_init(volinfo, NULL, mux_proc);
    if (ret)
        return -1;
    pthread_mutex_lock(&conf->attach_lock);
    {
        cds_list_add_tail(&mux_proc->svc_proc_list, &conf->shd_procs);
        svc->svc_proc = mux_proc;
        cds_list_del_init(&svc->mux_svc);
        cds_list_add_tail(&svc->mux_svc, &mux_proc->svcs);
    }
    pthread_mutex_unlock(&conf->attach_lock);

    ret = glusterd_new_shd_svc_start(svc, flags);
    if (!ret) {
        volinfo->shd.attached = _gf_true;
    }
    return ret;
}

int
glusterd_shdsvc_start(glusterd_svc_t *svc, int flags)
{
    int ret = -1;
    glusterd_shdsvc_t *shd = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_conf_t *conf = NULL;

    GF_VALIDATE_OR_GOTO("glusterd", svc, out);
    conf = THIS->private;
    GF_VALIDATE_OR_GOTO("glusterd", conf, out);

    /* Get volinfo->shd from svc object */
    shd = cds_list_entry(svc, glusterd_shdsvc_t, svc);
    if (!shd) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SHD_OBJ_GET_FAIL,
               "Failed to get shd object "
               "from shd service");
        return -1;
    }

    /* Get volinfo from shd */
    volinfo = cds_list_entry(shd, glusterd_volinfo_t, shd);
    if (!volinfo) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Failed to get volinfo from "
               "from shd");
        return -1;
    }

    if (volinfo->status != GLUSTERD_STATUS_STARTED)
        return -1;

    glusterd_volinfo_ref(volinfo);

    if (!svc->inited) {
        ret = glusterd_shd_svc_mux_init(volinfo, svc);
        if (ret)
            goto out;
    }

    if (shd->attached) {
        glusterd_volinfo_ref(volinfo);
        /* Unref will happen from glusterd_svc_attach_cbk */
        ret = glusterd_attach_svc(svc, volinfo, flags);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
                   "Failed to attach shd svc(volume=%s) to pid=%d",
                   volinfo->volname, glusterd_proc_get_pid(&svc->proc));
            glusterd_shd_svcproc_cleanup(&volinfo->shd);
            glusterd_volinfo_unref(volinfo);
            goto out1;
        }
        goto out;
    }
    ret = glusterd_new_shd_svc_start(svc, flags);
    if (!ret) {
        shd->attached = _gf_true;
    }
out:
    if (ret && volinfo)
        glusterd_shd_svcproc_cleanup(&volinfo->shd);
    if (volinfo)
        glusterd_volinfo_unref(volinfo);
out1:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

int
glusterd_shdsvc_reconfigure(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    xlator_t *this = NULL;
    gf_boolean_t identical = _gf_false;
    dict_t *mod_dict = NULL;
    glusterd_svc_t *svc = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    if (!volinfo) {
        /* reconfigure will be called separately*/
        ret = 0;
        goto out;
    }

    glusterd_volinfo_ref(volinfo);
    svc = &(volinfo->shd.svc);
    if (glusterd_svcs_shd_compatible_volumes_stopped(svc))
        goto manager;

    /*
     * Check both OLD and NEW volfiles, if they are SAME by size
     * and cksum i.e. "character-by-character". If YES, then
     * NOTHING has been changed, just return.
     */

    if (!glusterd_is_shd_compatible_volume(volinfo)) {
        if (svc->inited)
            goto manager;

        /* Nothing to do if not shd compatible */
        ret = 0;
        goto out;
    }
    mod_dict = dict_new();
    if (!mod_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }

    ret = dict_set_uint32(mod_dict, "cluster.background-self-heal-count", 0);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.background-self-heal-count", NULL);
        goto out;
    }

    ret = dict_set_str(mod_dict, "cluster.data-self-heal", "on");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.data-self-heal", NULL);
        goto out;
    }

    ret = dict_set_str(mod_dict, "cluster.metadata-self-heal", "on");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.metadata-self-heal", NULL);
        goto out;
    }

    ret = dict_set_int32(mod_dict, "graph-check", 1);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=graph-check", NULL);
        goto out;
    }

    ret = dict_set_str(mod_dict, "cluster.entry-self-heal", "on");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_SET_FAILED,
                "Key=cluster.entry-self-heal", NULL);
        goto out;
    }

    ret = glusterd_volume_svc_check_volfile_identical(
        "glustershd", mod_dict, volinfo, glusterd_shdsvc_generate_volfile,
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
    ret = glusterd_volume_svc_check_topology_identical(
        "glustershd", mod_dict, volinfo, glusterd_shdsvc_generate_volfile,
        &identical);
    if (ret)
        goto out;

    /* Topology is not changed, but just the options. But write the
     * options to shd volfile, so that shd will be reconfigured.
     */
    if (identical) {
        ret = glusterd_shdsvc_create_volfile(volinfo);
        if (ret == 0) { /* Only if above PASSES */
            ret = glusterd_fetchspec_notify(THIS);
        }
        goto out;
    }
manager:
    /*
     * shd volfile's topology has been changed. volfile needs
     * to be RECONFIGURED to ACT on the changed volfile.
     */
    ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);

out:
    if (volinfo)
        glusterd_volinfo_unref(volinfo);
    if (mod_dict)
        dict_unref(mod_dict);
    gf_msg_debug(this ? this->name : "glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_shdsvc_restart()
{
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_volinfo_t *tmp = NULL;
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_svc_t *svc = NULL;

    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    pthread_mutex_lock(&conf->volume_lock);
    cds_list_for_each_entry_safe(volinfo, tmp, &conf->volumes, vol_list)
    {
        glusterd_volinfo_ref(volinfo);
        pthread_mutex_unlock(&conf->volume_lock);
        /* Start per volume shd svc */
        if (volinfo->status == GLUSTERD_STATUS_STARTED) {
            svc = &(volinfo->shd.svc);
            ret = svc->manager(svc, volinfo, PROC_START_NO_WAIT);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SHD_START_FAIL,
                       "Couldn't start shd for "
                       "vol: %s on restart",
                       volinfo->volname);
                gf_event(EVENT_SVC_MANAGER_FAILED, "volume=%s;svc_name=%s",
                         volinfo->volname, svc->name);
                glusterd_volinfo_unref(volinfo);
                goto out;
            }
        }
        glusterd_volinfo_unref(volinfo);
        pthread_mutex_lock(&conf->volume_lock);
    }
    pthread_mutex_unlock(&conf->volume_lock);
out:
    return ret;
}

int
glusterd_shdsvc_stop(glusterd_svc_t *svc, int sig)
{
    int ret = -1;
    glusterd_svc_proc_t *svc_proc = NULL;
    glusterd_shdsvc_t *shd = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    gf_boolean_t empty = _gf_false;
    glusterd_conf_t *conf = NULL;
    int pid = -1;

    conf = THIS->private;
    GF_VALIDATE_OR_GOTO("glusterd", conf, out);
    GF_VALIDATE_OR_GOTO("glusterd", svc, out);
    svc_proc = svc->svc_proc;
    if (!svc_proc) {
        /*
         * This can happen when stop was called on a volume that is not shd
         * compatible.
         */
        gf_msg_debug("glusterd", 0, "svc_proc is null, ie shd already stopped");
        ret = 0;
        goto out;
    }

    /* Get volinfo->shd from svc object */
    shd = cds_list_entry(svc, glusterd_shdsvc_t, svc);
    if (!shd) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SHD_OBJ_GET_FAIL,
               "Failed to get shd object "
               "from shd service");
        return -1;
    }

    /* Get volinfo from shd */
    volinfo = cds_list_entry(shd, glusterd_volinfo_t, shd);
    if (!volinfo) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Failed to get volinfo from "
               "from shd");
        return -1;
    }

    glusterd_volinfo_ref(volinfo);
    pthread_mutex_lock(&conf->attach_lock);
    {
        if (!gf_is_service_running(svc->proc.pidfile, &pid)) {
            gf_msg_debug(THIS->name, 0, "shd isn't running");
        }
        cds_list_del_init(&svc->mux_svc);
        empty = cds_list_empty(&svc_proc->svcs);
        if (empty) {
            svc_proc->status = GF_SVC_STOPPING;
            cds_list_del_init(&svc_proc->svc_proc_list);
        }
    }
    pthread_mutex_unlock(&conf->attach_lock);
    if (empty) {
        /* Unref will happen when destroying the connection */
        glusterd_volinfo_ref(volinfo);
        svc_proc->data = volinfo;
        ret = glusterd_svc_stop(svc, sig);
        if (ret) {
            glusterd_volinfo_unref(volinfo);
            goto out;
        }
    }
    if (!empty && pid != -1) {
        ret = glusterd_detach_svc(svc, volinfo, sig);
        if (ret)
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_SVC_STOP_FAIL,
                   "shd service is failed to detach volume %s from pid %d",
                   volinfo->volname, glusterd_proc_get_pid(&svc->proc));
        else
            gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_SVC_STOP_SUCCESS,
                   "Shd service is detached for volume %s from pid %d",
                   volinfo->volname, glusterd_proc_get_pid(&svc->proc));
    }
    svc->online = _gf_false;
    (void)glusterd_unlink_file((char *)svc->proc.pidfile);
    glusterd_shd_svcproc_cleanup(shd);
    ret = 0;
    glusterd_volinfo_unref(volinfo);
out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}
