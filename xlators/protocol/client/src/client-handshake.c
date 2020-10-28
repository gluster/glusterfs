/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/fd-lk.h>
#include "client.h"
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/statedump.h>
#include <glusterfs/compat-errno.h>

#include "glusterfs3.h"
#include "portmap-xdr.h"
#include "rpc-common-xdr.h"
#include "client-messages.h"
#include "xdr-rpc.h"

#define CLIENT_REOPEN_MAX_ATTEMPTS 1024
extern rpc_clnt_prog_t clnt3_3_fop_prog;
extern rpc_clnt_prog_t clnt4_0_fop_prog;
extern rpc_clnt_prog_t clnt_pmap_prog;

int32_t
client3_getspec(call_frame_t *frame, xlator_t *this, void *data)
{
    CLIENT_STACK_UNWIND(getspec, frame, -1, ENOSYS, NULL);
    return 0;
}

static int
client_notify_parents_child_up(xlator_t *this)
{
    clnt_conf_t *conf = NULL;
    int ret = 0;

    GF_VALIDATE_OR_GOTO("client", this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    if (conf->child_up) {
        ret = client_notify_dispatch_uniq(this, GF_EVENT_CHILD_UP, NULL);
        if (ret) {
            gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_UP_NOTIFY_FAILED,
                    NULL);
            goto out;
        }
    } else {
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_STATUS, NULL);
    }

out:
    return 0;
}

void
client_default_reopen_done(clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this)
{
    gf_log_callingfn(this->name, GF_LOG_WARNING,
                     "This function should never be called");
}

static void
client_reopen_done(clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this)
{
    clnt_conf_t *conf = this->private;
    gf_boolean_t destroy = _gf_false;

    pthread_spin_lock(&conf->fd_lock);
    {
        fdctx->remote_fd = rfd;
        fdctx->reopen_attempts = 0;
        fdctx->reopen_done = client_default_reopen_done;
        if (!fdctx->released)
            list_add_tail(&fdctx->sfd_pos, &conf->saved_fds);
        else
            destroy = _gf_true;
    }
    pthread_spin_unlock(&conf->fd_lock);

    if (destroy)
        client_fdctx_destroy(this, fdctx);
}

static void
client_child_up_reopen_done(clnt_fd_ctx_t *fdctx, int64_t rfd, xlator_t *this)
{
    clnt_conf_t *conf = this->private;
    uint64_t fd_count = 0;

    LOCK(&conf->rec_lock);
    {
        fd_count = --(conf->reopen_fd_count);
    }
    UNLOCK(&conf->rec_lock);

    client_reopen_done(fdctx, rfd, this);
    if (fd_count == 0) {
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_UP_NOTIFY, NULL);
        client_notify_parents_child_up(this);
    }
}

int
client3_3_reopen_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    int32_t ret = -1;
    gfs3_open_rsp rsp = {
        0,
    };
    call_frame_t *frame = myframe;
    xlator_t *this = frame->this;
    clnt_local_t *local = frame->local;
    clnt_fd_ctx_t *fdctx = local->fdctx;

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, ENOTCONN,
                PC_MSG_RPC_STATUS_ERROR, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = ENOTCONN;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gfs3_open_rsp);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                PC_MSG_XDR_DECODING_FAILED, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    if (rsp.op_ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, rsp.op_errno,
                PC_MSG_REOPEN_FAILED, "path=%s", local->loc.path);
    } else {
        gf_msg_debug(frame->this->name, 0,
                     "reopen on %s succeeded (remote-fd = %" PRId64 ")",
                     local->loc.path, rsp.fd);
    }

    if (rsp.op_ret == -1) {
        goto out;
    }

out:
    fdctx->reopen_done(fdctx, (rsp.op_ret) ? -1 : rsp.fd, this);

    frame->local = NULL;
    STACK_DESTROY(frame->root);

    client_local_wipe(local);

    return 0;
}

int
client3_3_reopendir_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    int32_t ret = -1;
    gfs3_open_rsp rsp = {
        0,
    };
    call_frame_t *frame = myframe;
    clnt_local_t *local = frame->local;
    clnt_fd_ctx_t *fdctx = local->fdctx;

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, ENOTCONN,
                PC_MSG_RPC_STATUS_ERROR, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = ENOTCONN;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gfs3_opendir_rsp);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                PC_MSG_XDR_DECODING_FAILED, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    if (rsp.op_ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, rsp.op_errno,
                PC_MSG_REOPEN_FAILED, "path=%s", local->loc.path, NULL);
    } else {
        gf_smsg(frame->this->name, GF_LOG_INFO, 0, PC_MSG_DIR_OP_SUCCESS,
                "path=%s", local->loc.path, "fd=%" PRId64, rsp.fd, NULL);
    }

    if (-1 == rsp.op_ret) {
        goto out;
    }

