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
#include "glusterfs.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-svc-helper.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "glusterd-mgmt.h"
#include "run.h"
#include "syscall.h"

#include <signal.h>

int
glusterd_mgmt_v3_initiate_replace_brick_cmd_phases (rpcsvc_request_t *req,
                                                    glusterd_op_t op,
                                                    dict_t *dict);
int
__glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        char                            *src_brick = NULL;
        char                            *dst_brick = NULL;
        char                            *cli_op = NULL;
        glusterd_op_t                   op = -1;
        char                            *volname = NULL;
        char                            msg[2048] = {0,};
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);
        conf = this->private;

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REQ_DECODE_FAIL, "Failed to decode "
                        "request received from cli");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                GD_MSG_REPLACE_BRK_REQ_RCVD,
                "Received replace brick req");

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
                snprintf (msg, sizeof (msg), "Could not get volume name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }

        ret = dict_get_str (dict, "operation", &cli_op);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "dict_get on operation failed");
                snprintf (msg, sizeof (msg), "Could not get operation");
                goto out;
        }

        op = gd_cli_to_gd_op (cli_op);

        if (conf->op_version < GD_OP_VERSION_3_9_0 &&
            strcmp (cli_op, "GF_REPLACE_OP_COMMIT_FORCE")) {
                snprintf (msg, sizeof (msg), "Cannot execute command. The "
                          "cluster is operating at version %d. reset-brick "
                          "command %s is unavailable in this version.",
                          conf->op_version,
                          gd_rb_op_to_str (cli_op));
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "src-brick", &src_brick);

        if (ret) {
                snprintf (msg, sizeof (msg), "Failed to get src brick");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "%s", msg);
                goto out;
        }
        gf_msg_debug (this->name, 0,
                "src brick=%s", src_brick);

        if (!strcmp (cli_op, "GF_RESET_OP_COMMIT") ||
            !strcmp (cli_op, "GF_RESET_OP_COMMIT_FORCE") ||
            !strcmp (cli_op,  "GF_REPLACE_OP_COMMIT_FORCE")) {
                ret = dict_get_str (dict, "dst-brick", &dst_brick);

                if (ret) {
                        snprintf (msg, sizeof (msg), "Failed to get"
                                  "dest brick");
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", msg);
                        goto out;
                }

                gf_msg_debug (this->name, 0, "dst brick=%s", dst_brick);
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                (op == GD_OP_REPLACE_BRICK) ?
                 GD_MSG_REPLACE_BRK_COMMIT_FORCE_REQ_RCVD :
                 GD_MSG_RESET_BRICK_COMMIT_FORCE_REQ_RCVD,
                "Received %s request.",
                 gd_rb_op_to_str (cli_op));

        ret = glusterd_mgmt_v3_initiate_replace_brick_cmd_phases (req,
                                                                  op, dict);

out:
        if (ret) {
                glusterd_op_send_cli_response (op, ret, 0, req,
                                               dict, msg);
        }
        ret = 0;
        free (cli_req.dict.dict_val);//malloced by xdr

        return ret;
}

int
glusterd_handle_reset_brick (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_replace_brick);
}

int
glusterd_handle_replace_brick (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req,
                                            __glusterd_handle_replace_brick);
}

