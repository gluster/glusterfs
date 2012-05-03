/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
glusterd3_1_brick_op_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe);

void
glusterd_rebalance_cmd_attempted_log (int cmd, char *volname)
{
        switch (cmd) {
                case GF_DEFRAG_CMD_START_LAYOUT_FIX:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start fix layout , attempted",
                                    volname);
                        gf_log ("glusterd", GF_LOG_INFO, "Received rebalance "
                                "volume start layout fix on %s", volname);
                        break;
                case GF_DEFRAG_CMD_START_FORCE:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start data force attempted",
                                    volname);
                        gf_log ("glusterd", GF_LOG_INFO, "Received rebalance "
                                "volume start migrate data on %s", volname);
                        break;
                case GF_DEFRAG_CMD_START:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: start, attempted", volname);
                        gf_log ("glusterd", GF_LOG_INFO, "Received rebalance "
                                "volume start on %s", volname);
                        break;
                case GF_DEFRAG_CMD_STOP:
                        gf_cmd_log ("Volume rebalance"," on volname: %s "
                                    "cmd: stop, attempted", volname);
                        gf_log ("glusterd", GF_LOG_INFO, "Received rebalance "
                                "volume stop on %s", volname);
                        break;
                default:
                        break;
        }
}

void
glusterd_rebalance_cmd_log (int cmd, char *volname, int status)
{
        if (cmd != GF_DEFRAG_CMD_STATUS) {
                gf_cmd_log ("volume rebalance"," on volname: %s %d %s",
                            volname, cmd, ((status)?"FAILED":"SUCCESS"));
        }
}

int
glusterd_defrag_start_validate (glusterd_volinfo_t *volinfo, char *op_errstr,
                                size_t len)
{
        int     ret = -1;

        if (glusterd_is_defrag_on (volinfo)) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "rebalance on volume %s already started",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance on %s is already started",
                          volinfo->volname);
                goto out;
        }

        if (glusterd_is_rb_started (volinfo) ||
            glusterd_is_rb_paused (volinfo)) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "Rebalance failed as replace brick is in progress on volume %s",
                        volinfo->volname);
                snprintf (op_errstr, len, "Rebalance failed as replace brick is in progress on "
                          "volume %s", volinfo->volname);
                goto out;
        }
        ret = 0;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_defrag_notify (struct rpc_clnt *rpc, void *mydata,
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

        defrag = volinfo->defrag;
        if (!defrag)
                return 0;

        if ((event == RPC_CLNT_DISCONNECT) && defrag->connected)
                volinfo->defrag = NULL;

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

                if (!glusterd_is_service_running (pidfile, NULL)) {
                        if (volinfo->defrag_status ==
                                                     GF_DEFRAG_STATUS_STARTED) {
                                volinfo->defrag_status =
                                                        GF_DEFRAG_STATUS_FAILED;
                        } else {
                                volinfo->defrag_cmd = 0;
                        }
                 }

                glusterd_store_volinfo (volinfo,
                                        GLUSTERD_VOLINFO_VER_AC_INCREMENT);

                if (defrag->rpc) {
                        rpc_clnt_unref (defrag->rpc);
                        defrag->rpc = NULL;
                }
                if (defrag->cbk_fn)
                        defrag->cbk_fn (volinfo, volinfo->defrag_status);

                if (defrag)
                        GF_FREE (defrag);
                gf_log ("", GF_LOG_DEBUG, "%s got RPC_CLNT_DISCONNECT",
                        rpc->conn.trans->name);
                break;
        }
        default:
                gf_log ("", GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        return ret;
}

