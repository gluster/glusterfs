/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "fd-lk.h"
#include "client.h"
#include "xlator.h"
#include "defaults.h"
#include "glusterfs.h"
#include "statedump.h"
#include "compat-errno.h"

#include "glusterfs3.h"
#include "portmap-xdr.h"
#include "rpc-common-xdr.h"
#include "client-messages.h"

#define CLIENT_REOPEN_MAX_ATTEMPTS 1024
extern rpc_clnt_prog_t clnt3_3_fop_prog;
extern rpc_clnt_prog_t clnt_pmap_prog;

int client_set_lk_version_cbk (struct rpc_req *req, struct iovec *iov,
                               int count, void *myframe);

int client_set_lk_version (xlator_t *this);

typedef struct client_fd_lk_local {
        int             ref;
        gf_boolean_t    error;
        gf_lock_t       lock;
        clnt_fd_ctx_t *fdctx;
}clnt_fd_lk_local_t;

int
client3_getspec_cbk (struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
        gf_getspec_rsp           rsp   = {0,};
        call_frame_t            *frame = NULL;
        int                      ret   = 0;

        frame = myframe;

        if (!frame || !frame->this) {
                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL, PC_MSG_INVALID_ENTRY,
                        "frame not found with the request, returning EINVAL");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }
        if (-1 == req->rpc_status) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error, "
                        "returning ENOTCONN");
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED,
                        "XDR decoding failed, returning EINVAL");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        PC_MSG_VOL_FILE_NOT_FOUND, "failed to get the 'volume "
                        "file' from server");
                goto out;
        }

out:
        CLIENT_STACK_UNWIND (getspec, frame, rsp.op_ret, rsp.op_errno,
                             rsp.spec);

        /* Don't use 'GF_FREE', this is allocated by libc */
        free (rsp.spec);
        free (rsp.xdata.xdata_val);

        return 0;
}

int32_t client3_getspec (call_frame_t *frame, xlator_t *this, void *data)
{
        clnt_conf_t    *conf     = NULL;
        clnt_args_t    *args     = NULL;
        gf_getspec_req  req      = {0,};
        int             op_errno = ESTALE;
        int             ret      = 0;

        if (!frame || !this || !data)
                goto unwind;

        args = data;
        conf = this->private;
        req.flags = args->flags;
        req.key   = (char *)args->name;

        ret = client_submit_request (this, &req, frame, conf->handshake,
                                     GF_HNDSK_GETSPEC, client3_getspec_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_getspec_req);

        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_SEND_REQ_FAIL,
                        "failed to send the request");
        }

        return 0;
unwind:
        CLIENT_STACK_UNWIND (getspec, frame, -1, op_errno, NULL);
        return 0;

}

int
client_notify_parents_child_up (xlator_t *this)
{
        clnt_conf_t *conf = NULL;
        int          ret  = 0;

        GF_VALIDATE_OR_GOTO("client", this, out);
        conf = this->private;
        GF_VALIDATE_OR_GOTO(this->name, conf, out);

        if (conf->child_up) {
                ret = client_notify_dispatch_uniq (this, GF_EVENT_CHILD_UP,
                                                   NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                PC_MSG_CHILD_UP_NOTIFY_FAILED,
                                "notify of CHILD_UP failed");
                        goto out;
                }
        } else {
                gf_msg (this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_STATUS,
                        "Defering sending CHILD_UP message as the client "
                        "translators are not yet ready to serve.");
        }

out:
        return 0;
}

int
clnt_fd_lk_reacquire_failed (xlator_t *this, clnt_fd_ctx_t *fdctx,
                             clnt_conf_t *conf)
{
        int      ret = -1;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, conf, out);
        GF_VALIDATE_OR_GOTO (this->name, fdctx, out);

        pthread_mutex_lock (&conf->lock);
        {
                fdctx->remote_fd     = -1;
                fdctx->lk_heal_state = GF_LK_HEAL_DONE;
        }
        pthread_mutex_unlock (&conf->lock);

        ret = 0;
out:
        return ret;
}

