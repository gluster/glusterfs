/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

#include "globals.h"
#include "compat.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include "run.h"
#include "glusterd-volgen.h"

#include "syscall.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"

int32_t
glusterd_brick_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe);
int
glusterd_defrag_start_validate (glusterd_volinfo_t *volinfo, char *op_errstr,
                                size_t len, glusterd_op_t op)
{
        int      ret = -1;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        /* Check only if operation is not remove-brick */
        if ((GD_OP_REMOVE_BRICK != op) &&
            !gd_is_remove_brick_committed (volinfo)) {
                gf_log (this->name, GF_LOG_DEBUG, "A remove-brick task on "
                        "volume %s is not yet committed", volinfo->volname);
                snprintf (op_errstr, len, "A remove-brick task on volume %s is"
                          " not yet committed. Either commit or stop the "
                          "remove-brick task.", volinfo->volname);
                goto out;
        }

        if (glusterd_is_defrag_on (volinfo)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "rebalance on volume %s already started",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance on %s is already started",
                          volinfo->volname);
                goto out;
        }

        if (glusterd_is_rb_started (volinfo) ||
            glusterd_is_rb_paused (volinfo)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Rebalance failed as replace brick is in progress on volume %s",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance failed as replace brick is in progress on "
                          "volume %s", volinfo->volname);
                goto out;
        }
        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
__glusterd_defrag_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event, void *data)
{
        glusterd_volinfo_t      *volinfo = NULL;
        glusterd_defrag_info_t  *defrag  = NULL;
        int                     ret      = 0;
        char                    pidfile[PATH_MAX];
        glusterd_conf_t        *priv    = NULL;

        priv = THIS->private;
        if (!priv)
                return 0;

        volinfo = mydata;
        if (!volinfo)
                return 0;

        defrag = volinfo->rebal.defrag;
        if (!defrag)
                return 0;

        if ((event == RPC_CLNT_DISCONNECT) && defrag->connected)
                volinfo->rebal.defrag = NULL;

        GLUSTERD_GET_DEFRAG_PID_FILE(pidfile, volinfo, priv);

        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                if (defrag->connected)
                        return 0;

                LOCK (&defrag->lock);
                {
                        defrag->connected = 1;
                }
                UNLOCK (&defrag->lock);

               gf_log ("", GF_LOG_DEBUG, "%s got RPC_CLNT_CONNECT",
                        rpc->conn.trans->name);
               break;
        }

        case RPC_CLNT_DISCONNECT:
        {
                if (!defrag->connected)
                        return 0;

                LOCK (&defrag->lock);
                {
                        defrag->connected = 0;
                }
                UNLOCK (&defrag->lock);

                if (!gf_is_service_running (pidfile, NULL)) {
                        if (volinfo->rebal.defrag_status ==
                                                GF_DEFRAG_STATUS_STARTED) {
                                volinfo->rebal.defrag_status =
                                                   GF_DEFRAG_STATUS_FAILED;
                        }
                 }

                glusterd_store_perform_node_state_store (volinfo);

                if (defrag->rpc) {
                        glusterd_rpc_clnt_unref (priv, defrag->rpc);
                        defrag->rpc = NULL;
                }
                if (defrag->cbk_fn)
                        defrag->cbk_fn (volinfo,
                                        volinfo->rebal.defrag_status);

                GF_FREE (defrag);
                gf_log ("", GF_LOG_DEBUG, "%s got RPC_CLNT_DISCONNECT",
                        rpc->conn.trans->name);
                break;
        }
        case RPC_CLNT_DESTROY:
                glusterd_volinfo_unref (volinfo);
                break;
        default:
                gf_log ("", GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        return ret;
}

int32_t
glusterd_defrag_notify (struct rpc_clnt *rpc, void *mydata,
                        rpc_clnt_event_t event, void *data)
{
        return glusterd_big_locked_notify (rpc, mydata, event,
                                           data, __glusterd_defrag_notify);
}

