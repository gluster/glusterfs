/*
   Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include <glusterfs/glusterfs.h>
#include <glusterfs/dict.h>
#include <glusterfs/gf-event.h>
#include <glusterfs/defaults.h>

#include "rpc-clnt.h"
#include "protocol-common.h"
#include "glusterfsd-messages.h"
#include "glusterfs3.h"
#include "portmap-xdr.h"
#include "xdr-generic.h"

#include "glusterfsd.h"
#include "rpcsvc.h"
#include "cli1-xdr.h"
#include <glusterfs/statedump.h>
#include <glusterfs/syncop.h>
#include <glusterfs/xlator.h>
#include <glusterfs/syscall.h>
#include <glusterfs/monitoring.h>
#include "server.h"

static gf_boolean_t is_mgmt_rpc_reconnect = _gf_false;
int need_emancipate = 0;

int
glusterfs_mgmt_pmap_signin(glusterfs_ctx_t *ctx);
int
glusterfs_volfile_fetch(glusterfs_ctx_t *ctx);
int
glusterfs_process_volfp(glusterfs_ctx_t *ctx, FILE *fp);
int
emancipate(glusterfs_ctx_t *ctx, int ret);
int
glusterfs_process_svc_attach_volfp(glusterfs_ctx_t *ctx, FILE *fp,
                                   char *volfile_id, char *checksum,
                                   dict_t *dict);
int
glusterfs_mux_volfile_reconfigure(FILE *newvolfile_fp, glusterfs_ctx_t *ctx,
                                  gf_volfile_t *volfile_obj, char *checksum,
                                  dict_t *dict);
int
glusterfs_process_svc_attach_volfp(glusterfs_ctx_t *ctx, FILE *fp,
                                   char *volfile_id, char *checksum,
                                   dict_t *dict);
int
glusterfs_process_svc_detach(glusterfs_ctx_t *ctx, gf_volfile_t *volfile_obj);

gf_boolean_t
mgmt_is_multiplexed_daemon(char *name);

static int
glusterfs_volume_top_perf(const char *brick_path, dict_t *dict,
                          gf_boolean_t write_test);

int
mgmt_cbk_spec(struct rpc_clnt *rpc, void *mydata, void *data)
{
    glusterfs_ctx_t *ctx = NULL;

    ctx = glusterfsd_ctx;
    gf_log("mgmt", GF_LOG_INFO, "Volume file changed");

    glusterfs_volfile_fetch(ctx);
    return 0;
}

int
mgmt_process_volfile(const char *volfile, ssize_t size, char *volfile_id,
                     dict_t *dict)
{
    glusterfs_ctx_t *ctx = NULL;
    int ret = 0;
    FILE *tmpfp = NULL;
    gf_volfile_t *volfile_obj = NULL;
    gf_volfile_t *volfile_tmp = NULL;
    char sha256_hash[SHA256_DIGEST_LENGTH] = {
        0,
    };
    int tmp_fd = -1;
    char template[] = "/tmp/glfs.volfile.XXXXXX";

    glusterfs_compute_sha256((const unsigned char *)volfile, size, sha256_hash);
    ctx = THIS->ctx;
    LOCK(&ctx->volfile_lock);
    {
        list_for_each_entry(volfile_obj, &ctx->volfile_list, volfile_list)
        {
            if (!strcmp(volfile_id, volfile_obj->vol_id)) {
                if (!memcmp(sha256_hash, volfile_obj->volfile_checksum,
                            sizeof(volfile_obj->volfile_checksum))) {
                    UNLOCK(&ctx->volfile_lock);
                    gf_smsg(THIS->name, GF_LOG_INFO, 0, glusterfsd_msg_40,
                            NULL);
                    goto out;
                }
                volfile_tmp = volfile_obj;
                break;
            }
        }

        /* coverity[secure_temp] mkstemp uses 0600 as the mode */
        tmp_fd = mkstemp(template);
        if (-1 == tmp_fd) {
            UNLOCK(&ctx->volfile_lock);
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, glusterfsd_msg_39,
                    "create template=%s", template, NULL);
            ret = -1;
            goto out;
        }

        /* Calling unlink so that when the file is closed or program
         * terminates the temporary file is deleted.
         */
        ret = sys_unlink(template);
        if (ret < 0) {
            gf_smsg(THIS->name, GF_LOG_INFO, 0, glusterfsd_msg_39,
                    "delete template=%s", template, NULL);
            ret = 0;
        }

        tmpfp = fdopen(tmp_fd, "w+b");
        if (!tmpfp) {
            ret = -1;
            goto unlock;
        }

        fwrite(volfile, size, 1, tmpfp);
        fflush(tmpfp);
        if (ferror(tmpfp)) {
            ret = -1;
            goto unlock;
        }

        if (!volfile_tmp) {
            /* There is no checksum in the list, which means simple attach
             * the volfile
             */
            ret = glusterfs_process_svc_attach_volfp(ctx, tmpfp, volfile_id,
                                                     sha256_hash, dict);
            goto unlock;
        }
        ret = glusterfs_mux_volfile_reconfigure(tmpfp, ctx, volfile_obj,
                                                sha256_hash, dict);
        if (ret < 0) {
            gf_msg_debug("glusterfsd-mgmt", EINVAL, "Reconfigure failed !!");
        }
    }
unlock:
    UNLOCK(&ctx->volfile_lock);
out:
    if (tmpfp)
        fclose(tmpfp);
    else if (tmp_fd != -1)
        sys_close(tmp_fd);
    return ret;
}

int
mgmt_cbk_event(struct rpc_clnt *rpc, void *mydata, void *data)
{
    return 0;
}

struct iobuf *
glusterfs_serialize_reply(rpcsvc_request_t *req, void *arg,
                          struct iovec *outmsg, xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    ssize_t retlen = -1;
    ssize_t xdr_size = 0;

    /* First, get the io buffer into which the reply in arg will
     * be serialized.
     */
    xdr_size = xdr_sizeof(xdrproc, arg);
    iob = iobuf_get2(req->svc->ctx->iobuf_pool, xdr_size);
    if (!iob) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to get iobuf");
        goto ret;
    }

    iobuf_to_iovec(iob, outmsg);
    /* Use the given serializer to translate the give C structure in arg
     * to XDR format which will be written into the buffer in outmsg.
     */
    /* retlen is used to received the error since size_t is unsigned and we
     * need -1 for error notification during encoding.
     */
    retlen = xdr_serialize_generic(*outmsg, arg, xdrproc);
    if (retlen == -1) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to encode message");
        GF_FREE(iob);
        goto ret;
    }

    outmsg->iov_len = retlen;
ret:
    if (retlen == -1) {
        iob = NULL;
    }

    return iob;
}

int
glusterfs_submit_reply(rpcsvc_request_t *req, void *arg, struct iovec *payload,
                       int payloadcount, struct iobref *iobref,
                       xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    int ret = -1;
    struct iovec rsp = {
        0,
    };
    char new_iobref = 0;

    if (!req) {
        GF_ASSERT(req);
        goto out;
    }

    if (!iobref) {
        iobref = iobref_new();
        if (!iobref) {
            gf_log(THIS->name, GF_LOG_ERROR, "out of memory");
            goto out;
        }

        new_iobref = 1;
    }

    iob = glusterfs_serialize_reply(req, arg, &rsp, xdrproc);
    if (!iob) {
        gf_log_callingfn(THIS->name, GF_LOG_ERROR, "Failed to serialize reply");
    } else {
        iobref_add(iobref, iob);
    }

    ret = rpcsvc_submit_generic(req, &rsp, 1, payload, payloadcount, iobref);

    /* Now that we've done our job of handing the message to the RPC layer
     * we can safely unref the iob in the hope that RPC layer must have
     * ref'ed the iob on receiving into the txlist.
     */
    if (ret == -1) {
        gf_log(THIS->name, GF_LOG_ERROR, "Reply submission failed");
        goto out;
    }

    ret = 0;
out:
    if (iob)
        iobuf_unref(iob);

    if (new_iobref && iobref)
        iobref_unref(iobref);

    return ret;
}

int
glusterfs_terminate_response_send(rpcsvc_request_t *req, int op_ret)
{
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;
    int ret = 0;

    rsp.op_ret = op_ret;
    rsp.op_errno = 0;
    rsp.op_errstr = "";
    dict = dict_new();

    if (dict)
        ret = dict_allocate_and_serialize(dict, &rsp.output.output_val,
                                          &rsp.output.output_len);

    if (ret == 0)
        ret = glusterfs_submit_reply(req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);

    GF_FREE(rsp.output.output_val);
    if (dict)
        dict_unref(dict);
    return ret;
}

