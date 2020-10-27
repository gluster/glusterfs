/*
  Copyright (c) 2012-2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

#include <glusterfs/glusterfs.h>
#include "glfs.h"
#include <glusterfs/dict.h>

#include "rpc-clnt.h"
#include "protocol-common.h"
#include "xdr-generic.h"
#include "rpc-common-xdr.h"

#include <glusterfs/syncop.h>

#include "glfs-internal.h"
#include "gfapi-messages.h"
#include <glusterfs/syscall.h>

int
glfs_volfile_fetch(struct glfs *fs);
int32_t
glfs_get_volume_info_rpc(call_frame_t *frame, xlator_t *this, struct glfs *fs);

int
glfs_process_volfp(struct glfs *fs, FILE *fp)
{
    glusterfs_graph_t *graph = NULL;
    int ret = -1;
    xlator_t *trav = NULL;
    glusterfs_ctx_t *ctx = NULL;

    ctx = fs->ctx;
    graph = glusterfs_graph_construct(fp);
    if (!graph) {
        gf_smsg("glfs", GF_LOG_ERROR, errno, API_MSG_GRAPH_CONSTRUCT_FAILED,
                NULL);
        goto out;
    }

    for (trav = graph->first; trav; trav = trav->next) {
        if (strcmp(trav->type, "mount/api") == 0) {
            gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_API_XLATOR_ERROR,
                    NULL);
            goto out;
        }
    }

    ret = glusterfs_graph_prepare(graph, ctx, fs->volname);
    if (ret) {
        glusterfs_graph_destroy(graph);
        goto out;
    }

    ret = glusterfs_graph_activate(graph, ctx);

    if (ret) {
        glusterfs_graph_destroy(graph);
        goto out;
    }

    gf_log_dump_graph(fp, graph);

    ret = 0;
out:
    if (fp)
        fclose(fp);

    if (!ctx->active) {
        ret = -1;
    }

    return ret;
}

int
mgmt_cbk_spec(struct rpc_clnt *rpc, void *mydata, void *data)
{
    struct glfs *fs = NULL;
    xlator_t *this = NULL;

    this = mydata;
    fs = this->private;

    glfs_volfile_fetch(fs);

    return 0;
}

int
mgmt_cbk_event(struct rpc_clnt *rpc, void *mydata, void *data)
{
    return 0;
}

static int
mgmt_cbk_statedump(struct rpc_clnt *rpc, void *mydata, void *data)
{
    struct glfs *fs = NULL;
    xlator_t *this = NULL;
    gf_statedump target_pid = {
        0,
    };
    struct iovec *iov = NULL;
    int ret = -1;

    this = mydata;
    if (!this) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_NULL, "mydata", NULL);
        errno = EINVAL;
        goto out;
    }

    fs = this->private;
    if (!fs) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_NULL, "glfs", NULL);
        errno = EINVAL;
        goto out;
    }

    iov = (struct iovec *)data;
    if (!iov) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_NULL, "iovec data", NULL);
        errno = EINVAL;
        goto out;
    }

    ret = xdr_to_generic(*iov, &target_pid, (xdrproc_t)xdr_gf_statedump);
    if (ret < 0) {
        gf_smsg("glfs", GF_LOG_ERROR, EINVAL, API_MSG_DECODE_XDR_FAILED, NULL);
        goto out;
    }

    gf_msg_trace("glfs", 0, "statedump requested for pid: %d", target_pid.pid);

    if ((uint64_t)getpid() == target_pid.pid) {
        gf_msg_debug("glfs", 0, "Taking statedump for pid: %d", target_pid.pid);

        ret = glfs_sysrq(fs, GLFS_SYSRQ_STATEDUMP);
        if (ret < 0) {
            gf_smsg("glfs", GF_LOG_INFO, 0, API_MSG_STATEDUMP_FAILED, NULL);
        }
    }
out:
    return ret;
}

static rpcclnt_cb_actor_t mgmt_cbk_actors[GF_CBK_MAXVALUE] = {
    [GF_CBK_FETCHSPEC] = {"FETCHSPEC", mgmt_cbk_spec, GF_CBK_FETCHSPEC},
    [GF_CBK_EVENT_NOTIFY] = {"EVENTNOTIFY", mgmt_cbk_event,
                             GF_CBK_EVENT_NOTIFY},
    [GF_CBK_STATEDUMP] = {"STATEDUMP", mgmt_cbk_statedump, GF_CBK_STATEDUMP},
};

static struct rpcclnt_cb_program mgmt_cbk_prog = {
    .progname = "GlusterFS Callback",
    .prognum = GLUSTER_CBK_PROGRAM,
    .progver = GLUSTER_CBK_VERSION,
    .actors = mgmt_cbk_actors,
    .numactors = GF_CBK_MAXVALUE,
};

static char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
    [GF_HNDSK_NULL] = "NULL",
    [GF_HNDSK_SETVOLUME] = "SETVOLUME",
    [GF_HNDSK_GETSPEC] = "GETSPEC",
    [GF_HNDSK_PING] = "PING",
    [GF_HNDSK_EVENT_NOTIFY] = "EVENTNOTIFY",
    [GF_HNDSK_GET_VOLUME_INFO] = "GETVOLUMEINFO",
};

static rpc_clnt_prog_t clnt_handshake_prog = {
    .progname = "GlusterFS Handshake",
    .prognum = GLUSTER_HNDSK_PROGRAM,
    .progver = GLUSTER_HNDSK_VERSION,
    .procnames = clnt_handshake_procs,
};

int
mgmt_submit_request(void *req, call_frame_t *frame, glusterfs_ctx_t *ctx,
                    rpc_clnt_prog_t *prog, int procnum, fop_cbk_fn_t cbkfn,
                    xdrproc_t xdrproc)
{
    int ret = -1;
    int count = 0;
    struct iovec iov = {
        0,
    };
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    ssize_t xdr_size = 0;

    iobref = iobref_new();
    if (!iobref) {
        goto out;
    }

    if (req) {
        xdr_size = xdr_sizeof(xdrproc, req);

        iobuf = iobuf_get2(ctx->iobuf_pool, xdr_size);
        if (!iobuf) {
            goto out;
        };

        iobref_add(iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len = iobuf_pagesize(iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic(iov, req, xdrproc);
        if (ret == -1) {
            gf_smsg(THIS->name, GF_LOG_WARNING, 0, API_MSG_XDR_PAYLOAD_FAILED,
                    NULL);
            goto out;
        }
        iov.iov_len = ret;
        count = 1;
    }

    /* Send the msg */
    ret = rpc_clnt_submit(ctx->mgmt, prog, procnum, cbkfn, &iov, count, NULL, 0,
                          iobref, frame, NULL, 0, NULL, 0, NULL);

