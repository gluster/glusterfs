/*
   Copyright (c) 2012-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
/* rpc related syncops */
#include "glusterd-syncop.h"
#include "glusterd-mgmt.h"

#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-server-quorum.h"
#include "glusterd-locks.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-messages.h"
#include "glusterd-errno.h"

extern glusterd_op_info_t opinfo;

void
gd_synctask_barrier_wait(struct syncargs *args, int count)
{
    glusterd_conf_t *conf = THIS->private;

    synclock_unlock(&conf->big_lock);
    synctask_barrier_wait(args, count);
    synclock_lock(&conf->big_lock);
}

static void
gd_collate_errors(struct syncargs *args, int op_ret, int op_errno,
                  char *op_errstr, int op_code, uuid_t peerid, u_char *uuid)
{
    char err_str[PATH_MAX] = "Please check log file for details.";
    char op_err[PATH_MAX] = "";
    int len = -1;
    char *peer_str = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;

    if (op_ret) {
        args->op_ret = op_ret;
        args->op_errno = op_errno;

        RCU_READ_LOCK;
        peerinfo = glusterd_peerinfo_find(peerid, NULL);
        if (peerinfo)
            peer_str = gf_strdup(peerinfo->hostname);
        else
            peer_str = gf_strdup(uuid_utoa(uuid));
        RCU_READ_UNLOCK;

        if (op_errstr && strcmp(op_errstr, "")) {
            len = snprintf(err_str, sizeof(err_str) - 1, "Error: %s",
                           op_errstr);
            err_str[len] = '\0';
        }

        switch (op_code) {
            case GLUSTERD_MGMT_CLUSTER_LOCK: {
                len = snprintf(op_err, sizeof(op_err) - 1,
                               "Locking failed on %s. %s", peer_str, err_str);
                break;
            }
            case GLUSTERD_MGMT_CLUSTER_UNLOCK: {
                len = snprintf(op_err, sizeof(op_err) - 1,
                               "Unlocking failed on %s. %s", peer_str, err_str);
                break;
            }
            case GLUSTERD_MGMT_STAGE_OP: {
                len = snprintf(op_err, sizeof(op_err) - 1,
                               "Staging failed on %s. %s", peer_str, err_str);
                break;
            }
            case GLUSTERD_MGMT_COMMIT_OP: {
                len = snprintf(op_err, sizeof(op_err) - 1,
                               "Commit failed on %s. %s", peer_str, err_str);
                break;
            }
        }

        if (len > 0)
            op_err[len] = '\0';

        if (args->errstr) {
            len = snprintf(err_str, sizeof(err_str) - 1, "%s\n%s", args->errstr,
                           op_err);
            GF_FREE(args->errstr);
            args->errstr = NULL;
        } else
            len = snprintf(err_str, sizeof(err_str) - 1, "%s", op_err);
        err_str[len] = '\0';

        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MGMT_OP_FAIL, "%s", op_err);
        args->errstr = gf_strdup(err_str);
    }

    GF_FREE(peer_str);

    return;
}

int
gd_syncargs_init(struct syncargs *args, dict_t *op_ctx)
{
    int ret = 0;

    ret = pthread_mutex_init(&args->lock_dict, NULL);
    if (ret)
        return ret;

    ret = synctask_barrier_init(args);
    if (ret) {
        pthread_mutex_destroy(&args->lock_dict);
        return ret;
    }

    args->dict = op_ctx;
    return 0;
}

void
gd_syncargs_fini(struct syncargs *args)
{
    if (args->barrier.initialized) {
        pthread_mutex_destroy(&args->lock_dict);
        syncbarrier_destroy(&args->barrier);
    }
}

static void
gd_stage_op_req_free(gd1_mgmt_stage_op_req *req)
{
    if (!req)
        return;

    GF_FREE(req->buf.buf_val);
    GF_FREE(req);
}

static void
gd_commit_op_req_free(gd1_mgmt_commit_op_req *req)
{
    if (!req)
        return;

    GF_FREE(req->buf.buf_val);
    GF_FREE(req);
}

static void
gd_brick_op_req_free(gd1_mgmt_brick_op_req *req)
{
    if (!req)
        return;

    if (req->dict.dict_val)
        GF_FREE(req->dict.dict_val);
    GF_FREE(req->input.input_val);
    GF_FREE(req);
}

int
gd_syncop_submit_request(struct rpc_clnt *rpc, void *req, void *local,
                         void *cookie, rpc_clnt_prog_t *prog, int procnum,
                         fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
    int ret = -1;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    int count = 0;
    struct iovec iov = {
        0,
    };
    ssize_t req_size = 0;
    call_frame_t *frame = NULL;

    GF_ASSERT(rpc);
    if (!req)
        goto out;

    req_size = xdr_sizeof(xdrproc, req);
    iobuf = iobuf_get2(rpc->ctx->iobuf_pool, req_size);
    if (!iobuf)
        goto out;

    iobref = iobref_new();
    if (!iobref)
        goto out;

    frame = create_frame(THIS, THIS->ctx->pool);
    if (!frame)
        goto out;

    iobref_add(iobref, iobuf);

    iov.iov_base = iobuf->ptr;
    iov.iov_len = iobuf_pagesize(iobuf);

    /* Create the xdr payload */
    ret = xdr_serialize_generic(iov, req, xdrproc);
    if (ret == -1)
        goto out;

    iov.iov_len = ret;
    count = 1;

    frame->local = local;
    frame->cookie = cookie;

    /* Send the msg */
    ret = rpc_clnt_submit(rpc, prog, procnum, cbkfn, &iov, count, NULL, 0,
                          iobref, frame, NULL, 0, NULL, 0, NULL);

    /* TODO: do we need to start ping also? */
    /* In case of error the frame will be destroy by rpc_clnt_submit */
    frame = NULL;

out:
    iobref_unref(iobref);
    iobuf_unref(iobuf);

    if (ret && frame)
        STACK_DESTROY(frame->root);
    return ret;
}

/* Defined in glusterd-rpc-ops.c */
extern struct rpc_clnt_program gd_mgmt_prog;
extern struct rpc_clnt_program gd_brick_prog;
extern struct rpc_clnt_program gd_mgmt_v3_prog;

static int32_t
glusterd_append_gsync_status(dict_t *dst, dict_t *src)
{
    int ret = 0;
    char *stop_msg = NULL;

    ret = dict_get_str(src, "gsync-status", &stop_msg);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=gsync-status", NULL);
        ret = 0;
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(dst, "gsync-status", stop_msg);
    if (ret) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set the stop"
               "message in the ctx dictionary");
        goto out;
    }

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_append_status_dicts(dict_t *dst, dict_t *src)
{
    char sts_val_name[PATH_MAX] = "";
    int dst_count = 0;
    int src_count = 0;
    int i = 0;
    int ret = 0;
    gf_gsync_status_t *sts_val = NULL;
    gf_gsync_status_t *dst_sts_val = NULL;

    GF_ASSERT(dst);

    if (src == NULL)
        goto out;

    ret = dict_get_int32(dst, "gsync-count", &dst_count);
    if (ret)
        dst_count = 0;

    ret = dict_get_int32(src, "gsync-count", &src_count);
    if (ret || !src_count) {
        gf_msg_debug("glusterd", 0, "Source brick empty");
        ret = 0;
        goto out;
    }

    for (i = 0; i < src_count; i++) {
        snprintf(sts_val_name, sizeof(sts_val_name), "status_value%d", i);

        ret = dict_get_bin(src, sts_val_name, (void **)&sts_val);
        if (ret)
            goto out;

        dst_sts_val = GF_MALLOC(sizeof(gf_gsync_status_t),
                                gf_common_mt_gsync_status_t);
        if (!dst_sts_val) {
            gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Out Of Memory");
            goto out;
        }

        memcpy(dst_sts_val, sts_val, sizeof(gf_gsync_status_t));

        snprintf(sts_val_name, sizeof(sts_val_name), "status_value%d",
                 i + dst_count);

        ret = dict_set_bin(dst, sts_val_name, dst_sts_val,
                           sizeof(gf_gsync_status_t));
        if (ret) {
            GF_FREE(dst_sts_val);
            goto out;
        }
    }

    ret = dict_set_int32_sizen(dst, "gsync-count", dst_count + src_count);

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_gsync_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict, char *op_errstr)
{
    dict_t *ctx = NULL;
    int ret = 0;
    char *conf_path = NULL;

    if (aggr) {
        ctx = aggr;

    } else {
        ctx = glusterd_op_get_ctx();
        if (!ctx) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_OPCTX_GET_FAIL,
                   "Operation Context is not present");
            GF_ASSERT(0);
        }
    }

    if (rsp_dict) {
        ret = glusterd_append_status_dicts(ctx, rsp_dict);
        if (ret)
            goto out;

        ret = glusterd_append_gsync_status(ctx, rsp_dict);
        if (ret)
            goto out;

        ret = dict_get_str(rsp_dict, "conf_path", &conf_path);
        if (!ret && conf_path) {
            ret = dict_set_dynstr_with_alloc(ctx, "conf_path", conf_path);
            if (ret) {
                gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Unable to store conf path.");
                goto out;
            }
        }
    }
    if ((op_errstr) && (strcmp("", op_errstr))) {
        ret = dict_set_dynstr_with_alloc(ctx, "errstr", op_errstr);
        if (ret)
            goto out;
    }

    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d ", ret);
    return ret;
}