int
glusterfs_handle_terminate(rpcsvc_request_t *req)
{
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    ssize_t ret;
    glusterfs_ctx_t *ctx = NULL;
    xlator_t *top = NULL;
    xlator_t *victim = NULL;
    xlator_t *tvictim = NULL;
    xlator_list_t **trav_p = NULL;
    gf_boolean_t lockflag = _gf_false;
    gf_boolean_t still_bricks_attached = _gf_false;

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        return -1;
    }
    ctx = glusterfsd_ctx;

    LOCK(&ctx->volfile_lock);
    {
        /* Find the xlator_list_t that points to our victim. */
        if (glusterfsd_ctx->active) {
            top = glusterfsd_ctx->active->first;
            for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
                victim = (*trav_p)->xlator;
                if (!victim->cleanup_starting &&
                    strcmp(victim->name, xlator_req.name) == 0) {
                    break;
                }
            }
        }

        if (!top)
            goto err;
    }
    if (!*trav_p) {
        gf_log(THIS->name, GF_LOG_ERROR, "can't terminate %s - not found",
               xlator_req.name);
        /*
         * Used to be -ENOENT.  However, the caller asked us to
         * make sure it's down and if it's already down that's
         * good enough.
         */
        glusterfs_terminate_response_send(req, 0);
        goto err;
    }

    glusterfs_terminate_response_send(req, 0);
    for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
        tvictim = (*trav_p)->xlator;
        if (!tvictim->cleanup_starting &&
            !strcmp(tvictim->name, xlator_req.name)) {
            continue;
        }
        if (!tvictim->cleanup_starting) {
            still_bricks_attached = _gf_true;
            break;
        }
    }
    if (!still_bricks_attached) {
        gf_log(THIS->name, GF_LOG_INFO,
               "terminating after loss of last child %s", xlator_req.name);
        rpc_clnt_mgmt_pmap_signout(glusterfsd_ctx, xlator_req.name);
        kill(getpid(), SIGTERM);
    } else {
        /* TODO cleanup sequence needs to be done properly for
           Quota and Changelog
        */
        if (victim->cleanup_starting)
            goto err;

        rpc_clnt_mgmt_pmap_signout(glusterfsd_ctx, xlator_req.name);
        victim->cleanup_starting = 1;

        UNLOCK(&ctx->volfile_lock);
        lockflag = _gf_true;

        gf_log(THIS->name, GF_LOG_INFO,
               "detaching not-only"
               " child %s",
               xlator_req.name);
        top->notify(top, GF_EVENT_CLEANUP, victim);
    }
err:
    if (!lockflag)
        UNLOCK(&ctx->volfile_lock);
    if (xlator_req.input.input_val)
        free(xlator_req.input.input_val);
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);
    free(xlator_req.name);
    xlator_req.name = NULL;
    return 0;
}

int
glusterfs_translator_info_response_send(rpcsvc_request_t *req, int ret,
                                        char *msg, dict_t *output)
{
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    gf_boolean_t free_ptr = _gf_false;
    GF_ASSERT(req);

    rsp.op_ret = ret;
    rsp.op_errno = 0;
    if (ret && msg && msg[0])
        rsp.op_errstr = msg;
    else
        rsp.op_errstr = "";

    ret = -1;
    if (output) {
        ret = dict_allocate_and_serialize(output, &rsp.output.output_val,
                                          &rsp.output.output_len);
    }
    if (!ret)
        free_ptr = _gf_true;

    glusterfs_submit_reply(req, &rsp, NULL, 0, NULL,
                           (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
    ret = 0;
    if (free_ptr)
        GF_FREE(rsp.output.output_val);
    return ret;
}

int
glusterfs_xlator_op_response_send(rpcsvc_request_t *req, int op_ret, char *msg,
                                  dict_t *output)
{
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    int ret = -1;
    gf_boolean_t free_ptr = _gf_false;
    GF_ASSERT(req);

    rsp.op_ret = op_ret;
    rsp.op_errno = 0;
    if (op_ret && msg && msg[0])
        rsp.op_errstr = msg;
    else
        rsp.op_errstr = "";

    if (output) {
        ret = dict_allocate_and_serialize(output, &rsp.output.output_val,
                                          &rsp.output.output_len);
    }
    if (!ret)
        free_ptr = _gf_true;

    ret = glusterfs_submit_reply(req, &rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);

    if (free_ptr)
        GF_FREE(rsp.output.output_val);

    return ret;
}

int
glusterfs_handle_translator_info_get(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    dict_t *dict = NULL;
    xlator_t *this = NULL;
    gf1_cli_top_op top_op = 0;
    xlator_t *any = NULL;
    xlator_t *xlator = NULL;
    glusterfs_graph_t *active = NULL;
    glusterfs_ctx_t *ctx = NULL;
    char msg[2048] = {
        0,
    };
    dict_t *output = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    dict = dict_new();
    ret = dict_unserialize(xlator_req.input.input_val,
                           xlator_req.input.input_len, &dict);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to "
               "unserialize req-buffer to dictionary");
        goto out;
    }

    ret = dict_get_int32(dict, "top-op", (int32_t *)&top_op);
    if (ret)
        goto cont;
    if (GF_CLI_TOP_READ_PERF == top_op) {
        ret = glusterfs_volume_top_perf(xlator_req.name, dict, _gf_false);
    } else if (GF_CLI_TOP_WRITE_PERF == top_op) {
        ret = glusterfs_volume_top_perf(xlator_req.name, dict, _gf_true);
    }

cont:
    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);
    active = ctx->active;
    if (active == NULL) {
        gf_log(THIS->name, GF_LOG_ERROR, "ctx->active returned NULL");
        ret = -1;
        goto out;
    }
    any = active->first;

    xlator = get_xlator_by_name(any, xlator_req.name);
    if (!xlator) {
        ret = -1;
        snprintf(msg, sizeof(msg), "xlator %s is not loaded", xlator_req.name);
        goto out;
    }

    if (strcmp(xlator->type, "debug/io-stats")) {
        xlator = get_xlator_by_type(xlator, "debug/io-stats");
        if (!xlator) {
            ret = -1;
            snprintf(msg, sizeof(msg),
                     "xlator-type debug/io-stats is not loaded");
            goto out;
        }
    }

    output = dict_new();
    ret = xlator->notify(xlator, GF_EVENT_TRANSLATOR_INFO, dict, output);

out:
    ret = glusterfs_translator_info_response_send(req, ret, msg, output);

    free(xlator_req.name);
    free(xlator_req.input.input_val);
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);
    if (output)
        dict_unref(output);
    if (dict)
        dict_unref(dict);
    return ret;
}

static int
glusterfs_volume_top_perf(const char *brick_path, dict_t *dict,
                          gf_boolean_t write_test)
{
    int32_t fd = -1;
    int32_t output_fd = -1;
    char export_path[PATH_MAX] = {
        0,
    };
    char *buf = NULL;
    int32_t iter = 0;
    int32_t ret = -1;
    uint64_t total_blks = 0;
    uint32_t blk_size;
    uint32_t blk_count;
    double throughput = 0;
    double time = 0;
    struct timeval begin, end = {
                              0,
                          };

    GF_ASSERT(brick_path);

    ret = dict_get_uint32(dict, "blk-size", &blk_size);
    if (ret)
        goto out;
    ret = dict_get_uint32(dict, "blk-cnt", &blk_count);
    if (ret)
        goto out;

    if (!(blk_size > 0) || !(blk_count > 0))
        goto out;

    buf = GF_CALLOC(1, blk_size * sizeof(*buf), gf_common_mt_char);
    if (!buf) {
        ret = -1;
        gf_log("glusterd", GF_LOG_ERROR, "Could not allocate memory");
        goto out;
    }

    snprintf(export_path, sizeof(export_path), "%s/%s", brick_path,
             ".gf-tmp-stats-perf");
    fd = open(export_path, O_CREAT | O_RDWR, S_IRWXU);
    if (-1 == fd) {
        ret = -1;
        gf_log("glusterd", GF_LOG_ERROR, "Could not open tmp file");
        goto out;
    }

    gettimeofday(&begin, NULL);
    for (iter = 0; iter < blk_count; iter++) {
        ret = sys_write(fd, buf, blk_size);
        if (ret != blk_size) {
            ret = -1;
            goto out;
        }
        total_blks += ret;
    }
    gettimeofday(&end, NULL);
    if (total_blks != ((uint64_t)blk_size * blk_count)) {
        gf_log("glusterd", GF_LOG_WARNING, "Error in write");
        ret = -1;
        goto out;
    }

    time = gf_tvdiff(&begin, &end);
    throughput = total_blks / time;
    gf_log("glusterd", GF_LOG_INFO,
           "Throughput %.2f Mbps time %.2f secs "
           "bytes written %" PRId64,
           throughput, time, total_blks);

    /* if it's a write test, we are done. Otherwise, we continue to the read
     * part */
    if (write_test == _gf_true) {
        ret = 0;
        goto out;
    }

    ret = sys_fsync(fd);
    if (ret) {
        gf_log("glusterd", GF_LOG_ERROR, "could not flush cache");
        goto out;
    }
    ret = sys_lseek(fd, 0L, 0);
    if (ret != 0) {
        gf_log("glusterd", GF_LOG_ERROR, "could not seek back to start");
        ret = -1;
        goto out;
    }

    output_fd = open("/dev/null", O_RDWR);
    if (-1 == output_fd) {
        ret = -1;
        gf_log("glusterd", GF_LOG_ERROR, "Could not open output file");
        goto out;
    }

    total_blks = 0;

    gettimeofday(&begin, NULL);
    for (iter = 0; iter < blk_count; iter++) {
        ret = sys_read(fd, buf, blk_size);
        if (ret != blk_size) {
            ret = -1;
            goto out;
        }
        ret = sys_write(output_fd, buf, blk_size);
        if (ret != blk_size) {
            ret = -1;
            goto out;
        }
        total_blks += ret;
    }
    gettimeofday(&end, NULL);
    if (total_blks != ((uint64_t)blk_size * blk_count)) {
        ret = -1;
        gf_log("glusterd", GF_LOG_WARNING, "Error in read");
        goto out;
    }

    time = gf_tvdiff(&begin, &end);
    throughput = total_blks / time;
    gf_log("glusterd", GF_LOG_INFO,
           "Throughput %.2f Mbps time %.2f secs "
           "bytes read %" PRId64,
           throughput, time, total_blks);
    ret = 0;
out:
    if (fd >= 0)
        sys_close(fd);
    if (output_fd >= 0)
        sys_close(output_fd);
    GF_FREE(buf);
    sys_unlink(export_path);
    if (ret == 0) {
        ret = dict_set_double(dict, "time", time);
        if (ret)
            goto end;
        ret = dict_set_double(dict, "throughput", throughput);
        if (ret)
            goto end;
    }
end:
    return ret;
}