int
client_set_lk_version_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *myframe)
{
        int32_t           ret    = -1;
        call_frame_t     *fr     = NULL;
        gf_set_lk_ver_rsp rsp    = {0,};

        fr = (call_frame_t *) myframe;
        GF_VALIDATE_OR_GOTO ("client", fr, out);

        if (req->rpc_status == -1) {
                gf_msg (fr->this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_set_lk_ver_rsp);
        if (ret < 0)
                gf_msg (fr->this->name, GF_LOG_WARNING, 0,
                        PC_MSG_XDR_DECODING_FAILED, "xdr decoding failed");
        else
                gf_msg (fr->this->name, GF_LOG_INFO, 0,
                        PC_MSG_LOCK_VERSION_SERVER,
                        "Server lk version = %d", rsp.lk_ver);

        ret = 0;
out:
        if (fr)
                STACK_DESTROY (fr->root);

        return ret;
}

//TODO: Check for all released fdctx and destroy them
int
client_set_lk_version (xlator_t *this)
{
        int                 ret      = -1;
        clnt_conf_t        *conf     = NULL;
        call_frame_t       *frame    = NULL;
        gf_set_lk_ver_req   req      = {0, };
        char               *process_uuid = NULL;

        GF_VALIDATE_OR_GOTO ("client", this, err);

        conf = (clnt_conf_t *) this->private;

        req.lk_ver = client_get_lk_ver (conf);
        ret = dict_get_str (this->options, "process-uuid", &process_uuid);
        if (!process_uuid) {
                ret = -1;
                goto err;
        }
        req.uid = gf_strdup (process_uuid);
        if (!req.uid) {
                ret = -1;
                goto err;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        gf_msg_debug (this->name, 0, "Sending SET_LK_VERSION");

        ret = client_submit_request (this, &req, frame,
                                     conf->handshake,
                                     GF_HNDSK_SET_LK_VER,
                                     client_set_lk_version_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_set_lk_ver_req);
out:
        GF_FREE (req.uid);
        return ret;
err:
        gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_SET_LK_VERSION_ERROR,
                "Failed to send SET_LK_VERSION to server");

        return ret;
}

int
client_fd_lk_count (fd_lk_ctx_t *lk_ctx)
{
        int               count     = 0;
        fd_lk_ctx_node_t *fd_lk   = NULL;

        GF_VALIDATE_OR_GOTO ("client", lk_ctx, err);

        LOCK (&lk_ctx->lock);
        {
                list_for_each_entry (fd_lk, &lk_ctx->lk_list, next)
                        count++;
        }
        UNLOCK (&lk_ctx->lock);

        return count;
err:
        return -1;
}

clnt_fd_lk_local_t *
clnt_fd_lk_local_ref (xlator_t *this, clnt_fd_lk_local_t *local)
{
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        LOCK (&local->lock);
        {
                local->ref++;
        }
        UNLOCK (&local->lock);
out:
        return local;
}

int
clnt_fd_lk_local_unref (xlator_t *this, clnt_fd_lk_local_t *local)
{
        int   ref = -1;

        GF_VALIDATE_OR_GOTO (this->name, local, out);

        LOCK (&local->lock);
        {
                ref = --local->ref;
        }
        UNLOCK (&local->lock);

        if (ref == 0) {
                LOCK_DESTROY (&local->lock);
                GF_FREE (local);
        }
out:
        return ref;
}

clnt_fd_lk_local_t *
clnt_fd_lk_local_create (clnt_fd_ctx_t *fdctx)
{
        clnt_fd_lk_local_t *local = NULL;

        local = GF_CALLOC (1, sizeof (clnt_fd_lk_local_t),
                           gf_client_mt_clnt_fd_lk_local_t);
        if (!local)
                goto out;

        local->ref    = 1;
        local->error  = _gf_false;
        local->fdctx = fdctx;

        LOCK_INIT (&local->lock);
out:
        return local;
}

int
clnt_release_reopen_fd_cbk (struct rpc_req *req, struct iovec *iov,
                            int count, void *myframe)
{
        xlator_t       *this   = NULL;
        call_frame_t   *frame  = NULL;
        clnt_conf_t    *conf   = NULL;
        clnt_fd_ctx_t  *fdctx  = NULL;

        frame  = myframe;
        this   = frame->this;
        fdctx  = (clnt_fd_ctx_t *) frame->local;
        conf   = (clnt_conf_t *) this->private;

        clnt_fd_lk_reacquire_failed (this, fdctx, conf);

        fdctx->reopen_done (fdctx, fdctx->remote_fd, this);

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        return 0;
}

int
clnt_release_reopen_fd (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int               ret     = -1;
        clnt_conf_t      *conf    = NULL;
        call_frame_t     *frame   = NULL;
        gfs3_release_req  req     = {{0,},};

        conf = (clnt_conf_t *) this->private;

        frame  = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        frame->local = (void *) fdctx;
        req.fd       = fdctx->remote_fd;

        ret    = client_submit_request (this, &req, frame, conf->fops,
                                        GFS3_OP_RELEASE,
                                        clnt_release_reopen_fd_cbk, NULL,
                                        NULL, 0, NULL, 0, NULL,
                                        (xdrproc_t)xdr_gfs3_releasedir_req);
 out:
        if (ret) {
                clnt_fd_lk_reacquire_failed (this, fdctx, conf);
                fdctx->reopen_done (fdctx, fdctx->remote_fd, this);
        }
        return 0;
}

int
clnt_reacquire_lock_error (xlator_t *this, clnt_fd_ctx_t *fdctx,
                           clnt_conf_t *conf)
{
        int32_t   ret       = -1;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fdctx, out);
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        clnt_release_reopen_fd (this, fdctx);

        ret = 0;
out:
        return ret;
}

gf_boolean_t
clnt_fd_lk_local_error_status (xlator_t *this,
                               clnt_fd_lk_local_t *local)
{
        gf_boolean_t error = _gf_false;

        LOCK (&local->lock);
        {
                error = local->error;
        }
        UNLOCK (&local->lock);

        return error;
}

int
clnt_fd_lk_local_mark_error (xlator_t *this,
                             clnt_fd_lk_local_t *local)
{
        int32_t       ret   = -1;
        clnt_conf_t  *conf  = NULL;
        gf_boolean_t  error = _gf_false;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        conf = (clnt_conf_t *) this->private;

        LOCK (&local->lock);
        {
                error        = local->error;
                local->error = _gf_true;
        }
        UNLOCK (&local->lock);

        if (!error)
                clnt_reacquire_lock_error (this, local->fdctx, conf);
        ret = 0;
out:
        return ret;
}

int
client_reacquire_lock_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *myframe)
{
        int32_t             ret        = -1;
        xlator_t           *this       = NULL;
        gfs3_lk_rsp         rsp        = {0,};
        call_frame_t       *frame      = NULL;
        clnt_conf_t        *conf       = NULL;
        clnt_fd_ctx_t      *fdctx      = NULL;
        clnt_fd_lk_local_t *local      = NULL;
        struct gf_flock     lock       = {0,};

        frame = (call_frame_t *) myframe;
        this  = frame->this;
        local = (clnt_fd_lk_local_t *) frame->local;
        conf  = (clnt_conf_t *) this->private;

        if (req->rpc_status == -1) {
                gf_msg ("client", GF_LOG_WARNING, 0, PC_MSG_CLIENT_REQ_FAIL,
                        "request failed at rpc");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_lk_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                goto out;
        }

        if (rsp.op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_LOCK_REQ_FAIL,
                        "lock request failed");
                ret = -1;
                goto out;
        }

        fdctx = local->fdctx;

        gf_proto_flock_to_flock (&rsp.flock, &lock);

        gf_msg_debug (this->name, 0, "%s type lock reacquired on file "
                      "with gfid %s from %"PRIu64 " to %"PRIu64,
                      get_lk_type (lock.l_type), uuid_utoa (fdctx->gfid),
                      lock.l_start, lock.l_start + lock.l_len);

        if (!clnt_fd_lk_local_error_status (this, local) &&
            clnt_fd_lk_local_unref (this, local) == 0) {
                pthread_mutex_lock (&conf->lock);
                {
                        fdctx->lk_heal_state = GF_LK_HEAL_DONE;
                }
                pthread_mutex_unlock (&conf->lock);

                fdctx->reopen_done (fdctx, fdctx->remote_fd, this);
        }

        ret = 0;