static int
glusterd_max_opversion_use_rsp_dict(dict_t *dst, dict_t *src)
{
    int ret = -1;
    int src_max_opversion = -1;
    int max_opversion = -1;

    GF_VALIDATE_OR_GOTO(THIS->name, dst, out);
    GF_VALIDATE_OR_GOTO(THIS->name, src, out);

    ret = dict_get_int32(dst, "max-opversion", &max_opversion);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Maximum supported op-version not set in destination "
               "dictionary");

    ret = dict_get_int32(src, "max-opversion", &src_max_opversion);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get maximum supported op-version from source");
        goto out;
    }

    if (max_opversion == -1 || src_max_opversion < max_opversion)
        max_opversion = src_max_opversion;

    ret = dict_set_int32_sizen(dst, "max-opversion", max_opversion);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set max op-version");
        goto out;
    }
out:
    return ret;
}

static int
glusterd_volume_bitrot_scrub_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = -1;
    int j = 0;
    uint64_t value = 0;
    char key[64] = "";
    int keylen;
    char *last_scrub_time = NULL;
    char *scrub_time = NULL;
    char *volname = NULL;
    char *node_uuid = NULL;
    char *node_uuid_str = NULL;
    char *bitd_log = NULL;
    char *scrub_log = NULL;
    char *scrub_freq = NULL;
    char *scrub_state = NULL;
    char *scrub_impact = NULL;
    char *bad_gfid_str = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int src_count = 0;
    int dst_count = 0;
    int8_t scrub_running = 0;

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_str(aggr, "volname", &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Unable to find volinfo for volume: %s", volname);
        goto out;
    }

    ret = dict_get_int32(aggr, "count", &dst_count);

    ret = dict_get_int32(rsp_dict, "count", &src_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get count value");
        ret = 0;
        goto out;
    }

    ret = dict_set_int32_sizen(aggr, "count", src_count + dst_count);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set count in dictonary");

    keylen = snprintf(key, sizeof(key), "node-uuid-%d", src_count);
    ret = dict_get_strn(rsp_dict, key, keylen, &node_uuid);
    if (!ret) {
        node_uuid_str = gf_strdup(node_uuid);
        keylen = snprintf(key, sizeof(key), "node-uuid-%d",
                          src_count + dst_count);
        ret = dict_set_dynstrn(aggr, key, keylen, node_uuid_str);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set node-uuid");
        }
    }

    snprintf(key, sizeof(key), "scrub-running-%d", src_count);
    ret = dict_get_int8(rsp_dict, key, &scrub_running);
    if (!ret) {
        snprintf(key, sizeof(key), "scrub-running-%d", src_count + dst_count);
        ret = dict_set_int8(aggr, key, scrub_running);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-running value");
        }
    }

    snprintf(key, sizeof(key), "scrubbed-files-%d", src_count);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "scrubbed-files-%d", src_count + dst_count);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrubbed-file value");
        }
    }

    snprintf(key, sizeof(key), "unsigned-files-%d", src_count);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "unsigned-files-%d", src_count + dst_count);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "unsigned-file value");
        }
    }

    keylen = snprintf(key, sizeof(key), "last-scrub-time-%d", src_count);
    ret = dict_get_strn(rsp_dict, key, keylen, &last_scrub_time);
    if (!ret) {
        scrub_time = gf_strdup(last_scrub_time);
        keylen = snprintf(key, sizeof(key), "last-scrub-time-%d",
                          src_count + dst_count);
        ret = dict_set_dynstrn(aggr, key, keylen, scrub_time);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "last scrub time value");
        }
    }

    snprintf(key, sizeof(key), "scrub-duration-%d", src_count);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "scrub-duration-%d", src_count + dst_count);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrubbed-duration value");
        }
    }

    snprintf(key, sizeof(key), "error-count-%d", src_count);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "error-count-%d", src_count + dst_count);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set error "
                         "count value");
        }

        /* Storing all the bad files in the dictionary */
        for (j = 0; j < value; j++) {
            keylen = snprintf(key, sizeof(key), "quarantine-%d-%d", j,
                              src_count);
            ret = dict_get_strn(rsp_dict, key, keylen, &bad_gfid_str);
            if (!ret) {
                snprintf(key, sizeof(key), "quarantine-%d-%d", j,
                         src_count + dst_count);
                ret = dict_set_dynstr_with_alloc(aggr, key, bad_gfid_str);
                if (ret) {
                    gf_msg_debug(this->name, 0,
                                 "Failed to"
                                 "bad file gfid ");
                }
            }
        }
    }

    ret = dict_get_str(rsp_dict, "bitrot_log_file", &bitd_log);
    if (!ret) {
        ret = dict_set_dynstr_with_alloc(aggr, "bitrot_log_file", bitd_log);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "bitrot log file location");
            goto out;
        }
    }

    ret = dict_get_str(rsp_dict, "scrub_log_file", &scrub_log);
    if (!ret) {
        ret = dict_set_dynstr_with_alloc(aggr, "scrub_log_file", scrub_log);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrubber log file location");
            goto out;
        }
    }

    ret = dict_get_str(rsp_dict, "features.scrub-freq", &scrub_freq);
    if (!ret) {
        ret = dict_set_dynstr_with_alloc(aggr, "features.scrub-freq",
                                         scrub_freq);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-frequency value to dictionary");
            goto out;
        }
    }

    ret = dict_get_str(rsp_dict, "features.scrub-throttle", &scrub_impact);
    if (!ret) {
        ret = dict_set_dynstr_with_alloc(aggr, "features.scrub-throttle",
                                         scrub_impact);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-throttle value to dictionary");
            goto out;
        }
    }

    ret = dict_get_str(rsp_dict, "features.scrub", &scrub_state);
    if (!ret) {
        ret = dict_set_dynstr_with_alloc(aggr, "features.scrub", scrub_state);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub state value to dictionary");
            goto out;
        }
    }

    ret = 0;
out:
    return ret;
}

static int
glusterd_sys_exec_output_rsp_dict(dict_t *dst, dict_t *src)
{
    char output_name[64] = "";
    char *output = NULL;
    int ret = 0;
    int i = 0;
    int keylen;
    int src_output_count = 0;
    int dst_output_count = 0;

    if (!dst || !src) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_EMPTY,
               "Source or Destination "
               "dict is empty.");
        goto out;
    }

    ret = dict_get_int32(dst, "output_count", &dst_output_count);

    ret = dict_get_int32(src, "output_count", &src_output_count);
    if (ret) {
        gf_msg_debug("glusterd", 0, "No output from source");
        ret = 0;
        goto out;
    }

    for (i = 1; i <= src_output_count; i++) {
        keylen = snprintf(output_name, sizeof(output_name), "output_%d", i);
        if (keylen <= 0 || keylen >= sizeof(output_name)) {
            ret = -1;
            goto out;
        }
        ret = dict_get_strn(src, output_name, keylen, &output);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to fetch %s", output_name);
            goto out;
        }

        keylen = snprintf(output_name, sizeof(output_name), "output_%d",
                          i + dst_output_count);
        if (keylen <= 0 || keylen >= sizeof(output_name)) {
            ret = -1;
            goto out;
        }

        ret = dict_set_dynstrn(dst, output_name, keylen, gf_strdup(output));
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set %s", output_name);
            goto out;
        }
    }

    ret = dict_set_int32_sizen(dst, "output_count",
                               dst_output_count + src_output_count);
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = 0;

    GF_ASSERT(aggr);
    GF_ASSERT(rsp_dict);

    if (aggr)
        dict_copy(rsp_dict, aggr);

    return ret;
}

static int
glusterd_volume_heal_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = 0;
    dict_t *ctx_dict = NULL;
    uuid_t *txn_id = NULL;
    glusterd_op_info_t txn_op_info = {
        GD_OP_STATE_DEFAULT,
    };
    glusterd_op_t op = GD_OP_NONE;

    GF_ASSERT(rsp_dict);

    ret = dict_get_bin(aggr, "transaction_id", (void **)&txn_id);
    if (ret)
        goto out;
    gf_msg_debug(THIS->name, 0, "transaction ID = %s", uuid_utoa(*txn_id));

    ret = glusterd_get_txn_opinfo(txn_id, &txn_op_info);
    if (ret) {
        gf_msg_callingfn(THIS->name, GF_LOG_ERROR, 0,
                         GD_MSG_TRANS_OPINFO_GET_FAIL,
                         "Unable to get transaction opinfo "
                         "for transaction ID : %s",
                         uuid_utoa(*txn_id));
        goto out;
    }

    op = txn_op_info.op;
    GF_ASSERT(GD_OP_HEAL_VOLUME == op);

    if (aggr) {
        ctx_dict = aggr;

    } else {
        ctx_dict = txn_op_info.op_ctx;
    }

    if (!ctx_dict)
        goto out;
    dict_copy(rsp_dict, ctx_dict);
