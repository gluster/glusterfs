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

extern struct rpc_clnt_program gd_mgmt_v3_prog;


void
gd_mgmt_v3_collate_errors (struct syncargs *args, int op_ret, int op_errno,
                           char *op_errstr, int op_code,
                           glusterd_peerinfo_t *peerinfo, u_char *uuid)
{
        char      *peer_str          = NULL;
        char       err_str[PATH_MAX] = "Please check log file for details.";
        char       op_err[PATH_MAX]  = "";
        int32_t    len               = -1;
        xlator_t  *this              = NULL;
        int        is_operrstr_blk   = 0;
        char       *err_string       = NULL;
        char       *cli_err_str      = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (args);
        GF_ASSERT (uuid);

        if (op_ret) {
                args->op_ret = op_ret;
                args->op_errno = op_errno;

                if (peerinfo)
                        peer_str = peerinfo->hostname;
                else
                        peer_str = uuid_utoa (uuid);

                is_operrstr_blk = (op_errstr && strcmp (op_errstr, ""));
                err_string     = (is_operrstr_blk) ? op_errstr : err_str;

                switch (op_code) {
                case GLUSTERD_MGMT_V3_LOCK:
                        {
                                len = snprintf (op_err, sizeof(op_err),
                                                "Locking failed "
                                                "on %s. %s", peer_str,
                                                err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_PRE_VALIDATE:
                        {
                                len = snprintf (op_err, sizeof(op_err),
                                                "Pre Validation failed "
                                                "on %s. %s", peer_str,
                                                err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_BRICK_OP:
                        {
                                len = snprintf (op_err, sizeof(op_err),
                                                "Brick ops failed "
                                                "on %s. %s", peer_str,
                                                err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_COMMIT:
                        {
                                len = snprintf (op_err, sizeof(op_err),
                                                "Commit failed"
                                                " on %s. %s", peer_str,
                                                err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_POST_VALIDATE:
                        {
                                len = snprintf (op_err, sizeof(op_err),
                                                "Post Validation failed "
                                                "on %s. %s", peer_str,
                                                err_string);
                                break;
                        }
                case GLUSTERD_MGMT_V3_UNLOCK:
                        {
                                len = snprintf (op_err, sizeof(op_err),
                                                "Unlocking failed "
                                                "on %s. %s", peer_str,
                                                err_string);
                                break;
                        }
                default :
                        len = snprintf (op_err, sizeof(op_err),
                                       "Unknown error! "
                                       "on %s. %s", peer_str,
                                        err_string);
                }

                cli_err_str = ((is_operrstr_blk) ? op_errstr : op_err);

                if (args->errstr) {
                        len = snprintf (err_str, sizeof(err_str),
                                      "%s\n%s", args->errstr,
                                      cli_err_str);
                        GF_FREE (args->errstr);
                        args->errstr = NULL;
                } else
                        len = snprintf (err_str, sizeof(err_str),
                                "%s", cli_err_str);

                gf_log (this->name, GF_LOG_ERROR, "%s", op_err);
                args->errstr = gf_strdup (err_str);
        }

        return;
}

int32_t
gd_mgmt_v3_pre_validate_fn (glusterd_op_t op, dict_t *dict,
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
                ret = glusterd_snapshot_prevalidate (dict, op_errstr,
                                                     rsp_dict);

                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Snapshot Prevalidate Failed");
                        goto out;
                }

                break;

        default:
                break;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "OP = %d. Returning %d", op, ret);
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
                        gf_log (this->name, GF_LOG_WARNING, "snapshot brickop "
                                "failed");
                        goto out;
                }
                break;
        }
        default:
                break;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_TRACE, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_commit_fn (glusterd_op_t op, dict_t *dict,
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
                       ret = glusterd_snapshot (dict, op_errstr, rsp_dict);
                       if (ret) {
                               gf_log (this->name, GF_LOG_WARNING,
                                       "Snapshot Commit Failed");
                               goto out;
                       }
                       break;
               }
               default:
                       break;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_post_validate_fn (glusterd_op_t op, int32_t op_ret, dict_t *dict,
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
                       ret = glusterd_snapshot_postvalidate (dict, op_ret,
                                                             op_errstr,
                                                             rsp_dict);
                       if (ret) {
                               gf_log (this->name, GF_LOG_WARNING,
                                       "postvalidate operation failed");
                               goto out;
                       }
                       break;
               }
               default:
                       break;
        }

        ret = 0;

out:
        gf_log (this->name, GF_LOG_TRACE, "OP = %d. Returning %d", op, ret);
        return ret;
}

int32_t
gd_mgmt_v3_lock_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        int32_t                     ret        = -1;
        struct syncargs            *args       = NULL;
        glusterd_peerinfo_t        *peerinfo   = NULL;
        gd1_mgmt_v3_lock_rsp        rsp        = {{0},};
        call_frame_t               *frame      = NULL;
        int32_t                     op_ret     = -1;
        int32_t                     op_errno   = -1;
        xlator_t                   *this       = NULL;

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
        peerinfo = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        if (!iov) {
                gf_log (this->name, GF_LOG_ERROR, "iov is NULL");
                op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_lock_rsp);
        if (ret < 0)
                goto out;

        uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, NULL,
                                   GLUSTERD_MGMT_V3_LOCK,
                                   peerinfo, rsp.uuid);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
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
        glusterd_conf_t         *conf = THIS->private;
        int32_t                  ret  = -1;
        xlator_t                *this = NULL;

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

        uuid_copy (req.uuid, my_uuid);
        req.op = op;
        synclock_unlock (&conf->big_lock);
        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerinfo,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_LOCK,
                                        gd_mgmt_v3_lock_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_lock_req);
        synclock_lock (&conf->big_lock);
out:
        GF_FREE (req.dict.dict_val);
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_initiate_lockdown (glusterd_conf_t  *conf, glusterd_op_t op,
                                    dict_t *dict, char **op_errstr, int npeers,
                                    gf_boolean_t  *is_acquired)
{
        char                *volname    = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        struct syncargs      args       = {0};
        struct list_head    *peers      = NULL;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (conf);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (is_acquired);

        peers = &conf->xaction_peers;

        /* Trying to acquire multiple mgmt_v3 locks on local node */
        ret = glusterd_multiple_mgmt_v3_lock (dict, MY_UUID);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to acquire mgmt_v3 locks on localhost");
                goto out;
        }

        *is_acquired = _gf_true;

        if (!npeers) {
                ret = 0;
                goto out;
        }

        /* Sending mgmt_v3 lock req to other nodes in the cluster */
        gd_syncargs_init (&args, NULL);
        synctask_barrier_init((&args));
        peer_cnt = 0;
        list_for_each_entry (peerinfo, peers, op_peers_list) {
                gd_mgmt_v3_lock (op, dict, peerinfo, &args,
                                 MY_UUID, peer_uuid);
                peer_cnt++;
        }
        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.errstr)
                *op_errstr = gf_strdup (args.errstr);

        ret = args.op_ret;

        gf_log (this->name, GF_LOG_DEBUG, "Sent lock op req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        if (ret) {
                if (*op_errstr)
                        gf_log (this->name, GF_LOG_ERROR, "%s",
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to aggregate prevalidate "
                                "response dictionaries.");
                        goto out;
                }
                break;
        default:
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Invalid op (%s)",
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
        int32_t                     ret        = -1;
        struct syncargs            *args       = NULL;
        glusterd_peerinfo_t        *peerinfo   = NULL;
        gd1_mgmt_v3_pre_val_rsp      rsp        = {{0},};
        call_frame_t               *frame      = NULL;
        int32_t                     op_ret     = -1;
        int32_t                     op_errno   = -1;
        dict_t                     *rsp_dict   = NULL;
        xlator_t                   *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerinfo = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        if (!iov) {
                gf_log (this->name, GF_LOG_ERROR, "iov is NULL");
                op_errno = EINVAL;
        }

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

        uuid_copy (args->uuid, rsp.uuid);
        pthread_mutex_lock (&args->lock_dict);
        {
                ret = glusterd_pre_validate_aggr_rsp_dict (rsp.op, args->dict,
                                                           rsp_dict);
        }
        pthread_mutex_unlock (&args->lock_dict);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s",
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
                                  peerinfo, rsp.uuid);

        if (rsp.op_errstr)
                free (rsp.op_errstr);

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
        glusterd_conf_t         *conf  = THIS->private;
        xlator_t                *this  = NULL;

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

        uuid_copy (req.uuid, my_uuid);
        req.op = op;
        synclock_unlock (&conf->big_lock);
        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerinfo,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_PRE_VALIDATE,
                                        gd_mgmt_v3_pre_validate_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_pre_val_req);
        synclock_lock (&conf->big_lock);
