/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define RPC_CLNT_DEFAULT_REQUEST_COUNT 4096

#include "rpc-clnt.h"
#include "byte-order.h"
#include "xdr-rpcclnt.h"
#include "rpc-transport.h"
#include "protocol-common.h"
#include "mem-pool.h"
#include "xdr-rpc.h"

void
rpc_clnt_reply_deinit (struct rpc_req *req, struct mem_pool *pool);

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
__saved_frames_put (struct saved_frames *frames, void *frame,
                    struct rpc_req *rpcreq)
{
	struct saved_frame *saved_frame = NULL;

        saved_frame = mem_get (rpcreq->conn->rpc_clnt->saved_frames_pool);
	if (!saved_frame) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                goto out;
	}
        /* THIS should be saved and set back */

        memset (saved_frame, 0, sizeof (*saved_frame));
	INIT_LIST_HEAD (&saved_frame->list);

	saved_frame->capital_this = THIS;
	saved_frame->frame        = frame;
        saved_frame->rpcreq       = rpcreq;
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

        if (saved_frame->rpcreq != NULL) {
                rpc_clnt_reply_deinit (saved_frame->rpcreq,
                                       conn->rpc_clnt->reqpool);
        }

        mem_put (conn->rpc_clnt->saved_frames_pool, saved_frame);
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
        char                   frame_sent[256] = {0,};
        struct timeval         timeout = {0,};
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
                snprintf (frame_sent + strlen (frame_sent),
                          256 - strlen (frame_sent),
                          ".%"GF_PRI_SUSECONDS, trav->saved_at.tv_usec);

		gf_log (conn->trans->name, GF_LOG_ERROR,
			"bailing out frame type(%s) op(%s(%d)) xid = 0x%ux "
                        "sent = %s. timeout = %d",
			trav->rpcreq->prog->progname,
                        (trav->rpcreq->prog->procnames) ?
                        trav->rpcreq->prog->procnames[trav->rpcreq->procnum] :
                        "--",
                        trav->rpcreq->procnum, trav->rpcreq->xid, frame_sent,
                        conn->frame_timeout);

                trav->rpcreq->rpc_status = -1;
		trav->rpcreq->cbkfn (trav->rpcreq, &iov, 1, trav->frame);

                rpc_clnt_reply_deinit (trav->rpcreq, clnt->reqpool);
                list_del_init (&trav->list);
                mem_put (conn->rpc_clnt->saved_frames_pool, trav);
        }
out:
        return;
}


/* to be called with conn->lock held */
struct saved_frame *
__save_frame (struct rpc_clnt *rpc_clnt, call_frame_t *frame,
              struct rpc_req *rpcreq)
{
        rpc_clnt_connection_t *conn        = NULL;
        struct timeval         timeout     = {0, };
        struct saved_frame    *saved_frame = NULL;

        conn = &rpc_clnt->conn;