out:
    return ret;
}

static int
glusterd_volume_quota_copy_to_op_ctx_dict(dict_t *dict, dict_t *rsp_dict)
{
    int ret = -1;
    int i = 0;
    int count = 0;
    int rsp_dict_count = 0;
    char *uuid_str = NULL;
    char *uuid_str_dup = NULL;
    char key[64] = "";
    int keylen;
    xlator_t *this = THIS;
    int type = GF_QUOTA_OPTION_TYPE_NONE;

    ret = dict_get_int32(dict, "type", &type);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get quota opcode");
        goto out;
    }

    if ((type != GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) &&
        (type != GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS) &&
        (type != GF_QUOTA_OPTION_TYPE_REMOVE) &&
        (type != GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS)) {
        dict_copy(rsp_dict, dict);
        ret = 0;
        goto out;
    }

    ret = dict_get_int32(rsp_dict, "count", &rsp_dict_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get the count of "
               "gfids from the rsp dict");
        goto out;
    }

    ret = dict_get_int32(dict, "count", &count);
    if (ret)
        /* The key "count" is absent in op_ctx when this function is
         * called after self-staging on the originator. This must not
         * be treated as error.
         */
        gf_msg_debug(this->name, 0,
                     "Failed to get count of gfids"
                     " from req dict. This could be because count is not yet"
                     " copied from rsp_dict into op_ctx");

    for (i = 0; i < rsp_dict_count; i++) {
        keylen = snprintf(key, sizeof(key), "gfid%d", i);
        ret = dict_get_strn(rsp_dict, key, keylen, &uuid_str);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get gfid "
                   "from rsp dict");
            goto out;
        }

        uuid_str_dup = gf_strdup(uuid_str);
        if (!uuid_str_dup) {
            ret = -1;
            goto out;
        }

        keylen = snprintf(key, sizeof(key), "gfid%d", i + count);
        ret = dict_set_dynstrn(dict, key, keylen, uuid_str_dup);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set gfid "
                   "from rsp dict into req dict");
            GF_FREE(uuid_str_dup);
            goto out;
        }
    }

    ret = dict_set_int32_sizen(dict, "count", rsp_dict_count + count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set aggregated "
               "count in req dict");
        goto out;
    }

out:
    return ret;
}

int
glusterd_syncop_aggr_rsp_dict(glusterd_op_t op, dict_t *aggr, dict_t *rsp)
{
    int ret = 0;

    switch (op) {
        case GD_OP_CREATE_VOLUME:
        case GD_OP_ADD_BRICK:
        case GD_OP_START_VOLUME:
            ret = glusterd_aggr_brick_mount_dirs(aggr, rsp);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, 0,
                       GD_MSG_BRICK_MOUNDIRS_AGGR_FAIL,
                       "Failed to "
                       "aggregate brick mount dirs");
                goto out;
            }
            break;

        case GD_OP_REPLACE_BRICK:
        case GD_OP_RESET_BRICK:
            ret = glusterd_rb_use_rsp_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_SYNC_VOLUME:
            ret = glusterd_sync_use_rsp_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_GSYNC_CREATE:
            break;

        case GD_OP_GSYNC_SET:
            ret = glusterd_gsync_use_rsp_dict(aggr, rsp, NULL);
            if (ret)
                goto out;
            break;

        case GD_OP_STATUS_VOLUME:
            ret = glusterd_volume_status_copy_to_op_ctx_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_HEAL_VOLUME:
            ret = glusterd_volume_heal_use_rsp_dict(aggr, rsp);
            if (ret)
                goto out;

            break;

        case GD_OP_CLEARLOCKS_VOLUME:
            ret = glusterd_use_rsp_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_QUOTA:
            ret = glusterd_volume_quota_copy_to_op_ctx_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_SYS_EXEC:
            ret = glusterd_sys_exec_output_rsp_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_SNAP:
            ret = glusterd_snap_use_rsp_dict(aggr, rsp);
            if (ret)
                goto out;
            break;

        case GD_OP_SCRUB_STATUS:
            ret = glusterd_volume_bitrot_scrub_use_rsp_dict(aggr, rsp);
            break;

        case GD_OP_SCRUB_ONDEMAND:
            break;

        case GD_OP_MAX_OPVERSION:
            ret = glusterd_max_opversion_use_rsp_dict(aggr, rsp);
            break;

        case GD_OP_PROFILE_VOLUME:
            ret = glusterd_profile_volume_use_rsp_dict(aggr, rsp);
            break;

        case GD_OP_REBALANCE:
        case GD_OP_DEFRAG_BRICK_VOLUME:
            ret = glusterd_volume_rebalance_use_rsp_dict(aggr, rsp);
            break;

        default:
            break;
    }
out:
    return ret;
}

int32_t
gd_syncop_mgmt_v3_lock_cbk_fn(struct rpc_req *req, struct iovec *iov, int count,
                              void *myframe)
{
    int ret = -1;
    struct syncargs *args = NULL;
    gd1_mgmt_v3_lock_rsp rsp = {
        {0},
    };
    call_frame_t *frame = NULL;
    int op_ret = -1;
    int op_errno = -1;
    uuid_t *peerid = NULL;

    GF_ASSERT(req);
    GF_ASSERT(myframe);

    frame = myframe;
    args = frame->local;
    peerid = frame->cookie;
    frame->local = NULL;
    frame->cookie = NULL;

    if (-1 == req->rpc_status) {
        op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(THIS->name, iov, out, op_errno, EINVAL);

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_v3_lock_rsp);
    if (ret < 0)
        goto out;

    gf_uuid_copy(args->uuid, rsp.uuid);

    op_ret = rsp.op_ret;
    op_errno = rsp.op_errno;
out:
    gd_mgmt_v3_collate_errors(args, op_ret, op_errno, NULL,
                              GLUSTERD_MGMT_V3_LOCK, *peerid, rsp.uuid);

    GF_FREE(peerid);
    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    synctask_barrier_wake(args);
    return 0;
}

int32_t
gd_syncop_mgmt_v3_lock_cbk(struct rpc_req *req, struct iovec *iov, int count,
                           void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   gd_syncop_mgmt_v3_lock_cbk_fn);
}

int
gd_syncop_mgmt_v3_lock(glusterd_op_t op, dict_t *op_ctx,
                       glusterd_peerinfo_t *peerinfo, struct syncargs *args,
                       uuid_t my_uuid, uuid_t recv_uuid, uuid_t txn_id)
{
    int ret = -1;
    gd1_mgmt_v3_lock_req req = {
        {0},
    };
    uuid_t *peerid = NULL;

    GF_ASSERT(op_ctx);
    GF_ASSERT(peerinfo);
    GF_ASSERT(args);

    ret = dict_allocate_and_serialize(op_ctx, &req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno,
                GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
        goto out;
    }

    gf_uuid_copy(req.uuid, my_uuid);
    gf_uuid_copy(req.txn_id, txn_id);
    req.op = op;

    GD_ALLOC_COPY_UUID(peerid, peerinfo->uuid, ret);
    if (ret)
        goto out;

    ret = gd_syncop_submit_request(peerinfo->rpc, &req, args, peerid,
                                   &gd_mgmt_v3_prog, GLUSTERD_MGMT_V3_LOCK,
                                   gd_syncop_mgmt_v3_lock_cbk,
                                   (xdrproc_t)xdr_gd1_mgmt_v3_lock_req);
out:
    GF_FREE(req.dict.dict_val);
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int32_t
gd_syncop_mgmt_v3_unlock_cbk_fn(struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
    int ret = -1;
    struct syncargs *args = NULL;
    gd1_mgmt_v3_unlock_rsp rsp = {
        {0},
    };
    call_frame_t *frame = NULL;
    int op_ret = -1;
    int op_errno = -1;
    uuid_t *peerid = NULL;

    GF_ASSERT(req);
    GF_ASSERT(myframe);

    frame = myframe;
    args = frame->local;
    peerid = frame->cookie;
    frame->local = NULL;
    frame->cookie = NULL;

    if (-1 == req->rpc_status) {
        op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(THIS->name, iov, out, op_errno, EINVAL);

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_v3_unlock_rsp);
    if (ret < 0)
        goto out;

    gf_uuid_copy(args->uuid, rsp.uuid);

    op_ret = rsp.op_ret;
    op_errno = rsp.op_errno;
out:
    gd_mgmt_v3_collate_errors(args, op_ret, op_errno, NULL,
                              GLUSTERD_MGMT_V3_UNLOCK, *peerid, rsp.uuid);

    GF_FREE(peerid);
    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    synctask_barrier_wake(args);
    return 0;
}

int32_t
gd_syncop_mgmt_v3_unlock_cbk(struct rpc_req *req, struct iovec *iov, int count,
                             void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   gd_syncop_mgmt_v3_unlock_cbk_fn);
}