int
glusterfs_handle_translator_op(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    int32_t op_ret = 0;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    dict_t *input = NULL;
    xlator_t *xlator = NULL;
    xlator_t *any = NULL;
    dict_t *output = NULL;
    char key[32] = {0};
    int len;
    char *xname = NULL;
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *this = NULL;
    int i = 0;
    int count = 0;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    ctx = glusterfsd_ctx;
    active = ctx->active;
    if (!active) {
        ret = -1;
        gf_smsg(this->name, GF_LOG_ERROR, EAGAIN, glusterfsd_msg_38,
                "brick-op_no.=%d", xlator_req.op, NULL);
        goto out;
    }
    any = active->first;
    input = dict_new();
    ret = dict_unserialize(xlator_req.input.input_val,
                           xlator_req.input.input_len, &input);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to "
               "unserialize req-buffer to dictionary");
        goto out;
    } else {
        input->extra_stdfree = xlator_req.input.input_val;
    }

    ret = dict_get_int32(input, "count", &count);

    output = dict_new();
    if (!output) {
        ret = -1;
        goto out;
    }

    for (i = 0; i < count; i++) {
        len = snprintf(key, sizeof(key), "xl-%d", i);
        ret = dict_get_strn(input, key, len, &xname);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "Couldn't get "
                   "xlator %s ",
                   key);
            goto out;
        }
        xlator = xlator_search_by_name(any, xname);
        if (!xlator) {
            gf_log(this->name, GF_LOG_ERROR,
                   "xlator %s is not "
                   "loaded",
                   xname);
            goto out;
        }
    }
    for (i = 0; i < count; i++) {
        len = snprintf(key, sizeof(key), "xl-%d", i);
        ret = dict_get_strn(input, key, len, &xname);
        xlator = xlator_search_by_name(any, xname);
        XLATOR_NOTIFY(ret, xlator, GF_EVENT_TRANSLATOR_OP, input, output);
        /* If notify fails for an xlator we need to capture it but
         * continue with the loop. */
        if (ret)
            op_ret = -1;
    }
    ret = op_ret;
out:
    glusterfs_xlator_op_response_send(req, ret, "", output);
    if (input)
        dict_unref(input);
    if (output)
        dict_unref(output);
    free(xlator_req.name);  // malloced by xdr

    return 0;
}

int
glusterfs_handle_bitrot(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    dict_t *input = NULL;
    dict_t *output = NULL;
    xlator_t *any = NULL;
    xlator_t *this = NULL;
    xlator_t *xlator = NULL;
    char msg[2048] = {
        0,
    };
    char xname[1024] = {
        0,
    };
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    char *scrub_opt = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);

    if (ret < 0) {
        /*failed to decode msg;*/
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);

    active = ctx->active;
    if (!active) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    any = active->first;

    input = dict_new();
    if (!input)
        goto out;

    ret = dict_unserialize(xlator_req.input.input_val,
                           xlator_req.input.input_len, &input);

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, glusterfsd_msg_35, NULL);
        goto out;
    }

    /* Send scrubber request to bitrot xlator */
    snprintf(xname, sizeof(xname), "%s-bit-rot-0", xlator_req.name);
    xlator = xlator_search_by_name(any, xname);
    if (!xlator) {
        snprintf(msg, sizeof(msg), "xlator %s is not loaded", xname);
        gf_smsg(this->name, GF_LOG_ERROR, 0, glusterfsd_msg_36, NULL);
        goto out;
    }

    output = dict_new();
    if (!output) {
        ret = -1;
        goto out;
    }

    ret = dict_get_str(input, "scrub-value", &scrub_opt);
    if (ret) {
        snprintf(msg, sizeof(msg), "Failed to get scrub value");
        gf_smsg(this->name, GF_LOG_ERROR, 0, glusterfsd_msg_37, NULL);
        ret = -1;
        goto out;
    }

    if (!strncmp(scrub_opt, "status", SLEN("status"))) {
        ret = xlator->notify(xlator, GF_EVENT_SCRUB_STATUS, input, output);
    } else if (!strncmp(scrub_opt, "ondemand", SLEN("ondemand"))) {
        ret = xlator->notify(xlator, GF_EVENT_SCRUB_ONDEMAND, input, output);
        if (ret == -2) {
            snprintf(msg, sizeof(msg),
                     "Scrubber is in "
                     "Pause/Inactive/Running state");
            ret = -1;
            goto out;
        }
    }
out:
    glusterfs_translator_info_response_send(req, ret, msg, output);

    if (input)
        dict_unref(input);
    free(xlator_req.input.input_val); /*malloced by xdr*/
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);
    if (output)
        dict_unref(output);
    free(xlator_req.name);

    return 0;
}

int
glusterfs_handle_attach(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    xlator_t *this = NULL;
    xlator_t *nextchild = NULL;
    glusterfs_graph_t *newgraph = NULL;
    glusterfs_ctx_t *ctx = NULL;
    xlator_t *srv_xl = NULL;
    server_conf_t *srv_conf = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ctx = this->ctx;
    if (!ctx->cmd_args.volfile_id) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "No volfile-id provided, erroring out");
        return -1;
    }

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);

    if (ret < 0) {
        /*failed to decode msg;*/
        req->rpc_err = GARBAGE_ARGS;
        return -1;
    }
    ret = 0;

    if (!this->ctx->active) {
        gf_log(this->name, GF_LOG_WARNING,
               "got attach for %s but no active graph", xlator_req.name);
        goto post_unlock;
    }

    gf_log(this->name, GF_LOG_INFO, "got attach for %s", xlator_req.name);

    LOCK(&ctx->volfile_lock);
    {
        ret = glusterfs_graph_attach(this->ctx->active, xlator_req.name,
                                     &newgraph);
        if (!ret && (newgraph && newgraph->first)) {
            nextchild = newgraph->first;
            ret = xlator_notify(nextchild, GF_EVENT_PARENT_UP, nextchild);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0, LG_MSG_EVENT_NOTIFY_FAILED,
                        "event=ParentUp", "name=%s", nextchild->name, NULL);
                goto unlock;
            }
            /* we need a protocol/server xlator as
             * nextchild
             */
            srv_xl = this->ctx->active->first;
            srv_conf = (server_conf_t *)srv_xl->private;
            rpcsvc_autoscale_threads(this->ctx, srv_conf->rpc, 1);
        }
        if (ret) {
            ret = -1;
        }
        ret = glusterfs_translator_info_response_send(req, ret, NULL, NULL);
        if (ret) {
            /* Response sent back to glusterd, req is already destroyed. So
             * resetting the ret to 0. Otherwise another response will be
             * send from rpcsvc_check_and_reply_error. Which will lead to
             * double resource leak.
             */
            ret = 0;
        }
    unlock:
        UNLOCK(&ctx->volfile_lock);
    }
post_unlock:
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);
    free(xlator_req.input.input_val);
    free(xlator_req.name);

    return ret;
}

int
glusterfs_handle_svc_attach(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    xlator_t *this = NULL;
    dict_t *dict = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);

    if (ret < 0) {
        /*failed to decode msg;*/
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    gf_smsg(THIS->name, GF_LOG_INFO, 0, glusterfsd_msg_41, "volfile-id=%s",
            xlator_req.name, NULL);

    dict = dict_new();
    if (!dict) {
        ret = -1;
        errno = ENOMEM;
        goto out;
    }

    ret = dict_unserialize(xlator_req.dict.dict_val, xlator_req.dict.dict_len,
                           &dict);
    if (ret) {
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, glusterfsd_msg_42, NULL);
        goto out;
    }
    dict->extra_stdfree = xlator_req.dict.dict_val;

    ret = 0;

    ret = mgmt_process_volfile(xlator_req.input.input_val,
                               xlator_req.input.input_len, xlator_req.name,
                               dict);
out:
    if (dict)
        dict_unref(dict);
    if (xlator_req.input.input_val)
        free(xlator_req.input.input_val);
    if (xlator_req.name)
        free(xlator_req.name);
    glusterfs_translator_info_response_send(req, ret, NULL, NULL);
    return 0;
}

int
glusterfs_handle_svc_detach(rpcsvc_request_t *req)
{
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    ssize_t ret;
    gf_volfile_t *volfile_obj = NULL;
    glusterfs_ctx_t *ctx = NULL;
    gf_volfile_t *volfile_tmp = NULL;

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        return -1;
    }
    ctx = glusterfsd_ctx;

    LOCK(&ctx->volfile_lock);
    {
        list_for_each_entry(volfile_obj, &ctx->volfile_list, volfile_list)
        {
            if (!strcmp(xlator_req.name, volfile_obj->vol_id)) {
                volfile_tmp = volfile_obj;
                break;
            }
        }

        if (!volfile_tmp) {
            UNLOCK(&ctx->volfile_lock);
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, glusterfsd_msg_041, "name=%s",
                    xlator_req.name, NULL);
            /*
             * Used to be -ENOENT.  However, the caller asked us to
             * make sure it's down and if it's already down that's
             * good enough.
             */
            ret = 0;
            goto out;
        }
        /* coverity[ORDER_REVERSAL] */
        ret = glusterfs_process_svc_detach(ctx, volfile_tmp);
        if (ret) {
            UNLOCK(&ctx->volfile_lock);
            gf_smsg("glusterfsd-mgmt", GF_LOG_ERROR, EINVAL, glusterfsd_msg_042,
                    NULL);
            goto out;
        }
    }
    UNLOCK(&ctx->volfile_lock);
