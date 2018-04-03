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
#include "glusterd-gfproxyd-svc.h"
#include "glusterd-messages.h"
#include "glusterd-svc-helper.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-gfproxyd-svc-helper.h"
#include "syscall.h"

void
glusterd_gfproxydsvc_build (glusterd_svc_t *svc)
{
        svc->manager = glusterd_gfproxydsvc_manager;
        svc->start = glusterd_gfproxydsvc_start;
        svc->stop = glusterd_gfproxydsvc_stop;
        svc->reconfigure = glusterd_gfproxydsvc_reconfigure;
}


int glusterd_gfproxydsvc_stop (glusterd_svc_t *svc, int sig)
{
        glusterd_volinfo_t     *volinfo      = NULL;
        int                     ret          = 0;

        ret = glusterd_svc_stop (svc, sig);
        if (ret)
                goto out;

        volinfo = glusterd_gfproxyd_volinfo_from_svc (svc);
        volinfo->gfproxyd.port = 0;

out:
        return ret;
}


int glusterd_gfproxydsvc_init (glusterd_volinfo_t *volinfo)
{
        int                     ret                = -1;
        char                    rundir[PATH_MAX]   = {0,};
        char                    sockpath[PATH_MAX] = {0,};
        char                    pidfile[PATH_MAX]  = {0,};
        char                    volfile[PATH_MAX]  = {0,};
        char                    logdir[PATH_MAX]   = {0,};
        char                    logfile[PATH_MAX]  = {0,};
        char                    volfileid[256]     = {0};
        glusterd_svc_t         *svc                = NULL;
        glusterd_conf_t        *priv               = NULL;
        glusterd_conn_notify_t  notify             = NULL;
        xlator_t               *this               = NULL;
        char                    *volfileserver     = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        svc = &(volinfo->gfproxyd.svc);

        ret = snprintf (svc->name, sizeof (svc->name), "%s", gfproxyd_svc_name);
        if (ret < 0)
                goto out;

        notify = glusterd_svc_common_rpc_notify;

        glusterd_svc_build_gfproxyd_rundir (volinfo, rundir, sizeof (rundir));
        glusterd_svc_create_rundir (rundir);

        /* Initialize the connection mgmt */
        glusterd_svc_build_gfproxyd_socket_filepath (volinfo, sockpath,
                                                     sizeof (sockpath));
        ret = glusterd_conn_init (&(svc->conn), sockpath, 600, notify);
        if (ret)
                goto out;

        /* Initialize the process mgmt */
        glusterd_svc_build_gfproxyd_pidfile (volinfo, pidfile, sizeof (pidfile));
        glusterd_svc_build_gfproxyd_volfile_path (volinfo, volfile,
                                                  sizeof (volfile));
        glusterd_svc_build_gfproxyd_logdir (logdir, volinfo->volname,
                                             sizeof (logdir));
        ret = mkdir_p (logdir, 0755, _gf_true);
        if ((ret == -1) && (EEXIST != errno)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create logdir %s",
                        logdir);
                goto out;
        }
        glusterd_svc_build_gfproxyd_logfile (logfile, logdir, sizeof (logfile));
        snprintf (volfileid, sizeof (volfileid), "gfproxyd/%s", volinfo->volname);

        if (dict_get_str (this->options, "transport.socket.bind-address",
                          &volfileserver) != 0) {
                volfileserver = "localhost";
        }
        ret = glusterd_proc_init (&(svc->proc), gfproxyd_svc_name, pidfile,
                                  logdir, logfile, volfile, volfileid,
                                  volfileserver);
        if (ret)
                goto out;

out:
        gf_msg_debug (this ? this->name : "glusterd", 0, "Returning %d", ret);
        return ret;
}


static int
glusterd_gfproxydsvc_create_volfile (glusterd_volinfo_t *volinfo)
{
        int               ret                = -1;
        xlator_t         *this               = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        ret = glusterd_generate_gfproxyd_volfile (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "Failed to create volfile");
                goto out;
        }

out:
        gf_msg_debug (this ? this->name : "glusterd", 0, "Returning %d", ret);

        return ret;

}