int
gd_syncop_mgmt_v3_unlock(dict_t *op_ctx, glusterd_peerinfo_t *peerinfo,
                         struct syncargs *args, uuid_t my_uuid,
                         uuid_t recv_uuid, uuid_t txn_id)
{
    int ret = -1;
    gd1_mgmt_v3_unlock_req req = {
        {0},
    };
    uuid_t *peerid = NULL;

    GF_ASSERT(op_ctx);
    GF_ASSERT(peerinfo);
    GF_ASSERT(args);

    ret = dict_allocate_and_serialize(op_ctx, &req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno,
                GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
        goto out;
    }

    gf_uuid_copy(req.uuid, my_uuid);
    gf_uuid_copy(req.txn_id, txn_id);

    GD_ALLOC_COPY_UUID(peerid, peerinfo->uuid, ret);
    if (ret)
        goto out;

    ret = gd_syncop_submit_request(peerinfo->rpc, &req, args, peerid,
                                   &gd_mgmt_v3_prog, GLUSTERD_MGMT_V3_UNLOCK,
                                   gd_syncop_mgmt_v3_unlock_cbk,
                                   (xdrproc_t)xdr_gd1_mgmt_v3_unlock_req);
out:
    GF_FREE(req.dict.dict_val);
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int32_t
_gd_syncop_mgmt_lock_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    int ret = -1;
    struct syncargs *args = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    gd1_mgmt_cluster_lock_rsp rsp = {
        {0},
    };
    call_frame_t *frame = NULL;
    int op_ret = -1;
    int op_errno = -1;
    xlator_t *this = THIS;
    uuid_t *peerid = NULL;

    frame = myframe;
    args = frame->local;
    peerid = frame->cookie;
    frame->local = NULL;
    frame->cookie = NULL;

    if (-1 == req->rpc_status) {
        op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, iov, out, op_errno, EINVAL);

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);
    if (ret < 0)
        goto out;

    gf_uuid_copy(args->uuid, rsp.uuid);

    RCU_READ_LOCK;
    peerinfo = glusterd_peerinfo_find(*peerid, NULL);
    if (peerinfo) {
        /* Set peer as locked, so we unlock only the locked peers */
        if (rsp.op_ret == 0)
            peerinfo->locked = _gf_true;
        RCU_READ_UNLOCK;
    } else {
        RCU_READ_UNLOCK;
        rsp.op_ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_PEER_NOT_FOUND,
               "Could not find peer with "
               "ID %s",
               uuid_utoa(*peerid));
    }

    op_ret = rsp.op_ret;
    op_errno = rsp.op_errno;
out:
    gd_collate_errors(args, op_ret, op_errno, NULL, GLUSTERD_MGMT_CLUSTER_LOCK,
                      *peerid, rsp.uuid);

    GF_FREE(peerid);
    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    synctask_barrier_wake(args);
    return 0;
}

int32_t
gd_syncop_mgmt_lock_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   _gd_syncop_mgmt_lock_cbk);
}

int
gd_syncop_mgmt_lock(glusterd_peerinfo_t *peerinfo, struct syncargs *args,
                    uuid_t my_uuid, uuid_t recv_uuid)
{
    int ret = -1;
    gd1_mgmt_cluster_lock_req req = {
        {0},
    };
    uuid_t *peerid = NULL;

    gf_uuid_copy(req.uuid, my_uuid);
    GD_ALLOC_COPY_UUID(peerid, peerinfo->uuid, ret);
    if (ret)
        goto out;

    ret = gd_syncop_submit_request(peerinfo->rpc, &req, args, peerid,
                                   &gd_mgmt_prog, GLUSTERD_MGMT_CLUSTER_LOCK,
                                   gd_syncop_mgmt_lock_cbk,
                                   (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req);
out:
    return ret;
}

int32_t
_gd_syncop_mgmt_unlock_cbk(struct rpc_req *req, struct iovec *iov, int count,
                           void *myframe)
{
    int ret = -1;
    struct syncargs *args = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    gd1_mgmt_cluster_unlock_rsp rsp = {
        {0},
    };
    call_frame_t *frame = NULL;
    int op_ret = -1;
    int op_errno = -1;
    xlator_t *this = THIS;
    uuid_t *peerid = NULL;

    frame = myframe;
    args = frame->local;
    peerid = frame->cookie;
    frame->local = NULL;
    frame->cookie = NULL;

    if (-1 == req->rpc_status) {
        op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, iov, out, op_errno, EINVAL);

    ret = xdr_to_generic(*iov, &rsp,
                         (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);
    if (ret < 0)
        goto out;

    gf_uuid_copy(args->uuid, rsp.uuid);

    RCU_READ_LOCK;
    peerinfo = glusterd_peerinfo_find(*peerid, NULL);
    if (peerinfo) {
        peerinfo->locked = _gf_false;
        RCU_READ_UNLOCK;
    } else {
        RCU_READ_UNLOCK;
        rsp.op_ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_PEER_NOT_FOUND,
               "Could not find peer with "
               "ID %s",
               uuid_utoa(*peerid));
    }

    op_ret = rsp.op_ret;
    op_errno = rsp.op_errno;
out:
    gd_collate_errors(args, op_ret, op_errno, NULL,
                      GLUSTERD_MGMT_CLUSTER_UNLOCK, *peerid, rsp.uuid);

    GF_FREE(peerid);
    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    synctask_barrier_wake(args);
    return 0;
}

int32_t
gd_syncop_mgmt_unlock_cbk(struct rpc_req *req, struct iovec *iov, int count,
                          void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   _gd_syncop_mgmt_unlock_cbk);
}

int
gd_syncop_mgmt_unlock(glusterd_peerinfo_t *peerinfo, struct syncargs *args,
                      uuid_t my_uuid, uuid_t recv_uuid)
{
    int ret = -1;
    gd1_mgmt_cluster_unlock_req req = {
        {0},
    };
    uuid_t *peerid = NULL;

    gf_uuid_copy(req.uuid, my_uuid);
    GD_ALLOC_COPY_UUID(peerid, peerinfo->uuid, ret);
    if (ret)
        goto out;

    ret = gd_syncop_submit_request(peerinfo->rpc, &req, args, peerid,
                                   &gd_mgmt_prog, GLUSTERD_MGMT_CLUSTER_UNLOCK,
                                   gd_syncop_mgmt_unlock_cbk,
                                   (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req);
out:
    return ret;
}

int32_t
_gd_syncop_stage_op_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    int ret = -1;
    gd1_mgmt_stage_op_rsp rsp = {
        {0},
    };
    struct syncargs *args = NULL;
    xlator_t *this = THIS;
    dict_t *rsp_dict = NULL;
    call_frame_t *frame = NULL;
    int op_ret = -1;
    int op_errno = -1;
    uuid_t *peerid = NULL;

    frame = myframe;
    args = frame->local;
    peerid = frame->cookie;
    frame->local = NULL;
    frame->cookie = NULL;

    if (-1 == req->rpc_status) {
        op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, iov, out, op_errno, EINVAL);

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);
    if (ret < 0)
        goto out;

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        rsp_dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &rsp_dict);
        if (ret < 0) {
            GF_FREE(rsp.dict.dict_val);
            goto out;
        } else {
            rsp_dict->extra_stdfree = rsp.dict.dict_val;
        }
    }

    RCU_READ_LOCK;
    ret = (glusterd_peerinfo_find(rsp.uuid, NULL) == NULL);
    RCU_READ_UNLOCK;
    if (ret) {
        ret = -1;
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_RESP_FROM_UNKNOWN_PEER,
               "Staging response "
               "for 'Volume %s' received from unknown "
               "peer: %s",
               gd_op_list[rsp.op], uuid_utoa(rsp.uuid));
        goto out;
    }

    gf_uuid_copy(args->uuid, rsp.uuid);
    if (rsp.op == GD_OP_REPLACE_BRICK || rsp.op == GD_OP_QUOTA ||
        rsp.op == GD_OP_CREATE_VOLUME || rsp.op == GD_OP_ADD_BRICK ||
        rsp.op == GD_OP_START_VOLUME) {
        pthread_mutex_lock(&args->lock_dict);
        {
            ret = glusterd_syncop_aggr_rsp_dict(rsp.op, args->dict, rsp_dict);
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESP_AGGR_FAIL, "%s",
                       "Failed to aggregate response from "
                       " node/brick");
        }
        pthread_mutex_unlock(&args->lock_dict);
    }

    op_ret = rsp.op_ret;
    op_errno = rsp.op_errno;

out:
    gd_collate_errors(args, op_ret, op_errno, rsp.op_errstr,
                      GLUSTERD_MGMT_STAGE_OP, *peerid, rsp.uuid);

    if (rsp_dict)
        dict_unref(rsp_dict);
    GF_FREE(peerid);
    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    synctask_barrier_wake(args);
    return 0;
}

int32_t
gd_syncop_stage_op_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   _gd_syncop_stage_op_cbk);
}

