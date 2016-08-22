/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "cli1-xdr.h"
#include "quota.h"
#include "quotad-helpers.h"
#include "quotad-aggregator.h"

struct rpcsvc_program quotad_aggregator_prog;

struct iobuf *
quotad_serialize_reply (rpcsvc_request_t *req, void *arg, struct iovec *outmsg,
                        xdrproc_t xdrproc)
{
        struct iobuf *iob      = NULL;
        ssize_t       retlen   = 0;
        ssize_t       xdr_size = 0;

        GF_VALIDATE_OR_GOTO ("server", req, ret);

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        if (arg && xdrproc) {
                xdr_size = xdr_sizeof (xdrproc, arg);
                iob = iobuf_get2 (req->svc->ctx->iobuf_pool, xdr_size);
                if (!iob) {
                        gf_log_callingfn (THIS->name, GF_LOG_ERROR,
                                          "Failed to get iobuf");
                        goto ret;
                };

                iobuf_to_iovec (iob, outmsg);
                /* Use the given serializer to translate the given C structure
                 * in arg to XDR format which will be written into the buffer
                 * in outmsg.
                 */
                /* retlen is used to received the error since size_t is unsigned and we
                 * need -1 for error notification during encoding.
                 */

                retlen = xdr_serialize_generic (*outmsg, arg, xdrproc);
                if (retlen == -1) {
                        /* Failed to Encode 'GlusterFS' msg in RPC is not exactly
                           failure of RPC return values.. Client should get
                           notified about this, so there are no missing frames */
                        gf_log_callingfn ("", GF_LOG_ERROR, "Failed to encode message");
                        req->rpc_err = GARBAGE_ARGS;
                        retlen = 0;
                }
        }
        outmsg->iov_len = retlen;
ret:
        return iob;
}

int
quotad_aggregator_submit_reply (call_frame_t *frame, rpcsvc_request_t *req,
                                void *arg, struct iovec *payload,
                                int payloadcount, struct iobref *iobref,
                                xdrproc_t xdrproc)
{
        struct iobuf              *iob        = NULL;
        int                        ret        = -1;
        struct iovec               rsp        = {0,};
        quotad_aggregator_state_t *state      = NULL;
        char                       new_iobref = 0;

        GF_VALIDATE_OR_GOTO ("server", req, ret);

        if (frame) {
                state = frame->root->state;
                frame->local = NULL;
        }

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto ret;
                }

                new_iobref = 1;
        }

        iob = quotad_serialize_reply (req, arg, &rsp, xdrproc);
        if (!iob) {
                gf_msg ("", GF_LOG_ERROR, 0, Q_MSG_DICT_SERIALIZE_FAIL,
                        "Failed to serialize reply");
                goto ret;
        }

        iobref_add (iobref, iob);

        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        iobuf_unref (iob);

        ret = 0;
ret:
        if (state) {
                quotad_aggregator_free_state (state);
        }

        if (frame)
                STACK_DESTROY (frame->root);

        if (new_iobref) {
                iobref_unref (iobref);
        }

        return ret;
}

int
quotad_aggregator_getlimit_cbk (xlator_t *this, call_frame_t *frame,
                                void *lookup_rsp)
{
        gfs3_lookup_rsp            *rsp    = lookup_rsp;
        gf_cli_rsp                 cli_rsp = {0,};
        dict_t                     *xdata  = NULL;
        quotad_aggregator_state_t  *state  = NULL;
        int                         ret    = -1;
        int                         type   = 0;

        GF_PROTOCOL_DICT_UNSERIALIZE (frame->this, xdata,
                                      (rsp->xdata.xdata_val),
                                      (rsp->xdata.xdata_len), rsp->op_ret,
                                      rsp->op_errno, out);

        if (xdata) {
                state = frame->root->state;
                ret = dict_get_int32 (state->xdata, "type", &type);
                if (ret < 0)
                        goto out;

                ret = dict_set_int32 (xdata, "type", type);
                if (ret < 0)
                        goto out;
        }

        ret = 0;
out:
        rsp->op_ret = ret;
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        Q_MSG_DICT_UNSERIALIZE_FAIL,
                        "failed to unserialize "
                        "nameless lookup rsp");
                goto reply;
        }
        cli_rsp.op_ret = rsp->op_ret;
        cli_rsp.op_errno = rsp->op_errno;
        cli_rsp.op_errstr = "";
        if (xdata) {
                GF_PROTOCOL_DICT_SERIALIZE (frame->this, xdata,
                                            (&cli_rsp.dict.dict_val),
                                            (cli_rsp.dict.dict_len),
                                            cli_rsp.op_errno, reply);
        }