out:
    glusterfs_terminate_response_send(req, ret);
    free(xlator_req.name);
    xlator_req.name = NULL;

    return 0;
}

int
glusterfs_handle_dump_metrics(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;
    char *filepath = NULL;
    int fd = -1;
    struct stat statbuf = {
        0,
    };
    char *msg = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);

    if (ret < 0) {
        /*failed to decode msg;*/
        req->rpc_err = GARBAGE_ARGS;
        return -1;
    }
    ret = -1;
    ctx = this->ctx;

    /* Infra for monitoring */
    filepath = gf_monitor_metrics(ctx);
    if (!filepath)
        goto out;

    fd = sys_open(filepath, O_RDONLY, 0);
    if (fd < 0)
        goto out;

    if (sys_fstat(fd, &statbuf) < 0)
        goto out;

    if (statbuf.st_size > GF_UNIT_MB) {
        gf_smsg(this->name, GF_LOG_WARNING, ENOMEM, LG_MSG_NO_MEMORY,
                "reconsider logic (%" PRId64 ")", statbuf.st_size, NULL);
    }
    msg = GF_CALLOC(1, (statbuf.st_size + 1), gf_common_mt_char);
    if (!msg)
        goto out;

    ret = sys_read(fd, msg, statbuf.st_size);
    if (ret < 0)
        goto out;

    /* Send all the data in errstr, instead of dictionary for now */
    glusterfs_translator_info_response_send(req, 0, msg, NULL);

    ret = 0;
out:
    if (fd >= 0)
        sys_close(fd);

    GF_FREE(msg);
    GF_FREE(filepath);
    if (xlator_req.input.input_val)
        free(xlator_req.input.input_val);
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);

    return ret;
}

int
glusterfs_handle_defrag(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    dict_t *dict = NULL;
    xlator_t *xlator = NULL;
    xlator_t *any = NULL;
    dict_t *output = NULL;
    char msg[2048] = {0};
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *this = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);

    active = ctx->active;
    if (!active) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    any = active->first;
    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }
    dict = dict_new();
    if (!dict)
        goto out;

    ret = dict_unserialize(xlator_req.input.input_val,
                           xlator_req.input.input_len, &dict);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to "
               "unserialize req-buffer to dictionary");
        goto out;
    }
    xlator = xlator_search_by_name(any, xlator_req.name);
    if (!xlator) {
        snprintf(msg, sizeof(msg), "xlator %s is not loaded", xlator_req.name);
        goto out;
    }

    output = dict_new();
    if (!output) {
        ret = -1;
        goto out;
    }

    ret = xlator->notify(xlator, GF_EVENT_VOLUME_DEFRAG, dict, output);

    ret = glusterfs_translator_info_response_send(req, ret, msg, output);
out:
    if (dict)
        dict_unref(dict);
    free(xlator_req.input.input_val);  // malloced by xdr
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);
    if (output)
        dict_unref(output);
    free(xlator_req.name);  // malloced by xdr

    return ret;
}
int
glusterfs_handle_brick_status(rpcsvc_request_t *req)
{
    int ret = -1;
    gd1_mgmt_brick_op_req brick_req = {
        0,
    };
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *this = NULL;
    xlator_t *server_xl = NULL;
    xlator_t *brick_xl = NULL;
    dict_t *dict = NULL;
    dict_t *output = NULL;
    uint32_t cmd = 0;
    char *msg = NULL;
    char *brickname = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ret = xdr_to_generic(req->msg[0], &brick_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    dict = dict_new();
    ret = dict_unserialize(brick_req.input.input_val, brick_req.input.input_len,
                           &dict);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "Failed to unserialize "
               "req-buffer to dictionary");
        goto out;
    }

    ret = dict_get_uint32(dict, "cmd", &cmd);
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR, "Couldn't get status op");
        goto out;
    }

    ret = dict_get_str(dict, "brick-name", &brickname);
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "Couldn't get brickname from"
               " dict");
        goto out;
    }

    ctx = glusterfsd_ctx;
    if (ctx == NULL) {
        gf_log(this->name, GF_LOG_ERROR, "ctx returned NULL");
        ret = -1;
        goto out;
    }
    if (ctx->active == NULL) {
        gf_log(this->name, GF_LOG_ERROR, "ctx->active returned NULL");
        ret = -1;
        goto out;
    }
    active = ctx->active;
    if (ctx->active->first == NULL) {
        gf_log(this->name, GF_LOG_ERROR,
               "ctx->active->first "
               "returned NULL");
        ret = -1;
        goto out;
    }
    server_xl = active->first;

    brick_xl = get_xlator_by_name(server_xl, brickname);
    if (!brick_xl) {
        gf_log(this->name, GF_LOG_ERROR, "xlator is not loaded");
        ret = -1;
        goto out;
    }

    output = dict_new();
    switch (cmd & GF_CLI_STATUS_MASK) {
        case GF_CLI_STATUS_MEM:
            ret = 0;
            gf_proc_dump_mem_info_to_dict(output);
            gf_proc_dump_mempool_info_to_dict(ctx, output);
            break;

        case GF_CLI_STATUS_CLIENTS:
        case GF_CLI_STATUS_CLIENT_LIST:
            ret = server_xl->dumpops->priv_to_dict(server_xl, output,
                                                   brickname);
            break;

        case GF_CLI_STATUS_INODE:
            ret = server_xl->dumpops->inode_to_dict(brick_xl, output);
            break;

        case GF_CLI_STATUS_FD:
            ret = server_xl->dumpops->fd_to_dict(brick_xl, output);
            break;

        case GF_CLI_STATUS_CALLPOOL:
            ret = 0;
            gf_proc_dump_pending_frames_to_dict(ctx->pool, output);
            break;

        default:
            ret = -1;
            msg = gf_strdup("Unknown status op");
            break;
    }
    rsp.op_ret = ret;
    rsp.op_errno = 0;
    if (ret && msg)
        rsp.op_errstr = msg;
    else
        rsp.op_errstr = "";

    ret = dict_allocate_and_serialize(output, &rsp.output.output_val,
                                      &rsp.output.output_len);
    if (ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "Failed to serialize output dict to rsp");
        goto out;
    }

    glusterfs_submit_reply(req, &rsp, NULL, 0, NULL,
                           (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
    ret = 0;

out:
    if (dict)
        dict_unref(dict);
    if (output)
        dict_unref(output);
    free(brick_req.input.input_val);
    if (brick_req.dict.dict_val)
        free(brick_req.dict.dict_val);
    free(brick_req.name);
    GF_FREE(msg);
    GF_FREE(rsp.output.output_val);

    return ret;
}

int
glusterfs_handle_node_status(rpcsvc_request_t *req)
{
    int ret = -1;
    gd1_mgmt_brick_op_req node_req = {
        0,
    };
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *any = NULL;
    xlator_t *node = NULL;
    xlator_t *subvol = NULL;
    dict_t *dict = NULL;
    dict_t *output = NULL;
    char *volname = NULL;
    char *node_name = NULL;
    char *subvol_name = NULL;
    uint32_t cmd = 0;
    char *msg = NULL;

    GF_ASSERT(req);

    ret = xdr_to_generic(req->msg[0], &node_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    dict = dict_new();
    ret = dict_unserialize(node_req.input.input_val, node_req.input.input_len,
                           &dict);
    if (ret < 0) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to unserialize "
               "req buffer to dictionary");
        goto out;
    }

    ret = dict_get_uint32(dict, "cmd", &cmd);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR, "Couldn't get status op");
        goto out;
    }

    ret = dict_get_str(dict, "volname", &volname);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR, "Couldn't get volname");
        goto out;
    }

    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);
    active = ctx->active;
    if (active == NULL) {
        gf_log(THIS->name, GF_LOG_ERROR, "ctx->active returned NULL");
        ret = -1;
        goto out;
    }
    any = active->first;

    if ((cmd & GF_CLI_STATUS_SHD) != 0)
        ret = gf_asprintf(&node_name, "%s", "glustershd");
#ifdef BUILD_GNFS
    else if ((cmd & GF_CLI_STATUS_NFS) != 0)
        ret = gf_asprintf(&node_name, "%s", "nfs-server");
