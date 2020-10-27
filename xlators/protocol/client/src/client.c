/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "client.h"
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/statedump.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/gf-event.h>

#include "xdr-rpc.h"
#include "glusterfs3.h"
#include "client-messages.h"

extern rpc_clnt_prog_t clnt_handshake_prog;
extern rpc_clnt_prog_t clnt_dump_prog;
extern struct rpcclnt_cb_program gluster_cbk_prog;

int
client_handshake(xlator_t *this, struct rpc_clnt *rpc);
static int
client_destroy_rpc(xlator_t *this);

static void
client_filter_o_direct(clnt_conf_t *conf, int32_t *flags)
{
    if (conf->filter_o_direct)
        *flags = (*flags & ~O_DIRECT);
}

static int
client_fini_complete(xlator_t *this)
{
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    clnt_conf_t *conf = this->private;
    if (!conf->destroy)
        return 0;

    pthread_mutex_lock(&conf->lock);
    {
        conf->fini_completed = _gf_true;
        pthread_cond_broadcast(&conf->fini_complete_cond);
    }
    pthread_mutex_unlock(&conf->lock);

out:
    return 0;
}

static int
client_is_last_child_down(xlator_t *this, int32_t event, struct rpc_clnt *rpc)
{
    rpc_clnt_connection_t *conn = NULL;
    clnt_conf_t *conf = NULL;
    int ret = 0;

    if (!this || !rpc)
        goto out;

    conf = this->private;
    if (!conf)
        goto out;

    if (!conf->parent_down)
        goto out;

    if (event != GF_EVENT_CHILD_DOWN)
        goto out;

    conn = &rpc->conn;
    pthread_mutex_lock(&conn->lock);
    {
        if (!conn->reconnect && rpc->disabled) {
            ret = 1;
        }
    }
    pthread_mutex_unlock(&conn->lock);
out:
    return ret;
}

int
client_notify_dispatch_uniq(xlator_t *this, int32_t event, void *data, ...)
{
    clnt_conf_t *conf = this->private;
    glusterfs_ctx_t *ctx = this->ctx;
    glusterfs_graph_t *graph = this->graph;

    pthread_mutex_lock(&ctx->notify_lock);
    {
        while (ctx->notifying)
            pthread_cond_wait(&ctx->notify_cond, &ctx->notify_lock);

        if (client_is_last_child_down(this, event, data) && graph) {
            pthread_mutex_lock(&graph->mutex);
            {
                graph->parent_down++;
                if (graph->parent_down == graph_total_client_xlator(graph)) {
                    graph->used = 0;
                    pthread_cond_broadcast(&graph->child_down_cond);
                }
            }
            pthread_mutex_unlock(&graph->mutex);
        }
    }
    pthread_mutex_unlock(&ctx->notify_lock);

    if (conf->last_sent_event == event)
        return 0;

    return client_notify_dispatch(this, event, data);

    /* Please avoid any code that access xlator object here
     * Because for a child down event, once we do the signal
     * we will start cleanup.
     */
}

int
client_notify_dispatch(xlator_t *this, int32_t event, void *data, ...)
{
    int ret = -1;
    glusterfs_ctx_t *ctx = this->ctx;

    clnt_conf_t *conf = this->private;

    pthread_mutex_lock(&ctx->notify_lock);
    {
        while (ctx->notifying)
            pthread_cond_wait(&ctx->notify_cond, &ctx->notify_lock);
        ctx->notifying = 1;
    }
    pthread_mutex_unlock(&ctx->notify_lock);

    /* We assume that all translators in the graph handle notification
     * events in sequence.
     * */

    ret = default_notify(this, event, data);

    /* NB (Even) with MT-epoll and EPOLLET|EPOLLONESHOT we are guaranteed
     * that there would be atmost one poller thread executing this
     * notification function. This allows us to update last_sent_event
     * without explicit synchronization. See epoll(7).
     */
    conf->last_sent_event = event;

    pthread_mutex_lock(&ctx->notify_lock);
    {
        ctx->notifying = 0;
        pthread_cond_signal(&ctx->notify_cond);
    }
    pthread_mutex_unlock(&ctx->notify_lock);

    /* Please avoid any code that access xlator object here
     * Because for a child down event, once we do the signal
     * we will start cleanup.
     */

    return ret;
}

