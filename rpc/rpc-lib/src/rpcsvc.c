/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "rpc-transport.h"
#include "dict.h"
#include "logging.h"
#include "byte-order.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "list.h"
#include "xdr-rpc.h"
#include "iobuf.h"
#include "globals.h"
#include "xdr-common.h"
#include "xdr-generic.h"
#include "rpc-common-xdr.h"
#include "syncop.h"
#include "rpc-drc.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <arpa/inet.h>
#include <rpc/xdr.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdio.h>

#include "xdr-rpcclnt.h"
#include "glusterfs-acl.h"

struct rpcsvc_program gluster_dump_prog;

#define rpcsvc_alloc_request(svc, request)                              \
        do {                                                            \
                request = (rpcsvc_request_t *) mem_get ((svc)->rxpool); \
                memset (request, 0, sizeof (rpcsvc_request_t));         \
        } while (0)

rpcsvc_listener_t *
rpcsvc_get_listener (rpcsvc_t *svc, uint16_t port, rpc_transport_t *trans);

int
rpcsvc_notify (rpc_transport_t *trans, void *mydata,
               rpc_transport_event_t event, void *data, ...);

rpcsvc_notify_wrapper_t *
rpcsvc_notify_wrapper_alloc (void)
{
        rpcsvc_notify_wrapper_t *wrapper = NULL;

        wrapper = GF_CALLOC (1, sizeof (*wrapper), gf_common_mt_rpcsvc_wrapper_t);
        if (!wrapper) {
                goto out;
        }

        INIT_LIST_HEAD (&wrapper->list);
out:
        return wrapper;
}


void
rpcsvc_listener_destroy (rpcsvc_listener_t *listener)
{
        rpcsvc_t *svc = NULL;

        if (!listener) {
                goto out;
        }

        svc = listener->svc;
        if (!svc) {
                goto listener_free;
        }

        pthread_mutex_lock (&svc->rpclock);
        {
                list_del_init (&listener->list);
        }
        pthread_mutex_unlock (&svc->rpclock);

listener_free:
        GF_FREE (listener);
out:
        return;
}

rpcsvc_vector_sizer
rpcsvc_get_program_vector_sizer (rpcsvc_t *svc, uint32_t prognum,
                                 uint32_t progver, uint32_t procnum)
{
        rpcsvc_program_t        *program = NULL;
        char                    found    = 0;

        if (!svc)
                return NULL;

        pthread_mutex_lock (&svc->rpclock);
        {
                list_for_each_entry (program, &svc->programs, program) {
                        if ((program->prognum == prognum)
                            && (program->progver == progver)) {
                                found = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&svc->rpclock);

        if (found)
                return program->actors[procnum].vector_sizer;
        else
                return NULL;
}

int
rpcsvc_request_outstanding (rpcsvc_t *svc, rpc_transport_t *trans, int delta)
{
        int ret = 0;
        int old_count = 0;
        int new_count = 0;
        int limit = 0;

        pthread_mutex_lock (&trans->lock);
        {
                limit = svc->outstanding_rpc_limit;
                if (!limit)
                        goto unlock;

                old_count = trans->outstanding_rpc_count;
                trans->outstanding_rpc_count += delta;
                new_count = trans->outstanding_rpc_count;

                if (old_count <= limit && new_count > limit)
                        ret = rpc_transport_throttle (trans, _gf_true);

                if (old_count > limit && new_count <= limit)
                        ret = rpc_transport_throttle (trans, _gf_false);
        }
unlock:
        pthread_mutex_unlock (&trans->lock);

        return ret;
}


/* This needs to change to returning errors, since
 * we need to return RPC specific error messages when some
 * of the pointers below are NULL.
 */
rpcsvc_actor_t *
rpcsvc_program_actor (rpcsvc_request_t *req)
{
        rpcsvc_program_t        *program = NULL;
        int                     err      = SYSTEM_ERR;
        rpcsvc_actor_t          *actor   = NULL;
        rpcsvc_t                *svc     = NULL;
        char                    found    = 0;

        if (!req)
                goto err;

        svc = req->svc;
        pthread_mutex_lock (&svc->rpclock);
        {
                list_for_each_entry (program, &svc->programs, program) {
                        if (program->prognum == req->prognum) {
                                err = PROG_MISMATCH;
                        }

                        if ((program->prognum == req->prognum)
                            && (program->progver == req->progver)) {
                                found = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&svc->rpclock);

        if (!found) {
                if (err != PROG_MISMATCH) {
                        /* log in DEBUG when nfs clients try to see if
                         * ACL requests are accepted by nfs server
                         */
                        gf_log (GF_RPCSVC, (req->prognum == ACL_PROGRAM) ?
                                GF_LOG_DEBUG : GF_LOG_WARNING,
                                "RPC program not available (req %u %u)",
                                req->prognum, req->progver);
                        err = PROG_UNAVAIL;
                        goto err;
                }

                gf_log (GF_RPCSVC, GF_LOG_WARNING,
                        "RPC program version not available (req %u %u)",
                        req->prognum, req->progver);
                goto err;
        }
        req->prog = program;
        if (!program->actors) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING,
                        "RPC Actor not found for program %s %d",
                        program->progname, program->prognum);
                err = SYSTEM_ERR;
                goto err;
        }

        if ((req->procnum < 0) || (req->procnum >= program->numactors)) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "RPC Program procedure not"
                        " available for procedure %d in %s", req->procnum,
                        program->progname);
                err = PROC_UNAVAIL;
                goto err;
        }

        actor = &program->actors[req->procnum];
        if (!actor->actor) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "RPC Program procedure not"
                        " available for procedure %d in %s", req->procnum,
                        program->progname);
                err = PROC_UNAVAIL;
                actor = NULL;
                goto err;
        }

        req->synctask = program->synctask;

        err = SUCCESS;
        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Actor found: %s - %s",
                program->progname, actor->procname);
err:
        if (req)
                req->rpc_err = err;

        return actor;
}


/* this procedure can only pass 4 arguments to registered notifyfn. To send more
 * arguments call wrapper->notify directly.
 */
static inline void
rpcsvc_program_notify (rpcsvc_listener_t *listener, rpcsvc_event_t event,
                       void *data)
{
        rpcsvc_notify_wrapper_t *wrapper = NULL;

        if (!listener) {
                goto out;
        }

        list_for_each_entry (wrapper, &listener->svc->notify, list) {
                if (wrapper->notify) {
                        wrapper->notify (listener->svc,
                                         wrapper->data,
                                         event, data);
                }
        }

out:
        return;
}


static inline int
rpcsvc_accept (rpcsvc_t *svc, rpc_transport_t *listen_trans,
               rpc_transport_t *new_trans)
{
        rpcsvc_listener_t *listener = NULL;
        int32_t            ret      = -1;

        listener = rpcsvc_get_listener (svc, -1, listen_trans);
        if (listener == NULL) {
                goto out;
        }

        rpcsvc_program_notify (listener, RPCSVC_EVENT_ACCEPT, new_trans);
        ret = 0;
out:
        return ret;
}


void
rpcsvc_request_destroy (rpcsvc_request_t *req)
{
        if (!req) {
                goto out;
        }

        if (req->iobref) {
                iobref_unref (req->iobref);
        }

        if (req->hdr_iobuf)
                iobuf_unref (req->hdr_iobuf);

        /* This marks the "end" of an RPC request. Reply is
           completely written to the socket and is on the way
           to the client. It is time to decrement the
           outstanding request counter by 1.
        */
        rpcsvc_request_outstanding (req->svc, req->trans, -1);

        rpc_transport_unref (req->trans);

	GF_FREE (req->auxgidlarge);

        mem_put (req);

out:
        return;
}


rpcsvc_request_t *
rpcsvc_request_init (rpcsvc_t *svc, rpc_transport_t *trans,
                     struct rpc_msg *callmsg,
                     struct iovec progmsg, rpc_transport_pollin_t *msg,
                     rpcsvc_request_t *req)
{
        int i = 0;

        if ((!trans) || (!callmsg)|| (!req) || (!msg))
                return NULL;

        /* We start a RPC request as always denied. */
        req->rpc_status = MSG_DENIED;
        req->xid = rpc_call_xid (callmsg);
        req->prognum = rpc_call_program (callmsg);
        req->progver = rpc_call_progver (callmsg);
        req->procnum = rpc_call_progproc (callmsg);
        req->trans = rpc_transport_ref (trans);
        req->count = msg->count;
        req->msg[0] = progmsg;
        req->iobref = iobref_ref (msg->iobref);
        if (msg->vectored) {
                /* msg->vector[2] is defined in structure. prevent a
                   out of bound access */
                for (i = 1; i < min (msg->count, 2); i++) {
                        req->msg[i] = msg->vector[i];
                }
        }

        req->svc = svc;
        req->trans_private = msg->private;

        INIT_LIST_HEAD (&req->txlist);
        req->payloadsize = 0;

        /* By this time, the data bytes for the auth scheme would have already
         * been copied into the required sections of the req structure,
         * we just need to fill in the meta-data about it now.
         */
        rpcsvc_auth_request_init (req, callmsg);
        return req;
}