out:
    fdctx->reopen_done(fdctx, (rsp.op_ret) ? -1 : rsp.fd, frame->this);

    frame->local = NULL;
    STACK_DESTROY(frame->root);
    client_local_wipe(local);

    return 0;
}

static int
protocol_client_reopendir(clnt_fd_ctx_t *fdctx, xlator_t *this)
{
    int ret = -1;
    gfs3_opendir_req req = {
        {
            0,
        },
    };
    clnt_local_t *local = NULL;
    call_frame_t *frame = NULL;
    clnt_conf_t *conf = NULL;

    conf = this->private;

    local = mem_get0(this->local_pool);
    if (!local) {
        goto out;
    }
    local->fdctx = fdctx;

    gf_uuid_copy(local->loc.gfid, fdctx->gfid);
    ret = loc_path(&local->loc, NULL);
    if (ret < 0)
        goto out;

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        goto out;
    }

    memcpy(req.gfid, fdctx->gfid, 16);

    gf_msg_debug(frame->this->name, 0, "attempting reopen on %s",
                 local->loc.path);

    frame->local = local;

    ret = client_submit_request(this, &req, frame, conf->fops, GFS3_OP_OPENDIR,
                                client3_3_reopendir_cbk, NULL,
                                (xdrproc_t)xdr_gfs3_opendir_req);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DIR_OP_FAILED, NULL);
    }

    return 0;

out:
    if (local)
        client_local_wipe(local);

    fdctx->reopen_done(fdctx, fdctx->remote_fd, this);

    return 0;
}

static int
protocol_client_reopenfile(clnt_fd_ctx_t *fdctx, xlator_t *this)
{
    int ret = -1;
    gfs3_open_req req = {
        {
            0,
        },
    };
    clnt_local_t *local = NULL;
    call_frame_t *frame = NULL;
    clnt_conf_t *conf = NULL;

    conf = this->private;

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        goto out;
    }

    local = mem_get0(this->local_pool);
    if (!local) {
        goto out;
    }

    local->fdctx = fdctx;
    gf_uuid_copy(local->loc.gfid, fdctx->gfid);
    ret = loc_path(&local->loc, NULL);
    if (ret < 0)
        goto out;

    frame->local = local;

    memcpy(req.gfid, fdctx->gfid, 16);
    req.flags = gf_flags_from_flags(fdctx->flags);
    req.flags = req.flags & (~(O_TRUNC | O_CREAT | O_EXCL));

    gf_msg_debug(frame->this->name, 0, "attempting reopen on %s",
                 local->loc.path);

    ret = client_submit_request(this, &req, frame, conf->fops, GFS3_OP_OPEN,
                                client3_3_reopen_cbk, NULL,
                                (xdrproc_t)xdr_gfs3_open_req);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DIR_OP_FAILED, NULL);
    }

    return 0;

out:
    if (frame) {
        frame->local = NULL;
        STACK_DESTROY(frame->root);
    }

    if (local)
        client_local_wipe(local);

    fdctx->reopen_done(fdctx, fdctx->remote_fd, this);

    return 0;
}

static void
protocol_client_reopen(clnt_fd_ctx_t *fdctx, xlator_t *this)
{
    if (fdctx->is_dir)
        protocol_client_reopendir(fdctx, this);
    else
        protocol_client_reopenfile(fdctx, this);
}

/* v4.x +  */
int
client4_0_reopen_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    int32_t ret = -1;
    gfx_open_rsp rsp = {
        0,
    };
    call_frame_t *frame = myframe;
    xlator_t *this = frame->this;
    clnt_local_t *local = frame->local;
    clnt_fd_ctx_t *fdctx = local->fdctx;

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, ENOTCONN,
                PC_MSG_RPC_STATUS_ERROR, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = ENOTCONN;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gfx_open_rsp);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                PC_MSG_XDR_DECODING_FAILED, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    if (rsp.op_ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, rsp.op_errno,
                PC_MSG_REOPEN_FAILED, "path=%s", local->loc.path, NULL);
    } else {
        gf_msg_debug(frame->this->name, 0,
                     "reopen on %s succeeded (remote-fd = %" PRId64 ")",
                     local->loc.path, rsp.fd);
    }

    if (rsp.op_ret == -1) {
        goto out;
    }

out:
    fdctx->reopen_done(fdctx, (rsp.op_ret) ? -1 : rsp.fd, this);

    frame->local = NULL;
    STACK_DESTROY(frame->root);

    client_local_wipe(local);

    return 0;
}