int
glusterd_op_stage_replace_brick (dict_t *dict, char **op_errstr,
                                 dict_t *rsp_dict)
{
        int                                      ret                = 0;
        char                                    *src_brick          = NULL;
        char                                    *dst_brick          = NULL;
        char                                    *volname            = NULL;
        char                                    *op                 = NULL;
        glusterd_op_t                            gd_op              = -1;
        glusterd_volinfo_t                      *volinfo            = NULL;
        glusterd_brickinfo_t                    *src_brickinfo      = NULL;
        char                                    *host               = NULL;
        char                                     msg[2048]          = {0};
        glusterd_peerinfo_t                     *peerinfo           = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo      = NULL;
        glusterd_conf_t                         *priv               = NULL;
        char                                     pidfile[PATH_MAX]  = {0};
        xlator_t                                *this               = NULL;
        gf_boolean_t                             is_force           = _gf_false;
        char                                    *dup_dstbrick       = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_brick_op_prerequisites (dict, &op, &gd_op,
                                               &volname, &volinfo,
                                               &src_brick, &src_brickinfo,
                                               pidfile,
                                               op_errstr, rsp_dict);
        if (ret)
                goto out;

        if (strcmp (op, "GF_REPLACE_OP_COMMIT_FORCE")) {
                ret = -1;
                goto out;
        } else {
                is_force = _gf_true;
	}

	ret = glusterd_get_dst_brick_info (&dst_brick, volname,
                                           op_errstr,
                                           &dst_brickinfo, &host,
                                           dict, &dup_dstbrick);
        if (ret)
                goto out;

        ret = glusterd_new_brick_validate (dst_brick, dst_brickinfo,
                                           msg, sizeof (msg), op);
        /* fail if brick being replaced with itself */
	if (ret) {
                *op_errstr = gf_strdup (msg);
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_VALIDATE_FAIL, "%s", *op_errstr);
                goto out;
	}

        volinfo->rep_brick.src_brick = src_brickinfo;
        volinfo->rep_brick.dst_brick = dst_brickinfo;

        if (glusterd_rb_check_bricks (volinfo, src_brickinfo, dst_brickinfo)) {

                ret = -1;
                *op_errstr = gf_strdup ("Incorrect source or "
                                        "destination brick");
                if (*op_errstr)
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_BRICK_NOT_FOUND, "%s", *op_errstr);
                goto out;
       }

        if (gf_is_local_addr (host)) {
                ret = glusterd_validate_and_create_brickpath (dst_brickinfo,
                                                  volinfo->volume_id,
                                                  op_errstr, is_force);
                if (ret)
                        goto out;
        }

        if (!gf_is_local_addr (host)) {
                rcu_read_lock ();

                peerinfo = glusterd_peerinfo_find (NULL, host);
                if (peerinfo == NULL) {
                        ret = -1;
                        snprintf (msg, sizeof (msg), "%s, is not a friend",
                                  host);
                        *op_errstr = gf_strdup (msg);

                } else if (!peerinfo->connected) {
                        snprintf (msg, sizeof (msg), "%s, is not connected at "
                                  "the moment", host);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;

                } else if (GD_FRIEND_STATE_BEFRIENDED !=
                                peerinfo->state.state) {
                        snprintf (msg, sizeof (msg), "%s, is not befriended "
                                  "at the moment", host);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                }
                rcu_read_unlock ();

                if (ret)
                        goto out;

        } else if (priv->op_version >= GD_OP_VERSION_3_6_0) {
                /* A bricks mount dir is required only by snapshots which were
                 * introduced in gluster-3.6.0
                 */
                ret = glusterd_get_brick_mount_dir (dst_brickinfo->path,
                                                    dst_brickinfo->hostname,
                                                    dst_brickinfo->mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                "Failed to get brick mount_dir");
                        goto out;
                }

                ret = dict_set_dynstr_with_alloc (rsp_dict, "brick1.mount_dir",
                                                  dst_brickinfo->mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set brick1.mount_dir");
                        goto out;
                }

                ret = dict_set_int32 (rsp_dict, "brick_count", 1);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Failed to set local_brick_count");
                        goto out;
                }
        }

        ret = 0;

out:
        GF_FREE (dup_dstbrick);
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}