rpcsvc_request_t *
rpcsvc_request_create (rpcsvc_t *svc, rpc_transport_t *trans,
                       rpc_transport_pollin_t *msg)
{
        char                    *msgbuf = NULL;
        struct rpc_msg          rpcmsg;
        struct iovec            progmsg;        /* RPC Program payload */
        rpcsvc_request_t        *req    = NULL;
        size_t                  msglen  = 0;
        int                     ret     = -1;

        if (!svc || !trans)
                return NULL;

        /* We need to allocate the request before actually calling
         * rpcsvc_request_init on the request so that we, can fill the auth
         * data directly into the request structure from the message iobuf.
         * This avoids a need to keep a temp buffer into which the auth data
         * would've been copied otherwise.
         */
        rpcsvc_alloc_request (svc, req);
        if (!req) {
                goto err;
        }

        /* We just received a new request from the wire. Account for
           it in the outsanding request counter to make sure we don't
           ingest too many concurrent requests from the same client.
        */
        ret = rpcsvc_request_outstanding (svc, trans, +1);

        msgbuf = msg->vector[0].iov_base;
        msglen = msg->vector[0].iov_len;

        ret = xdr_to_rpc_call (msgbuf, msglen, &rpcmsg, &progmsg,
                               req->cred.authdata,req->verf.authdata);

        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "RPC call decoding failed");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                req->trans = rpc_transport_ref (trans);
                req->svc = svc;
                goto err;
        }

        ret = -1;
        rpcsvc_request_init (svc, trans, &rpcmsg, progmsg, msg, req);

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "received rpc-message (XID: 0x%lx, "
                "Ver: %ld, Program: %ld, ProgVers: %ld, Proc: %ld) from"
                " rpc-transport (%s)", rpc_call_xid (&rpcmsg),
                rpc_call_rpcvers (&rpcmsg), rpc_call_program (&rpcmsg),
                rpc_call_progver (&rpcmsg), rpc_call_progproc (&rpcmsg),
                trans->name);

        if (rpc_call_rpcvers (&rpcmsg) != 2) {
                /* LOG- TODO: print rpc version, also print the peerinfo
                   from transport */
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "RPC version not supported "
                        "(XID: 0x%lx, Ver: %ld, Prog: %ld, ProgVers: %ld, "
                        "Proc: %ld) from trans (%s)", rpc_call_xid (&rpcmsg),
                        rpc_call_rpcvers (&rpcmsg), rpc_call_program (&rpcmsg),
                        rpc_call_progver (&rpcmsg), rpc_call_progproc (&rpcmsg),
                        trans->name);
                rpcsvc_request_seterr (req, RPC_MISMATCH);
                goto err;
        }

        ret = rpcsvc_authenticate (req);
        if (ret == RPCSVC_AUTH_REJECT) {
                /* No need to set auth_err, that is the responsibility of
                 * the authentication handler since only that know what exact
                 * error happened.
                 */
                rpcsvc_request_seterr (req, AUTH_ERROR);
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "auth failed on request. "
                        "(XID: 0x%lx, Ver: %ld, Prog: %ld, ProgVers: %ld, "
                        "Proc: %ld) from trans (%s)", rpc_call_xid (&rpcmsg),
                        rpc_call_rpcvers (&rpcmsg), rpc_call_program (&rpcmsg),
                        rpc_call_progver (&rpcmsg), rpc_call_progproc (&rpcmsg),
                        trans->name);
                ret = -1;
                goto err;
        }


        /* If the error is not RPC_MISMATCH, we consider the call as accepted
         * since we are not handling authentication failures for now.
         */
        req->rpc_status = MSG_ACCEPTED;
        req->reply = NULL;
        ret = 0;
err:
        if (ret == -1) {
                ret = rpcsvc_error_reply (req);
                if (ret)
                        gf_log ("rpcsvc", GF_LOG_WARNING,
                                "failed to queue error reply");
                req = NULL;
        }

        return req;
}


int
rpcsvc_check_and_reply_error (int ret, call_frame_t *frame, void *opaque)
{
        rpcsvc_request_t  *req = NULL;

        req = opaque;

        if (ret)
                gf_log ("rpcsvc", GF_LOG_ERROR,
                        "rpc actor failed to complete successfully");

        if (ret == RPCSVC_ACTOR_ERROR) {
                ret = rpcsvc_error_reply (req);
                if (ret)
                        gf_log ("rpcsvc", GF_LOG_WARNING,
                                "failed to queue error reply");
        }

        return 0;
}

int
rpcsvc_handle_rpc_call (rpcsvc_t *svc, rpc_transport_t *trans,
                        rpc_transport_pollin_t *msg)
{
        rpcsvc_actor_t         *actor          = NULL;
        rpcsvc_actor            actor_fn       = NULL;
        rpcsvc_request_t       *req            = NULL;
        int                     ret            = -1;
        uint16_t                port           = 0;
        gf_boolean_t            is_unix        = _gf_false;
        gf_boolean_t            unprivileged   = _gf_false;
        drc_cached_op_t        *reply          = NULL;
        rpcsvc_drc_globals_t   *drc            = NULL;

        if (!trans || !svc)
                return -1;

        switch (trans->peerinfo.sockaddr.ss_family) {
        case AF_INET:
                port = ((struct sockaddr_in *)&trans->peerinfo.sockaddr)->sin_port;
                break;

        case AF_INET6:
                port = ((struct sockaddr_in6 *)&trans->peerinfo.sockaddr)->sin6_port;
                break;
        case AF_UNIX:
                is_unix = _gf_true;
                break;
        default:
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "invalid address family (%d)",
                        trans->peerinfo.sockaddr.ss_family);
                return -1;
        }



        if (is_unix == _gf_false) {
                port = ntohs (port);

                gf_log ("rpcsvc", GF_LOG_TRACE, "Client port: %d", (int)port);

                if (port > 1024)
                        unprivileged = _gf_true;
        }

        req = rpcsvc_request_create (svc, trans, msg);
        if (!req)
                goto out;

        if (!rpcsvc_request_accepted (req))
                goto err_reply;

        actor = rpcsvc_program_actor (req);
        if (!actor)
                goto err_reply;

        if (0 == svc->allow_insecure && unprivileged && !actor->unprivileged) {
                        /* Non-privileged user, fail request */
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Request received from non-"
                                "privileged port. Failing request");
                        rpcsvc_request_destroy (req);
                        return -1;
        }

        /* DRC */
        if (rpcsvc_need_drc (req)) {
                drc = req->svc->drc;

                LOCK (&drc->lock);
                reply = rpcsvc_drc_lookup (req);

                /* retransmission of completed request, send cached reply */
                if (reply && reply->state == DRC_OP_CACHED) {
                        gf_log (GF_RPCSVC, GF_LOG_INFO, "duplicate request:"
                                " XID: 0x%x", req->xid);
                        ret = rpcsvc_send_cached_reply (req, reply);
                        drc->cache_hits++;
                        UNLOCK (&drc->lock);
                        goto out;

                } /* retransmitted request, original op in transit, drop it */
                else if (reply && reply->state == DRC_OP_IN_TRANSIT) {
                        gf_log (GF_RPCSVC, GF_LOG_INFO, "op in transit,"
                                " discarding. XID: 0x%x", req->xid);
                        ret = 0;
                        drc->intransit_hits++;
                        rpcsvc_request_destroy (req);
                        UNLOCK (&drc->lock);
                        goto out;

                } /* fresh request, cache it as in-transit and proceed */
                else {
                        ret = rpcsvc_cache_request (req);
                }
                UNLOCK (&drc->lock);
        }

        if (req->rpc_err == SUCCESS) {
                /* Before going to xlator code, set the THIS properly */
                THIS = svc->mydata;

                actor_fn = actor->actor;

                if (!actor_fn) {
                        rpcsvc_request_seterr (req, PROC_UNAVAIL);
                        /* LOG TODO: print more info about procnum,
                           prognum etc, also print transport info */
                        gf_log (GF_RPCSVC, GF_LOG_ERROR,
                                "No vectored handler present");
                        ret = RPCSVC_ACTOR_ERROR;
                        goto err_reply;
                }

                if (req->synctask) {
                        if (msg->hdr_iobuf)
                                req->hdr_iobuf = iobuf_ref (msg->hdr_iobuf);

                        ret = synctask_new (THIS->ctx->env,
                                            (synctask_fn_t) actor_fn,
                                            rpcsvc_check_and_reply_error, NULL,
                                            req);
                } else {
                        ret = actor_fn (req);
                }
        }