int
client_submit_request(xlator_t *this, void *req, call_frame_t *frame,
                      rpc_clnt_prog_t *prog, int procnum, fop_cbk_fn_t cbkfn,
                      client_payload_t *cp, xdrproc_t xdrproc)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    struct iovec iov = {
        0,
    };
    struct iobuf *iobuf = NULL;
    int count = 0;
    struct iobref *new_iobref = NULL;
    ssize_t xdr_size = 0;
    struct rpc_req rpcreq = {
        0,
    };

    GF_VALIDATE_OR_GOTO("client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, prog, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);

    conf = this->private;

    /* If 'setvolume' is not successful, we should not send frames to
       server, mean time we should be able to send 'DUMP' and 'SETVOLUME'
       call itself even if its not connected */
    if (!(conf->connected || ((prog->prognum == GLUSTER_DUMP_PROGRAM) ||
                              (prog->prognum == GLUSTER_PMAP_PROGRAM) ||
                              ((prog->prognum == GLUSTER_HNDSK_PROGRAM) &&
                               (procnum == GF_HNDSK_SETVOLUME))))) {
        /* This particular error captured/logged in
           functions calling this */
        gf_msg_debug(this->name, 0, "connection in disconnected state");
        goto out;
    }

    if (req && xdrproc) {
        xdr_size = xdr_sizeof(xdrproc, req);
        iobuf = iobuf_get2(this->ctx->iobuf_pool, xdr_size);
        if (!iobuf) {
            goto out;
        }

        new_iobref = iobref_new();
        if (!new_iobref) {
            goto out;
        }

        if (cp && cp->iobref != NULL) {
            ret = iobref_merge(new_iobref, cp->iobref);
            if (ret != 0) {
                gf_smsg(this->name, GF_LOG_WARNING, ENOMEM,
                        PC_MSG_MERGE_IOBREF_FAILED, NULL);
            }
        }

        ret = iobref_add(new_iobref, iobuf);
        if (ret != 0) {
            gf_smsg(this->name, GF_LOG_WARNING, ENOMEM, PC_MSG_ADD_IOBUF_FAILED,
                    NULL);
            goto out;
        }

        iov.iov_base = iobuf->ptr;
        iov.iov_len = iobuf_size(iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic(iov, req, xdrproc);
        if (ret == -1) {
            /* callingfn so that, we can get to know which xdr
               function was called */
            gf_log_callingfn(this->name, GF_LOG_WARNING,
                             "XDR payload creation failed");
            goto out;
        }
        iov.iov_len = ret;
        count = 1;
    }

    /* do not send all groups if they are resolved server-side */
    if (!conf->send_gids) {
        if (frame->root->ngrps <= SMALL_GROUP_COUNT) {
            frame->root->groups_small[0] = frame->root->gid;
            frame->root->groups = frame->root->groups_small;
        }
        frame->root->ngrps = 1;
    }

    /* Send the msg */
    if (cp) {
        ret = rpc_clnt_submit(conf->rpc, prog, procnum, cbkfn, &iov, count,
                              cp->payload, cp->payload_cnt, new_iobref, frame,
                              cp->rsphdr, cp->rsphdr_cnt, cp->rsp_payload,
                              cp->rsp_payload_cnt, cp->rsp_iobref);
    } else {
        ret = rpc_clnt_submit(conf->rpc, prog, procnum, cbkfn, &iov, count,
                              NULL, 0, new_iobref, frame, NULL, 0, NULL, 0,
                              NULL);
    }

    if (ret < 0) {
        gf_msg_debug(this->name, 0, "rpc_clnt_submit failed");
    }

    ret = 0;

    if (new_iobref)
        iobref_unref(new_iobref);

    if (iobuf)
        iobuf_unref(iobuf);

    return ret;

out:
    rpcreq.rpc_status = -1;

    cbkfn(&rpcreq, NULL, 0, frame);

    if (new_iobref)
        iobref_unref(new_iobref);

    if (iobuf)
        iobuf_unref(iobuf);

    return ret;
}

static int32_t
client_forget(xlator_t *this, inode_t *inode)
{
    /* Nothing here */
    return 0;
}

static int32_t
client_releasedir(xlator_t *this, fd_t *fd)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_RELEASEDIR];
    if (proc->fn) {
        args.fd = fd;
        ret = proc->fn(NULL, this, &args);
    }
out:
    if (ret)
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_RELEASE_DIR_OP_FAILED,
                NULL);
    return 0;
}

static int32_t
client_release(xlator_t *this, fd_t *fd)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_RELEASE];
    if (proc->fn) {
        args.fd = fd;
        ret = proc->fn(NULL, this, &args);
    }
out:
    if (ret)
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FILE_OP_FAILED, NULL);
    return 0;
}

static int32_t
client_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_LOOKUP];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    /* think of avoiding a missing frame */
    if (ret)
        STACK_UNWIND_STRICT(lookup, frame, -1, ENOTCONN, NULL, NULL, NULL,
                            NULL);

    return 0;
}

