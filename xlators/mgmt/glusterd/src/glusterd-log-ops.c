/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "syscall.h"

#include <signal.h>

int
__glusterd_handle_log_rotate (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gf_cli_req              cli_req = {{0,}};
        dict_t                 *dict    = NULL;
        glusterd_op_t           cli_op = GD_OP_LOG_ROTATE;
        char                   *volname = NULL;
        char                   msg[2048] = {0,};
        xlator_t               *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

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
                                GD_MSG_DICT_UNSERIALIZE_FAIL,
                                "failed to "
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

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_LOG_ROTATE_REQ_RECVD,
                "Received log rotate req "
                "for volume %s", volname);

        ret = dict_set_uint64 (dict, "rotate-key", (uint64_t)time (NULL));
        if (ret)
                goto out;

        ret = glusterd_op_begin_synctask (req, GD_OP_LOG_ROTATE, dict);

out:
        if (ret) {
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, msg);
        }

        free (cli_req.dict.dict_val);
        return ret;
}

int
glusterd_handle_log_rotate (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_log_rotate);
}

/* op-sm */
int
glusterd_op_stage_log_rotate (dict_t *dict, char **op_errstr)
{
        int                                     ret = -1;
        char                                    *volname = NULL;
        glusterd_volinfo_t                      *volinfo = NULL;
        gf_boolean_t                            exists = _gf_false;
        char                                    msg[2048] = {0};
        char                                    *brick = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (!exists) {
                snprintf (msg, sizeof (msg), "Volume %s does not exist",
                          volname);
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        if (_gf_false == glusterd_is_volume_started (volinfo)) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started before"
                          " log rotate.", volname);
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_STARTED, "%s", msg);
                *op_errstr = gf_strdup (msg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        /* If no brick is specified, do log-rotate for
           all the bricks in the volume */
        if (ret) {
                ret = 0;
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (brick, volinfo, NULL,
                                                      _gf_false);
        if (ret) {
                snprintf (msg, sizeof (msg), "Incorrect brick %s "
                          "for volume %s", brick, volname);
                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "%s", msg);
                *op_errstr = gf_strdup (msg);
                goto out;
        }
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

        return ret;
}


int
glusterd_op_log_rotate (dict_t *dict)
{
        int                   ret                = -1;
        glusterd_conf_t      *priv               = NULL;
        glusterd_volinfo_t   *volinfo            = NULL;
        glusterd_brickinfo_t *brickinfo          = NULL;
        xlator_t             *this               = NULL;
        char                 *volname            = NULL;
        char                 *brick              = NULL;
        char                  logfile[PATH_MAX]  = {0,};
        char                  pidfile[PATH_MAX]  = {0,};
        FILE                 *file               = NULL;
        pid_t                 pid                = 0;
        uint64_t              key                = 0;
        int                   valid_brick        = 0;
        glusterd_brickinfo_t *tmpbrkinfo         = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "volname not found");
                goto out;
        }

        ret = dict_get_uint64 (dict, "rotate-key", &key);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "rotate key not found");
                goto out;
        }

        ret = dict_get_str (dict, "brick", &brick);
        /* If no brick is specified, do log-rotate for
           all the bricks in the volume */
        if (ret)
                goto cont;

        ret = glusterd_brickinfo_new_from_brick (brick, &tmpbrkinfo,
                                                 _gf_false, NULL);
        if (ret) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_NOT_FOUND,
                        "cannot get brickinfo from brick");
                goto out;
        }

cont:
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret)
                goto out;

        ret = -1;
        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                if (brick &&
                    (strcmp (tmpbrkinfo->hostname, brickinfo->hostname) ||
                     strcmp (tmpbrkinfo->path,brickinfo->path)))
                        continue;

                valid_brick = 1;

                GLUSTERD_GET_BRICK_PIDFILE (pidfile, volinfo, brickinfo, priv);
                file = fopen (pidfile, "r+");
                if (!file) {
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "Unable to open pidfile: %s",
                                pidfile);
                        ret = -1;
                        goto out;
                }

                ret = fscanf (file, "%d", &pid);
                if (ret <= 0) {
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "Unable to read pidfile: %s",
                                pidfile);
                        ret = -1;
                        goto out;
                }
                fclose (file);
                file = NULL;

                snprintf (logfile, PATH_MAX, "%s.%"PRIu64,
                          brickinfo->logfile, key);

                ret = sys_rename (brickinfo->logfile, logfile);
                if (ret)
                        gf_msg ("glusterd", GF_LOG_WARNING, errno,
                                GD_MSG_FILE_OP_FAILED, "rename failed");

                ret = kill (pid, SIGHUP);
                if (ret) {
                        gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                GD_MSG_PID_KILL_FAIL, "Unable to SIGHUP to %d", pid);
                        goto out;
                }
                ret = 0;

                /* If request was for brick, only one iteration is enough */
                if (brick)
                        break;
        }

        if (ret && !valid_brick)
                ret = 0;

out:
        if (tmpbrkinfo)
                glusterd_brickinfo_delete (tmpbrkinfo);

        return ret;
}