reply:
        quotad_aggregator_submit_reply (frame, frame->local, (void*)&cli_rsp, NULL, 0,
                                        NULL, (xdrproc_t)xdr_gf_cli_rsp);

        dict_unref (xdata);
        GF_FREE (cli_rsp.dict.dict_val);
        return 0;
}

int
quotad_aggregator_getlimit (rpcsvc_request_t *req)
{
        call_frame_t              *frame = NULL;
        gf_cli_req                 cli_req = {{0}, };
        gf_cli_rsp                 cli_rsp = {0};
        gfs3_lookup_req            args  = {{0,},};
        quotad_aggregator_state_t *state = NULL;
        xlator_t                  *this  = NULL;
        dict_t                    *dict  = NULL;
        int                        ret   = -1, op_errno = 0;
        char                      *gfid_str = NULL;
        uuid_t                     gfid = {0};

        GF_VALIDATE_OR_GOTO ("quotad-aggregator", req, err);

        this = THIS;

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0)  {
                //failed to decode msg;
                gf_msg ("this->name", GF_LOG_ERROR, 0, Q_MSG_XDR_DECODE_ERROR,
                        "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto err;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();
                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                Q_MSG_DICT_UNSERIALIZE_FAIL,
                                "Failed to unserialize req-buffer to "
                                "dictionary");
                        goto err;
                }
        }

        ret = dict_get_str (dict, "gfid", &gfid_str);
        if (ret) {
                goto err;
        }

        gf_uuid_parse ((const char*)gfid_str, gfid);

        frame = quotad_aggregator_get_frame_from_req (req);
        if (frame == NULL) {
                cli_rsp.op_errno = ENOMEM;
                goto errx;
        }
        state = frame->root->state;
        state->xdata = dict;

        ret = dict_set_int32 (state->xdata, QUOTA_LIMIT_KEY, 42);
        if (ret)
                goto err;

        ret = dict_set_int32 (state->xdata, QUOTA_LIMIT_OBJECTS_KEY, 42);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, Q_MSG_ENOMEM,
                        "Failed to set QUOTA_LIMIT_OBJECTS_KEY");
                goto err;
        }

        ret = dict_set_int32 (state->xdata, QUOTA_SIZE_KEY, 42);
        if (ret)
                goto err;

        ret = dict_set_int32 (state->xdata, GET_ANCESTRY_PATH_KEY, 42);
        if (ret)
                goto err;

        memcpy (&args.gfid, &gfid, 16);

        args.bname           = alloca (req->msg[0].iov_len);
        args.xdata.xdata_val = alloca (req->msg[0].iov_len);

        ret = qd_nameless_lookup (this, frame, &args, state->xdata,
                                  quotad_aggregator_getlimit_cbk);
        if (ret) {
                cli_rsp.op_errno = ret;
                goto errx;
        }

        return ret;

err:
        cli_rsp.op_errno = op_errno;
errx:
        cli_rsp.op_ret = -1;
        cli_rsp.op_errstr = "";

        quotad_aggregator_getlimit_cbk (this, frame, &cli_rsp);
        if (dict)
                dict_unref (dict);

        return ret;
}

int
quotad_aggregator_lookup_cbk (xlator_t *this, call_frame_t *frame,
                              void *rsp)
{
        quotad_aggregator_submit_reply (frame, frame->local, rsp, NULL, 0, NULL,
                                        (xdrproc_t)xdr_gfs3_lookup_rsp);

        return 0;
}


