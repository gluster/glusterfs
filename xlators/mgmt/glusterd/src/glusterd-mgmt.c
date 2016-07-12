/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
/* rpc related syncops */
#include "rpc-clnt.h"
#include "protocol-common.h"
#include "xdr-generic.h"
#include "glusterd1-xdr.h"
#include "glusterd-syncop.h"

#include "glusterd.h"
#include "glusterd-utils.h"
#include "glusterd-locks.h"
#include "glusterd-mgmt.h"
#include "glusterd-op-sm.h"
#include "glusterd-volgen.h"
#include "glusterd-store.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-messages.h"
#include "glusterd-errno.h"
#include "glusterd-hooks.h"

extern struct rpc_clnt_program gd_mgmt_v3_prog;


void
gd_mgmt_v3_collate_errors (struct syncargs *args, int op_ret, int op_errno,
                           char *op_errstr, int op_code, uuid_t peerid,
                           u_char *uuid)
{
        char      *peer_str          = NULL;
        char       err_str[PATH_MAX] = "Please check log file for details.";
        char       op_err[PATH_MAX]  = "";
        xlator_t  *this              = NULL;
        int        is_operrstr_blk   = 0;
        char       *err_string       = NULL;
        glusterd_peerinfo_t *peerinfo = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (args);
        GF_ASSERT (uuid);

        if (op_ret) {
                args->op_ret = op_ret;
                args->op_errno = op_errno;

                rcu_read_lock ();
                peerinfo = glusterd_peerinfo_find (peerid, NULL);
                if (peerinfo)
                        peer_str = gf_strdup (peerinfo->hostname);
                else
                        peer_str = gf_strdup (uuid_utoa (uuid));

                rcu_read_unlock ();

                is_operrstr_blk = (op_errstr && strcmp (op_errstr, ""));
                err_string     = (is_operrstr_blk) ? op_errstr : err_str;

                switch (op_code) {
                case GLUSTERD_MGMT_V3_LOCK:
                        {
                                snprintf (op_err, sizeof(op_err),
                                          "Locking failed on %s. %s",
                                          peer_str, err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_PRE_VALIDATE:
                        {
                                snprintf (op_err, sizeof(op_err),
                                          "Pre Validation failed on %s. %s",
                                          peer_str, err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_BRICK_OP:
                        {
                                snprintf (op_err, sizeof(op_err),
                                          "Brick ops failed on %s. %s",
                                          peer_str, err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_COMMIT:
                        {
                                snprintf (op_err, sizeof(op_err),
                                          "Commit failed on %s. %s",
                                          peer_str, err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_POST_VALIDATE:
                        {
                                snprintf (op_err, sizeof(op_err),
                                          "Post Validation failed on %s. %s",
                                          peer_str, err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_UNLOCK:
                        {
                                snprintf (op_err, sizeof(op_err),
                                          "Unlocking failed on %s. %s",
                                          peer_str, err_string);
                                break;
                        }
                default :
                        snprintf (op_err, sizeof(op_err),
                                  "Unknown error! on %s. %s",
                                  peer_str, err_string);
                }

                if (args->errstr) {
                        snprintf (err_str, sizeof(err_str),
                                  "%s\n%s", args->errstr, op_err);
                        GF_FREE (args->errstr);
                        args->errstr = NULL;
                } else
                        snprintf (err_str, sizeof(err_str), "%s", op_err);

                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_OP_FAIL, "%s", op_err);
                args->errstr = gf_strdup (err_str);
        }

        GF_FREE (peer_str);

        return;
}

int32_t
gd_mgmt_v3_pre_validate_fn (glusterd_op_t op, dict_t *dict,
                            char **op_errstr, dict_t *rsp_dict,
                            uint32_t *op_errno)
{
        int32_t       ret = -1;
        xlator_t     *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        switch (op) {
        case GD_OP_SNAP:
                ret = glusterd_snapshot_prevalidate (dict, op_errstr,
                                                     rsp_dict, op_errno);

                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Snapshot Prevalidate Failed");
                        goto out;
                }

                break;

        case GD_OP_REPLACE_BRICK:
                ret = glusterd_op_stage_replace_brick (dict, op_errstr,
                                                       rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Replace-brick prevalidation failed.");
                        goto out;
                }
                break;
        case GD_OP_ADD_BRICK:
                ret = glusterd_op_stage_add_brick (dict, op_errstr, rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "ADD-brick prevalidation failed.");
                        goto out;
                }
                break;
        case GD_OP_START_VOLUME:
                ret = glusterd_op_stage_start_volume (dict, op_errstr,
                                                      rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Volume start prevalidation failed.");
                        goto out;
                }
                break;
        case GD_OP_TIER_START_STOP:
        case GD_OP_TIER_STATUS:
        case GD_OP_DETACH_TIER_STATUS:
        case GD_OP_REMOVE_TIER_BRICK:
                ret = glusterd_op_stage_tier (dict, op_errstr, rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_COMMAND_NOT_FOUND, "tier "
                                "prevalidation failed");
                        goto out;
                }
                break;

        case GD_OP_RESET_BRICK:
               ret = glusterd_reset_brick_prevalidate (dict, op_errstr,
                                                       rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Reset brick prevalidation failed.");
                        goto out;
                }
                break;

        case GD_OP_MAX_OPVERSION:
                ret = 0;
                break;

        default:
                break;
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_brick_op_fn (glusterd_op_t op, dict_t *dict,
                        char **op_errstr, dict_t *rsp_dict)
{
        int32_t       ret = -1;
        xlator_t     *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);

        switch (op) {
        case GD_OP_SNAP:
        {
                ret = glusterd_snapshot_brickop (dict, op_errstr, rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_BRICK_OP_FAIL,
                                "snapshot brickop failed");
                        goto out;
                }
                break;
        }
        default:
                break;
        }

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_commit_fn (glusterd_op_t op, dict_t *dict,
                      char **op_errstr, uint32_t *op_errno,
                      dict_t *rsp_dict)
{
        int32_t       ret = -1;
        xlator_t     *this = NULL;
        int32_t       cmd  = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);
        GF_ASSERT (rsp_dict);

        glusterd_op_commit_hook (op, dict, GD_COMMIT_HOOK_PRE);
        switch (op) {
               case GD_OP_SNAP:
               {
                       ret = glusterd_snapshot (dict, op_errstr,
                                                op_errno, rsp_dict);
                       if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                               GD_MSG_COMMIT_OP_FAIL,
                                               "Snapshot Commit Failed");
                                goto out;
                       }
                       break;
               }
                case GD_OP_REPLACE_BRICK:
                {
                        ret = glusterd_op_replace_brick (dict, rsp_dict);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "Replace-brick commit failed.");
                                goto out;
                        }
                        break;
                }
                case GD_OP_ADD_BRICK:
                {
                        ret = glusterd_op_add_brick (dict, op_errstr);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "Add-brick commit failed.");
                                goto out;
                        }
                        break;

                }
                case GD_OP_START_VOLUME:
                {
                        ret = glusterd_op_start_volume (dict, op_errstr);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "Volume start commit failed.");
                                goto out;
                        }
                        break;

                }
                case GD_OP_RESET_BRICK:
                {
                        ret = glusterd_op_reset_brick (dict, rsp_dict);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "Reset-brick commit failed.");
                                goto out;
                        }
                        break;
                }
                case GD_OP_MAX_OPVERSION:
                {
                        ret = glusterd_op_get_max_opversion (op_errstr,
                                                             rsp_dict);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "Commit failed.");
                                goto out;
                        }
                        break;
                }
                case GD_OP_TIER_START_STOP:
                {
                        ret = glusterd_op_tier_start_stop (dict, op_errstr,
                                                           rsp_dict);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "tier commit failed.");
                                goto out;
                        }
                        break;
                }
                case GD_OP_REMOVE_TIER_BRICK:
                {
                        ret = glusterd_op_remove_tier_brick (dict, op_errstr,
                                                       rsp_dict);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "tier detach commit failed.");
                                goto out;
                        }
                        ret = dict_get_int32 (dict, "rebalance-command", &cmd);
                        if (ret) {
                                gf_msg_debug (this->name, 0, "cmd not found");
                                goto out;
                        }

                        if (cmd != GF_DEFRAG_CMD_DETACH_STOP)
                                break;
                }
                case GD_OP_DETACH_TIER_STATUS:
                case GD_OP_TIER_STATUS:
                {
                        ret = glusterd_op_tier_status (dict, op_errstr,
                                                       rsp_dict, op);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_COMMIT_OP_FAIL,
                                        "tier status commit failed");
                                goto out;
                        }
                }

               default:
                       break;
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_post_validate_fn (glusterd_op_t op, int32_t op_ret, dict_t *dict,
                             char **op_errstr, dict_t *rsp_dict)
{
        int32_t                  ret       = -1;
        xlator_t                *this      = NULL;
        char                    *volname   = NULL;
        glusterd_volinfo_t      *volinfo   = NULL;
        glusterd_svc_t          *svc       = NULL;


        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);

        if (op_ret == 0)
            glusterd_op_commit_hook (op, dict, GD_COMMIT_HOOK_POST);

        switch (op) {
               case GD_OP_SNAP:
               {
                       ret = glusterd_snapshot_postvalidate (dict, op_ret,
                                                             op_errstr,
                                                             rsp_dict);
                       if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                               GD_MSG_POST_VALIDATION_FAIL,
                                               "postvalidate operation failed");
                                goto out;
                       }
                       break;
               }
               case GD_OP_ADD_BRICK:
               {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED, "Unable to get"
                                        " volume name");
                                goto out;
                        }

                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                                        GD_MSG_VOL_NOT_FOUND, "Unable to "
                                        "allocate memory");
                                goto out;
                        }
                        ret = glusterd_create_volfiles_and_notify_services (
                                                                     volinfo);
                        if (ret)
                                goto out;
                        ret = glusterd_store_volinfo (volinfo,
                                            GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                        if (ret)
                                goto out;
                        break;

               }
               case GD_OP_START_VOLUME:
               {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED, "Unable to get"
                                        " volume name");
                                goto out;
                        }

                        ret = glusterd_volinfo_find (volname, &volinfo);
                        if (ret) {
                                gf_msg ("glusterd", GF_LOG_ERROR, EINVAL,
                                        GD_MSG_VOL_NOT_FOUND, "Unable to "
                                        "allocate memory");
                                goto out;
                        }

                        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
                                svc = &(volinfo->tierd.svc);
                                ret = svc->manager (svc, volinfo,
                                                    PROC_START_NO_WAIT);
                                if (ret)
                                        goto out;
                        }
                        break;
               }

               default:
                        break;
        }

        ret = 0;