out:
        if (ret < 0) {
                clnt_fd_lk_local_mark_error (this, local);

                clnt_fd_lk_local_unref (this, local);
        }

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        return ret;
}

int
_client_reacquire_lock (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int32_t             ret       = -1;
        int32_t             gf_cmd    = 0;
        int32_t             gf_type   = 0;
        gfs3_lk_req         req       = {{0,},};
        struct gf_flock     flock     = {0,};
        fd_lk_ctx_t        *lk_ctx    = NULL;
        clnt_fd_lk_local_t *local     = NULL;
        fd_lk_ctx_node_t   *fd_lk     = NULL;
        call_frame_t       *frame     = NULL;
        clnt_conf_t        *conf      = NULL;

        conf   = (clnt_conf_t *) this->private;
        lk_ctx = fdctx->lk_ctx;

        local = clnt_fd_lk_local_create (fdctx);
        if (!local) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_LOCK_ERROR,
                        "clnt_fd_lk_local_create failed, aborting reacquring "
                        "of locks on %s.", uuid_utoa (fdctx->gfid));
                clnt_reacquire_lock_error (this, fdctx, conf);
                goto out;
        }

        list_for_each_entry (fd_lk, &lk_ctx->lk_list, next) {
                memcpy (&flock, &fd_lk->user_flock,
                        sizeof (struct gf_flock));

                /* Always send F_SETLK even if the cmd was F_SETLKW */
                /* to avoid frame being blocked if lock cannot be granted. */
                ret = client_cmd_to_gf_cmd (F_SETLK, &gf_cmd);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                PC_MSG_LOCK_ERROR, "client_cmd_to_gf_cmd "
                                "failed, aborting reacquiring of locks");
                        break;
                }

                gf_type   = client_type_to_gf_type (flock.l_type);
                req.fd    = fdctx->remote_fd;
                req.cmd   = gf_cmd;
                req.type  = gf_type;
                (void) gf_proto_flock_from_flock (&req.flock,
                                                  &flock);

                memcpy (req.gfid, fdctx->gfid, 16);

                frame = create_frame (this, this->ctx->pool);
                if (!frame) {
                        ret = -1;
                        break;
                }

                frame->local          = clnt_fd_lk_local_ref (this, local);
                frame->root->lk_owner = fd_lk->user_flock.l_owner;

                ret = client_submit_request (this, &req, frame,
                                             conf->fops, GFS3_OP_LK,
                                             client_reacquire_lock_cbk,
                                             NULL, NULL, 0, NULL, 0, NULL,
                                             (xdrproc_t)xdr_gfs3_lk_req);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                PC_MSG_LOCK_REACQUIRE, "reacquiring locks "
                                "failed on file with gfid %s",
                                uuid_utoa (fdctx->gfid));
                        break;
                }

                ret   = 0;
                frame = NULL;
        }

        if (local)
                (void) clnt_fd_lk_local_unref (this, local);
out:
        return ret;
}

int
client_reacquire_lock (xlator_t *this, clnt_fd_ctx_t *fdctx)
{
        int32_t          ret       = -1;
        fd_lk_ctx_t     *lk_ctx    = NULL;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fdctx, out);

        if (client_fd_lk_list_empty (fdctx->lk_ctx, _gf_false)) {
                gf_msg_debug (this->name, 0,
                              "fd lock list is empty");
                fdctx->reopen_done (fdctx, fdctx->remote_fd, this);
        } else {
                lk_ctx = fdctx->lk_ctx;

                LOCK (&lk_ctx->lock);
                {
                        (void) _client_reacquire_lock (this, fdctx);
                }
                UNLOCK (&lk_ctx->lock);
        }
        ret = 0;
out:
        return ret;
}

void
client_default_reopen_done (clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this)
{
        gf_log_callingfn (this->name, GF_LOG_WARNING,
                          "This function should never be called");
}

void
client_reopen_done (clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this)
{
        clnt_conf_t  *conf    = NULL;
        gf_boolean_t destroy  = _gf_false;

        conf = this->private;

        pthread_mutex_lock (&conf->lock);
        {
                fdctx->remote_fd = rfd;
                fdctx->reopen_attempts = 0;
                fdctx->reopen_done = client_default_reopen_done;
                if (!fdctx->released)
                        list_add_tail (&fdctx->sfd_pos, &conf->saved_fds);
                else
                        destroy = _gf_true;
        }
        pthread_mutex_unlock (&conf->lock);

        if (destroy)
                client_fdctx_destroy (this, fdctx);

}

void
client_child_up_reopen_done (clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this)
{
        clnt_conf_t  *conf    = NULL;
        uint64_t     fd_count = 0;

        conf = this->private;

        LOCK (&conf->rec_lock);
        {
                fd_count = --(conf->reopen_fd_count);
        }
        UNLOCK (&conf->rec_lock);

        client_reopen_done (fdctx, rfd, this);
        if (fd_count == 0) {
                gf_msg (this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_UP_NOTIFY,
                        "last fd open'd/lock-self-heal'd - notifying CHILD-UP");
                client_set_lk_version (this);
                client_notify_parents_child_up (this);
        }
}