out:
    if (iobref)
        iobref_unref(iobref);

    if (iobuf)
        iobuf_unref(iobuf);
    return ret;
}

/*
 * Callback routine for 'GF_HNDSK_GET_VOLUME_INFO' rpc request
 */
int
mgmt_get_volinfo_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    int ret = 0;
    char *volume_id_str = NULL;
    dict_t *dict = NULL;
    gf_get_volume_info_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    glusterfs_ctx_t *ctx = NULL;
    struct glfs *fs = NULL;
    struct syncargs *args;

    frame = myframe;
    ctx = frame->this->ctx;
    args = frame->local;

    if (!ctx) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL, API_MSG_NULL,
                "context", NULL);
        errno = EINVAL;
        ret = -1;
        goto out;
    }

    fs = ((xlator_t *)ctx->primary)->private;

    if (-1 == req->rpc_status) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                API_MSG_CALL_NOT_SUCCESSFUL, NULL);
        errno = EINVAL;
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_get_volume_info_rsp);

    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, 0,
                API_MSG_XDR_RESPONSE_DECODE_FAILED, NULL);
        goto out;
    }

    gf_msg_debug(frame->this->name, 0,
                 "Received resp to GET_VOLUME_INFO "
                 "RPC: %d",
                 rsp.op_ret);

    if (rsp.op_ret == -1) {
        errno = rsp.op_errno;
        ret = -1;
        goto out;
    }

    if (!rsp.dict.dict_len) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL, API_MSG_CALL_NOT_VALID,
                NULL);
        ret = -1;
        errno = EINVAL;
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        errno = ENOMEM;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret) {
        errno = ENOMEM;
        goto out;
    }

    ret = dict_get_str_sizen(dict, "volume_id", &volume_id_str);
    if (ret) {
        errno = EINVAL;
        goto out;
    }

    ret = 0;