        saved_frame = __saved_frames_put (conn->saved_frames, frame, rpcreq);

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
		if (tmp->rpcreq->xid == callid) {
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
		if (tmp->rpcreq->xid == callid) {
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
        struct mem_pool      *saved_frames_pool = NULL;
        struct tm            *frame_sent_tm = NULL;
        char                 timestr[256] = {0,};

        struct iovec          iov = {0,};

	list_for_each_entry_safe (trav, tmp, &saved_frames->sf.list, list) {
                frame_sent_tm = localtime (&trav->saved_at.tv_sec);
                strftime (timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S",
                          frame_sent_tm);
                snprintf (timestr + strlen (timestr),
                          sizeof(timestr) - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, trav->saved_at.tv_usec);

                if (!trav->rpcreq || !trav->rpcreq->prog)
                        continue;

                gf_log_callingfn ("rpc-clnt", GF_LOG_ERROR,
                                  "forced unwinding frame type(%s) op(%s(%d)) "
                                  "called at %s",
                                  trav->rpcreq->prog->progname,
                                  (trav->rpcreq->prog->procnames) ?
                                  trav->rpcreq->prog->procnames[trav->rpcreq->procnum]
                                  : "--",
                                  trav->rpcreq->procnum, timestr);
		saved_frames->count--;

                trav->rpcreq->rpc_status = -1;
                trav->rpcreq->cbkfn (trav->rpcreq, &iov, 1, trav->frame);

                saved_frames_pool
                        = trav->rpcreq->conn->rpc_clnt->saved_frames_pool;
                rpc_clnt_reply_deinit (trav->rpcreq,
                                       trav->rpcreq->conn->rpc_clnt->reqpool);

		list_del_init (&trav->list);
                mem_put (saved_frames_pool, trav);
	}
}


void
saved_frames_destroy (struct saved_frames *frames)
{
        if (!frames)
                return;

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
                        ret = rpc_transport_connect (trans, conn->config.remote_port);

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
                        "frame corresponding to xid (%d) for msg arrived on "
                        "transport %s",
                        info->xid, clnt->conn.trans->name);
                goto out;
        }

        info->prognum = saved_frame.rpcreq->prog->prognum;
        info->procnum = saved_frame.rpcreq->procnum;
        info->progver = saved_frame.rpcreq->prog->progver;
        info->rpc_req = saved_frame.rpcreq;
        info->rsp     = saved_frame.rsp;

        ret = 0;
out:
        return ret;
}

int
rpc_clnt_reconnect_cleanup (rpc_clnt_connection_t *conn)
{
        struct rpc_clnt         *clnt  = NULL;

        if (!conn) {
                goto out;
        }

        clnt = conn->rpc_clnt;

        pthread_mutex_lock (&conn->lock);
        {

                if (conn->reconnect) {
                        gf_timer_call_cancel (clnt->ctx, conn->reconnect);
                        conn->reconnect = NULL;
                }

        }
        pthread_mutex_unlock (&conn->lock);

out:
        return 0;
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
                     struct rpc_req *req,
                     struct saved_frame *saved_frame)
{
        int             ret   = -1;

        if ((!conn) || (!replymsg)|| (!req) || (!saved_frame) || (!msg)) {
                goto out;
        }

        req->rpc_status = 0;
        if ((rpc_reply_status (replymsg) == MSG_DENIED)
            || (rpc_accepted_reply_status (replymsg) != SUCCESS)) {
                req->rpc_status = -1;
        }

        req->rsp[0] = progmsg;
        req->rsp_iobref = iobref_ref (msg->iobref);