int
client3_3_reopen_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void           *myframe)
{
        int32_t        ret                   = -1;
        gfs3_open_rsp  rsp                   = {0,};
        gf_boolean_t   attempt_lock_recovery = _gf_false;
        clnt_local_t  *local                 = NULL;
        clnt_conf_t   *conf                  = NULL;
        clnt_fd_ctx_t *fdctx                 = NULL;
        call_frame_t  *frame                 = NULL;
        xlator_t      *this                  = NULL;

        frame = myframe;
        this  = frame->this;
        conf  = this->private;
        local = frame->local;
        fdctx = local->fdctx;

        if (-1 == req->rpc_status) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error, "
                        "returning ENOTCONN");
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_open_rsp);
        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret < 0) {
                gf_msg (frame->this->name, GF_LOG_WARNING, rsp.op_errno,
                        PC_MSG_DIR_OP_SUCCESS, "reopen on %s failed.",
                        local->loc.path);
        } else {
                gf_msg_debug (frame->this->name, 0,
                              "reopen on %s succeeded (remote-fd = %"PRId64")",
                              local->loc.path, rsp.fd);
        }

        if (rsp.op_ret == -1) {
                ret = -1;
                goto out;
        }

        pthread_mutex_lock (&conf->lock);
        {
                if (!fdctx->released) {
                        if (conf->lk_heal &&
                            !client_fd_lk_list_empty (fdctx->lk_ctx,
                                                      _gf_false)) {
                                attempt_lock_recovery = _gf_true;
                                fdctx->lk_heal_state  = GF_LK_HEAL_IN_PROGRESS;
                        }
                }
        }
        pthread_mutex_unlock (&conf->lock);

        ret = 0;

        if (attempt_lock_recovery) {
                /* Delay decrementing the reopen fd count until all the
                   locks corresponding to this fd are acquired.*/
                gf_msg_debug (this->name, 0, "acquiring locks "
                              "on %s", local->loc.path);
                ret = client_reacquire_lock (frame->this, local->fdctx);
                if (ret) {
                        clnt_reacquire_lock_error (this, local->fdctx, conf);
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                PC_MSG_LOCK_ERROR, "acquiring locks failed "
                                "on %s", local->loc.path);
                }
        }

out:
        if (!attempt_lock_recovery)
                fdctx->reopen_done (fdctx, (rsp.op_ret) ? -1 : rsp.fd, this);

        frame->local = NULL;
        STACK_DESTROY (frame->root);

        client_local_wipe (local);

        return 0;
}

int
client3_3_reopendir_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void           *myframe)
{
        int32_t        ret   = -1;
        gfs3_open_rsp  rsp   = {0,};
        clnt_local_t  *local = NULL;
        clnt_fd_ctx_t *fdctx = NULL;
        call_frame_t  *frame = NULL;

        frame = myframe;
        local = frame->local;
        fdctx = local->fdctx;


        if (-1 == req->rpc_status) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error, "
                        "returning ENOTCONN");
                rsp.op_ret   = -1;
                rsp.op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_opendir_rsp);
        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (rsp.op_ret < 0) {
                gf_msg (frame->this->name, GF_LOG_WARNING, rsp.op_errno,
                        PC_MSG_DIR_OP_FAILED, "reopendir on %s failed",
                        local->loc.path);
        } else {
                gf_msg (frame->this->name, GF_LOG_INFO, 0,
                        PC_MSG_DIR_OP_SUCCESS, "reopendir on %s succeeded "
                        "(fd = %"PRId64")", local->loc.path, rsp.fd);
        }

        if (-1 == rsp.op_ret) {
                ret = -1;
                goto out;
        }

out:
        fdctx->reopen_done (fdctx, (rsp.op_ret) ? -1 : rsp.fd, frame->this);

        frame->local = NULL;
        STACK_DESTROY (frame->root);
        client_local_wipe (local);

        return 0;
}

static int
protocol_client_reopendir (clnt_fd_ctx_t *fdctx, xlator_t *this)
{
        int               ret   = -1;
        gfs3_opendir_req  req   = {{0,},};
        clnt_local_t     *local = NULL;
        call_frame_t     *frame = NULL;
        clnt_conf_t      *conf  = NULL;

        conf = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                ret = -1;
                goto out;
        }
        local->fdctx    = fdctx;

        gf_uuid_copy (local->loc.gfid, fdctx->gfid);
        ret = loc_path (&local->loc, NULL);
        if (ret < 0)
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        memcpy (req.gfid, fdctx->gfid, 16);

        gf_msg_debug (frame->this->name, 0,
                      "attempting reopen on %s", local->loc.path);

        frame->local = local;

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPENDIR,
                                     client3_3_reopendir_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_opendir_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_DIR_OP_FAILED,
                        "failed to send the re-opendir request");
        }

        return 0;

out:
        if (local)
                client_local_wipe (local);

        fdctx->reopen_done (fdctx, fdctx->remote_fd, this);

        return 0;

}

static int
protocol_client_reopenfile (clnt_fd_ctx_t *fdctx, xlator_t *this)
{
        int            ret   = -1;
        gfs3_open_req  req   = {{0,},};
        clnt_local_t  *local = NULL;
        call_frame_t  *frame = NULL;
        clnt_conf_t   *conf  = NULL;

        conf  = this->private;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                ret = -1;
                goto out;
        }

        local->fdctx    = fdctx;
        gf_uuid_copy (local->loc.gfid, fdctx->gfid);
        ret = loc_path (&local->loc, NULL);
        if (ret < 0)
                goto out;

        frame->local    = local;

        memcpy (req.gfid, fdctx->gfid, 16);
        req.flags    = gf_flags_from_flags (fdctx->flags);
        req.flags    = req.flags & (~(O_TRUNC|O_CREAT|O_EXCL));

        gf_msg_debug (frame->this->name, 0,
                      "attempting reopen on %s", local->loc.path);

        ret = client_submit_request (this, &req, frame, conf->fops,
                                     GFS3_OP_OPEN, client3_3_reopen_cbk, NULL,
                                     NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gfs3_open_req);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_DIR_OP_FAILED,
                        "failed to send the re-open request");
        }

        return 0;

