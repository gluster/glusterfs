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
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-conn-mgmt.h"
#include "glusterd-proc-mgmt.h"
#include "glusterd-snapd-svc.h"
#include "glusterd-snapd-svc-helper.h"
#include "glusterd-snapshot-utils.h"
#include "syscall.h"

char *snapd_svc_name = "snapd";

static void
glusterd_svc_build_snapd_logdir (char *logdir, char *volname, size_t len)
{
        snprintf (logdir, len, "%s/snaps/%s", DEFAULT_LOG_FILE_DIRECTORY,
                  volname);
}

static void
glusterd_svc_build_snapd_logfile (char *logfile, char *logdir, size_t len)
{
        snprintf (logfile, len, "%s/snapd.log", logdir);
}

void
glusterd_snapdsvc_build (glusterd_svc_t *svc)
{
        svc->manager = glusterd_snapdsvc_manager;
        svc->start = glusterd_snapdsvc_start;
        svc->stop = glusterd_svc_stop;
}

int
glusterd_snapdsvc_init (void *data)
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
        glusterd_volinfo_t     *volinfo            = NULL;
        glusterd_conf_t        *priv               = NULL;
        glusterd_conn_notify_t  notify             = NULL;
        xlator_t               *this               = NULL;
        char                    *volfileserver     = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        volinfo = data;

        svc = &(volinfo->snapd.svc);

        ret = snprintf (svc->name, sizeof (svc->name), "%s", snapd_svc_name);
        if (ret < 0)
                goto out;

        notify = glusterd_snapdsvc_rpc_notify;

        glusterd_svc_build_snapd_rundir (volinfo, rundir, sizeof (rundir));
        glusterd_svc_create_rundir (rundir);

        /* Initialize the connection mgmt */
        glusterd_svc_build_snapd_socket_filepath (volinfo, sockpath,
                                                  sizeof (sockpath));
        ret = glusterd_conn_init (&(svc->conn), sockpath, 600, notify);
        if (ret)
                goto out;

        /* Initialize the process mgmt */
        glusterd_svc_build_snapd_pidfile (volinfo, pidfile, sizeof (pidfile));
        glusterd_svc_build_snapd_volfile (volinfo, volfile, sizeof (volfile));
        glusterd_svc_build_snapd_logdir (logdir, volinfo->volname,
                                         sizeof (logdir));
        ret = mkdir_p (logdir, 0755, _gf_true);
        if ((ret == -1) && (EEXIST != errno)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create logdir %s",
                        logdir);
                goto out;
        }
        glusterd_svc_build_snapd_logfile (logfile, logdir, sizeof (logfile));
        snprintf (volfileid, sizeof (volfileid), "snapd/%s", volinfo->volname);

        if (dict_get_str (this->options, "transport.socket.bind-address",
                          &volfileserver) != 0) {
                volfileserver = "localhost";
        }
        ret = glusterd_proc_init (&(svc->proc), snapd_svc_name, pidfile, logdir,
                                  logfile, volfile, volfileid, volfileserver);
        if (ret)
                goto out;

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_snapdsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = 0;
        xlator_t           *this    = THIS;
        glusterd_volinfo_t *volinfo = NULL;

        volinfo = data;

        if (!svc->inited) {
                ret = glusterd_snapdsvc_init (volinfo);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_INIT_FAIL, "Failed to initialize "
                                "snapd service for volume %s",
                                volinfo->volname);
                        goto out;
                } else {
                        svc->inited = _gf_true;
                        gf_msg_debug (THIS->name, 0, "snapd service "
                                      "initialized");
                }
        }

        ret = glusterd_is_snapd_enabled (volinfo);
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
                                                "Couldn't stop snapd for "
                                                "volume: %s",
                                                volinfo->volname);
                        } else {
                                /* Since snapd is not running set ret to 0 */
                                ret = 0;
                        }
                        goto out;
                }

                ret = glusterd_snapdsvc_create_volfile (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_CREATE_FAIL, "Couldn't create "
                                "snapd volfile for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                ret = svc->start (svc, flags);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SNAPD_START_FAIL, "Couldn't start "
                                "snapd for volume: %s", volinfo->volname);
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
                                "Couldn't stop snapd for volume: %s",
                                volinfo->volname);
                        goto out;
                }
                volinfo->snapd.port = 0;
        }

out:
        if (ret) {
                gf_event (EVENT_SVC_MANAGER_FAILED, "volume=%s;svc_name=%s",
                          volinfo->volname, svc->name);
        }
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

