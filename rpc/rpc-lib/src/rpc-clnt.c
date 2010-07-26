/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpc-clnt.h"
#include "xdr-rpcclnt.h"
#include "rpc-transport.h"
#include "protocol-common.h"

uint64_t
rpc_clnt_new_callid (struct rpc_clnt *clnt)
{
        uint64_t callid = 0;

        pthread_mutex_lock (&clnt->lock);
        {
                callid = ++clnt->xid;
        }
        pthread_mutex_unlock (&clnt->lock);

        return callid;
}


struct saved_frame *
__saved_frames_get_timedout (struct saved_frames *frames, uint32_t timeout,
                             struct timeval *current)
{
	struct saved_frame *bailout_frame = NULL, *tmp = NULL;

	if (!list_empty(&frames->sf.list)) {
		tmp = list_entry (frames->sf.list.next, typeof (*tmp), list);
		if ((tmp->saved_at.tv_sec + timeout) < current->tv_sec) {
			bailout_frame = tmp;
			list_del_init (&bailout_frame->list);
			frames->count--;
		}
	}

	return bailout_frame;
}


struct saved_frame *
__saved_frames_put (struct saved_frames *frames, void *frame, int32_t procnum,
                    rpc_clnt_prog_t *prog, fop_cbk_fn_t cbk, int64_t callid)
{
	struct saved_frame *saved_frame = NULL;

	saved_frame = GF_CALLOC (1, sizeof (*saved_frame),
                                 gf_common_mt_rpcclnt_savedframe_t);
	if (!saved_frame) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                goto out;
	}
        /* THIS should be saved and set back */

	INIT_LIST_HEAD (&saved_frame->list);

	saved_frame->capital_this = THIS;
	saved_frame->frame        = frame;
	saved_frame->procnum      = procnum;
	saved_frame->callid       = callid;
        saved_frame->prog         = prog;
        saved_frame->cbkfn        = cbk;

	gettimeofday (&saved_frame->saved_at, NULL);

	list_add_tail (&saved_frame->list, &frames->sf.list);
	frames->count++;

out:
	return saved_frame;
}


void
saved_frames_delete (struct saved_frame *saved_frame,
                     rpc_clnt_connection_t *conn)
{
        if (!saved_frame || !conn) {
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                list_del_init (&saved_frame->list);
                conn->saved_frames->count--;
        }
        pthread_mutex_unlock (&conn->lock);

        GF_FREE (saved_frame);
out:
        return;
}


static void
call_bail (void *data)
{
        struct rpc_clnt       *clnt = NULL;
        rpc_clnt_connection_t *conn = NULL;
        struct timeval         current;
        struct list_head       list;
        struct saved_frame    *saved_frame = NULL;
        struct saved_frame    *trav = NULL;
        struct saved_frame    *tmp = NULL;
        struct tm              frame_sent_tm;
        char                   frame_sent[32] = {0,};
        struct timeval         timeout = {0,};
        gf_timer_cbk_t         timer_cbk = NULL;
        struct rpc_req         req;
        struct iovec           iov = {0,};

        GF_VALIDATE_OR_GOTO ("client", data, out);

        clnt = data;

        conn = &clnt->conn;

        gettimeofday (&current, NULL);
        INIT_LIST_HEAD (&list);

        pthread_mutex_lock (&conn->lock);
        {
                /* Chaining to get call-always functionality from
                   call-once timer */
                if (conn->timer) {
                        timer_cbk = conn->timer->callbk;

                        timeout.tv_sec = 10;
                        timeout.tv_usec = 0;

                        gf_timer_call_cancel (clnt->ctx, conn->timer);
                        conn->timer = gf_timer_call_after (clnt->ctx,
                                                           timeout,
                                                           call_bail,
                                                           (void *) clnt);

                        if (conn->timer == NULL) {
                                gf_log (conn->trans->name, GF_LOG_DEBUG,
                                        "Cannot create bailout timer");
                        }
                }

                do {
                        saved_frame =
                                __saved_frames_get_timedout (conn->saved_frames,
                                                             conn->frame_timeout,
                                                             &current);
                        if (saved_frame)
                                list_add (&saved_frame->list, &list);

                } while (saved_frame);
        }
        pthread_mutex_unlock (&conn->lock);

        list_for_each_entry_safe (trav, tmp, &list, list) {
                localtime_r (&trav->saved_at.tv_sec, &frame_sent_tm);
                strftime (frame_sent, 32, "%Y-%m-%d %H:%M:%S", &frame_sent_tm);

		gf_log (conn->trans->name, GF_LOG_ERROR,
			"bailing out frame type(%s) op(%s(%d)) sent = %s. "
                        "timeout = %d",
			trav->prog->progname, (trav->prog->procnames) ?
                        trav->prog->procnames[trav->procnum] : "--",
                        trav->procnum, frame_sent,
                        conn->frame_timeout);

		trav->cbkfn (&req, &iov, 1, trav->frame);

                list_del_init (&trav->list);
                GF_FREE (trav);
        }
out:
        return;
}