out:
    if (volume_id_str) {
        gf_msg_debug(frame->this->name, 0, "Volume Id: %s", volume_id_str);
        pthread_mutex_lock(&fs->mutex);
        gf_uuid_parse(volume_id_str, fs->vol_uuid);
        pthread_mutex_unlock(&fs->mutex);
    }

    if (ret) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, errno,
                API_MSG_GET_VOLINFO_CBK_FAILED, "error=%s", strerror(errno),
                NULL);
    }

    if (dict)
        dict_unref(dict);

    if (rsp.dict.dict_val)
        free(rsp.dict.dict_val);

    if (rsp.op_errstr)
        free(rsp.op_errstr);

    gf_msg_debug(frame->this->name, 0, "Returning: %d", ret);

    __wake(args);

    return ret;
}

GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_get_volumeid, 3.5.0)
int
pub_glfs_get_volumeid(struct glfs *fs, char *volid, size_t size)
{
    /* TODO: Define a global macro to store UUID size */
    size_t uuid_size = 16;

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    pthread_mutex_lock(&fs->mutex);
    {
        /* check if the volume uuid is initialized */
        if (!gf_uuid_is_null(fs->vol_uuid)) {
            pthread_mutex_unlock(&fs->mutex);
            goto done;
        }
    }
    pthread_mutex_unlock(&fs->mutex);

    /* Need to fetch volume_uuid */
    glfs_get_volume_info(fs);

    if (gf_uuid_is_null(fs->vol_uuid)) {
        gf_smsg(THIS->name, GF_LOG_ERROR, EINVAL, API_MSG_FETCH_VOLUUID_FAILED,
                NULL);
        goto out;
    }

done:
    if (!volid || !size) {
        gf_msg_debug(THIS->name, 0, "volumeid/size is null");
        __GLFS_EXIT_FS;
        return uuid_size;
    }

    if (size < uuid_size) {
        gf_smsg(THIS->name, GF_LOG_ERROR, ERANGE, API_MSG_INSUFF_SIZE, NULL);
        errno = ERANGE;
        goto out;
    }

    memcpy(volid, fs->vol_uuid, uuid_size);

    __GLFS_EXIT_FS;

    return uuid_size;

out:
    __GLFS_EXIT_FS;

invalid_fs:
    return -1;
}

int
glfs_get_volume_info(struct glfs *fs)
{
    call_frame_t *frame = NULL;
    glusterfs_ctx_t *ctx = NULL;
    struct syncargs args = {
        0,
    };
    int ret = 0;

    ctx = fs->ctx;
    frame = create_frame(THIS, ctx->pool);
    if (!frame) {
        gf_smsg("glfs", GF_LOG_ERROR, ENOMEM, API_MSG_FRAME_CREAT_FAILED, NULL);
        ret = -1;
        goto out;
    }

    frame->local = &args;

    __yawn((&args));

    ret = glfs_get_volume_info_rpc(frame, THIS, fs);
    if (ret)
        goto out;

    __yield((&args));

    frame->local = NULL;
    STACK_DESTROY(frame->root);

out:
    return ret;
}

int32_t
glfs_get_volume_info_rpc(call_frame_t *frame, xlator_t *this, struct glfs *fs)
{
    gf_get_volume_info_req req = {{
        0,
    }};
    int ret = 0;
    glusterfs_ctx_t *ctx = NULL;
    dict_t *dict = NULL;
    int32_t flags = 0;

    if (!frame || !this || !fs) {
        ret = -1;
        goto out;
    }

    ctx = fs->ctx;

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    if (fs->volname) {
        ret = dict_set_str(dict, "volname", fs->volname);
        if (ret)
            goto out;
    }

    // Set the flags for the fields which we are interested in
    flags = (int32_t)GF_GET_VOLUME_UUID;  // ctx->flags;
    ret = dict_set_int32(dict, "flags", flags);
    if (ret) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL,
                API_MSG_DICT_SET_FAILED, "flags", NULL);
        goto out;
    }

    ret = dict_allocate_and_serialize(dict, &req.dict.dict_val,
                                      &req.dict.dict_len);

    ret = mgmt_submit_request(&req, frame, ctx, &clnt_handshake_prog,
                              GF_HNDSK_GET_VOLUME_INFO, mgmt_get_volinfo_cbk,
                              (xdrproc_t)xdr_gf_get_volume_info_req);