int32_t
glusterd_snapdsvc_start (glusterd_svc_t *svc, int flags)
{
        int                  ret                        = -1;
        runner_t             runner                     = {0,};
        glusterd_conf_t     *priv                       = NULL;
        xlator_t            *this                       = NULL;
        char                 valgrind_logfile[PATH_MAX] = {0};
        int                  snapd_port                 = 0;
        char                 msg[1024]                  = {0,};
        char                 snapd_id[PATH_MAX]         = {0,};
        glusterd_volinfo_t  *volinfo                    = NULL;
        glusterd_snapdsvc_t *snapd                      = NULL;

        this = THIS;
        GF_ASSERT(this);

        priv = this->private;
        GF_ASSERT (priv);

        if (glusterd_proc_is_running (&svc->proc)) {
                ret = 0;
                goto out;
        }

        /* Get volinfo->snapd from svc object */
        snapd = cds_list_entry (svc, glusterd_snapdsvc_t, svc);
        if (!snapd) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_OBJ_GET_FAIL, "Failed to get snapd object "
                        "from snapd service");
                goto out;
        }

        /* Get volinfo from snapd */
        volinfo = cds_list_entry (snapd, glusterd_volinfo_t, snapd);
        if (!volinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "Failed to get volinfo from "
                        "from snapd");
                goto out;
        }

        ret = sys_access (svc->proc.volfile, F_OK);
        if (ret) {
                gf_msg (this->name, GF_LOG_DEBUG, 0,
                        GD_MSG_VOLINFO_GET_FAIL,
                        "snapd Volfile %s is not present", svc->proc.volfile);
                /* If glusterd is down on one of the nodes and during
                 * that time "USS is enabled" for the first time. After some
                 * time when the glusterd which was down comes back it tries
                 * to look for the snapd volfile and it does not find snapd
                 * volfile and because of this starting of snapd fails.
                 * Therefore, if volfile is not present then create a fresh
                 * volfile.
                 */
                ret = glusterd_snapdsvc_create_volfile (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL, "Couldn't create "
                                "snapd volfile for volume: %s",
                                volinfo->volname);
                        goto out;
                }
        }
        runinit (&runner);

        if (priv->valgrind) {
                snprintf (valgrind_logfile, PATH_MAX, "%s/valgrind-snapd.log",
                          svc->proc.logdir);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        snprintf (snapd_id, sizeof (snapd_id), "snapd-%s", volinfo->volname);
        runner_add_args (&runner, SBIN_DIR"/glusterfsd",
                         "-s", svc->proc.volfileserver,
                         "--volfile-id", svc->proc.volfileid,
                         "-p", svc->proc.pidfile,
                         "-l", svc->proc.logfile,
                         "--brick-name", snapd_id,
                         "-S", svc->conn.sockpath, NULL);

        snapd_port = pmap_assign_port (THIS, volinfo->snapd.port, snapd_id);
        volinfo->snapd.port = snapd_port;

        runner_add_arg (&runner, "--brick-port");
        runner_argprintf (&runner, "%d", snapd_port);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "%s-server.listen-port=%d",
                         volinfo->volname, snapd_port);
        runner_add_arg (&runner, "--no-mem-accounting");

        snprintf (msg, sizeof (msg),
                  "Starting the snapd service for volume %s", volinfo->volname);
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
glusterd_snapdsvc_restart ()
{
        glusterd_volinfo_t      *volinfo        = NULL;
        int                     ret             = 0;
        xlator_t                *this           = THIS;
        glusterd_conf_t         *conf           = NULL;
        glusterd_svc_t          *svc            = NULL;

        GF_ASSERT (this);

        conf = this->private;
        GF_ASSERT (conf);

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                /* Start per volume snapd svc */
                if (volinfo->status == GLUSTERD_STATUS_STARTED) {
                        svc = &(volinfo->snapd.svc);
                        ret = svc->manager (svc, volinfo, PROC_START_NO_WAIT);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SNAPD_START_FAIL,
                                        "Couldn't resolve snapd for "
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
glusterd_snapdsvc_rpc_notify (glusterd_conn_t *conn, rpc_clnt_event_t event)
{
        int                  ret     = 0;
        glusterd_svc_t      *svc     = NULL;
        xlator_t            *this    = NULL;
        glusterd_volinfo_t  *volinfo = NULL;
        glusterd_snapdsvc_t *snapd   = NULL;

        this = THIS;
        GF_ASSERT (this);

        svc = cds_list_entry (conn, glusterd_svc_t, conn);
        if (!svc) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SVC_GET_FAIL, "Failed to get the service");
                return -1;
        }
        snapd = cds_list_entry (svc, glusterd_snapdsvc_t, svc);
        if (!snapd) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SNAPD_OBJ_GET_FAIL, "Failed to get the "
                        "snapd object");
                return -1;
        }

        volinfo = cds_list_entry (snapd, glusterd_volinfo_t, snapd);
        if (!volinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "Failed to get the "
                        "volinfo object");
                return -1;
        }

        switch (event) {
        case RPC_CLNT_CONNECT:
                gf_msg_debug (this->name, 0, "%s has connected with "
                        "glusterd.", svc->name);
                gf_event (EVENT_SVC_CONNECTED, "volume=%s;svc_name=%s",
                          volinfo->volname, svc->name);
                svc->online =  _gf_true;
                break;

        case RPC_CLNT_DISCONNECT:
                if (svc->online) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_NODE_DISCONNECTED, "%s has disconnected "
                                "from glusterd.", svc->name);
                        gf_event (EVENT_SVC_DISCONNECTED,
                                  "volume=%s;svc_name=%s", volinfo->volname,
                                  svc->name);
                        svc->online =  _gf_false;
                }
                break;

        case RPC_CLNT_DESTROY:
                glusterd_volinfo_unref (volinfo);

        default:
                gf_msg_trace (this->name, 0,
                        "got some other RPC event %d", event);
                break;
        }

        return ret;
}