out:
        GF_FREE (req.dict.dict_val);
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_pre_validate (glusterd_conf_t  *conf, glusterd_op_t op,
                               dict_t *req_dict, char **op_errstr, int npeers)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        struct list_head    *peers      = NULL;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (conf);
        GF_ASSERT (req_dict);
        GF_ASSERT (op_errstr);

        peers = &conf->xaction_peers;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Pre Validation on local node */
        ret = gd_mgmt_v3_pre_validate_fn (op, req_dict, op_errstr,
                                          rsp_dict);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
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

        ret = glusterd_pre_validate_aggr_rsp_dict (op, req_dict,
                                                   rsp_dict);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s",
                        "Failed to aggregate response from "
                        " node/brick");
                goto out;
        }

        dict_unref (rsp_dict);
        rsp_dict = NULL;

        if (!npeers) {
                ret = 0;
                goto out;
        }

        /* Sending Pre Validation req to other nodes in the cluster */
        gd_syncargs_init (&args, req_dict);
        synctask_barrier_init((&args));
        peer_cnt = 0;
        list_for_each_entry (peerinfo, peers, op_peers_list) {
                gd_mgmt_v3_pre_validate_req (op, req_dict, peerinfo, &args,
                                             MY_UUID, peer_uuid);
                peer_cnt++;
        }
        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Pre Validation failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_log (this->name, GF_LOG_DEBUG, "Sent pre valaidation req for %s "
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

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (op_errstr);
        GF_ASSERT (dict);

        req_dict = dict_new ();
        if (!req_dict)
                goto out;

        switch (op) {
                case GD_OP_SNAP:
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
        int32_t                     ret        = -1;
        struct syncargs            *args       = NULL;
        glusterd_peerinfo_t        *peerinfo   = NULL;
        gd1_mgmt_v3_brick_op_rsp     rsp        = {{0},};
        call_frame_t               *frame      = NULL;
        int32_t                     op_ret     = -1;
        int32_t                     op_errno   = -1;
        xlator_t                   *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerinfo = frame->cookie;
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

        if (!iov) {
                gf_log (this->name, GF_LOG_ERROR, "iov is NULL");
                op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_brick_op_rsp);
        if (ret < 0)
                goto out;

        uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, rsp.op_errstr,
                                   GLUSTERD_MGMT_V3_BRICK_OP,
                                   peerinfo, rsp.uuid);

        if (rsp.op_errstr)
                free (rsp.op_errstr);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);

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
        glusterd_conf_t          *conf = THIS->private;
        xlator_t                 *this = NULL;

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

        uuid_copy (req.uuid, my_uuid);
        req.op = op;
        synclock_unlock (&conf->big_lock);
        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerinfo,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_BRICK_OP,
                                        gd_mgmt_v3_brick_op_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_brick_op_req);
        synclock_lock (&conf->big_lock);