int
quotad_aggregator_lookup (rpcsvc_request_t *req)
{
        call_frame_t              *frame = NULL;
        gfs3_lookup_req            args  = {{0,},};
        int                        ret   = -1, op_errno = 0;
        gfs3_lookup_rsp            rsp   = {0,};
        quotad_aggregator_state_t *state = NULL;
        xlator_t                  *this  = NULL;

        GF_VALIDATE_OR_GOTO ("quotad-aggregator", req, err);

        this = THIS;

        args.bname           = alloca (req->msg[0].iov_len);
        args.xdata.xdata_val = alloca (req->msg[0].iov_len);

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gfs3_lookup_req);
        if (ret < 0) {
                rsp.op_errno = EINVAL;
                goto err;
        }

        frame = quotad_aggregator_get_frame_from_req (req);
        if (frame == NULL) {
                rsp.op_errno = ENOMEM;
                goto err;
        }

        state = frame->root->state;

        GF_PROTOCOL_DICT_UNSERIALIZE (this, state->xdata,
                                      (args.xdata.xdata_val),
                                      (args.xdata.xdata_len), ret,
                                      op_errno, err);


        ret = qd_nameless_lookup (this, frame, &args, state->xdata,
                                  quotad_aggregator_lookup_cbk);
        if (ret) {
                rsp.op_errno = ret;
                goto err;
        }

        return ret;

err:
        rsp.op_ret = -1;
        rsp.op_errno = op_errno;

        quotad_aggregator_lookup_cbk (this, frame, &rsp);
        return ret;
}

int
quotad_aggregator_rpc_notify (rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                              void *data)
{
        if (!xl || !data) {
                gf_log_callingfn ("server", GF_LOG_WARNING,
                                  "Calling rpc_notify without initializing");
                goto out;
        }

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
                break;

        case RPCSVC_EVENT_DISCONNECT:
                break;

        default:
                break;
        }

out:
        return 0;
}

int
quotad_aggregator_init (xlator_t *this)
{
        quota_priv_t *priv = NULL;
        int           ret  = -1;

        priv = this->private;

        if (priv->rpcsvc) {
                /* Listener already created */
                return 0;
        }

        ret = dict_set_str (this->options, "transport.address-family", "unix");
        if (ret)
                goto out;

        ret = dict_set_str (this->options, "transport-type", "socket");
        if (ret)
                goto out;

        ret = dict_set_str (this->options, "transport.socket.listen-path",
                            "/var/run/gluster/quotad.socket");
        if (ret)
                goto out;

        /* RPC related */
        priv->rpcsvc = rpcsvc_init (this, this->ctx, this->options, 0);
        if (priv->rpcsvc == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        Q_MSG_RPCSVC_INIT_FAILED,
                        "creation of rpcsvc failed");
                ret = -1;
                goto out;
        }

        ret = rpcsvc_create_listeners (priv->rpcsvc, this->options,
                                       this->name);
        if (ret < 1) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        Q_MSG_RPCSVC_LISTENER_CREATION_FAILED,
                        "creation of listener failed");
                ret = -1;
                goto out;
        }

        priv->quotad_aggregator = &quotad_aggregator_prog;
        quotad_aggregator_prog.options = this->options;

        ret = rpcsvc_program_register (priv->rpcsvc, &quotad_aggregator_prog);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        Q_MSG_RPCSVC_REGISTER_FAILED,
                        "registration of program (name:%s, prognum:%d, "
                        "progver:%d) failed", quotad_aggregator_prog.progname,
                        quotad_aggregator_prog.prognum,
                        quotad_aggregator_prog.progver);
                goto out;
        }

        ret = 0;
out:
        if (ret && priv->rpcsvc) {
                GF_FREE (priv->rpcsvc);
                priv->rpcsvc = NULL;
        }

        return ret;
}

rpcsvc_actor_t quotad_aggregator_actors[GF_AGGREGATOR_MAXVALUE] = {
        [GF_AGGREGATOR_NULL]     = {"NULL", GF_AGGREGATOR_NULL, NULL, NULL, 0,
                                    DRC_NA},
        [GF_AGGREGATOR_LOOKUP]   = {"LOOKUP", GF_AGGREGATOR_NULL,
                                    quotad_aggregator_lookup, NULL, 0, DRC_NA},
        [GF_AGGREGATOR_GETLIMIT] = {"GETLIMIT", GF_AGGREGATOR_GETLIMIT,
                                   quotad_aggregator_getlimit, NULL, 0, DRC_NA},
};


struct rpcsvc_program quotad_aggregator_prog = {
        .progname  = "GlusterFS 3.3",
        .prognum   = GLUSTER_AGGREGATOR_PROGRAM,
        .progver   = GLUSTER_AGGREGATOR_VERSION,
        .numactors = GF_AGGREGATOR_MAXVALUE,
        .actors    = quotad_aggregator_actors
};
