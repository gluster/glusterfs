/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <netdb.h>
#include <signal.h>
#include <libgen.h>

#include <sys/utsname.h>

#include <stdint.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
#include <mcheck.h>
#endif
#endif

#include "quota.h"
#include "quota-messages.h"

extern struct rpc_clnt_program quota_enforcer_clnt;

int32_t
quota_validate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent);

int
quota_enforcer_submit_request (void *req, call_frame_t *frame,
                               rpc_clnt_prog_t *prog,
                               int procnum, struct iobref *iobref,
                               xlator_t *this, fop_cbk_fn_t cbkfn,
                               xdrproc_t xdrproc)
{
        int           ret        = -1;
        int           count      = 0;
        struct iovec  iov        = {0, };
        struct iobuf *iobuf      = NULL;
        char          new_iobref = 0;
        ssize_t       xdr_size   = 0;
        quota_priv_t *priv       = NULL;

        GF_ASSERT (this);

        priv = this->private;

        if (req) {
                xdr_size = xdr_sizeof (xdrproc, req);
                iobuf = iobuf_get2 (this->ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto out;
                }

                if (!iobref) {
                        iobref = iobref_new ();
                        if (!iobref) {
                                goto out;
                        }

                        new_iobref = 1;
                }

                iobref_add (iobref, iobuf);

                iov.iov_base = iobuf->ptr;
                iov.iov_len  = iobuf_size (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (priv->rpc_clnt, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);
        ret = 0;

out:
        if (new_iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        return ret;
}

int
quota_enforcer_lookup_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *myframe)
{
        quota_local_t    *local       = NULL;
        call_frame_t     *frame       = NULL;
        int               ret         = 0;
        gfs3_lookup_rsp   rsp         = {0,};
        struct iatt       stbuf       = {0,};
        struct iatt       postparent  = {0,};
        int               op_errno    = EINVAL;
        dict_t           *xdata       = NULL;
        inode_t          *inode       = NULL;
        xlator_t         *this        = NULL;
        quota_priv_t     *priv        = NULL;
        struct timespec   retry_delay = {0,};
        gf_timer_t       *timer       = NULL;

        this = THIS;

        frame = myframe;
        local = frame->local;
        inode = local->validate_loc.inode;
        priv  = this->private;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gfs3_lookup_rsp);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        Q_MSG_XDR_DECODING_FAILED,
                        "XDR decoding failed");
                rsp.op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        op_errno = gf_error_to_errno (rsp.op_errno);
        gf_stat_to_iatt (&rsp.postparent, &postparent);

        if (rsp.op_ret == -1)
                goto out;

        rsp.op_ret = -1;
        gf_stat_to_iatt (&rsp.stat, &stbuf);

        GF_PROTOCOL_DICT_UNSERIALIZE (frame->this, xdata, (rsp.xdata.xdata_val),
                                      (rsp.xdata.xdata_len), rsp.op_ret,
                                      op_errno, out);

        if ((!gf_uuid_is_null (inode->gfid))
            && (gf_uuid_compare (stbuf.ia_gfid, inode->gfid) != 0)) {
                gf_msg_debug (frame->this->name, ESTALE,
                              "gfid changed for %s", local->validate_loc.path);
                rsp.op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        rsp.op_ret = 0;

out:
        rsp.op_errno = op_errno;

        /* We need to retry connecting to quotad on ENOTCONN error.
         * Suppose if there are two volumes vol1 and vol2,
         * and quota is enabled and limit is set on vol1.
         * Now if IO is happening on vol1 and quota is enabled/disabled
         * on vol2, quotad gets restarted and client will receive
         * ENOTCONN in the IO path of vol1
         */
        if (rsp.op_ret == -1 && rsp.op_errno == ENOTCONN) {
                if (local->quotad_conn_retry >= 12) {
                        priv->quotad_conn_status = 1;
                        gf_log (this->name, GF_LOG_WARNING, "failed to connect "
                                "to quotad after retry count %d)",
                                local->quotad_conn_retry);
                } else {
                        local->quotad_conn_retry++;
                }

                if (priv->quotad_conn_status == 0) {
                        /* retry connecting after 5secs for 12 retries
                         * (upto 60sec).
                         */
                        gf_log (this->name, GF_LOG_DEBUG, "retry connecting to "
                                "quotad (retry count %d)",
                                local->quotad_conn_retry);

                        retry_delay.tv_sec = 5;
                        retry_delay.tv_nsec = 0;
                        timer = gf_timer_call_after (this->ctx, retry_delay,
                                                     _quota_enforcer_lookup,
                                                     (void *) frame);
                        if (timer == NULL) {
                                gf_log (this->name, GF_LOG_WARNING, "failed to "
                                        "set quota_enforcer_lookup with timer");
                        } else {
                                goto clean;
                        }
                }
        } else {
                priv->quotad_conn_status = 0;
        }

        if (rsp.op_ret == -1) {
                /* any error other than ENOENT */
                if (rsp.op_errno != ENOENT)
                        gf_msg (this->name, GF_LOG_WARNING, rsp.op_errno,
                                Q_MSG_LOOKUP_FAILED,
                                "Getting cluster-wide size of directory failed "
                                "(path: %s gfid:%s)", local->validate_loc.path,
                                loc_gfid_utoa (&local->validate_loc));
                else
                        gf_msg_trace (this->name, ENOENT,
                                      "not found on remote node");

        } else if (local->quotad_conn_retry) {
                gf_log (this->name, GF_LOG_DEBUG, "connected to quotad after "
                        "retry count %d", local->quotad_conn_retry);
        }

        local->validate_cbk (frame, NULL, this, rsp.op_ret, rsp.op_errno, inode,
                             &stbuf, xdata, &postparent);

clean:
        if (xdata)
                dict_unref (xdata);

        free (rsp.xdata.xdata_val);

        return 0;
}

void
_quota_enforcer_lookup (void *data)
{
        quota_local_t          *local      = NULL;
        gfs3_lookup_req         req        = {{0,},};
        int                     ret        = 0;
        int                     op_errno   = ESTALE;
        quota_priv_t           *priv       = NULL;
        call_frame_t           *frame      = NULL;
        loc_t                  *loc        = NULL;
        xlator_t               *this       = NULL;
        char                   *dir_path       = NULL;

        frame = data;
        local = frame->local;
        this  = local->this;
        loc   = &local->validate_loc;

        priv = this->private;

        if (!(loc && loc->inode))
                goto unwind;

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (req.gfid, loc->inode->gfid, 16);
        else
                memcpy (req.gfid, loc->gfid, 16);

        if (local->validate_xdata) {
                GF_PROTOCOL_DICT_SERIALIZE (this, local->validate_xdata,
                                            (&req.xdata.xdata_val),
                                            req.xdata.xdata_len,
                                            op_errno, unwind);
        }

        if (loc->name)
                req.bname = (char *)loc->name;
        else
                req.bname = "";

        if (loc->path)
                dir_path = (char *)loc->path;
        else
                dir_path = "";

        ret = quota_enforcer_submit_request (&req, frame,
                                             priv->quota_enforcer,
                                             GF_AGGREGATOR_LOOKUP,
                                             NULL, this,
                                             quota_enforcer_lookup_cbk,
                                             (xdrproc_t)xdr_gfs3_lookup_req);

        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
			Q_MSG_RPC_SUBMIT_FAILED, "Couldn't send the request to "
                        "fetch cluster wide size of directory (path:%s gfid:%s)"
                        , dir_path, req.gfid);
        }

        GF_FREE (req.xdata.xdata_val);

        return;