        if (msg->vectored) {
                req->rsp[1] = msg->vector[1];
                req->rspcnt = 2;
        } else {
                req->rspcnt = 1;
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
rpc_clnt_reply_deinit (struct rpc_req *req, struct mem_pool *pool)
{
        if (!req) {
                goto out;
        }

        if (req->rsp_iobref) {
                iobref_unref (req->rsp_iobref);
        }

        mem_put (pool, req);
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

        msgbuf = msg->vector[0].iov_base;
        msglen = msg->vector[0].iov_len;

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

        gf_log ("rpc-clnt", GF_LOG_TRACE, "recieved rpc message (RPC XID: 0x%ux"
                " Program: %s, ProgVers: %d, Proc: %d) from rpc-transport (%s)",
                saved_frame->rpcreq->xid,
                saved_frame->rpcreq->prog->progname,
                saved_frame->rpcreq->prog->progver,
                saved_frame->rpcreq->procnum, conn->trans->name);
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
rpc_clnt_handle_cbk (struct rpc_clnt *clnt, rpc_transport_pollin_t *msg)
{
        char                 *msgbuf = NULL;
        rpcclnt_cb_program_t *program = NULL;
        struct rpc_msg        rpcmsg;
        struct iovec          progmsg; /* RPC Program payload */
        size_t                msglen = 0;
        int                   found  = 0;
        int                   ret    = -1;
        int                   procnum = 0;

        msgbuf = msg->vector[0].iov_base;
        msglen = msg->vector[0].iov_len;

        clnt = rpc_clnt_ref (clnt);
        ret = xdr_to_rpc_call (msgbuf, msglen, &rpcmsg, &progmsg, NULL,NULL);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "RPC call decoding failed");
                goto out;
        }

        gf_log ("rpc-clnt", GF_LOG_INFO, "recieved rpc message (XID: 0x%lx, "
                "Ver: %ld, Program: %ld, ProgVers: %ld, Proc: %ld) "
                "from rpc-transport (%s)", rpc_call_xid (&rpcmsg),
                rpc_call_rpcvers (&rpcmsg), rpc_call_program (&rpcmsg),
                rpc_call_progver (&rpcmsg), rpc_call_progproc (&rpcmsg),
                clnt->conn.trans->name);

        procnum = rpc_call_progproc (&rpcmsg);

        pthread_mutex_lock (&clnt->lock);
        {
                list_for_each_entry (program, &clnt->programs, program) {
                        if ((program->prognum == rpc_call_program (&rpcmsg))
                            && (program->progver
                                == rpc_call_progver (&rpcmsg))) {
                                found = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&clnt->lock);

        if (found && (procnum < program->numactors) &&
            (program->actors[procnum].actor)) {
                program->actors[procnum].actor (&progmsg);
        }

out:
        clnt = rpc_clnt_unref (clnt);
        return ret;
}

int
rpc_clnt_handle_reply (struct rpc_clnt *clnt, rpc_transport_pollin_t *pollin)
{
        rpc_clnt_connection_t *conn         = NULL;
        struct saved_frame    *saved_frame  = NULL;
        int                    ret          = -1;
        struct rpc_req        *req          = NULL;
        uint32_t               xid          = 0;

        clnt = rpc_clnt_ref (clnt);
        conn = &clnt->conn;

        xid = ntoh32 (*((uint32_t *)pollin->vector[0].iov_base));
        saved_frame = lookup_frame (conn, xid);
        if (saved_frame == NULL) {
                gf_log ("rpc-clnt", GF_LOG_CRITICAL, "cannot lookup the "
                        "saved frame for reply with xid (%d)", xid);
                goto out;
        }

        req = saved_frame->rpcreq;
        if (req == NULL) {
                gf_log ("rpc-clnt", GF_LOG_CRITICAL,
                        "saved_frame for reply with xid (%d)", xid);
                goto out;
        }

        ret = rpc_clnt_reply_init (conn, pollin, req, saved_frame);
        if (ret != 0) {
                req->rpc_status = -1;
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "initialising rpc reply "
                        "failed");
        }

        req->cbkfn (req, req->rsp, req->rspcnt, saved_frame->frame);

        if (req) {
                rpc_clnt_reply_deinit (req, conn->rpc_clnt->reqpool);
        }
out:

        if (saved_frame) {
                mem_put (conn->rpc_clnt->saved_frames_pool, saved_frame);
        }

        clnt = rpc_clnt_unref (clnt);
        return ret;
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
        if (!clnt)
                goto out;

        switch (event) {
        case RPC_TRANSPORT_DISCONNECT:
        {
                rpc_clnt_connection_cleanup (conn);

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
                if (pollin->is_reply)
                        ret = rpc_clnt_handle_reply (clnt, pollin);
                else
                        ret = rpc_clnt_handle_cbk (clnt, pollin);
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
        struct rpc_clnt_connection *conn = NULL;

        rpc = GF_CALLOC (1, sizeof (*rpc), gf_common_mt_rpcclnt_t);
        if (!rpc) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                goto out;
        }

        pthread_mutex_init (&rpc->lock, NULL);
        rpc->ctx = ctx;

        rpc->reqpool = mem_pool_new (struct rpc_req,
                                     RPC_CLNT_DEFAULT_REQUEST_COUNT);
        if (rpc->reqpool == NULL) {
                pthread_mutex_destroy (&rpc->lock);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }

        rpc->saved_frames_pool = mem_pool_new (struct saved_frame,
                                              RPC_CLNT_DEFAULT_REQUEST_COUNT);
        if (rpc->saved_frames_pool == NULL) {
                pthread_mutex_destroy (&rpc->lock);
                mem_pool_destroy (rpc->reqpool);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }

        ret = rpc_clnt_connection_init (rpc, ctx, options, name);
        if (ret == -1) {
                pthread_mutex_destroy (&rpc->lock);
                mem_pool_destroy (rpc->reqpool);
                mem_pool_destroy (rpc->saved_frames_pool);
                GF_FREE (rpc);
                rpc = NULL;
                if (options)
                        dict_unref (options);
                goto out;
        }

        conn = &rpc->conn;
        rpc_clnt_reconnect (conn->trans);

        rpc = rpc_clnt_ref (rpc);
        INIT_LIST_HEAD (&rpc->programs);

out:
        return rpc;
}


struct rpc_clnt *
rpc_clnt_new (struct rpc_clnt_config *config, dict_t *options,
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

        rpc->reqpool = mem_pool_new (struct rpc_req,
                                     RPC_CLNT_DEFAULT_REQUEST_COUNT);
        if (rpc->reqpool == NULL) {
                pthread_mutex_destroy (&rpc->lock);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }

        rpc->saved_frames_pool = mem_pool_new (struct saved_frame,
                                              RPC_CLNT_DEFAULT_REQUEST_COUNT);
        if (rpc->saved_frames_pool == NULL) {
                pthread_mutex_destroy (&rpc->lock);
                mem_pool_destroy (rpc->reqpool);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }

        ret = rpc_clnt_connection_init (rpc, ctx, options, name);
        if (ret == -1) {
                pthread_mutex_destroy (&rpc->lock);
                mem_pool_destroy (rpc->reqpool);
                mem_pool_destroy (rpc->saved_frames_pool);
                GF_FREE (rpc);
                rpc = NULL;
                if (options)
                        dict_unref (options);
                goto out;
        }

        rpc = rpc_clnt_ref (rpc);
        INIT_LIST_HEAD (&rpc->programs);

out:
        return rpc;
}


int
rpc_clnt_start (struct rpc_clnt *rpc)
{
        struct rpc_clnt_connection *conn = NULL;

        if (!rpc)
                return -1;

        conn = &rpc->conn;

        rpc_clnt_reconnect (conn->trans);

        return 0;
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
                       struct rpc_msg *request, char *auth_data)
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

        /* TODO: Using AUTH_GLUSTERFS for time-being. Make it modular in
         * future so it is easy to plug-in new authentication schemes.
         */
        ret = xdr_serialize_glusterfs_auth (auth_data, au);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "cannot encode credentials");
                goto out;
        }