int
glusterd_handle_defrag_start (glusterd_volinfo_t *volinfo, char *op_errstr,
                              size_t len, int cmd, defrag_cbk_fn_t cbk,
                              glusterd_op_t op)
{
        int                    ret = -1;
        glusterd_defrag_info_t *defrag =  NULL;
        runner_t               runner = {0,};
        glusterd_conf_t        *priv = NULL;
        char                   defrag_path[PATH_MAX];
        char                   sockfile[PATH_MAX] = {0,};
        char                   pidfile[PATH_MAX] = {0,};
        char                   logfile[PATH_MAX] = {0,};
        char                   valgrind_logfile[PATH_MAX] = {0,};

        priv    = THIS->private;

        GF_ASSERT (volinfo);
        GF_ASSERT (op_errstr);

        ret = glusterd_defrag_start_validate (volinfo, op_errstr, len, op);
        if (ret)
                goto out;
        if (!volinfo->rebal.defrag)
                volinfo->rebal.defrag =
                        GF_CALLOC (1, sizeof (*volinfo->rebal.defrag),
                                   gf_gld_mt_defrag_info);
        if (!volinfo->rebal.defrag)
                goto out;

        defrag = volinfo->rebal.defrag;

        defrag->cmd = cmd;

        volinfo->rebal.defrag_cmd = cmd;
        volinfo->rebal.op = op;

        LOCK_INIT (&defrag->lock);

        volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_STARTED;

        glusterd_volinfo_reset_defrag_stats (volinfo);
        glusterd_store_perform_node_state_store (volinfo);

        GLUSTERD_GET_DEFRAG_DIR (defrag_path, volinfo, priv);
        ret = mkdir_p (defrag_path, 0777, _gf_true);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to create "
                        "directory %s", defrag_path);
                goto out;
        }

        GLUSTERD_GET_DEFRAG_SOCK_FILE (sockfile, volinfo);
        GLUSTERD_GET_DEFRAG_PID_FILE (pidfile, volinfo, priv);
        snprintf (logfile, PATH_MAX, "%s/%s-rebalance.log",
                    DEFAULT_LOG_FILE_DIRECTORY, volinfo->volname);
        runinit (&runner);

        if (priv->valgrind) {
                snprintf (valgrind_logfile, PATH_MAX,
                          "%s/valgrind-%s-rebalance.log",
                          DEFAULT_LOG_FILE_DIRECTORY,
                          volinfo->volname);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", "--track-origins=yes",
                                 NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }

        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", "localhost", "--volfile-id", volinfo->volname,
                         "--xlator-option", "*dht.use-readdirp=yes",
                         "--xlator-option", "*dht.lookup-unhashed=yes",
                         "--xlator-option", "*dht.assert-no-child-down=yes",
                         "--xlator-option", "*replicate*.data-self-heal=off",
                         "--xlator-option",
                         "*replicate*.metadata-self-heal=off",
                         "--xlator-option", "*replicate*.entry-self-heal=off",
                         "--xlator-option", "*replicate*.readdir-failover=off",
                         "--xlator-option", "*dht.readdir-optimize=on",
                         NULL);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf ( &runner, "*dht.rebalance-cmd=%d",cmd);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.node-uuid=%s", uuid_utoa(MY_UUID));
        runner_add_arg (&runner, "--socket-file");
        runner_argprintf (&runner, "%s",sockfile);
        runner_add_arg (&runner, "--pid-file");
        runner_argprintf (&runner, "%s",pidfile);
        runner_add_arg (&runner, "-l");
        runner_argprintf (&runner, logfile);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        ret = runner_run_nowait (&runner);
        if (ret) {
                gf_log ("glusterd", GF_LOG_DEBUG, "rebalance command failed");
                goto out;
        }

        sleep (5);

        ret = glusterd_rebalance_rpc_create (volinfo, _gf_false);

        //FIXME: this cbk is passed as NULL in all occurrences. May be
        //we never needed it.
        if (cbk)
                defrag->cbk_fn = cbk;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int
glusterd_rebalance_rpc_create (glusterd_volinfo_t *volinfo,
                               gf_boolean_t reconnect)
{
        dict_t                  *options = NULL;
        char                     sockfile[PATH_MAX] = {0,};
        int                      ret = -1;
        glusterd_defrag_info_t  *defrag = volinfo->rebal.defrag;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        struct stat             buf = {0,};

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        //rebalance process is not started
        if (!defrag)
                goto out;

        //rpc obj for rebalance process already in place.
        if (defrag->rpc) {
                ret = 0;
                goto out;
        }
        GLUSTERD_GET_DEFRAG_SOCK_FILE (sockfile, volinfo);
        /* If reconnecting check if defrag sockfile exists in the new location
         * in /var/run/ , if it does not try the old location
         */
        if (reconnect) {
                ret = sys_stat (sockfile, &buf);
                /* TODO: Remove this once we don't need backward compatability
                 * with the older path
                 */
                if (ret && (errno == ENOENT)) {
                        gf_log (this->name, GF_LOG_WARNING, "Rebalance sockfile "
                                "%s does not exist. Trying old path.",
                                sockfile);
                        GLUSTERD_GET_DEFRAG_SOCK_FILE_OLD (sockfile, volinfo,
                                                           priv);
                        ret =sys_stat (sockfile, &buf);
                        if (ret && (ENOENT == errno)) {
                                gf_log (this->name, GF_LOG_ERROR, "Rebalance "
                                        "sockfile %s does not exist.",
                                        sockfile);
                                goto out;
                        }
                }
        }

        /* Setting frame-timeout to 10mins (600seconds).
         * Unix domain sockets ensures that the connection is reliable. The
         * default timeout of 30mins used for unreliable network connections is
         * too long for unix domain socket connections.
         */
        ret = rpc_transport_unix_options_build (&options, sockfile, 600);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Unix options build failed");
                goto out;
        }

        glusterd_volinfo_ref (volinfo);
        synclock_unlock (&priv->big_lock);
        ret = glusterd_rpc_create (&defrag->rpc, options,
                                   glusterd_defrag_notify, volinfo);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "RPC create failed");
                goto out;
        }
        ret = 0;