out:
        GF_FREE (req.dict.dict_val);
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_brick_op (glusterd_conf_t  *conf, glusterd_op_t op,
                           dict_t *req_dict, char **op_errstr, int npeers)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        struct list_head    *peers      = NULL;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (conf);
        GF_ASSERT (req_dict);
        GF_ASSERT (op_errstr);

        peers = &conf->xaction_peers;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Perform brick op on local node */
        ret = gd_mgmt_v3_brick_op_fn (op, req_dict, op_errstr,
                                     rsp_dict);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
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

        if (!npeers) {
                ret = 0;
                goto out;
        }

        /* Sending brick op req to other nodes in the cluster */
        gd_syncargs_init (&args, NULL);
        synctask_barrier_init((&args));
        peer_cnt = 0;
        list_for_each_entry (peerinfo, peers, op_peers_list) {
                gd_mgmt_v3_brick_op_req (op, req_dict, peerinfo, &args,
                                         MY_UUID, peer_uuid);
                peer_cnt++;
        }
        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Brick ops failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_log (this->name, GF_LOG_DEBUG, "Sent brick op req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        return ret;
}

int32_t
gd_mgmt_v3_commit_cbk_fn (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        int32_t                     ret        = -1;
        struct syncargs            *args       = NULL;
        glusterd_peerinfo_t        *peerinfo   = NULL;
        gd1_mgmt_v3_commit_rsp       rsp        = {{0},};
        call_frame_t               *frame      = NULL;
        int32_t                     op_ret     = -1;
        int32_t                     op_errno   = -1;
        dict_t                     *rsp_dict   = NULL;
        xlator_t                   *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerinfo = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        if (!iov) {
                gf_log (this->name, GF_LOG_ERROR, "iov is NULL");
                op_errno = EINVAL;
                goto out;
        }

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

        uuid_copy (args->uuid, rsp.uuid);
        pthread_mutex_lock (&args->lock_dict);
        {
                ret = glusterd_syncop_aggr_rsp_dict (rsp.op, args->dict,
                                                     rsp_dict);
        }
        pthread_mutex_unlock (&args->lock_dict);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s",
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
                                  GLUSTERD_MGMT_V3_COMMIT,
                                  peerinfo, rsp.uuid);

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
        glusterd_conf_t         *conf = THIS->private;
        xlator_t                *this = NULL;

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

        uuid_copy (req.uuid, my_uuid);
        req.op = op;
        synclock_unlock (&conf->big_lock);
        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerinfo,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_COMMIT,
                                        gd_mgmt_v3_commit_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_commit_req);
        synclock_lock (&conf->big_lock);
