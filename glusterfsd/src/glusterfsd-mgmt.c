/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "stack.h"
#include "dict.h"
#include "event.h"
#include "defaults.h"

#include "rpc-clnt.h"
#include "protocol-common.h"
#include "glusterfs3.h"
#include "portmap.h"

static char is_mgmt_rpc_reconnect;

typedef ssize_t (*mgmt_serialize_t) (struct iovec outmsg, void *args);


char *clnt_pmap_procs[GF_PMAP_MAXVALUE] = {
        [GF_PMAP_NULL]        = "NULL",
        [GF_PMAP_PORTBYBRICK] = "PORTBYBRICK",
        [GF_PMAP_BRICKBYPORT] = "BRICKBYPORT",
        [GF_PMAP_SIGNIN]      = "SIGNIN",
        [GF_PMAP_SIGNOUT]     = "SIGNOUT",
        [GF_PMAP_SIGNUP]      = "SIGNUP",
};


rpc_clnt_prog_t clnt_pmap_prog = {
        .progname  = "Gluster Portmap",
        .prognum   = GLUSTER_PMAP_PROGRAM,
        .progver   = GLUSTER_PMAP_VERSION,
        .procnames = clnt_pmap_procs,
};

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = "NULL",
        [GF_HNDSK_SETVOLUME]    = "SETVOLUME",
        [GF_HNDSK_GETSPEC]      = "GETSPEC",
        [GF_HNDSK_PING]         = "PING",
};

rpc_clnt_prog_t clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};


int glusterfs_mgmt_pmap_signin (glusterfs_ctx_t *ctx);
int glusterfs_volfile_fetch (glusterfs_ctx_t *ctx);
int glusterfs_process_volfp (glusterfs_ctx_t *ctx, FILE *fp);

int
mgmt_submit_request (void *req, call_frame_t *frame,
                     glusterfs_ctx_t *ctx,
                     rpc_clnt_prog_t *prog, int procnum,
                     mgmt_serialize_t sfunc, fop_cbk_fn_t cbkfn)
{
        int                     ret         = -1;
        int                     count      = 0;
        struct iovec            iov         = {0, };
        struct iobuf            *iobuf = NULL;
        struct iobref           *iobref = NULL;

        iobref = iobref_new ();
        if (!iobref) {
                goto out;
        }

        iobuf = iobuf_get (ctx->iobuf_pool);
        if (!iobuf) {
                goto out;
        };

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = 128 * GF_UNIT_KB;


        /* Create the xdr payload */
        if (req && sfunc) {
                ret = sfunc (iov, req);
                if (ret == -1) {
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (ctx->mgmt, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

out:
        if (iobref)
                iobref_unref (iobref);

        return ret;
}


/* XXX: move these into @ctx */
static char oldvolfile[131072];
static int oldvollen = 0;
static void *timer;

int
mgmt_getspec_cbk (struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
        gf_getspec_rsp           rsp   = {0,};
        call_frame_t            *frame = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        int                      ret   = 0;
        ssize_t                  size = 0;
        FILE                    *tmpfp = NULL;
        struct timeval           tv = {0, };

        frame = myframe;
        ctx = frame->this->ctx;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_getspec_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the 'volume file' from server");
                goto out;
        }

        size = rsp.op_ret;

        if (size == oldvollen && (memcmp (oldvolfile, rsp.spec, size) == 0))
                goto out;

        tmpfp = tmpfile ();
        if (!tmpfp)
                goto out;

        fwrite (rsp.spec, size, 1, tmpfp);
        fflush (tmpfp);

        ret = glusterfs_process_volfp (ctx, tmpfp);
        if (ret)
                goto out;

        oldvollen = size;
        memcpy (oldvolfile, rsp.spec, size);
        if (!is_mgmt_rpc_reconnect) {
                glusterfs_mgmt_pmap_signin (ctx);
                is_mgmt_rpc_reconnect = 1;
        }

out:
        tv.tv_sec = 1;
        timer = gf_timer_call_after (ctx, tv,
                                     (gf_timer_cbk_t) glusterfs_volfile_fetch,
                                     ctx);

        STACK_DESTROY (frame->root);

        if (rsp.spec)
                free (rsp.spec);

        return 0;
}


int
glusterfs_volfile_fetch (glusterfs_ctx_t *ctx)
{
        cmd_args_t       *cmd_args = NULL;
        gf_getspec_req    req = {0, };
        int               ret = 0;
        call_frame_t     *frame = NULL;

        {
                if (timer)
                        gf_timer_call_cancel (ctx, timer);
                timer = NULL;
        }

        cmd_args = &ctx->cmd_args;

        frame = create_frame (THIS, ctx->pool);

        req.key = cmd_args->volfile_id;
        req.flags = 0;

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
                                   GF_HNDSK_GETSPEC, xdr_from_getspec_req,
                                   mgmt_getspec_cbk);
        return ret;
}


static int
mgmt_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                 void *data)
{
        xlator_t        *this = NULL;
        glusterfs_ctx_t *ctx = NULL;
        int              ret = 0;

        this = mydata;
        ctx = this->ctx;

        switch (event) {
        case RPC_CLNT_CONNECT:
                rpc_clnt_set_connected (ctx->mgmt);

                ret =  glusterfs_volfile_fetch (ctx);

                if (is_mgmt_rpc_reconnect)
                        glusterfs_mgmt_pmap_signin (ctx);
                break;
        default:
                break;
        }

        return 0;
}