out:
        gf_msg_trace (this->name, 0, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_lock_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        int32_t                     ret           = -1;
        struct syncargs            *args          = NULL;
        gd1_mgmt_v3_lock_rsp        rsp           = {{0},};
        call_frame_t               *frame         = NULL;
        int32_t                     op_ret        = -1;
        int32_t                     op_errno      = -1;
        xlator_t                   *this          = NULL;
        uuid_t                     *peerid        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        /* Even though the lock command has failed, while collating the errors
           (gd_mgmt_v3_collate_errors), args->op_ret and args->op_errno will be
           used. @args is obtained from frame->local. So before checking the
           status of the request and going out if its a failure, args should be
           set to frame->local. Otherwise, while collating args will be NULL.
           This applies to other phases such as prevalidate, brickop, commit and
           postvalidate also.
        */
        frame  = myframe;
        args   = frame->local;
        peerid = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, iov, out, op_errno,
                                        EINVAL);

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_lock_rsp);
        if (ret < 0)
                goto out;

        gf_uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, NULL,
                                   GLUSTERD_MGMT_V3_LOCK, *peerid, rsp.uuid);
        GF_FREE (peerid);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
         * the caller function.
         */
        if (req->rpc_status != -1)
                STACK_DESTROY (frame->root);
        synctask_barrier_wake(args);
        return 0;
}

int32_t
gd_mgmt_v3_lock_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        gd_mgmt_v3_lock_cbk_fn);
}

int
gd_mgmt_v3_lock (glusterd_op_t op, dict_t *op_ctx,
                 glusterd_peerinfo_t *peerinfo,
                 struct syncargs *args, uuid_t my_uuid,
                 uuid_t recv_uuid)
{
        gd1_mgmt_v3_lock_req     req  = {{0},};
        int32_t                  ret  = -1;
        xlator_t                *this = NULL;
        uuid_t                  *peerid = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_ctx);
        GF_ASSERT (peerinfo);
        GF_ASSERT (args);

        ret = dict_allocate_and_serialize (op_ctx,
                                           &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, my_uuid);
        req.op = op;

        GD_ALLOC_COPY_UUID (peerid, peerinfo->uuid, ret);
        if (ret)
                goto out;

        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerid,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_LOCK,
                                        gd_mgmt_v3_lock_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_lock_req);