static int32_t
client_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_STAT];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(stat, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_TRUNCATE];
    if (proc->fn) {
        args.loc = loc;
        args.offset = offset;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(truncate, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FTRUNCATE];
    if (proc->fn) {
        args.fd = fd;
        args.offset = offset;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(ftruncate, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
              dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_ACCESS];
    if (proc->fn) {
        args.loc = loc;
        args.mask = mask;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(access, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_READLINK];
    if (proc->fn) {
        args.loc = loc;
        args.size = size;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(readlink, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int
client_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dev_t rdev, mode_t umask, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_MKNOD];
    if (proc->fn) {
        args.loc = loc;
        args.mode = mode;
        args.rdev = rdev;
        args.umask = umask;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(mknod, frame, -1, ENOTCONN, NULL, NULL, NULL, NULL,
                            NULL);

    return 0;
}

static int
client_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_MKDIR];
    if (proc->fn) {
        args.loc = loc;
        args.mode = mode;
        args.umask = umask;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(mkdir, frame, -1, ENOTCONN, NULL, NULL, NULL, NULL,
                            NULL);

    return 0;
}

static int32_t
client_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_UNLINK];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        args.flags = xflag;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(unlink, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
             dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_RMDIR];
    if (proc->fn) {
        args.loc = loc;
        args.flags = flags;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    /* think of avoiding a missing frame */
    if (ret)
        STACK_UNWIND_STRICT(rmdir, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int
client_symlink(call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc, mode_t umask, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_SYMLINK];
    if (proc->fn) {
        args.linkname = linkpath;
        args.loc = loc;
        args.umask = umask;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(symlink, frame, -1, ENOTCONN, NULL, NULL, NULL,
                            NULL, NULL);

    return 0;
}

static int32_t
client_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_RENAME];
    if (proc->fn) {
        args.oldloc = oldloc;
        args.newloc = newloc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(rename, frame, -1, ENOTCONN, NULL, NULL, NULL, NULL,
                            NULL, NULL);

    return 0;
}

static int32_t
client_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_LINK];
    if (proc->fn) {
        args.oldloc = oldloc;
        args.newloc = newloc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(link, frame, -1, ENOTCONN, NULL, NULL, NULL, NULL,
                            NULL);

    return 0;
}

static int32_t
client_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_CREATE];
    if (proc->fn) {
        args.loc = loc;
        args.mode = mode;
        args.fd = fd;
        args.umask = umask;
        args.xdata = xdata;
        args.flags = flags;
        client_filter_o_direct(conf, &args.flags);
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(create, frame, -1, ENOTCONN, NULL, NULL, NULL, NULL,
                            NULL, NULL);

    return 0;
}

static int32_t
client_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            fd_t *fd, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_OPEN];
    if (proc->fn) {
        args.loc = loc;
        args.fd = fd;
        args.xdata = xdata;
        args.flags = flags;
        client_filter_o_direct(conf, &args.flags);
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(open, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, uint32_t flags, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_READ];
    if (proc->fn) {
        args.fd = fd;
        args.size = size;
        args.offset = offset;
        args.flags = flags;
        args.xdata = xdata;
        client_filter_o_direct(conf, &args.flags);

        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(readv, frame, -1, ENOTCONN, NULL, 0, NULL, NULL,
                            NULL);

    return 0;
}

static int32_t
client_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t off, uint32_t flags,
              struct iobref *iobref, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_WRITE];
    if (proc->fn) {
        args.fd = fd;
        args.vector = vector;
        args.count = count;
        args.offset = off;
        args.size = iov_length(vector, count);
        args.flags = flags;
        args.iobref = iobref;
        args.xdata = xdata;
        client_filter_o_direct(conf, &args.flags);
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(writev, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FLUSH];
    if (proc->fn) {
        args.fd = fd;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(flush, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FSYNC];
    if (proc->fn) {
        args.fd = fd;
        args.flags = flags;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fsync, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FSTAT];
    if (proc->fn) {
        args.fd = fd;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fstat, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
               dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_OPENDIR];
    if (proc->fn) {
        args.loc = loc;
        args.fd = fd;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(opendir, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

int32_t
client_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
                dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FSYNCDIR];
    if (proc->fn) {
        args.fd = fd;
        args.flags = flags;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fsyncdir, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_STATFS];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(statfs, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_copy_file_range(call_frame_t *frame, xlator_t *this, fd_t *fd_in,
                       off_t off_in, fd_t *fd_out, off_t off_out, size_t len,
                       uint32_t flags, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_COPY_FILE_RANGE];
    if (proc->fn) {
        args.fd = fd_in;
        args.fd_out = fd_out;
        args.offset = off_in;
        args.off_out = off_out;
        args.size = len;
        args.flags = flags;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(copy_file_range, frame, -1, ENOTCONN, NULL, NULL,
                            NULL, NULL);

    return 0;
}

static gf_boolean_t
is_client_rpc_init_command(dict_t *dict, xlator_t *this, char **value)
{
    gf_boolean_t ret = _gf_false;

    int dict_ret = dict_get_str_sizen(dict, CLIENT_CMD_CONNECT, value);
    if (dict_ret) {
        gf_msg_trace(this->name, 0, "key %s not present", CLIENT_CMD_CONNECT);
        goto out;
    }

    ret = _gf_true;

out:
    return ret;
}

static gf_boolean_t
is_client_rpc_destroy_command(dict_t *dict, xlator_t *this)
{
    gf_boolean_t ret = _gf_false;
    int dict_ret = -1;
    char *dummy = NULL;

    if (strncmp(this->name, "replace-brick", 13)) {
        gf_msg_trace(this->name, 0, "name is !replace-brick");
        goto out;
    }

    dict_ret = dict_get_str_sizen(dict, CLIENT_CMD_DISCONNECT, &dummy);
    if (dict_ret) {
        gf_msg_trace(this->name, 0, "key %s not present",
                     CLIENT_CMD_DISCONNECT);
        goto out;
    }

    ret = _gf_true;

out:
    return ret;
}

static int
client_set_remote_options(char *value, xlator_t *this)
{
    char *dup_value = NULL;
    char *host = NULL;
    char *subvol = NULL;
    char *host_dup = NULL;
    char *subvol_dup = NULL;
    char *remote_port_str = NULL;
    char *tmp = NULL;
    int remote_port = 0;
    int ret = -1;

    dup_value = gf_strdup(value);
    if (dup_value == NULL) {
        goto out;
    }
    host = strtok_r(dup_value, ":", &tmp);
    subvol = strtok_r(NULL, ":", &tmp);
    remote_port_str = strtok_r(NULL, ":", &tmp);

    if (host) {
        host_dup = gf_strdup(host);
        if (!host_dup) {
            goto out;
        }
        ret = dict_set_dynstr_sizen(this->options, "remote-host", host_dup);
        if (ret) {
            gf_smsg(this->name, GF_LOG_WARNING, 0,
                    PC_MSG_REMOTE_HOST_SET_FAILED, "host=%s", host, NULL);
            GF_FREE(host_dup);
            goto out;
        }
    }

    if (subvol) {
        subvol_dup = gf_strdup(subvol);
        if (!subvol_dup) {
            goto out;
        }

        ret = dict_set_dynstr_sizen(this->options, "remote-subvolume",
                                    subvol_dup);
        if (ret) {
            gf_smsg(this->name, GF_LOG_WARNING, 0,
                    PC_MSG_REMOTE_HOST_SET_FAILED, "host=%s", host, NULL);
            GF_FREE(subvol_dup);
            goto out;
        }
    }

    if (remote_port_str) {
        remote_port = atoi(remote_port_str);

        ret = dict_set_int32_sizen(this->options, "remote-port", remote_port);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_REMOTE_PORT_SET_FAILED,
                    "remote-port=%d", remote_port, NULL);
            goto out;
        }
    }

    ret = 0;
out:
    GF_FREE(dup_value);

    return ret;
}

static int32_t
client_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                int32_t flags, dict_t *xdata)
{
    int ret = -1;
    int op_ret = -1;
    int op_errno = ENOTCONN;
    int need_unwind = 0;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };
    char *value = NULL;

    if (is_client_rpc_init_command(dict, this, &value) == _gf_true) {
        GF_ASSERT(value);
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_RPC_INIT, NULL);
        ret = client_set_remote_options(value, this);
        if (!ret) {
            op_ret = 0;
            op_errno = 0;
        }
        need_unwind = 1;
        goto out;
    }

    if (is_client_rpc_destroy_command(dict, this) == _gf_true) {
        gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_RPC_DESTROY, NULL);
        ret = client_destroy_rpc(this);
        if (ret) {
            op_ret = 0;
            op_errno = 0;
        }
        need_unwind = 1;
        goto out;
    }

    conf = this->private;
    if (!conf || !conf->fops) {
        op_errno = ENOTCONN;
        need_unwind = 1;
        goto out;
    }

    proc = &conf->fops->proctable[GF_FOP_SETXATTR];
    if (proc->fn) {
        args.loc = loc;
        args.xattr = dict;
        args.flags = flags;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
        if (ret) {
            need_unwind = 1;
        }
    }
out:
    if (need_unwind)
        STACK_UNWIND_STRICT(setxattr, frame, op_ret, op_errno, NULL);

    return 0;
}