unwind:
        local->validate_cbk (frame, NULL, this, -1, op_errno, NULL, NULL, NULL,
                             NULL);

        GF_FREE (req.xdata.xdata_val);

        return;
}

int
quota_enforcer_lookup (call_frame_t *frame, xlator_t *this, dict_t *xdata,
                       fop_lookup_cbk_t validate_cbk)
{
        quota_local_t          *local      = NULL;

        if (!frame || !this)
                goto unwind;

        local = frame->local;
        local->this = this;
        local->validate_cbk = validate_cbk;
        local->validate_xdata = dict_ref (xdata);

        _quota_enforcer_lookup (frame);

        return 0;

unwind:
        validate_cbk (frame, NULL, this, -1, ESTALE, NULL, NULL, NULL, NULL);

        return 0;
}

int
quota_enforcer_notify (struct rpc_clnt *rpc, void *mydata,
                       rpc_clnt_event_t event, void *data)
{
        xlator_t                *this = NULL;
        int                     ret = 0;

        this = mydata;

        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                gf_msg_trace (this->name, 0, "got RPC_CLNT_CONNECT");
                break;
        }

        case RPC_CLNT_DISCONNECT:
        {
                gf_msg_trace (this->name, 0, "got RPC_CLNT_DISCONNECT");
                break;
        }

        default:
                gf_msg_trace (this->name, 0,
                              "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        return ret;
}