/* to be called with conn->lock held */
struct saved_frame *
__save_frame (struct rpc_clnt *rpc_clnt, call_frame_t *frame, int procnum,
              rpc_clnt_prog_t *prog, fop_cbk_fn_t cbk, uint64_t callid)
{
        rpc_clnt_connection_t *conn        = NULL;
        struct timeval         timeout     = {0, };
        struct saved_frame    *saved_frame = NULL;

        conn = &rpc_clnt->conn;

        saved_frame = __saved_frames_put (conn->saved_frames, frame,
                                          procnum, prog, cbk, callid);
        if (saved_frame == NULL) {
                goto out;
        }

        /* TODO: make timeout configurable */
        if (conn->timer == NULL) {
                timeout.tv_sec  = 10;
                timeout.tv_usec = 0;
                conn->timer = gf_timer_call_after (rpc_clnt->ctx,
                                                   timeout,
                                                   call_bail,
                                                   (void *) rpc_clnt);
        }

out:
        return saved_frame;
}


struct saved_frames *
saved_frames_new (void)
{
	struct saved_frames *saved_frames = NULL;

	saved_frames = GF_CALLOC (1, sizeof (*saved_frames),
                                  gf_common_mt_rpcclnt_savedframe_t);
	if (!saved_frames) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
		return NULL;
	}

	INIT_LIST_HEAD (&saved_frames->sf.list);

	return saved_frames;
}


int
__saved_frame_copy (struct saved_frames *frames, int64_t callid,
                    struct saved_frame *saved_frame)
{
	struct saved_frame *tmp   = NULL;
        int                 ret   = -1;

        if (!saved_frame) {
                ret = 0;
                goto out;
        }

	list_for_each_entry (tmp, &frames->sf.list, list) {
		if (tmp->callid == callid) {
			*saved_frame = *tmp;
                        ret = 0;
			break;
		}
	}

out:
	return ret;
}


struct saved_frame *
__saved_frame_get (struct saved_frames *frames, int64_t callid)
{
	struct saved_frame *saved_frame = NULL;
	struct saved_frame *tmp = NULL;

	list_for_each_entry (tmp, &frames->sf.list, list) {
		if (tmp->callid == callid) {
			list_del_init (&tmp->list);
			frames->count--;
			saved_frame = tmp;
			break;
		}
	}

	if (saved_frame) {
                THIS  = saved_frame->capital_this;
        }

	return saved_frame;
}

void
saved_frames_unwind (struct saved_frames *saved_frames)
{
	struct saved_frame   *trav = NULL;
	struct saved_frame   *tmp = NULL;
        struct tm            *frame_sent_tm = NULL;
        char                 timestr[256] = {0,};

        struct rpc_req        req;
        struct iovec          iov = {0,};

        memset (&req, 0, sizeof (req));

        req.rpc_status = -1;

	list_for_each_entry_safe (trav, tmp, &saved_frames->sf.list, list) {
                frame_sent_tm = localtime (&trav->saved_at.tv_sec);
                strftime (timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S",
                          frame_sent_tm);
                snprintf (timestr + strlen (timestr), sizeof(timestr) - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, trav->saved_at.tv_usec);

		gf_log ("rpc-clnt", GF_LOG_ERROR,
			"forced unwinding frame type(%s) op(%s(%d)) called at %s",
			trav->prog->progname, (trav->prog->procnames) ?
                        trav->prog->procnames[trav->procnum] : "--",
                        trav->procnum, timestr);

		saved_frames->count--;

		trav->cbkfn (&req, &iov, 1, trav->frame);

		list_del_init (&trav->list);
		GF_FREE (trav);
	}
}