static int32_t
client_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                 int32_t flags, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FSETXATTR];
    if (proc->fn) {
        args.fd = fd;
        args.xattr = dict;
        args.flags = flags;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fsetxattr, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FGETXATTR];
    if (proc->fn) {
        args.fd = fd;
        args.name = name;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fgetxattr, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_GETXATTR];
    if (proc->fn) {
        args.name = name;
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(getxattr, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_xattrop(call_frame_t *frame, xlator_t *this, loc_t *loc,
               gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_XATTROP];
    if (proc->fn) {
        args.loc = loc;
        args.flags = flags;
        args.xattr = dict;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(xattrop, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_fxattrop(call_frame_t *frame, xlator_t *this, fd_t *fd,
                gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FXATTROP];
    if (proc->fn) {
        args.fd = fd;
        args.flags = flags;
        args.xattr = dict;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fxattrop, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   const char *name, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_REMOVEXATTR];
    if (proc->fn) {
        args.name = name;
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(removexattr, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                    const char *name, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FREMOVEXATTR];
    if (proc->fn) {
        args.name = name;
        args.fd = fd;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fremovexattr, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_lease(call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct gf_lease *lease, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_LEASE];
    if (proc->fn) {
        args.loc = loc;
        args.lease = lease;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(lk, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_lk(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
          struct gf_flock *lock, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_LK];
    if (proc->fn) {
        args.fd = fd;
        args.cmd = cmd;
        args.flock = lock;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(lk, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_inodelk(call_frame_t *frame, xlator_t *this, const char *volume,
               loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    GF_ASSERT(!is_lk_owner_null(&frame->root->lk_owner));
    proc = &conf->fops->proctable[GF_FOP_INODELK];
    if (proc->fn) {
        args.loc = loc;
        args.cmd = cmd;
        args.flock = lock;
        args.volume = volume;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(inodelk, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_finodelk(call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    GF_ASSERT(!is_lk_owner_null(&frame->root->lk_owner));
    proc = &conf->fops->proctable[GF_FOP_FINODELK];
    if (proc->fn) {
        args.fd = fd;
        args.cmd = cmd;
        args.flock = lock;
        args.volume = volume;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(finodelk, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_entrylk(call_frame_t *frame, xlator_t *this, const char *volume,
               loc_t *loc, const char *basename, entrylk_cmd cmd,
               entrylk_type type, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    GF_ASSERT(!is_lk_owner_null(&frame->root->lk_owner));
    proc = &conf->fops->proctable[GF_FOP_ENTRYLK];
    if (proc->fn) {
        args.loc = loc;
        args.basename = basename;
        args.type = type;
        args.volume = volume;
        args.cmd_entrylk = cmd;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(entrylk, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_fentrylk(call_frame_t *frame, xlator_t *this, const char *volume,
                fd_t *fd, const char *basename, entrylk_cmd cmd,
                entrylk_type type, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    GF_ASSERT(!is_lk_owner_null(&frame->root->lk_owner));
    proc = &conf->fops->proctable[GF_FOP_FENTRYLK];
    if (proc->fn) {
        args.fd = fd;
        args.basename = basename;
        args.type = type;
        args.volume = volume;
        args.cmd_entrylk = cmd;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fentrylk, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_rchecksum(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 int32_t len, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_RCHECKSUM];
    if (proc->fn) {
        args.fd = fd;
        args.offset = offset;
        args.len = len;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(rchecksum, frame, -1, ENOTCONN, 0, NULL, NULL);

    return 0;
}

int32_t
client_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t off, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_READDIR];
    if (proc->fn) {
        if (off != 0)
            off = gf_dirent_orig_offset(this, off);

        args.fd = fd;
        args.size = size;
        args.offset = off;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(readdir, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t off, dict_t *dict)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_READDIRP];
    if (proc->fn) {
        if (off != 0)
            off = gf_dirent_orig_offset(this, off);

        args.fd = fd;
        args.size = size;
        args.offset = off;
        args.xdata = dict;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(readdirp, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_SETATTR];
    if (proc->fn) {
        args.loc = loc;
        args.stbuf = stbuf;
        args.valid = valid;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(setattr, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FSETATTR];
    if (proc->fn) {
        args.fd = fd;
        args.stbuf = stbuf;
        args.valid = valid;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fsetattr, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
                 off_t offset, size_t len, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_FALLOCATE];
    if (proc->fn) {
        args.fd = fd;
        args.flags = mode;
        args.offset = offset;
        args.size = len;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(fallocate, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               size_t len, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_DISCARD];
    if (proc->fn) {
        args.fd = fd;
        args.offset = offset;
        args.size = len;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(discard, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                off_t len, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_ZEROFILL];
    if (proc->fn) {
        args.fd = fd;
        args.offset = offset;
        args.size = len;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(zerofill, frame, -1, ENOTCONN, NULL, NULL, NULL);

    return 0;
}

static int32_t
client_ipc(call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_IPC];
    if (proc->fn) {
        args.cmd = op;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(ipc, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            gf_seek_what_t what, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_SEEK];
    if (proc->fn) {
        args.fd = fd;
        args.offset = offset;
        args.what = what;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(seek, frame, -1, ENOTCONN, 0, NULL);

    return 0;
}

static int32_t
client_getactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_GETACTIVELK];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(getactivelk, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

static int32_t
client_setactivelk(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   lock_migration_info_t *locklist, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_SETACTIVELK];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        args.locklist = locklist;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(setactivelk, frame, -1, ENOTCONN, NULL);

    return 0;
}

static int32_t
client_getspec(call_frame_t *frame, xlator_t *this, const char *key,
               int32_t flags)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops || !conf->handshake)
        goto out;

    /* For all other xlators, getspec is an fop, hence its in fops table */
    proc = &conf->fops->proctable[GF_FOP_GETSPEC];
    if (proc->fn) {
        args.name = key;
        args.flags = flags;
        /* But at protocol level, this is handshake */
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(getspec, frame, -1, EINVAL, NULL);

    return 0;
}

static int32_t
client_compound(call_frame_t *frame, xlator_t *this, void *data, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    compound_args_t *args = data;
    rpc_clnt_procedure_t *proc = NULL;

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_COMPOUND];
    if (proc->fn) {
        args->xdata = xdata;
        ret = proc->fn(frame, this, args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(compound, frame, -1, ENOTCONN, NULL, NULL);

    return 0;
}

int32_t
client_namelink(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int32_t ret = -1;
    clnt_conf_t *conf = NULL;
    clnt_args_t args = {
        0,
    };
    rpc_clnt_procedure_t *proc = NULL;

    conf = this->private;
    if (!conf || !conf->fops || !conf->handshake)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_NAMELINK];
    if (proc->fn) {
        args.loc = loc;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(namelink, frame, -1, EINVAL, NULL, NULL, NULL);
    return 0;
}

static int32_t
client_icreate(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
               dict_t *xdata)
{
    int32_t ret = -1;
    clnt_conf_t *conf = NULL;
    clnt_args_t args = {
        0,
    };
    rpc_clnt_procedure_t *proc = NULL;

    conf = this->private;
    if (!conf || !conf->fops || !conf->handshake)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_ICREATE];
    if (proc->fn) {
        args.loc = loc;
        args.mode = mode;
        args.xdata = xdata;
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(icreate, frame, -1, EINVAL, NULL, NULL, NULL);
    return 0;
}

static int32_t
client_put(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           mode_t umask, uint32_t flags, struct iovec *vector, int32_t count,
           off_t off, struct iobref *iobref, dict_t *xattr, dict_t *xdata)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;
    rpc_clnt_procedure_t *proc = NULL;
    clnt_args_t args = {
        0,
    };

    conf = this->private;
    if (!conf || !conf->fops)
        goto out;

    proc = &conf->fops->proctable[GF_FOP_PUT];
    if (proc->fn) {
        args.loc = loc;
        args.mode = mode;
        args.umask = umask;
        args.flags = flags;
        args.vector = vector;
        args.count = count;
        args.offset = off;
        args.size = iov_length(vector, count);
        args.iobref = iobref;
        args.xattr = xattr;
        args.xdata = xdata;

        client_filter_o_direct(conf, &args.flags);
        ret = proc->fn(frame, this, &args);
    }
out:
    if (ret)
        STACK_UNWIND_STRICT(put, frame, -1, ENOTCONN, NULL, NULL, NULL, NULL,
                            NULL);

    return 0;
}

static void
client_mark_fd_bad(xlator_t *this)
{
    clnt_conf_t *conf = this->private;
    clnt_fd_ctx_t *tmp = NULL, *fdctx = NULL;

    pthread_spin_lock(&conf->fd_lock);
    {
        list_for_each_entry_safe(fdctx, tmp, &conf->saved_fds, sfd_pos)
        {
            fdctx->remote_fd = -1;
        }
    }
    pthread_spin_unlock(&conf->fd_lock);
}

int
client_rpc_notify(struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                  void *data)
{
    xlator_t *this = NULL;
    clnt_conf_t *conf = NULL;
    gf_boolean_t is_parent_down = _gf_false;
    int ret = 0;

    this = mydata;
    if (!this || !this->private) {
        gf_smsg("client", GF_LOG_ERROR, EINVAL, PC_MSG_XLATOR_NULL,
                (this != NULL) ? "private structue" : "", NULL);
        goto out;
    }

    conf = this->private;

    switch (event) {
        case RPC_CLNT_PING: {
            if (conf->connection_to_brick) {
                ret = default_notify(this, GF_EVENT_CHILD_PING, data);
                if (ret)
                    gf_log(this->name, GF_LOG_INFO, "CHILD_PING notify failed");
                conf->last_sent_event = GF_EVENT_CHILD_PING;
            }
            break;
        }
        case RPC_CLNT_CONNECT: {
            conf->can_log_disconnect = 1;
            // connect happened, send 'get_supported_versions' mop

            gf_msg_debug(this->name, 0, "got RPC_CLNT_CONNECT");

            ret = client_handshake(this, rpc);
            if (ret)
                gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_HANDSHAKE_RETURN,
                        "ret=%d", ret, NULL);
            break;
        }
        case RPC_CLNT_DISCONNECT:
            gf_msg_debug(this->name, 0, "got RPC_CLNT_DISCONNECT");

            client_mark_fd_bad(this);

            if (!conf->skip_notify) {
                if (conf->can_log_disconnect) {
                    if (!conf->disconnect_err_logged) {
                        gf_smsg(this->name, GF_LOG_INFO, 0,
                                PC_MSG_CLIENT_DISCONNECTED, "conn-name=%s",
                                conf->rpc->conn.name, NULL);
                    } else {
                        gf_msg_debug(this->name, 0,
                                     "disconnected from %s. "
                                     "Client process will keep"
                                     " trying to connect to "
                                     "glusterd until brick's "
                                     "port is available",
                                     conf->rpc->conn.name);
                    }
                    if (conf->portmap_err_logged)
                        conf->disconnect_err_logged = 1;
                }
                /*
                 * Once we complete the child down notification,
                 * There is a chance that the graph might get freed,
                 * So it is not safe to access any xlator contens
                 * So here we are checking whether the parent is down
                 * or not.
                 */
                pthread_mutex_lock(&conf->lock);
                {
                    is_parent_down = conf->parent_down;
                }
                pthread_mutex_unlock(&conf->lock);

                /* If the CHILD_DOWN event goes to parent xlator
                   multiple times, the logic of parent xlator notify
                   may get screwed up.. (eg. CHILD_MODIFIED event in
                   replicate), hence make sure events which are passed
                   to parent are genuine */
                ret = client_notify_dispatch_uniq(this, GF_EVENT_CHILD_DOWN,
                                                  rpc);
                if (is_parent_down) {
                    /* If parent is down, then there should not be any
                     * operation after a child down.
                     */
                    goto out;
                }
                if (ret)
                    gf_smsg(this->name, GF_LOG_INFO, 0,
                            PC_MSG_CHILD_DOWN_NOTIFY_FAILED, NULL);

            } else {
                if (conf->can_log_disconnect)
                    gf_msg_debug(this->name, 0,
                                 "disconnected (skipped notify)");
            }

            conf->connected = 0;
            conf->can_log_disconnect = 0;
            conf->skip_notify = 0;

            if (conf->quick_reconnect) {
                conf->connection_to_brick = _gf_true;
                conf->quick_reconnect = 0;
                rpc_clnt_cleanup_and_start(rpc);

            } else {
                rpc->conn.config.remote_port = 0;
                conf->connection_to_brick = _gf_false;
            }
            break;
        case RPC_CLNT_DESTROY:
            ret = client_fini_complete(this);
            break;

        default:
            gf_msg_trace(this->name, 0, "got some other RPC event %d", event);

            break;
    }

out:
    return 0;
}

int
notify(xlator_t *this, int32_t event, void *data, ...)
{
    clnt_conf_t *conf = NULL;
    glusterfs_graph_t *graph = this->graph;
    int ret = -1;

    conf = this->private;
    if (!conf)
        return 0;

    switch (event) {
        case GF_EVENT_PARENT_UP: {
            gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_PARENT_UP, NULL);

            rpc_clnt_start(conf->rpc);
            break;
        }

        case GF_EVENT_PARENT_DOWN:
            gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_PARENT_DOWN, NULL);

            pthread_mutex_lock(&conf->lock);
            {
                conf->parent_down = 1;
            }
            pthread_mutex_unlock(&conf->lock);

            ret = rpc_clnt_disable(conf->rpc);
            if (ret == -1 && graph) {
                pthread_mutex_lock(&graph->mutex);
                {
                    graph->parent_down++;
                    if (graph->parent_down ==
                        graph_total_client_xlator(graph)) {
                        graph->used = 0;
                        pthread_cond_broadcast(&graph->child_down_cond);
                    }
                }
                pthread_mutex_unlock(&graph->mutex);
            }
            break;

        default:
            gf_msg_debug(this->name, 0, "got %d, calling default_notify ()",
                         event);

            default_notify(this, event, data);
            conf->last_sent_event = event;
            break;
    }

    return 0;
}

static int
client_check_remote_host(xlator_t *this, dict_t *options)
{
    char *remote_host = NULL;
    int ret = -1;

    ret = dict_get_str_sizen(options, "remote-host", &remote_host);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_INFO, EINVAL, PC_MSG_REMOTE_HOST_NOT_SET,
                NULL);

        if (!this->ctx->cmd_args.volfile_server) {
            gf_smsg(this->name, GF_LOG_ERROR, EINVAL, PC_MSG_NOREMOTE_HOST,
                    NULL);
            goto out;
        }

        ret = dict_set_str_sizen(options, "remote-host",
                                 this->ctx->cmd_args.volfile_server);
        if (ret == -1) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_REMOTE_HOST_SET_FAILED,
                    NULL);
            goto out;
        }
    }

    ret = 0;
out:
    return ret;
}

static int
build_client_config(xlator_t *this, clnt_conf_t *conf)
{
    int ret = -1;

    GF_OPTION_INIT("frame-timeout", conf->rpc_conf.rpc_timeout, int32, out);

    GF_OPTION_INIT("remote-port", conf->rpc_conf.remote_port, int32, out);

    GF_OPTION_INIT("ping-timeout", conf->opt.ping_timeout, int32, out);

    GF_OPTION_INIT("remote-subvolume", conf->opt.remote_subvolume, path, out);
    if (!conf->opt.remote_subvolume)
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL,
                PC_MSG_REMOTE_SUBVOL_NOT_GIVEN, NULL);

    GF_OPTION_INIT("filter-O_DIRECT", conf->filter_o_direct, bool, out);

    GF_OPTION_INIT("send-gids", conf->send_gids, bool, out);

    GF_OPTION_INIT("testing.old-protocol", conf->old_protocol, bool, out);
    GF_OPTION_INIT("strict-locks", conf->strict_locks, bool, out);

    conf->client_id = glusterfs_leaf_position(this);

    ret = client_check_remote_host(this, this->options);
    if (ret)
        goto out;

    ret = 0;
out:
    return ret;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_client_mt_end + 1);

    if (ret != 0) {
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, PC_MSG_NO_MEMORY, NULL);
        return ret;
    }

    return ret;
}

static int
client_destroy_rpc(xlator_t *this)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;

    conf = this->private;
    if (!conf)
        goto out;

    if (conf->rpc) {
        /* cleanup the saved-frames before last unref */
        rpc_clnt_connection_cleanup(&conf->rpc->conn);

        conf->rpc = rpc_clnt_unref(conf->rpc);
        ret = 0;
        gf_msg_debug(this->name, 0, "Client rpc conn destroyed");
        goto out;
    }

    gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_RPC_INVALID_CALL, NULL);

out:
    return ret;
}

static int
client_init_rpc(xlator_t *this)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;

    conf = this->private;

    if (conf->rpc) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_RPC_INITED_ALREADY, NULL);
        ret = -1;
        goto out;
    }

    conf->rpc = rpc_clnt_new(this->options, this, this->name, 0);
    if (!conf->rpc) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_RPC_INIT_FAILED, NULL);
        goto out;
    }

    ret = rpc_clnt_register_notify(conf->rpc, client_rpc_notify, this);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_RPC_NOTIFY_FAILED, NULL);
        goto out;
    }

    conf->handshake = &clnt_handshake_prog;
    conf->dump = &clnt_dump_prog;

    ret = rpcclnt_cbk_program_register(conf->rpc, &gluster_cbk_prog, this);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_RPC_CBK_FAILED, NULL);
        goto out;
    }

    ret = 0;

    gf_msg_debug(this->name, 0, "client init successful");
out:
    return ret;
}

static int
client_check_event_threads(xlator_t *this, clnt_conf_t *conf, int32_t old,
                           int32_t new)
{
    if (old == new)
        return 0;

    conf->event_threads = new;
    return gf_event_reconfigure_threads(this->ctx->event_pool,
                                        conf->event_threads);
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    clnt_conf_t *conf = NULL;
    int ret = -1;
    int subvol_ret = 0;
    char *old_remote_subvol = NULL;
    char *new_remote_subvol = NULL;
    char *old_remote_host = NULL;
    char *new_remote_host = NULL;
    int32_t new_nthread = 0;
    struct rpc_clnt_config rpc_config = {
        0,
    };

    conf = this->private;

    GF_OPTION_RECONF("frame-timeout", conf->rpc_conf.rpc_timeout, options,
                     int32, out);

    GF_OPTION_RECONF("ping-timeout", rpc_config.ping_timeout, options, int32,
                     out);

    GF_OPTION_RECONF("event-threads", new_nthread, options, int32, out);
    ret = client_check_event_threads(this, conf, conf->event_threads,
                                     new_nthread);
    if (ret)
        goto out;

    ret = client_check_remote_host(this, options);
    if (ret)
        goto out;

    subvol_ret = dict_get_str_sizen(this->options, "remote-host",
                                    &old_remote_host);

    if (subvol_ret == 0) {
        subvol_ret = dict_get_str_sizen(options, "remote-host",
                                        &new_remote_host);
        if (subvol_ret == 0) {
            if (strcmp(old_remote_host, new_remote_host)) {
                ret = 1;
                goto out;
            }
        }
    }

    subvol_ret = dict_get_str_sizen(this->options, "remote-subvolume",
                                    &old_remote_subvol);

    if (subvol_ret == 0) {
        subvol_ret = dict_get_str_sizen(options, "remote-subvolume",
                                        &new_remote_subvol);
        if (subvol_ret == 0) {
            if (strcmp(old_remote_subvol, new_remote_subvol)) {
                ret = 1;
                goto out;
            }
        }
    }

    /* Reconfiguring client xlator's @rpc with new frame-timeout
     * and ping-timeout */
    rpc_clnt_reconfig(conf->rpc, &rpc_config);

    GF_OPTION_RECONF("filter-O_DIRECT", conf->filter_o_direct, options, bool,
                     out);

    GF_OPTION_RECONF("send-gids", conf->send_gids, options, bool, out);
    GF_OPTION_RECONF("strict-locks", conf->strict_locks, options, bool, out);

    ret = 0;
out:
    return ret;
}

int
init(xlator_t *this)
{
    int ret = -1;
    clnt_conf_t *conf = NULL;

    if (this->children) {
        gf_smsg(this->name, GF_LOG_ERROR, EINVAL, PC_MSG_FATAL_CLIENT_PROTOCOL,
                NULL);
        goto out;
    }

    if (!this->parents) {
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PC_MSG_VOL_DANGLING, NULL);
    }

    conf = GF_CALLOC(1, sizeof(*conf), gf_client_mt_clnt_conf_t);
    if (!conf)
        goto out;

    pthread_mutex_init(&conf->lock, NULL);
    pthread_cond_init(&conf->fini_complete_cond, NULL);
    pthread_spin_init(&conf->fd_lock, 0);
    INIT_LIST_HEAD(&conf->saved_fds);

    conf->child_up = _gf_false;

    /* Set event threads to the configured default */
    GF_OPTION_INIT("event-threads", conf->event_threads, int32, out);
    ret = client_check_event_threads(this, conf, STARTING_EVENT_THREADS,
                                     conf->event_threads);
    if (ret)
        goto out;

    LOCK_INIT(&conf->rec_lock);

    conf->last_sent_event = -1; /* To start with we don't have any events */

    this->private = conf;

    /* If it returns -1, then its a failure, if it returns +1 we need
       have to understand that 'this' is subvolume of a xlator which,
       will set the remote host and remote subvolume in a setxattr
       call.
    */

    ret = build_client_config(this, conf);
    if (ret == -1)
        goto out;

    if (ret) {
        ret = 0;
        goto out;
    }

    this->local_pool = mem_pool_new(clnt_local_t, 64);
    if (!this->local_pool) {
        ret = -1;
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, PC_MSG_CREATE_MEM_POOL_FAILED,
                NULL);
        goto out;
    }

    ret = client_init_rpc(this);
out:
    if (ret)
        this->fini(this);

    return ret;
}