int
gd_syncop_mgmt_stage_op(glusterd_peerinfo_t *peerinfo, struct syncargs *args,
                        uuid_t my_uuid, uuid_t recv_uuid, int op,
                        dict_t *dict_out, dict_t *op_ctx)
{
    gd1_mgmt_stage_op_req *req = NULL;
    int ret = -1;
    uuid_t *peerid = NULL;

    req = GF_CALLOC(1, sizeof(*req), gf_gld_mt_mop_stage_req_t);
    if (!req) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto out;
    }

    gf_uuid_copy(req->uuid, my_uuid);
    req->op = op;

    ret = dict_allocate_and_serialize(dict_out, &req->buf.buf_val,
                                      &req->buf.buf_len);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno,
                GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
        goto out;
    }

    GD_ALLOC_COPY_UUID(peerid, peerinfo->uuid, ret);
    if (ret)
        goto out;

    ret = gd_syncop_submit_request(
        peerinfo->rpc, req, args, peerid, &gd_mgmt_prog, GLUSTERD_MGMT_STAGE_OP,
        gd_syncop_stage_op_cbk, (xdrproc_t)xdr_gd1_mgmt_stage_op_req);
out:
    gd_stage_op_req_free(req);
    return ret;
}

int32_t
_gd_syncop_brick_op_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    struct syncargs *args = NULL;
    gd1_mgmt_brick_op_rsp rsp = {
        0,
    };
    int ret = -1;
    call_frame_t *frame = NULL;
    xlator_t *this = THIS;

    frame = myframe;
    args = frame->local;
    frame->local = NULL;

    /* initialize */
    args->op_ret = -1;
    args->op_errno = EINVAL;

    if (-1 == req->rpc_status) {
        args->op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, iov, out, args->op_errno,
                                   EINVAL);

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
    if (ret < 0)
        goto out;

    if (rsp.output.output_len) {
        args->dict = dict_new();
        if (!args->dict) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL,
                    NULL);
            ret = -1;
            args->op_errno = ENOMEM;
            goto out;
        }

        ret = dict_unserialize(rsp.output.output_val, rsp.output.output_len,
                               &args->dict);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, errno,
                    GD_MSG_DICT_UNSERIALIZE_FAIL, NULL);
            goto out;
        }
    }

    args->op_ret = rsp.op_ret;
    args->op_errno = rsp.op_errno;
    args->errstr = gf_strdup(rsp.op_errstr);

out:
    if ((rsp.op_errstr) && (strcmp(rsp.op_errstr, "") != 0))
        free(rsp.op_errstr);
    free(rsp.output.output_val);

    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    __wake(args);

    return 0;
}

int32_t
gd_syncop_brick_op_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   _gd_syncop_brick_op_cbk);
}

int
gd_syncop_mgmt_brick_op(struct rpc_clnt *rpc, glusterd_pending_node_t *pnode,
                        int op, dict_t *dict_out, dict_t *op_ctx, char **errstr)
{
    struct syncargs args = {
        0,
    };
    gd1_mgmt_brick_op_req *req = NULL;
    int ret = 0;

    args.op_ret = -1;
    args.op_errno = ENOTCONN;

    if ((pnode->type == GD_NODE_NFS) || (pnode->type == GD_NODE_QUOTAD) ||
        (pnode->type == GD_NODE_SCRUB) ||
        ((pnode->type == GD_NODE_SHD) && (op == GD_OP_STATUS_VOLUME))) {
        ret = glusterd_node_op_build_payload(op, &req, dict_out);

    } else {
        ret = glusterd_brick_op_build_payload(op, pnode->node, &req, dict_out);
    }

    if (ret)
        goto out;

    GD_SYNCOP(rpc, (&args), NULL, gd_syncop_brick_op_cbk, req, &gd_brick_prog,
              req->op, xdr_gd1_mgmt_brick_op_req);

    if (args.errstr) {
        if ((strlen(args.errstr) > 0) && errstr)
            *errstr = args.errstr;
        else
            GF_FREE(args.errstr);
    }

    if (GD_OP_STATUS_VOLUME == op) {
        ret = dict_set_int32(args.dict, "index", pnode->index);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Error setting index on brick status"
                   " rsp dict");
            args.op_ret = -1;
            goto out;
        }
    }

    if (req->op == GLUSTERD_BRICK_TERMINATE) {
        if (args.op_ret && (args.op_errno == ENOTCONN)) {
            /*
             * This is actually OK.  It happens when the target
             * brick process exits and we saw the closed connection
             * before we read the response.  If we didn't read the
             * response quickly enough that's kind of our own
             * fault, and the fact that the process exited means
             * that our goal of terminating the brick was achieved.
             */
            args.op_ret = 0;
        }
    }

    if (args.op_ret == 0)
        glusterd_handle_node_rsp(dict_out, pnode->node, op, args.dict, op_ctx,
                                 errstr, pnode->type);

out:
    errno = args.op_errno;
    if (args.dict)
        dict_unref(args.dict);
    if (args.op_ret && errstr && (*errstr == NULL)) {
        if (op == GD_OP_HEAL_VOLUME) {
            gf_asprintf(errstr,
                        "Glusterd Syncop Mgmt brick op '%s' failed."
                        " Please check glustershd log file for details.",
                        gd_op_list[op]);
        } else {
            gf_asprintf(errstr,
                        "Glusterd Syncop Mgmt brick op '%s' failed."
                        " Please check brick log file for details.",
                        gd_op_list[op]);
        }
    }
    gd_brick_op_req_free(req);
    return args.op_ret;
}

int32_t
_gd_syncop_commit_op_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    int ret = -1;
    gd1_mgmt_commit_op_rsp rsp = {
        {0},
    };
    struct syncargs *args = NULL;
    xlator_t *this = THIS;
    dict_t *rsp_dict = NULL;
    call_frame_t *frame = NULL;
    int op_ret = -1;
    int op_errno = -1;
    int type = GF_QUOTA_OPTION_TYPE_NONE;
    uuid_t *peerid = NULL;

    frame = myframe;
    args = frame->local;
    peerid = frame->cookie;
    frame->local = NULL;
    frame->cookie = NULL;

    if (-1 == req->rpc_status) {
        op_errno = ENOTCONN;
        goto out;
    }

    GF_VALIDATE_OR_GOTO_WITH_ERROR(this->name, iov, out, op_errno, EINVAL);

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);
    if (ret < 0) {
        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        rsp_dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &rsp_dict);
        if (ret < 0) {
            GF_FREE(rsp.dict.dict_val);
            goto out;
        } else {
            rsp_dict->extra_stdfree = rsp.dict.dict_val;
        }
    }

    RCU_READ_LOCK;
    ret = (glusterd_peerinfo_find(rsp.uuid, NULL) == 0);
    RCU_READ_UNLOCK;
    if (ret) {
        ret = -1;
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_RESP_FROM_UNKNOWN_PEER,
               "Commit response "
               "for 'Volume %s' received from unknown "
               "peer: %s",
               gd_op_list[rsp.op], uuid_utoa(rsp.uuid));
        goto out;
    }

    gf_uuid_copy(args->uuid, rsp.uuid);
    if (rsp.op == GD_OP_QUOTA) {
        ret = dict_get_int32(args->dict, "type", &type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "opcode");
            goto out;
        }
    }

    if ((rsp.op != GD_OP_QUOTA) || (type == GF_QUOTA_OPTION_TYPE_LIST)) {
        pthread_mutex_lock(&args->lock_dict);
        {
            ret = glusterd_syncop_aggr_rsp_dict(rsp.op, args->dict, rsp_dict);
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESP_AGGR_FAIL, "%s",
                       "Failed to aggregate response from "
                       " node/brick");
        }
        pthread_mutex_unlock(&args->lock_dict);
    }

    op_ret = rsp.op_ret;
    op_errno = rsp.op_errno;

out:
    gd_collate_errors(args, op_ret, op_errno, rsp.op_errstr,
                      GLUSTERD_MGMT_COMMIT_OP, *peerid, rsp.uuid);
    if (rsp_dict)
        dict_unref(rsp_dict);
    GF_FREE(peerid);
    /* req->rpc_status set to -1 means, STACK_DESTROY will be called from
     * the caller function.
     */
    if (req->rpc_status != -1)
        STACK_DESTROY(frame->root);
    synctask_barrier_wake(args);

    return 0;
}

int32_t
gd_syncop_commit_op_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    return glusterd_big_locked_cbk(req, iov, count, myframe,
                                   _gd_syncop_commit_op_cbk);
}

int
gd_syncop_mgmt_commit_op(glusterd_peerinfo_t *peerinfo, struct syncargs *args,
                         uuid_t my_uuid, uuid_t recv_uuid, int op,
                         dict_t *dict_out, dict_t *op_ctx)
{
    gd1_mgmt_commit_op_req *req = NULL;
    int ret = -1;
    uuid_t *peerid = NULL;

    req = GF_CALLOC(1, sizeof(*req), gf_gld_mt_mop_commit_req_t);
    if (!req) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto out;
    }

    gf_uuid_copy(req->uuid, my_uuid);
    req->op = op;

    ret = dict_allocate_and_serialize(dict_out, &req->buf.buf_val,
                                      &req->buf.buf_len);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno,
                GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
        goto out;
    }

    GD_ALLOC_COPY_UUID(peerid, peerinfo->uuid, ret);
    if (ret)
        goto out;

    ret = gd_syncop_submit_request(peerinfo->rpc, req, args, peerid,
                                   &gd_mgmt_prog, GLUSTERD_MGMT_COMMIT_OP,
                                   gd_syncop_commit_op_cbk,
                                   (xdrproc_t)xdr_gd1_mgmt_commit_op_req);