err_reply:

        ret = rpcsvc_check_and_reply_error (ret, NULL, req);
        /* No need to propagate error beyond this function since the reply
         * has now been queued. */
        ret = 0;

out:
        return ret;
}


int
rpcsvc_handle_disconnect (rpcsvc_t *svc, rpc_transport_t *trans)
{
        rpcsvc_event_t           event;
        rpcsvc_notify_wrapper_t *wrappers = NULL, *wrapper;
        int32_t                  ret      = -1, i = 0, wrapper_count = 0;
        rpcsvc_listener_t       *listener = NULL;

        event = (trans->listener == NULL) ? RPCSVC_EVENT_LISTENER_DEAD
                : RPCSVC_EVENT_DISCONNECT;

        pthread_mutex_lock (&svc->rpclock);
        {
                if (!svc->notify_count)
                        goto unlock;

                wrappers = GF_CALLOC (svc->notify_count, sizeof (*wrapper),
                                      gf_common_mt_rpcsvc_wrapper_t);
                if (!wrappers) {
                        goto unlock;
                }

                list_for_each_entry (wrapper, &svc->notify, list) {
                        if (wrapper->notify) {
                                wrappers[i++] = *wrapper;
                        }
                }

                wrapper_count = i;
        }
unlock:
        pthread_mutex_unlock (&svc->rpclock);

        if (wrappers) {
                for (i = 0; i < wrapper_count; i++) {
                        wrappers[i].notify (svc, wrappers[i].data,
                                            event, trans);
                }

                GF_FREE (wrappers);
        }

        if (event == RPCSVC_EVENT_LISTENER_DEAD) {
                listener = rpcsvc_get_listener (svc, -1, trans->listener);
                rpcsvc_listener_destroy (listener);
        }

        return ret;
}


int
rpcsvc_notify (rpc_transport_t *trans, void *mydata,
               rpc_transport_event_t event, void *data, ...)
{
        int                     ret       = -1;
        rpc_transport_pollin_t *msg       = NULL;
        rpc_transport_t        *new_trans = NULL;
        rpcsvc_t               *svc       = NULL;
        rpcsvc_listener_t      *listener  = NULL;

        svc = mydata;
        if (svc == NULL) {
                goto out;
        }

        switch (event) {
        case RPC_TRANSPORT_ACCEPT:
                new_trans = data;
                ret = rpcsvc_accept (svc, trans, new_trans);
                break;

        case RPC_TRANSPORT_DISCONNECT:
                ret = rpcsvc_handle_disconnect (svc, trans);
                break;

        case RPC_TRANSPORT_MSG_RECEIVED:
                msg = data;
                ret = rpcsvc_handle_rpc_call (svc, trans, msg);
                break;

        case RPC_TRANSPORT_MSG_SENT:
                ret = 0;
                break;

        case RPC_TRANSPORT_CONNECT:
                /* do nothing, no need for rpcsvc to handle this, client should
                 * handle this event
                 */
                /* print info about transport too : LOG TODO */
                gf_log ("rpcsvc", GF_LOG_CRITICAL,
                        "got CONNECT event, which should have not come");
                ret = 0;
                break;

        case RPC_TRANSPORT_CLEANUP:
                listener = rpcsvc_get_listener (svc, -1, trans->listener);
                if (listener == NULL) {
                        goto out;
                }

                rpcsvc_program_notify (listener, RPCSVC_EVENT_TRANSPORT_DESTROY,
                                       trans);
                ret = 0;
                break;

        case RPC_TRANSPORT_MAP_XID_REQUEST:
                /* FIXME: think about this later */
                gf_log ("rpcsvc", GF_LOG_CRITICAL,
                        "got MAP_XID event, which should have not come");
                ret = 0;
                break;
        }

out:
        return ret;
}


/* Given the RPC reply structure and the payload handed by the RPC program,
 * encode the RPC record header into the buffer pointed by recordstart.
 */
struct iovec
rpcsvc_record_build_header (char *recordstart, size_t rlen,
                            struct rpc_msg reply, size_t payload)
{
        struct iovec    replyhdr;
        struct iovec    txrecord = {0, 0};
        size_t          fraglen = 0;
        int             ret = -1;

        /* After leaving aside the 4 bytes for the fragment header, lets
         * encode the RPC reply structure into the buffer given to us.
         */
        ret = rpc_reply_to_xdr (&reply, recordstart, rlen, &replyhdr);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "Failed to create RPC reply");
                goto err;
        }

        fraglen = payload + replyhdr.iov_len;
        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Reply fraglen %zu, payload: %zu, "
                "rpc hdr: %zu", fraglen, payload, replyhdr.iov_len);

        txrecord.iov_base = recordstart;

        /* Remember, this is only the vec for the RPC header and does not
         * include the payload above. We needed the payload only to calculate
         * the size of the full fragment. This size is sent in the fragment
         * header.
         */
        txrecord.iov_len = replyhdr.iov_len;
err:
        return txrecord;
}

static inline int
rpcsvc_get_callid (rpcsvc_t *rpc)
{
        return GF_UNIVERSAL_ANSWER;
}

int
rpcsvc_fill_callback (int prognum, int progver, int procnum, int payload,
                      uint64_t xid, struct rpc_msg *request)
{
        int   ret          = -1;

        if (!request) {
                goto out;
        }

        memset (request, 0, sizeof (*request));

        request->rm_xid = xid;
        request->rm_direction = CALL;

        request->rm_call.cb_rpcvers = 2;
        request->rm_call.cb_prog = prognum;
        request->rm_call.cb_vers = progver;
        request->rm_call.cb_proc = procnum;

        request->rm_call.cb_cred.oa_flavor = AUTH_NONE;
        request->rm_call.cb_cred.oa_base   = NULL;
        request->rm_call.cb_cred.oa_length = 0;

        request->rm_call.cb_verf.oa_flavor = AUTH_NONE;
        request->rm_call.cb_verf.oa_base = NULL;
        request->rm_call.cb_verf.oa_length = 0;

        ret = 0;
out:
        return ret;
}


struct iovec
rpcsvc_callback_build_header (char *recordstart, size_t rlen,
                             struct rpc_msg *request, size_t payload)
{
        struct iovec    requesthdr = {0, };
        struct iovec    txrecord   = {0, 0};
        int             ret        = -1;
        size_t          fraglen    = 0;

        ret = rpc_request_to_xdr (request, recordstart, rlen, &requesthdr);
        if (ret == -1) {
                gf_log ("rpcsvc", GF_LOG_WARNING,
                        "Failed to create RPC request");
                goto out;
        }

        fraglen = payload + requesthdr.iov_len;
        gf_log ("rpcsvc", GF_LOG_TRACE, "Request fraglen %zu, payload: %zu, "
                "rpc hdr: %zu", fraglen, payload, requesthdr.iov_len);

        txrecord.iov_base = recordstart;

        /* Remember, this is only the vec for the RPC header and does not
         * include the payload above. We needed the payload only to calculate
         * the size of the full fragment. This size is sent in the fragment
         * header.
         */
        txrecord.iov_len = requesthdr.iov_len;

out:
        return txrecord;
}

struct iobuf *
rpcsvc_callback_build_record (rpcsvc_t *rpc, int prognum, int progver,
                              int procnum, size_t payload, uint64_t xid,
                              struct iovec *recbuf)
{
        struct rpc_msg           request     = {0, };
        struct iobuf            *request_iob = NULL;
        char                    *record      = NULL;
        struct iovec             recordhdr   = {0, };
        size_t                   pagesize    = 0;
        size_t                   xdr_size    = 0;
        int                      ret         = -1;

        if ((!rpc) || (!recbuf)) {
                goto out;
        }

        /* Fill the rpc structure and XDR it into the buffer got above. */
        ret = rpcsvc_fill_callback (prognum, progver, procnum, payload, xid,
                                    &request);
        if (ret == -1) {
                gf_log ("rpcsvc", GF_LOG_WARNING, "cannot build a rpc-request "
                        "xid (%"PRIu64")", xid);
                goto out;
        }

