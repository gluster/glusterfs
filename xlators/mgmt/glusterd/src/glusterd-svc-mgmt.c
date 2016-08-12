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
#include "glusterd-proc-mgmt.h"
#include "glusterd-conn-mgmt.h"
#include "glusterd-messages.h"
#include "syscall.h"

int
glusterd_svc_create_rundir (char *rundir)
{
        int ret      = -1;

        ret = mkdir_p (rundir, 0777, _gf_true);
        if ((ret == -1) && (EEXIST != errno)) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create rundir %s",
                        rundir);
        }
        return ret;
}

static void
glusterd_svc_build_logfile_path (char *server, char *logdir, char *logfile,
                                 size_t len)
{
        snprintf (logfile, len, "%s/%s.log", logdir, server);
}

static void
glusterd_svc_build_volfileid_path (char *server, char *volfileid, size_t len)
{
        snprintf (volfileid, len, "gluster/%s", server);
}

static int
glusterd_svc_init_common (glusterd_svc_t *svc,
                          char *svc_name, char *workdir,
                          char *rundir, char *logdir,
                          glusterd_conn_notify_t notify)
{
        int                     ret                        = -1;
        glusterd_conf_t        *priv                       = NULL;
        xlator_t               *this                       = NULL;
        char                    pidfile[PATH_MAX]          = {0,};
        char                    logfile[PATH_MAX]          = {0,};
        char                    volfile[PATH_MAX]          = {0,};
        char                    sockfpath[PATH_MAX]        = {0,};
        char                    volfileid[256]             = {0};
        char                    *volfileserver             = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = snprintf (svc->name, sizeof (svc->name), "%s", svc_name);
        if (ret < 0)
                goto out;

        if (!notify)
                notify = glusterd_svc_common_rpc_notify;

        glusterd_svc_create_rundir (rundir);

        /* Initialize the connection mgmt */
        glusterd_conn_build_socket_filepath (rundir, MY_UUID,
                                             sockfpath, sizeof (sockfpath));

        ret = glusterd_conn_init (&(svc->conn), sockfpath, 600, notify);
        if (ret)
                goto out;

        /* Initialize the process mgmt */
        glusterd_svc_build_pidfile_path (svc_name, workdir, pidfile,
                                         sizeof(pidfile));
        glusterd_svc_build_volfile_path (svc_name, workdir, volfile,
                                         sizeof (volfile));

        glusterd_svc_build_logfile_path (svc_name, logdir, logfile,
                                         sizeof (logfile));
        glusterd_svc_build_volfileid_path (svc_name, volfileid,
                                           sizeof(volfileid));

        if (dict_get_str (this->options, "transport.socket.bind-address",
                          &volfileserver) != 0) {
                volfileserver = "localhost";
        }

        ret = glusterd_proc_init (&(svc->proc), svc_name, pidfile, logdir,
                                  logfile, volfile, volfileid, volfileserver);
        if (ret)
                goto out;

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}


static int
svc_add_args (dict_t *cmdline, char *arg, data_t *value, void *data)
{
        runner_t *runner = data;
        runner_add_arg (runner, value->data);
        return 0;
}

int glusterd_svc_init (glusterd_svc_t *svc, char *svc_name)
{
        int              ret               = -1;
        char             rundir[PATH_MAX]  = {0,};
        glusterd_conf_t *priv              = NULL;
        xlator_t        *this              = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        glusterd_svc_build_rundir (svc_name, priv->workdir, rundir,
                                   sizeof (rundir));
        ret = glusterd_svc_init_common (svc, svc_name, priv->workdir, rundir,
                                        DEFAULT_LOG_FILE_DIRECTORY, NULL);

        return ret;
}