out:
    gd_commit_op_req_free(req);
    return ret;
}

int
gd_lock_op_phase(glusterd_conf_t *conf, glusterd_op_t op, dict_t *op_ctx,
                 char **op_errstr, uuid_t txn_id,
                 glusterd_op_info_t *txn_opinfo, gf_boolean_t cluster_lock)
{
    int ret = -1;
    int peer_cnt = 0;
    uuid_t peer_uuid = {0};
    xlator_t *this = THIS;
    glusterd_peerinfo_t *peerinfo = NULL;
    struct syncargs args = {0};

    ret = synctask_barrier_init((&args));
    if (ret)
        goto out;

    peer_cnt = 0;

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > txn_opinfo->txn_generation)
            continue;

        if (!peerinfo->connected)
            continue;
        if (op != GD_OP_SYNC_VOLUME &&
            peerinfo->state != GD_FRIEND_STATE_BEFRIENDED)
            continue;

        if (cluster_lock) {
            /* Reset lock status */
            peerinfo->locked = _gf_false;
            gd_syncop_mgmt_lock(peerinfo, &args, MY_UUID, peer_uuid);
        } else
            gd_syncop_mgmt_v3_lock(op, op_ctx, peerinfo, &args, MY_UUID,
                                   peer_uuid, txn_id);
        peer_cnt++;
    }
    RCU_READ_UNLOCK;

    if (0 == peer_cnt) {
        ret = 0;
        goto out;
    }

    gd_synctask_barrier_wait((&args), peer_cnt);

    if (args.op_ret) {
        if (args.errstr)
            *op_errstr = gf_strdup(args.errstr);
        else {
            ret = gf_asprintf(op_errstr,
                              "Another transaction "
                              "could be in progress. Please try "
                              "again after some time.");
            if (ret == -1)
                *op_errstr = NULL;

            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_LOCK_FAIL,
                   "Failed to acquire lock");
        }
    }

    ret = args.op_ret;

    gf_msg_debug(this->name, 0,
                 "Sent lock op req for 'Volume %s' "
                 "to %d peers. Returning %d",
                 gd_op_list[op], peer_cnt, ret);
out:
    return ret;
}

static int
glusterd_validate_and_set_gfid(dict_t *op_ctx, dict_t *req_dict,
                               char **op_errstr)
{
    int ret = -1;
    int count = 0;
    int i = 0;
    int op_code = GF_QUOTA_OPTION_TYPE_NONE;
    uuid_t uuid1 = {0};
    uuid_t uuid2 = {
        0,
    };
    char *path = NULL;
    char key[64] = "";
    int keylen;
    char *uuid1_str = NULL;
    char *uuid1_str_dup = NULL;
    char *uuid2_str = NULL;

    ret = dict_get_int32(op_ctx, "type", &op_code);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get quota opcode");
        goto out;
    }

    if ((op_code != GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) &&
        (op_code != GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS) &&
        (op_code != GF_QUOTA_OPTION_TYPE_REMOVE) &&
        (op_code != GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS)) {
        ret = 0;
        goto out;
    }

    ret = dict_get_str(op_ctx, "path", &path);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get path");
        goto out;
    }

    ret = dict_get_int32(op_ctx, "count", &count);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get count");
        goto out;
    }

    /* If count is 0, fail the command with ENOENT.
     *
     * If count is 1, treat gfid0 as the gfid on which the operation
     * is to be performed and resume the command.
     *
     * if count > 1, get the 0th gfid from the op_ctx and,
     * compare it with the remaining 'count -1' gfids.
     * If they are found to be the same, set gfid0 in the op_ctx and
     * resume the operation, else error out.
     */

    if (count == 0) {
        gf_asprintf(op_errstr,
                    "Failed to get trusted.gfid attribute "
                    "on path %s. Reason : %s",
                    path, strerror(ENOENT));
        ret = -ENOENT;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "gfid%d", 0);

    ret = dict_get_strn(op_ctx, key, keylen, &uuid1_str);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get key '%s'", key);
        goto out;
    }

    gf_uuid_parse(uuid1_str, uuid1);

    for (i = 1; i < count; i++) {
        keylen = snprintf(key, sizeof(key), "gfid%d", i);

        ret = dict_get_strn(op_ctx, key, keylen, &uuid2_str);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get key "
                   "'%s'",
                   key);
            goto out;
        }

        gf_uuid_parse(uuid2_str, uuid2);

        if (gf_uuid_compare(uuid1, uuid2)) {
            gf_asprintf(op_errstr,
                        "gfid mismatch between %s and "
                        "%s for path %s",
                        uuid1_str, uuid2_str, path);
            ret = -1;
            goto out;
        }
    }

    if (i == count) {
        uuid1_str_dup = gf_strdup(uuid1_str);
        if (!uuid1_str_dup) {
            ret = -1;
            goto out;
        }

        ret = dict_set_dynstr_sizen(req_dict, "gfid", uuid1_str_dup);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set gfid");
            GF_FREE(uuid1_str_dup);
            goto out;
        }
    } else {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_ITER_FAIL,
               "Failed to iterate through %d"
               " entries in the req dict",
               count);
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
gd_stage_op_phase(glusterd_op_t op, dict_t *op_ctx, dict_t *req_dict,
                  char **op_errstr, glusterd_op_info_t *txn_opinfo)
{
    int ret = -1;
    int peer_cnt = 0;
    dict_t *rsp_dict = NULL;
    char *hostname = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    uuid_t tmp_uuid = {0};
    char *errstr = NULL;
    struct syncargs args = {0};
    dict_t *aggr_dict = NULL;

    conf = this->private;
    GF_ASSERT(conf);

    rsp_dict = dict_new();
    if (!rsp_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }

    if ((op == GD_OP_CREATE_VOLUME) || (op == GD_OP_ADD_BRICK) ||
        (op == GD_OP_START_VOLUME))
        aggr_dict = req_dict;
    else
        aggr_dict = op_ctx;

    ret = glusterd_validate_quorum(this, op, req_dict, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_SERVER_QUORUM_NOT_MET,
               "Server quorum not met. Rejecting operation.");
        goto out;
    }

    ret = glusterd_op_stage_validate(op, req_dict, op_errstr, rsp_dict);
    if (ret) {
        hostname = "localhost";
        goto stage_done;
    }

    if ((op == GD_OP_REPLACE_BRICK || op == GD_OP_QUOTA ||
         op == GD_OP_CREATE_VOLUME || op == GD_OP_ADD_BRICK ||
         op == GD_OP_START_VOLUME)) {
        ret = glusterd_syncop_aggr_rsp_dict(op, aggr_dict, rsp_dict);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESP_AGGR_FAIL, "%s",
                   "Failed to aggregate response from node/brick");
            goto out;
        }
    }
    dict_unref(rsp_dict);
    rsp_dict = NULL;

stage_done:
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VALIDATE_FAILED,
               LOGSTR_STAGE_FAIL, gd_op_list[op], hostname,
               (*op_errstr) ? ":" : " ", (*op_errstr) ? *op_errstr : " ");
        if (*op_errstr == NULL)
            gf_asprintf(op_errstr, OPERRSTR_STAGE_FAIL, hostname);
        goto out;
    }

    ret = gd_syncargs_init(&args, aggr_dict);
    if (ret)
        goto out;

    peer_cnt = 0;

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > txn_opinfo->txn_generation)
            continue;

        if (!peerinfo->connected)
            continue;
        if (op != GD_OP_SYNC_VOLUME &&
            peerinfo->state != GD_FRIEND_STATE_BEFRIENDED)
            continue;

        (void)gd_syncop_mgmt_stage_op(peerinfo, &args, MY_UUID, tmp_uuid, op,
                                      req_dict, op_ctx);
        peer_cnt++;
    }
    RCU_READ_UNLOCK;

    if (0 == peer_cnt) {
        ret = 0;
        goto out;
    }

    gf_msg_debug(this->name, 0,
                 "Sent stage op req for 'Volume %s' "
                 "to %d peers",
                 gd_op_list[op], peer_cnt);

    gd_synctask_barrier_wait((&args), peer_cnt);

    if (args.errstr)
        *op_errstr = gf_strdup(args.errstr);
    else if (dict_get_str(aggr_dict, "errstr", &errstr) == 0)
        *op_errstr = gf_strdup(errstr);

    ret = args.op_ret;

out:
    if ((ret == 0) && (op == GD_OP_QUOTA)) {
        ret = glusterd_validate_and_set_gfid(op_ctx, req_dict, op_errstr);
        if (ret)
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GFID_VALIDATE_SET_FAIL,
                   "Failed to validate and set gfid");
    }

    if (rsp_dict)
        dict_unref(rsp_dict);

    gd_syncargs_fini(&args);
    return ret;
}