int
client4_0_reopendir_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    int32_t ret = -1;
    gfx_open_rsp rsp = {
        0,
    };
    call_frame_t *frame = myframe;
    clnt_local_t *local = frame->local;
    clnt_fd_ctx_t *fdctx = local->fdctx;

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, ENOTCONN,
                PC_MSG_RPC_STATUS_ERROR, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = ENOTCONN;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gfx_open_rsp);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                PC_MSG_XDR_DECODING_FAILED, NULL);
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    if (rsp.op_ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, rsp.op_errno,
                PC_MSG_DIR_OP_FAILED, "dir-path=%s", local->loc.path, NULL);
    } else {
        gf_smsg(frame->this->name, GF_LOG_INFO, 0, PC_MSG_DIR_OP_SUCCESS,
                "path=%s", local->loc.path, "fd=%" PRId64, rsp.fd, NULL);
    }

    if (-1 == rsp.op_ret) {
        goto out;
    }

out:
    fdctx->reopen_done(fdctx, (rsp.op_ret) ? -1 : rsp.fd, frame->this);

    frame->local = NULL;
    STACK_DESTROY(frame->root);
    client_local_wipe(local);

    return 0;
}

static int
protocol_client_reopendir_v2(clnt_fd_ctx_t *fdctx, xlator_t *this)
{
    int ret = -1;
    gfx_opendir_req req = {
        {
            0,
        },
    };
    call_frame_t *frame = NULL;
    clnt_conf_t *conf = this->private;
    clnt_local_t *local = mem_get0(this->local_pool);

    if (!local) {
        ret = -1;
        goto out;
    }
    local->fdctx = fdctx;

    gf_uuid_copy(local->loc.gfid, fdctx->gfid);
    ret = loc_path(&local->loc, NULL);
    if (ret < 0)
        goto out;

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        ret = -1;
        goto out;
    }

    memcpy(req.gfid, fdctx->gfid, 16);

    gf_msg_debug(frame->this->name, 0, "attempting reopen on %s",
                 local->loc.path);

    frame->local = local;

    ret = client_submit_request(this, &req, frame, conf->fops, GFS3_OP_OPENDIR,
                                client4_0_reopendir_cbk, NULL,
                                (xdrproc_t)xdr_gfx_opendir_req);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DIR_OP_FAILED, NULL);
    }

    return 0;

out:
    if (local)
        client_local_wipe(local);

    fdctx->reopen_done(fdctx, fdctx->remote_fd, this);

    return 0;
}

static int
protocol_client_reopenfile_v2(clnt_fd_ctx_t *fdctx, xlator_t *this)
{
    int ret = -1;
    gfx_open_req req = {
        {
            0,
        },
    };
    clnt_local_t *local = NULL;
    clnt_conf_t *conf = this->private;
    call_frame_t *frame = create_frame(this, this->ctx->pool);

    if (!frame) {
        ret = -1;
        goto out;
    }

    local = mem_get0(this->local_pool);
    if (!local) {
        ret = -1;
        goto out;
    }

    local->fdctx = fdctx;
    gf_uuid_copy(local->loc.gfid, fdctx->gfid);
    ret = loc_path(&local->loc, NULL);
    if (ret < 0)
        goto out;

    frame->local = local;

    memcpy(req.gfid, fdctx->gfid, 16);
    req.flags = gf_flags_from_flags(fdctx->flags);
    req.flags = req.flags & (~(O_TRUNC | O_CREAT | O_EXCL));

    gf_msg_debug(frame->this->name, 0, "attempting reopen on %s",
                 local->loc.path);

    ret = client_submit_request(this, &req, frame, conf->fops, GFS3_OP_OPEN,
                                client4_0_reopen_cbk, NULL,
                                (xdrproc_t)xdr_gfx_open_req);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DIR_OP_FAILED, NULL);
    }

    return 0;

out:
    if (frame) {
        frame->local = NULL;
        STACK_DESTROY(frame->root);
    }

    if (local)
        client_local_wipe(local);

    fdctx->reopen_done(fdctx, fdctx->remote_fd, this);

    return 0;
}

static void
protocol_client_reopen_v2(clnt_fd_ctx_t *fdctx, xlator_t *this)
{
    if (fdctx->is_dir)
        protocol_client_reopendir_v2(fdctx, this);
    else
        protocol_client_reopenfile_v2(fdctx, this);
}

gf_boolean_t
__is_fd_reopen_in_progress(clnt_fd_ctx_t *fdctx)
{
    if (fdctx->reopen_done == client_default_reopen_done)
        return _gf_false;
    return _gf_true;
}