out:
    if (dict) {
        dict_unref(dict);
    }

    GF_FREE(req.dict.dict_val);

    return ret;
}

static int
glusterfs_oldvolfile_update(struct glfs *fs, char *volfile, ssize_t size)
{
    int ret = -1;

    pthread_mutex_lock(&fs->mutex);

    fs->oldvollen = size;
    if (!fs->oldvolfile) {
        fs->oldvolfile = CALLOC(1, size + 1);
    } else {
        fs->oldvolfile = REALLOC(fs->oldvolfile, size + 1);
    }

    if (!fs->oldvolfile) {
        fs->oldvollen = 0;
    } else {
        memcpy(fs->oldvolfile, volfile, size);
        fs->oldvollen = size;
        ret = 0;
    }

    pthread_mutex_unlock(&fs->mutex);

    return ret;
}

int
glfs_mgmt_getspec_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    gf_getspec_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int ret = 0;
    ssize_t size = 0;
    FILE *tmpfp = NULL;
    int need_retry = 0;
    struct glfs *fs = NULL;
    dict_t *dict = NULL;
    char *servers_list = NULL;
    int tmp_fd = -1;
    char template[] = "/tmp/gfapi.volfile.XXXXXX";

    frame = myframe;
    ctx = frame->this->ctx;

    if (!ctx) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, EINVAL, API_MSG_NULL,
                "context", NULL);
        errno = EINVAL;
        ret = -1;
        goto out;
    }

    fs = ((xlator_t *)ctx->primary)->private;

    if (-1 == req->rpc_status) {
        ret = -1;
        need_retry = 1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, 0, API_MSG_XDR_DECODE_FAILED,
                NULL);
        ret = -1;
        goto out;
    }

    if (-1 == rsp.op_ret) {
        gf_smsg(frame->this->name, GF_LOG_ERROR, rsp.op_errno,
                API_MSG_GET_VOLFILE_FAILED, "from server", NULL);
        ret = -1;
        errno = rsp.op_errno;
        goto out;
    }

    if (!rsp.xdata.xdata_len) {
        goto volfile;
    }

    dict = dict_new();
    if (!dict) {
        ret = -1;
        errno = ENOMEM;
        goto out;
    }

    ret = dict_unserialize(rsp.xdata.xdata_val, rsp.xdata.xdata_len, &dict);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to unserialize xdata to dictionary");
        goto out;
    }
    dict->extra_stdfree = rsp.xdata.xdata_val;

    /* glusterd2 only */
    ret = dict_get_str(dict, "servers-list", &servers_list);
    if (ret) {
        goto volfile;
    }

    gf_log(frame->this->name, GF_LOG_INFO,
           "Received list of available volfile servers: %s", servers_list);

    ret = gf_process_getspec_servers_list(&ctx->cmd_args, servers_list);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "Failed (%s) to process servers list: %s", strerror(errno),
               servers_list);
    }