out:
        GF_FREE (req.dict.dict_val);
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_commit (glusterd_conf_t  *conf, glusterd_op_t op,
                         dict_t *op_ctx, dict_t *req_dict,
                         char **op_errstr, int npeers)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        struct list_head    *peers      = NULL;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (conf);
        GF_ASSERT (op_ctx);
        GF_ASSERT (req_dict);
        GF_ASSERT (op_errstr);

        peers = &conf->xaction_peers;

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Commit on local node */
        ret = gd_mgmt_v3_commit_fn (op, req_dict, op_errstr,
                                   rsp_dict);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
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
                gf_log (this->name, GF_LOG_ERROR, "%s",
                        "Failed to aggregate response from "
                        " node/brick");
                goto out;
        }

        dict_unref (rsp_dict);
        rsp_dict = NULL;

        if (!npeers) {
                ret = 0;
                goto out;
        }

        /* Sending commit req to other nodes in the cluster */
        gd_syncargs_init (&args, op_ctx);
        synctask_barrier_init((&args));
        peer_cnt = 0;
        list_for_each_entry (peerinfo, peers, op_peers_list) {
                gd_mgmt_v3_commit_req (op, req_dict, peerinfo, &args,
                                       MY_UUID, peer_uuid);
                peer_cnt++;
        }
        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Commit failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_log (this->name, GF_LOG_DEBUG, "Sent commit req for %s to %d "
                "peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        return ret;
}

