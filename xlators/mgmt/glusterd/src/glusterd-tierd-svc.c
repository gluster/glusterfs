/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
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
#include "glusterd-tierd-svc.h"
#include "glusterd-tierd-svc-helper.h"
#include "glusterd-svc-helper.h"
#include "syscall.h"
#include "glusterd-store.h"

char *tierd_svc_name = "tierd";

void
glusterd_tierdsvc_build (glusterd_svc_t *svc)
{
        svc->manager = glusterd_tierdsvc_manager;
        svc->start = glusterd_tierdsvc_start;
        svc->stop = glusterd_svc_stop;
        svc->reconfigure = glusterd_tierdsvc_reconfigure;
}

/* a separate service framework is used because the tierd is a
 * volume based framework while the common services are for node
 * based daemons. when volume based common framework is available
 * this can be consolidated into it.
 */

int
glusterd_tierdsvc_init (void *data)
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
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        volinfo = data;
        GF_VALIDATE_OR_GOTO (this->name, data, out);

        svc = &(volinfo->tierd.svc);

        ret = snprintf (svc->name, sizeof (svc->name), "%s", tierd_svc_name);
        if (ret < 0)
                goto out;

        notify = glusterd_svc_common_rpc_notify;
        glusterd_store_perform_node_state_store (volinfo);

        glusterd_svc_build_tierd_rundir (volinfo, rundir, sizeof (rundir));
        glusterd_svc_create_rundir (rundir);

        /* Initialize the connection mgmt */
        glusterd_svc_build_tierd_socket_filepath (volinfo, sockpath,
                                                  sizeof (sockpath));
        ret = glusterd_conn_init (&(svc->conn), sockpath, 600, notify);
        if (ret)
                goto out;

        /* Initialize the process mgmt */
        glusterd_svc_build_tierd_pidfile (volinfo, pidfile, sizeof (pidfile));
        glusterd_svc_build_tierd_volfile_path (volinfo, volfile,
                        sizeof (volfile));
        glusterd_svc_build_tierd_logdir (logdir, volinfo->volname,
                                         sizeof (logdir));
        ret = mkdir_p (logdir, 0755, _gf_true);
        if ((ret == -1) && (EEXIST != errno)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Unable to create logdir %s",
                        logdir);
                goto out;
        }
        glusterd_svc_build_tierd_logfile (logfile, logdir, sizeof (logfile));
        snprintf (volfileid, sizeof (volfileid), "tierd/%s", volinfo->volname);

        if (dict_get_str (this->options, "transport.socket.bind-address",
                          &volfileserver) != 0) {
                volfileserver = "localhost";
        }
        ret = glusterd_proc_init (&(svc->proc), tierd_svc_name, pidfile, logdir,
                                  logfile, volfile, volfileid, volfileserver);
        if (ret)
                goto out;

out:
        gf_msg_debug (this ? this->name : "glusterd", 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_tierdsvc_create_volfile (glusterd_volinfo_t *volinfo)
{
        char              filepath[PATH_MAX] = {0,};
        int               ret                = -1;
        glusterd_conf_t  *conf               = NULL;
        xlator_t         *this               = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);
        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        glusterd_svc_build_tierd_volfile_path (volinfo, filepath,
                        sizeof (filepath));
        ret = build_rebalance_volfile (volinfo, filepath, NULL);

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
glusterd_tierdsvc_manager (glusterd_svc_t *svc, void *data, int flags)
{
        int                 ret     = 0;
        xlator_t           *this    = THIS;
        glusterd_volinfo_t *volinfo = NULL;

        volinfo = data;
        GF_VALIDATE_OR_GOTO (this->name, data, out);

        if (!svc->inited) {
                ret = glusterd_tierdsvc_init (volinfo);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_TIERD_INIT_FAIL, "Failed to initialize "
                                "tierd service for volume %s",
                                volinfo->volname);
                        goto out;
                } else {
                        svc->inited = _gf_true;
                        gf_msg_debug (THIS->name, 0, "tierd service "
                                      "initialized");
                }
        }

        ret = glusterd_is_tierd_enabled (volinfo);
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
                                                GD_MSG_TIERD_STOP_FAIL,
                                                "Couldn't stop tierd for "
                                                "volume: %s",
                                                volinfo->volname);
                        } else {
                                ret = 0;
                        }
                        goto out;
                }

                ret = glusterd_tierdsvc_create_volfile (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_TIERD_CREATE_FAIL, "Couldn't create "
                                "tierd volfile for volume: %s",
                                volinfo->volname);
                        goto out;
                }

                ret = svc->start (svc, flags);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_TIERD_START_FAIL, "Couldn't start "
                                "tierd for volume: %s", volinfo->volname);
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
                                GD_MSG_TIERD_STOP_FAIL,
                                "Couldn't stop tierd for volume: %s",
                                volinfo->volname);
                        goto out;
                }
                volinfo->tierd.port = 0;
        }

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);

        return ret;
}