volfile:
    ret = 0;
    size = rsp.op_ret;

    pthread_mutex_lock(&fs->mutex);
    if ((size == fs->oldvollen) &&
        (memcmp(fs->oldvolfile, rsp.spec, size) == 0)) {
        pthread_mutex_unlock(&fs->mutex);
        gf_smsg(frame->this->name, GF_LOG_INFO, 0, API_MSG_VOLFILE_INFO, NULL);
        goto out;
    }
    pthread_mutex_unlock(&fs->mutex);

    /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
    tmp_fd = mkstemp(template);
    if (-1 == tmp_fd) {
        ret = -1;
        goto out;
    }

    /* Calling unlink so that when the file is closed or program
     * terminates the temporary file is deleted.
     */
    ret = sys_unlink(template);
    if (ret < 0) {
        gf_smsg(frame->this->name, GF_LOG_INFO, 0, API_MSG_UNABLE_TO_DEL,
                "template=%s", template, NULL);
        ret = 0;
    }

    tmpfp = fdopen(tmp_fd, "w+b");
    if (!tmpfp) {
        ret = -1;
        goto out;
    }

    fwrite(rsp.spec, size, 1, tmpfp);
    fflush(tmpfp);
    if (ferror(tmpfp)) {
        ret = -1;
        goto out;
    }

    /*  Check if only options have changed. No need to reload the
     *  volfile if topology hasn't changed.
     *  glusterfs_volfile_reconfigure returns 3 possible return states
     *  return 0	     =======> reconfiguration of options has succeeded
     *  return 1	     =======> the graph has to be reconstructed and all
     * the xlators should be inited return -1(or -ve) =======> Some Internal
     * Error occurred during the operation
     */

    pthread_mutex_lock(&fs->mutex);
    ret = gf_volfile_reconfigure(fs->oldvollen, tmpfp, fs->ctx, fs->oldvolfile);
    pthread_mutex_unlock(&fs->mutex);

    if (ret == 0) {
        gf_msg_debug("glusterfsd-mgmt", 0,
                     "No need to re-load "
                     "volfile, reconfigure done");
        ret = glusterfs_oldvolfile_update(fs, rsp.spec, size);
        goto out;
    }

    if (ret < 0) {
        gf_msg_debug("glusterfsd-mgmt", 0, "Reconfigure failed !!");
        goto out;
    }

    ret = glfs_process_volfp(fs, tmpfp);
    /* tmpfp closed */
    tmpfp = NULL;
    tmp_fd = -1;
    if (ret)
        goto out;

    ret = glusterfs_oldvolfile_update(fs, rsp.spec, size);
out:
    STACK_DESTROY(frame->root);

    if (rsp.spec)
        free(rsp.spec);

    if (dict)
        dict_unref(dict);

    // Stop if server is running at an unsupported op-version
    if (ENOTSUP == ret) {
        gf_smsg("mgmt", GF_LOG_ERROR, ENOTSUP, API_MSG_WRONG_OPVERSION, NULL);
        errno = ENOTSUP;
        glfs_init_done(fs, -1);
    }

    if (ret && ctx && !ctx->active) {
        /* Do it only for the first time */
        /* Failed to get the volume file, something wrong,
           restart the process */
        gf_smsg("glfs-mgmt", GF_LOG_ERROR, EINVAL, API_MSG_GET_VOLFILE_FAILED,
                "key=%s", ctx->cmd_args.volfile_id, NULL);
        if (!need_retry) {
            if (!errno)
                errno = EINVAL;
            glfs_init_done(fs, -1);
        }
    }

    if (tmpfp)
        fclose(tmpfp);
    else if (tmp_fd != -1)
        sys_close(tmp_fd);

    return 0;
}

int
glfs_volfile_fetch(struct glfs *fs)
{
    cmd_args_t *cmd_args = NULL;
    gf_getspec_req req = {
        0,
    };
    int ret = -1;
    call_frame_t *frame = NULL;
    glusterfs_ctx_t *ctx = NULL;
    dict_t *dict = NULL;

    ctx = fs->ctx;
    cmd_args = &ctx->cmd_args;

    req.key = cmd_args->volfile_id;
    req.flags = 0;

    dict = dict_new();
    if (!dict) {
        goto out;
    }

    // Set the supported min and max op-versions, so glusterd can make a
    // decision
    ret = dict_set_int32(dict, "min-op-version", GD_OP_VERSION_MIN);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, EINVAL, API_MSG_DICT_SET_FAILED,
                "min-op-version", NULL);
        goto out;
    }

    ret = dict_set_int32(dict, "max-op-version", GD_OP_VERSION_MAX);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, EINVAL, API_MSG_DICT_SET_FAILED,
                "max-op-version", NULL);
        goto out;
    }

    /* Ask for a list of volfile (glusterd2 only) servers */
    if (GF_CLIENT_PROCESS == ctx->process_mode) {
        req.flags = req.flags | GF_GETSPEC_FLAG_SERVERS_LIST;
    }

    ret = dict_allocate_and_serialize(dict, &req.xdata.xdata_val,
                                      &req.xdata.xdata_len);
    if (ret < 0) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, API_MSG_DICT_SERIALIZE_FAILED,
                NULL);
        goto out;
    }

    frame = create_frame(THIS, ctx->pool);
    if (!frame) {
        ret = -1;
        goto out;
    }

    ret = mgmt_submit_request(&req, frame, ctx, &clnt_handshake_prog,
                              GF_HNDSK_GETSPEC, glfs_mgmt_getspec_cbk,
                              (xdrproc_t)xdr_gf_getspec_req);