void
saved_frames_destroy (struct saved_frames *frames)
{
	saved_frames_unwind (frames);

	GF_FREE (frames);
}


void
rpc_clnt_reconnect (void *trans_ptr)
{
        rpc_transport_t         *trans = NULL;
        rpc_clnt_connection_t   *conn  = NULL;
        struct timeval           tv    = {0, 0};
        int32_t                  ret   = 0;
        struct rpc_clnt         *clnt  = NULL;

        trans = trans_ptr;
        if (!trans || !trans->mydata)
                return;

        conn  = trans->mydata;
        clnt = conn->rpc_clnt;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->reconnect)
                        gf_timer_call_cancel (clnt->ctx,
                                              conn->reconnect);
                conn->reconnect = 0;

                if (conn->connected == 0) {
                        tv.tv_sec = 3;

                        gf_log (trans->name, GF_LOG_TRACE,
                                "attempting reconnect");
                        ret = rpc_transport_connect (trans);

                        conn->reconnect =
                                gf_timer_call_after (clnt->ctx, tv,
                                                     rpc_clnt_reconnect,
                                                     trans);
                } else {
                        gf_log (trans->name, GF_LOG_TRACE,
                                "breaking reconnect chain");
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if ((ret == -1) && (errno != EINPROGRESS) && (clnt->notifyfn)) {
                clnt->notifyfn (clnt, clnt->mydata, RPC_CLNT_DISCONNECT, NULL);
        }

        return;
}


int
rpc_clnt_fill_request_info (struct rpc_clnt *clnt, rpc_request_info_t *info)
{
        struct saved_frame  saved_frame = {{}, 0};
        int                 ret         = -1;

        pthread_mutex_lock (&clnt->conn.lock);
        {
                ret = __saved_frame_copy (clnt->conn.saved_frames, info->xid,
                                          &saved_frame);
        }
        pthread_mutex_unlock (&clnt->conn.lock);

        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_CRITICAL, "cannot lookup the saved "
                        "frame corresponding to xid (%d)", info->xid);
                goto out;
        }

        info->prognum = saved_frame.prog->prognum;
        info->procnum = saved_frame.procnum;
        info->progver = saved_frame.prog->progver;
        info->rsp     = saved_frame.rsp;

        ret = 0;
out:
        return ret;
}


/*
 * client_protocol_cleanup - cleanup function
 * @trans: transport object
 *
 */
int
rpc_clnt_connection_cleanup (rpc_clnt_connection_t *conn)
{
        struct saved_frames    *saved_frames = NULL;
        struct rpc_clnt         *clnt  = NULL;

        if (!conn) {
                goto out;
        }

        clnt = conn->rpc_clnt;

        gf_log ("rpc-clnt", GF_LOG_DEBUG,
                "cleaning up state in transport object %p", conn->trans);

        pthread_mutex_lock (&conn->lock);
        {
                saved_frames = conn->saved_frames;
                conn->saved_frames = saved_frames_new ();

                /* bailout logic cleanup */
                if (conn->timer) {
                        gf_timer_call_cancel (clnt->ctx, conn->timer);
                        conn->timer = NULL;
                }

                if (conn->reconnect == NULL) {
                        /* :O This part is empty.. any thing missing? */
                }

                conn->connected = 0;
        }
        pthread_mutex_unlock (&conn->lock);

        saved_frames_destroy (saved_frames);

out:
        return 0;
}

/*
 * lookup_frame - lookup call frame corresponding to a given callid
 * @trans: transport object
 * @callid: call id of the frame
 *
 * not for external reference
 */

static struct saved_frame *
lookup_frame (rpc_clnt_connection_t *conn, int64_t callid)
{
        struct saved_frame *frame = NULL;

        pthread_mutex_lock (&conn->lock);
        {
                frame = __saved_frame_get (conn->saved_frames, callid);
        }
        pthread_mutex_unlock (&conn->lock);

        return frame;
}


int
rpc_clnt_reply_fill (rpc_transport_pollin_t *msg,
                     rpc_clnt_connection_t *conn,
                     struct rpc_msg *replymsg, struct iovec progmsg,
                     struct rpc_req *req, struct saved_frame *saved_frame)
{
        int           ret   = -1;

