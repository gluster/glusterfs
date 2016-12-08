/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
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
#include "glusterd-messages.h"
#include "glusterd-store.h"
#include "run.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"

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
                gf_msg_debug (this->name, 0, "A remove-brick task on "
                        "volume %s is not yet committed", volinfo->volname);
                snprintf (op_errstr, len, "A remove-brick task on volume %s is"
                          " not yet committed. Either commit or stop the "
                          "remove-brick task.", volinfo->volname);
                goto out;
        }

        if (glusterd_is_defrag_on (volinfo)) {
                gf_msg_debug (this->name, 0,
                        "rebalance on volume %s already started",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance on %s is already started",
                          volinfo->volname);
                goto out;
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
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
        xlator_t               *this    = NULL;

        this = THIS;
        if (!this)
                return 0;

        priv = this->private;
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

               gf_msg_debug (this->name, 0, "%s got RPC_CLNT_CONNECT",
                        rpc->conn.name);
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

                rpc_clnt_reconnect_cleanup (&defrag->rpc->conn);
                glusterd_defrag_rpc_put (defrag);
                if (defrag->cbk_fn)
                        defrag->cbk_fn (volinfo,
                                        volinfo->rebal.defrag_status);

                GF_FREE (defrag);
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_REBALANCE_DISCONNECTED,
                        "Rebalance process for volume %s has disconnected.",
                        volinfo->volname);
                break;
        }
        case RPC_CLNT_DESTROY:
                glusterd_volinfo_unref (volinfo);
                break;
        default:
                gf_msg_trace (this->name, 0,
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
        char                   volname[PATH_MAX] = {0,};
        char                   valgrind_logfile[PATH_MAX] = {0,};
        char                   *volfileserver = NULL;

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
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "Failed to create "
                        "directory %s", defrag_path);
                goto out;
        }

        GLUSTERD_GET_DEFRAG_SOCK_FILE (sockfile, volinfo);
        GLUSTERD_GET_DEFRAG_PID_FILE (pidfile, volinfo, priv);
        snprintf (logfile, PATH_MAX, "%s/%s-%s.log",
                    DEFAULT_LOG_FILE_DIRECTORY, volinfo->volname,
                    (cmd == GF_DEFRAG_CMD_START_TIER ? "tier":"rebalance"));
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

        snprintf (volname, sizeof(volname), "rebalance/%s", volinfo->volname);

        if (dict_get_str (THIS->options, "transport.socket.bind-address",
                          &volfileserver) == 0) {
               /*In the case of running multiple glusterds on a single machine,
                *we should ensure that log file and unix socket file shouls be
                *unique in given cluster */

                GLUSTERD_GET_DEFRAG_SOCK_FILE_OLD (sockfile, volinfo,
                                                   priv);
                snprintf (logfile, PATH_MAX, "%s/%s-%s-%s.log",
                          DEFAULT_LOG_FILE_DIRECTORY, volinfo->volname,
                          (cmd == GF_DEFRAG_CMD_START_TIER ?
                           "tier":"rebalance"),
                          uuid_utoa(MY_UUID));

        } else {
                volfileserver = "localhost";
        }

        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", volfileserver, "--volfile-id", volname,
                         "--xlator-option", "*dht.use-readdirp=yes",
                         "--xlator-option", "*dht.lookup-unhashed=yes",
                         "--xlator-option", "*dht.assert-no-child-down=yes",
                         "--xlator-option", "*replicate*.data-self-heal=off",
                         "--xlator-option",
                         "*replicate*.metadata-self-heal=off",
                         "--xlator-option", "*replicate*.entry-self-heal=off",
                         "--xlator-option", "*dht.readdir-optimize=on",
                         NULL);

        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                runner_add_arg (&runner, "--xlator-option");
                runner_argprintf (&runner,
                                  "*tier-dht.xattr-name=trusted.tier.tier-dht");
        }

        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf ( &runner, "*dht.rebalance-cmd=%d",cmd);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.node-uuid=%s", uuid_utoa(MY_UUID));
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.commit-hash=%u",
                          volinfo->rebal.commit_hash);
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
                gf_msg_debug ("glusterd", 0, "rebalance command failed");
                goto out;
        }

        sleep (5);

        ret = glusterd_rebalance_rpc_create (volinfo);

        //FIXME: this cbk is passed as NULL in all occurrences. May be
        //we never needed it.
        if (cbk)
                defrag->cbk_fn = cbk;