out:
        return ret;
}

int
glusterd_rebalance_cmd_validate (int cmd, char *volname,
                                 glusterd_volinfo_t **volinfo,
                                 char *op_errstr, size_t len)
{
        int ret = -1;

        if (glusterd_volinfo_find(volname, volinfo)) {
                gf_log ("glusterd", GF_LOG_ERROR, "Received rebalance on invalid"
                        " volname %s", volname);
                snprintf (op_errstr, len, "Volume %s does not exist",
                          volname);
                goto out;
        }
        if ((*volinfo)->brick_count <= (*volinfo)->dist_leaf_count) {
                gf_log ("glusterd", GF_LOG_ERROR, "Volume %s is not a "
                "distribute type or contains only 1 brick", volname);
                snprintf (op_errstr, len, "Volume %s is not a distribute "
                          "volume or contains only 1 brick.\n"
                          "Not performing rebalance", volname);
                goto out;
        }

        if ((*volinfo)->status != GLUSTERD_STATUS_STARTED) {
                gf_log ("glusterd", GF_LOG_ERROR, "Received rebalance on stopped"
                        " volname %s", volname);
                snprintf (op_errstr, len, "Volume %s needs to "
                          "be started to perform rebalance", volname);
                goto out;
        }

        ret = 0;

out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
__glusterd_handle_defrag_volume (rpcsvc_request_t *req)
{
        int32_t                 ret       = -1;
        gf_cli_req              cli_req   = {{0,}};
        glusterd_conf_t        *priv      = NULL;
        dict_t                 *dict      = NULL;
        char                   *volname   = NULL;
        gf_cli_defrag_type      cmd       = 0;
        char                    msg[2048] = {0,};
        xlator_t               *this      = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg, sizeof (msg), "Unable to decode the "
                                  "command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get volume name");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", (int32_t*)&cmd);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get command");
                gf_log (this->name, GF_LOG_ERROR, "%s", msg);
                goto out;
        }

        ret = dict_set_static_bin (dict, "node-uuid", MY_UUID, 16);
        if (ret)
                goto out;

        if ((cmd == GF_DEFRAG_CMD_STATUS) ||
              (cmd == GF_DEFRAG_CMD_STOP)) {
                ret = glusterd_op_begin (req, GD_OP_DEFRAG_BRICK_VOLUME,
                                         dict, msg, sizeof (msg));
        } else
                ret = glusterd_op_begin (req, GD_OP_REBALANCE, dict,
                                         msg, sizeof (msg));

out:

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                ret = glusterd_op_send_cli_response (GD_OP_REBALANCE, ret, 0,
                                                     req, dict, msg);

        }

        free (cli_req.dict.dict_val);//malloced by xdr

        return 0;
}

int
glusterd_handle_defrag_volume (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_defrag_volume);
}


int
glusterd_op_stage_rebalance (dict_t *dict, char **op_errstr)
{
        char                    *volname = NULL;
        int                     ret = 0;
        int32_t                 cmd = 0;
        char                    msg[2048] = {0};
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *task_id_str = NULL;
        dict_t                  *op_ctx = NULL;
        xlator_t                *this = 0;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "volname not found");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "cmd not found");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to validate");
                goto out;
        }
        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_FORCE:
                if (is_origin_glusterd ()) {
                        op_ctx = glusterd_op_get_ctx ();
                        if (!op_ctx) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get op_ctx");
                                goto out;
                        }

                        ret = glusterd_generate_and_set_task_id
                                (op_ctx, GF_REBALANCE_TID_KEY);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to generate task-id");
                                goto out;
                        }
                } else {
                        ret = dict_get_str (dict, GF_REBALANCE_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                snprintf (msg, sizeof (msg),
                                          "Missing rebalance-id");
                                gf_log (this->name, GF_LOG_WARNING, "%s", msg);
                                ret = 0;
                        }
                }
                ret = glusterd_defrag_start_validate (volinfo, msg,
                                                      sizeof (msg),
                                                      GD_OP_REBALANCE);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                        "start validate failed");
                        goto out;
                }
                break;
        case GF_DEFRAG_CMD_STATUS:
        case GF_DEFRAG_CMD_STOP:
                break;
        default:
                break;
        }

        ret = 0;