        if ((!conn) || (!replymsg)|| (!req) || (!saved_frame) || (!msg)) {
                goto out;
        }

        req->rpc_status = 0;
        if ((rpc_reply_status (replymsg) == MSG_DENIED)
            || (rpc_accepted_reply_status (replymsg) != SUCCESS)) {
                req->rpc_status = -1;
        }

        req->xid = rpc_reply_xid (replymsg);
        req->prog = saved_frame->prog;
        req->procnum = saved_frame->procnum;
        req->conn = conn;

        req->rsp[0] = progmsg;

        if (msg->vectored) {
                req->rsp[1].iov_base = iobuf_ptr (msg->data.vector.iobuf2);
                req->rsp[1].iov_len = msg->data.vector.size2;

                req->rspcnt = 2;

                req->rsp_prochdr = iobuf_ref (msg->data.vector.iobuf1);
                req->rsp_procpayload = iobuf_ref (msg->data.vector.iobuf2);
        } else {
                req->rspcnt = 1;

                req->rsp_prochdr = iobuf_ref (msg->data.simple.iobuf);
        }

        /* By this time, the data bytes for the auth scheme would have already
         * been copied into the required sections of the req structure,
         * we just need to fill in the meta-data about it now.
         */
        if (req->rpc_status == 0) {
                /*
                 * req->verf.flavour = rpc_reply_verf_flavour (replymsg);
                 * req->verf.datalen = rpc_reply_verf_len (replymsg);
                 */
        }

        ret = 0;

out:
        return ret;
}


void
rpc_clnt_reply_deinit (struct rpc_req *req)
{
        if (!req) {
                goto out;
        }

        if (req->rsp_prochdr) {
                iobuf_unref (req->rsp_prochdr);
        }

        if (req->rsp_procpayload) {
                iobuf_unref (req->rsp_procpayload);
        }

out:
        return;
}


/* TODO: use mem-pool for allocating requests */
int
rpc_clnt_reply_init (rpc_clnt_connection_t *conn, rpc_transport_pollin_t *msg,
                     struct rpc_req *req, struct saved_frame *saved_frame)
{
        char                    *msgbuf = NULL;
        struct rpc_msg          rpcmsg;
        struct iovec            progmsg;        /* RPC Program payload */
        size_t                  msglen  = 0;
        int                     ret     = -1;

        if (msg->vectored) {
                msgbuf = iobuf_ptr (msg->data.vector.iobuf1);
                msglen = msg->data.vector.size1;
        } else {
                msgbuf = iobuf_ptr (msg->data.simple.iobuf);
                msglen = msg->data.simple.size;
        }

        ret = xdr_to_rpc_reply (msgbuf, msglen, &rpcmsg, &progmsg,
                                req->verf.authdata);
        if (ret != 0) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "RPC reply decoding failed");
                goto out;
        }

        ret = rpc_clnt_reply_fill (msg, conn, &rpcmsg, progmsg, req,
                                   saved_frame);
        if (ret != 0) {
                goto out;
        }

        gf_log ("rpc-clnt", GF_LOG_TRACE, "RPC XID: %"PRIx64", Program: %s,"
                " ProgVers: %d, Proc: %d", saved_frame->callid,
                saved_frame->prog->progname, saved_frame->prog->progver,
                saved_frame->procnum);
/* TODO: */
        /* TODO: AUTH */
        /* The verifier that is sent in a reply is a string that can be used as
         * a shorthand in credentials for future transactions. We can opt not to
         * use this shorthand, preffering to use the original AUTH_UNIX method
         * for authentication (containing all the details for authentication in
         * credential itself). Hence it is not mandatory for us to be checking
         * the verifier. See Appendix A of rfc-5531 for more details.
         */

        /*
         * ret = rpc_authenticate (req);
         * if (ret == RPC_AUTH_REJECT) {
         * gf_log ("rpc-clnt", GF_LOG_ERROR, "Failed authentication");
         * ret = -1;
         * goto out;
         * }
         */

        /* If the error is not RPC_MISMATCH, we consider the call as accepted
         * since we are not handling authentication failures for now.
         */
        req->rpc_status = 0;