int
glusterd_svc_start (glusterd_svc_t *svc, int flags, dict_t *cmdline)
{
        int                  ret                        = -1;
        runner_t             runner                     = {0,};
        glusterd_conf_t     *priv                       = NULL;
        xlator_t            *this                       = NULL;
        char                 valgrind_logfile[PATH_MAX] = {0};

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        if (glusterd_proc_is_running (&(svc->proc))) {
                ret = 0;
                goto out;
        }

        ret = sys_access (svc->proc.volfile, F_OK);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_NOT_FOUND, "Volfile %s is not present",
                        svc->proc.volfile);
                goto out;
        }

        runinit (&runner);

        if (priv->valgrind) {
                snprintf (valgrind_logfile, PATH_MAX, "%s/valgrind-%s.log",
                          svc->proc.logfile, svc->name);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", svc->proc.volfileserver,
                         "--volfile-id", svc->proc.volfileid,
                         "-p", svc->proc.pidfile,
                         "-l", svc->proc.logfile,
                         "-S", svc->conn.sockpath,
                         NULL);

        if (cmdline)
                dict_foreach (cmdline, svc_add_args, (void *) &runner);

        gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_SVC_START_SUCCESS,
                "Starting %s service", svc->name);

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
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

int glusterd_svc_stop (glusterd_svc_t *svc, int sig)
{
        int ret = -1;

        ret = glusterd_proc_stop (&(svc->proc), sig, PROC_STOP_FORCE);
        if (ret)
                goto out;
        glusterd_conn_disconnect (&(svc->conn));

        if (ret == 0) {
                svc->online =  _gf_false;
                (void) glusterd_unlink_file ((char *)svc->conn.sockpath);
        }
        gf_msg (THIS->name, GF_LOG_INFO, 0, GD_MSG_SVC_STOP_SUCCESS,
                "%s service is stopped", svc->name);
out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}

void
glusterd_svc_build_pidfile_path (char *server, char *workdir, char *path,
                                 size_t len)
{
        char    dir[PATH_MAX] = {0};

        GF_ASSERT (len == PATH_MAX);

        glusterd_svc_build_rundir (server, workdir, dir, sizeof (dir));
        snprintf (path, len, "%s/%s.pid", dir, server);
}

void
glusterd_svc_build_volfile_path (char *server, char *workdir, char *volfile,
                                 size_t len)
{
        char  dir[PATH_MAX] = {0,};

        GF_ASSERT (len == PATH_MAX);

        glusterd_svc_build_svcdir (server, workdir, dir, sizeof (dir));

        if (!strcmp(server, "quotad")) /*quotad has different volfile name*/
                snprintf (volfile, len, "%s/%s.vol", dir, server);
       else
                snprintf (volfile, len, "%s/%s-server.vol", dir, server);
}

void
glusterd_svc_build_svcdir (char *server, char *workdir, char *path, size_t len)
{
        GF_ASSERT (len == PATH_MAX);

        snprintf (path, len, "%s/%s", workdir, server);
}

void
glusterd_svc_build_rundir (char *server, char *workdir, char *path, size_t len)
{
        char    dir[PATH_MAX] = {0};

        GF_ASSERT (len == PATH_MAX);

        glusterd_svc_build_svcdir (server, workdir, dir, sizeof (dir));
        snprintf (path, len, "%s/run", dir);
}

int
glusterd_svc_reconfigure (int (*create_volfile) ())
{
        int ret = -1;

        ret = create_volfile ();
        if (ret)
                goto out;

        ret = glusterd_fetchspec_notify (THIS);
out:
        return ret;
}

int
glusterd_svc_common_rpc_notify (glusterd_conn_t *conn,
                                rpc_clnt_event_t event)
{
        int                  ret  = 0;
        glusterd_svc_t      *svc  = NULL;
        xlator_t            *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        /* Get the parent onject i.e. svc using list_entry macro */
        svc = cds_list_entry (conn, glusterd_svc_t, conn);
        if (!svc) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SVC_GET_FAIL, "Failed to get the service");
                return -1;
        }

        switch (event) {
        case RPC_CLNT_CONNECT:
                gf_msg_debug (this->name, 0, "%s has connected with "
                        "glusterd.", svc->name);
                gf_event (EVENT_SVC_CONNECTED, "svc_name=%s", svc->name);
                svc->online =  _gf_true;
                break;

        case RPC_CLNT_DISCONNECT:
                if (svc->online) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_NODE_DISCONNECTED, "%s has disconnected "
                                "from glusterd.", svc->name);
                        gf_event (EVENT_SVC_DISCONNECTED, "svc_name=%s",
                                  svc->name);
                        svc->online =  _gf_false;
                }
                break;

        default:
                gf_msg_trace (this->name, 0,
                        "got some other RPC event %d", event);
                break;
        }

        return ret;
}