out:
        if (ret && op_errstr && msg[0])
                *op_errstr = gf_strdup (msg);

        return ret;
}


int
glusterd_op_rebalance (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char                    *volname   = NULL;
        int                     ret       = 0;
        int32_t                 cmd       = 0;
        char                    msg[2048] = {0};
        glusterd_volinfo_t      *volinfo   = NULL;
        glusterd_conf_t         *priv      = NULL;
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_brickinfo_t    *tmp      = NULL;
        gf_boolean_t            volfile_update = _gf_false;
        char                    *task_id_str = NULL;
        dict_t                  *ctx = NULL;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "volname not given");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "command not given");
                goto out;
        }


        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "cmd validate failed");
                goto out;
        }

        /* Set task-id, if available, in op_ctx dict for operations other than
         * start
         */
        if (cmd == GF_DEFRAG_CMD_STATUS || cmd == GF_DEFRAG_CMD_STOP) {
                if (!uuid_is_null (volinfo->rebal.rebalance_id)) {
                        ctx = glusterd_op_get_ctx ();
                        if (!ctx) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to get op_ctx");
                                ret = -1;
                                goto out;
                        }

                        if (GD_OP_REMOVE_BRICK == volinfo->rebal.op)
                                ret = glusterd_copy_uuid_to_dict
                                        (volinfo->rebal.rebalance_id, ctx,
                                         GF_REMOVE_BRICK_TID_KEY);
                        else
                                ret = glusterd_copy_uuid_to_dict
                                        (volinfo->rebal.rebalance_id, ctx,
                                         GF_REBALANCE_TID_KEY);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to set task-id");
                                goto out;
                        }
                }
        }

        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_FORCE:
                /* Reset defrag status to 'NOT STARTED' whenever a
                 * remove-brick/rebalance command is issued to remove
                 * stale information from previous run.
                 */
                volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_NOT_STARTED;

                ret = dict_get_str (dict, GF_REBALANCE_TID_KEY, &task_id_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG, "Missing rebalance "
                                "id");
                        ret = 0;
                } else {
                        uuid_parse (task_id_str, volinfo->rebal.rebalance_id) ;
                        volinfo->rebal.op = GD_OP_REBALANCE;
                }
                if (!gd_should_i_start_rebalance (volinfo))
                        break;
                ret = glusterd_handle_defrag_start (volinfo, msg, sizeof (msg),
                                                    cmd, NULL, GD_OP_REBALANCE);
                break;
        case GF_DEFRAG_CMD_STOP:
                /* Clear task-id only on explicitly stopping rebalance.
                 * Also clear the stored operation, so it doesn't cause trouble
                 * with future rebalance/remove-brick starts
                 */
                uuid_clear (volinfo->rebal.rebalance_id);
                volinfo->rebal.op = GD_OP_NONE;

                /* Fall back to the old volume file in case of decommission*/
                list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
                                          brick_list) {
                        if (!brickinfo->decommissioned)
                                continue;
                        brickinfo->decommissioned = 0;
                        volfile_update = _gf_true;
                }

                if (volfile_update == _gf_false) {
                        ret = 0;
                        break;
                }

                ret = glusterd_create_volfiles_and_notify_services (volinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to create volfiles");
                        goto out;
                }

                ret = glusterd_store_volinfo (volinfo,
                                             GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to store volinfo");
                        goto out;
                }

                ret = 0;
                break;

        case GF_DEFRAG_CMD_STATUS:
                break;
        default:
                break;
        }

out:
        if (ret && op_errstr && msg[0])
                *op_errstr = gf_strdup (msg);

        return ret;
}

int32_t
glusterd_defrag_event_notify_handle (dict_t *dict)
{
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *volname = NULL;
        int32_t                  ret     = -1;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Failed to get volname");
                return ret;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Failed to get volinfo for %s"
                        , volname);
                return ret;
        }

        ret = glusterd_defrag_volume_status_update (volinfo, dict);

        if (ret)
                gf_log ("", GF_LOG_ERROR, "Failed to update status");
        return ret;
}