out:
        if (frame) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
        }

        if (local)
                client_local_wipe (local);

        fdctx->reopen_done (fdctx, fdctx->remote_fd, this);

        return 0;

}

static void
protocol_client_reopen (clnt_fd_ctx_t *fdctx, xlator_t *this)
{
        if (fdctx->is_dir)
                protocol_client_reopendir (fdctx, this);
        else
                protocol_client_reopenfile (fdctx, this);
}

gf_boolean_t
__is_fd_reopen_in_progress (clnt_fd_ctx_t *fdctx)
{
        if (fdctx->reopen_done == client_default_reopen_done)
                return _gf_false;
        return _gf_true;
}

void
client_attempt_reopen (fd_t *fd, xlator_t *this)
{
        clnt_conf_t   *conf  = NULL;
        clnt_fd_ctx_t *fdctx = NULL;
        gf_boolean_t  reopen = _gf_false;

        if (!fd || !this)
                goto out;

        conf = this->private;
        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (fd, this);
                if (!fdctx)
                        goto unlock;
                if (__is_fd_reopen_in_progress (fdctx))
                        goto unlock;
                if (fdctx->remote_fd != -1)
                        goto unlock;

                if (fdctx->reopen_attempts == CLIENT_REOPEN_MAX_ATTEMPTS) {
                        reopen = _gf_true;
                        fdctx->reopen_done = client_reopen_done;
                        list_del_init (&fdctx->sfd_pos);
                } else {
                        fdctx->reopen_attempts++;
                }
        }
unlock:
        pthread_mutex_unlock (&conf->lock);
        if (reopen)
                protocol_client_reopen (fdctx, this);
out:
        return;
}

int
client_post_handshake (call_frame_t *frame, xlator_t *this)
{
        clnt_conf_t            *conf = NULL;
        clnt_fd_ctx_t          *tmp = NULL;
        clnt_fd_ctx_t          *fdctx = NULL;
        struct list_head        reopen_head;

        int count = 0;

        if (!this || !this->private)
                goto out;

        conf = this->private;
        INIT_LIST_HEAD (&reopen_head);

        pthread_mutex_lock (&conf->lock);
        {
                list_for_each_entry_safe (fdctx, tmp, &conf->saved_fds,
                                          sfd_pos) {
                        if (fdctx->remote_fd != -1)
                                continue;

                        fdctx->reopen_done = client_child_up_reopen_done;
                        list_del_init (&fdctx->sfd_pos);
                        list_add_tail (&fdctx->sfd_pos, &reopen_head);
                        count++;
                }
        }
        pthread_mutex_unlock (&conf->lock);

        /* Delay notifying CHILD_UP to parents
           until all locks are recovered */
        if (count > 0) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PC_MSG_CHILD_UP_NOTIFY_DELAY, "%d fds open - Delaying "
                        "child_up until they are re-opened", count);
                client_save_number_fds (conf, count);

                list_for_each_entry_safe (fdctx, tmp, &reopen_head, sfd_pos) {
                        list_del_init (&fdctx->sfd_pos);

                        protocol_client_reopen (fdctx, this);
                }
        } else {
                gf_msg_debug (this->name, 0,
                              "No fds to open - notifying all parents child "
                              "up");
                client_set_lk_version (this);
                client_notify_parents_child_up (this);
        }
out:
        return 0;
}