void
client_attempt_reopen(fd_t *fd, xlator_t *this)
{
    if (!fd || !this)
        goto out;

    clnt_conf_t *conf = this->private;
    clnt_fd_ctx_t *fdctx = NULL;
    gf_boolean_t reopen = _gf_false;

    pthread_spin_lock(&conf->fd_lock);
    {
        fdctx = this_fd_get_ctx(fd, this);
        if (!fdctx) {
            pthread_spin_unlock(&conf->fd_lock);
            goto out;
        }

        if (__is_fd_reopen_in_progress(fdctx))
            goto unlock;
        if (fdctx->remote_fd != -1)
            goto unlock;

        if (fdctx->reopen_attempts == CLIENT_REOPEN_MAX_ATTEMPTS) {
            reopen = _gf_true;
            fdctx->reopen_done = client_reopen_done;
            list_del_init(&fdctx->sfd_pos);
        } else {
            fdctx->reopen_attempts++;
        }
    }
unlock:
    pthread_spin_unlock(&conf->fd_lock);
    if (reopen) {
        if (conf->fops->progver == GLUSTER_FOP_VERSION_v2)
            protocol_client_reopen_v2(fdctx, this);
        else
            protocol_client_reopen(fdctx, this);
    }
out:
    return;
}

static int
client_post_handshake(call_frame_t *frame, xlator_t *this)
{
    clnt_conf_t *conf = NULL;
    clnt_fd_ctx_t *tmp = NULL;
    clnt_fd_ctx_t *fdctx = NULL;
    struct list_head reopen_head;

    int count = 0;

    if (!this || !this->private)
        goto out;

    conf = this->private;
    INIT_LIST_HEAD(&reopen_head);

    pthread_spin_lock(&conf->fd_lock);
    {
        list_for_each_entry_safe(fdctx, tmp, &conf->saved_fds, sfd_pos)
        {
            if (fdctx->remote_fd != -1 ||
                (!list_empty(&fdctx->lock_list) && conf->strict_locks))
                continue;

            fdctx->reopen_done = client_child_up_reopen_done;
            list_del_init(&fdctx->sfd_pos);
            list_add_tail(&fdctx->sfd_pos, &reopen_head);
            count++;
        }
    }
    pthread_spin_unlock(&conf->fd_lock);

    /* Delay notifying CHILD_UP to parents
       until all locks are recovered */
    if (count > 0) {
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_UP_NOTIFY_DELAY,
                "count=%d", count, NULL);
        client_save_number_fds(conf, count);

        list_for_each_entry_safe(fdctx, tmp, &reopen_head, sfd_pos)
        {
            list_del_init(&fdctx->sfd_pos);

            if (conf->fops->progver == GLUSTER_FOP_VERSION_v2)
                protocol_client_reopen_v2(fdctx, this);
            else
                protocol_client_reopen(fdctx, this);
        }
    } else {
        gf_msg_debug(this->name, 0,
                     "No fds to open - notifying all parents child "
                     "up");
        client_notify_parents_child_up(this);
    }
out:
    return 0;
}