int
gd_commit_op_phase(glusterd_op_t op, dict_t *op_ctx, dict_t *req_dict,
                   char **op_errstr, glusterd_op_info_t *txn_opinfo)
{
    dict_t *rsp_dict = NULL;
    int peer_cnt = -1;
    int ret = -1;
    char *hostname = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    uuid_t tmp_uuid = {0};
    char *errstr = NULL;
    struct syncargs args = {0};
    int type = GF_QUOTA_OPTION_TYPE_NONE;
    uint32_t cmd = 0;
    gf_boolean_t origin_glusterd = _gf_false;

    conf = this->private;
    GF_ASSERT(conf);

    rsp_dict = dict_new();
    if (!rsp_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -1;
        goto out;
    }

    ret = glusterd_op_commit_perform(op, req_dict, op_errstr, rsp_dict);
    if (ret) {
        hostname = "localhost";
        goto commit_done;
    }

    if (op == GD_OP_QUOTA) {
        ret = dict_get_int32(op_ctx, "type", &type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get "
                   "opcode");
            goto out;
        }
    }

    if (((op == GD_OP_QUOTA) &&
         ((type == GF_QUOTA_OPTION_TYPE_LIST) ||
          (type == GF_QUOTA_OPTION_TYPE_LIST_OBJECTS))) ||
        ((op != GD_OP_SYNC_VOLUME) && (op != GD_OP_QUOTA))) {
        ret = glusterd_syncop_aggr_rsp_dict(op, op_ctx, rsp_dict);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESP_AGGR_FAIL, "%s",
                   "Failed to aggregate "
                   "response from node/brick");
            goto out;
        }
    }

    dict_unref(rsp_dict);
    rsp_dict = NULL;

commit_done:
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_COMMIT_OP_FAIL,
               LOGSTR_COMMIT_FAIL, gd_op_list[op], hostname,
               (*op_errstr) ? ":" : " ", (*op_errstr) ? *op_errstr : " ");
        if (*op_errstr == NULL)
            gf_asprintf(op_errstr, OPERRSTR_COMMIT_FAIL, hostname);
        goto out;
    }

    ret = gd_syncargs_init(&args, op_ctx);
    if (ret)
        goto out;

    peer_cnt = 0;
    origin_glusterd = is_origin_glusterd(req_dict);

    if (op == GD_OP_STATUS_VOLUME) {
        ret = dict_get_uint32(req_dict, "cmd", &cmd);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                    "Key=cmd", NULL);
            goto out;
        }

        if (origin_glusterd) {
            if ((cmd & GF_CLI_STATUS_ALL)) {
                ret = 0;
                goto out;
            }
        }
    }

    RCU_READ_LOCK;
    cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
    {
        /* Only send requests to peers who were available before the
         * transaction started
         */
        if (peerinfo->generation > txn_opinfo->txn_generation)
            continue;

        if (!peerinfo->connected)
            continue;
        if (op != GD_OP_SYNC_VOLUME &&
            peerinfo->state != GD_FRIEND_STATE_BEFRIENDED)
            continue;

        (void)gd_syncop_mgmt_commit_op(peerinfo, &args, MY_UUID, tmp_uuid, op,
                                       req_dict, op_ctx);
        peer_cnt++;
    }
    RCU_READ_UNLOCK;

    if (0 == peer_cnt) {
        ret = 0;
        goto out;
    }

    gd_synctask_barrier_wait((&args), peer_cnt);
    ret = args.op_ret;
    if (args.errstr)
        *op_errstr = gf_strdup(args.errstr);
    else if (dict_get_str(op_ctx, "errstr", &errstr) == 0)
        *op_errstr = gf_strdup(errstr);

    gf_msg_debug(this->name, 0,
                 "Sent commit op req for 'Volume %s' "
                 "to %d peers",
                 gd_op_list[op], peer_cnt);
out:
    if (!ret)
        glusterd_op_modify_op_ctx(op, op_ctx);

    if (rsp_dict)
        dict_unref(rsp_dict);

    GF_FREE(args.errstr);
    args.errstr = NULL;

    gd_syncargs_fini(&args);
    return ret;
}

int
gd_unlock_op_phase(glusterd_conf_t *conf, glusterd_op_t op, int *op_ret,
                   rpcsvc_request_t *req, dict_t *op_ctx, char *op_errstr,
                   char *volname, gf_boolean_t is_acquired, uuid_t txn_id,
                   glusterd_op_info_t *txn_opinfo, gf_boolean_t cluster_lock)
{
    glusterd_peerinfo_t *peerinfo = NULL;
    uuid_t tmp_uuid = {0};
    int peer_cnt = 0;
    int ret = -1;
    xlator_t *this = THIS;
    struct syncargs args = {0};
    int32_t global = 0;
    char *type = NULL;

    /* If the lock has not been held during this
     * transaction, do not send unlock requests */
    if (!is_acquired) {
        ret = 0;
        goto out;
    }

    ret = synctask_barrier_init((&args));
    if (ret)
        goto out;

    peer_cnt = 0;

    if (cluster_lock) {
        RCU_READ_LOCK;
        cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
        {
            /* Only send requests to peers who were available before
             * the transaction started
             */
            if (peerinfo->generation > txn_opinfo->txn_generation)
                continue;

            if (!peerinfo->connected)
                continue;
            if (op != GD_OP_SYNC_VOLUME &&
                peerinfo->state != GD_FRIEND_STATE_BEFRIENDED)
                continue;

            /* Only unlock peers that were locked */
            if (peerinfo->locked) {
                gd_syncop_mgmt_unlock(peerinfo, &args, MY_UUID, tmp_uuid);
                peer_cnt++;
            }
        }
        RCU_READ_UNLOCK;
    } else {
        ret = dict_get_int32(op_ctx, "hold_global_locks", &global);
        if (!ret && global)
            type = "global";
        else
            type = "vol";
        if (volname || global) {
            RCU_READ_LOCK;
            cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
            {
                /* Only send requests to peers who were
                 * available before the transaction started
                 */
                if (peerinfo->generation > txn_opinfo->txn_generation)
                    continue;

                if (!peerinfo->connected)
                    continue;
                if (op != GD_OP_SYNC_VOLUME &&
                    peerinfo->state != GD_FRIEND_STATE_BEFRIENDED)
                    continue;

                gd_syncop_mgmt_v3_unlock(op_ctx, peerinfo, &args, MY_UUID,
                                         tmp_uuid, txn_id);
                peer_cnt++;
            }
            RCU_READ_UNLOCK;
        }
    }

    if (0 == peer_cnt) {
        ret = 0;
        goto out;
    }

    gd_synctask_barrier_wait((&args), peer_cnt);

    ret = args.op_ret;

    gf_msg_debug(this->name, 0,
                 "Sent unlock op req for 'Volume %s' "
                 "to %d peers. Returning %d",
                 gd_op_list[op], peer_cnt, ret);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_UNLOCK_FAIL,
               "Failed to unlock "
               "on some peer(s)");
    }

out:
    /* If unlock failed, and op_ret was previously set
     * priority is given to the op_ret. If op_ret was
     * not set, and unlock failed, then set op_ret */
    if (!*op_ret)
        *op_ret = ret;

    if (is_acquired) {
        /* Based on the op-version,
         * we release the cluster or mgmt_v3 lock
         * and clear the op */

        glusterd_op_clear_op();
        if (cluster_lock)
            glusterd_unlock(MY_UUID);
        else {
            if (type) {
                ret = glusterd_mgmt_v3_unlock(volname, MY_UUID, type);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_MGMTV3_UNLOCK_FAIL,
                           "Unable to release lock for %s", volname);
            }
        }
    }

    if (!*op_ret)
        *op_ret = ret;

    /*
     * If there are any quorum events while the OP is in progress, process
     * them.
     */
    if (conf->pending_quorum_action)
        glusterd_do_quorum_action();

    return 0;
}

int
gd_get_brick_count(struct cds_list_head *bricks)
{
    glusterd_pending_node_t *pending_node = NULL;
    int npeers = 0;
    cds_list_for_each_entry(pending_node, bricks, list) { npeers++; }
    return npeers;
}