out:
    if (req.xdata.xdata_val)
        GF_FREE(req.xdata.xdata_val);
    if (dict)
        dict_unref(dict);

    return ret;
}

static int
mgmt_rpc_notify(struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                void *data)
{
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;
    server_cmdline_t *server = NULL;
    rpc_transport_t *rpc_trans = NULL;
    struct glfs *fs = NULL;
    int ret = 0;
    struct dnscache6 *dnscache = NULL;

    this = mydata;
    rpc_trans = rpc->conn.trans;

    ctx = this->ctx;
    if (!ctx)
        goto out;

    fs = ((xlator_t *)ctx->primary)->private;

    switch (event) {
        case RPC_CLNT_DISCONNECT:
            if (!ctx->active) {
                if (rpc_trans->connect_failed)
                    gf_smsg("glfs-mgmt", GF_LOG_ERROR, 0,
                            API_MSG_REMOTE_HOST_CONN_FAILED, "server=%s",
                            ctx->cmd_args.volfile_server, NULL);
                else
                    gf_smsg("glfs-mgmt", GF_LOG_INFO, 0,
                            API_MSG_REMOTE_HOST_CONN_FAILED, "server=%s",
                            ctx->cmd_args.volfile_server, NULL);

                if (!rpc->disabled) {
                    /*
                     * Check if dnscache is exhausted for current server
                     * and continue until cache is exhausted
                     */
                    dnscache = rpc_trans->dnscache;
                    if (dnscache && dnscache->next) {
                        break;
                    }
                }
                server = ctx->cmd_args.curr_server;
                if (server->list.next == &ctx->cmd_args.volfile_servers) {
                    errno = ENOTCONN;
                    gf_smsg("glfs-mgmt", GF_LOG_INFO, ENOTCONN,
                            API_MSG_VOLFILE_SERVER_EXHAUST, NULL);
                    glfs_init_done(fs, -1);
                    break;
                }
                server = list_entry(server->list.next, typeof(*server), list);
                ctx->cmd_args.curr_server = server;
                ctx->cmd_args.volfile_server_port = server->port;
                ctx->cmd_args.volfile_server = server->volfile_server;
                ctx->cmd_args.volfile_server_transport = server->transport;

                ret = dict_set_str(rpc_trans->options, "transport-type",
                                   server->transport);
                if (ret != 0) {
                    gf_smsg("glfs-mgmt", GF_LOG_ERROR, ENOTCONN,
                            API_MSG_DICT_SET_FAILED, "transport-type=%s",
                            server->transport, NULL);
                    errno = ENOTCONN;
                    glfs_init_done(fs, -1);
                    break;
                }

                if (strcmp(server->transport, "unix") == 0) {
                    ret = dict_set_str(rpc_trans->options,
                                       "transport.socket.connect-path",
                                       server->volfile_server);
                    if (ret != 0) {
                        gf_smsg("glfs-mgmt", GF_LOG_ERROR, ENOTCONN,
                                API_MSG_DICT_SET_FAILED,
                                "socket.connect-path=%s",
                                server->volfile_server, NULL);
                        errno = ENOTCONN;
                        glfs_init_done(fs, -1);
                        break;
                    }
                    /* delete the remote-host and remote-port keys
                     * in case they were set while looping through
                     * list of volfile servers previously
                     */
                    dict_del(rpc_trans->options, "remote-host");
                    dict_del(rpc_trans->options, "remote-port");
                } else {
                    ret = dict_set_int32(rpc_trans->options, "remote-port",
                                         server->port);
                    if (ret != 0) {
                        gf_smsg("glfs-mgmt", GF_LOG_ERROR, ENOTCONN,
                                API_MSG_DICT_SET_FAILED, "remote-port=%d",
                                server->port, NULL);
                        errno = ENOTCONN;
                        glfs_init_done(fs, -1);
                        break;
                    }

                    ret = dict_set_str(rpc_trans->options, "remote-host",
                                       server->volfile_server);
                    if (ret != 0) {
                        gf_smsg("glfs-mgmt", GF_LOG_ERROR, ENOTCONN,
                                API_MSG_DICT_SET_FAILED, "remote-host=%s",
                                server->volfile_server, NULL);
                        errno = ENOTCONN;
                        glfs_init_done(fs, -1);
                        break;
                    }
                    /* delete the "transport.socket.connect-path"
                     * key in case if it was set while looping
                     * through list of volfile servers previously
                     */
                    dict_del(rpc_trans->options,
                             "transport.socket.connect-path");
                }

                gf_smsg("glfs-mgmt", GF_LOG_INFO, 0, API_MSG_VOLFILE_CONNECTING,
                        "server=%s", server->volfile_server, "port=%d",
                        server->port, "transport=%s", server->transport, NULL);
            }
            break;
        case RPC_CLNT_CONNECT:
            ret = glfs_volfile_fetch(fs);
            if (ret && (ctx->active == NULL)) {
                /* Do it only for the first time */
                /* Exit the process.. there are some wrong options */
                gf_smsg("glfs-mgmt", GF_LOG_ERROR, EINVAL,
                        API_MSG_GET_VOLFILE_FAILED, "key=%s",
                        ctx->cmd_args.volfile_id, NULL);
                errno = EINVAL;
                glfs_init_done(fs, -1);
            }

            break;
        default:
            break;
    }
out:
    return 0;
}