int
glusterd_handle_defrag_start (glusterd_volinfo_t *volinfo, char *op_errstr,
                              size_t len, int cmd, defrag_cbk_fn_t cbk)
{
        int                    ret = -1;
        glusterd_defrag_info_t *defrag =  NULL;
        runner_t               runner = {0,};
        glusterd_conf_t        *priv = NULL;
        char                   defrag_path[PATH_MAX];
        struct stat            buf = {0,};
        char                   sockfile[PATH_MAX] = {0,};
        char                   pidfile[PATH_MAX] = {0,};
        char                   logfile[PATH_MAX] = {0,};
        dict_t                 *options = NULL;
#ifdef DEBUG
        char                   valgrind_logfile[PATH_MAX] = {0,};
#endif
        priv    = THIS->private;

        GF_ASSERT (volinfo);
        GF_ASSERT (op_errstr);

        ret = glusterd_defrag_start_validate (volinfo, op_errstr, len);
        if (ret)
                goto out;
        if (!volinfo->defrag)
                volinfo->defrag = GF_CALLOC (1, sizeof (glusterd_defrag_info_t),
                                             gf_gld_mt_defrag_info);
        if (!volinfo->defrag)
                goto out;

        defrag = volinfo->defrag;

        defrag->cmd = cmd;

        LOCK_INIT (&defrag->lock);

        volinfo->defrag_status = GF_DEFRAG_STATUS_STARTED;

        volinfo->rebalance_files = 0;
        volinfo->rebalance_data = 0;
        volinfo->lookedup_files = 0;
        volinfo->rebalance_failures = 0;

        volinfo->defrag_cmd = cmd;
        glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);

        GLUSTERD_GET_DEFRAG_DIR (defrag_path, volinfo, priv);
        ret = stat (defrag_path, &buf);
        if (ret && (errno == ENOENT)) {
                runinit (&runner);
                runner_add_args (&runner, "mkdir", "-p", defrag_path, NULL);
                ret = runner_run_reuse (&runner);
                if (ret) {
                        runner_log (&runner, "glusterd", GF_LOG_DEBUG,
                                    "command failed");
                        runner_end (&runner);
                        goto out;
                }
                runner_end (&runner);
        }

        GLUSTERD_GET_DEFRAG_SOCK_FILE (sockfile, volinfo, priv);
        GLUSTERD_GET_DEFRAG_PID_FILE (pidfile, volinfo, priv);
        snprintf (logfile, PATH_MAX, "%s/%s-rebalance.log",
                    DEFAULT_LOG_FILE_DIRECTORY, volinfo->volname);
        runinit (&runner);
#ifdef DEBUG
        if (priv->valgrind) {
                snprintf (valgrind_logfile, PATH_MAX,
                          "%s/valgrind-%s-rebalance.log",
                          DEFAULT_LOG_FILE_DIRECTORY,
                          volinfo->volname);

                runner_add_args (&runner, "valgrind", "--leak-check=full",
                                 "--trace-children=yes", NULL);
                runner_argprintf (&runner, "--log-file=%s", valgrind_logfile);
        }
#endif

        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", "localhost", "--volfile-id", volinfo->volname,
                         "--xlator-option", "*dht.use-readdirp=yes",
                         "--xlator-option", "*dht.lookup-unhashed=yes",
                         "--xlator-option", "*dht.assert-no-child-down=yes",
                         "--xlator-option", "*replicate*.data-self-heal=off",
                         "--xlator-option",
                         "*replicate*.metadata-self-heal=off",
                         "--xlator-option", "*replicate*.entry-self-heal=off",
                         NULL);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf ( &runner, "*dht.rebalance-cmd=%d",cmd);
        runner_add_arg (&runner, "--xlator-option");
        runner_argprintf (&runner, "*dht.node-uuid=%s", uuid_utoa(priv->uuid));
        runner_add_arg (&runner, "--socket-file");
        runner_argprintf (&runner, "%s",sockfile);
        runner_add_arg (&runner, "--pid-file");
        runner_argprintf (&runner, "%s",pidfile);
        runner_add_arg (&runner, "-l");
        runner_argprintf (&runner, logfile);
        if (volinfo->memory_accounting)
                runner_add_arg (&runner, "--mem-accounting");

        ret = runner_run_reuse (&runner);
        if (ret) {
                runner_log (&runner, "glusterd", GF_LOG_DEBUG, "command failed");
                runner_end (&runner);
                goto out;
        }

        sleep (5);
        ret = rpc_clnt_transport_unix_options_build (&options, sockfile);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Unix options build failed");
                goto out;
        }

        ret = glusterd_rpc_create (&defrag->rpc, options,
                                   glusterd_defrag_notify, volinfo);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "RPC create failed");
                goto out;
        }

        if (cbk)
                defrag->cbk_fn = cbk;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int
