/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "changelog-rpc-common.h"
#include "changelog-messages.h"

#include "syscall.h"
/**
*****************************************************
                  Client Interface
*****************************************************
*/

/**
 * Initialize and return an RPC client object for a given unix
 * domain socket.
 */

void *
changelog_rpc_poller (void *arg)
{
        xlator_t *this = arg;

        (void) event_dispatch (this->ctx->event_pool);
        return NULL;
}

struct rpc_clnt *
changelog_rpc_client_init (xlator_t *this, void *cbkdata,
                           char *sockfile, rpc_clnt_notify_t fn)
{
        int              ret         = 0;
        struct rpc_clnt *rpc         = NULL;
        dict_t          *options     = NULL;

        if (!cbkdata)
                cbkdata = this;

        options = dict_new ();
        if (!options)
                goto error_return;

        ret = rpc_transport_unix_options_build (&options, sockfile, 0);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_RPC_BUILD_ERROR,
                        "failed to build rpc options");
                goto dealloc_dict;
        }

        rpc = rpc_clnt_new (options, this, this->name, 16);
        if (!rpc)
                goto dealloc_dict;

        ret = rpc_clnt_register_notify (rpc, fn, cbkdata);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_NOTIFY_REGISTER_FAILED,
                        "failed to register notify");
                goto dealloc_rpc_clnt;
        }

        ret = rpc_clnt_start (rpc);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_RPC_START_ERROR,
                        "failed to start rpc");
                goto dealloc_rpc_clnt;
        }

        return rpc;

 dealloc_rpc_clnt:
        rpc_clnt_unref (rpc);
 dealloc_dict:
        dict_unref (options);
 error_return:
        return NULL;
}

/**
 * Generic RPC client routine to dispatch a request to an
 * RPC server.
 */
int
changelog_rpc_sumbit_req (struct rpc_clnt *rpc, void *req,
                          call_frame_t *frame, rpc_clnt_prog_t *prog,
                          int procnum, struct iovec *payload, int payloadcnt,
                          struct iobref *iobref, xlator_t *this,
                          fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        int           ret        = 0;
        int           count      = 0;
        struct iovec  iov        = {0, };
        struct iobuf *iobuf      = NULL;
        char          new_iobref = 0;
        ssize_t       xdr_size   = 0;

        GF_ASSERT (this);

        if (req) {
                xdr_size = xdr_sizeof (xdrproc, req);

                iobuf = iobuf_get2 (this->ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto out;
                };

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

        ret = rpc_clnt_submit (rpc, prog, procnum, cbkfn, &iov, count,
                               payload, payloadcnt, iobref, frame, NULL,
                               0, NULL, 0, NULL);

 out:
        if (new_iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);
        return ret;
}

/**
 * Entry point to perform a remote procedure call
 */
int
changelog_invoke_rpc (xlator_t *this, struct rpc_clnt *rpc,
                      rpc_clnt_prog_t *prog, int procidx, void *arg)
{
        int                   ret   = 0;
        call_frame_t         *frame = NULL;
        rpc_clnt_procedure_t *proc  = NULL;

        if (!this || !prog)
                goto error_return;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_CREATE_FRAME_FAILED,
                        "failed to create frame");
                goto error_return;
        }

        proc = &prog->proctable[procidx];
        if (proc->fn)
                ret = proc->fn (frame, this, arg);

        STACK_DESTROY (frame->root);
        return ret;

 error_return:
        return -1;
}

/**
*****************************************************
                  Server Interface
*****************************************************
*/

struct iobuf *
__changelog_rpc_serialize_reply (rpcsvc_request_t *req, void *arg,
                                 struct iovec *outmsg, xdrproc_t xdrproc)
{
        struct iobuf *iob      = NULL;
        ssize_t       retlen   = 0;
        ssize_t       rsp_size = 0;

        rsp_size = xdr_sizeof (xdrproc, arg);
        iob = iobuf_get2 (req->svc->ctx->iobuf_pool, rsp_size);
        if (!iob)
                goto error_return;