#endif
    else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0)
        ret = gf_asprintf(&node_name, "%s", "quotad");
    else if ((cmd & GF_CLI_STATUS_BITD) != 0)
        ret = gf_asprintf(&node_name, "%s", "bitd");
    else if ((cmd & GF_CLI_STATUS_SCRUB) != 0)
        ret = gf_asprintf(&node_name, "%s", "scrubber");

    else {
        ret = -1;
        goto out;
    }
    if (ret == -1) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to set node xlator name");
        goto out;
    }

    node = xlator_search_by_name(any, node_name);
    if (!node) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR, "%s xlator is not loaded", node_name);
        goto out;
    }

    if ((cmd & GF_CLI_STATUS_NFS) != 0)
        ret = gf_asprintf(&subvol_name, "%s", volname);
    else if ((cmd & GF_CLI_STATUS_SHD) != 0)
        ret = gf_asprintf(&subvol_name, "%s-replicate-0", volname);
    else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0)
        ret = gf_asprintf(&subvol_name, "%s", volname);
    else if ((cmd & GF_CLI_STATUS_BITD) != 0)
        ret = gf_asprintf(&subvol_name, "%s", volname);
    else if ((cmd & GF_CLI_STATUS_SCRUB) != 0)
        ret = gf_asprintf(&subvol_name, "%s", volname);
    else {
        ret = -1;
        goto out;
    }
    if (ret == -1) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to set node xlator name");
        goto out;
    }

    subvol = xlator_search_by_name(node, subvol_name);
    if (!subvol) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR, "%s xlator is not loaded",
               subvol_name);
        goto out;
    }

    output = dict_new();
    switch (cmd & GF_CLI_STATUS_MASK) {
        case GF_CLI_STATUS_MEM:
            ret = 0;
            gf_proc_dump_mem_info_to_dict(output);
            gf_proc_dump_mempool_info_to_dict(ctx, output);
            break;

        case GF_CLI_STATUS_CLIENTS:
            // clients not available for SHD
            if ((cmd & GF_CLI_STATUS_SHD) != 0)
                break;

            ret = dict_set_str(output, "volname", volname);
            if (ret) {
                gf_log(THIS->name, GF_LOG_ERROR,
                       "Error setting volname to dict");
                goto out;
            }
            ret = node->dumpops->priv_to_dict(node, output, NULL);
            break;

        case GF_CLI_STATUS_INODE:
            ret = 0;
            inode_table_dump_to_dict(subvol->itable, "conn0", output);
            ret = dict_set_int32(output, "conncount", 1);
            break;

        case GF_CLI_STATUS_FD:
            // cannot find fd-tables in nfs-server graph
            // TODO: finish once found
            break;

        case GF_CLI_STATUS_CALLPOOL:
            ret = 0;
            gf_proc_dump_pending_frames_to_dict(ctx->pool, output);
            break;

        default:
            ret = -1;
            msg = gf_strdup("Unknown status op");
            gf_log(THIS->name, GF_LOG_ERROR, "%s", msg);
            break;
    }
    rsp.op_ret = ret;
    rsp.op_errno = 0;
    if (ret && msg)
        rsp.op_errstr = msg;
    else
        rsp.op_errstr = "";

    ret = dict_allocate_and_serialize(output, &rsp.output.output_val,
                                      &rsp.output.output_len);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to serialize output dict to rsp");
        goto out;
    }

    glusterfs_submit_reply(req, &rsp, NULL, 0, NULL,
                           (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
    ret = 0;

out:
    if (dict)
        dict_unref(dict);
    free(node_req.input.input_val);
    if (node_req.dict.dict_val)
        free(node_req.dict.dict_val);
    GF_FREE(msg);
    GF_FREE(rsp.output.output_val);
    GF_FREE(node_name);
    GF_FREE(subvol_name);

    gf_log(THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
    return ret;
}

int
glusterfs_handle_nfs_profile(rpcsvc_request_t *req)
{
    int ret = -1;
    gd1_mgmt_brick_op_req nfs_req = {
        0,
    };
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *any = NULL;
    xlator_t *nfs = NULL;
    xlator_t *subvol = NULL;
    char *volname = NULL;
    dict_t *output = NULL;

    GF_ASSERT(req);

    ret = xdr_to_generic(req->msg[0], &nfs_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    dict = dict_new();
    ret = dict_unserialize(nfs_req.input.input_val, nfs_req.input.input_len,
                           &dict);
    if (ret < 0) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to "
               "unserialize req-buffer to dict");
        goto out;
    }

    ret = dict_get_str(dict, "volname", &volname);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR, "Couldn't get volname");
        goto out;
    }

    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);

    active = ctx->active;
    if (active == NULL) {
        gf_log(THIS->name, GF_LOG_ERROR, "ctx->active returned NULL");
        ret = -1;
        goto out;
    }
    any = active->first;

    // is this needed?
    // are problems possible by searching for subvol directly from "any"?
    nfs = xlator_search_by_name(any, "nfs-server");
    if (!nfs) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR,
               "xlator nfs-server is "
               "not loaded");
        goto out;
    }

    subvol = xlator_search_by_name(nfs, volname);
    if (!subvol) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR, "xlator %s is no loaded", volname);
        goto out;
    }

    output = dict_new();
    ret = subvol->notify(subvol, GF_EVENT_TRANSLATOR_INFO, dict, output);

    rsp.op_ret = ret;
    rsp.op_errno = 0;
    rsp.op_errstr = "";

    ret = dict_allocate_and_serialize(output, &rsp.output.output_val,
                                      &rsp.output.output_len);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to serialize output dict to rsp");
        goto out;
    }

    glusterfs_submit_reply(req, &rsp, NULL, 0, NULL,
                           (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
    ret = 0;

out:
    free(nfs_req.input.input_val);
    if (nfs_req.dict.dict_val)
        free(nfs_req.dict.dict_val);
    if (dict)
        dict_unref(dict);
    if (output)
        dict_unref(output);
    GF_FREE(rsp.output.output_val);

    gf_log(THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
    return ret;
}

int
glusterfs_handle_volume_barrier_op(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    gd1_mgmt_brick_op_req xlator_req = {
        0,
    };
    dict_t *dict = NULL;
    xlator_t *xlator = NULL;
    xlator_t *any = NULL;
    dict_t *output = NULL;
    char msg[2048] = {0};
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *this = NULL;

    GF_ASSERT(req);
    this = THIS;
    GF_ASSERT(this);

    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);

    active = ctx->active;
    if (!active) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    any = active->first;
    ret = xdr_to_generic(req->msg[0], &xlator_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }
    dict = dict_new();
    if (!dict)
        goto out;

    ret = dict_unserialize(xlator_req.input.input_val,
                           xlator_req.input.input_len, &dict);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR,
               "failed to "
               "unserialize req-buffer to dictionary");
        goto out;
    }
    xlator = xlator_search_by_name(any, xlator_req.name);
    if (!xlator) {
        snprintf(msg, sizeof(msg), "xlator %s is not loaded", xlator_req.name);
        goto out;
    }

    output = dict_new();
    if (!output) {
        ret = -1;
        goto out;
    }

    ret = xlator->notify(xlator, GF_EVENT_VOLUME_BARRIER_OP, dict, output);

    ret = glusterfs_translator_info_response_send(req, ret, msg, output);
out:
    if (dict)
        dict_unref(dict);
    free(xlator_req.input.input_val);  // malloced by xdr
    if (xlator_req.dict.dict_val)
        free(xlator_req.dict.dict_val);
    if (output)
        dict_unref(output);
    free(xlator_req.name);  // malloced by xdr

    return ret;
}

int
glusterfs_handle_barrier(rpcsvc_request_t *req)
{
    int ret = -1;
    gd1_mgmt_brick_op_req brick_req = {
        0,
    };
    gd1_mgmt_brick_op_rsp brick_rsp = {
        0,
    };
    glusterfs_ctx_t *ctx = NULL;
    glusterfs_graph_t *active = NULL;
    xlator_t *top = NULL;
    xlator_t *xlator = NULL;
    xlator_t *old_THIS = NULL;
    dict_t *dict = NULL;
    gf_boolean_t barrier = _gf_true;
    xlator_list_t *trav;

    GF_ASSERT(req);

    ret = xdr_to_generic(req->msg[0], &brick_req,
                         (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        goto out;
    }

    ctx = glusterfsd_ctx;
    GF_ASSERT(ctx);
    active = ctx->active;
    if (active == NULL) {
        gf_log(THIS->name, GF_LOG_ERROR, "ctx->active returned NULL");
        ret = -1;
        goto out;
    }
    top = active->first;

    for (trav = top->children; trav; trav = trav->next) {
        if (strcmp(trav->xlator->name, brick_req.name) == 0) {
            break;
        }
    }
    if (!trav) {
        ret = -1;
        goto out;
    }
    top = trav->xlator;

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(brick_req.input.input_val, brick_req.input.input_len,
                           &dict);
    if (ret < 0) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to unserialize "
               "request dictionary");
        goto out;
    }

    brick_rsp.op_ret = 0;
    brick_rsp.op_errstr = "";  // initing to prevent serilaztion failures
    old_THIS = THIS;

    /* Send barrier request to the barrier xlator */
    xlator = get_xlator_by_type(top, "features/barrier");
    if (!xlator) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR, "%s xlator is not loaded",
               "features/barrier");
        goto out;
    }

    THIS = xlator;
    // TODO: Extend this to accept return of errnos
    ret = xlator->notify(xlator, GF_EVENT_TRANSLATOR_OP, dict);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR, "barrier notify failed");
        brick_rsp.op_ret = ret;
        brick_rsp.op_errstr = gf_strdup(
            "Failed to reconfigure "
            "barrier.");
        /* This is to invoke changelog-barrier disable if barrier
         * disable fails and don't invoke if barrier enable fails.
         */
        barrier = dict_get_str_boolean(dict, "barrier", _gf_true);
        if (barrier)
            goto submit_reply;
    }

    /* Reset THIS so that we have it correct in case of an error below
     */
    THIS = old_THIS;

    /* Send barrier request to changelog as well */
    xlator = get_xlator_by_type(top, "features/changelog");
    if (!xlator) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR, "%s xlator is not loaded",
               "features/changelog");
        goto out;
    }

    THIS = xlator;
    ret = xlator->notify(xlator, GF_EVENT_TRANSLATOR_OP, dict);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR, "changelog notify failed");
        brick_rsp.op_ret = ret;
        brick_rsp.op_errstr = gf_strdup("changelog notify failed");
        goto submit_reply;
    }