        request->rm_call.cb_cred.oa_flavor = AUTH_GLUSTERFS;
        request->rm_call.cb_cred.oa_base   = auth_data;
        request->rm_call.cb_cred.oa_length = ret;

        request->rm_call.cb_verf.oa_flavor = AUTH_NONE;
        request->rm_call.cb_verf.oa_base = NULL;
        request->rm_call.cb_verf.oa_length = 0;

        ret = 0;
out:
        return ret;
}


struct iovec
rpc_clnt_record_build_header (char *recordstart, size_t rlen,
                              struct rpc_msg *request, size_t payload)
{
        struct iovec    requesthdr = {0, };
        struct iovec    txrecord   = {0, 0};
        int             ret        = -1;
        size_t          fraglen    = 0;

        ret = rpc_request_to_xdr (request, recordstart, rlen, &requesthdr);
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG,
                        "Failed to create RPC request");
                goto out;
        }

        fraglen = payload + requesthdr.iov_len;
        gf_log ("rpc-clnt", GF_LOG_TRACE, "Request fraglen %zu, payload: %zu, "
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
rpc_clnt_record_build_record (struct rpc_clnt *clnt, int prognum, int progver,
                              int procnum, size_t payload, uint64_t xid,
                              struct auth_glusterfs_parms *au, struct iovec *recbuf)
{
        struct rpc_msg           request                            = {0, };
        struct iobuf            *request_iob                        = NULL;
        char                    *record                             = NULL;
        struct iovec             recordhdr                          = {0, };
        size_t                   pagesize                           = 0;
        int                      ret                                = -1;
        char                     auth_data[RPC_CLNT_MAX_AUTH_BYTES] = {0, };

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
                                     au, &request, auth_data);
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
rpcclnt_cbk_program_register (struct rpc_clnt *clnt,
                              rpcclnt_cb_program_t *program)
{
        int                   ret                = -1;
        char                  already_registered = 0;
        rpcclnt_cb_program_t *tmp                = NULL;

        if (!clnt)
                goto out;

        if (program->actors == NULL)
                goto out;

        pthread_mutex_lock (&clnt->lock);
        {
                list_for_each_entry (tmp, &clnt->programs, program) {
                        if ((program->prognum == tmp->prognum)
                            && (program->progver == tmp->progver)) {
                                already_registered = 1;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&clnt->lock);

        if (already_registered) {
                gf_log ("rpc-clnt", GF_LOG_DEBUG, "already registered");
                ret = 0;
                goto out;
        }

        tmp = GF_CALLOC (1, sizeof (*tmp),
                         gf_common_mt_rpcclnt_cb_program_t);
        if (tmp == NULL) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                goto out;
        }

        memcpy (tmp, program, sizeof (*tmp));
        INIT_LIST_HEAD (&tmp->program);

        pthread_mutex_lock (&clnt->lock);
        {
                list_add_tail (&tmp->program, &clnt->programs);
        }
        pthread_mutex_unlock (&clnt->lock);

        ret = 0;
        gf_log ("rpc-clnt", GF_LOG_DEBUG, "New program registered: %s, Num: %d,"
                " Ver: %d", program->progname, program->prognum,
                program->progver);

out:
        if (ret == -1) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "Program registration failed:"
                        " %s, Num: %d, Ver: %d", program->progname,
                        program->prognum, program->progver);
        }

        return ret;
}