        /* First, try to get a pointer into the buffer which the RPC
         * layer can use.
         */
        xdr_size = xdr_sizeof ((xdrproc_t)xdr_callmsg, &request);

        request_iob = iobuf_get2 (rpc->ctx->iobuf_pool, (xdr_size + payload));
        if (!request_iob) {
                goto out;
        }

        pagesize = iobuf_pagesize (request_iob);

        record = iobuf_ptr (request_iob);  /* Now we have it. */

        recordhdr = rpcsvc_callback_build_header (record, pagesize, &request,
                                                  payload);

        if (!recordhdr.iov_base) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "Failed to build record "
                        " header");
                iobuf_unref (request_iob);
                request_iob = NULL;
                recbuf->iov_base = NULL;
                goto out;
        }

        recbuf->iov_base = recordhdr.iov_base;
        recbuf->iov_len = recordhdr.iov_len;

out:
        return request_iob;
}

int
rpcsvc_callback_submit (rpcsvc_t *rpc, rpc_transport_t *trans,
                        rpcsvc_cbk_program_t *prog, int procnum,
                        struct iovec *proghdr, int proghdrcount)
{
        struct iobuf          *request_iob = NULL;
        struct iovec           rpchdr      = {0,};
        rpc_transport_req_t    req;
        int                    ret         = -1;
        int                    proglen     = 0;
        uint64_t               callid      = 0;

        if (!rpc) {
                goto out;
        }

        memset (&req, 0, sizeof (req));

        callid = rpcsvc_get_callid (rpc);

        if (proghdr) {
                proglen += iov_length (proghdr, proghdrcount);
        }

        request_iob = rpcsvc_callback_build_record (rpc, prog->prognum,
                                                    prog->progver, procnum,
                                                    proglen, callid,
                                                    &rpchdr);
        if (!request_iob) {
                gf_log ("rpcsvc", GF_LOG_WARNING,
                        "cannot build rpc-record");
                goto out;
        }

        req.msg.rpchdr = &rpchdr;
        req.msg.rpchdrcount = 1;
        req.msg.proghdr = proghdr;
        req.msg.proghdrcount = proghdrcount;

        ret = rpc_transport_submit_request (trans, &req);
        if (ret == -1) {
                gf_log ("rpcsvc", GF_LOG_WARNING,
                        "transmission of rpc-request failed");
                goto out;
        }

        ret = 0;

out:
        iobuf_unref (request_iob);

        return ret;
}

int
rpcsvc_transport_submit (rpc_transport_t *trans, struct iovec *rpchdr,
                         int rpchdrcount, struct iovec *proghdr,
                         int proghdrcount, struct iovec *progpayload,
                         int progpayloadcount, struct iobref *iobref,
                         void *priv)
{
        int                   ret   = -1;
        rpc_transport_reply_t reply = {{0, }};

        if ((!trans) || (!rpchdr) || (!rpchdr->iov_base)) {
                goto out;
        }

        reply.msg.rpchdr = rpchdr;
        reply.msg.rpchdrcount = rpchdrcount;
        reply.msg.proghdr = proghdr;
        reply.msg.proghdrcount = proghdrcount;
        reply.msg.progpayload = progpayload;
        reply.msg.progpayloadcount = progpayloadcount;
        reply.msg.iobref = iobref;
        reply.private = priv;

        ret = rpc_transport_submit_reply (trans, &reply);

out:
        return ret;
}


int
rpcsvc_fill_reply (rpcsvc_request_t *req, struct rpc_msg *reply)
{
        int                      ret  = -1;
        rpcsvc_program_t        *prog = NULL;
        if ((!req) || (!reply))
                goto out;

        ret = 0;
        rpc_fill_empty_reply (reply, req->xid);
        if (req->rpc_status == MSG_DENIED) {
                rpc_fill_denied_reply (reply, req->rpc_err, req->auth_err);
                goto out;
        }

        prog = rpcsvc_request_program (req);

        if (req->rpc_status == MSG_ACCEPTED)
                rpc_fill_accepted_reply (reply, req->rpc_err,
                                         (prog) ? prog->proglowvers : 0,
                                         (prog) ? prog->proghighvers: 0,
                                         req->verf.flavour, req->verf.datalen,
                                         req->verf.authdata);
        else
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Invalid rpc_status value");

out:
        return ret;
}


/* Given a request and the reply payload, build a reply and encodes the reply
 * into a record header. This record header is encoded into the vector pointed
 * to be recbuf.
 * msgvec is the buffer that points to the payload of the RPC program.
 * This buffer can be NULL, if an RPC error reply is being constructed.
 * The only reason it is needed here is that in case the buffer is provided,
 * we should account for the length of that buffer in the RPC fragment header.
 */
struct iobuf *
rpcsvc_record_build_record (rpcsvc_request_t *req, size_t payload,
                            size_t hdrlen, struct iovec *recbuf)
{
        struct rpc_msg          reply;
        struct iobuf            *replyiob = NULL;
        char                    *record = NULL;
        struct iovec            recordhdr = {0, };
        size_t                  pagesize = 0;
        size_t                  xdr_size = 0;
        rpcsvc_t                *svc = NULL;
        int                     ret = -1;

        if ((!req) || (!req->trans) || (!req->svc) || (!recbuf))
                return NULL;

        svc = req->svc;

        /* Fill the rpc structure and XDR it into the buffer got above. */
        ret = rpcsvc_fill_reply (req, &reply);
        if (ret)
                goto err_exit;

        xdr_size = xdr_sizeof ((xdrproc_t)xdr_replymsg, &reply);

        /* Payload would include 'readv' size etc too, where as
           that comes as another payload iobuf */
        replyiob = iobuf_get2 (svc->ctx->iobuf_pool, (xdr_size + hdrlen));
        if (!replyiob) {
                goto err_exit;
        }

        pagesize = iobuf_pagesize (replyiob);

        record = iobuf_ptr (replyiob);  /* Now we have it. */

        recordhdr = rpcsvc_record_build_header (record, pagesize, reply,
                                                payload);
        if (!recordhdr.iov_base) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to build record "
                        " header");
                iobuf_unref (replyiob);
                replyiob = NULL;
                recbuf->iov_base = NULL;
                goto err_exit;
        }

        recbuf->iov_base = recordhdr.iov_base;
        recbuf->iov_len = recordhdr.iov_len;
err_exit:
        return replyiob;
}


/*
 * The function to submit a program message to the RPC service.
 * This message is added to the transmission queue of the
 * conn.
 *
 * Program callers are not expected to use the msgvec->iov_base
 * address for anything else.
 * Nor are they expected to free it once this function returns.
 * Once the transmission of the buffer is completed by the RPC service,
 * the memory area as referenced through @msg will be unrefed.
 * If a higher layer does not want anything to do with this iobuf
 * after this function returns, it should call unref on it. For keeping
 * it around till the transmission is actually complete, rpcsvc also refs it.
 *  *
 * If this function returns an error by returning -1, the
 * higher layer programs should assume that a disconnection happened
 * and should know that the conn memory area as well as the req structure
 * has been freed internally.
 *
 * For now, this function assumes that a submit is always called
 * to send a new record. Later, if there is a situation where different
 * buffers for the same record come from different sources, then we'll
 * need to change this code to account for multiple submit calls adding
 * the buffers into a single record.
 */

int
rpcsvc_submit_generic (rpcsvc_request_t *req, struct iovec *proghdr,
                       int hdrcount, struct iovec *payload, int payloadcount,
                       struct iobref *iobref)
{
        int                     ret        = -1, i = 0;
        struct iobuf           *replyiob   = NULL;
        struct iovec            recordhdr  = {0, };
        rpc_transport_t        *trans      = NULL;
        size_t                  msglen     = 0;
        size_t                  hdrlen     = 0;
        char                    new_iobref = 0;
        rpcsvc_drc_globals_t   *drc        = NULL;

        if ((!req) || (!req->trans))
                return -1;

        trans = req->trans;

        for (i = 0; i < hdrcount; i++) {
                msglen += proghdr[i].iov_len;
        }

        for (i = 0; i < payloadcount; i++) {
                msglen += payload[i].iov_len;
        }

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Tx message: %zu", msglen);