submit_reply:
    THIS = old_THIS;

    ret = glusterfs_submit_reply(req, &brick_rsp, NULL, 0, NULL,
                                 (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);

out:
    if (dict)
        dict_unref(dict);
    free(brick_req.input.input_val);
    if (brick_req.dict.dict_val)
        free(brick_req.dict.dict_val);
    gf_log(THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
    return ret;
}

int
glusterfs_handle_rpc_msg(rpcsvc_request_t *req)
{
    int ret = -1;
    /* for now, nothing */
    return ret;
}

static rpcclnt_cb_actor_t mgmt_cbk_actors[GF_CBK_MAXVALUE] = {
    [GF_CBK_FETCHSPEC] = {"FETCHSPEC", mgmt_cbk_spec, GF_CBK_FETCHSPEC},
    [GF_CBK_EVENT_NOTIFY] = {"EVENTNOTIFY", mgmt_cbk_event,
                             GF_CBK_EVENT_NOTIFY},
    [GF_CBK_STATEDUMP] = {"STATEDUMP", mgmt_cbk_event, GF_CBK_STATEDUMP},
};

static struct rpcclnt_cb_program mgmt_cbk_prog = {
    .progname = "GlusterFS Callback",
    .prognum = GLUSTER_CBK_PROGRAM,
    .progver = GLUSTER_CBK_VERSION,
    .actors = mgmt_cbk_actors,
    .numactors = GF_CBK_MAXVALUE,
};

static char *clnt_pmap_procs[GF_PMAP_MAXVALUE] = {
    [GF_PMAP_NULL] = "NULL",
    [GF_PMAP_PORTBYBRICK] = "PORTBYBRICK",
    [GF_PMAP_BRICKBYPORT] = "BRICKBYPORT",
    [GF_PMAP_SIGNIN] = "SIGNIN",
    [GF_PMAP_SIGNOUT] = "SIGNOUT",
    [GF_PMAP_SIGNUP] = "SIGNUP", /* DEPRECATED - DON'T USE! */
};

static rpc_clnt_prog_t clnt_pmap_prog = {
    .progname = "Gluster Portmap",
    .prognum = GLUSTER_PMAP_PROGRAM,
    .progver = GLUSTER_PMAP_VERSION,
    .procnames = clnt_pmap_procs,
};

static char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
    [GF_HNDSK_NULL] = "NULL",
    [GF_HNDSK_SETVOLUME] = "SETVOLUME",
    [GF_HNDSK_GETSPEC] = "GETSPEC",
    [GF_HNDSK_PING] = "PING",
    [GF_HNDSK_EVENT_NOTIFY] = "EVENTNOTIFY",
};

static rpc_clnt_prog_t clnt_handshake_prog = {
    .progname = "GlusterFS Handshake",
    .prognum = GLUSTER_HNDSK_PROGRAM,
    .progver = GLUSTER_HNDSK_VERSION,
    .procnames = clnt_handshake_procs,
};

static rpcsvc_actor_t glusterfs_actors[GLUSTERD_BRICK_MAXVALUE] = {
    [GLUSTERD_BRICK_NULL] = {"NULL", glusterfs_handle_rpc_msg, NULL,
                             GLUSTERD_BRICK_NULL, DRC_NA, 0},
    [GLUSTERD_BRICK_TERMINATE] = {"TERMINATE", glusterfs_handle_terminate, NULL,
                                  GLUSTERD_BRICK_TERMINATE, DRC_NA, 0},
    [GLUSTERD_BRICK_XLATOR_INFO] = {"TRANSLATOR INFO",
                                    glusterfs_handle_translator_info_get, NULL,
                                    GLUSTERD_BRICK_XLATOR_INFO, DRC_NA, 0},
    [GLUSTERD_BRICK_XLATOR_OP] = {"TRANSLATOR OP",
                                  glusterfs_handle_translator_op, NULL,
                                  GLUSTERD_BRICK_XLATOR_OP, DRC_NA, 0},
    [GLUSTERD_BRICK_STATUS] = {"STATUS", glusterfs_handle_brick_status, NULL,
                               GLUSTERD_BRICK_STATUS, DRC_NA, 0},
    [GLUSTERD_BRICK_XLATOR_DEFRAG] = {"TRANSLATOR DEFRAG",
                                      glusterfs_handle_defrag, NULL,
                                      GLUSTERD_BRICK_XLATOR_DEFRAG, DRC_NA, 0},
    [GLUSTERD_NODE_PROFILE] = {"NFS PROFILE", glusterfs_handle_nfs_profile,
                               NULL, GLUSTERD_NODE_PROFILE, DRC_NA, 0},
    [GLUSTERD_NODE_STATUS] = {"NFS STATUS", glusterfs_handle_node_status, NULL,
                              GLUSTERD_NODE_STATUS, DRC_NA, 0},
    [GLUSTERD_VOLUME_BARRIER_OP] = {"VOLUME BARRIER OP",
                                    glusterfs_handle_volume_barrier_op, NULL,
                                    GLUSTERD_VOLUME_BARRIER_OP, DRC_NA, 0},
    [GLUSTERD_BRICK_BARRIER] = {"BARRIER", glusterfs_handle_barrier, NULL,
                                GLUSTERD_BRICK_BARRIER, DRC_NA, 0},
    [GLUSTERD_NODE_BITROT] = {"BITROT", glusterfs_handle_bitrot, NULL,
                              GLUSTERD_NODE_BITROT, DRC_NA, 0},
    [GLUSTERD_BRICK_ATTACH] = {"ATTACH", glusterfs_handle_attach, NULL,
                               GLUSTERD_BRICK_ATTACH, DRC_NA, 0},

    [GLUSTERD_DUMP_METRICS] = {"DUMP METRICS", glusterfs_handle_dump_metrics,
                               NULL, GLUSTERD_DUMP_METRICS, DRC_NA, 0},

    [GLUSTERD_SVC_ATTACH] = {"ATTACH CLIENT", glusterfs_handle_svc_attach, NULL,
                             GLUSTERD_SVC_ATTACH, DRC_NA, 0},

    [GLUSTERD_SVC_DETACH] = {"DETACH CLIENT", glusterfs_handle_svc_detach, NULL,
                             GLUSTERD_SVC_DETACH, DRC_NA, 0},

};

static struct rpcsvc_program glusterfs_mop_prog = {
    .progname = "Gluster Brick operations",
    .prognum = GD_BRICK_PROGRAM,
    .progver = GD_BRICK_VERSION,
    .actors = glusterfs_actors,
    .numactors = GLUSTERD_BRICK_MAXVALUE,
    .synctask = _gf_true,
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
            gf_log(THIS->name, GF_LOG_WARNING, "failed to create XDR payload");
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

int
mgmt_getspec_cbk(struct rpc_req *req, struct iovec *iov, int count,
                 void *myframe)
{
    gf_getspec_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int ret = 0, locked = 0;
    ssize_t size = 0;
    FILE *tmpfp = NULL;
    char *volfile_id = NULL;
    gf_volfile_t *volfile_obj = NULL;
    gf_volfile_t *volfile_tmp = NULL;
    char sha256_hash[SHA256_DIGEST_LENGTH] = {
        0,
    };
    dict_t *dict = NULL;
    char *servers_list = NULL;
    int tmp_fd = -1;
    char template[] = "/tmp/glfs.volfile.XXXXXX";

    frame = myframe;
    ctx = frame->this->ctx;

    if (-1 == req->rpc_status) {
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, "XDR decoding error");
        ret = -1;
        goto out;
    }

