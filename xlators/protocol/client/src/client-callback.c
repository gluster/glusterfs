/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "client.h"
#include "rpc-clnt.h"
#include "client-messages.h"

static int
client_cbk_null(struct rpc_clnt *rpc, void *mydata, void *data)
{
    gf_smsg(THIS->name, GF_LOG_WARNING, 0, PC_MSG_FUNCTION_CALL_ERROR, NULL);
    return 0;
}

static int
client_cbk_fetchspec(struct rpc_clnt *rpc, void *mydata, void *data)
{
    gf_smsg(THIS->name, GF_LOG_WARNING, 0, PC_MSG_FUNCTION_CALL_ERROR, NULL);
    return 0;
}

static int
client_cbk_ino_flush(struct rpc_clnt *rpc, void *mydata, void *data)
{
    gf_smsg(THIS->name, GF_LOG_WARNING, 0, PC_MSG_FUNCTION_CALL_ERROR, NULL);
    return 0;
}

static int
client_cbk_recall_lease(struct rpc_clnt *rpc, void *mydata, void *data)
{
    int ret = -1;
    struct iovec *iov = NULL;
    struct gf_upcall upcall_data = {
        0,
    };
    struct gf_upcall_recall_lease rl_data = {
        0,
    };
    gfs3_recall_lease_req recall_lease = {
        {
            0,
        },
    };
    xlator_t *this;

    GF_VALIDATE_OR_GOTO("client-callback", data, out);

    this = THIS;
    iov = (struct iovec *)data;
    ret = xdr_to_generic(*iov, &recall_lease,
                         (xdrproc_t)xdr_gfs3_recall_lease_req);

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, -ret, PC_MSG_RECALL_LEASE_FAIL,
                NULL);
        goto out;
    }

    upcall_data.data = &rl_data;
    ret = gf_proto_recall_lease_to_upcall(this, &recall_lease, &upcall_data);
    if (ret < 0)
        goto out;

    upcall_data.event_type = GF_UPCALL_RECALL_LEASE;

    gf_msg_trace(this->name, 0, "Upcall gfid = %s, ret = %d", recall_lease.gfid,
                 ret);

    default_notify(this, GF_EVENT_UPCALL, &upcall_data);

out:
    if (recall_lease.xdata.xdata_val)
        free(recall_lease.xdata.xdata_val);

    if (rl_data.dict)
        dict_unref(rl_data.dict);

    return ret;
}

static int
client_cbk_cache_invalidation(struct rpc_clnt *rpc, void *mydata, void *data)
{
    int ret = -1;
    struct iovec *iov = NULL;
    struct gf_upcall upcall_data = {
        0,
    };
    struct gf_upcall_cache_invalidation ca_data = {
        0,
    };
    gfs3_cbk_cache_invalidation_req ca_req = {
        0,
    };
    xlator_t *this = THIS;

    gf_msg_trace(this->name, 0, "Upcall callback is called");

    if (!data)
        goto out;

    iov = (struct iovec *)data;
    ret = xdr_to_generic(*iov, &ca_req,
                         (xdrproc_t)xdr_gfs3_cbk_cache_invalidation_req);

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, -ret,
                PC_MSG_CACHE_INVALIDATION_FAIL, NULL);
        goto out;
    }

    upcall_data.data = &ca_data;
    ret = gf_proto_cache_invalidation_to_upcall(this, &ca_req, &upcall_data);
    if (ret < 0)
        goto out;

    gf_msg_trace(this->name, 0,
                 "Cache invalidation cbk received for gfid:"
                 " %s, ret = %d",
                 ca_req.gfid, ret);

    default_notify(this, GF_EVENT_UPCALL, &upcall_data);

out:
    if (ca_req.gfid)
        free(ca_req.gfid);

    if (ca_req.xdata.xdata_val)
        free(ca_req.xdata.xdata_val);

    if (ca_data.dict)
        dict_unref(ca_data.dict);

    return 0;
}