out:
        GF_FREE (req.dict.dict_val);
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_initiate_lockdown (glusterd_op_t op, dict_t *dict,
                                    char **op_errstr, uint32_t *op_errno,
                                    gf_boolean_t  *is_acquired,
                                    uint32_t txn_generation)
{
        char                *volname    = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        struct syncargs      args       = {0};
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_conf_t     *conf       = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (is_acquired);

        /* Trying to acquire multiple mgmt_v3 locks on local node */
        ret = glusterd_multiple_mgmt_v3_lock (dict, MY_UUID, op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_LOCK_GET_FAIL,
                        "Failed to acquire mgmt_v3 locks on localhost");
                goto out;
        }

        *is_acquired = _gf_true;

        /* Sending mgmt_v3 lock req to other nodes in the cluster */
        gd_syncargs_init (&args, NULL);
        synctask_barrier_init((&args));
        peer_cnt = 0;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Only send requests to peers who were available before the
                 * transaction started
                 */
                if (peerinfo->generation > txn_generation)
                        continue;

                if (!peerinfo->connected)
                        continue;
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                gd_mgmt_v3_lock (op, dict, peerinfo, &args,
                                 MY_UUID, peer_uuid);
                peer_cnt++;
        }
        rcu_read_unlock ();

        if (0 == peer_cnt) {
                ret = 0;
                goto out;
        }

        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.errstr)
                *op_errstr = gf_strdup (args.errstr);

        ret = args.op_ret;
        *op_errno = args.op_errno;

        gf_msg_debug (this->name, 0, "Sent lock op req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        if (ret) {
                if (*op_errstr)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_LOCK_GET_FAIL, "%s",
                                *op_errstr);

                if (volname)
                        ret = gf_asprintf (op_errstr,
                                           "Another transaction is in progress "
                                           "for %s. Please try again after "
                                           "sometime.", volname);
                else
                        ret = gf_asprintf (op_errstr,
                                           "Another transaction is in progress "
                                           "Please try again after sometime.");

                if (ret == -1)
                        *op_errstr = NULL;

                ret = -1;
        }

        return ret;
}

int
glusterd_pre_validate_aggr_rsp_dict (glusterd_op_t op,
                                     dict_t *aggr, dict_t *rsp)
{
        int32_t              ret      = 0;
        xlator_t            *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (aggr);
        GF_ASSERT (rsp);

        switch (op) {
        case GD_OP_SNAP:
                ret = glusterd_snap_pre_validate_use_rsp_dict (aggr, rsp);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Failed to aggregate prevalidate "
                                "response dictionaries.");
                        goto out;
                }
                break;
        case GD_OP_REPLACE_BRICK:
                ret = glusterd_rb_use_rsp_dict (aggr, rsp);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Failed to aggregate prevalidate "
                                "response dictionaries.");
                        goto out;
                }
                break;
        case GD_OP_START_VOLUME:
        case GD_OP_ADD_BRICK:
                ret = glusterd_aggr_brick_mount_dirs (aggr, rsp);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_BRICK_MOUNDIRS_AGGR_FAIL, "Failed to "
                                "aggregate brick mount dirs");
                        goto out;
                }
                break;
        case GD_OP_RESET_BRICK:
                ret = glusterd_rb_use_rsp_dict (aggr, rsp);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PRE_VALIDATION_FAIL,
                                "Failed to aggregate prevalidate "
                                "response dictionaries.");
                        goto out;
                }
        case GD_OP_TIER_STATUS:
        case GD_OP_DETACH_TIER_STATUS:
        case GD_OP_TIER_START_STOP:
        case GD_OP_REMOVE_TIER_BRICK:
                break;
        case GD_OP_MAX_OPVERSION:
                break;
        default:
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "Invalid op (%s)",
                        gd_op_list[op]);

                break;
        }
out:
        return ret;
}

int32_t
gd_mgmt_v3_pre_validate_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        int32_t                     ret           = -1;
        struct syncargs            *args          = NULL;
        gd1_mgmt_v3_pre_val_rsp     rsp           = {{0},};
        call_frame_t               *frame         = NULL;
        int32_t                     op_ret        = -1;
        int32_t                     op_errno      = -1;
        dict_t                     *rsp_dict      = NULL;
        xlator_t                   *this          = NULL;
        uuid_t                     *peerid        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerid = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, iov, out, op_errno,
                                        EINVAL);

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_pre_val_rsp);
        if (ret < 0)
                goto out;

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                rsp_dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &rsp_dict);
                if (ret < 0) {
                        free (rsp.dict.dict_val);
                        goto out;
                } else {
                        rsp_dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

        gf_uuid_copy (args->uuid, rsp.uuid);
        pthread_mutex_lock (&args->lock_dict);
        {
                ret = glusterd_pre_validate_aggr_rsp_dict (rsp.op, args->dict,
                                                           rsp_dict);
        }
        pthread_mutex_unlock (&args->lock_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RESP_AGGR_FAIL, "%s",
                        "Failed to aggregate response from "
                        " node/brick");
                if (!rsp.op_ret)
                        op_ret = ret;
                else {
                        op_ret = rsp.op_ret;
                        op_errno = rsp.op_errno;
                }
        } else {
                op_ret = rsp.op_ret;
                op_errno = rsp.op_errno;
        }

out:
        if (rsp_dict)
                dict_unref (rsp_dict);

        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, rsp.op_errstr,
                                  GLUSTERD_MGMT_V3_PRE_VALIDATE,
                                  *peerid, rsp.uuid);

        if (rsp.op_errstr)
                free (rsp.op_errstr);
        GF_FREE (peerid);
        /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
         * the caller function.
         */
        if (req->rpc_status != -1)
                STACK_DESTROY (frame->root);
        synctask_barrier_wake(args);
        return 0;
}

int32_t
gd_mgmt_v3_pre_validate_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        gd_mgmt_v3_pre_validate_cbk_fn);
}

int
gd_mgmt_v3_pre_validate_req (glusterd_op_t op, dict_t *op_ctx,
                             glusterd_peerinfo_t *peerinfo,
                             struct syncargs *args, uuid_t my_uuid,
                             uuid_t recv_uuid)
{
        int32_t                  ret   = -1;
        gd1_mgmt_v3_pre_val_req  req   = {{0},};
        xlator_t                *this  = NULL;
        uuid_t                  *peerid = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_ctx);
        GF_ASSERT (peerinfo);
        GF_ASSERT (args);

        ret = dict_allocate_and_serialize (op_ctx,
                                           &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, my_uuid);
        req.op = op;

        GD_ALLOC_COPY_UUID (peerid, peerinfo->uuid, ret);
        if (ret)
                goto out;

        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerid,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_PRE_VALIDATE,
                                        gd_mgmt_v3_pre_validate_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_pre_val_req);