out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_rebalance_defrag_init (glusterd_volinfo_t *volinfo,
                                defrag_cbk_fn_t cbk)

{
        glusterd_defrag_info_t  *defrag         = NULL;
        int                      ret            = -1;

        if (!volinfo->rebal.defrag) {
                volinfo->rebal.defrag =
                        GF_CALLOC (1, sizeof (*volinfo->rebal.defrag),
                                   gf_gld_mt_defrag_info);
        } else {
                /*
                 * if defrag variable is already initialized,
                 * we skip the initialization.
                 */
                ret = 0;
                goto out;
        }

        if (!volinfo->rebal.defrag)
                goto out;
        defrag = volinfo->rebal.defrag;

        defrag->cmd = volinfo->rebal.defrag_cmd;
        LOCK_INIT (&defrag->lock);
        if (cbk)
                defrag->cbk_fn = cbk;
        ret = 0;
out:
        return ret;

}

int
glusterd_rebalance_rpc_create (glusterd_volinfo_t *volinfo)
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

        GLUSTERD_GET_DEFRAG_SOCK_FILE (sockfile, volinfo);
        /* Check if defrag sockfile exists in the new location
         * in /var/run/ , if it does not try the old location
         */
        ret = sys_stat (sockfile, &buf);
        /* TODO: Remove this once we don't need backward compatibility
         * with the older path
         */
        if (ret && (errno == ENOENT)) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        GD_MSG_FILE_OP_FAILED, "Rebalance sockfile "
                        "%s does not exist. Trying old path.",
                        sockfile);
                GLUSTERD_GET_DEFRAG_SOCK_FILE_OLD (sockfile, volinfo,
                                                   priv);
                ret =sys_stat (sockfile, &buf);
                if (ret && (ENOENT == errno)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_REBAL_NO_SOCK_FILE, "Rebalance "
                                "sockfile %s does not exist", sockfile);
                        goto out;
                }
        }

        /* Setting frame-timeout to 10mins (600seconds).
         * Unix domain sockets ensures that the connection is reliable. The
         * default timeout of 30mins used for unreliable network connections is
         * too long for unix domain socket connections.
         */
        ret = rpc_transport_unix_options_build (&options, sockfile, 600);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, GD_MSG_UNIX_OP_BUILD_FAIL,
                        "Unix options build failed");
                goto out;
        }

        glusterd_volinfo_ref (volinfo);
        ret = glusterd_rpc_create (&defrag->rpc, options,
                                   glusterd_defrag_notify, volinfo, _gf_true);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, GD_MSG_RPC_CREATE_FAIL,
                        "Glusterd RPC creation failed");
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
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND, "Received rebalance on invalid"
                        " volname %s", volname);
                snprintf (op_errstr, len, "Volume %s does not exist",
                          volname);
                goto out;
        }
        if ((*volinfo)->brick_count <= (*volinfo)->dist_leaf_count) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_DISTRIBUTE, "Volume %s is not a "
                "distribute type or contains only 1 brick", volname);
                snprintf (op_errstr, len, "Volume %s is not a distribute "
                          "volume or contains only 1 brick.\n"
                          "Not performing rebalance", volname);
                goto out;
        }

        if ((*volinfo)->status != GLUSTERD_STATUS_STARTED) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_STOPPED, "Received rebalance on stopped"
                        " volname %s", volname);
                snprintf (op_errstr, len, "Volume %s needs to "
                          "be started to perform rebalance", volname);
                goto out;
        }

        ret = glusterd_disallow_op_for_tier (*volinfo, GD_OP_REBALANCE, cmd);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_REBALANCE_CMD_IN_TIER_VOL,
                        "Received rebalance command "
                        "on Tier volume %s", volname);
                snprintf (op_errstr, len, "Rebalance operations are not "
                          "supported on a tiered volume");
                goto out;
        }

        ret = 0;