int32_t
gd_mgmt_v3_post_validate_cbk_fn (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        int32_t                     ret        = -1;
        struct syncargs            *args       = NULL;
        glusterd_peerinfo_t        *peerinfo   = NULL;
        gd1_mgmt_v3_post_val_rsp     rsp        = {{0},};
        call_frame_t               *frame      = NULL;
        int32_t                     op_ret     = -1;
        int32_t                     op_errno   = -1;
        xlator_t                   *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerinfo = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        if (!iov) {
                gf_log (this->name, GF_LOG_ERROR, "iov is NULL");
                op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_post_val_rsp);
        if (ret < 0)
                goto out;

        uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, rsp.op_errstr,
                                  GLUSTERD_MGMT_V3_POST_VALIDATE,
                                  peerinfo, rsp.uuid);
        if (rsp.op_errstr)
                free (rsp.op_errstr);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
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
        glusterd_conf_t          *conf = THIS->private;
        xlator_t                 *this = NULL;

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

        uuid_copy (req.uuid, my_uuid);
        req.op = op;
        req.op_ret = op_ret;
        synclock_unlock (&conf->big_lock);
        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerinfo,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_POST_VALIDATE,
                                        gd_mgmt_v3_post_validate_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_post_val_req);
        synclock_lock (&conf->big_lock);
out:
        GF_FREE (req.dict.dict_val);
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_post_validate (glusterd_conf_t  *conf, glusterd_op_t op,
                                int32_t op_ret, dict_t *dict, dict_t *req_dict,
                                char **op_errstr, int npeers)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        dict_t              *rsp_dict   = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        struct list_head    *peers      = NULL;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (conf);
        GF_ASSERT (dict);
        GF_VALIDATE_OR_GOTO (this->name, req_dict, out);
        GF_ASSERT (op_errstr);

        peers = &conf->xaction_peers;
        GF_ASSERT (peers);

        rsp_dict = dict_new ();
        if (!rsp_dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create response dictionary");
                goto out;
        }

        /* Copy the contents of dict like missed snaps info to req_dict */
        dict_copy (dict, req_dict);

        /* Post Validation on local node */
        ret = gd_mgmt_v3_post_validate_fn (op, op_ret, req_dict, op_errstr,
                                           rsp_dict);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
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

        if (!npeers) {
                ret = 0;
                goto out;
        }

        /* Sending Post Validation req to other nodes in the cluster */
        gd_syncargs_init (&args, req_dict);
        synctask_barrier_init((&args));
        peer_cnt = 0;
        list_for_each_entry (peerinfo, peers, op_peers_list) {
                gd_mgmt_v3_post_validate_req (op, op_ret, req_dict, peerinfo,
                                              &args, MY_UUID, peer_uuid);
                peer_cnt++;
        }
        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Post Validation failed on peers");

                if (args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_log (this->name, GF_LOG_DEBUG, "Sent post valaidation req for %s "
                "to %d peers. Returning %d", gd_op_list[op], peer_cnt, ret);
out:
        return ret;
}