out:
        GF_FREE (req.dict.dict_val);
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_pre_validate (glusterd_op_t op, dict_t *req_dict,
                               char **op_errstr, uint32_t *op_errno,
                               uint32_t txn_generation)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_conf_t     *conf       = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (req_dict);
        GF_ASSERT (op_errstr);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Pre Validation on local node */
        ret = gd_mgmt_v3_pre_validate_fn (op, req_dict, op_errstr,
                                          rsp_dict, op_errno);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL,
                        "Pre Validation failed for "
                        "operation %s on local node",
                        gd_op_list[op]);

                if (*op_errstr == NULL) {
                        ret = gf_asprintf (op_errstr,
                                           "Pre-validation failed "
                                           "on localhost. Please "
                                           "check log file for details");
                        if (ret == -1)
                                *op_errstr = NULL;

                        ret = -1;
                }
                goto out;
        }

        if (op != GD_OP_MAX_OPVERSION) {
                ret = glusterd_pre_validate_aggr_rsp_dict (op, req_dict,
                                                           rsp_dict);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PRE_VALIDATION_FAIL, "%s",
                                "Failed to aggregate response from "
                                " node/brick");
                        goto out;
                }

                dict_unref (rsp_dict);
                rsp_dict = NULL;
        }

        /* Sending Pre Validation req to other nodes in the cluster */
        gd_syncargs_init (&args, req_dict);
        synctask_barrier_init((&args));
        peer_cnt = 0;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Only send requests to peers who were available before the
                 * transaction started
                 */
                if (peerinfo->generation > txn_generation)
                        continue;

                if (!peerinfo->connected)
                        continue;
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                gd_mgmt_v3_pre_validate_req (op, req_dict, peerinfo, &args,
                                             MY_UUID, peer_uuid);
                peer_cnt++;
        }
        rcu_read_unlock ();

        if (0 == peer_cnt) {
                ret = 0;
                goto out;
        }

        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL,
                        "Pre Validation failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;
        *op_errno = args.op_errno;

        gf_msg_debug (this->name, 0, "Sent pre valaidation req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        return ret;
}

int
glusterd_mgmt_v3_build_payload (dict_t **req, char **op_errstr, dict_t *dict,
                                glusterd_op_t op)
{
        int32_t                 ret      = -1;
        dict_t                 *req_dict = NULL;
        xlator_t               *this     = NULL;
        char                   *volname  = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (op_errstr);
        GF_ASSERT (dict);

        req_dict = dict_new ();
        if (!req_dict)
                goto out;

        switch (op) {
        case GD_OP_MAX_OPVERSION:
        case GD_OP_SNAP:
                dict_copy (dict, req_dict);
                break;
        case GD_OP_START_VOLUME:
        case GD_OP_ADD_BRICK:
        case GD_OP_REPLACE_BRICK:
        case GD_OP_RESET_BRICK:
                {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                                        GD_MSG_DICT_GET_FAILED,
                                        "volname is not present in "
                                        "operation ctx");
                                goto out;
                        }

                        if (strcasecmp (volname, "all")) {
                                ret = glusterd_dict_set_volid (dict,
                                                               volname,
                                                             op_errstr);
                                if (ret)
                                        goto out;
                        }
                        dict_copy (dict, req_dict);
                }
                        break;
        case GD_OP_TIER_START_STOP:
        case GD_OP_REMOVE_TIER_BRICK:
        case GD_OP_DETACH_TIER_STATUS:
        case GD_OP_TIER_STATUS:
                        dict_copy (dict, req_dict);
                        break;
        default:
                        break;
        }

        *req = req_dict;
        ret = 0;
out:
        return ret;
}

int32_t
gd_mgmt_v3_brick_op_cbk_fn (struct rpc_req *req, struct iovec *iov,
                            int count, void *myframe)
{
        int32_t                     ret           = -1;
        struct syncargs            *args          = NULL;
        gd1_mgmt_v3_brick_op_rsp     rsp          = {{0},};
        call_frame_t               *frame         = NULL;
        int32_t                     op_ret        = -1;
        int32_t                     op_errno      = -1;
        xlator_t                   *this          = NULL;
        uuid_t                     *peerid        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerid = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        /* If the operation failed, then iov can be NULL. So better check the
           status of the operation and then worry about iov (if the status of
           the command is success)
        */
        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, iov, out, op_errno,
                                        EINVAL);

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_brick_op_rsp);
        if (ret < 0)
                goto out;

        gf_uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, rsp.op_errstr,
                                   GLUSTERD_MGMT_V3_BRICK_OP, *peerid,
                                   rsp.uuid);

        if (rsp.op_errstr)
                free (rsp.op_errstr);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        GF_FREE (peerid);
        /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
         * the caller function.
         */
        if (req->rpc_status != -1)
                STACK_DESTROY (frame->root);
        synctask_barrier_wake(args);
        return 0;
}

int32_t
gd_mgmt_v3_brick_op_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        gd_mgmt_v3_brick_op_cbk_fn);
}

int
gd_mgmt_v3_brick_op_req (glusterd_op_t op, dict_t *op_ctx,
                         glusterd_peerinfo_t *peerinfo,
                         struct syncargs *args, uuid_t my_uuid,
                         uuid_t recv_uuid)
{
        int32_t                   ret  = -1;
        gd1_mgmt_v3_brick_op_req  req  = {{0},};
        xlator_t                 *this = NULL;
        uuid_t                   *peerid = {0,};

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_ctx);
        GF_ASSERT (peerinfo);
        GF_ASSERT (args);

        ret = dict_allocate_and_serialize (op_ctx,
                                           &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, my_uuid);
        req.op = op;

        GD_ALLOC_COPY_UUID (peerid, peerinfo->uuid, ret);
        if (ret)
                goto out;

        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerid,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_BRICK_OP,
                                        gd_mgmt_v3_brick_op_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_brick_op_req);
