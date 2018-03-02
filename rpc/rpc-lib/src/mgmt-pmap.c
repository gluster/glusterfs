/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "portmap-xdr.h"
#include "protocol-common.h"
#include "rpc-clnt.h"
#include "xdr-generic.h"
#include "xlator.h"

/* Defining a minimal RPC client program for portmap signout
 */
char *clnt_pmap_signout_procs[GF_PMAP_MAXVALUE] = {
        [GF_PMAP_SIGNOUT]     = "SIGNOUT",
};


rpc_clnt_prog_t clnt_pmap_signout_prog = {
        .progname  = "Gluster Portmap",
        .prognum   = GLUSTER_PMAP_PROGRAM,
        .progver   = GLUSTER_PMAP_VERSION,
        .procnames = clnt_pmap_signout_procs,
};

static int
mgmt_pmap_signout_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        pmap_signout_rsp  rsp   = {0,};
        int              ret   = 0;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_signout_rsp);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to register the port with glusterd");
                goto out;
        }
out:
        return 0;
}

int
rpc_clnt_mgmt_pmap_signout (glusterfs_ctx_t *ctx, char *brickname)
{
        int               ret = 0;
        pmap_signout_req  req = {0, };
        call_frame_t     *frame = NULL;
        cmd_args_t       *cmd_args = NULL;
        char              brick_name[PATH_MAX]  = {0,};
        struct iovec      iov = {0, };
        struct iobuf     *iobuf = NULL;
        struct iobref    *iobref = NULL;
        ssize_t           xdr_size = 0;

        frame = create_frame (THIS, ctx->pool);
        cmd_args = &ctx->cmd_args;

        if (!cmd_args->brick_port && (!cmd_args->brick_name || !brickname)) {
                gf_log ("fsd-mgmt", GF_LOG_DEBUG,
                        "portmapper signout arguments not given");
                goto out;
        }

        if (cmd_args->volfile_server_transport &&
            !strcmp(cmd_args->volfile_server_transport, "rdma")) {
                snprintf (brick_name, sizeof(brick_name), "%s.rdma",
                          cmd_args->brick_name);
                req.brick = brick_name;
        } else {
                if (brickname)
                        req.brick = brickname;
                else
                        req.brick = cmd_args->brick_name;
        }

        req.port  = cmd_args->brick_port;
        req.rdma_port = cmd_args->brick_port2;

        /* mgmt_submit_request is not available in libglusterfs.
         * Need to serialize and submit manually.
         */
        iobref = iobref_new ();
        if (!iobref) {
                goto out;
        }

        xdr_size = xdr_sizeof ((xdrproc_t)xdr_pmap_signout_req, &req);
        iobuf = iobuf_get2 (ctx->iobuf_pool, xdr_size);
        if (!iobuf) {
                goto out;
        };

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = iobuf_pagesize (iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic (iov, &req,
                                     (xdrproc_t)xdr_pmap_signout_req);
        if (ret == -1) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to create XDR payload");
                goto out;
        }
        iov.iov_len = ret;

        ret = rpc_clnt_submit (ctx->mgmt, &clnt_pmap_signout_prog,
                               GF_PMAP_SIGNOUT, mgmt_pmap_signout_cbk,
                               &iov, 1,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);
out:
        if (iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);
        return ret;
}