    if (-1 == rsp.op_ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to get the 'volume file' from server");
        ret = rsp.op_errno;
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

    ret = dict_get_str(dict, "servers-list", &servers_list);
    if (ret) {
        /* Server list is set by glusterd at the time of getspec */
        ret = dict_get_str(dict, GLUSTERD_BRICK_SERVERS, &servers_list);
        if (ret)
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
    size = rsp.op_ret;
    volfile_id = frame->local;
    if (mgmt_is_multiplexed_daemon(ctx->cmd_args.process_name)) {
        ret = mgmt_process_volfile((const char *)rsp.spec, size, volfile_id,
                                   dict);
        goto post_graph_mgmt;
    }

    ret = 0;
    glusterfs_compute_sha256((const unsigned char *)rsp.spec, size,
                             sha256_hash);

    LOCK(&ctx->volfile_lock);
    {
        locked = 1;

        list_for_each_entry(volfile_obj, &ctx->volfile_list, volfile_list)
        {
            if (!strcmp(volfile_id, volfile_obj->vol_id)) {
                if (!memcmp(sha256_hash, volfile_obj->volfile_checksum,
                            sizeof(volfile_obj->volfile_checksum))) {
                    UNLOCK(&ctx->volfile_lock);
                    gf_log(frame->this->name, GF_LOG_INFO,
                           "No change in volfile,"
                           "continuing");
                    goto post_unlock;
                }
                volfile_tmp = volfile_obj;
                break;
            }
        }

        /* coverity[secure_temp] mkstemp uses 0600 as the mode */
        tmp_fd = mkstemp(template);
        if (-1 == tmp_fd) {
            UNLOCK(&ctx->volfile_lock);
            gf_smsg(frame->this->name, GF_LOG_ERROR, 0, glusterfsd_msg_39,
                    "create template=%s", template, NULL);
            ret = -1;
            goto post_unlock;
        }

        /* Calling unlink so that when the file is closed or program
         * terminates the temporary file is deleted.
         */
        ret = sys_unlink(template);
        if (ret < 0) {
            gf_smsg(frame->this->name, GF_LOG_INFO, 0, glusterfsd_msg_39,
                    "delete template=%s", template, NULL);
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
         *  return 0          =======> reconfiguration of options has succeeded
         *  return 1          =======> the graph has to be reconstructed and all
         * the xlators should be inited return -1(or -ve) =======> Some Internal
         * Error occurred during the operation
         */

        ret = glusterfs_volfile_reconfigure(tmpfp, ctx);
        if (ret == 0) {
            gf_log("glusterfsd-mgmt", GF_LOG_DEBUG,
                   "No need to re-load volfile, reconfigure done");
            if (!volfile_tmp) {
                ret = -1;
                UNLOCK(&ctx->volfile_lock);
                gf_log("mgmt", GF_LOG_ERROR,
                       "Graph reconfigure succeeded with out having "
                       "checksum.");
                goto post_unlock;
            }
            memcpy(volfile_tmp->volfile_checksum, sha256_hash,
                   sizeof(volfile_tmp->volfile_checksum));
            goto out;
        }

        if (ret < 0) {
            UNLOCK(&ctx->volfile_lock);
            gf_log("glusterfsd-mgmt", GF_LOG_DEBUG, "Reconfigure failed !!");
            goto post_unlock;
        }

        ret = glusterfs_process_volfp(ctx, tmpfp);
        /* tmpfp closed */
        tmpfp = NULL;
        tmp_fd = -1;
        if (ret)
            goto out;

        if (!volfile_tmp) {
            volfile_tmp = GF_CALLOC(1, sizeof(gf_volfile_t),
                                    gf_common_volfile_t);
            if (!volfile_tmp) {
                ret = -1;
                goto out;
            }

            INIT_LIST_HEAD(&volfile_tmp->volfile_list);
            volfile_tmp->graph = ctx->active;
            list_add(&volfile_tmp->volfile_list, &ctx->volfile_list);
            snprintf(volfile_tmp->vol_id, sizeof(volfile_tmp->vol_id), "%s",
                     volfile_id);
        }
        memcpy(volfile_tmp->volfile_checksum, sha256_hash,
               sizeof(volfile_tmp->volfile_checksum));
    }
    UNLOCK(&ctx->volfile_lock);

    locked = 0;

post_graph_mgmt:
    if (!is_mgmt_rpc_reconnect) {
        need_emancipate = 1;
        glusterfs_mgmt_pmap_signin(ctx);
        is_mgmt_rpc_reconnect = _gf_true;
    }

out:

    if (locked)
        UNLOCK(&ctx->volfile_lock);
post_unlock:
    GF_FREE(frame->local);
    frame->local = NULL;
    STACK_DESTROY(frame->root);
    free(rsp.spec);

    if (dict)
        dict_unref(dict);

    // Stop if server is running at an unsupported op-version
    if (ENOTSUP == ret) {
        gf_log("mgmt", GF_LOG_ERROR,
               "Server is operating at an "
               "op-version which is not supported");
        cleanup_and_exit(0);
    }

    if (ret && ctx && !ctx->active) {
        /* Do it only for the first time */
        /* Failed to get the volume file, something wrong,
           restart the process */
        gf_log("mgmt", GF_LOG_ERROR, "failed to fetch volume file (key:%s)",
               ctx->cmd_args.volfile_id);
        cleanup_and_exit(0);
    }

    if (tmpfp)
        fclose(tmpfp);
    else if (tmp_fd != -1)
        sys_close(tmp_fd);

    return 0;
}

static int
glusterfs_volfile_fetch_one(glusterfs_ctx_t *ctx, char *volfile_id)
{
    cmd_args_t *cmd_args = NULL;
    gf_getspec_req req = {
        0,
    };
    int ret = 0;
    call_frame_t *frame = NULL;
    dict_t *dict = NULL;

    cmd_args = &ctx->cmd_args;
    if (!volfile_id) {
        volfile_id = ctx->cmd_args.volfile_id;
        if (!volfile_id) {
            gf_log(THIS->name, GF_LOG_ERROR,
                   "No volfile-id provided, erroring out");
            return -1;
        }
    }

    frame = create_frame(THIS, ctx->pool);
    if (!frame) {
        ret = -1;
        goto out;
    }

    req.key = volfile_id;
    req.flags = 0;
    /*
     * We are only storing one variable in local, hence using the same
     * variable. If multiple local variable is required, create a struct.
     */
    frame->local = gf_strdup(volfile_id);
    if (!frame->local) {
        ret = -1;
        goto out;
    }

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    // Set the supported min and max op-versions, so glusterd can make a
    // decision
    ret = dict_set_int32(dict, "min-op-version", GD_OP_VERSION_MIN);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to set min-op-version"
               " in request dict");
        goto out;
    }

    ret = dict_set_int32(dict, "max-op-version", GD_OP_VERSION_MAX);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to set max-op-version"
               " in request dict");
        goto out;
    }

    /* Ask for a list of volfile (glusterd2 only) servers */
    if (GF_CLIENT_PROCESS == ctx->process_mode) {
        req.flags = req.flags | GF_GETSPEC_FLAG_SERVERS_LIST;
    }

    if (cmd_args->brick_name) {
        ret = dict_set_dynstr_with_alloc(dict, "brick_name",
                                         cmd_args->brick_name);
        if (ret) {
            gf_log(THIS->name, GF_LOG_ERROR,
                   "Failed to set brick_name in request dict");
            goto out;
        }
    }

    ret = dict_allocate_and_serialize(dict, &req.xdata.xdata_val,
                                      &req.xdata.xdata_len);
    if (ret < 0) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to serialize dictionary");
        goto out;
    }

    ret = mgmt_submit_request(&req, frame, ctx, &clnt_handshake_prog,
                              GF_HNDSK_GETSPEC, mgmt_getspec_cbk,
                              (xdrproc_t)xdr_gf_getspec_req);

out:
    GF_FREE(req.xdata.xdata_val);
    if (dict)
        dict_unref(dict);
    if (ret && frame) {
        /* Free the frame->local fast, because we have not used memget
         */
        GF_FREE(frame->local);
        frame->local = NULL;
        STACK_DESTROY(frame->root);
    }

    return ret;
}

int
glusterfs_volfile_fetch(glusterfs_ctx_t *ctx)
{
    xlator_t *server_xl = NULL;
    xlator_list_t *trav;
    gf_volfile_t *volfile_obj = NULL;
    int ret = 0;

    LOCK(&ctx->volfile_lock);
    {
        if (ctx->active &&
            mgmt_is_multiplexed_daemon(ctx->cmd_args.process_name)) {
            list_for_each_entry(volfile_obj, &ctx->volfile_list, volfile_list)
            {
                ret |= glusterfs_volfile_fetch_one(ctx, volfile_obj->vol_id);
            }
            UNLOCK(&ctx->volfile_lock);
            return ret;
        }

        if (ctx->active) {
            server_xl = ctx->active->first;
            if (strcmp(server_xl->type, "protocol/server") != 0) {
                server_xl = NULL;
            }
        }
        if (!server_xl) {
            /* Startup (ctx->active not set) or non-server. */
            UNLOCK(&ctx->volfile_lock);
            return glusterfs_volfile_fetch_one(ctx, ctx->cmd_args.volfile_id);
        }

        ret = 0;
        for (trav = server_xl->children; trav; trav = trav->next) {
            ret |= glusterfs_volfile_fetch_one(ctx, trav->xlator->volfile_id);
        }
    }
    UNLOCK(&ctx->volfile_lock);
    return ret;
}

int32_t
mgmt_event_notify_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    gf_event_notify_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    int ret = 0;

    frame = myframe;

    if (-1 == req->rpc_status) {
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_event_notify_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, "XDR decoding error");
        ret = -1;
        goto out;
    }

    if (-1 == rsp.op_ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to get the rsp from server");
        ret = -1;
        goto out;
    }
out:
    free(rsp.dict.dict_val);  // malloced by xdr
    return ret;
}

int32_t
glusterfs_rebalance_event_notify_cbk(struct rpc_req *req, struct iovec *iov,
                                     int count, void *myframe)
{
    gf_event_notify_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    int ret = 0;

    frame = myframe;

    if (-1 == req->rpc_status) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to get the rsp from server");
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_event_notify_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, "XDR decoding error");
        ret = -1;
        goto out;
    }

    if (-1 == rsp.op_ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "Received error (%s) from server", strerror(rsp.op_errno));
        ret = -1;
        goto out;
    }
out:
    free(rsp.dict.dict_val);  // malloced by xdr

    if (frame) {
        STACK_DESTROY(frame->root);
    }

    return ret;
}