int
glusterfs_mgmt_notify(int32_t op, void *data, ...)
{
    int ret = 0;

    switch (op) {
        case GF_EN_DEFRAG_STATUS:
            break;

        default:
            break;
    }

    return ret;
}

int
glfs_mgmt_init(struct glfs *fs)
{
    cmd_args_t *cmd_args = NULL;
    struct rpc_clnt *rpc = NULL;
    dict_t *options = NULL;
    int ret = -1;
    int port = GF_DEFAULT_BASE_PORT;
    char *host = NULL;
    glusterfs_ctx_t *ctx = NULL;

    ctx = fs->ctx;
    cmd_args = &ctx->cmd_args;

    if (ctx->mgmt)
        return 0;

    options = dict_new();
    if (!options)
        goto out;

    if (cmd_args->volfile_server_port)
        port = cmd_args->volfile_server_port;

    if (cmd_args->volfile_server) {
        host = cmd_args->volfile_server;
    } else if (cmd_args->volfile_server_transport &&
               !strcmp(cmd_args->volfile_server_transport, "unix")) {
        host = DEFAULT_GLUSTERD_SOCKFILE;
    } else {
        host = "localhost";
    }

    if (cmd_args->volfile_server_transport &&
        !strcmp(cmd_args->volfile_server_transport, "unix")) {
        ret = rpc_transport_unix_options_build(options, host, 0);
    } else {
        xlator_cmdline_option_t *opt = find_xlator_option_in_cmd_args_t(
            "address-family", cmd_args);
        ret = rpc_transport_inet_options_build(options, host, port,
                                               (opt ? opt->value : NULL));
    }

    if (ret)
        goto out;

    rpc = rpc_clnt_new(options, THIS, THIS->name, 8);
    if (!rpc) {
        ret = -1;
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, API_MSG_CREATE_RPC_CLIENT_FAILED,
                NULL);
        goto out;
    }

    ret = rpc_clnt_register_notify(rpc, mgmt_rpc_notify, THIS);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, API_MSG_REG_NOTIFY_FUNC_FAILED,
                NULL);
        goto out;
    }

    ret = rpcclnt_cbk_program_register(rpc, &mgmt_cbk_prog, THIS);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, API_MSG_REG_CBK_FUNC_FAILED,
                NULL);
        goto out;
    }

    ctx->notify = glusterfs_mgmt_notify;

    /* This value should be set before doing the 'rpc_clnt_start()' as
       the notify function uses this variable */
    ctx->mgmt = rpc;

    ret = rpc_clnt_start(rpc);
out:
    if (options)
        dict_unref(options);
    return ret;
}