void
fini(xlator_t *this)
{
    clnt_conf_t *conf = NULL;

    conf = this->private;
    if (!conf)
        return;

    conf->fini_completed = _gf_false;
    conf->destroy = 1;
    if (conf->rpc) {
        /* cleanup the saved-frames before last unref */
        rpc_clnt_connection_cleanup(&conf->rpc->conn);
        rpc_clnt_unref(conf->rpc);
    }

    pthread_mutex_lock(&conf->lock);
    {
        while (!conf->fini_completed)
            pthread_cond_wait(&conf->fini_complete_cond, &conf->lock);
    }
    pthread_mutex_unlock(&conf->lock);

    pthread_spin_destroy(&conf->fd_lock);
    pthread_mutex_destroy(&conf->lock);
    pthread_cond_destroy(&conf->fini_complete_cond);
    GF_FREE(conf);

    /* Saved Fds */
    /* TODO: */

    return;
}

static void
client_fd_lk_ctx_dump(xlator_t *this, fd_lk_ctx_t *lk_ctx, int nth_fd)
{
    gf_boolean_t use_try_lock = _gf_true;
    int ret = -1;
    int lock_no = 0;
    fd_lk_ctx_t *lk_ctx_ref = NULL;
    fd_lk_ctx_node_t *plock = NULL;
    char key[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };

    lk_ctx_ref = fd_lk_ctx_ref(lk_ctx);
    if (!lk_ctx_ref)
        return;

    ret = client_fd_lk_list_empty(lk_ctx_ref, (use_try_lock = _gf_true));
    if (ret != 0)
        return;

    gf_proc_dump_write("------", "------");

    lock_no = 0;

    ret = TRY_LOCK(&lk_ctx_ref->lock);
    if (ret)
        return;

    list_for_each_entry(plock, &lk_ctx_ref->lk_list, next)
    {
        snprintf(key, sizeof(key), "granted-posix-lock[%d]", lock_no++);
        gf_proc_dump_write(
            key,
            "owner = %s, cmd = %s "
            "fl_type = %s, fl_start = %" PRId64 ", fl_end = %" PRId64
            ", user_flock: l_type = %s, "
            "l_start = %" PRId64 ", l_len = %" PRId64,
            lkowner_utoa(&plock->user_flock.l_owner), get_lk_cmd(plock->cmd),
            get_lk_type(plock->fl_type), plock->fl_start, plock->fl_end,
            get_lk_type(plock->user_flock.l_type), plock->user_flock.l_start,
            plock->user_flock.l_len);
    }
    UNLOCK(&lk_ctx_ref->lock);

    gf_proc_dump_write("------", "------");

    fd_lk_ctx_unref(lk_ctx_ref);
}