        iobuf_to_iovec (iob, outmsg);

        retlen = xdr_serialize_generic (*outmsg, arg, xdrproc);
        if (retlen == -1)
                goto unref_iob;

        outmsg->iov_len = retlen;
        return iob;

 unref_iob:
        iobuf_unref (iob);
 error_return:
        return NULL;
}

int
changelog_rpc_sumbit_reply (rpcsvc_request_t *req,
                            void *arg, struct iovec *payload, int payloadcount,
                            struct iobref *iobref, xdrproc_t xdrproc)
{
        int           ret        = -1;
        struct iobuf *iob        = NULL;
        struct iovec  iov        = {0,};
        char          new_iobref = 0;

        if (!req)
                goto return_ret;

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref)
                        goto return_ret;
                new_iobref = 1;
        }

        iob = __changelog_rpc_serialize_reply (req, arg, &iov, xdrproc);
        if (!iob)
                gf_msg ("", GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_RPC_SUBMIT_REPLY_FAILED,
                        "failed to serialize reply");
        else
                iobref_add (iobref, iob);

        ret = rpcsvc_submit_generic (req, &iov,
                                     1, payload, payloadcount, iobref);

        if (new_iobref)
                iobref_unref (iobref);
        if (iob)
                iobuf_unref (iob);
 return_ret:
        return ret;
}

void
changelog_rpc_server_destroy (xlator_t *this, rpcsvc_t *rpc, char *sockfile,
                              rpcsvc_notify_t fn, struct rpcsvc_program **progs)
{
        rpcsvc_listener_t      *listener = NULL;
        rpcsvc_listener_t      *next     = NULL;
        struct rpcsvc_program  *prog     = NULL;

        while (*progs) {
                prog = *progs;
                (void) rpcsvc_program_unregister (rpc, prog);
        }

        list_for_each_entry_safe (listener, next, &rpc->listeners, list) {
                rpcsvc_listener_destroy (listener);
        }

        (void) rpcsvc_unregister_notify (rpc, fn, this);
        sys_unlink (sockfile);

        GF_FREE (rpc);
}

rpcsvc_t *
changelog_rpc_server_init (xlator_t *this, char *sockfile, void *cbkdata,
                           rpcsvc_notify_t fn, struct rpcsvc_program **progs)
{
        int                    ret     = 0;
        rpcsvc_t              *rpc     = NULL;
        dict_t                *options = NULL;
        struct rpcsvc_program *prog    = NULL;

        if (!cbkdata)
                cbkdata = this;

        options = dict_new ();
        if (!options)
                goto error_return;

        ret = rpcsvc_transport_unix_options_build (&options, sockfile);
        if (ret)
                goto dealloc_dict;

        rpc = rpcsvc_init (this, this->ctx, options, 8);
        if (rpc == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_RPC_START_ERROR,
                        "failed to init rpc");
                goto dealloc_dict;
        }

        ret = rpcsvc_register_notify (rpc, fn, cbkdata);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_NOTIFY_REGISTER_FAILED,
                        "failed to register notify function");
                goto dealloc_rpc;
        }

        ret = rpcsvc_create_listeners (rpc, options, this->name);
        if (ret != 1) {
                gf_msg_debug (this->name,
                              0, "failed to create listeners");
                goto dealloc_rpc;
        }

        while (*progs) {
                prog = *progs;
                ret = rpcsvc_program_register (rpc, prog);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_MSG_PROGRAM_NAME_REG_FAILED,
                                "cannot register program "
                                "(name: %s, prognum: %d, pogver: %d)",
                                prog->progname, prog->prognum, prog->progver);
                        goto dealloc_rpc;
                }

                progs++;
        }

        dict_unref (options);
        return rpc;

 dealloc_rpc:
        GF_FREE (rpc);
 dealloc_dict:
        dict_unref (options);
 error_return:
        return NULL;
}