int
client_setvolume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    call_frame_t *frame = myframe;
    xlator_t *this = frame->this;
    clnt_conf_t *conf = this->private;
    dict_t *reply = NULL;
    char *process_uuid = NULL;
    char *volume_id = NULL;
    char *remote_error = NULL;
    char *remote_subvol = NULL;
    gf_setvolume_rsp rsp = {
        0,
    };
    int ret = 0;
    int32_t op_ret = 0;
    int32_t op_errno = 0;
    gf_boolean_t auth_fail = _gf_false;
    glusterfs_ctx_t *ctx = NULL;

    GF_VALIDATE_OR_GOTO(this->name, conf, out);
    ctx = this->ctx;
    GF_VALIDATE_OR_GOTO(this->name, ctx, out);

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, ENOTCONN,
                PC_MSG_RPC_STATUS_ERROR, NULL);
        op_ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_setvolume_rsp);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, EINVAL, PC_MSG_XDR_DECODING_FAILED,
                NULL);
        op_ret = -1;
        goto out;
    }
    op_ret = rsp.op_ret;
    op_errno = gf_error_to_errno(rsp.op_errno);
    if (-1 == rsp.op_ret) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, op_errno,
                PC_MSG_VOL_SET_FAIL, NULL);
    }

    reply = dict_new();
    if (!reply)
        goto out;

    if (rsp.dict.dict_len) {
        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &reply);
        if (ret < 0) {
            gf_smsg(frame->this->name, GF_LOG_WARNING, 0,
                    PC_MSG_DICT_UNSERIALIZE_FAIL, NULL);
            goto out;
        }
    }

    ret = dict_get_str_sizen(reply, "ERROR", &remote_error);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PC_MSG_DICT_GET_FAILED,
                "ERROR string", NULL);
    }

    ret = dict_get_str_sizen(reply, "process-uuid", &process_uuid);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PC_MSG_DICT_GET_FAILED,
                "process-uuid", NULL);
    }

    if (op_ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, PC_MSG_SETVOLUME_FAIL,
                "remote-error=%s", remote_error, NULL);

        errno = op_errno;
        if (remote_error && (op_errno == EACCES)) {
            auth_fail = _gf_true;
            op_ret = 0;
        }
        if ((op_errno == ENOENT) && this->ctx->cmd_args.subdir_mount &&
            (ctx->graph_id <= 1)) {
            /* A case of subdir not being present at the moment,
               ride on auth_fail framework to notify the error */
            /* Make sure this case is handled only in the new
               graph, so mount may fail in this case. In case
               of 'add-brick' etc, we need to continue retry */
            auth_fail = _gf_true;
            op_ret = 0;
        }
        if (op_errno == ESTALE) {
            ret = client_notify_dispatch(this, GF_EVENT_VOLFILE_MODIFIED, NULL);
            if (ret)
                gf_smsg(this->name, GF_LOG_INFO, 0,
                        PC_MSG_VOLFILE_NOTIFY_FAILED, NULL);
        }
        goto out;
    }

    ret = dict_get_str_sizen(this->options, "remote-subvolume", &remote_subvol);
    if (ret || !remote_subvol) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FIND_KEY_FAILED,
                "remote-subvolume", NULL);
        goto out;
    }

    ret = dict_get_str_sizen(reply, "volume-id", &volume_id);
    if (ret < 0) {
        /* this can happen if the server is of old version, so treat it as
           just debug message */
        gf_msg_debug(this->name, EINVAL,
                     "failed to get 'volume-id' from reply dict");
    } else if (ctx->primary && strncmp("snapd", remote_subvol, 5)) {
        /* TODO: if it is a fuse mount or a snapshot enabled client, don't
           bother */
        /* If any value is set, the first element will be non-0.
           It would be '0', but not '\0' :-) */
        if (ctx->volume_id[0]) {
            if (strcmp(ctx->volume_id, volume_id)) {
                /* Ideally it shouldn't even come here, as server itself
                   should fail the handshake in that case */
                gf_smsg(this->name, GF_LOG_ERROR, EINVAL, PC_MSG_VOL_ID_CHANGED,
                        "vol-id=%s", volume_id, "ctx->vol-id=%s",
                        ctx->volume_id, NULL);
                op_ret = -1;
                goto out;
            }
        } else {
            strncpy(ctx->volume_id, volume_id, GF_UUID_BUF_SIZE);
        }
    }

    uint32_t child_up_int;
    ret = dict_get_uint32(reply, "child_up", &child_up_int);
    if (ret) {
        /*
         * This would happen in cases where the server trying to     *
         * connect to this client is running an older version. Hence *
         * setting the child_up to _gf_true in this case.            *
         */
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FIND_KEY_FAILED,
                "child_up", NULL);
        conf->child_up = _gf_true;
    } else {
        conf->child_up = (child_up_int != 0);
    }

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

    gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_REMOTE_VOL_CONNECTED,
            "conn-name=%s", conf->rpc->conn.name, "remote_subvol=%s",
            remote_subvol, NULL);

    op_ret = 0;
    conf->connected = 1;

    client_post_handshake(frame, frame->this);
out:
    if (auth_fail) {
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_AUTH_FAILED, NULL);
        ret = client_notify_dispatch(this, GF_EVENT_AUTH_FAILED, NULL);
        if (ret)
            gf_smsg(this->name, GF_LOG_INFO, 0,
                    PC_MSG_AUTH_FAILED_NOTIFY_FAILED, NULL);
        conf->connected = 0;
        ret = -1;
    }
    if (-1 == op_ret) {
        /* Let the connection/re-connection happen in
         * background, for now, don't hang here,
         * tell the parents that i am all ok..
         */
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_CHILD_CONNECTING_EVENT,
                NULL);
        ret = client_notify_dispatch(this, GF_EVENT_CHILD_CONNECTING, NULL);
        if (ret)
            gf_smsg(this->name, GF_LOG_INFO, 0,
                    PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED, NULL);
        /*
         * The reconnection *won't* happen in the background (see
         * previous comment) unless we kill the current connection.
         */
        rpc_transport_disconnect(conf->rpc->conn.trans, _gf_false);
        ret = 0;
    }

    free(rsp.dict.dict_val);

    STACK_DESTROY(frame->root);

    if (reply)
        dict_unref(reply);

    return ret;
}