int
glusterd_gfproxydsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = -1;
        glusterd_volinfo_t *volinfo = NULL;
        xlator_t           *this    = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        volinfo = data;
        GF_VALIDATE_OR_GOTO (this->name, data, out);

        if (!svc->inited) {
                ret = glusterd_gfproxydsvc_init (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FAILED_INIT_QUOTASVC, "Failed to init "
                                "gfproxyd service");
                        goto out;
                } else {
                        svc->inited = _gf_true;
                        gf_msg_debug (this->name, 0, "gfproxyd service "
                                      "initialized");
                }
        }

        ret = glusterd_is_gfproxyd_enabled (volinfo);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "Failed to read volume "
                        "options");
                goto out;
        }

        if (ret) {
                if (!glusterd_is_volume_started (volinfo)) {
                        if (glusterd_proc_is_running (&svc->proc)) {
                                ret = svc->stop (svc, SIGTERM);
                                if (ret)
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SNAPD_STOP_FAIL,
                                                "Couldn't stop gfproxyd for "
                                                "volume: %s",
                                                volinfo->volname);
                        } else {
                                /* Since gfproxyd is not running set ret to 0 */
                                ret = 0;
                        }
                        goto out;
                }

                ret = glusterd_gfproxydsvc_create_volfile (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_CREATE_FAIL, "Couldn't create "
                                "gfroxyd volfile for volume: %s",
                                volinfo->volname);
                        goto out;
                }
                ret = svc->stop (svc, SIGTERM);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_START_FAIL, "Couldn't stop "
                                "gfproxyd for volume: %s", volinfo->volname);
                        goto out;
                }

                ret = svc->start (svc, flags);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_START_FAIL, "Couldn't start "
                                "gfproxyd for volume: %s", volinfo->volname);
                        goto out;
                }

                glusterd_volinfo_ref (volinfo);
                ret = glusterd_conn_connect (&(svc->conn));
                if (ret) {
                        glusterd_volinfo_unref (volinfo);
                        goto out;
                }

        } else if (glusterd_proc_is_running (&svc->proc)) {
                ret = svc->stop (svc, SIGTERM);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_STOP_FAIL,
                                "Couldn't stop gfproxyd for volume: %s",
                                volinfo->volname);
                        goto out;
                }
        }

out:
        if (ret) {
                if (volinfo) {
                        gf_event (EVENT_SVC_MANAGER_FAILED,
                                  "volume=%s;svc_name=%s",
                                  volinfo->volname, svc->name);
                }
        }

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

        return ret;
}