out:
        GF_FREE (req.dict.dict_val);
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_brick_op (glusterd_op_t op, dict_t *req_dict, char **op_errstr,
                           uint32_t txn_generation)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_conf_t     *conf       = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (req_dict);
        GF_ASSERT (op_errstr);

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Perform brick op on local node */
        ret = gd_mgmt_v3_brick_op_fn (op, req_dict, op_errstr,
                                     rsp_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_OP_FAIL,
                        "Brick ops failed for "
                        "operation %s on local node",
                        gd_op_list[op]);

                if (*op_errstr == NULL) {
                        ret = gf_asprintf (op_errstr,
                                           "Brick ops failed "
                                           "on localhost. Please "
                                           "check log file for details");
                        if (ret == -1)
                                *op_errstr = NULL;

                        ret = -1;
                }
                goto out;
        }

        dict_unref (rsp_dict);
        rsp_dict = NULL;

        /* Sending brick op req to other nodes in the cluster */
        gd_syncargs_init (&args, NULL);
        synctask_barrier_init((&args));
        peer_cnt = 0;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Only send requests to peers who were available before the
                 * transaction started
                 */
                if (peerinfo->generation > txn_generation)
                        continue;

                if (!peerinfo->connected)
                        continue;
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                gd_mgmt_v3_brick_op_req (op, req_dict, peerinfo, &args,
                                         MY_UUID, peer_uuid);
                peer_cnt++;
        }
        rcu_read_unlock ();

        if (0 == peer_cnt) {
                ret = 0;
                goto out;
        }

        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_OP_FAIL,
                        "Brick ops failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_msg_debug (this->name, 0, "Sent brick op req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        return ret;
}

int32_t
gd_mgmt_v3_commit_cbk_fn (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        int32_t                     ret           = -1;
        struct syncargs            *args          = NULL;
        gd1_mgmt_v3_commit_rsp       rsp          = {{0},};
        call_frame_t               *frame         = NULL;
        int32_t                     op_ret        = -1;
        int32_t                     op_errno      = -1;
        dict_t                     *rsp_dict      = NULL;
        xlator_t                   *this          = NULL;
        uuid_t                     *peerid        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerid = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, iov, out, op_errno,
                                        EINVAL);

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_commit_rsp);
        if (ret < 0)
                goto out;

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                rsp_dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &rsp_dict);
                if (ret < 0) {
                        free (rsp.dict.dict_val);
                        goto out;
                } else {
                        rsp_dict->extra_stdfree = rsp.dict.dict_val;
                }
        }

        gf_uuid_copy (args->uuid, rsp.uuid);
        pthread_mutex_lock (&args->lock_dict);
        {
                ret = glusterd_syncop_aggr_rsp_dict (rsp.op, args->dict,
                                                     rsp_dict);
        }
        pthread_mutex_unlock (&args->lock_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RESP_AGGR_FAIL, "%s",
                        "Failed to aggregate response from "
                        " node/brick");
                if (!rsp.op_ret)
                        op_ret = ret;
                else {
                        op_ret = rsp.op_ret;
                        op_errno = rsp.op_errno;
                }
        } else {
                op_ret = rsp.op_ret;
                op_errno = rsp.op_errno;
        }

out:
        if (rsp_dict)
                dict_unref (rsp_dict);

        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, rsp.op_errstr,
                                  GLUSTERD_MGMT_V3_COMMIT, *peerid, rsp.uuid);
        GF_FREE (peerid);

        if (rsp.op_errstr)
                free (rsp.op_errstr);

        /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
         * the caller function.
         */
        if (req->rpc_status != -1)
                STACK_DESTROY (frame->root);
        synctask_barrier_wake(args);
        return 0;
}

int32_t
gd_mgmt_v3_commit_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        gd_mgmt_v3_commit_cbk_fn);
}

int
gd_mgmt_v3_commit_req (glusterd_op_t op, dict_t *op_ctx,
                       glusterd_peerinfo_t *peerinfo,
                       struct syncargs *args, uuid_t my_uuid,
                       uuid_t recv_uuid)
{
        int32_t                  ret  = -1;
        gd1_mgmt_v3_commit_req   req  = {{0},};
        xlator_t                *this = NULL;
        uuid_t                  *peerid = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_ctx);
        GF_ASSERT (peerinfo);
        GF_ASSERT (args);

        ret = dict_allocate_and_serialize (op_ctx,
                                           &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, my_uuid);
        req.op = op;

        GD_ALLOC_COPY_UUID (peerid, peerinfo->uuid, ret);
        if (ret)
                goto out;

        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerid,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_COMMIT,
                                        gd_mgmt_v3_commit_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_commit_req);
out:
        GF_FREE (req.dict.dict_val);
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_commit (glusterd_op_t op, dict_t *op_ctx, dict_t *req_dict,
                         char **op_errstr, uint32_t *op_errno,
                         uint32_t txn_generation)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_conf_t     *conf       = NULL;
        int32_t              count      = 0;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (op_ctx);
        GF_ASSERT (req_dict);
        GF_ASSERT (op_errstr);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Commit on local node */
        ret = gd_mgmt_v3_commit_fn (op, req_dict, op_errstr,
                                    op_errno, rsp_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_OP_FAIL,
                        "Commit failed for "
                        "operation %s on local node",
                        gd_op_list[op]);

                if (*op_errstr == NULL) {
                        ret = gf_asprintf (op_errstr,
                                           "Commit failed "
                                           "on localhost. Please "
                                           "check log file for details.");
                        if (ret == -1)
                                *op_errstr = NULL;

                        ret = -1;
                }
                goto out;
        }

        ret = glusterd_syncop_aggr_rsp_dict (op, op_ctx,
                                             rsp_dict);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_RESP_AGGR_FAIL, "%s",
                        "Failed to aggregate response from "
                        " node/brick");
                goto out;
        }


        dict_unref (rsp_dict);
        rsp_dict = NULL;

        /* Sending commit req to other nodes in the cluster */
        gd_syncargs_init (&args, op_ctx);
        synctask_barrier_init((&args));
        peer_cnt = 0;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Only send requests to peers who were available before the
                 * transaction started
                 */
                if (peerinfo->generation > txn_generation)
                        continue;

                if (!peerinfo->connected) {
                        if (op == GD_OP_TIER_STATUS || op ==
                                        GD_OP_DETACH_TIER_STATUS) {
                                ret = dict_get_int32 (args.dict, "count",
                                                      &count);
                                if (ret)
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_GET_FAILED,
                                                "failed to get index");
                                count++;
                                ret = dict_set_int32 (args.dict, "count",
                                                      count);
                                if (ret)
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_DICT_GET_FAILED,
                                                "failed to set index");
                        }
                        continue;
                }
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                gd_mgmt_v3_commit_req (op, req_dict, peerinfo, &args,
                                       MY_UUID, peer_uuid);
                peer_cnt++;
        }
        rcu_read_unlock ();

        if (0 == peer_cnt) {
                ret = 0;
                goto out;
        }

        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_OP_FAIL,
                        "Commit failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;
        *op_errno = args.op_errno;

        gf_msg_debug (this->name, 0, "Sent commit req for %s to %d "
                "peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        glusterd_op_modify_op_ctx (op, op_ctx);
        return ret;
}