int
client_setvolume_cbk (struct rpc_req *req, struct iovec *iov, int count, void *myframe)
{
        call_frame_t         *frame         = NULL;
        clnt_conf_t          *conf          = NULL;
        xlator_t             *this          = NULL;
        dict_t               *reply         = NULL;
        char                 *process_uuid  = NULL;
        char                 *remote_error  = NULL;
        char                 *remote_subvol = NULL;
        gf_setvolume_rsp      rsp           = {0,};
        int                   ret           = 0;
        int32_t               op_ret        = 0;
        int32_t               op_errno      = 0;
        gf_boolean_t          auth_fail     = _gf_false;
        uint32_t              lk_ver        = 0;

        frame = myframe;
        this  = frame->this;
        conf  = this->private;

        if (-1 == req->rpc_status) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error");
                op_ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_setvolume_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                op_ret = -1;
                goto out;
        }
        op_ret   = rsp.op_ret;
        op_errno = gf_error_to_errno (rsp.op_errno);
        if (-1 == rsp.op_ret) {
                gf_msg (frame->this->name, GF_LOG_WARNING, op_errno,
                        PC_MSG_VOL_SET_FAIL, "failed to set the volume");
        }

        reply = dict_new ();
        if (!reply)
                goto out;

        if (rsp.dict.dict_len) {
                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len, &reply);
                if (ret < 0) {
                        gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                                PC_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize buffer to dict");
                        goto out;
                }
        }

        ret = dict_get_str (reply, "ERROR", &remote_error);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_DICT_GET_FAILED, "failed to get ERROR "
                        "string from reply dict");
        }

        ret = dict_get_str (reply, "process-uuid", &process_uuid);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_DICT_GET_FAILED, "failed to get "
                        "'process-uuid' from reply dict");
        }

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        PC_MSG_SETVOLUME_FAIL,
                        "SETVOLUME on remote-host failed");
                errno = op_errno;
                if (remote_error &&
                    (strcmp ("Authentication failed", remote_error) == 0)) {
                        auth_fail = _gf_true;
                        op_ret = 0;
                }
                if (op_errno == ESTALE) {
                        ret = client_notify_dispatch (this,
                                                      GF_EVENT_VOLFILE_MODIFIED,
                                                      NULL);
                        if (ret)
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        PC_MSG_VOLFILE_NOTIFY_FAILED,
                                        "notify of VOLFILE_MODIFIED failed");
                }
                goto out;
        }

        ret = dict_get_str (this->options, "remote-subvolume",
                            &remote_subvol);
        if (ret || !remote_subvol) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_GET_FAILED,
                        "failed to find key 'remote-subvolume' in the options");
                goto out;
        }

        ret = dict_get_uint32 (reply, "child_up", &conf->child_up);
        if (ret) {
                /*
                 * This would happen in cases where the server trying to     *
                 * connect to this client is running an older version. Hence *
                 * setting the child_up to _gf_true in this case.            *
                 */
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_GET_FAILED,
                        "failed to find key 'child_up' in the options");
                conf->child_up = _gf_true;
        }

        ret = dict_get_uint32 (reply, "clnt-lk-version", &lk_ver);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_GET_FAILED,
                        "failed to find key 'clnt-lk-version' in the options");
                goto out;
        }

        gf_msg_debug (this->name, 0, "clnt-lk-version = %d, "
                      "server-lk-version = %d", client_get_lk_ver (conf),
                      lk_ver);
        /* TODO: currently setpeer path is broken */
        /*
        if (process_uuid && req->conn &&
            !strcmp (this->ctx->process_uuid, process_uuid)) {
                rpc_transport_t      *peer_trans    = NULL;
                uint64_t              peertrans_int = 0;

                ret = dict_get_uint64 (reply, "transport-ptr",
                                       &peertrans_int);
                if (ret)
                        goto out;

                gf_log (this->name, GF_LOG_WARNING,
                        "attaching to the local volume '%s'",
                        remote_subvol);

                peer_trans = (void *) (long) (peertrans_int);

                rpc_transport_setpeer (req->conn->trans, peer_trans);
        }
        */

        conf->client_id = glusterfs_leaf_position(this);

        gf_msg (this->name, GF_LOG_INFO, 0, PC_MSG_REMOTE_VOL_CONNECTED,
                "Connected to %s, attached to remote volume '%s'.",
                conf->rpc->conn.name,
                remote_subvol);

        rpc_clnt_set_connected (&conf->rpc->conn);

        op_ret = 0;
        conf->connecting = 0;
        conf->connected = 1;

        if (lk_ver != client_get_lk_ver (conf)) {
                gf_msg (this->name, GF_LOG_INFO, 0, PC_MSG_LOCK_MISMATCH,
                        "Server and Client lk-version numbers are not same, "
                        "reopening the fds");
                client_mark_fd_bad (this);
                client_post_handshake (frame, frame->this);
        } else {
                /*TODO: Traverse the saved fd list, and send
                  release to the server on fd's that were closed
                  during grace period */
                gf_msg (this->name, GF_LOG_INFO, 0, PC_MSG_LOCK_MATCH,
                        "Server and Client lk-version numbers are same, no "
                        "need to reopen the fds");
                client_notify_parents_child_up (frame->this);
        }

out:
        if (auth_fail) {
                gf_msg (this->name, GF_LOG_INFO, 0, PC_MSG_AUTH_FAILED,
                        "sending AUTH_FAILED event");
                ret = client_notify_dispatch (this, GF_EVENT_AUTH_FAILED, NULL);
                if (ret)
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                PC_MSG_AUTH_FAILED_NOTIFY_FAILED, "notify of "
                                "AUTH_FAILED failed");
                conf->connecting = 0;
                conf->connected = 0;
                ret = -1;
        }
        if (-1 == op_ret) {
                /* Let the connection/re-connection happen in
                 * background, for now, don't hang here,
                 * tell the parents that i am all ok..
                 */
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PC_MSG_CHILD_CONNECTING_EVENT, "sending "
                        "CHILD_CONNECTING event");
                ret = client_notify_dispatch (this, GF_EVENT_CHILD_CONNECTING,
                                              NULL);
                if (ret)
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED,
                                "notify of CHILD_CONNECTING failed");
                conf->connecting= 1;
                /*
                 * The reconnection *won't* happen in the background (see
                 * previous comment) unless we kill the current connection.
                 */
                rpc_transport_disconnect (conf->rpc->conn.trans, _gf_false);
                ret = 0;
        }

        free (rsp.dict.dict_val);

        STACK_DESTROY (frame->root);

        if (reply)
                dict_unref (reply);

        return ret;
}