int
glusterd_op_perform_replace_brick (glusterd_volinfo_t  *volinfo,
                                   char *old_brick, char *new_brick,
                                   dict_t *dict)
{
        char                                    *brick_mount_dir = NULL;
        glusterd_brickinfo_t                    *old_brickinfo = NULL;
        glusterd_brickinfo_t                    *new_brickinfo = NULL;
        int32_t                                 ret = -1;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *conf = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (volinfo);

        conf = this->private;
        GF_ASSERT (conf);

        ret = glusterd_brickinfo_new_from_brick (new_brick, &new_brickinfo,
                                                 _gf_true, NULL);
        if (ret)
                goto out;

        ret = glusterd_resolve_brick (new_brickinfo);
        if (ret)
                goto out;

        ret = glusterd_volume_brickinfo_get_by_brick (old_brick,
                                                      volinfo, &old_brickinfo,
                                                      _gf_false);
        if (ret)
                goto out;

        strncpy (new_brickinfo->brick_id, old_brickinfo->brick_id,
                 sizeof (new_brickinfo->brick_id));
        new_brickinfo->port = old_brickinfo->port;

        /* A bricks mount dir is required only by snapshots which were
         * introduced in gluster-3.6.0
         */
        if (conf->op_version >= GD_OP_VERSION_3_6_0) {
                ret = dict_get_str (dict, "brick1.mount_dir", &brick_mount_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_BRICK_MOUNTDIR_GET_FAIL,
                                "brick1.mount_dir not present");
                        goto out;
                }
                strncpy (new_brickinfo->mount_dir, brick_mount_dir,
                         sizeof(new_brickinfo->mount_dir));
        }

        cds_list_add (&new_brickinfo->brick_list,
                      &old_brickinfo->brick_list);

        volinfo->brick_count++;

        ret = glusterd_op_perform_remove_brick (volinfo, old_brick, 1, NULL);
        if (ret)
                goto out;

        /* if the volume is a replicate volume, do: */
        if (glusterd_is_volume_replicate (volinfo)) {
                if (!gf_uuid_compare (new_brickinfo->uuid, MY_UUID)) {
                        ret = glusterd_handle_replicate_brick_ops (volinfo,
                                        new_brickinfo, GD_OP_REPLACE_BRICK);
                        if (ret < 0)
                                goto out;
                }
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_brick_start (volinfo, new_brickinfo, _gf_false);
                if (ret)
                        goto out;
        }

out:

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_replace_brick (dict_t *dict, dict_t *rsp_dict)
{
        int                                      ret           = 0;
        char                                    *replace_op    = NULL;
        glusterd_volinfo_t                      *volinfo       = NULL;
        char                                    *volname       = NULL;
        xlator_t                                *this          = NULL;
        glusterd_conf_t                         *priv          = NULL;
        char                                    *src_brick     = NULL;
        char                                    *dst_brick     = NULL;
        glusterd_brickinfo_t                    *src_brickinfo = NULL;
        glusterd_brickinfo_t                    *dst_brickinfo = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get src brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "src brick=%s", src_brick);

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get dst brick");
                goto out;
        }

        gf_msg_debug (this->name, 0, "dst brick=%s", dst_brick);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = dict_get_str (dict, "operation", &replace_op);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "dict_get on operation failed");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        GD_MSG_NO_MEMORY, "Unable to allocate memory");
                goto out;
        }

        ret = glusterd_volume_brickinfo_get_by_brick (src_brick, volinfo,
                                                      &src_brickinfo,
                                                      _gf_false);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Unable to get src-brickinfo");
                goto out;
        }


        ret = glusterd_get_rb_dst_brickinfo (volinfo, &dst_brickinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RB_BRICKINFO_GET_FAIL, "Unable to get "
                         "replace brick destination brickinfo");
                goto out;
        }

        ret = glusterd_resolve_brick (dst_brickinfo);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Unable to resolve dst-brickinfo");
                goto out;
        }

        ret = rb_update_dstbrick_port (dst_brickinfo, rsp_dict,
                                       dict);
        if (ret)
                goto out;

        if (strcmp (replace_op, "GF_REPLACE_OP_COMMIT_FORCE")) {
                ret = -1;
                goto out;
        }

        ret = glusterd_svcs_stop (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTER_SERVICES_STOP_FAIL,
                        "Unable to stop gluster services, ret: %d", ret);
        }

        ret = glusterd_op_perform_replace_brick (volinfo, src_brick,
                                                 dst_brick, dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_BRICK_ADD_FAIL, "Unable to add dst-brick: "
                        "%s to volume: %s", dst_brick, volinfo->volname);
                (void) glusterd_svcs_manager (volinfo);
                goto out;
        }

        volinfo->rebal.defrag_status = 0;

        ret = glusterd_svcs_manager (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        GD_MSG_GLUSTER_SERVICE_START_FAIL,
                        "Failed to start one or more gluster services.");
        }


        ret = glusterd_fetchspec_notify (THIS);
        glusterd_brickinfo_delete (volinfo->rep_brick.dst_brick);
        volinfo->rep_brick.src_brick = NULL;
        volinfo->rep_brick.dst_brick = NULL;

        if (!ret)
                ret = glusterd_store_volinfo (volinfo,
                                              GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RBOP_STATE_STORE_FAIL, "Couldn't store"
                        " replace brick operation's state");