int
client_setvolume(xlator_t *this, struct rpc_clnt *rpc)
{
    int ret = 0;
    gf_setvolume_req req = {
        {
            0,
        },
    };
    call_frame_t *fr = NULL;
    char *process_uuid_xl = NULL;
    char *remote_subvol = NULL;
    clnt_conf_t *conf = this->private;
    dict_t *options = this->options;
    char counter_str[32] = {0};
    char hostname[256] = {
        0,
    };

    if (conf->fops) {
        ret = dict_set_int32_sizen(options, "fops-version",
                                   conf->fops->prognum);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SET_FAILED,
                    "version-fops=%d", conf->fops->prognum, NULL);
            goto fail;
        }
    }

    if (conf->mgmt) {
        ret = dict_set_int32_sizen(options, "mgmt-version",
                                   conf->mgmt->prognum);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SET_FAILED,
                    "version-mgmt=%d", conf->mgmt->prognum, NULL);
            goto fail;
        }
    }

    /*
     * Connection-id should always be unique so that server never gets to
     * reuse the previous connection resources so it cleans up the resources
     * on every disconnect. Otherwise it may lead to stale resources, i.e.
     * leaked file descriptors, inode/entry locks
     */

    snprintf(counter_str, sizeof(counter_str), "-%" PRIu64, conf->setvol_count);
    conf->setvol_count++;

    if (gethostname(hostname, 256) == -1) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, PC_MSG_GETHOSTNAME_FAILED,
                NULL);

        goto fail;
    }

    ret = gf_asprintf(&process_uuid_xl, GLUSTER_PROCESS_UUID_FMT,
                      this->ctx->process_uuid, this->graph->id, getpid(),
                      hostname, this->name, counter_str);
    if (-1 == ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_PROCESS_UUID_SET_FAIL,
                NULL);
        goto fail;
    }

    ret = dict_set_dynstr_sizen(options, "process-uuid", process_uuid_xl);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SET_FAILED,
                "process-uuid=%s", process_uuid_xl, NULL);
        goto fail;
    }

    if (this->ctx->cmd_args.process_name) {
        ret = dict_set_str_sizen(options, "process-name",
                                 this->ctx->cmd_args.process_name);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_DICT_SET_FAILED,
                    "process-name", NULL);
        }
    }

    ret = dict_set_str_sizen(options, "client-version", PACKAGE_VERSION);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_SET_FAILED,
                "client-version=%s", PACKAGE_VERSION, NULL);
    }

    ret = dict_get_str_sizen(this->options, "remote-subvolume", &remote_subvol);
    if (ret || !remote_subvol) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FIND_KEY_FAILED,
                "remote-subvolume", NULL);
        goto fail;
    }

    /* volume-id to be sent only for regular volume, not snap volume */
    if (strncmp("snapd", remote_subvol, 5)) {
        /* If any value is set, the first element will be non-0.
           It would be '0', but not '\0' :-) */
        if (!this->ctx->volume_id[0]) {
            strncpy(this->ctx->volume_id, this->graph->volume_id,
                    GF_UUID_BUF_SIZE);
        }
        if (this->ctx->volume_id[0]) {
            ret = dict_set_str(options, "volume-id", this->ctx->volume_id);
            if (ret < 0) {
                gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_DICT_SET_FAILED,
                        "volume-id", NULL);
            }
        }
    }

    if (this->ctx->cmd_args.volfile_server) {
        if (this->ctx->cmd_args.volfile_id) {
            ret = dict_set_str_sizen(options, "volfile-key",
                                     this->ctx->cmd_args.volfile_id);
            if (ret)
                gf_smsg(this->name, GF_LOG_ERROR, 0,
                        PC_MSG_VOLFILE_KEY_SET_FAILED, NULL);
        }
        ret = dict_set_uint32(options, "volfile-checksum",
                              this->graph->volfile_checksum);
        if (ret)
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_VOLFILE_CHECKSUM_FAILED,
                    NULL);
    }

    if (this->ctx->cmd_args.subdir_mount) {
        ret = dict_set_str_sizen(options, "subdir-mount",
                                 this->ctx->cmd_args.subdir_mount);
        if (ret) {
            gf_log(THIS->name, GF_LOG_ERROR, "Failed to set subdir_mount");
            /* It makes sense to fail, as per the CLI, we
               should be doing a subdir_mount */
            goto fail;
        }
    }

    /* Insert a dummy key value pair to avoid failure at server side for
     * clnt-lk-version with new clients.
     */
    ret = dict_set_uint32(options, "clnt-lk-version", 1);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_DICT_SET_FAILED,
                "clnt-lk-version(1)", NULL);
    }

    ret = dict_set_int32_sizen(options, "opversion", GD_OP_VERSION_MAX);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SET_FAILED,
                "client opversion", NULL);
    }

    ret = dict_allocate_and_serialize(options, (char **)&req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret != 0) {
        ret = -1;
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_DICT_SERIALIZE_FAIL, NULL);
        goto fail;
    }

    fr = create_frame(this, this->ctx->pool);
    if (!fr)
        goto fail;

    ret = client_submit_request(this, &req, fr, conf->handshake,
                                GF_HNDSK_SETVOLUME, client_setvolume_cbk, NULL,
                                (xdrproc_t)xdr_gf_setvolume_req);