int
glusterfs_mgmt_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t              *cmd_args = NULL;
        struct rpc_clnt         *rpc = NULL;
        struct rpc_clnt_config  rpc_cfg = {0,};
        dict_t                  *options = NULL;
        int                     ret = -1;
        int                     port = 6969;
        char                    *host = NULL;

        cmd_args = &ctx->cmd_args;

        if (ctx->mgmt)
                return 0;

        options = dict_new ();
        if (!options)
                goto out;

        if (cmd_args->volfile_server_port)
                port = cmd_args->volfile_server_port;

        host = "localhost";
        if (cmd_args->volfile_server)
                host = cmd_args->volfile_server;

        rpc_cfg.remote_host = host;
        rpc_cfg.remote_port = port;

        ret = dict_set_int32 (options, "remote-port", port);
        if (ret)
                goto out;

        ret = dict_set_str (options, "remote-host", host);
        if (ret)
                goto out;

        ret = dict_set_str (options, "transport.address-family", "inet");
        if (ret)
                goto out;

        rpc = rpc_clnt_init (&rpc_cfg, options, THIS->ctx, THIS->name);
        if (!rpc) {
                ret = -1;
                goto out;
        }

        ctx->mgmt = rpc;

        ret = rpc_clnt_register_notify (rpc, mgmt_rpc_notify, THIS);
        if (ret)
                goto out;

out:
        return ret;
}


static int
mgmt_pmap_signin_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        pmap_signin_rsp  rsp   = {0,};
        call_frame_t    *frame = NULL;
        int              ret   = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_pmap_signin_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to register the port with glusterd");
                goto out;
        }
out:

        STACK_DESTROY (frame->root);
        return 0;
}

int
glusterfs_mgmt_pmap_signin (glusterfs_ctx_t *ctx)
{
        call_frame_t     *frame = NULL;
        pmap_signin_req   req = {0, };
        int               ret = -1;
        cmd_args_t       *cmd_args = NULL;

        frame = create_frame (THIS, ctx->pool);
        cmd_args = &ctx->cmd_args;

        if (!cmd_args->brick_port || !cmd_args->brick_name) {
                gf_log ("fsd-mgmt", GF_LOG_DEBUG,
                        "portmapper signin arguments not given");
                goto out;
        }

        req.port  = cmd_args->brick_port;
        req.brick = cmd_args->brick_name;

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_pmap_prog,
                                   GF_PMAP_SIGNIN, xdr_from_pmap_signin_req,
                                   mgmt_pmap_signin_cbk);

out:
        return ret;
}


static int
mgmt_pmap_signout_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        pmap_signout_rsp  rsp   = {0,};
        call_frame_t    *frame = NULL;
        int              ret   = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_pmap_signout_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to register the port with glusterd");
                goto out;
        }
out:
        if (frame)
                STACK_DESTROY (frame->root);
        return 0;
}


int
glusterfs_mgmt_pmap_signout (glusterfs_ctx_t *ctx)
{
        int               ret = 0;
        pmap_signout_req  req = {0, };
        call_frame_t     *frame = NULL;
        cmd_args_t       *cmd_args = NULL;

        frame = create_frame (THIS, ctx->pool);
        cmd_args = &ctx->cmd_args;

        if (!cmd_args->brick_port || !cmd_args->brick_name) {
                gf_log ("fsd-mgmt", GF_LOG_DEBUG,
                        "portmapper signout arguments not given");
                goto out;
        }

        req.port  = cmd_args->brick_port;
        req.brick = cmd_args->brick_name;

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_pmap_prog,
                                   GF_PMAP_SIGNOUT, xdr_from_pmap_signout_req,
                                   mgmt_pmap_signout_cbk);
out:
        return ret;
}