        /* Build the buffer containing the encoded RPC reply. */
        replyiob = rpcsvc_record_build_record (req, msglen, hdrlen, &recordhdr);
        if (!replyiob) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR,"Reply record creation failed");
                goto disconnect_exit;
        }

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto disconnect_exit;
                }

                new_iobref = 1;
        }

        iobref_add (iobref, replyiob);

        /* cache the request in the duplicate request cache for appropriate ops */
        if ((req->reply) && (rpcsvc_need_drc (req))) {
                drc = req->svc->drc;

                LOCK (&drc->lock);
                ret = rpcsvc_cache_reply (req, iobref, &recordhdr, 1,
                                          proghdr, hdrcount,
                                          payload, payloadcount);
                UNLOCK (&drc->lock);
        }

        ret = rpcsvc_transport_submit (trans, &recordhdr, 1, proghdr, hdrcount,
                                       payload, payloadcount, iobref,
                                       req->trans_private);

        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "failed to submit message "
                        "(XID: 0x%x, Program: %s, ProgVers: %d, Proc: %d) to "
                        "rpc-transport (%s)", req->xid,
                        req->prog ? req->prog->progname : "(not matched)",
                        req->prog ? req->prog->progver : 0,
                        req->procnum, trans->name);
        } else {
                gf_log (GF_RPCSVC, GF_LOG_TRACE,
                        "submitted reply for rpc-message (XID: 0x%x, "
                        "Program: %s, ProgVers: %d, Proc: %d) to rpc-transport "
                        "(%s)", req->xid, req->prog ? req->prog->progname: "-",
                        req->prog ? req->prog->progver : 0,
                        req->procnum, trans->name);
        }

disconnect_exit:
        if (replyiob) {
                iobuf_unref (replyiob);
        }

        if (new_iobref) {
                iobref_unref (iobref);
        }

        rpcsvc_request_destroy (req);

        return ret;
}


int
rpcsvc_error_reply (rpcsvc_request_t *req)
{
        struct iovec    dummyvec = {0, };

        if (!req)
                return -1;

        gf_log_callingfn ("", GF_LOG_DEBUG, "sending a RPC error reply");

        /* At this point the req should already have been filled with the
         * appropriate RPC error numbers.
         */
        return rpcsvc_submit_generic (req, &dummyvec, 0, NULL, 0, NULL);
}


/* Register the program with the local portmapper service. */
inline int
rpcsvc_program_register_portmap (rpcsvc_program_t *newprog, uint32_t port)
{
        int                ret   = -1; /* FAIL */

        if (!newprog) {
                goto out;
        }

        /* pmap_set() returns 0 for FAIL and 1 for SUCCESS */
        if (!(pmap_set (newprog->prognum, newprog->progver, IPPROTO_TCP,
                        port))) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Could not register with"
                        " portmap");
                goto out;
        }

        ret = 0; /* SUCCESS */
out:
        return ret;
}


inline int
rpcsvc_program_unregister_portmap (rpcsvc_program_t *prog)
{
        int ret = -1;

        if (!prog)
                goto out;

        if (!(pmap_unset(prog->prognum, prog->progver))) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Could not unregister with"
                        " portmap");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
rpcsvc_register_portmap_enabled (rpcsvc_t *svc)
{
        return svc->register_portmap;
}

int32_t
rpcsvc_get_listener_port (rpcsvc_listener_t *listener)
{
        int32_t listener_port = -1;

        if ((listener == NULL) || (listener->trans == NULL)) {
                goto out;
        }

        switch (listener->trans->myinfo.sockaddr.ss_family) {
        case AF_INET:
                listener_port = ((struct sockaddr_in *)&listener->trans->myinfo.sockaddr)->sin_port;
                break;

        case AF_INET6:
                listener_port = ((struct sockaddr_in6 *)&listener->trans->myinfo.sockaddr)->sin6_port;
                break;

        default:
                gf_log (GF_RPCSVC, GF_LOG_DEBUG,
                        "invalid address family (%d)",
                        listener->trans->myinfo.sockaddr.ss_family);
                goto out;
        }

        listener_port = ntohs (listener_port);

out:
        return listener_port;
}


rpcsvc_listener_t *
rpcsvc_get_listener (rpcsvc_t *svc, uint16_t port, rpc_transport_t *trans)
{
        rpcsvc_listener_t  *listener      = NULL;
        char                found         = 0;
        uint32_t            listener_port = 0;

        if (!svc) {
                goto out;
        }

        pthread_mutex_lock (&svc->rpclock);
        {
                list_for_each_entry (listener, &svc->listeners, list) {
                        if (trans != NULL) {
                                if (listener->trans == trans) {
                                        found = 1;
                                        break;
                                }

                                continue;
                        }

                        listener_port = rpcsvc_get_listener_port (listener);
                        if (listener_port == -1) {
                                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                                        "invalid port for listener %s",
                                        listener->trans->name);
                                continue;
                        }

                        if (listener_port == port) {
                                found = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&svc->rpclock);

        if (!found) {
                listener = NULL;
        }

out:
        return listener;
}


/* The only difference between the generic submit and this one is that the
 * generic submit is also used for submitting RPC error replies in where there
 * are no payloads so the msgvec and msgbuf can be NULL.
 * Since RPC programs should be using this function along with their payloads
 * we must perform NULL checks before calling the generic submit.
 */
int
rpcsvc_submit_message (rpcsvc_request_t *req, struct iovec *proghdr,
                       int hdrcount, struct iovec *payload, int payloadcount,
                       struct iobref *iobref)
{
        if ((!req) || (!req->trans) || (!proghdr) || (!proghdr->iov_base))
                return -1;

        return rpcsvc_submit_generic (req, proghdr, hdrcount, payload,
                                      payloadcount, iobref);
}


int
rpcsvc_program_unregister (rpcsvc_t *svc, rpcsvc_program_t *program)
{
        int                     ret = -1;
        rpcsvc_program_t        *prog = NULL;
        if (!svc || !program) {
                goto out;
        }

        ret = rpcsvc_program_unregister_portmap (program);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "portmap unregistration of"
                        " program failed");
                goto out;
        }

        pthread_mutex_lock (&svc->rpclock);
        {
                list_for_each_entry (prog, &svc->programs, program) {
                        if ((prog->prognum == program->prognum)
                            && (prog->progver == program->progver)) {
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&svc->rpclock);

        if (prog == NULL) {
                ret = -1;
                goto out;
        }

        gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Program unregistered: %s, Num: %d,"
                " Ver: %d, Port: %d", prog->progname, prog->prognum,
                prog->progver, prog->progport);

        pthread_mutex_lock (&svc->rpclock);
        {
                list_del_init (&prog->program);
        }
        pthread_mutex_unlock (&svc->rpclock);

        ret = 0;
out:
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Program unregistration failed"
                        ": %s, Num: %d, Ver: %d, Port: %d", program->progname,
                        program->prognum, program->progver, program->progport);
        }

        return ret;
}


inline int
rpcsvc_transport_peername (rpc_transport_t *trans, char *hostname, int hostlen)
{
        if (!trans) {
                return -1;
        }

        return rpc_transport_get_peername (trans, hostname, hostlen);
}


inline int
rpcsvc_transport_peeraddr (rpc_transport_t *trans, char *addrstr, int addrlen,
                           struct sockaddr_storage *sa, socklen_t sasize)
{
        if (!trans) {
                return -1;
        }

        return rpc_transport_get_peeraddr(trans, addrstr, addrlen, sa,
                                          sasize);
}


rpc_transport_t *
rpcsvc_transport_create (rpcsvc_t *svc, dict_t *options, char *name)
{
        int                ret   = -1;
        rpc_transport_t   *trans = NULL;

        trans = rpc_transport_load (svc->ctx, options, name);
        if (!trans) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "cannot create listener, "
                        "initing the transport failed");
                goto out;
        }

        ret = rpc_transport_listen (trans);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING,
                        "listening on transport failed");
                goto out;
        }

        ret = rpc_transport_register_notify (trans, rpcsvc_notify, svc);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "registering notify failed");
                goto out;
        }

        ret = 0;
out:
        if ((ret == -1) && (trans)) {
                rpc_transport_disconnect (trans);
                trans = NULL;
        }

        return trans;
}

rpcsvc_listener_t *
rpcsvc_listener_alloc (rpcsvc_t *svc, rpc_transport_t *trans)
{
        rpcsvc_listener_t *listener = NULL;

        listener = GF_CALLOC (1, sizeof (*listener),
                              gf_common_mt_rpcsvc_listener_t);
        if (!listener) {
                goto out;
        }

        listener->trans = trans;
        listener->svc = svc;

        INIT_LIST_HEAD (&listener->list);

        pthread_mutex_lock (&svc->rpclock);
        {
                list_add_tail (&listener->list, &svc->listeners);
        }
        pthread_mutex_unlock (&svc->rpclock);
out:
        return listener;
}