int
glusterd_gfproxydsvc_start (glusterd_svc_t *svc, int flags)
{
        int                  ret                        = -1;
        runner_t             runner                     = {0,};
        glusterd_conf_t     *priv                       = NULL;
        xlator_t            *this                       = NULL;
        char                 valgrind_logfile[PATH_MAX] = {0};
        int                  gfproxyd_port              = 0;
        char                 msg[1024]                  = {0,};
        char                 gfproxyd_id[PATH_MAX]      = {0,};
        glusterd_volinfo_t  *volinfo                    = NULL;
        char                *localtime_logging          = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        volinfo = glusterd_gfproxyd_volinfo_from_svc (svc);
        if (!volinfo)
                goto out;

        ret = sys_access (svc->proc.volfile, F_OK);
        if (ret) {
                gf_msg (this->name, GF_LOG_DEBUG, 0,
                        GD_MSG_VOLINFO_GET_FAIL,
                        "gfproxyd Volfile %s is not present", svc->proc.volfile);
                ret = glusterd_gfproxydsvc_create_volfile (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL, "Couldn't create "
                                "gfproxyd volfile for volume: %s",
                                volinfo->volname);
                        goto out;
                }
        }
        runinit (&runner);

        if (this->ctx->cmd_args.valgrind) {
                snprintf (valgrind_logfile, PATH_MAX, "%s/valgrind-%s",
                          svc->proc.logdir, svc->proc.logfile);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        snprintf (gfproxyd_id, sizeof (gfproxyd_id), "gfproxyd-%s",
                  volinfo->volname);
        runner_add_args (&runner, SBIN_DIR"/glusterfsd",
                         "-s", svc->proc.volfileserver,
                         "--volfile-id", svc->proc.volfileid,
                         "-p", svc->proc.pidfile,
                         "-l", svc->proc.logfile,
                         "--brick-name", gfproxyd_id,
                         "-S", svc->conn.sockpath, NULL);

        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");
        if (dict_get_str (priv->opts, GLUSTERD_LOCALTIME_LOGGING_KEY,
                          &localtime_logging) == 0) {
                if (strcmp (localtime_logging, "enable") == 0)
                        runner_add_arg (&runner, "--localtime-logging");
        }

        gfproxyd_port = pmap_assign_port (this, volinfo->gfproxyd.port,
                                          gfproxyd_id);
        volinfo->gfproxyd.port = gfproxyd_port;

        runner_add_arg (&runner, "--brick-port");
        runner_argprintf (&runner, "%d", gfproxyd_port);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "%s-server.listen-port=%d",
                         volinfo->volname, gfproxyd_port);

        snprintf (msg, sizeof (msg),
                  "Starting the gfproxyd service for volume %s",
                  volinfo->volname);
        runner_log (&runner, this->name, GF_LOG_DEBUG, msg);

        if (flags == PROC_START_NO_WAIT) {
                ret = runner_run_nowait (&runner);
        } else {
                synclock_unlock (&priv->big_lock);
                {
                        ret = runner_run (&runner);
                }
                synclock_lock (&priv->big_lock);
        }

out:
        return ret;
}


int
glusterd_gfproxydsvc_restart ()
{
        glusterd_volinfo_t      *volinfo        = NULL;
        int                     ret             = -1;
        xlator_t                *this           = THIS;
        glusterd_conf_t         *conf           = NULL;
        glusterd_svc_t          *svc            = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                /* Start per volume gfproxyd svc */
                if (volinfo->status == GLUSTERD_STATUS_STARTED) {
                        svc = &(volinfo->gfproxyd.svc);
                        ret = svc->manager (svc, volinfo, PROC_START_NO_WAIT);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAPD_START_FAIL,
                                        "Couldn't resolve gfproxyd for "
                                        "vol: %s on restart", volinfo->volname);
                                gf_event (EVENT_SVC_MANAGER_FAILED,
                                          "volume=%s;svc_name=%s",
                                          volinfo->volname, svc->name);
                                goto out;
                        }
                }
        }
out:
        return ret;
}


int
glusterd_gfproxydsvc_reconfigure (void *data)
{
        int                   ret             = -1;
        xlator_t             *this            = NULL;
        gf_boolean_t          identical       = _gf_false;
        glusterd_volinfo_t   *volinfo         = NULL;

        volinfo = data;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        if (!volinfo->gfproxyd.svc.inited)
                goto manager;

        if (!glusterd_is_gfproxyd_enabled (volinfo))
                goto manager;
        else if (!glusterd_proc_is_running (&volinfo->gfproxyd.svc.proc))
                goto manager;

        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */
        ret = glusterd_svc_check_gfproxyd_volfile_identical
               (volinfo->gfproxyd.svc.name, volinfo, &identical);
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
        ret = glusterd_svc_check_gfproxyd_topology_identical
               (volinfo->gfproxyd.svc.name, volinfo, &identical);
        if (ret)
                goto out;

        /* Topology is not changed, but just the options. But write the
         * options to gfproxyd volfile, so that gfproxyd will be reconfigured.
         */
        if (identical) {
                ret = glusterd_gfproxydsvc_create_volfile (volinfo);
                if (ret == 0) {/* Only if above PASSES */
                        ret = glusterd_fetchspec_notify (this);
                }
                goto out;
        }
manager:
        /*
         * gfproxyd volfile's topology has been changed. gfproxyd server needs
         * to be RESTARTED to ACT on the changed volfile.
         */
        ret = volinfo->gfproxyd.svc.manager (&(volinfo->gfproxyd.svc), volinfo,
                                             PROC_START_NO_WAIT);

out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}