static int
client_priv_dump(xlator_t *this)
{
    clnt_conf_t *conf = NULL;
    int ret = -1;
    clnt_fd_ctx_t *tmp = NULL;
    int i = 0;
    char key[GF_DUMP_MAX_BUF_LEN];
    char key_prefix[GF_DUMP_MAX_BUF_LEN];
    rpc_clnt_connection_t *conn = NULL;

    if (!this)
        return -1;

    conf = this->private;
    if (!conf)
        return -1;

    gf_proc_dump_build_key(key_prefix, "xlator.protocol.client", "%s.priv",
                           this->name);

    gf_proc_dump_add_section("%s", key_prefix);

    ret = pthread_mutex_trylock(&conf->lock);
    if (ret)
        return -1;

    pthread_spin_lock(&conf->fd_lock);
    list_for_each_entry(tmp, &conf->saved_fds, sfd_pos)
    {
        sprintf(key, "fd.%d.remote_fd", i);
        gf_proc_dump_write(key, "%" PRId64, tmp->remote_fd);
        client_fd_lk_ctx_dump(this, tmp->lk_ctx, i);
        i++;
    }
    pthread_spin_unlock(&conf->fd_lock);

    gf_proc_dump_write("connected", "%d", conf->connected);

    if (conf->rpc) {
        conn = &conf->rpc->conn;
        gf_proc_dump_write("total_bytes_read", "%" PRIu64,
                           conn->trans->total_bytes_read);
        gf_proc_dump_write("ping_timeout", "%" PRIu32, conn->ping_timeout);
        gf_proc_dump_write("total_bytes_written", "%" PRIu64,
                           conn->trans->total_bytes_write);
        gf_proc_dump_write("ping_msgs_sent", "%" PRIu64, conn->pingcnt);
        gf_proc_dump_write("msgs_sent", "%" PRIu64, conn->msgcnt);
    }
    pthread_mutex_unlock(&conf->lock);

    return 0;
}