out:
        if (ret != 0) {
                req->rpc_status = -1;
        }

        return ret;
}


int
rpc_clnt_handle_reply (struct rpc_clnt *clnt, rpc_transport_pollin_t *pollin)
{
        rpc_clnt_connection_t *conn         = NULL;
        struct saved_frame    *saved_frame  = NULL;
        rpc_request_info_t    *request_info = NULL;
        int                    ret          = -1;
        struct rpc_req         req          = {0, };
        int                    cbk_ret      = -1;

        conn = &clnt->conn;

        request_info = pollin->private;

        saved_frame = lookup_frame (conn, (int64_t)request_info->xid);
        if (saved_frame == NULL) {
                gf_log ("rpc-clnt", GF_LOG_CRITICAL, "cannot lookup the "
                        "saved frame for reply with xid (%d), "
                        "prog-version (%d), prog-num (%d),"
                        "procnum (%d)", request_info->xid,
                        request_info->progver, request_info->prognum,
                        request_info->procnum);
                goto out;
        }

        ret = rpc_clnt_reply_init (conn, pollin, &req, saved_frame);
        if (ret != 0) {
                req.rpc_status = -1;
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "initialising rpc reply "
                        "failed");
        }

        cbk_ret = saved_frame->cbkfn (&req, req.rsp, req.rspcnt,
                                      saved_frame->frame);

        if (ret == 0) {
                rpc_clnt_reply_deinit (&req);
        }

        ret = 0;
out:

        if (saved_frame) {
                GF_FREE (saved_frame);
        }

        return cbk_ret;
}


inline void
rpc_clnt_set_connected (rpc_clnt_connection_t *conn)
{
        if (!conn) {
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                conn->connected = 1;
        }
        pthread_mutex_unlock (&conn->lock);

out:
        return;
}


void
rpc_clnt_unset_connected (rpc_clnt_connection_t *conn)
{
        if (!conn) {
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                conn->connected = 0;
        }
        pthread_mutex_unlock (&conn->lock);

out:
        return;
}


int
rpc_clnt_notify (rpc_transport_t *trans, void *mydata,
                 rpc_transport_event_t event, void *data, ...)
{
        rpc_clnt_connection_t  *conn     = NULL;
        struct rpc_clnt        *clnt     = NULL;
        int                     ret      = -1;
        rpc_request_info_t     *req_info = NULL;
        rpc_transport_pollin_t *pollin   = NULL;
        struct timeval          tv       = {0, };

        conn = mydata;
        if (conn == NULL) {
                goto out;
        }
        clnt = conn->rpc_clnt;

        switch (event) {
        case RPC_TRANSPORT_DISCONNECT:
        {
                rpc_clnt_connection_cleanup (&clnt->conn);

                pthread_mutex_lock (&conn->lock);
                {
                        if (conn->reconnect == NULL) {
                                tv.tv_sec = 10;

                                conn->reconnect =
                                        gf_timer_call_after (clnt->ctx, tv,
                                                             rpc_clnt_reconnect,
                                                             conn->trans);
                        }
                }
                pthread_mutex_unlock (&conn->lock);

                if (clnt->notifyfn)
                        ret = clnt->notifyfn (clnt, clnt->mydata, RPC_CLNT_DISCONNECT,
                                              NULL);
                break;
        }

        case RPC_TRANSPORT_CLEANUP:
                /* this event should not be received on a client for, a
                 * transport is only disconnected, but never destroyed.
                 */
                ret = 0;
                break;

        case RPC_TRANSPORT_MAP_XID_REQUEST:
        {
                req_info = data;
                ret = rpc_clnt_fill_request_info (clnt, req_info);
                break;
        }

        case RPC_TRANSPORT_MSG_RECEIVED:
        {
                pollin = data;
                ret = rpc_clnt_handle_reply (clnt, pollin);
                /* ret = clnt->notifyfn (clnt, clnt->mydata, RPC_CLNT_MSG,
                 * data);
                 */
                break;
        }

        case RPC_TRANSPORT_MSG_SENT:
        {
                pthread_mutex_lock (&conn->lock);
                {
                        gettimeofday (&conn->last_sent, NULL);
                }
                pthread_mutex_unlock (&conn->lock);

                ret = 0;
                break;
        }

        case RPC_TRANSPORT_CONNECT:
        {
                if (clnt->notifyfn)
                        ret = clnt->notifyfn (clnt, clnt->mydata, RPC_CLNT_CONNECT, NULL);
                break;
        }

        case RPC_TRANSPORT_ACCEPT:
                /* only meaningful on a server, no need of handling this event
                 * in a client.
                 */
                ret = 0;
                break;
        }

out:
        return ret;
}