int
gd_brick_op_phase(glusterd_op_t op, dict_t *op_ctx, dict_t *req_dict,
                  char **op_errstr)
{
    glusterd_pending_node_t *pending_node = NULL;
    glusterd_pending_node_t *tmp = NULL;
    struct cds_list_head selected = {
        0,
    };
    xlator_t *this = THIS;
    int brick_count = 0;
    int ret = -1;
    rpc_clnt_t *rpc = NULL;
    dict_t *rsp_dict = NULL;
    int32_t cmd = GF_OP_CMD_NONE;
    glusterd_volinfo_t *volinfo = NULL;

    rsp_dict = dict_new();
    if (!rsp_dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -1;
        goto out;
    }

    CDS_INIT_LIST_HEAD(&selected);
    ret = glusterd_op_bricks_select(op, req_dict, op_errstr, &selected,
                                    rsp_dict);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_OP_FAIL, "%s",
               (*op_errstr) ? *op_errstr
                            : "Brick op failed. Check "
                              "glusterd log file for more details.");
        goto out;
    }

    if (op == GD_OP_HEAL_VOLUME) {
        ret = glusterd_syncop_aggr_rsp_dict(op, op_ctx, rsp_dict);
        if (ret)
            goto out;
    }
    dict_unref(rsp_dict);
    rsp_dict = NULL;

    brick_count = 0;
    cds_list_for_each_entry_safe(pending_node, tmp, &selected, list)
    {
        rpc = glusterd_pending_node_get_rpc(pending_node);
        /* In the case of rebalance if the rpc object is null, we try to
         * create the rpc object. if the rebalance daemon is down, it returns
         * -1. otherwise, rpc object will be created and referenced.
         */
        if (!rpc) {
            if (pending_node->type == GD_NODE_REBALANCE && pending_node->node) {
                volinfo = pending_node->node;
                glusterd_defrag_ref(volinfo->rebal.defrag);
                ret = glusterd_rebalance_rpc_create(volinfo);
                if (ret) {
                    ret = 0;
                    glusterd_defrag_volume_node_rsp(req_dict, NULL, op_ctx);
                    goto out;
                } else {
                    rpc = glusterd_defrag_rpc_get(volinfo->rebal.defrag);
                }
            } else {
                ret = -1;
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RPC_FAILURE,
                       "Brick Op failed "
                       "due to rpc failure.");
                goto out;
            }
        }

        ret = gd_syncop_mgmt_brick_op(rpc, pending_node, op, req_dict, op_ctx,
                                      op_errstr);
        if (op == GD_OP_STATUS_VOLUME) {
            /* for client-list its enough to quit the loop
             * once we get the value from one brick
             * */
            ret = dict_get_int32(req_dict, "cmd", &cmd);
            if (!ret && (cmd & GF_CLI_STATUS_CLIENT_LIST)) {
                if (dict_get(op_ctx, "client-count"))
                    break;
            }
        }
        if (ret)
            goto out;

        brick_count++;
        glusterd_pending_node_put_rpc(pending_node);
        GF_FREE(pending_node);
    }

    pending_node = NULL;
    ret = 0;
out:
    if (pending_node && pending_node->node)
        glusterd_pending_node_put_rpc(pending_node);

    if (rsp_dict)
        dict_unref(rsp_dict);
    gf_msg_debug(this->name, 0, "Sent op req to %d bricks", brick_count);
    return ret;
}

void
gd_sync_task_begin(dict_t *op_ctx, rpcsvc_request_t *req)
{
    int ret = -1;
    int op_ret = -1;
    dict_t *req_dict = NULL;
    glusterd_conf_t *conf = NULL;
    glusterd_op_t op = GD_OP_NONE;
    int32_t tmp_op = 0;
    char *op_errstr = NULL;
    char *tmp = NULL;
    char *global = NULL;
    char *volname = NULL;
    xlator_t *this = THIS;
    gf_boolean_t is_acquired = _gf_false;
    gf_boolean_t is_global = _gf_false;
    uuid_t *txn_id = NULL;
    glusterd_op_info_t txn_opinfo = {
        GD_OP_STATE_DEFAULT,
    };
    uint32_t op_errno = 0;
    gf_boolean_t cluster_lock = _gf_false;
    time_t timeout = 0;

    conf = this->private;
    GF_ASSERT(conf);

    ret = dict_get_int32(op_ctx, GD_SYNC_OPCODE_KEY, &tmp_op);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get volume "
               "operation");
        goto out;
    }
    op = tmp_op;

    /* Generate a transaction-id for this operation and
     * save it in the dict */
    ret = glusterd_generate_txn_id(op_ctx, &txn_id);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_IDGEN_FAIL,
               "Failed to generate transaction id");
        goto out;
    }

    /* Save opinfo for this transaction with the transaction id. */
    glusterd_txn_opinfo_init(&txn_opinfo, 0, (int *)&op, NULL, NULL);
    ret = glusterd_set_txn_opinfo(txn_id, &txn_opinfo);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_SET_FAIL,
               "Unable to set transaction's opinfo");

    gf_msg_debug(this->name, 0, "Transaction ID : %s", uuid_utoa(*txn_id));

    /* Save the MY_UUID as the originator_uuid */
    ret = glusterd_set_originator_uuid(op_ctx);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UUID_SET_FAIL,
               "Failed to set originator_uuid.");
        goto out;
    }

    if (conf->op_version < GD_OP_VERSION_3_6_0)
        cluster_lock = _gf_true;

    /* Based on the op_version, acquire a cluster or mgmt_v3 lock */
    if (cluster_lock) {
        ret = glusterd_lock(MY_UUID);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_LOCK_FAIL,
                   "Unable to acquire lock");
            gf_asprintf(&op_errstr,
                        "Another transaction is in progress. "
                        "Please try again after some time.");
            goto out;
        }
    } else {
        /* Cli will add timeout key to dict if the default timeout is
         * other than 2 minutes. Here we use this value to check whether
         * mgmt_v3_lock_timeout should be set to default value or we
         * need to change the value according to timeout value
         * i.e, timeout + 120 seconds. */
        ret = dict_get_time(op_ctx, "timeout", &timeout);
        if (!ret)
            conf->mgmt_v3_lock_timeout = timeout + 120;

        ret = dict_get_str(op_ctx, "globalname", &global);
        if (!ret) {
            is_global = _gf_true;
            goto global;
        }

        /* If no volname is given as a part of the command, locks will
         * not be held */
        ret = dict_get_str(op_ctx, "volname", &tmp);
        if (ret) {
            gf_msg_debug("glusterd", 0,
                         "Failed to get volume "
                         "name");
            goto local_locking_done;
        } else {
            /* Use a copy of volname, as cli response will be
             * sent before the unlock, and the volname in the
             * dict, might be removed */
            volname = gf_strdup(tmp);
            if (!volname)
                goto out;
        }

        ret = glusterd_mgmt_v3_lock(volname, MY_UUID, &op_errno, "vol");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_LOCK_GET_FAIL,
                   "Unable to acquire lock for %s", volname);
            gf_asprintf(&op_errstr,
                        "Another transaction is in progress "
                        "for %s. Please try again after some time.",
                        volname);
            goto out;
        }
    }

global:
    if (is_global) {
        ret = glusterd_mgmt_v3_lock(global, MY_UUID, &op_errno, "global");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_LOCK_GET_FAIL,
                   "Unable to acquire lock for %s", global);
            gf_asprintf(&op_errstr,
                        "Another transaction is in progress "
                        "for %s. Please try again after some time.",
                        global);
            is_global = _gf_false;
            goto out;
        }
    }

    is_acquired = _gf_true;

local_locking_done:

    /* If no volname is given as a part of the command, locks will
     * not be held */
    if (volname || cluster_lock || is_global) {
        ret = gd_lock_op_phase(conf, op, op_ctx, &op_errstr, *txn_id,
                               &txn_opinfo, cluster_lock);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_LOCK_FAIL,
                   "Locking Peers Failed.");
            goto out;
        }
    }

    ret = glusterd_op_build_payload(&req_dict, &op_errstr, op_ctx);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_OP_PAYLOAD_BUILD_FAIL,
               LOGSTR_BUILD_PAYLOAD, gd_op_list[op]);
        if (op_errstr == NULL)
            gf_asprintf(&op_errstr, OPERRSTR_BUILD_PAYLOAD);
        goto out;
    }

    ret = gd_stage_op_phase(op, op_ctx, req_dict, &op_errstr, &txn_opinfo);
    if (ret)
        goto out;

    ret = gd_brick_op_phase(op, op_ctx, req_dict, &op_errstr);
    if (ret)
        goto out;

    ret = gd_commit_op_phase(op, op_ctx, req_dict, &op_errstr, &txn_opinfo);
    if (ret)
        goto out;

    ret = 0;
out:
    op_ret = ret;
    if (txn_id) {
        if (global)
            (void)gd_unlock_op_phase(conf, op, &op_ret, req, op_ctx, op_errstr,
                                     global, is_acquired, *txn_id, &txn_opinfo,
                                     cluster_lock);
        else
            (void)gd_unlock_op_phase(conf, op, &op_ret, req, op_ctx, op_errstr,
                                     volname, is_acquired, *txn_id, &txn_opinfo,
                                     cluster_lock);

        /* Clearing the transaction opinfo */
        ret = glusterd_clear_txn_opinfo(txn_id);
        if (ret)
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_TRANS_OPINFO_CLEAR_FAIL,
                   "Unable to clear transaction's "
                   "opinfo for transaction ID : %s",
                   uuid_utoa(*txn_id));
    }

    if (op_ret && (op_errno == 0))
        op_errno = EG_INTRNL;

    glusterd_op_send_cli_response(op, op_ret, op_errno, req, op_ctx, op_errstr);

    if (volname)
        GF_FREE(volname);

    if (req_dict)
        dict_unref(req_dict);

    if (op_errstr) {
        GF_FREE(op_errstr);
        op_errstr = NULL;
    }

    return;
}

int32_t
glusterd_op_begin_synctask(rpcsvc_request_t *req, glusterd_op_t op, void *dict)
{
    int ret = 0;

    ret = dict_set_int32(dict, GD_SYNC_OPCODE_KEY, op);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "dict set failed for setting operations");
        goto out;
    }

    gd_sync_task_begin(dict, req);
    ret = 0;
out:

    return ret;
}