fail:
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int
select_server_supported_programs(xlator_t *this, gf_prog_detail *prog)
{
    gf_prog_detail *trav = NULL;
    clnt_conf_t *conf = NULL;
    int ret = -1;

    if (!this || !prog) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, PC_MSG_PGM_NOT_FOUND, NULL);
        goto out;
    }

    conf = this->private;
    trav = prog;

    while (trav) {
        /* Select 'programs' */
        if ((clnt3_3_fop_prog.prognum == trav->prognum) &&
            (clnt3_3_fop_prog.progver == trav->progver)) {
            conf->fops = &clnt3_3_fop_prog;
            if (conf->rpc)
                conf->rpc->auth_value = AUTH_GLUSTERFS_v2;
            ret = 0;
            /* In normal flow, we don't want to use old protocol type.
               but if it is for testing, lets use it */
            if (conf->old_protocol)
                goto done;
        }

        if ((clnt4_0_fop_prog.prognum == trav->prognum) &&
            (clnt4_0_fop_prog.progver == trav->progver)) {
            conf->fops = &clnt4_0_fop_prog;
            if (conf->rpc)
                conf->rpc->auth_value = AUTH_GLUSTERFS_v3;
            ret = 0;
            /* this is latest program, lets use this program only */
            /* if we are testing for old-protocol, lets not break this */
            if (!conf->old_protocol)
                goto done;
        }

        if (ret) {
            gf_msg_debug(this->name, 0, "%s (%" PRId64 ") not supported",
                         trav->progname, trav->progver);
        }
        trav = trav->next;
    }

done:
    if (!ret)
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_VERSION_INFO,
                "Program-name=%s", conf->fops->progname, "Num=%d",
                conf->fops->prognum, "Version=%d", conf->fops->progver, NULL);

out:
    return ret;
}

int
server_has_portmap(xlator_t *this, gf_prog_detail *prog)
{
    gf_prog_detail *trav = NULL;
    int ret = -1;

    if (!this || !prog) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, PC_MSG_PGM_NOT_FOUND, NULL);
        goto out;
    }

    trav = prog;

    while (trav) {
        if ((trav->prognum == GLUSTER_PMAP_PROGRAM) &&
            (trav->progver == GLUSTER_PMAP_VERSION)) {
            gf_msg_debug(this->name, 0, "detected portmapper on server");
            ret = 0;
            break;
        }
        trav = trav->next;
    }

out:
    return ret;
}

int
client_query_portmap_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    struct pmap_port_by_brick_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    clnt_conf_t *conf = NULL;
    int ret = -1;
    struct rpc_clnt_config config = {
        0,
    };
    xlator_t *this = NULL;

    frame = myframe;
    if (!frame || !frame->this || !frame->this->private) {
        gf_smsg(THIS->name, GF_LOG_WARNING, EINVAL, PC_MSG_FRAME_NOT_FOUND,
                NULL);
        goto out;
    }
    this = frame->this;
    conf = frame->this->private;

    if (-1 == req->rpc_status) {
        gf_smsg(this->name, GF_LOG_WARNING, ENOTCONN, PC_MSG_RPC_STATUS_ERROR,
                NULL);
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_pmap_port_by_brick_rsp);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, EINVAL, PC_MSG_XDR_DECODING_FAILED,
                NULL);
        goto out;
    }

    if (-1 == rsp.op_ret) {
        ret = -1;
        if (!conf->portmap_err_logged) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_PORT_NUM_ERROR, NULL);
        } else {
            gf_msg_debug(this->name, 0,
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
    rpc_clnt_reconfig(conf->rpc, &config);

    conf->skip_notify = 1;
    conf->quick_reconnect = 1;

out:
    if (frame)
        STACK_DESTROY(frame->root);

    if (conf) {
        /* Need this to connect the same transport on different port */
        /* ie, glusterd to glusterfsd */
        rpc_transport_disconnect(conf->rpc->conn.trans, _gf_false);
    }

    return ret;
}

int
client_query_portmap(xlator_t *this, struct rpc_clnt *rpc)
{
    int ret = -1;
    pmap_port_by_brick_req req = {
        0,
    };
    call_frame_t *fr = NULL;
    dict_t *options = NULL;
    char *remote_subvol = NULL;
    char *xprt = NULL;
    char brick_name[PATH_MAX] = {
        0,
    };

    options = this->options;

    ret = dict_get_str_sizen(options, "remote-subvolume", &remote_subvol);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_REMOTE_SUBVOL_SET_FAIL,
                NULL);
        goto fail;
    }

    req.brick = remote_subvol;

    if (!dict_get_str_sizen(options, "transport-type", &xprt)) {
        if (!strcmp(xprt, "rdma")) {
            snprintf(brick_name, sizeof(brick_name), "%s.rdma", remote_subvol);
            req.brick = brick_name;
        }
    }

    fr = create_frame(this, this->ctx->pool);
    if (!fr) {
        ret = -1;
        goto fail;
    }

    ret = client_submit_request(this, &req, fr, &clnt_pmap_prog,
                                GF_PMAP_PORTBYBRICK, client_query_portmap_cbk,
                                NULL, (xdrproc_t)xdr_pmap_port_by_brick_req);