glusterd_rebalance_rpc_create (glusterd_volinfo_t *volinfo,
                               glusterd_conf_t *priv, int cmd)
{
        dict_t                  *options = NULL;
        char                     sockfile[PATH_MAX] = {0,};
        int                      ret = -1;
        glusterd_defrag_info_t  *defrag =  NULL;

        if (!volinfo->defrag)
                volinfo->defrag = GF_CALLOC (1, sizeof (glusterd_defrag_info_t),
                                             gf_gld_mt_defrag_info);
        if (!volinfo->defrag)
                goto out;

        defrag = volinfo->defrag;

        defrag->cmd = cmd;

        LOCK_INIT (&defrag->lock);

        GLUSTERD_GET_DEFRAG_SOCK_FILE (sockfile, volinfo, priv);
        ret = rpc_clnt_transport_unix_options_build (&options, sockfile);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Unix options build failed");
                goto out;
        }

        ret = glusterd_rpc_create (&defrag->rpc, options,
                                   glusterd_defrag_notify, volinfo);
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
glusterd_handle_defrag_volume (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf_cli_req              cli_req = {{0,}};
        glusterd_conf_t        *priv    = NULL;
        dict_t                 *dict    = NULL;
        char                   *volname = NULL;
        gf_cli_defrag_type      cmd     = 0;

        GF_ASSERT (req);

        priv = THIS->private;

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
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
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to get volname");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", (int32_t*)&cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to get command");
                goto out;
        }

        glusterd_rebalance_cmd_attempted_log (cmd, volname);

        ret = dict_set_static_bin (dict, "node-uuid", priv->uuid, 16);
        if (ret)
                goto out;

        if ((cmd == GF_DEFRAG_CMD_STATUS) ||
              (cmd == GF_DEFRAG_CMD_STOP)) {
                ret = glusterd_op_begin (req, GD_OP_DEFRAG_BRICK_VOLUME,
                                                  dict);
        } else
                ret = glusterd_op_begin (req, GD_OP_REBALANCE, dict);

out:

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (GD_OP_REBALANCE, ret, 0, req,
                                                     NULL, "operation failed");
        }

        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val);//malloced by xdr

        return 0;
}


int
glusterd_op_stage_rebalance (dict_t *dict, char **op_errstr)
{
        char *volname = NULL;
        int ret = 0;
        int32_t cmd = 0;
        char msg[2048] = {0};
        glusterd_volinfo_t  *volinfo = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "volname not found");
                goto out;
        }
        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "cmd not found");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "failed to validate");
                goto out;
        }
        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_FORCE:
                ret = glusterd_defrag_start_validate (volinfo,
                                                      msg, sizeof (msg));
                if (ret) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
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
        char               *volname   = NULL;
        int                 ret       = 0;
        int32_t             cmd       = 0;
        char                msg[2048] = {0};
        glusterd_volinfo_t *volinfo   = NULL;
        glusterd_conf_t    *priv      = NULL;
        glusterd_brickinfo_t *brickinfo = NULL;
        glusterd_brickinfo_t *tmp      = NULL;
        gf_boolean_t        volfile_update = _gf_false;

        priv = THIS->private;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "volname not given");
                goto out;
        }

        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "command not given");
                goto out;
        }

        ret = glusterd_rebalance_cmd_validate (cmd, volname, &volinfo,
                                               msg, sizeof (msg));
        if (ret) {
                gf_log (THIS->name, GF_LOG_DEBUG, "cmd validate failed");
                goto out;
        }

        switch (cmd) {
        case GF_DEFRAG_CMD_START:
        case GF_DEFRAG_CMD_START_LAYOUT_FIX:
        case GF_DEFRAG_CMD_START_FORCE:
                ret = glusterd_handle_defrag_start (volinfo, msg, sizeof (msg),
                                                    cmd, NULL);
                 break;
        case GF_DEFRAG_CMD_STOP:
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
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "failed to create volfiles");
                        goto out;
                }

                ret = glusterd_store_volinfo (volinfo,
                                             GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_WARNING,
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

        glusterd_rebalance_cmd_log (cmd, volname, ret);

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