int32_t
client_inodectx_dump(xlator_t *this, inode_t *inode)
{
    if (!inode)
        return -1;

    if (!this)
        return -1;

    /*TODO*/

    return 0;
}

struct xlator_cbks cbks = {.forget = client_forget,
                           .release = client_release,
                           .releasedir = client_releasedir};

struct xlator_fops fops = {
    .stat = client_stat,
    .readlink = client_readlink,
    .mknod = client_mknod,
    .mkdir = client_mkdir,
    .unlink = client_unlink,
    .rmdir = client_rmdir,
    .symlink = client_symlink,
    .rename = client_rename,
    .link = client_link,
    .truncate = client_truncate,
    .open = client_open,
    .readv = client_readv,
    .writev = client_writev,
    .statfs = client_statfs,
    .flush = client_flush,
    .fsync = client_fsync,
    .setxattr = client_setxattr,
    .getxattr = client_getxattr,
    .fsetxattr = client_fsetxattr,
    .fgetxattr = client_fgetxattr,
    .removexattr = client_removexattr,
    .fremovexattr = client_fremovexattr,
    .opendir = client_opendir,
    .readdir = client_readdir,
    .readdirp = client_readdirp,
    .fsyncdir = client_fsyncdir,
    .access = client_access,
    .ftruncate = client_ftruncate,
    .fstat = client_fstat,
    .create = client_create,
    .lk = client_lk,
    .inodelk = client_inodelk,
    .finodelk = client_finodelk,
    .entrylk = client_entrylk,
    .fentrylk = client_fentrylk,
    .lookup = client_lookup,
    .rchecksum = client_rchecksum,
    .xattrop = client_xattrop,
    .fxattrop = client_fxattrop,
    .setattr = client_setattr,
    .fsetattr = client_fsetattr,
    .fallocate = client_fallocate,
    .discard = client_discard,
    .zerofill = client_zerofill,
    .getspec = client_getspec,
    .ipc = client_ipc,
    .seek = client_seek,
    .lease = client_lease,
    .compound = client_compound,
    .getactivelk = client_getactivelk,
    .setactivelk = client_setactivelk,
    .icreate = client_icreate,
    .namelink = client_namelink,
    .put = client_put,
    .copy_file_range = client_copy_file_range,
};