int
quota_enforcer_blocking_connect (rpc_clnt_t *rpc)
{
        dict_t *options = NULL;
        int     ret     = -1;

        options = dict_new ();
        if (options == NULL)
                goto out;

        ret = dict_set_str (options, "non-blocking-io", "no");
        if (ret)
                goto out;

        rpc->conn.trans->reconfigure (rpc->conn.trans, options);

        rpc_clnt_start (rpc);

        ret = dict_set_str (options, "non-blocking-io", "yes");
        if (ret)
                goto out;

        rpc->conn.trans->reconfigure (rpc->conn.trans, options);

        ret = 0;
out:
        if (options)
                dict_unref (options);

        return ret;
}

//Returns a started rpc_clnt. Creates a new rpc_clnt if quota_priv doesn't have
//one already
struct rpc_clnt *
quota_enforcer_init (xlator_t *this, dict_t *options)
{
        struct rpc_clnt *rpc  = NULL;
        quota_priv_t    *priv = NULL;
        int              ret  = -1;

        priv = this->private;

        LOCK (&priv->lock);
        {
                if (priv->rpc_clnt) {
                        ret = 0;
                        rpc = priv->rpc_clnt;
                }
        }
        UNLOCK (&priv->lock);

        if (rpc)
                goto out;

        priv->quota_enforcer = &quota_enforcer_clnt;

        ret = dict_set_str (options, "transport.address-family", "unix");
        if (ret)
                goto out;

        ret = dict_set_str (options, "transport-type", "socket");
        if (ret)
                goto out;

        ret = dict_set_str (options, "transport.socket.connect-path",
                            "/var/run/gluster/quotad.socket");
        if (ret)
                goto out;

        rpc = rpc_clnt_new (options, this, this->name, 16);
        if (!rpc) {
                ret = -1;
                goto out;
        }

        ret = rpc_clnt_register_notify (rpc, quota_enforcer_notify, this);
        if (ret) {
                gf_msg ("quota", GF_LOG_ERROR, 0,
                        Q_MSG_RPCCLNT_REGISTER_NOTIFY_FAILED,
                        "failed to register notify");
                goto out;
        }

        ret = quota_enforcer_blocking_connect (rpc);
        if (ret)
                goto out;

        ret = 0;
out:
        if (ret) {
                if (rpc)
                        rpc_clnt_unref (rpc);
                rpc = NULL;
        }

        return rpc;
        }

struct rpc_clnt_procedure quota_enforcer_actors[GF_AGGREGATOR_MAXVALUE] = {
        [GF_AGGREGATOR_NULL]     = {"NULL", NULL},
        [GF_AGGREGATOR_LOOKUP]   = {"LOOKUP", NULL},
};

struct rpc_clnt_program quota_enforcer_clnt = {
        .progname  = "Quota enforcer",
        .prognum   = GLUSTER_AGGREGATOR_PROGRAM,
        .progver   = GLUSTER_AGGREGATOR_VERSION,
        .numproc   = GF_AGGREGATOR_MAXVALUE,
        .proctable = quota_enforcer_actors,
};