int32_t
glusterd_tierdsvc_start (glusterd_svc_t *svc, int flags)
{
        int                  ret                        = -1;
        runner_t             runner                     = {0,};
        glusterd_conf_t     *priv                       = NULL;
        xlator_t            *this                       = NULL;
        char                 valgrind_logfile[PATH_MAX] = {0};
        int                  tierd_port                 = 0;
        char                 msg[1024]                  = {0,};
        char                 tierd_id[PATH_MAX]         = {0,};
        glusterd_volinfo_t  *volinfo                    = NULL;
        glusterd_tierdsvc_t *tierd                      = NULL;
        int                  cmd                        = GF_DEFRAG_CMD_START_TIER;

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        if (glusterd_proc_is_running (&svc->proc)) {
                ret = 0;
                goto out;
        }

        /* Get volinfo->tierd from svc object */
        tierd = cds_list_entry (svc, glusterd_tierdsvc_t, svc);
        if (!tierd) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_TIERD_OBJ_GET_FAIL, "Failed to get tierd object "
                        "from tierd service");
                goto out;
        }

        /* Get volinfo from tierd */
        volinfo = cds_list_entry (tierd, glusterd_volinfo_t, tierd);
        if (!volinfo) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL, "Failed to get volinfo from "
                        "from tierd");
                goto out;
        }

        ret = sys_access (svc->proc.volfile, F_OK);
        if (ret) {
                gf_msg (this->name, GF_LOG_DEBUG, 0,
                        GD_MSG_VOLINFO_GET_FAIL,
                        "tierd Volfile %s is not present", svc->proc.volfile);
                /* If glusterd is down on one of the nodes and during
                 * that time if tier is started for the first time. After some
                 * time when the glusterd which was down comes back it tries
                 * to look for the tierd volfile and it does not find tierd
                 * volfile and because of this starting of tierd fails.
                 * Therefore, if volfile is not present then create a fresh
                 * volfile.
                 */
                ret = glusterd_tierdsvc_create_volfile (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL, "Couldn't create "
                                "tierd volfile for volume: %s",
                                volinfo->volname);
                        goto out;
                }
        }
        runinit (&runner);

        if (priv->valgrind) {
                snprintf (valgrind_logfile, PATH_MAX, "%s/valgrind-tierd.log",
                          svc->proc.logdir);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        snprintf (tierd_id, sizeof (tierd_id), "tierd-%s", volinfo->volname);
        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", svc->proc.volfileserver,
                         "--volfile-id", svc->proc.volfileid,
                         "-p", svc->proc.pidfile,
                         "-l", svc->proc.logfile,
                         "--brick-name", tierd_id,
                         "-S", svc->conn.sockpath,
                         "--xlator-option", "*dht.use-readdirp=yes",
                         "--xlator-option", "*dht.lookup-unhashed=yes",
                         "--xlator-option", "*dht.assert-no-child-down=yes",
                         "--xlator-option", "*replicate*.data-self-heal=off",
                         "--xlator-option",
                         "*replicate*.metadata-self-heal=off",
                         "--xlator-option", "*replicate*.entry-self-heal=off",
                         "--xlator-option", "*dht.readdir-optimize=on",
                         "--xlator-option",
                         "*tier-dht.xattr-name=trusted.tier.tier-dht",
                         NULL);


        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.rebalance-cmd=%d", cmd);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.node-uuid=%s", uuid_utoa(MY_UUID));
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.commit-hash=%u",
                          volinfo->rebal.commit_hash);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        /* Do a pmap registry remove on the older connected port */
        if (volinfo->tierd.port) {
                ret = pmap_registry_remove (this, volinfo->tierd.port,
                                tierd_id, GF_PMAP_PORT_BRICKSERVER,
                                NULL);
                if (ret) {
                        snprintf (msg, sizeof (msg), "Failed to remove pmap "
                                        "registry for older signin");
                        goto out;
                }
        }



        tierd_port = pmap_registry_alloc (this);
        if (!tierd_port) {
                snprintf (msg, sizeof (msg), "Could not allocate port "
                                "for tierd service for volume %s",
                                volinfo->volname);
                runner_log (&runner, this->name, GF_LOG_DEBUG, msg);
                ret = -1;
                goto out;
        }

        volinfo->tierd.port = tierd_port;

        snprintf (msg, sizeof (msg),
                  "Starting the tierd service for volume %s", volinfo->volname);
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
glusterd_tierdsvc_restart ()
{
        glusterd_volinfo_t      *volinfo        = NULL;
        int                     ret             = 0;
        xlator_t                *this           = THIS;
        glusterd_conf_t         *conf           = NULL;
        glusterd_svc_t          *svc            = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        cds_list_for_each_entry (volinfo, &conf->volumes, vol_list) {
                /* Start per volume tierd svc */
                if (volinfo->status == GLUSTERD_STATUS_STARTED &&
                    glusterd_is_tierd_enabled (volinfo)) {
                        svc = &(volinfo->tierd.svc);
                        ret = svc->manager (svc, volinfo, PROC_START_NO_WAIT);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_TIERD_START_FAIL,
                                        "Couldn't restart tierd for "
                                        "vol: %s", volinfo->volname);
                                goto out;
                        }
                }
        }