int32_t
rpcsvc_create_listener (rpcsvc_t *svc, dict_t *options, char *name)
{
        rpc_transport_t   *trans    = NULL;
        rpcsvc_listener_t *listener = NULL;
        int32_t            ret      = -1;

        if (!svc || !options) {
                goto out;
        }

        trans = rpcsvc_transport_create (svc, options, name);
        if (!trans) {
                /* LOG TODO */
                goto out;
        }

        listener = rpcsvc_listener_alloc (svc, trans);
        if (listener == NULL) {
                goto out;
        }

        ret = 0;
out:
        if (!listener && trans) {
                rpc_transport_disconnect (trans);
        }

        return ret;
}


int32_t
rpcsvc_create_listeners (rpcsvc_t *svc, dict_t *options, char *name)
{
        int32_t  ret            = -1, count = 0;
        data_t  *data           = NULL;
        char    *str            = NULL, *ptr = NULL, *transport_name = NULL;
        char    *transport_type = NULL, *saveptr = NULL, *tmp = NULL;

        if ((svc == NULL) || (options == NULL) || (name == NULL)) {
                goto out;
        }

        data = dict_get (options, "transport-type");
        if (data == NULL) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "option transport-type not set");
                goto out;
        }

        transport_type = data_to_str (data);
        if (transport_type == NULL) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "option transport-type not set");
                goto out;
        }

        /* duplicate transport_type, since following dict_set will free it */
        transport_type = gf_strdup (transport_type);
        if (transport_type == NULL) {
                goto out;
        }

        str = gf_strdup (transport_type);
        if (str == NULL) {
                goto out;
        }

        ptr = strtok_r (str, ",", &saveptr);

        while (ptr != NULL) {
                tmp = gf_strdup (ptr);
                if (tmp == NULL) {
                        goto out;
                }

                ret = gf_asprintf (&transport_name, "%s.%s", tmp, name);
                if (ret == -1) {
                        goto out;
                }

                ret = dict_set_dynstr (options, "transport-type", tmp);
                if (ret == -1) {
                        goto out;
                }

                tmp = NULL;
                ptr = strtok_r (NULL, ",", &saveptr);

                ret = rpcsvc_create_listener (svc, options, transport_name);
                if (ret != 0) {
                        goto out;
                }

                GF_FREE (transport_name);
                transport_name = NULL;
                count++;
        }

        ret = dict_set_dynstr (options, "transport-type", transport_type);
        if (ret == -1) {
                goto out;
        }

        transport_type = NULL;

out:
        GF_FREE (str);

        GF_FREE (transport_type);

        GF_FREE (tmp);

        GF_FREE (transport_name);

        return count;
}


int
rpcsvc_unregister_notify (rpcsvc_t *svc, rpcsvc_notify_t notify, void *mydata)
{
        rpcsvc_notify_wrapper_t *wrapper = NULL, *tmp = NULL;
        int                      ret     = 0;

        if (!svc || !notify) {
                goto out;
        }

        pthread_mutex_lock (&svc->rpclock);
        {
                list_for_each_entry_safe (wrapper, tmp, &svc->notify, list) {
                        if ((wrapper->notify == notify)
                            && (mydata == wrapper->data)) {
                                list_del_init (&wrapper->list);
                                GF_FREE (wrapper);
                                ret++;
                        }
                }
        }
        pthread_mutex_unlock (&svc->rpclock);

out:
        return ret;
}

int
rpcsvc_register_notify (rpcsvc_t *svc, rpcsvc_notify_t notify, void *mydata)
{
        rpcsvc_notify_wrapper_t *wrapper = NULL;
        int                      ret     = -1;

        wrapper = rpcsvc_notify_wrapper_alloc ();
        if (!wrapper) {
                goto out;
        }
        svc->mydata   = mydata;  /* this_xlator */
        wrapper->data = mydata;
        wrapper->notify = notify;

        pthread_mutex_lock (&svc->rpclock);
        {
                list_add_tail (&wrapper->list, &svc->notify);
                svc->notify_count++;
        }
        pthread_mutex_unlock (&svc->rpclock);

        ret = 0;
out:
        return ret;
}


inline int
rpcsvc_program_register (rpcsvc_t *svc, rpcsvc_program_t *program)
{
        int               ret                = -1;
        rpcsvc_program_t *newprog            = NULL;
        char              already_registered = 0;

        if (!svc) {
                goto out;
        }

        if (program->actors == NULL) {
                goto out;
        }

        pthread_mutex_lock (&svc->rpclock);
        {
                list_for_each_entry (newprog, &svc->programs, program) {
                        if ((newprog->prognum == program->prognum)
                            && (newprog->progver == program->progver)) {
                                already_registered = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&svc->rpclock);

        if (already_registered) {
                ret = 0;
                goto out;
        }

        newprog = GF_CALLOC (1, sizeof(*newprog),gf_common_mt_rpcsvc_program_t);
        if (newprog == NULL) {
                goto out;
        }

        memcpy (newprog, program, sizeof (*program));

        INIT_LIST_HEAD (&newprog->program);

        pthread_mutex_lock (&svc->rpclock);
        {
                list_add_tail (&newprog->program, &svc->programs);
        }
        pthread_mutex_unlock (&svc->rpclock);

        ret = 0;
        gf_log (GF_RPCSVC, GF_LOG_DEBUG, "New program registered: %s, Num: %d,"
                " Ver: %d, Port: %d", newprog->progname, newprog->prognum,
                newprog->progver, newprog->progport);

out:
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Program registration failed:"
                        " %s, Num: %d, Ver: %d, Port: %d", program->progname,
                        program->prognum, program->progver, program->progport);
        }

        return ret;
}

static void
free_prog_details (gf_dump_rsp *rsp)
{
        gf_prog_detail *prev = NULL;
        gf_prog_detail *trav = NULL;

        trav = rsp->prog;
        while (trav) {
                prev = trav;
                trav = trav->next;
                GF_FREE (prev);
        }
}

static int
build_prog_details (rpcsvc_request_t *req, gf_dump_rsp *rsp)
{
        int               ret     = -1;
        rpcsvc_program_t *program = NULL;
        gf_prog_detail   *prog    = NULL;
        gf_prog_detail   *prev    = NULL;

        if (!req || !req->trans || !req->svc)
                goto out;

        list_for_each_entry (program, &req->svc->programs, program) {
                prog = GF_CALLOC (1, sizeof (*prog), 0);
                if (!prog)
                        goto out;
                prog->progname = program->progname;
                prog->prognum  = program->prognum;
                prog->progver  = program->progver;
                if (!rsp->prog)
                        rsp->prog = prog;
                if (prev)
                        prev->next = prog;
                prev = prog;
        }
        if (prev)
                ret = 0;
out:
        return ret;
}

static int
rpcsvc_dump (rpcsvc_request_t *req)
{
        char         rsp_buf[8 * 1024] = {0,};
        gf_dump_rsp  rsp               = {0,};
        struct iovec iov               = {0,};
        int          op_errno          = EINVAL;
        int          ret               = -1;
        uint32_t     dump_rsp_len      = 0;

        if (!req)
                goto sendrsp;

        ret = build_prog_details (req, &rsp);
        if (ret < 0) {
                op_errno = -ret;
                goto sendrsp;
        }

        op_errno = 0;

sendrsp:
        rsp.op_errno = gf_errno_to_error (op_errno);
        rsp.op_ret   = ret;

        dump_rsp_len = xdr_sizeof ((xdrproc_t) xdr_gf_dump_rsp,
                                   &rsp);

        iov.iov_base = rsp_buf;
        iov.iov_len  = dump_rsp_len;

        ret = xdr_serialize_generic (iov, &rsp, (xdrproc_t)xdr_gf_dump_rsp);
        if (ret < 0) {
                ret = RPCSVC_ACTOR_ERROR;
        } else {
                rpcsvc_submit_generic (req, &iov, 1, NULL, 0, NULL);
                ret = 0;
        }

        free_prog_details (&rsp);

        return ret;
}

int
rpcsvc_init_options (rpcsvc_t *svc, dict_t *options)
{
        char            *optstr = NULL;
        int             ret = -1;

        if ((!svc) || (!options))
                return -1;

        svc->memfactor = RPCSVC_DEFAULT_MEMFACTOR;

        svc->register_portmap = _gf_true;
        if (dict_get (options, "rpc.register-with-portmap")) {
                ret = dict_get_str (options, "rpc.register-with-portmap",
                                    &optstr);
                if (ret < 0) {
                        gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to parse "
                                "dict");
                        goto out;
                }

                ret = gf_string2boolean (optstr, &svc->register_portmap);
                if (ret < 0) {
                        gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to parse bool "
                                "string");
                        goto out;
                }
        }

        if (!svc->register_portmap)
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Portmap registration "
                        "disabled");
        ret = 0;