fail:
    return ret;
}

static int
client_dump_version_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    gf_dump_rsp rsp = {
        0,
    };
    gf_prog_detail *trav = NULL;
    gf_prog_detail *next = NULL;
    call_frame_t *frame = NULL;
    clnt_conf_t *conf = NULL;
    int ret = 0;

    frame = myframe;
    conf = frame->this->private;

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, ENOTCONN,
                PC_MSG_RPC_STATUS_ERROR, NULL);
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_dump_rsp);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                PC_MSG_XDR_DECODING_FAILED, NULL);
        goto out;
    }
    if (-1 == rsp.op_ret) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, 0, PC_MSG_VERSION_ERROR,
                NULL);
        goto out;
    }

    if (server_has_portmap(frame->this, rsp.prog) == 0) {
        ret = client_query_portmap(frame->this, conf->rpc);
        goto out;
    }

    /* Check for the proper version string */
    /* Reply in "Name:Program-Number:Program-Version,..." format */
    ret = select_server_supported_programs(frame->this, rsp.prog);
    if (ret) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, 0, PC_MSG_VERSION_ERROR, NULL);
        goto out;
    }

    client_setvolume(frame->this, conf->rpc);

out:
    /* don't use GF_FREE, buffer was allocated by libc */
    if (rsp.prog) {
        trav = rsp.prog;
        while (trav) {
            next = trav->next;
            free(trav->progname);
            free(trav);
            trav = next;
        }
    }

    STACK_DESTROY(frame->root);

    if (ret != 0)
        rpc_transport_disconnect(conf->rpc->conn.trans, _gf_false);

    return ret;
}

int
client_handshake(xlator_t *this, struct rpc_clnt *rpc)
{
    call_frame_t *frame = NULL;
    clnt_conf_t *conf = NULL;
    gf_dump_req req = {
        0,
    };
    int ret = 0;

    conf = this->private;
    if (!conf->handshake) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_HANDSHAKE_PGM_NOT_FOUND,
                NULL);
        goto out;
    }

    frame = create_frame(this, this->ctx->pool);
    if (!frame)
        goto out;

    req.gfs_id = 0xbabe;
    ret = client_submit_request(this, &req, frame, conf->dump, GF_DUMP_DUMP,
                                client_dump_version_cbk, NULL,
                                (xdrproc_t)xdr_gf_dump_req);

out:
    return ret;
}

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
    [GF_HNDSK_NULL] = "NULL",
    [GF_HNDSK_SETVOLUME] = "SETVOLUME",
    [GF_HNDSK_GETSPEC] = "GETSPEC",
    [GF_HNDSK_PING] = "PING",
};

rpc_clnt_prog_t clnt_handshake_prog = {
    .progname = "GlusterFS Handshake",
    .prognum = GLUSTER_HNDSK_PROGRAM,
    .progver = GLUSTER_HNDSK_VERSION,
    .procnames = clnt_handshake_procs,
};

char *clnt_dump_proc[GF_DUMP_MAXVALUE] = {
    [GF_DUMP_NULL] = "NULL",
    [GF_DUMP_DUMP] = "DUMP",
};

rpc_clnt_prog_t clnt_dump_prog = {
    .progname = "GF-DUMP",
    .prognum = GLUSTER_DUMP_PROGRAM,
    .progver = GLUSTER_DUMP_VERSION,
    .procnames = clnt_dump_proc,
};

char *clnt_pmap_procs[GF_PMAP_MAXVALUE] = {
    [GF_PMAP_PORTBYBRICK] = "PORTBYBRICK",
};

rpc_clnt_prog_t clnt_pmap_prog = {
    .progname = "PORTMAP",
    .prognum = GLUSTER_PMAP_PROGRAM,
    .progver = GLUSTER_PMAP_VERSION,
    .procnames = clnt_pmap_procs,
};