static int
client_cbk_child_up(struct rpc_clnt *rpc, void *mydata, void *data)
{
    clnt_conf_t *conf = NULL;
    xlator_t *this = THIS;

    GF_VALIDATE_OR_GOTO("client", this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    gf_msg_debug(this->name, 0, "Received CHILD_UP");
    conf->child_up = _gf_true;

    this->notify(this, GF_EVENT_CHILD_UP, NULL);
out:
    return 0;
}

static int
client_cbk_child_down(struct rpc_clnt *rpc, void *mydata, void *data)
{
    clnt_conf_t *conf = NULL;
    xlator_t *this = THIS;

    GF_VALIDATE_OR_GOTO("client", this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    gf_msg_debug(this->name, 0, "Received CHILD_DOWN");
    conf->child_up = _gf_false;

    this->notify(this, GF_EVENT_CHILD_DOWN, NULL);
out:
    return 0;
}

static int
client_cbk_inodelk_contention(struct rpc_clnt *rpc, void *mydata, void *data)
{
    int ret = -1;
    struct iovec *iov = NULL;
    struct gf_upcall upcall_data = {
        0,
    };
    struct gf_upcall_inodelk_contention lc = {
        {
            0,
        },
    };
    gfs4_inodelk_contention_req proto_lc = {
        {
            0,
        },
    };
    xlator_t *this;

    GF_VALIDATE_OR_GOTO("client-callback", data, out);

    this = THIS;
    iov = (struct iovec *)data;
    ret = xdr_to_generic(*iov, &proto_lc,
                         (xdrproc_t)xdr_gfs4_inodelk_contention_req);

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, -ret,
                PC_MSG_INODELK_CONTENTION_FAIL, NULL);
        goto out;
    }

    upcall_data.data = &lc;
    ret = gf_proto_inodelk_contention_to_upcall(this, &proto_lc, &upcall_data);
    if (ret < 0)
        goto out;

    upcall_data.event_type = GF_UPCALL_INODELK_CONTENTION;

    default_notify(this, GF_EVENT_UPCALL, &upcall_data);

out:
    if (proto_lc.domain)
        free(proto_lc.domain);

    if (proto_lc.xdata.xdata_val)
        free(proto_lc.xdata.xdata_val);

    if (lc.xdata)
        dict_unref(lc.xdata);

    return ret;
}

static int
client_cbk_entrylk_contention(struct rpc_clnt *rpc, void *mydata, void *data)
{
    int ret = -1;
    struct iovec *iov = NULL;
    struct gf_upcall upcall_data = {
        0,
    };
    struct gf_upcall_entrylk_contention lc = {
        0,
    };
    gfs4_entrylk_contention_req proto_lc = {
        {
            0,
        },
    };
    xlator_t *this;

    GF_VALIDATE_OR_GOTO("client-callback", data, out);

    this = THIS;
    iov = (struct iovec *)data;
    ret = xdr_to_generic(*iov, &proto_lc,
                         (xdrproc_t)xdr_gfs4_entrylk_contention_req);

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, -ret,
                PC_MSG_ENTRYLK_CONTENTION_FAIL, NULL);
        goto out;
    }

    upcall_data.data = &lc;
    ret = gf_proto_entrylk_contention_to_upcall(this, &proto_lc, &upcall_data);
    if (ret < 0)
        goto out;

    upcall_data.event_type = GF_UPCALL_ENTRYLK_CONTENTION;

    default_notify(this, GF_EVENT_UPCALL, &upcall_data);

out:
    if (proto_lc.name)
        free(proto_lc.name);

    if (proto_lc.domain)
        free(proto_lc.domain);

    if (proto_lc.xdata.xdata_val)
        free(proto_lc.xdata.xdata_val);

    if (lc.xdata)
        dict_unref(lc.xdata);

    return ret;
}

static rpcclnt_cb_actor_t gluster_cbk_actors[GF_CBK_MAXVALUE] = {
    [GF_CBK_NULL] = {"NULL", client_cbk_null, GF_CBK_NULL},
    [GF_CBK_FETCHSPEC] = {"FETCHSPEC", client_cbk_fetchspec, GF_CBK_FETCHSPEC},
    [GF_CBK_INO_FLUSH] = {"INO_FLUSH", client_cbk_ino_flush, GF_CBK_INO_FLUSH},
    [GF_CBK_CACHE_INVALIDATION] = {"CACHE_INVALIDATION",
                                   client_cbk_cache_invalidation,
                                   GF_CBK_CACHE_INVALIDATION},
    [GF_CBK_CHILD_UP] = {"CHILD_UP", client_cbk_child_up, GF_CBK_CHILD_UP},
    [GF_CBK_CHILD_DOWN] = {"CHILD_DOWN", client_cbk_child_down,
                           GF_CBK_CHILD_DOWN},
    [GF_CBK_RECALL_LEASE] = {"RECALL_LEASE", client_cbk_recall_lease,
                             GF_CBK_RECALL_LEASE},
    [GF_CBK_INODELK_CONTENTION] = {"INODELK_CONTENTION",
                                   client_cbk_inodelk_contention,
                                   GF_CBK_INODELK_CONTENTION},
    [GF_CBK_ENTRYLK_CONTENTION] = {"ENTRYLK_CONTENTION",
                                   client_cbk_entrylk_contention,
                                   GF_CBK_ENTRYLK_CONTENTION},
};

struct rpcclnt_cb_program gluster_cbk_prog = {
    .progname = "GlusterFS Callback",
    .prognum = GLUSTER_CBK_PROGRAM,
    .progver = GLUSTER_CBK_VERSION,
    .actors = gluster_cbk_actors,
    .numactors = GF_CBK_MAXVALUE,
};