out:
        return ret;
}


int
glusterd_tierdsvc_reconfigure (void *data)
{
        int                     ret                     = -1;
        xlator_t                *this                   = NULL;
        gf_boolean_t            identical_topology      = _gf_false;
        gf_boolean_t            identical_volfile       = _gf_false;
        glusterd_volinfo_t      *volinfo                = NULL;

        volinfo = data;
        GF_VALIDATE_OR_GOTO (this->name, data, out);

        /* reconfigure function is not available for other volume based
         * service. but it has been implemented for tier because there can be
         * changes on the volfile that need not be related to topology.
         * during these changes it is better not to restart the tierd.
         * So reconfigure is written to avoid calling restart at such
         * situations.
         */

        this = THIS;
        GF_VALIDATE_OR_GOTO (THIS->name, this, out);

        if (glusterd_is_tierd_enabled (volinfo))
                goto manager;
        /*
         * Check both OLD and NEW volfiles, if they are SAME by size
         * and cksum i.e. "character-by-character". If YES, then
         * NOTHING has been changed, just return.
         */

        ret = glusterd_svc_check_tier_volfile_identical
                (volinfo->tierd.svc.name, volinfo, &identical_volfile);
        if (ret)
                goto out;
        if (identical_volfile) {
                ret = 0;
                goto out;
        }

        /*
         * They are not identical. Find out if the topology is changed
         * OR just the volume options. If just the options which got
         * changed, then inform the xlator to reconfigure the options.
         */
        ret = glusterd_svc_check_tier_topology_identical
                (volinfo->tierd.svc.name, volinfo, &identical_topology);
        if (ret)
                goto out; /*not able to compare due to some corruption */

        /* Topology is not changed, but just the options. But write the
         * options to tierd volfile, so that tierd will be reconfigured.
         */
        if (identical_topology) {
                ret = glusterd_tierdsvc_create_volfile (volinfo);
                if (ret == 0) {/* Only if above PASSES */
                        ret = glusterd_fetchspec_notify (this);
                }
                goto out;
        }
        goto out;
        /*pending add/remove brick functionality*/

manager:
        /*
         * tierd volfile's topology has been changed. tierd server needs
         * to be RESTARTED to ACT on the changed volfile.
         */
        ret = volinfo->tierd.svc.manager (&(volinfo->tierd.svc),
                                          volinfo, PROC_START_NO_WAIT);

out:
        gf_msg_debug (THIS->name, 0, "Returning %d", ret);
        return ret;
}