int32_t
gd_mgmt_v3_post_validate_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        int32_t                     ret           = -1;
        struct syncargs            *args          = NULL;
        gd1_mgmt_v3_post_val_rsp    rsp           = {{0},};
        call_frame_t               *frame         = NULL;
        int32_t                     op_ret        = -1;
        int32_t                     op_errno      = -1;
        xlator_t                   *this          = NULL;
        uuid_t                     *peerid        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerid = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, iov, out, op_errno,
                                        EINVAL);

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_post_val_rsp);
        if (ret < 0)
                goto out;

        gf_uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, rsp.op_errstr,
                                  GLUSTERD_MGMT_V3_POST_VALIDATE, *peerid,
                                  rsp.uuid);
        if (rsp.op_errstr)
                free (rsp.op_errstr);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        GF_FREE (peerid);
        /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
         * the caller function.
         */
        if (req->rpc_status != -1)
                STACK_DESTROY (frame->root);
        synctask_barrier_wake(args);
        return 0;
}

int32_t
gd_mgmt_v3_post_validate_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        gd_mgmt_v3_post_validate_cbk_fn);
}

int
gd_mgmt_v3_post_validate_req (glusterd_op_t op, int32_t op_ret, dict_t *op_ctx,
                              glusterd_peerinfo_t *peerinfo,
                              struct syncargs *args, uuid_t my_uuid,
                              uuid_t recv_uuid)
{
        int32_t                   ret  = -1;
        gd1_mgmt_v3_post_val_req  req  = {{0},};
        xlator_t                 *this = NULL;
        uuid_t                   *peerid = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_ctx);
        GF_ASSERT (peerinfo);
        GF_ASSERT (args);

        ret = dict_allocate_and_serialize (op_ctx,
                                           &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, my_uuid);
        req.op = op;
        req.op_ret = op_ret;

        GD_ALLOC_COPY_UUID (peerid, peerinfo->uuid, ret);
        if (ret)
                goto out;

        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerid,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_POST_VALIDATE,
                                        gd_mgmt_v3_post_validate_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_post_val_req);
out:
        GF_FREE (req.dict.dict_val);
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_post_validate (glusterd_op_t op, int32_t op_ret, dict_t *dict,
                                dict_t *req_dict, char **op_errstr,
                                uint32_t txn_generation)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_conf_t     *conf       = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (dict);
        GF_VALIDATE_OR_GOTO (this->name, req_dict, out);
        GF_ASSERT (op_errstr);

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Copy the contents of dict like missed snaps info to req_dict */
        if (op != GD_OP_REMOVE_TIER_BRICK)
                /* dict and req_dict has the same values during remove tier
                 * brick (detach start) So this rewrite make the remove brick
                 * id to become empty.
                 * Avoiding to copy it retains the value. */
                dict_copy (dict, req_dict);

        /* Post Validation on local node */
        ret = gd_mgmt_v3_post_validate_fn (op, op_ret, req_dict, op_errstr,
                                           rsp_dict);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_POST_VALIDATION_FAIL,
                        "Post Validation failed for "
                        "operation %s on local node",
                        gd_op_list[op]);

                if (*op_errstr == NULL) {
                        ret = gf_asprintf (op_errstr,
                                           "Post-validation failed "
                                           "on localhost. Please check "
                                           "log file for details");
                        if (ret == -1)
                                *op_errstr = NULL;

                        ret = -1;
                }
                goto out;
        }

        dict_unref (rsp_dict);
        rsp_dict = NULL;

        /* Sending Post Validation req to other nodes in the cluster */
        gd_syncargs_init (&args, req_dict);
        synctask_barrier_init((&args));
        peer_cnt = 0;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Only send requests to peers who were available before the
                 * transaction started
                 */
                if (peerinfo->generation > txn_generation)
                        continue;

                if (!peerinfo->connected)
                        continue;
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                gd_mgmt_v3_post_validate_req (op, op_ret, req_dict, peerinfo,
                                              &args, MY_UUID, peer_uuid);
                peer_cnt++;
        }
        rcu_read_unlock ();

        if (0 == peer_cnt) {
                ret = 0;
                goto out;
        }

        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_POST_VALIDATION_FAIL,
                        "Post Validation failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_msg_debug (this->name, 0, "Sent post valaidation req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        return ret;
}

int32_t
gd_mgmt_v3_unlock_cbk_fn (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        int32_t                     ret           = -1;
        struct syncargs            *args          = NULL;
        gd1_mgmt_v3_unlock_rsp      rsp           = {{0},};
        call_frame_t               *frame         = NULL;
        int32_t                     op_ret        = -1;
        int32_t                     op_errno      = -1;
        xlator_t                   *this          = NULL;
        uuid_t                     *peerid        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerid = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, iov, out, op_errno,
                                        EINVAL);

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_unlock_rsp);
        if (ret < 0)
                goto out;

        gf_uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, NULL,
                                  GLUSTERD_MGMT_V3_UNLOCK, *peerid, rsp.uuid);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        GF_FREE (peerid);
        /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
         * the caller function.
         */
        if (req->rpc_status != -1)
                STACK_DESTROY (frame->root);
        synctask_barrier_wake(args);
        return 0;
}

int32_t
gd_mgmt_v3_unlock_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        return glusterd_big_locked_cbk (req, iov, count, myframe,
                                        gd_mgmt_v3_unlock_cbk_fn);
}

int
gd_mgmt_v3_unlock (glusterd_op_t op, dict_t *op_ctx,
                   glusterd_peerinfo_t *peerinfo,
                   struct syncargs *args, uuid_t my_uuid,
                   uuid_t recv_uuid)
{
        int32_t                  ret  = -1;
        gd1_mgmt_v3_unlock_req   req  = {{0},};
        xlator_t                *this = NULL;
        uuid_t                  *peerid = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (op_ctx);
        GF_ASSERT (peerinfo);
        GF_ASSERT (args);

        ret = dict_allocate_and_serialize (op_ctx,
                                           &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret)
                goto out;

        gf_uuid_copy (req.uuid, my_uuid);
        req.op = op;

        GD_ALLOC_COPY_UUID (peerid, peerinfo->uuid, ret);
        if (ret)
                goto out;

        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerid,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_UNLOCK,
                                        gd_mgmt_v3_unlock_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_unlock_req);