out:
        return ret;
}

int
rpcsvc_reconfigure_options (rpcsvc_t *svc, dict_t *options)
{
        xlator_t         *xlator    = NULL;
        xlator_list_t    *volentry  = NULL;
        char             *srchkey   = NULL;
        char             *keyval    = NULL;
        int              ret        = -1;

        if ((!svc) || (!svc->options) || (!options))
                return (-1);

        /* Fetch the xlator from svc */
        xlator = (xlator_t *) svc->mydata;
        if (!xlator)
                return (-1);

        /* Reconfigure the volume specific rpc-auth.addr allow part */
        volentry = xlator->children;
        while (volentry) {
                ret = gf_asprintf (&srchkey, "rpc-auth.addr.%s.allow",
                                             volentry->xlator->name);
                if (ret == -1) {
                        gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                        return (-1);
                }

                /* If found the srchkey, delete old key/val pair
                 * and set the key with new value.
                 */
                if (!dict_get_str (options, srchkey, &keyval)) {
                        dict_del (svc->options, srchkey);
                        ret = dict_set_str (svc->options, srchkey, keyval);
                        if (ret < 0) {
                                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                                        "dict_set_str error");
                                GF_FREE (srchkey);
                                return (-1);
                        }
                }

                GF_FREE (srchkey);
                volentry = volentry->next;
        }

        /* Reconfigure the volume specific rpc-auth.addr reject part */
        volentry = xlator->children;
        while (volentry) {
                ret = gf_asprintf (&srchkey, "rpc-auth.addr.%s.reject",
                                             volentry->xlator->name);
                if (ret == -1) {
                        gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                        return (-1);
                }

                /* If found the srchkey, delete old key/val pair
                 * and set the key with new value.
                 */
                if (!dict_get_str (options, srchkey, &keyval)) {
                        dict_del (svc->options, srchkey);
                        ret = dict_set_str (svc->options, srchkey, keyval);
                        if (ret < 0) {
                                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                                        "dict_set_str error");
                                GF_FREE (srchkey);
                                return (-1);
                        }
                }

                GF_FREE (srchkey);
                volentry = volentry->next;
        }

        ret = rpcsvc_init_options (svc, options);
        if (ret)
                return (-1);

        return rpcsvc_auth_reconf (svc, options);
}

int
rpcsvc_transport_unix_options_build (dict_t **options, char *filepath)
{
        dict_t                  *dict = NULL;
        char                    *fpath = NULL;
        int                     ret = -1;

        GF_ASSERT (filepath);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        fpath = gf_strdup (filepath);
        if (!fpath) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (dict, "transport.socket.listen-path", fpath);
        if (ret)
                goto out;

        ret = dict_set_str (dict, "transport.address-family", "unix");
        if (ret)
                goto out;

        ret = dict_set_str (dict, "transport.socket.nodelay", "off");
        if (ret)
                goto out;

        ret = dict_set_str (dict, "transport-type", "socket");
        if (ret)
                goto out;

        *options = dict;
out:
        if (ret) {
                GF_FREE (fpath);
                if (dict)
                        dict_unref (dict);
        }
        return ret;
}

/*
 * Configure() the rpc.outstanding-rpc-limit param.
 * If dict_get_int32() for dict-key "rpc.outstanding-rpc-limit" FAILS,
 * it would set the value as "defvalue". Otherwise it would fetch the
 * value and round up to multiple-of-8. defvalue must be +ve.
 *
 * NB: defval or set-value "0" is special which means unlimited/65536.
 */
int
rpcsvc_set_outstanding_rpc_limit (rpcsvc_t *svc, dict_t *options, int defvalue)
{
        int            ret        = -1; /* FAILURE */
        int            rpclim     = 0;
        static char    *rpclimkey = "rpc.outstanding-rpc-limit";

        if ((!svc) || (!options))
                return (-1);

        if ((defvalue < RPCSVC_MIN_OUTSTANDING_RPC_LIMIT) ||
            (defvalue > RPCSVC_MAX_OUTSTANDING_RPC_LIMIT)) {
                return (-1);
        }

        /* Fetch the rpc.outstanding-rpc-limit from dict. */
        ret = dict_get_int32 (options, rpclimkey, &rpclim);
        if (ret < 0) {
                /* Fall back to default for FAILURE */
                rpclim = defvalue;
        }

        /* Round up to multiple-of-8. It must not exceed
         * RPCSVC_MAX_OUTSTANDING_RPC_LIMIT.
         */
        rpclim = ((rpclim + 8 - 1) >> 3) * 8;
        if (rpclim > RPCSVC_MAX_OUTSTANDING_RPC_LIMIT) {
                rpclim = RPCSVC_MAX_OUTSTANDING_RPC_LIMIT;
        }

        if (svc->outstanding_rpc_limit != rpclim) {
                svc->outstanding_rpc_limit = rpclim;
                gf_log (GF_RPCSVC, GF_LOG_INFO,
                        "Configured %s with value %d", rpclimkey, rpclim);
        }

        return (0);
}

/* The global RPC service initializer.
 */
rpcsvc_t *
rpcsvc_init (xlator_t *xl, glusterfs_ctx_t *ctx, dict_t *options,
             uint32_t poolcount)
{
        rpcsvc_t          *svc              = NULL;
        int                ret              = -1;

        if ((!xl) || (!ctx) || (!options))
                return NULL;

        svc = GF_CALLOC (1, sizeof (*svc), gf_common_mt_rpcsvc_t);
        if (!svc)
                return NULL;

        pthread_mutex_init (&svc->rpclock, NULL);
        INIT_LIST_HEAD (&svc->authschemes);
        INIT_LIST_HEAD (&svc->notify);
        INIT_LIST_HEAD (&svc->listeners);
        INIT_LIST_HEAD (&svc->programs);

        ret = rpcsvc_init_options (svc, options);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to init options");
                goto free_svc;
        }

        if (!poolcount)
                poolcount = RPCSVC_POOLCOUNT_MULT * svc->memfactor;

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "rx pool: %d", poolcount);
        svc->rxpool = mem_pool_new (rpcsvc_request_t, poolcount);
        /* TODO: leak */
        if (!svc->rxpool) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "mem pool allocation failed");
                goto free_svc;
        }

        ret = rpcsvc_auth_init (svc, options);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to init "
                        "authentication");
                goto free_svc;
        }

        ret = -1;
        svc->options = options;
        svc->ctx = ctx;
        svc->mydata = xl;
        gf_log (GF_RPCSVC, GF_LOG_DEBUG, "RPC service inited.");

        gluster_dump_prog.options = options;

        ret = rpcsvc_program_register (svc, &gluster_dump_prog);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "failed to register DUMP program");
                goto free_svc;
        }

        ret = 0;
free_svc:
        if (ret == -1) {
                GF_FREE (svc);
                svc = NULL;
        }

        return svc;
}


int
rpcsvc_transport_peer_check_search (dict_t *options, char *pattern,
                                    char *ip, char *hostname)
{
        int                      ret           = -1;
        char                    *addrtok       = NULL;
        char                    *addrstr       = NULL;
        char                    *dup_addrstr   = NULL;
        char                    *svptr         = NULL;

        if ((!options) || (!ip))
                return -1;

        ret = dict_get_str (options, pattern, &addrstr);
        if (ret < 0) {
                ret = -1;
                goto err;
        }

        if (!addrstr) {
                ret = -1;
                goto err;
        }

        dup_addrstr = gf_strdup (addrstr);
        addrtok = strtok_r (dup_addrstr, ",", &svptr);
        while (addrtok) {

                /* CASEFOLD not present on Solaris */
#ifdef FNM_CASEFOLD
                ret = fnmatch (addrtok, ip, FNM_CASEFOLD);
#else
                ret = fnmatch (addrtok, ip, 0);
#endif
                if (ret == 0)
                        goto err;

                /* compare hostnames if applicable */
                if (hostname) {
#ifdef FNM_CASEFOLD
                        ret = fnmatch (addrtok, hostname, FNM_CASEFOLD);
#else
                        ret = fnmatch (addrtok, hostname, 0);
#endif
                        if (ret == 0)
                                goto err;
                }

                addrtok = strtok_r (NULL, ",", &svptr);
        }

        ret = -1;
err:
        GF_FREE (dup_addrstr);

        return ret;
}