int
rpc_clnt_submit (struct rpc_clnt *rpc, rpc_clnt_prog_t *prog,
                 int procnum, fop_cbk_fn_t cbkfn,
                 struct iovec *proghdr, int proghdrcount,
                 struct iovec *progpayload, int progpayloadcount,
                 struct iobref *iobref, void *frame, struct iovec *rsphdr,
                 int rsphdr_count, struct iovec *rsp_payload,
                 int rsp_payload_count, struct iobref *rsp_iobref)
{
        rpc_clnt_connection_t *conn        = NULL;
        struct iobuf          *request_iob = NULL;
        struct iovec           rpchdr      = {0,};
        struct rpc_req        *rpcreq      = NULL;
        rpc_transport_req_t    req;
        int                    ret         = -1;
        int                    proglen     = 0;
        char                   new_iobref  = 0;
        uint64_t               callid      = 0;

        if (!rpc || !prog || !frame) {
                goto out;
        }

        rpcreq = mem_get (rpc->reqpool);
        if (rpcreq == NULL) {
                gf_log ("rpc-clnt", GF_LOG_ERROR, "out of memory");
                goto out;
        }

        memset (rpcreq, 0, sizeof (*rpcreq));
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

        rpcreq->prog = prog;
        rpcreq->procnum = procnum;
        rpcreq->conn = conn;
        rpcreq->xid = callid;
        rpcreq->cbkfn = cbkfn;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->connected == 0) {
                        rpc_transport_connect (conn->trans,
                                               conn->config.remote_port);
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

                req.rsp.rsphdr = rsphdr;
                req.rsp.rsphdr_count = rsphdr_count;
                req.rsp.rsp_payload = rsp_payload;
                req.rsp.rsp_payload_count = rsp_payload_count;
                req.rsp.rsp_iobref = rsp_iobref;
                req.rpc_req = rpcreq;

                ret = rpc_transport_submit_request (rpc->conn.trans,
                                                    &req);
                if (ret == -1) {
                        gf_log ("rpc-clnt", GF_LOG_TRACE, "failed to "
                                "submit rpc-request "
                                "(XID: 0x%ux Program: %s, ProgVers: %d, "
                                "Proc: %d) to rpc-transport (%s)", rpcreq->xid,
                                rpcreq->prog->progname, rpcreq->prog->progver,
                                rpcreq->procnum, rpc->conn.trans->name);
                }

                if ((ret >= 0) && frame) {
                        gettimeofday (&conn->last_sent, NULL);
                        /* Save the frame in queue */
                        __save_frame (rpc, frame, rpcreq);

                        gf_log ("rpc-clnt", GF_LOG_TRACE, "submitted request "
                                "(XID: 0x%ux Program: %s, ProgVers: %d, "
                                "Proc: %d) to rpc-transport (%s)", rpcreq->xid,
                                rpcreq->prog->progname, rpcreq->prog->progver,
                                rpcreq->procnum, rpc->conn.trans->name);
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
                if (rpcreq) {
                        rpcreq->rpc_status = -1;
                        cbkfn (rpcreq, NULL, 0, frame);
                        mem_put (rpc->reqpool, rpcreq);
                }
        }
        return ret;
}