int
client_setvolume (xlator_t *this, struct rpc_clnt *rpc)
{
        int               ret             = 0;
        gf_setvolume_req  req             = {{0,},};
        call_frame_t     *fr              = NULL;
        char             *process_uuid_xl = NULL;
        clnt_conf_t      *conf            = NULL;
        dict_t           *options         = NULL;
        char             counter_str[32]  = {0};

        options = this->options;
        conf    = this->private;

        if (conf->fops) {
                ret = dict_set_int32 (options, "fops-version",
                                      conf->fops->prognum);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PC_MSG_DICT_SET_FAILED, "failed to set "
                                "version-fops(%d) in handshake msg",
                                conf->fops->prognum);
                        goto fail;
                }
        }

        if (conf->mgmt) {
                ret = dict_set_int32 (options, "mgmt-version", conf->mgmt->prognum);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PC_MSG_DICT_SET_FAILED, "failed to set "
                                "version-mgmt(%d) in handshake msg",
                                conf->mgmt->prognum);
                        goto fail;
                }
        }

        /* When lock-heal is enabled:
         * With multiple graphs possible in the same process, we need a
           field to bring the uniqueness. Graph-ID should be enough to get the
           job done.
         * When lock-heal is disabled, connection-id should always be unique so
         * that server never gets to reuse the previous connection resources
         * so it cleans up the resources on every disconnect. Otherwise
         * it may lead to stale resources, i.e. leaked file desciptors,
         * inode/entry locks
        */
        if (!conf->lk_heal) {
                snprintf (counter_str, sizeof (counter_str),
                          "-%"PRIu64, conf->setvol_count);
                conf->setvol_count++;
        }
        ret = gf_asprintf (&process_uuid_xl, "%s-%s-%d%s",
                           this->ctx->process_uuid, this->name,
                           this->graph->id, counter_str);
        if (-1 == ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        PC_MSG_PROCESS_UUID_SET_FAIL, "asprintf failed while "
                        "setting process_uuid");
                goto fail;
        }

        ret = dict_set_dynstr (options, "process-uuid", process_uuid_xl);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SET_FAILED,
                        "failed to set process-uuid(%s) in handshake msg",
                        process_uuid_xl);
                goto fail;
        }

        ret = dict_set_str (options, "client-version", PACKAGE_VERSION);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_SET_FAILED,
                        "failed to set client-version(%s) in handshake msg",
                        PACKAGE_VERSION);
        }

        if (this->ctx->cmd_args.volfile_server) {
                if (this->ctx->cmd_args.volfile_id) {
                        ret = dict_set_str (options, "volfile-key",
                                            this->ctx->cmd_args.volfile_id);
                        if (ret)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        PC_MSG_DICT_SET_FAILED, "failed to "
                                        "set 'volfile-key'");
                }
                ret = dict_set_uint32 (options, "volfile-checksum",
                                       this->graph->volfile_checksum);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PC_MSG_DICT_SET_FAILED, "failed to set "
                                "'volfile-checksum'");
        }

        ret = dict_set_int16 (options, "clnt-lk-version",
                              client_get_lk_ver (conf));
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_SET_FAILED,
                        "failed to set clnt-lk-version(%"PRIu32") in handshake "
                        "msg", client_get_lk_ver (conf));
        }

        ret = dict_set_int32 (options, "opversion", GD_OP_VERSION_MAX);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SET_FAILED,
                        "Failed to set client opversion in handshake message");
        }

        ret = dict_serialized_length (options);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_ERROR,
                        "failed to get serialized length of dict");
                ret = -1;
                goto fail;
        }
        req.dict.dict_len = ret;
        req.dict.dict_val = GF_CALLOC (1, req.dict.dict_len,
                                       gf_client_mt_clnt_req_buf_t);
        ret = dict_serialize (options, req.dict.dict_val);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        PC_MSG_DICT_SERIALIZE_FAIL, "failed to serialize "
                        "dictionary");
                goto fail;
        }

        fr  = create_frame (this, this->ctx->pool);
        if (!fr)
                goto fail;

        ret = client_submit_request (this, &req, fr, conf->handshake,
                                     GF_HNDSK_SETVOLUME, client_setvolume_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_setvolume_req);

fail:
        GF_FREE (req.dict.dict_val);

        return ret;
}

int
select_server_supported_programs (xlator_t *this, gf_prog_detail *prog)
{
        gf_prog_detail *trav     = NULL;
        clnt_conf_t    *conf     = NULL;
        int             ret      = -1;

        if (!this || !prog) {
                gf_msg (THIS->name, GF_LOG_WARNING, 0, PC_MSG_PGM_NOT_FOUND,
                        "xlator not found OR RPC program not found");
                goto out;
        }

        conf = this->private;
        trav = prog;

        while (trav) {
                /* Select 'programs' */
                if ((clnt3_3_fop_prog.prognum == trav->prognum) &&
                    (clnt3_3_fop_prog.progver == trav->progver)) {
                        conf->fops = &clnt3_3_fop_prog;
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                PC_MSG_VERSION_INFO, "Using Program %s, "
                                "Num (%"PRId64"), Version (%"PRId64")",
                                trav->progname, trav->prognum, trav->progver);
                        ret = 0;
                }
                if (ret) {
                        gf_msg_trace (this->name, 0,
                                      "%s (%"PRId64") not supported",
                                      trav->progname, trav->progver);
                }
                trav = trav->next;
        }

out:
        return ret;
}


int
server_has_portmap (xlator_t *this, gf_prog_detail *prog)
{
        gf_prog_detail *trav     = NULL;
        int             ret      = -1;

        if (!this || !prog) {
                gf_msg (THIS->name, GF_LOG_WARNING, 0, PC_MSG_PGM_NOT_FOUND,
                        "xlator not found OR RPC program not found");
                goto out;
        }

        trav = prog;

        while (trav) {
                if ((trav->prognum == GLUSTER_PMAP_PROGRAM) &&
                    (trav->progver == GLUSTER_PMAP_VERSION)) {
                        gf_msg_debug (this->name, 0,
                                      "detected portmapper on server");
                        ret = 0;
                        break;
                }
                trav = trav->next;
        }

out:
        return ret;
}


int
client_query_portmap_cbk (struct rpc_req *req, struct iovec *iov, int count, void *myframe)
{
        struct pmap_port_by_brick_rsp     rsp   = {0,};
        call_frame_t                     *frame = NULL;
        clnt_conf_t                      *conf  = NULL;
        int                               ret   = -1;
        struct rpc_clnt_config            config = {0, };
        xlator_t                         *this   = NULL;

        frame = myframe;
        if (!frame || !frame->this || !frame->this->private) {
                gf_msg (THIS->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_INVALID_ENTRY, "frame not found with rpc "
                        "request");
                goto out;
        }
        this  = frame->this;
        conf  = frame->this->private;

        if (-1 == req->rpc_status) {
                gf_msg (this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error, "
                        "try again later");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_port_by_brick_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                goto out;
        }

        if (-1 == rsp.op_ret) {
                ret = -1;
                if (!conf->portmap_err_logged) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PC_MSG_PORT_NUM_ERROR, "failed to get the "
                                "port number for remote subvolume. Please run "
                                "'gluster volume status' on server to see if "
                                "brick process is running.");
                } else {
                        gf_msg_debug (this->name, 0,
                                      "failed to get the port number for "
                                      "remote subvolume. Please run 'gluster "
                                      "volume status' on server to see "
                                      "if brick process is running.");
                }
                conf->portmap_err_logged = 1;
                goto out;
        }

        conf->portmap_err_logged = 0;
        conf->disconnect_err_logged = 0;
        config.remote_port = rsp.port;
        rpc_clnt_reconfig (conf->rpc, &config);

        conf->skip_notify = 1;
	conf->quick_reconnect = 1;