out:
        return ret;
}

int
glusterd_mgmt_v3_initiate_replace_brick_cmd_phases (rpcsvc_request_t *req,
                                                    glusterd_op_t op,
                                                    dict_t *dict)
{
        int32_t                     ret              = -1;
        int32_t                     op_ret           = -1;
        uint32_t                    txn_generation   = 0;
        uint32_t                    op_errno         = 0;
        char                        *op_errstr       = NULL;
        dict_t                      *req_dict        = NULL;
        dict_t                      *tmp_dict        = NULL;
        uuid_t                      *originator_uuid = NULL;
        xlator_t                    *this            = NULL;
        glusterd_conf_t             *conf            = NULL;
        gf_boolean_t                is_acquired      = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        conf = this->private;
        GF_ASSERT (conf);

        txn_generation = conf->generation;
        originator_uuid = GF_CALLOC (1, sizeof(uuid_t),
                                     gf_common_mt_uuid_t);
        if (!originator_uuid) {
                ret = -1;
                goto out;
        }

        gf_uuid_copy (*originator_uuid, MY_UUID);
        ret = dict_set_bin (dict, "originator_uuid",
                            originator_uuid, sizeof (uuid_t));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set originator_uuid.");
                GF_FREE (originator_uuid);
                goto out;
        }

        ret = dict_set_int32 (dict, "is_synctasked", _gf_true);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set synctasked flag to true.");
                goto out;
        }

        tmp_dict = dict_new();
        if (!tmp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL, "Unable to create dict");
                goto out;
        }
        dict_copy (dict, tmp_dict);

        ret = glusterd_mgmt_v3_initiate_lockdown (op, dict, &op_errstr,
                                                  &op_errno, &is_acquired,
                                                  txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_LOCKDOWN_FAIL,
                        "mgmt_v3 lockdown failed.");
                goto out;
        }

        ret = glusterd_mgmt_v3_build_payload (&req_dict, &op_errstr, dict, op);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_PAYLOAD_BUILD_FAIL, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                goto out;
        }

        ret = glusterd_mgmt_v3_pre_validate (op, req_dict, &op_errstr,
                                             &op_errno, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL, "Pre Validation Failed");
                goto out;
        }

        ret = glusterd_mgmt_v3_commit (op, dict, req_dict, &op_errstr,
                                       &op_errno, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_OP_FAIL, "Commit Op Failed");
                goto out;
        }

        ret = 0;

out:
        op_ret = ret;

        (void) glusterd_mgmt_v3_release_peer_locks (op, dict, op_ret,
                                                    &op_errstr, is_acquired,
                                                    txn_generation);

        if (is_acquired) {
                ret = glusterd_multiple_mgmt_v3_unlock (tmp_dict, MY_UUID);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release mgmt_v3 locks on "
                                "localhost.");
                        op_ret = ret;
                }
        }
        /* SEND CLI RESPONSE */
        glusterd_op_send_cli_response (op, op_ret, op_errno, req,
                                       dict, op_errstr);

        if (req_dict)
                dict_unref (req_dict);

        if (tmp_dict)
                dict_unref (tmp_dict);

        if (op_errstr) {
                GF_FREE (op_errstr);
                op_errstr = NULL;
        }

        return 0;
}