void
rpc_clnt_connection_deinit (rpc_clnt_connection_t *conn)
{
        return;
}


inline int
rpc_clnt_connection_init (struct rpc_clnt *clnt, glusterfs_ctx_t *ctx,
                          dict_t *options, char *name)
{
        int                    ret  = -1;
        rpc_clnt_connection_t *conn = NULL;

        conn = &clnt->conn;
        pthread_mutex_init (&clnt->conn.lock, NULL);

        ret = dict_get_int32 (options, "frame-timeout",
                              &conn->frame_timeout);
        if (ret >= 0) {
                gf_log (name, GF_LOG_DEBUG,
                        "setting frame-timeout to %d", conn->frame_timeout);
        } else {
                gf_log (name, GF_LOG_DEBUG,
                        "defaulting frame-timeout to 30mins");
                conn->frame_timeout = 1800;
        }

        conn->trans = rpc_transport_load (ctx, options, name);
        if (!conn->trans) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "loading of new rpc-transport"
                        " failed");
                goto out;
        }

        rpc_transport_ref (conn->trans);

        conn->rpc_clnt = clnt;

        ret = rpc_transport_register_notify (conn->trans, rpc_clnt_notify,
                                             conn);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "registering notify failed");
                rpc_clnt_connection_cleanup (conn);
                conn = NULL;
                goto out;
        }

        conn->saved_frames = saved_frames_new ();
        if (!conn->saved_frames) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "creation of saved_frames "
                        "failed");
                rpc_clnt_connection_cleanup (conn);
                goto out;
        }

        rpc_clnt_reconnect (conn->trans);

        ret = 0;

out:
        return ret;
}


struct rpc_clnt *
rpc_clnt_init (struct rpc_clnt_config *config, dict_t *options,
               glusterfs_ctx_t *ctx, char *name)
{
        int                    ret  = -1;
        struct rpc_clnt       *rpc  = NULL;

        rpc = GF_CALLOC (1, sizeof (*rpc), gf_common_mt_rpcclnt_t);
        if (!rpc) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                goto out;
        }

        pthread_mutex_init (&rpc->lock, NULL);
        rpc->ctx = ctx;

        ret = rpc_clnt_connection_init (rpc, ctx, options, name);
        if (ret == -1) {
                pthread_mutex_destroy (&rpc->lock);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }
out:
        return rpc;
}


int
rpc_clnt_register_notify (struct rpc_clnt *rpc, rpc_clnt_notify_t fn,
                          void *mydata)
{
        rpc->mydata = mydata;
        rpc->notifyfn = fn;

        return 0;
}

ssize_t
xdr_serialize_glusterfs_auth (char *dest, struct auth_glusterfs_parms *au)
{
        ssize_t ret = -1;
        XDR     xdr;

        if ((!dest) || (!au))
                return -1;

        xdrmem_create (&xdr, dest, 1024,
                       XDR_ENCODE);

        if (!xdr_auth_glusterfs_parms (&xdr, au)) {
                ret = -1;
                goto ret;
        }

        ret = (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base));

ret:
        return ret;
}


int
rpc_clnt_fill_request (int prognum, int progver, int procnum, int payload,
                       uint64_t xid, struct auth_glusterfs_parms *au,
                       struct rpc_msg *request)
{
        int   ret          = -1;
        char  dest[1024]   = {0,};

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

        /* TODO: Using AUTH_GLUSTERFS for time-being. Make it modular in
         * future so it is easy to plug-in new authentication schemes.
         */
        ret = xdr_serialize_glusterfs_auth (dest, au);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "cannot encode credentials");
                goto out;
        }

        request->rm_call.cb_cred.oa_flavor = AUTH_GLUSTERFS;
        request->rm_call.cb_cred.oa_base   = dest;
        request->rm_call.cb_cred.oa_length = ret;

        request->rm_call.cb_verf.oa_flavor = AUTH_NONE;
        request->rm_call.cb_verf.oa_base = NULL;
        request->rm_call.cb_verf.oa_length = 0;

        ret = 0;