out:
        if (frame)
                STACK_DESTROY (frame->root);

        if (conf) {
                /* Need this to connect the same transport on different port */
                /* ie, glusterd to glusterfsd */
                rpc_transport_disconnect (conf->rpc->conn.trans, _gf_false);
        }

        return ret;
}


int
client_query_portmap (xlator_t *this, struct rpc_clnt *rpc)
{
        int                      ret             = -1;
        pmap_port_by_brick_req   req             = {0,};
        call_frame_t            *fr              = NULL;
        dict_t                  *options         = NULL;
        char                    *remote_subvol   = NULL;
        char                    *xprt            = NULL;
        char                     brick_name[PATH_MAX] = {0,};

        options = this->options;

        ret = dict_get_str (options, "remote-subvolume", &remote_subvol);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, PC_MSG_VOL_SET_FAIL,
                        "remote-subvolume not set in volfile");
                goto fail;
        }

        req.brick = remote_subvol;

        if (!dict_get_str (options, "transport-type", &xprt)) {
                if (!strcmp (xprt, "rdma")) {
                        snprintf (brick_name, sizeof(brick_name), "%s.rdma",
                                  remote_subvol);
                        req.brick = brick_name;
                }
        }

        fr  = create_frame (this, this->ctx->pool);
        if (!fr) {
                ret = -1;
                goto fail;
        }

        ret = client_submit_request (this, &req, fr, &clnt_pmap_prog,
                                     GF_PMAP_PORTBYBRICK,
                                     client_query_portmap_cbk,
                                     NULL, NULL, 0, NULL, 0, NULL,
                                     (xdrproc_t)xdr_pmap_port_by_brick_req);

fail:
        return ret;
}


int
client_dump_version_cbk (struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
        gf_dump_rsp     rsp   = {0,};
        gf_prog_detail *trav  = NULL;
        gf_prog_detail *next  = NULL;
        call_frame_t   *frame = NULL;
        clnt_conf_t    *conf  = NULL;
        int             ret   = 0;

        frame = myframe;
        conf  = frame->this->private;

        if (-1 == req->rpc_status) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOTCONN,
                        PC_MSG_RPC_STATUS_ERROR, "received RPC status error");
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_dump_rsp);
        if (ret < 0) {
                gf_msg (frame->this->name, GF_LOG_ERROR, EINVAL,
                        PC_MSG_XDR_DECODING_FAILED, "XDR decoding failed");
                goto out;
        }
        if (-1 == rsp.op_ret) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        PC_MSG_VERSION_ERROR, "failed to get the 'versions' "
                        "from server");
                goto out;
        }

        if (server_has_portmap (frame->this, rsp.prog) == 0) {
                ret = client_query_portmap (frame->this, conf->rpc);
                goto out;
        }

        /* Check for the proper version string */
        /* Reply in "Name:Program-Number:Program-Version,..." format */
        ret = select_server_supported_programs (frame->this, rsp.prog);
        if (ret) {
                gf_msg (frame->this->name, GF_LOG_ERROR, 0,
                        PC_MSG_VERSION_ERROR, "server doesn't support the "
                        "version");
                goto out;
        }

        client_setvolume (frame->this, conf->rpc);

out:
        /* don't use GF_FREE, buffer was allocated by libc */
        if (rsp.prog) {
                trav = rsp.prog;
                while (trav) {
                        next = trav->next;
                        free (trav->progname);
                        free (trav);
                        trav = next;
                }
        }

        STACK_DESTROY (frame->root);

        if (ret != 0)
                rpc_transport_disconnect (conf->rpc->conn.trans, _gf_false);

        return ret;
}

int
client_handshake (xlator_t *this, struct rpc_clnt *rpc)
{
        call_frame_t *frame = NULL;
        clnt_conf_t  *conf  = NULL;
        gf_dump_req   req   = {0,};
        int           ret   = 0;

        conf = this->private;
        if (!conf->handshake) {
                gf_msg (this->name, GF_LOG_WARNING, 0, PC_MSG_PGM_NOT_FOUND,
                        "handshake program not found");
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        req.gfs_id = 0xbabe;
        ret = client_submit_request (this, &req, frame, conf->dump,
                                     GF_DUMP_DUMP, client_dump_version_cbk,
                                     NULL, NULL, 0, NULL, 0,
                                     NULL, (xdrproc_t)xdr_gf_dump_req);

out:
        return ret;
}

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = "NULL",
        [GF_HNDSK_SETVOLUME]    = "SETVOLUME",
        [GF_HNDSK_GETSPEC]      = "GETSPEC",
        [GF_HNDSK_PING]         = "PING",
        [GF_HNDSK_SET_LK_VER]   = "SET_LK_VER"
};

rpc_clnt_prog_t clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};

char *clnt_dump_proc[GF_DUMP_MAXVALUE] = {
        [GF_DUMP_NULL] = "NULL",
        [GF_DUMP_DUMP] = "DUMP",
};

rpc_clnt_prog_t clnt_dump_prog = {
        .progname  = "GF-DUMP",
        .prognum   = GLUSTER_DUMP_PROGRAM,
        .progver   = GLUSTER_DUMP_VERSION,
        .procnames = clnt_dump_proc,
};

char *clnt_pmap_procs[GF_PMAP_MAXVALUE] = {
        [GF_PMAP_PORTBYBRICK] = "PORTBYBRICK",
};

rpc_clnt_prog_t clnt_pmap_prog = {
        .progname   = "PORTMAP",
        .prognum    = GLUSTER_PMAP_PROGRAM,
        .progver    = GLUSTER_PMAP_VERSION,
        .procnames  = clnt_pmap_procs,
};