out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (msg, sizeof (msg), "Unable to decode the "
                                  "command");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get volume name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", (int32_t*)&cmd);
        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get command");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }

        ret = dict_set_static_bin (dict, "node-uuid", MY_UUID, 16);
        if (ret)
                goto out;

        if ((cmd == GF_DEFRAG_CMD_STATUS) ||
            (cmd == GF_DEFRAG_CMD_STATUS_TIER) ||
            (cmd == GF_DEFRAG_CMD_STOP_DETACH_TIER) ||
            (cmd == GF_DEFRAG_CMD_STOP) ||
            (cmd == GF_DEFRAG_CMD_DETACH_STATUS)) {
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

static int
glusterd_brick_validation  (dict_t *dict, char *key, data_t *value,
                            void *data)
{
        int32_t                   ret              = -1;
        xlator_t                 *this             = NULL;
        glusterd_volinfo_t       *volinfo          = data;
        glusterd_brickinfo_t     *brickinfo        = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_volume_brickinfo_get_by_brick (value->data, volinfo,
                                                      &brickinfo,
                                                      _gf_false);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_BRICK_NOT_FOUND,
                        "Incorrect brick %s for "
                        "volume %s", value->data, volinfo->volname);
                return ret;
        }

        if (!brickinfo->decommissioned) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_BRICK_NOT_FOUND, "Incorrect brick %s for "
                        "volume %s", value->data, volinfo->volname);
                ret = -1;
                return ret;
        }

        return ret;
}