out:
        GF_FREE (req.dict.dict_val);
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_release_peer_locks (glusterd_op_t op, dict_t *dict,
                                     int32_t op_ret, char **op_errstr,
                                     gf_boolean_t  is_acquired,
                                     uint32_t txn_generation)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        glusterd_conf_t     *conf       = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        /* If the lock has not been held during this
         * transaction, do not send unlock requests */
        if (!is_acquired)
                goto out;

        /* Sending mgmt_v3 unlock req to other nodes in the cluster */
        gd_syncargs_init (&args, NULL);
        synctask_barrier_init((&args));
        peer_cnt = 0;

        rcu_read_lock ();
        cds_list_for_each_entry_rcu (peerinfo, &conf->peers, uuid_list) {
                /* Only send requests to peers who were available before the
                 * transaction started
                 */
                if (peerinfo->generation > txn_generation)
                        continue;

                if (!peerinfo->connected)
                        continue;
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state.state != GD_FRIEND_STATE_BEFRIENDED)
                        continue;

                gd_mgmt_v3_unlock (op, dict, peerinfo, &args,
                                   MY_UUID, peer_uuid);
                peer_cnt++;
        }
        rcu_read_unlock ();

        if (0 == peer_cnt) {
                ret = 0;
                goto out;
        }

        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_UNLOCK_FAIL,
                        "Unlock failed on peers");

                if (!op_ret && args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_msg_debug (this->name, 0, "Sent unlock op req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);

out:
        return ret;
}

int32_t
glusterd_mgmt_v3_initiate_all_phases (rpcsvc_request_t *req, glusterd_op_t op,
                                      dict_t *dict)
{
        int32_t                     ret              = -1;
        int32_t                     op_ret           = -1;
        dict_t                      *req_dict        = NULL;
        dict_t                      *tmp_dict        = NULL;
        glusterd_conf_t             *conf            = NULL;
        char                        *op_errstr       = NULL;
        xlator_t                    *this            = NULL;
        gf_boolean_t                is_acquired      = _gf_false;
        uuid_t                      *originator_uuid = NULL;
        uint32_t                    txn_generation   = 0;
        uint32_t                    op_errno         = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        conf = this->private;
        GF_ASSERT (conf);

        /* Save the peer list generation */
        txn_generation = conf->generation;
        cmm_smp_rmb ();
        /* This read memory barrier makes sure that this assignment happens here
         * only and is not reordered and optimized by either the compiler or the
         * processor.
         */

        /* Save the MY_UUID as the originator_uuid. This originator_uuid
         * will be used by is_origin_glusterd() to determine if a node
         * is the originator node for a command. */
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

        /* Marking the operation as complete synctasked */
        ret = dict_set_int32 (dict, "is_synctasked", _gf_true);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set synctasked flag.");
                goto out;
        }

        /* Use a copy at local unlock as cli response will be sent before
         * the unlock and the volname in the dict might be removed */
        tmp_dict = dict_new();
        if (!tmp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL, "Unable to create dict");
                goto out;
        }
        dict_copy (dict, tmp_dict);

        /* LOCKDOWN PHASE - Acquire mgmt_v3 locks */
        ret = glusterd_mgmt_v3_initiate_lockdown (op, dict, &op_errstr,
                                                  &op_errno, &is_acquired,
                                                  txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_LOCKDOWN_FAIL,
                        "mgmt_v3 lockdown failed.");
                goto out;
        }

        /* BUILD PAYLOAD */
        ret = glusterd_mgmt_v3_build_payload (&req_dict, &op_errstr, dict, op);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_PAYLOAD_BUILD_FAIL, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                goto out;
        }

        /* PRE-COMMIT VALIDATE PHASE */
        ret = glusterd_mgmt_v3_pre_validate (op, req_dict, &op_errstr,
                                             &op_errno, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL, "Pre Validation Failed");
                goto out;
        }

        /* COMMIT OP PHASE */
        ret = glusterd_mgmt_v3_commit (op, dict, req_dict, &op_errstr,
                                       &op_errno, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_OP_FAIL, "Commit Op Failed");
                goto out;
        }

        /* POST-COMMIT VALIDATE PHASE */
        /* As of now, post_validate is not trying to cleanup any failed
           commands. So as of now, I am sending 0 (op_ret as 0).
        */
        ret = glusterd_mgmt_v3_post_validate (op, 0, dict, req_dict, &op_errstr,
                                              txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_POST_VALIDATION_FAIL, "Post Validation Failed");
                goto out;
        }

        ret = 0;
out:
        op_ret = ret;
        /* UNLOCK PHASE FOR PEERS*/
        (void) glusterd_mgmt_v3_release_peer_locks (op, dict, op_ret,
                                                    &op_errstr, is_acquired,
                                                    txn_generation);

        /* LOCAL VOLUME(S) UNLOCK */
        if (is_acquired) {
                /* Trying to release multiple mgmt_v3 locks */
                ret = glusterd_multiple_mgmt_v3_unlock (tmp_dict, MY_UUID);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release mgmt_v3 locks on localhost");
                        op_ret = ret;
                }
        }

        if (op_ret && (op_errno == 0))
                op_errno = EG_INTRNL;

        if (op != GD_OP_MAX_OPVERSION) {
                /* SEND CLI RESPONSE */
                glusterd_op_send_cli_response (op, op_ret, op_errno, req,
                                               dict, op_errstr);
        }

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