int32_t
glusterfs_rebalance_event_notify(dict_t *dict)
{
    glusterfs_ctx_t *ctx = NULL;
    gf_event_notify_req req = {
        0,
    };
    int32_t ret = -1;
    cmd_args_t *cmd_args = NULL;
    call_frame_t *frame = NULL;

    ctx = glusterfsd_ctx;
    cmd_args = &ctx->cmd_args;

    frame = create_frame(THIS, ctx->pool);

    req.op = GF_EN_DEFRAG_STATUS;

    if (dict) {
        ret = dict_set_str(dict, "volname", cmd_args->volfile_id);
        if (ret) {
            gf_log("", GF_LOG_ERROR, "failed to set volname");
        }
        ret = dict_allocate_and_serialize(dict, &req.dict.dict_val,
                                          &req.dict.dict_len);
        if (ret) {
            gf_log("", GF_LOG_ERROR, "failed to serialize dict");
        }
    }

    ret = mgmt_submit_request(&req, frame, ctx, &clnt_handshake_prog,
                              GF_HNDSK_EVENT_NOTIFY,
                              glusterfs_rebalance_event_notify_cbk,
                              (xdrproc_t)xdr_gf_event_notify_req);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int
mgmt_rpc_notify(struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                void *data)
{
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int ret = 0;
    server_cmdline_t *server = NULL;
    rpc_transport_t *rpc_trans = NULL;
    int need_term = 0;
    int emval = 0;
    static int log_ctr1;
    static int log_ctr2;
    struct dnscache6 *dnscache = NULL;

    this = mydata;
    rpc_trans = rpc->conn.trans;
    ctx = this->ctx;

    switch (event) {
        case RPC_CLNT_DISCONNECT:
            if (rpc_trans->connect_failed) {
                GF_LOG_OCCASIONALLY(log_ctr1, "glusterfsd-mgmt", GF_LOG_ERROR,
                                    "failed to connect to remote-"
                                    "host: %s",
                                    ctx->cmd_args.volfile_server);
            } else {
                GF_LOG_OCCASIONALLY(log_ctr1, "glusterfsd-mgmt", GF_LOG_INFO,
                                    "disconnected from remote-"
                                    "host: %s",
                                    ctx->cmd_args.volfile_server);
            }

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
                if (!ctx->active) {
                    need_term = 1;
                }
                emval = ENOTCONN;
                GF_LOG_OCCASIONALLY(log_ctr2, "glusterfsd-mgmt", GF_LOG_INFO,
                                    "Exhausted all volfile servers");
                break;
            }
            server = list_entry(server->list.next, typeof(*server), list);
            ctx->cmd_args.curr_server = server;
            ctx->cmd_args.volfile_server = server->volfile_server;

            ret = dict_set_str(rpc_trans->options, "remote-host",
                               server->volfile_server);
            if (ret != 0) {
                gf_log("glusterfsd-mgmt", GF_LOG_ERROR,
                       "failed to set remote-host: %s", server->volfile_server);
                if (!ctx->active) {
                    need_term = 1;
                }
                emval = ENOTCONN;
                break;
            }
            gf_log("glusterfsd-mgmt", GF_LOG_INFO,
                   "connecting to next volfile server %s",
                   server->volfile_server);
            break;
        case RPC_CLNT_CONNECT:
            ret = glusterfs_volfile_fetch(ctx);
            if (ret) {
                emval = ret;
                if (!ctx->active) {
                    need_term = 1;
                    gf_log("glusterfsd-mgmt", GF_LOG_ERROR,
                           "failed to fetch volume file (key:%s)",
                           ctx->cmd_args.volfile_id);
                    break;
                }
            }

            if (is_mgmt_rpc_reconnect)
                glusterfs_mgmt_pmap_signin(ctx);

            break;
        default:
            break;
    }

    if (need_term) {
        emancipate(ctx, emval);
        cleanup_and_exit(1);
    }

    return 0;
}

int
glusterfs_rpcsvc_notify(rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                        void *data)
{
    if (!xl || !data) {
        goto out;
    }

    switch (event) {
        case RPCSVC_EVENT_ACCEPT: {
            break;
        }
        case RPCSVC_EVENT_DISCONNECT: {
            break;
        }

        default:
            break;
    }

out:
    return 0;
}

int
glusterfs_listener_init(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;
    rpcsvc_t *rpc = NULL;
    dict_t *options = NULL;
    int ret = -1;

    cmd_args = &ctx->cmd_args;

    if (ctx->listener)
        return 0;

    if (!cmd_args->sock_file)
        return 0;

    options = dict_new();
    if (!options)
        goto out;

    ret = rpcsvc_transport_unix_options_build(options, cmd_args->sock_file);
    if (ret)
        goto out;

    rpc = rpcsvc_init(THIS, ctx, options, 8);
    if (rpc == NULL) {
        goto out;
    }

    ret = rpcsvc_register_notify(rpc, glusterfs_rpcsvc_notify, THIS);
    if (ret) {
        goto out;
    }

    ret = rpcsvc_create_listeners(rpc, options, "glusterfsd");
    if (ret < 1) {
        goto out;
    }

    ret = rpcsvc_program_register(rpc, &glusterfs_mop_prog, _gf_false);
    if (ret) {
        goto out;
    }

    ctx->listener = rpc;

out:
    if (options)
        dict_unref(options);
    return ret;
}

int
glusterfs_mgmt_notify(int32_t op, void *data, ...)
{
    int ret = 0;
    switch (op) {
        case GF_EN_DEFRAG_STATUS:
            ret = glusterfs_rebalance_event_notify((dict_t *)data);
            break;

        default:
            gf_log("", GF_LOG_ERROR, "Invalid op");
            break;
    }

    return ret;
}

int
glusterfs_mgmt_init(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;
    struct rpc_clnt *rpc = NULL;
    dict_t *options = NULL;
    int ret = -1;
    int port = GF_DEFAULT_BASE_PORT;
    char *host = NULL;
    xlator_cmdline_option_t *opt = NULL;

    cmd_args = &ctx->cmd_args;
    GF_VALIDATE_OR_GOTO(THIS->name, cmd_args->volfile_server, out);

    if (ctx->mgmt)
        return 0;

    options = dict_new();
    if (!options)
        goto out;

    LOCK_INIT(&ctx->volfile_lock);

    if (cmd_args->volfile_server_port)
        port = cmd_args->volfile_server_port;

    host = cmd_args->volfile_server;

    if (cmd_args->volfile_server_transport &&
        !strcmp(cmd_args->volfile_server_transport, "unix")) {
        ret = rpc_transport_unix_options_build(options, host, 0);
    } else {
        opt = find_xlator_option_in_cmd_args_t("address-family", cmd_args);
        ret = rpc_transport_inet_options_build(options, host, port,
                                               (opt ? opt->value : NULL));
    }
    if (ret)
        goto out;

    /* Explicitly turn on encrypted transport. */
    if (ctx->secure_mgmt) {
        ret = dict_set_dynstr_with_alloc(options,
                                         "transport.socket.ssl-enabled", "yes");
        if (ret) {
            gf_log(THIS->name, GF_LOG_ERROR,
                   "failed to set 'transport.socket.ssl-enabled' "
                   "in options dict");
            goto out;
        }

        ctx->ssl_cert_depth = glusterfs_read_secure_access_file();
    }

    rpc = rpc_clnt_new(options, THIS, THIS->name, 8);
    if (!rpc) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_WARNING, "failed to create rpc clnt");
        goto out;
    }

    ret = rpc_clnt_register_notify(rpc, mgmt_rpc_notify, THIS);
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING,
               "failed to register notify function");
        goto out;
    }

    ret = rpcclnt_cbk_program_register(rpc, &mgmt_cbk_prog, THIS);
    if (ret) {
        gf_log(THIS->name, GF_LOG_WARNING,
               "failed to register callback function");
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

static int
mgmt_pmap_signin2_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    pmap_signin_rsp rsp = {
        0,
    };
    glusterfs_ctx_t *ctx = NULL;
    call_frame_t *frame = NULL;
    int ret = 0;

    ctx = glusterfsd_ctx;
    frame = myframe;

    if (-1 == req->rpc_status) {
        ret = -1;
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_pmap_signin_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, "XDR decode error");
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    if (-1 == rsp.op_ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to register the port with glusterd");
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    if (need_emancipate)
        emancipate(ctx, ret);

    STACK_DESTROY(frame->root);
    return 0;
}

static int
mgmt_pmap_signin_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    pmap_signin_rsp rsp = {
        0,
    };
    call_frame_t *frame = NULL;
    int ret = 0;
    int emancipate_ret = -1;
    pmap_signin_req pmap_req = {
        0,
    };
    cmd_args_t *cmd_args = NULL;
    glusterfs_ctx_t *ctx = NULL;
    char brick_name[PATH_MAX] = {
        0,
    };

    frame = myframe;
    ctx = glusterfsd_ctx;
    cmd_args = &ctx->cmd_args;

    if (-1 == req->rpc_status) {
        ret = -1;
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_pmap_signin_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, "XDR decode error");
        rsp.op_ret = -1;
        rsp.op_errno = EINVAL;
        goto out;
    }

    if (-1 == rsp.op_ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to register the port with glusterd");
        ret = -1;
        goto out;
    }

    if (!cmd_args->brick_port2) {
        /* We are done with signin process */
        emancipate_ret = 0;
        goto out;
    }

    snprintf(brick_name, PATH_MAX, "%s.rdma", cmd_args->brick_name);
    pmap_req.port = cmd_args->brick_port2;
    pmap_req.brick = brick_name;

    ret = mgmt_submit_request(&pmap_req, frame, ctx, &clnt_pmap_prog,
                              GF_PMAP_SIGNIN, mgmt_pmap_signin2_cbk,
                              (xdrproc_t)xdr_pmap_signin_req);
    if (ret)
        goto out;

    return 0;

out:
    if (need_emancipate && (ret < 0 || !cmd_args->brick_port2))
        emancipate(ctx, emancipate_ret);

    STACK_DESTROY(frame->root);
    return 0;
}

int
glusterfs_mgmt_pmap_signin(glusterfs_ctx_t *ctx)
{
    call_frame_t *frame = NULL;
    xlator_list_t **trav_p;
    xlator_t *top;
    pmap_signin_req req = {
        0,
    };
    int ret = -1;
    int emancipate_ret = -1;
    cmd_args_t *cmd_args = NULL;

    cmd_args = &ctx->cmd_args;

    if (!cmd_args->brick_port || !cmd_args->brick_name) {
        gf_log("fsd-mgmt", GF_LOG_DEBUG,
               "portmapper signin arguments not given");
        emancipate_ret = 0;
        goto out;
    }

    req.port = cmd_args->brick_port;
    req.pid = (int)getpid(); /* only glusterd2 consumes this */

    if (ctx->active) {
        top = ctx->active->first;
        for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
            frame = create_frame(THIS, ctx->pool);
            req.brick = (*trav_p)->xlator->name;
            ret = mgmt_submit_request(&req, frame, ctx, &clnt_pmap_prog,
                                      GF_PMAP_SIGNIN, mgmt_pmap_signin_cbk,
                                      (xdrproc_t)xdr_pmap_signin_req);
            if (ret < 0) {
                gf_log(THIS->name, GF_LOG_WARNING,
                       "failed to send sign in request; brick = %s", req.brick);
            }
        }
    }

    /* unfortunately, the caller doesn't care about the returned value */

out:
    if (need_emancipate && ret < 0)
        emancipate(ctx, emancipate_ret);
    return ret;
}