struct xlator_dumpops dumpops = {
    .priv = client_priv_dump,
    .inodectx = client_inodectx_dump,
};

struct volume_options options[] = {
    {.key = {"username"}, .type = GF_OPTION_TYPE_ANY},
    {.key = {"password"}, .type = GF_OPTION_TYPE_ANY},
    {
        .key = {"transport-type"},
        .value = {"tcp", "socket", "ib-verbs", "unix", "ib-sdp", "tcp/client",
                  "ib-verbs/client", "rdma"},
        .type = GF_OPTION_TYPE_STR,
        .default_value = "tcp",
    },
    {.key = {"remote-host"},
     .type = GF_OPTION_TYPE_INTERNET_ADDRESS,
     .default_value = "{{ brick.hostname }}"},
    {
        .key = {"remote-port"},
        .type = GF_OPTION_TYPE_INT,
    },
    {.key = {"remote-subvolume"},
     .type = GF_OPTION_TYPE_ANY,
     .default_value = "{{ brick.path }}"},
    {.key = {"frame-timeout", "rpc-timeout"},
     .type = GF_OPTION_TYPE_TIME,
     .min = 0,
     .max = 86400,
     .default_value = "1800",
     .description = "Time frame after which the (file) operation would be "
                    "declared as dead, if the server does not respond for "
                    "a particular (file) operation.",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"ping-timeout"},
     .type = GF_OPTION_TYPE_TIME,
     .min = 0,
     .max = 1013,
     .default_value = TOSTRING(GF_NETWORK_TIMEOUT),
     .description = "Time duration for which the client waits to "
                    "check if the server is responsive.",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"client-bind-insecure"}, .type = GF_OPTION_TYPE_BOOL},
    {.key = {"tcp-window-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .min = GF_MIN_SOCKET_WINDOW_SIZE,
     .max = GF_MAX_SOCKET_WINDOW_SIZE,
     .description = "Specifies the window size for tcp socket.",
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"filter-O_DIRECT"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "disable",
     .description =
         "If enabled, in open/creat/readv/writev fops, "
         "O_DIRECT flag will be filtered at the client protocol level so "
         "server will still continue to cache the file. This works similar to "
         "NFS's behavior of O_DIRECT. Anon-fds can choose to readv/writev "
         "using O_DIRECT",
     .op_version = {2},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},
    {.key = {"send-gids"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .op_version = {GD_OP_VERSION_3_6_0},
     .flags = OPT_FLAG_SETTABLE},
    {.key = {"event-threads"},
     .type = GF_OPTION_TYPE_INT,
     .min = 1,
     .max = 32,
     .default_value = "2",
     .description = "Specifies the number of event threads to execute "
                    "in parallel. Larger values would help process"
                    " responses faster, depending on available processing"
                    " power. Range 1-32 threads.",
     .op_version = {GD_OP_VERSION_3_7_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC},

    /* This option is required for running code-coverage tests with
       old protocol */
    {
        .key = {"testing.old-protocol"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .op_version = {GD_OP_VERSION_7_0},
        .flags = OPT_FLAG_SETTABLE,
    },
    {.key = {"strict-locks"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     .op_version = {GD_OP_VERSION_7_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "When set, doesn't reopen saved fds after reconnect "
                    "if POSIX locks are held on them. Hence subsequent "
                    "operations on these fds will fail. This is "
                    "necessary for stricter lock complaince as bricks "
                    "cleanup any granted locks when a client "
                    "disconnects."},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = notify,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "client",
    .category = GF_MAINTAINED,
};