out:
        return ret;
}


void
rpc_clnt_set_lastfrag (uint32_t *fragsize) {
        (*fragsize) |= 0x80000000U;
}


void
rpc_clnt_set_frag_header_size (uint32_t size, char *haddr)
{
        size = htonl (size);
        memcpy (haddr, &size, sizeof (size));
}


void
rpc_clnt_set_last_frag_header_size (uint32_t size, char *haddr)
{
        rpc_clnt_set_lastfrag (&size);
        rpc_clnt_set_frag_header_size (size, haddr);
}


struct iovec
rpc_clnt_record_build_header (char *recordstart, size_t rlen,
                              struct rpc_msg *request, size_t payload)
{
        struct iovec    requesthdr = {0, };
        struct iovec    txrecord   = {0, 0};
        size_t          fraglen    = 0;
        int             ret        = -1;

        /* After leaving aside the 4 bytes for the fragment header, lets
         * encode the RPC reply structure into the buffer given to us.
         */
        ret = rpc_request_to_xdr (request, (recordstart + RPC_FRAGHDR_SIZE),
                                  rlen, &requesthdr);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG,
                        "Failed to create RPC request");
                goto out;
        }

        fraglen = payload + requesthdr.iov_len;
        gf_log ("rpc-clnt", GF_LOG_TRACE, "Request fraglen %zu, payload: %zu, "
                "rpc hdr: %zu", fraglen, payload, requesthdr.iov_len);

        /* Since we're not spreading RPC records over mutiple fragments
         * we just set this fragment as the first and last fragment for this
         * record.
         */
        rpc_clnt_set_last_frag_header_size (fraglen, recordstart);

        /* Even though the RPC record starts at recordstart+RPCSVC_FRAGHDR_SIZE
         * we need to transmit the record with the fragment header, which starts
         * at recordstart.
         */
        txrecord.iov_base = recordstart;

        /* Remember, this is only the vec for the RPC header and does not
         * include the payload above. We needed the payload only to calculate
         * the size of the full fragment. This size is sent in the fragment
         * header.
         */
        txrecord.iov_len = RPC_FRAGHDR_SIZE + requesthdr.iov_len;

out:
        return txrecord;
}


struct iobuf *
rpc_clnt_record_build_record (struct rpc_clnt *clnt, int prognum, int progver,
                              int procnum, size_t payload, uint64_t xid,
                              struct auth_glusterfs_parms *au, struct iovec *recbuf)
{
        struct rpc_msg           request     = {0, };
        struct iobuf            *request_iob = NULL;
        char                    *record      = NULL;
        struct iovec             recordhdr   = {0, };
        size_t                   pagesize    = 0;
        int                      ret         = -1;

        if ((!clnt) || (!recbuf) || (!au)) {
                goto out;
        }

        /* First, try to get a pointer into the buffer which the RPC
         * layer can use.
         */
        request_iob = iobuf_get (clnt->ctx->iobuf_pool);
        if (!request_iob) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "Failed to get iobuf");
                goto out;
        }

        pagesize = ((struct iobuf_pool *)clnt->ctx->iobuf_pool)->page_size;

        record = iobuf_ptr (request_iob);  /* Now we have it. */

        /* Fill the rpc structure and XDR it into the buffer got above. */
        ret = rpc_clnt_fill_request (prognum, progver, procnum, payload, xid,
                                     au, &request);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "cannot build a rpc-request "
                        "xid (%"PRIu64")", xid);
                goto out;
        }

        recordhdr = rpc_clnt_record_build_header (record, pagesize, &request,
                                                  payload);

        //GF_FREE (request.rm_call.cb_cred.oa_base);

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


struct iobuf *
rpc_clnt_record (struct rpc_clnt *clnt, call_frame_t *call_frame,
                 rpc_clnt_prog_t *prog,int procnum, size_t payload_len,
                 struct iovec *rpchdr, uint64_t callid)
{
        struct auth_glusterfs_parms  au                    = {0, };
        struct iobuf                *request_iob           = NULL;

        if (!prog || !rpchdr || !call_frame) {
                goto out;
        }