static int
rpcsvc_transport_peer_check_allow (dict_t *options, char *volname,
                                   char *ip, char *hostname)
{
        int      ret     = RPCSVC_AUTH_DONTCARE;
        char    *srchstr = NULL;

        if ((!options) || (!ip) || (!volname))
                return ret;

        ret = gf_asprintf (&srchstr, "rpc-auth.addr.%s.allow", volname);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                ret = RPCSVC_AUTH_DONTCARE;
                goto out;
        }

        ret = rpcsvc_transport_peer_check_search (options, srchstr,
                                                  ip, hostname);
        GF_FREE (srchstr);

        if (ret == 0)
                ret = RPCSVC_AUTH_ACCEPT;
        else
                ret = RPCSVC_AUTH_REJECT;
out:
        return ret;
}

static int
rpcsvc_transport_peer_check_reject (dict_t *options, char *volname,
                                    char *ip, char *hostname)
{
        int      ret     = RPCSVC_AUTH_DONTCARE;
        char    *srchstr = NULL;

        if ((!options) || (!ip) || (!volname))
                return ret;

        ret = gf_asprintf (&srchstr, "rpc-auth.addr.%s.reject",
                           volname);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                ret = RPCSVC_AUTH_REJECT;
                goto out;
        }

        ret = rpcsvc_transport_peer_check_search (options, srchstr,
                                                  ip, hostname);
        GF_FREE (srchstr);

        if (ret == 0)
                ret = RPCSVC_AUTH_REJECT;
        else
                ret = RPCSVC_AUTH_DONTCARE;
out:
        return ret;
}


/* Combines rpc auth's allow and reject options.
 * Order of checks is important.
 * First,              REJECT if either rejects.
 * If neither rejects, ACCEPT if either accepts.
 * If neither accepts, DONTCARE
 */
int
rpcsvc_combine_allow_reject_volume_check (int allow, int reject)
{
        if (allow == RPCSVC_AUTH_REJECT ||
            reject == RPCSVC_AUTH_REJECT)
                return RPCSVC_AUTH_REJECT;

        if (allow == RPCSVC_AUTH_ACCEPT ||
            reject == RPCSVC_AUTH_ACCEPT)
                return RPCSVC_AUTH_ACCEPT;

        return RPCSVC_AUTH_DONTCARE;
}

int
rpcsvc_auth_check (rpcsvc_t *svc, char *volname,
                   rpc_transport_t *trans)
{
        int     ret                            = RPCSVC_AUTH_REJECT;
        int     accept                         = RPCSVC_AUTH_REJECT;
        int     reject                         = RPCSVC_AUTH_REJECT;
        char   *hostname                       = NULL;
        char   *ip                             = NULL;
        char    client_ip[RPCSVC_PEER_STRLEN]  = {0};
        char   *allow_str                      = NULL;
        char   *reject_str                     = NULL;
        char   *srchstr                        = NULL;
        dict_t *options                        = NULL;

        if (!svc || !volname || !trans)
                return ret;

        /* Fetch the options from svc struct and validate */
        options = svc->options;
        if (!options)
                return ret;

        ret = rpcsvc_transport_peername (trans, client_ip, RPCSVC_PEER_STRLEN);
        if (ret != 0) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to get remote addr: "
                        "%s", gai_strerror (ret));
                return RPCSVC_AUTH_REJECT;
        }

        /* Accept if its the default case: Allow all, Reject none
         * The default volfile always contains a 'allow *' rule
         * for each volume. If allow rule is missing (which implies
         * there is some bad volfile generating code doing this), we
         * assume no one is allowed mounts, and thus, we reject mounts.
         */
        ret = gf_asprintf (&srchstr, "rpc-auth.addr.%s.allow", volname);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                return RPCSVC_AUTH_REJECT;
        }

        ret = dict_get_str (options, srchstr, &allow_str);
        GF_FREE (srchstr);
        if (ret < 0)
                return RPCSVC_AUTH_REJECT;

        ret = gf_asprintf (&srchstr, "rpc-auth.addr.%s.reject", volname);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                return RPCSVC_AUTH_REJECT;
        }

        ret = dict_get_str (options, srchstr, &reject_str);
        GF_FREE (srchstr);
        if (reject_str == NULL && !strcmp ("*", allow_str))
                return RPCSVC_AUTH_ACCEPT;

        /* Non-default rule, authenticate */
        if (!get_host_name (client_ip, &ip))
                ip = client_ip;

        /* addr-namelookup check */
        if (svc->addr_namelookup == _gf_true) {
                ret = gf_get_hostname_from_ip (ip, &hostname);
                if (ret) {
                        if (hostname)
                                GF_FREE (hostname);
                        /* failed to get hostname, but hostname auth
                         * is enabled, so authentication will not be
                         * 100% correct. reject mounts
                         */
                        return RPCSVC_AUTH_REJECT;
                }
        }

        accept = rpcsvc_transport_peer_check_allow (options, volname,
                                                    ip, hostname);

        reject = rpcsvc_transport_peer_check_reject (options, volname,
                                                     ip, hostname);

        if (hostname)
                GF_FREE (hostname);
        return rpcsvc_combine_allow_reject_volume_check (accept, reject);
}

int
rpcsvc_transport_privport_check (rpcsvc_t *svc, char *volname,
                                 rpc_transport_t *trans)
{
        union gf_sock_union     sock_union;
        int                     ret = RPCSVC_AUTH_REJECT;
        socklen_t               sinsize = sizeof (&sock_union.sin);
        char                    *srchstr = NULL;
        char                    *valstr = NULL;
        uint16_t                port = 0;
        gf_boolean_t            insecure = _gf_false;

        memset (&sock_union, 0, sizeof (sock_union));

        if ((!svc) || (!volname) || (!trans))
                return ret;

        ret = rpcsvc_transport_peeraddr (trans, NULL, 0, &sock_union.storage,
                                         sinsize);
        if (ret != 0) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to get peer addr: %s",
                        gai_strerror (ret));
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        port = ntohs (sock_union.sin.sin_port);
        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Client port: %d", (int)port);
        /* If the port is already a privileged one, dont bother with checking
         * options.
         */
        if (port <= 1024) {
                ret = RPCSVC_AUTH_ACCEPT;
                goto err;
        }

        /* Disabled by default */
        ret = gf_asprintf (&srchstr, "rpc-auth.ports.%s.insecure", volname);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        ret = dict_get_str (svc->options, srchstr, &valstr);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to"
                        " read rpc-auth.ports.insecure value");
                goto err;
        }

        ret = gf_string2boolean (valstr, &insecure);
        if (ret) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to"
                        " convert rpc-auth.ports.insecure value");
                goto err;
        }

        ret = insecure ? RPCSVC_AUTH_ACCEPT : RPCSVC_AUTH_REJECT;

        if (ret == RPCSVC_AUTH_ACCEPT)
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Unprivileged port allowed");
        else
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Unprivileged port not"
                        " allowed");

err:
        if (srchstr)
                GF_FREE (srchstr);

        return ret;
}


char *
rpcsvc_volume_allowed (dict_t *options, char *volname)
{
        char    globalrule[] = "rpc-auth.addr.allow";
        char    *srchstr = NULL;
        char    *addrstr = NULL;
        int     ret = -1;

        if ((!options) || (!volname))
                return NULL;

        ret = gf_asprintf (&srchstr, "rpc-auth.addr.%s.allow", volname);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "asprintf failed");
                goto out;
        }

        if (!dict_get (options, srchstr))
                ret = dict_get_str (options, globalrule, &addrstr);
        else
                ret = dict_get_str (options, srchstr, &addrstr);

out:
        GF_FREE (srchstr);

        return addrstr;
}


rpcsvc_actor_t gluster_dump_actors[] = {
        [GF_DUMP_NULL]      = {"NULL",     GF_DUMP_NULL,     NULL,        NULL, 0, DRC_NA},
        [GF_DUMP_DUMP]      = {"DUMP",     GF_DUMP_DUMP,     rpcsvc_dump, NULL, 0, DRC_NA},
        [GF_DUMP_MAXVALUE]  = {"MAXVALUE", GF_DUMP_MAXVALUE, NULL,        NULL, 0, DRC_NA},
};


struct rpcsvc_program gluster_dump_prog = {
        .progname  = "GF-DUMP",
        .prognum   = GLUSTER_DUMP_PROGRAM,
        .progver   = GLUSTER_DUMP_VERSION,
        .actors    = gluster_dump_actors,
        .numactors = 2,
};