struct rpc_clnt *
rpc_clnt_ref (struct rpc_clnt *rpc)
{
        if (!rpc)
                return NULL;
        pthread_mutex_lock (&rpc->lock);
        {
                rpc->refcount++;
        }
        pthread_mutex_unlock (&rpc->lock);
        return rpc;
}


static void
rpc_clnt_destroy (struct rpc_clnt *rpc)
{
        if (!rpc)
                return;

        if (rpc->conn.trans) {
                rpc->conn.trans->mydata = NULL;
                rpc_transport_unref (rpc->conn.trans);
                //rpc_transport_destroy (rpc->conn.trans);
        }

        rpc_clnt_connection_cleanup (&rpc->conn);
        rpc_clnt_reconnect_cleanup (&rpc->conn);
        saved_frames_destroy (rpc->conn.saved_frames);
        pthread_mutex_destroy (&rpc->lock);
        pthread_mutex_destroy (&rpc->conn.lock);

        /* mem-pool should be destroyed, otherwise,
           it will cause huge memory leaks */
        mem_pool_destroy (rpc->reqpool);
        mem_pool_destroy (rpc->saved_frames_pool);

        GF_FREE (rpc);
        return;
}

struct rpc_clnt *
rpc_clnt_unref (struct rpc_clnt *rpc)
{
        int     count = 0;

        if (!rpc)
                return NULL;
        pthread_mutex_lock (&rpc->lock);
        {
                count = --rpc->refcount;
        }
        pthread_mutex_unlock (&rpc->lock);
        if (!count) {
                rpc_clnt_destroy (rpc);
                return NULL;
        }
        return rpc;
}


void
rpc_clnt_reconfig (struct rpc_clnt *rpc, struct rpc_clnt_config *config)
{
        if (config->rpc_timeout)
                rpc->conn.config.rpc_timeout = config->rpc_timeout;

        if (config->remote_port)
                rpc->conn.config.remote_port = config->remote_port;

        if (config->remote_host) {
                if (rpc->conn.config.remote_host)
                        FREE (rpc->conn.config.remote_host);
                rpc->conn.config.remote_host = gf_strdup (config->remote_host);
        }
}