int32_t
gd_mgmt_v3_unlock_cbk_fn (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        int32_t                     ret        = -1;
        struct syncargs            *args       = NULL;
        glusterd_peerinfo_t        *peerinfo   = NULL;
        gd1_mgmt_v3_unlock_rsp      rsp        = {{0},};
        call_frame_t               *frame      = NULL;
        int32_t                     op_ret     = -1;
        int32_t                     op_errno   = -1;
        xlator_t                   *this       = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (myframe);

        frame  = myframe;
        args   = frame->local;
        peerinfo = frame->cookie;
        frame->local = NULL;
        frame->cookie = NULL;

        if (-1 == req->rpc_status) {
                op_errno = ENOTCONN;
                goto out;
        }

        if (!iov) {
                gf_log (this->name, GF_LOG_ERROR, "iov is NULL");
                op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gd1_mgmt_v3_unlock_rsp);
        if (ret < 0)
                goto out;

        uuid_copy (args->uuid, rsp.uuid);

        op_ret = rsp.op_ret;
        op_errno = rsp.op_errno;

out:
        gd_mgmt_v3_collate_errors (args, op_ret, op_errno, NULL,
                                  GLUSTERD_MGMT_V3_UNLOCK,
                                  peerinfo, rsp.uuid);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
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
        glusterd_conf_t         *conf = THIS->private;
        xlator_t                *this = NULL;

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

        uuid_copy (req.uuid, my_uuid);
        req.op = op;
        synclock_unlock (&conf->big_lock);
        ret = gd_syncop_submit_request (peerinfo->rpc, &req, args, peerinfo,
                                        &gd_mgmt_v3_prog,
                                        GLUSTERD_MGMT_V3_UNLOCK,
                                        gd_mgmt_v3_unlock_cbk,
                                        (xdrproc_t) xdr_gd1_mgmt_v3_unlock_req);
        synclock_lock (&conf->big_lock);
out:
        GF_FREE (req.dict.dict_val);
        gf_log (this->name, GF_LOG_TRACE, "Returning %d", ret);
        return ret;
}

int
glusterd_mgmt_v3_release_peer_locks (glusterd_conf_t  *conf, glusterd_op_t op,
                                     dict_t *dict, int32_t op_ret,
                                     char **op_errstr, int npeers,
                                     gf_boolean_t  is_acquired)
{
        int32_t              ret        = -1;
        int32_t              peer_cnt   = 0;
        uuid_t               peer_uuid  = {0};
        xlator_t            *this       = NULL;
        glusterd_peerinfo_t *peerinfo   = NULL;
        struct syncargs      args       = {0};
        struct list_head    *peers      = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (conf);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        peers = &conf->xaction_peers;

        /* If the lock has not been held during this
         * transaction, do not send unlock requests */
        if (!is_acquired)
                goto out;

        if (!npeers) {
                ret = 0;
                goto out;
        }

        /* Sending mgmt_v3 unlock req to other nodes in the cluster */
        gd_syncargs_init (&args, NULL);
        synctask_barrier_init((&args));
        peer_cnt = 0;
        list_for_each_entry (peerinfo, peers, op_peers_list) {
                gd_mgmt_v3_unlock (op, dict, peerinfo, &args,
                                   MY_UUID, peer_uuid);
                peer_cnt++;
        }
        gd_synctask_barrier_wait((&args), peer_cnt);

        if (args.op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unlock failed on peers");

                if (!op_ret && args.errstr)
                         *op_errstr = gf_strdup (args.errstr);
        }

        ret = args.op_ret;

        gf_log (this->name, GF_LOG_DEBUG, "Sent unlock op req for %s "
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
        int32_t                     npeers           = 0;
        dict_t                      *req_dict        = NULL;
        dict_t                      *tmp_dict        = NULL;
        glusterd_conf_t             *conf            = NULL;
        char                        *op_errstr       = NULL;
        xlator_t                    *this            = NULL;
        gf_boolean_t                is_acquired      = _gf_false;
        uuid_t                      *originator_uuid = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        conf = this->private;
        GF_ASSERT (conf);

        /* Save the MY_UUID as the originator_uuid. This originator_uuid
         * will be used by is_origin_glusterd() to determine if a node
         * is the originator node for a command. */
        originator_uuid = GF_CALLOC (1, sizeof(uuid_t),
                                     gf_common_mt_uuid_t);
        if (!originator_uuid) {
                ret = -1;
                goto out;
        }

        uuid_copy (*originator_uuid, MY_UUID);
        ret = dict_set_bin (dict, "originator_uuid",
                            originator_uuid, sizeof (uuid_t));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set originator_uuid.");
                goto out;
        }

        /* Marking the operation as complete synctasked */
        ret = dict_set_int32 (dict, "is_synctasked", _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set synctasked flag.");
                goto out;
        }

        /* Use a copy at local unlock as cli response will be sent before
         * the unlock and the volname in the dict might be removed */
        tmp_dict = dict_new();
        if (!tmp_dict) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to create dict");
                goto out;
        }
        dict_copy (dict, tmp_dict);

        /* BUILD PEERS LIST */
        INIT_LIST_HEAD (&conf->xaction_peers);
        npeers = gd_build_peers_list  (&conf->peers, &conf->xaction_peers, op);

        /* LOCKDOWN PHASE - Acquire mgmt_v3 locks */
        ret = glusterd_mgmt_v3_initiate_lockdown (conf, op, dict, &op_errstr,
                                                  npeers, &is_acquired);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "mgmt_v3 lockdown failed.");
                goto out;
        }

        /* BUILD PAYLOAD */
        ret = glusterd_mgmt_v3_build_payload (&req_dict, &op_errstr, dict, op);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                goto out;
        }

        /* PRE-COMMIT VALIDATE PHASE */
        ret = glusterd_mgmt_v3_pre_validate (conf, op, req_dict,
                                             &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Pre Validation Failed");
                goto out;
        }

        /* COMMIT OP PHASE */
        ret = glusterd_mgmt_v3_commit (conf, op, dict, req_dict,
                                       &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Commit Op Failed");
                goto out;
        }

        /* POST-COMMIT VALIDATE PHASE */
        /* As of now, post_validate is not handling any other
           commands other than snapshot. So as of now, I am
           sending 0 (op_ret as 0).
        */
        ret = glusterd_mgmt_v3_post_validate (conf, op, 0, dict, req_dict,
                                              &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Post Validation Failed");
                goto out;
        }

        ret = 0;