        au.pid      = call_frame->root->pid;
        au.uid      = call_frame->root->uid;
        au.gid      = call_frame->root->gid;
        au.ngrps    = call_frame->root->ngrps;
        au.lk_owner = call_frame->root->lk_owner;
        if (!au.lk_owner)
                au.lk_owner = au.pid;

        gf_log ("", GF_LOG_TRACE, "Auth Info: pid: %u, uid: %d"
                ", gid: %d, owner: %"PRId64,
                au.pid, au.uid, au.gid, au.lk_owner);

        memcpy (au.groups, call_frame->root->groups, 16);

        //rpc_transport_get_myname (clnt->conn.trans, myname, UNIX_PATH_MAX);
        //au.aup_machname = myname;

        /* Assuming the client program would like to speak to the same versioned
         * program on server.
         */
        request_iob = rpc_clnt_record_build_record (clnt, prog->prognum,
                                                    prog->progver,
                                                    procnum, payload_len,
                                                    callid, &au,
                                                    rpchdr);
        if (!request_iob) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "cannot build rpc-record");
                goto out;
        }

out:
        return request_iob;
}


int
rpc_clnt_submit (struct rpc_clnt *rpc, rpc_clnt_prog_t *prog,
                 int procnum, fop_cbk_fn_t cbkfn,
                 struct iovec *proghdr, int proghdrcount,
                 struct iovec *progpayload, int progpayloadcount,
                 struct iobref *iobref, void *frame)
{
        rpc_clnt_connection_t *conn        = NULL;
        struct iobuf          *request_iob = NULL;
        struct iovec           rpchdr      = {0,};
        struct rpc_req         rpcreq      = {0,};
        rpc_transport_req_t    req;
        int                    ret         = -1;
        int                    proglen     = 0;
        char                   new_iobref  = 0;
        uint64_t               callid      = 0;

        if (!rpc || !prog || !frame) {
                goto out;
        }

        memset (&req, 0, sizeof (req));

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                        goto out;
                }

                new_iobref = 1;
        }

        callid = rpc_clnt_new_callid (rpc);

        conn = &rpc->conn;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->connected == 0) {
                        rpc_transport_connect (conn->trans);
                }

                ret = -1;

                if (proghdr) {
                        proglen += iov_length (proghdr, proghdrcount);
                }

                if (progpayload) {
                        proglen += iov_length (progpayload,
                                               progpayloadcount);
                }

                request_iob = rpc_clnt_record (rpc, frame, prog,
                                               procnum, proglen,
                                               &rpchdr, callid);
                if (!request_iob) {
                        gf_log ("rpc-clnt", GF_LOG_DEBUG,
                                "cannot build rpc-record");
                        goto unlock;
                }

                iobref_add (iobref, request_iob);

                req.msg.rpchdr = &rpchdr;
                req.msg.rpchdrcount = 1;
                req.msg.proghdr = proghdr;
                req.msg.proghdrcount = proghdrcount;
                req.msg.progpayload = progpayload;
                req.msg.progpayloadcount = progpayloadcount;
                req.msg.iobref = iobref;

                ret = rpc_transport_submit_request (rpc->conn.trans,
                                                    &req);
                if (ret == -1) {
                        gf_log ("rpc-clnt", GF_LOG_DEBUG,
                                "transmission of rpc-request failed");
                }

                if ((ret >= 0) && frame) {
                        gettimeofday (&conn->last_sent, NULL);
                        /* Save the frame in queue */
                        __save_frame (rpc, frame, procnum, prog, cbkfn, callid);
                }

        }
unlock:
        pthread_mutex_unlock (&conn->lock);

        if (ret == -1) {
                goto out;
        }

        ret = 0;

out:
        iobuf_unref (request_iob);

        if (new_iobref && iobref) {
                iobref_unref (iobref);
        }

        if (frame && (ret == -1)) {
                rpcreq.rpc_status = -1;
                cbkfn (&rpcreq, NULL, 0, frame);
        }
        return ret;
}


void
rpc_clnt_destroy (struct rpc_clnt *rpc)
{
        rpc_clnt_connection_cleanup (&rpc->conn);
        pthread_mutex_destroy (&rpc->lock);
        pthread_mutex_destroy (&rpc->conn.lock);
        GF_FREE (rpc);
        return;
}