int32_t
glusterd_set_barrier_value (dict_t *dict, char *option)
{
        int32_t                 ret             = -1;
        xlator_t                *this           = NULL;
        glusterd_volinfo_t     *vol             = NULL;
        char                    *volname        = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (dict);
        GF_ASSERT (option);

        /* TODO : Change this when we support multiple volume.
         * As of now only snapshot of single volume is supported,
         * Hence volname1 is directly fetched
         */
        ret = dict_get_str (dict, "volname1", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Volname not present in "
                        "dict");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_FOUND, "Volume %s not found ",
                        volname);
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (dict, "barrier", option);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set barrier op "
                        "in request dictionary");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (vol->dict, "features.barrier",
                                          option);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set barrier op "
                        "in volume option dict");
                goto out;
        }

        gd_update_volume_op_versions (vol);

        ret = glusterd_create_volfiles (vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL,
                        "Failed to create volfiles");
                goto out;
        }

        ret = glusterd_store_volinfo (vol, GLUSTERD_VOLINFO_VER_AC_INCREMENT);

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_mgmt_v3_initiate_snap_phases (rpcsvc_request_t *req, glusterd_op_t op,
                                       dict_t *dict)
{
        int32_t                     ret              = -1;
        int32_t                     op_ret           = -1;
        dict_t                      *req_dict        = NULL;
        dict_t                      *tmp_dict        = NULL;
        glusterd_conf_t             *conf            = NULL;
        char                        *op_errstr       = NULL;
        xlator_t                    *this            = NULL;
        gf_boolean_t                is_acquired      = _gf_false;
        uuid_t                      *originator_uuid = NULL;
        gf_boolean_t                success          = _gf_false;
        char                        *cli_errstr      = NULL;
        uint32_t                    txn_generation   = 0;
        uint32_t                    op_errno         = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        conf = this->private;
        GF_ASSERT (conf);

        /* Save the peer list generation */
        txn_generation = conf->generation;
        cmm_smp_rmb ();
        /* This read memory barrier makes sure that this assignment happens here
         * only and is not reordered and optimized by either the compiler or the
         * processor.
         */

        /* Save the MY_UUID as the originator_uuid. This originator_uuid
         * will be used by is_origin_glusterd() to determine if a node
         * is the originator node for a command. */
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

        /* Marking the operation as complete synctasked */
        ret = dict_set_int32 (dict, "is_synctasked", _gf_true);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Failed to set synctasked flag.");
                goto out;
        }

        /* Use a copy at local unlock as cli response will be sent before
         * the unlock and the volname in the dict might be removed */
        tmp_dict = dict_new();
        if (!tmp_dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_CREATE_FAIL, "Unable to create dict");
                goto out;
        }
        dict_copy (dict, tmp_dict);

        /* LOCKDOWN PHASE - Acquire mgmt_v3 locks */
        ret = glusterd_mgmt_v3_initiate_lockdown (op, dict, &op_errstr,
                                                  &op_errno, &is_acquired,
                                                  txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_LOCKDOWN_FAIL,
                        "mgmt_v3 lockdown failed.");
                goto out;
        }

        /* BUILD PAYLOAD */
        ret = glusterd_mgmt_v3_build_payload (&req_dict, &op_errstr, dict, op);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MGMTV3_PAYLOAD_BUILD_FAIL, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                goto out;
        }

        /* PRE-COMMIT VALIDATE PHASE */
        ret = glusterd_mgmt_v3_pre_validate (op, req_dict, &op_errstr,
                                             &op_errno, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL, "Pre Validation Failed");
                goto out;
        }

        /* quorum check of the volume is done here */
        ret = glusterd_snap_quorum_check (req_dict, _gf_false, &op_errstr,
                                          &op_errno);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_QUORUM_CHECK_FAIL, "Volume quorum check failed");
                goto out;
        }

        /* Set the operation type as pre, so that differentiation can be
         * made whether the brickop is sent during pre-commit or post-commit
         */
        ret = dict_set_dynstr_with_alloc (req_dict, "operation-type", "pre");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set "
                        "operation-type in dictionary");
                goto out;
        }

        ret = glusterd_mgmt_v3_brick_op (op, req_dict, &op_errstr,
                                         txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_OP_FAIL, "Brick Ops Failed");
                goto unbarrier;
        }

        /* COMMIT OP PHASE */
        /* TODO: As of now, the plan is to do quorum check before sending the
           commit fop and if the quorum succeeds, then commit is sent to all
           the other glusterds.
           snap create functionality now creates the in memory and on disk
           objects for the snapshot (marking them as incomplete), takes the lvm
           snapshot and then updates the status of the in memory and on disk
           snap objects as complete. Suppose one of the glusterds goes down
           after taking the lvm snapshot, but before updating the snap object,
           then treat it as a snapshot create failure and trigger cleanup.
           i.e the number of commit responses received by the originator
           glusterd shold be the same as the number of peers it has sent the
           request to (i.e npeers variable). If not, then originator glusterd
           will initiate cleanup in post-validate fop.
           Question: What if one of the other glusterds goes down as explained
           above and along with it the originator glusterd also goes down?
           Who will initiate the cleanup?
        */
        ret = dict_set_int32 (req_dict, "cleanup", 1);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "failed to set dict");
                goto unbarrier;
        }

        ret = glusterd_mgmt_v3_commit (op, dict, req_dict, &op_errstr,
                                       &op_errno, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_COMMIT_OP_FAIL,  "Commit Op Failed");
                /* If the main op fails, we should save the error string.
                   Because, op_errstr will be used for unbarrier and
                   unlock ops also. We might lose the actual error that
                   caused the failure.
                */
                cli_errstr = op_errstr;
                op_errstr = NULL;
                goto unbarrier;
        }

        success = _gf_true;
unbarrier:
        /* Set the operation type as post, so that differentiation can be
         * made whether the brickop is sent during pre-commit or post-commit
         */
        ret = dict_set_dynstr_with_alloc (req_dict, "operation-type", "post");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set "
                        "operation-type in dictionary");
                goto out;
        }

        ret = glusterd_mgmt_v3_brick_op (op, req_dict, &op_errstr,
                                         txn_generation);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_BRICK_OP_FAIL, "Brick Ops Failed");
                goto out;
        }

        /*Do a quorum check if the commit phase is successful*/
        if (success) {
                //quorum check of the snapshot volume
                ret = glusterd_snap_quorum_check (dict, _gf_true, &op_errstr,
                                                  &op_errno);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_QUORUM_CHECK_FAIL,
                                "Snapshot Volume quorum check failed");
                        goto out;
                }
        }

        ret = 0;

out:
        op_ret = ret;

        if (success == _gf_false)
                op_ret = -1;

        /* POST-COMMIT VALIDATE PHASE */
        ret = glusterd_mgmt_v3_post_validate (op, op_ret, dict, req_dict,
                                              &op_errstr, txn_generation);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_PRE_VALIDATION_FAIL, "Post Validation Failed");
                op_ret = -1;
        }

        /* UNLOCK PHASE FOR PEERS*/
        (void) glusterd_mgmt_v3_release_peer_locks (op, dict, op_ret,
                                                    &op_errstr, is_acquired,
                                                    txn_generation);

        /* If the commit op (snapshot taking) failed, then the error is stored
           in cli_errstr and unbarrier is called. Suppose, if unbarrier also
           fails, then the error happened in unbarrier is logged and freed.
           The error happened in commit op, which is stored in cli_errstr
           is sent to cli.
        */
        if (cli_errstr) {
                GF_FREE (op_errstr);
                op_errstr = NULL;
                op_errstr = cli_errstr;
        }

        /* LOCAL VOLUME(S) UNLOCK */
        if (is_acquired) {
                /* Trying to release multiple mgmt_v3 locks */
                ret = glusterd_multiple_mgmt_v3_unlock (tmp_dict, MY_UUID);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release mgmt_v3 locks on localhost");
                        op_ret = ret;
                }
        }

        if (op_ret && (op_errno == 0))
                op_errno = EG_INTRNL;

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