out:
        op_ret = ret;
        /* UNLOCK PHASE FOR PEERS*/
        (void) glusterd_mgmt_v3_release_peer_locks (conf, op, dict,
                                                    op_ret, &op_errstr,
                                                    npeers, is_acquired);

        /* LOCAL VOLUME(S) UNLOCK */
        if (is_acquired) {
                /* Trying to release multiple mgmt_v3 locks */
                ret = glusterd_multiple_mgmt_v3_unlock (tmp_dict, MY_UUID);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to release mgmt_v3 locks on localhost");
                        op_ret = ret;
                }
        }

        /* SEND CLI RESPONSE */
        glusterd_op_send_cli_response (op, op_ret, 0, req, dict, op_errstr);

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
                gf_log (this->name, GF_LOG_ERROR, "Volname not present in "
                        "dict");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &vol);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Volume %s not found ",
                        volname);
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (dict, "barrier", option);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set barrier op "
                        "in request dictionary");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (vol->dict, "features.barrier",
                                          option);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set barrier op "
                        "in volume option dict");
                goto out;
        }

        gd_update_volume_op_versions (vol);

        ret = glusterd_create_volfiles (vol);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create volfiles");
                goto out;
        }

        ret = glusterd_store_volinfo (vol, GLUSTERD_VOLINFO_VER_AC_INCREMENT);

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_mgmt_v3_initiate_snap_phases (rpcsvc_request_t *req, glusterd_op_t op,
                                       dict_t *dict)
{
        int32_t                     ret              = -1;
        int32_t                     op_ret           = -1;
        int32_t                     npeers           = 0;
        dict_t                      *req_dict        = NULL;
        dict_t                      *tmp_dict        = NULL;
        glusterd_conf_t             *conf            = NULL;
        char                        *op_errstr       = NULL;
        xlator_t                    *this            = NULL;
        gf_boolean_t                is_acquired      = _gf_false;
        uuid_t                      *originator_uuid = NULL;
        gf_boolean_t                success          = _gf_false;
        char                        *cli_errstr      = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (req);
        GF_ASSERT (dict);
        conf = this->private;
        GF_ASSERT (conf);

        /* Save the MY_UUID as the originator_uuid. This originator_uuid
         * will be used by is_origin_glusterd() to determine if a node
         * is the originator node for a command. */
        originator_uuid = GF_CALLOC (1, sizeof(uuid_t),
                                     gf_common_mt_uuid_t);
        if (!originator_uuid) {
                ret = -1;
                goto out;
        }

        uuid_copy (*originator_uuid, MY_UUID);
        ret = dict_set_bin (dict, "originator_uuid",
                            originator_uuid, sizeof (uuid_t));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set originator_uuid.");
                goto out;
        }

        /* Marking the operation as complete synctasked */
        ret = dict_set_int32 (dict, "is_synctasked", _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to set synctasked flag.");
                goto out;
        }

        /* Use a copy at local unlock as cli response will be sent before
         * the unlock and the volname in the dict might be removed */
        tmp_dict = dict_new();
        if (!tmp_dict) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to create dict");
                goto out;
        }
        dict_copy (dict, tmp_dict);

        /* BUILD PEERS LIST */
        INIT_LIST_HEAD (&conf->xaction_peers);
        npeers = gd_build_peers_list  (&conf->peers, &conf->xaction_peers, op);

        /* LOCKDOWN PHASE - Acquire mgmt_v3 locks */
        ret = glusterd_mgmt_v3_initiate_lockdown (conf, op, dict, &op_errstr,
                                                  npeers, &is_acquired);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "mgmt_v3 lockdown failed.");
                goto out;
        }

        /* BUILD PAYLOAD */
        ret = glusterd_mgmt_v3_build_payload (&req_dict, &op_errstr, dict, op);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, LOGSTR_BUILD_PAYLOAD,
                        gd_op_list[op]);
                if (op_errstr == NULL)
                        gf_asprintf (&op_errstr, OPERRSTR_BUILD_PAYLOAD);
                goto out;
        }

        /* PRE-COMMIT VALIDATE PHASE */
        ret = glusterd_mgmt_v3_pre_validate (conf, op, req_dict,
                                            &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Pre Validation Failed");
                goto out;
        }

        /* quorum check of the volume is done here */
        ret = glusterd_snap_quorum_check (req_dict, _gf_false, &op_errstr);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                                "Volume quorum check failed");
                goto out;
        }

        /* Set the operation type as pre, so that differentiation can be
         * made whether the brickop is sent during pre-commit or post-commit
         */
        ret = dict_set_dynstr_with_alloc (req_dict, "operation-type", "pre");
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "operation-type in dictionary");
                goto out;
        }

        ret = glusterd_mgmt_v3_brick_op (conf, op, req_dict,
                                        &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Brick Ops Failed");
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
                gf_log (this->name, GF_LOG_ERROR, "failed to set dict");
                goto unbarrier;
        }

        ret = glusterd_mgmt_v3_commit (conf, op, dict, req_dict,
                                       &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Commit Op Failed");
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
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "operation-type in dictionary");
                goto out;
        }

        ret = glusterd_mgmt_v3_brick_op (conf, op, req_dict,
                                         &op_errstr, npeers);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Brick Ops Failed");
                goto out;
        }

        /*Do a quorum check if the commit phase is successful*/
        if (success) {
                //quorum check of the snapshot volume
                ret = glusterd_snap_quorum_check (dict, _gf_true, &op_errstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, 
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
        ret = glusterd_mgmt_v3_post_validate (conf, op, op_ret, dict, req_dict,
                                              &op_errstr, npeers);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Post Validation Failed");
                op_ret = -1;
        }

        /* UNLOCK PHASE FOR PEERS*/
        (void) glusterd_mgmt_v3_release_peer_locks (conf, op, dict,
                                                    op_ret, &op_errstr,
                                                    npeers, is_acquired);

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
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to release mgmt_v3 locks on localhost");
                        op_ret = ret;
                }
        }

        /* SEND CLI RESPONSE */
        glusterd_op_send_cli_response (op, op_ret, 0, req, dict, op_errstr);

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