int
glusterd_op_stage_rebalance (dict_t *dict, char **op_errstr)
{
        char                    *volname     = NULL;
        char                    *cmd_str     = NULL;
        int                     ret          = 0;
        int32_t                 cmd          = 0;
        char                    msg[2048]    = {0};
        glusterd_volinfo_t      *volinfo     = NULL;
        char                    *task_id_str = NULL;
        dict_t                  *op_ctx      = NULL;
        xlator_t                *this        = 0;
        int32_t                 is_force     = 0;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg_debug (this->name, 0, "volname not found");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_msg_debug (this->name, 0, "cmd not found");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_msg_debug (this->name, 0, "failed to validate");
                goto out;
        }
        switch (cmd) {
        case GF_DEFRAG_CMD_START_TIER:
                ret = dict_get_int32 (dict, "force", &is_force);
                if (ret)
                        is_force = 0;

                if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                        gf_asprintf (op_errstr, "volume %s is not a tier "
                                     "volume.", volinfo->volname);
                        ret = -1;
                        goto out;
                }
                if ((!is_force) && glusterd_is_tier_daemon_running (volinfo)) {
                        ret = gf_asprintf (op_errstr, "A Tier daemon is "
                                        "already running on volume %s",
                                        volname);
                        ret = -1;
                        goto out;
                }
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
                /* Check if the connected clients are all of version
                 * glusterfs-3.6 and higher. This is needed to prevent some data
                 * loss issues that could occur when older clients are connected
                 * when rebalance is run. This check can be bypassed by using
                 * 'force'
                 */
                ret = glusterd_check_client_op_version_support
                        (volname, GD_OP_VERSION_3_6_0, NULL);
                if (ret) {
                        ret = gf_asprintf (op_errstr, "Volume %s has one or "
                                           "more connected clients of a version"
                                           " lower than GlusterFS-v3.6.0. "
                                           "Starting rebalance in this state "
                                           "could lead to data loss.\nPlease "
                                           "disconnect those clients before "
                                           "attempting this command again.",
                                           volname);
                        goto out;
                }

        case GF_DEFRAG_CMD_START_FORCE:
                if (is_origin_glusterd (dict)) {
                        op_ctx = glusterd_op_get_ctx ();
                        if (!op_ctx) {
                                ret = -1;
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_OPCTX_GET_FAIL,
                                        "Failed to get op_ctx");
                                goto out;
                        }

                        ret = glusterd_generate_and_set_task_id
                                (op_ctx, GF_REBALANCE_TID_KEY);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_TASKID_GEN_FAIL,
                                        "Failed to generate task-id");
                                goto out;
                        }
                } else {
                        ret = dict_get_str (dict, GF_REBALANCE_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                snprintf (msg, sizeof (msg),
                                          "Missing rebalance-id");
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_REBALANCE_ID_MISSING, "%s", msg);
                                ret = 0;
                        }
                }
                ret = glusterd_defrag_start_validate (volinfo, msg,
                                                      sizeof (msg),
                                                      GD_OP_REBALANCE);
                if (ret) {
                        gf_msg_debug (this->name, 0,
                                        "start validate failed");
                        goto out;
                }
                break;
        case GF_DEFRAG_CMD_STATUS_TIER:
        case GF_DEFRAG_CMD_STATUS:
        case GF_DEFRAG_CMD_STOP:

                ret = dict_get_str (dict, "cmd-str", &cmd_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Failed to get "
                                "command string");
                        ret = -1;
                        goto out;
                }
                if ((strstr(cmd_str, "rebalance") != NULL) &&
                    (volinfo->rebal.op != GD_OP_REBALANCE)) {
                        snprintf (msg, sizeof(msg), "Rebalance not started.");
                        ret = -1;
                        goto out;
                }

                if (strstr(cmd_str, "remove-brick") != NULL) {
                        if (volinfo->rebal.op != GD_OP_REMOVE_BRICK) {
                                snprintf (msg, sizeof(msg), "remove-brick not "
                                          "started.");
                                ret = -1;
                                goto out;
                        }

                        /* For remove-brick status/stop command check whether
                         * given input brick is part of volume or not.*/

                        ret = dict_foreach_fnmatch (dict, "brick*",
                                                    glusterd_brick_validation,
                                                    volinfo);
                        if (ret == -1) {
                                snprintf (msg, sizeof (msg), "Incorrect brick"
                                          " for volume %s", volinfo->volname);
                                goto out;
                        }
                }
                if (cmd == GF_DEFRAG_CMD_STATUS_TIER) {
                        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                                snprintf (msg, sizeof(msg), "volume %s is not "
                                          "a tier volume.", volinfo->volname);
                                ret = -1;
                                goto out;
                        }
                }

                break;

        case GF_DEFRAG_CMD_STOP_DETACH_TIER:
        case GF_DEFRAG_CMD_DETACH_STATUS:
                if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                        snprintf (msg, sizeof(msg), "volume %s is not "
                                  "a tier volume.", volinfo->volname);
                        ret = -1;
                        goto out;
                }

                if (volinfo->rebal.op != GD_OP_REMOVE_BRICK) {
                        snprintf (msg, sizeof(msg), "Detach-tier "
                                  "not started");
                        ret = -1;
                        goto out;
                }
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
        glusterd_brickinfo_t    *brickinfo = NULL;
        glusterd_brickinfo_t    *tmp      = NULL;
        gf_boolean_t            volfile_update = _gf_false;
        char                    *task_id_str = NULL;
        dict_t                  *ctx = NULL;
        xlator_t                *this = NULL;
        uint32_t                commit_hash;
        int32_t                 is_force    = 0;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg_debug (this->name, 0, "volname not given");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_msg_debug (this->name, 0, "command not given");
                goto out;
        }


        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_msg_debug (this->name, 0, "cmd validate failed");
                goto out;
        }

        /* Set task-id, if available, in op_ctx dict for operations other than
         * start
         */
        if (cmd == GF_DEFRAG_CMD_STATUS ||
            cmd == GF_DEFRAG_CMD_STOP ||
            cmd == GF_DEFRAG_CMD_STATUS_TIER) {
                if (!gf_uuid_is_null (volinfo->rebal.rebalance_id)) {
                        ctx = glusterd_op_get_ctx ();
                        if (!ctx) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_OPCTX_GET_FAIL,
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
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_TASKID_GEN_FAIL,
                                        "Failed to set task-id");
                                goto out;
                        }
                }
        }

        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_FORCE:
        case GF_DEFRAG_CMD_START_TIER:


                ret = dict_get_int32 (dict, "force", &is_force);
                if (ret)
                        is_force = 0;
                if (!is_force) {
                        /* Reset defrag status to 'NOT STARTED' whenever a
                         * remove-brick/rebalance command is issued to remove
                         * stale information from previous run.
                         */
                        volinfo->rebal.defrag_status =
                               GF_DEFRAG_STATUS_NOT_STARTED;

                        ret = dict_get_str (dict, GF_REBALANCE_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_msg_debug (this->name, 0, "Missing rebalance"
                                              " id");
                                ret = 0;
                        } else {
                                gf_uuid_parse (task_id_str,
                                               volinfo->rebal.rebalance_id);
                                volinfo->rebal.op = GD_OP_REBALANCE;
                        }
                        if (!gd_should_i_start_rebalance (volinfo)) {
                                /* Store the rebalance-id and rebalance command
                                 * even if the peer isn't starting a rebalance
                                 * process. On peers where a rebalance process
                                 * is started, glusterd_handle_defrag_start
                                 * performs the storing.
                                 * Storing this is needed for having
                                 * 'volume status' work correctly.
                                 */
                                glusterd_store_perform_node_state_store
                                        (volinfo);
                                break;
                        }
                        if (dict_get_uint32 (dict, "commit-hash", &commit_hash)
                                             == 0) {
                                volinfo->rebal.commit_hash = commit_hash;
                        }
                        ret = glusterd_handle_defrag_start (volinfo, msg,
                                        sizeof (msg),
                                        cmd, NULL, GD_OP_REBALANCE);
                        break;
                } else {
                        /* Reset defrag status to 'STARTED' so that the
                         * pid is checked and restarted accordingly.
                         * If the pid is not running it executes the
                         * "NOT_STARTED" case and restarts the process
                         */
                        volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_STARTED;
                        volinfo->rebal.defrag_cmd = cmd;
                        volinfo->rebal.op = GD_OP_REBALANCE;

                        ret = dict_get_str (dict, GF_REBALANCE_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_msg_debug (this->name, 0, "Missing rebalance"
                                              " id");
                                ret = 0;
                        } else {
                                gf_uuid_parse (task_id_str,
                                               volinfo->rebal.rebalance_id);
                                volinfo->rebal.op = GD_OP_REBALANCE;
                        }
                        if (dict_get_uint32 (dict, "commit-hash", &commit_hash)
                                             == 0) {
                                volinfo->rebal.commit_hash = commit_hash;
                        }
                        ret = glusterd_restart_rebalance_for_volume (volinfo);
                        break;
                }
        case GF_DEFRAG_CMD_STOP:
        case GF_DEFRAG_CMD_STOP_DETACH_TIER:
                /* Clear task-id only on explicitly stopping rebalance.
                 * Also clear the stored operation, so it doesn't cause trouble
                 * with future rebalance/remove-brick starts
                 */
                gf_uuid_clear (volinfo->rebal.rebalance_id);
                volinfo->rebal.op = GD_OP_NONE;

                /* Fall back to the old volume file in case of decommission*/
                cds_list_for_each_entry_safe (brickinfo, tmp, &volinfo->bricks,
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
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOLFILE_CREATE_FAIL,
                                "failed to create volfiles");
                        goto out;
                }

                ret = glusterd_store_volinfo (volinfo,
                                             GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOLINFO_SET_FAIL,
                                "failed to store volinfo");
                        goto out;
                }

                if (volinfo->type == GF_CLUSTER_TYPE_TIER &&
                                cmd == GF_OP_CMD_STOP_DETACH_TIER) {
                        glusterd_defrag_info_set (volinfo, dict,
                                        GF_DEFRAG_CMD_START_TIER,
                                        GF_DEFRAG_CMD_START,
                                        GD_OP_REBALANCE);
                        glusterd_restart_rebalance_for_volume (volinfo);
                }

                ret = 0;
                break;

        case GF_DEFRAG_CMD_START_DETACH_TIER:
        case GF_DEFRAG_CMD_STATUS:
        case GF_DEFRAG_CMD_STATUS_TIER:
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
        glusterd_volinfo_t      *volinfo     = NULL;
        char                    *volname     = NULL;
        char                    *volname_ptr = NULL;
        int32_t                  ret         = -1;
        xlator_t                *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get volname");
                return ret;
        }

        volname_ptr = strstr (volname, "rebalance/");
        if (volname_ptr) {
                volname_ptr = strchr (volname_ptr, '/');
                volname = volname_ptr + 1;
        } else {
                volname_ptr = strstr (volname, "tierd/");
                if (volname_ptr) {
                        volname_ptr = strchr (volname_ptr, '/');
                        if (!volname_ptr) {
                                ret = -1;
                                goto out;
                        }
                        volname = volname_ptr + 1;
                } else {

                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_NO_REBALANCE_PFX_IN_VOLNAME,
                                "volname received (%s) is not prefixed with "
                                "rebalance or tierd.", volname);
                        ret = -1;
                        goto out;
                }
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLINFO_GET_FAIL,
                        "Failed to get volinfo for %s"
                        , volname);
                return ret;
        }

        ret = glusterd_defrag_volume_status_update (volinfo, dict, 0);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DEFRAG_STATUS_UPDATE_FAIL,
                        "Failed to update status");
                gf_event (EVENT_REBALANCE_STATUS_UPDATE_FAILED, "volume=%s",
                          volinfo->volname);
        }

out:
        return ret;
}
