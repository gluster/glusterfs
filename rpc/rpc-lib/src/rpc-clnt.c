/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#define RPC_CLNT_DEFAULT_REQUEST_COUNT 512

#include "rpc-clnt.h"
#include "rpc-clnt-ping.h"
#include "byte-order.h"
#include "xdr-rpcclnt.h"
#include "rpc-transport.h"
#include "protocol-common.h"
#include "mem-pool.h"
#include "xdr-rpc.h"
#include "rpc-common-xdr.h"

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

static int
_is_lock_fop (struct saved_frame *sframe)
{
        int     fop     = 0;

        if (SFRAME_GET_PROGNUM (sframe) == GLUSTER_FOP_PROGRAM &&
            SFRAME_GET_PROGVER (sframe) == GLUSTER_FOP_VERSION)
                fop = SFRAME_GET_PROCNUM (sframe);

        return ((fop == GFS3_OP_LK) ||
                (fop == GFS3_OP_INODELK) ||
                (fop == GFS3_OP_FINODELK) ||
                (fop == GFS3_OP_ENTRYLK) ||
                (fop == GFS3_OP_FENTRYLK));
}

struct saved_frame *
__saved_frames_put (struct saved_frames *frames, void *frame,
                    struct rpc_req *rpcreq)
{
	struct saved_frame *saved_frame = NULL;

        saved_frame = mem_get (rpcreq->conn->rpc_clnt->saved_frames_pool);
	if (!saved_frame) {
                goto out;
	}
        /* THIS should be saved and set back */

        memset (saved_frame, 0, sizeof (*saved_frame));
	INIT_LIST_HEAD (&saved_frame->list);

	saved_frame->capital_this = THIS;
	saved_frame->frame        = frame;
        saved_frame->rpcreq       = rpcreq;
	gettimeofday (&saved_frame->saved_at, NULL);

        if (_is_lock_fop (saved_frame))
                list_add_tail (&saved_frame->list, &frames->lk_sf.list);
        else
                list_add_tail (&saved_frame->list, &frames->sf.list);

	frames->count++;

out:
	return saved_frame;
}



static void
call_bail (void *data)
{
        rpc_transport_t       *trans = NULL;
        struct rpc_clnt       *clnt = NULL;
        rpc_clnt_connection_t *conn = NULL;
        struct timeval         current;
        struct list_head       list;
        struct saved_frame    *saved_frame = NULL;
        struct saved_frame    *trav = NULL;
        struct saved_frame    *tmp = NULL;
        char                   frame_sent[256] = {0,};
        struct timespec        timeout = {0,};
        struct iovec           iov = {0,};
        char                   peerid[UNIX_PATH_MAX] = {0};
        gf_boolean_t           need_unref = _gf_false;

        GF_VALIDATE_OR_GOTO ("client", data, out);

        clnt = data;

        conn = &clnt->conn;
        pthread_mutex_lock (&conn->lock);
        {
            trans = conn->trans;
            if (trans) {
                    strncpy (peerid, conn->trans->peerinfo.identifier,
                             sizeof (peerid)-1);

            }
        }
        pthread_mutex_unlock (&conn->lock);
        /*rpc_clnt_connection_cleanup will be unwinding all saved frames,
         * bailed or otherwise*/
        if (!trans)
                goto out;

        gettimeofday (&current, NULL);
        INIT_LIST_HEAD (&list);

        pthread_mutex_lock (&conn->lock);
        {
                /* Chaining to get call-always functionality from
                   call-once timer */
                if (conn->timer) {
                        timeout.tv_sec = 10;
                        timeout.tv_nsec = 0;

                        /* Ref rpc as it's added to timer event queue */
                        rpc_clnt_ref (clnt);
                        gf_timer_call_cancel (clnt->ctx, conn->timer);
                        conn->timer = gf_timer_call_after (clnt->ctx,
                                                           timeout,
                                                           call_bail,
                                                           (void *) clnt);

                        if (conn->timer == NULL) {
                                gf_log (conn->name, GF_LOG_WARNING,
                                        "Cannot create bailout timer for %s",
                                        peerid);
                                need_unref = _gf_true;
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
                gf_time_fmt (frame_sent, sizeof frame_sent,
                             trav->saved_at.tv_sec, gf_timefmt_FT);
                snprintf (frame_sent + strlen (frame_sent),
                          256 - strlen (frame_sent),
                          ".%"GF_PRI_SUSECONDS, trav->saved_at.tv_usec);

		gf_log (conn->name, GF_LOG_ERROR,
			"bailing out frame type(%s) op(%s(%d)) xid = 0x%x "
                        "sent = %s. timeout = %d for %s",
			trav->rpcreq->prog->progname,
                        (trav->rpcreq->prog->procnames) ?
                        trav->rpcreq->prog->procnames[trav->rpcreq->procnum] :
                        "--",
                        trav->rpcreq->procnum, trav->rpcreq->xid, frame_sent,
                        conn->frame_timeout, peerid);

                clnt = rpc_clnt_ref (clnt);
                trav->rpcreq->rpc_status = -1;
		trav->rpcreq->cbkfn (trav->rpcreq, &iov, 1, trav->frame);

                rpc_clnt_reply_deinit (trav->rpcreq, clnt->reqpool);
                clnt = rpc_clnt_unref (clnt);
                list_del_init (&trav->list);
                mem_put (trav);
        }
out:
        rpc_clnt_unref (clnt);
        if (need_unref)
                rpc_clnt_unref (clnt);
        return;
}


/* to be called with conn->lock held */
struct saved_frame *
__save_frame (struct rpc_clnt *rpc_clnt, call_frame_t *frame,
              struct rpc_req *rpcreq)
{
        rpc_clnt_connection_t *conn        = NULL;
        struct timespec        timeout     = {0, };
        struct saved_frame    *saved_frame = NULL;

        conn = &rpc_clnt->conn;

        saved_frame = __saved_frames_put (conn->saved_frames, frame, rpcreq);

        if (saved_frame == NULL) {
                goto out;
        }

        /* TODO: make timeout configurable */
        if (conn->timer == NULL) {
                timeout.tv_sec  = 10;
                timeout.tv_nsec = 0;
                rpc_clnt_ref (rpc_clnt);
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
		return NULL;
	}

	INIT_LIST_HEAD (&saved_frames->sf.list);
	INIT_LIST_HEAD (&saved_frames->lk_sf.list);

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
			goto out;
		}
	}

	list_for_each_entry (tmp, &frames->lk_sf.list, list) {
		if (tmp->rpcreq->xid == callid) {
			*saved_frame = *tmp;
                        ret = 0;
			goto out;
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
			goto out;
		}
	}

	list_for_each_entry (tmp, &frames->lk_sf.list, list) {
		if (tmp->rpcreq->xid == callid) {
			list_del_init (&tmp->list);
			frames->count--;
			saved_frame = tmp;
			goto out;
		}
	}

out:
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
        char                  timestr[1024] = {0,};
        struct iovec          iov = {0,};

        list_splice_init (&saved_frames->lk_sf.list, &saved_frames->sf.list);

	list_for_each_entry_safe (trav, tmp, &saved_frames->sf.list, list) {
                gf_time_fmt (timestr, sizeof timestr,
                             trav->saved_at.tv_sec, gf_timefmt_FT);
                snprintf (timestr + strlen (timestr),
                          sizeof(timestr) - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, trav->saved_at.tv_usec);

                if (!trav->rpcreq || !trav->rpcreq->prog)
                        continue;

                gf_log_callingfn (trav->rpcreq->conn->name,
                                  GF_LOG_ERROR,
                                  "forced unwinding frame type(%s) op(%s(%d)) "
                                  "called at %s (xid=0x%x)",
                                  trav->rpcreq->prog->progname,
                                  ((trav->rpcreq->prog->procnames) ?
                                   trav->rpcreq->prog->procnames[trav->rpcreq->procnum]
                                   : "--"),
                                  trav->rpcreq->procnum, timestr,
                                  trav->rpcreq->xid);
		saved_frames->count--;

                trav->rpcreq->rpc_status = -1;
                trav->rpcreq->cbkfn (trav->rpcreq, &iov, 1, trav->frame);

                rpc_clnt_reply_deinit (trav->rpcreq,
                                       trav->rpcreq->conn->rpc_clnt->reqpool);

		list_del_init (&trav->list);
                mem_put (trav);
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
rpc_clnt_reconnect (void *conn_ptr)
{
        rpc_transport_t         *trans = NULL;
        rpc_clnt_connection_t   *conn  = NULL;
        struct timespec          ts    = {0, 0};
        struct rpc_clnt         *clnt  = NULL;
        gf_boolean_t             need_unref = _gf_false;

        conn  = conn_ptr;
        clnt = conn->rpc_clnt;

        pthread_mutex_lock (&conn->lock);
        {
                trans = conn->trans;
                if (!trans) {
                        pthread_mutex_unlock (&conn->lock);
                        return;
                }
                if (conn->reconnect)
                        gf_timer_call_cancel (clnt->ctx,
                                              conn->reconnect);
                conn->reconnect = 0;

                if ((conn->connected == 0) && !clnt->disabled) {
                        ts.tv_sec = 3;
                        ts.tv_nsec = 0;

                        gf_log (conn->name, GF_LOG_TRACE,
                                "attempting reconnect");
                        (void) rpc_transport_connect (trans,
                                                      conn->config.remote_port);
                        rpc_clnt_ref (clnt);
                        conn->reconnect =
                                gf_timer_call_after (clnt->ctx, ts,
                                                     rpc_clnt_reconnect,
                                                     conn);
                        if (!conn->reconnect) {
                                need_unref = _gf_true;
                                gf_log (conn->name, GF_LOG_ERROR,
                                        "Error adding to timer event queue");
                        }
                } else {
                        gf_log (conn->name, GF_LOG_TRACE,
                                "breaking reconnect chain");
                }
        }
        pthread_mutex_unlock (&conn->lock);

        rpc_clnt_unref (clnt);
        if (need_unref)
                rpc_clnt_unref (clnt);
        return;
}


int
rpc_clnt_fill_request_info (struct rpc_clnt *clnt, rpc_request_info_t *info)
{
        struct saved_frame  saved_frame;
        int                 ret         = -1;

        pthread_mutex_lock (&clnt->conn.lock);
        {
                ret = __saved_frame_copy (clnt->conn.saved_frames, info->xid,
                                          &saved_frame);
        }
        pthread_mutex_unlock (&clnt->conn.lock);

        if (ret == -1) {
                gf_log (clnt->conn.name, GF_LOG_CRITICAL,
                        "cannot lookup the saved "
                        "frame corresponding to xid (%d)", info->xid);
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
        int                      ret   = 0;
        gf_boolean_t             reconnect_unref = _gf_false;

        if (!conn) {
                goto out;
        }

        clnt = conn->rpc_clnt;

        pthread_mutex_lock (&conn->lock);
        {

                if (conn->reconnect) {
                        ret = gf_timer_call_cancel (clnt->ctx, conn->reconnect);
                        if (!ret)
                                reconnect_unref = _gf_true;
                        conn->reconnect = NULL;
                }

        }
        pthread_mutex_unlock (&conn->lock);

        if (reconnect_unref)
                rpc_clnt_unref (clnt);

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
        int                     unref = 0;
        int                     ret   = 0;
        gf_boolean_t            timer_unref = _gf_false;

        if (!conn) {
                goto out;
        }

        clnt = conn->rpc_clnt;

        pthread_mutex_lock (&conn->lock);
        {

                saved_frames = conn->saved_frames;
                conn->saved_frames = saved_frames_new ();

                /* bailout logic cleanup */
                if (conn->timer) {
                        ret = gf_timer_call_cancel (clnt->ctx, conn->timer);
                        if (!ret)
                                timer_unref = _gf_true;
                        conn->timer = NULL;
                }

                conn->connected = 0;

                unref = rpc_clnt_remove_ping_timer_locked (clnt);
                /*reset rpc msgs stats*/
                conn->pingcnt = 0;
                conn->msgcnt = 0;
        }
        pthread_mutex_unlock (&conn->lock);

        saved_frames_destroy (saved_frames);
        if (unref)
                rpc_clnt_unref (clnt);

        if (timer_unref)
                rpc_clnt_unref (clnt);

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

        mem_put (req);
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
                gf_log (conn->name, GF_LOG_WARNING,
                        "RPC reply decoding failed");
                goto out;
        }

        ret = rpc_clnt_reply_fill (msg, conn, &rpcmsg, progmsg, req,
                                   saved_frame);
        if (ret != 0) {
                goto out;
        }

        gf_log (conn->name, GF_LOG_TRACE,
                "received rpc message (RPC XID: 0x%x"
                " Program: %s, ProgVers: %d, Proc: %d) from rpc-transport (%s)",
                saved_frame->rpcreq->xid,
                saved_frame->rpcreq->prog->progname,
                saved_frame->rpcreq->prog->progver,
                saved_frame->rpcreq->procnum, conn->name);

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
                gf_log (clnt->conn.name, GF_LOG_WARNING,
                        "RPC call decoding failed");
                goto out;
        }

        gf_log (clnt->conn.name, GF_LOG_TRACE,
		"receivd rpc message (XID: 0x%" GF_PRI_RPC_XID ", "
		"Ver: %" GF_PRI_RPC_VERSION ", Program: %" GF_PRI_RPC_PROG_ID ", "
		"ProgVers: %" GF_PRI_RPC_PROG_VERS ", Proc: %" GF_PRI_RPC_PROC ") "
                "from rpc-transport (%s)", rpc_call_xid (&rpcmsg),
                rpc_call_rpcvers (&rpcmsg), rpc_call_program (&rpcmsg),
                rpc_call_progver (&rpcmsg), rpc_call_progproc (&rpcmsg),
                clnt->conn.name);

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
                program->actors[procnum].actor (clnt, program->mydata,
                                                &progmsg);
        }

out:
        rpc_clnt_unref (clnt);
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
                gf_log (conn->name, GF_LOG_ERROR,
                        "cannot lookup the saved frame for reply with xid (%u)",
                        xid);
                goto out;
        }

        req = saved_frame->rpcreq;
        if (req == NULL) {
                gf_log (conn->name, GF_LOG_ERROR,
                        "no request with frame for xid (%u)", xid);
                goto out;
        }

        ret = rpc_clnt_reply_init (conn, pollin, req, saved_frame);
        if (ret != 0) {
                req->rpc_status = -1;
                gf_log (conn->name, GF_LOG_WARNING,
                        "initialising rpc reply failed");
        }

        req->cbkfn (req, req->rsp, req->rspcnt, saved_frame->frame);

        if (req) {
                rpc_clnt_reply_deinit (req, conn->rpc_clnt->reqpool);
        }
out:

        if (saved_frame) {
                mem_put (saved_frame);
        }

        rpc_clnt_unref (clnt);
        return ret;
}


void
rpc_clnt_set_connected (rpc_clnt_connection_t *conn)
{
        if (!conn) {
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                conn->connected = 1;
                conn->disconnected = _gf_false;
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
                conn->disconnected = _gf_true;
        }
        pthread_mutex_unlock (&conn->lock);

out:
        return;
}

gf_boolean_t
is_rpc_clnt_disconnected (rpc_clnt_connection_t *conn)
{
        gf_boolean_t disconnected = _gf_true;

        if (!conn)
                return disconnected;

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->disconnected == _gf_false)
                        disconnected = _gf_false;
        }
        pthread_mutex_unlock (&conn->lock);

        return disconnected;
}

static void
rpc_clnt_destroy (struct rpc_clnt *rpc);

#define RPC_THIS_SAVE(xl) do {                                  \
        old_THIS = THIS ;                                       \
        if (!old_THIS)                                          \
                gf_log_callingfn ("rpc", GF_LOG_CRITICAL,       \
                                  "THIS is not initialised.");  \
        THIS = xl;                                              \
} while (0)

#define RPC_THIS_RESTORE        (THIS = old_THIS)

static int
rpc_clnt_handle_disconnect (struct rpc_clnt *clnt, rpc_clnt_connection_t *conn)
{
        struct timespec         ts          = {0, };
        gf_boolean_t            unref_clnt  = _gf_false;

        rpc_clnt_connection_cleanup (conn);

        pthread_mutex_lock (&conn->lock);
        {
                if (!conn->rpc_clnt->disabled && (conn->reconnect == NULL)) {
                        ts.tv_sec = 10;
                        ts.tv_nsec = 0;

                        rpc_clnt_ref (clnt);
                        conn->reconnect = gf_timer_call_after (clnt->ctx, ts,
                                                rpc_clnt_reconnect, conn);
                        if (conn->reconnect == NULL) {
                                gf_log (conn->name, GF_LOG_WARNING,
                                                "Cannot create rpc_clnt_reconnect timer");
                                unref_clnt = _gf_true;
                        }
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (clnt->notifyfn)
                clnt->notifyfn (clnt, clnt->mydata, RPC_CLNT_DISCONNECT, NULL);

        if (unref_clnt)
                rpc_clnt_ref (clnt);

        return 0;
}

int
rpc_clnt_notify (rpc_transport_t *trans, void *mydata,
                 rpc_transport_event_t event, void *data, ...)
{
        rpc_clnt_connection_t  *conn        = NULL;
        struct rpc_clnt        *clnt        = NULL;
        int                     ret         = -1;
        rpc_request_info_t     *req_info    = NULL;
        rpc_transport_pollin_t *pollin      = NULL;
        void                   *clnt_mydata = NULL;
        DECLARE_OLD_THIS;

        conn = mydata;
        if (conn == NULL) {
                goto out;
        }
        clnt = conn->rpc_clnt;
        if (!clnt)
                goto out;

        RPC_THIS_SAVE (clnt->owner);

        switch (event) {
        case RPC_TRANSPORT_DISCONNECT:
        {
                pthread_mutex_lock (&clnt->notifylock);
                {
                        rpc_clnt_handle_disconnect (clnt, conn);
                }
                pthread_mutex_unlock (&clnt->notifylock);
                break;
        }

        case RPC_TRANSPORT_CLEANUP:
                if (clnt->notifyfn) {
                        clnt_mydata = clnt->mydata;
                        clnt->mydata = NULL;
                        ret = clnt->notifyfn (clnt, clnt_mydata,
                                              RPC_CLNT_DESTROY, NULL);
                }
                rpc_clnt_destroy (clnt);
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
                pthread_mutex_lock (&conn->lock);
                {
                        gettimeofday (&conn->last_received, NULL);
                }
                pthread_mutex_unlock (&conn->lock);

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
                pthread_mutex_lock (&clnt->notifylock);
                {
                        /* Every time there is a disconnection, processes
                         * should try to connect to 'glusterd' (ie, default
                         * port) or whichever port given as 'option remote-port'
                         * in volume file. */
                        /* Below code makes sure the (re-)configured port lasts
                         * for just one successful attempt */
                        conn->config.remote_port = 0;

                        if (clnt->notifyfn)
                                ret = clnt->notifyfn (clnt, clnt->mydata,
                                                RPC_CLNT_CONNECT, NULL);
                }
                pthread_mutex_unlock (&clnt->notifylock);
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
        RPC_THIS_RESTORE;
        return ret;
}

static int
rpc_clnt_connection_init (struct rpc_clnt *clnt, glusterfs_ctx_t *ctx,
                          dict_t *options, char *name)
{
        int                    ret  = -1;
        rpc_clnt_connection_t *conn = NULL;
        rpc_transport_t       *trans = NULL;

        conn = &clnt->conn;
        pthread_mutex_init (&clnt->conn.lock, NULL);

        conn->name = gf_strdup (name);
        if (!conn->name) {
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (options, "frame-timeout",
                              &conn->frame_timeout);
        if (ret >= 0) {
                gf_log (name, GF_LOG_INFO,
                        "setting frame-timeout to %d", conn->frame_timeout);
        } else {
                gf_log (name, GF_LOG_DEBUG,
                        "defaulting frame-timeout to 30mins");
                conn->frame_timeout = 1800;
        }
        conn->rpc_clnt = clnt;

        ret = dict_get_int32 (options, "ping-timeout",
                              &conn->ping_timeout);
        if (ret >= 0) {
                gf_log (name, GF_LOG_DEBUG,
                        "setting ping-timeout to %d", conn->ping_timeout);
        } else {
                /*TODO: Once the epoll thread model is fixed,
                  change the default ping-timeout to 30sec */
                gf_log (name, GF_LOG_DEBUG,
                        "disable ping-timeout");
                conn->ping_timeout = 0;
        }

        trans = rpc_transport_load (ctx, options, name);
        if (!trans) {
                gf_log (name, GF_LOG_WARNING, "loading of new rpc-transport"
                        " failed");
                ret = -1;
                goto out;
        }
        rpc_transport_ref (trans);

        pthread_mutex_lock (&conn->lock);
        {
                conn->trans = trans;
                trans = NULL;
        }
        pthread_mutex_unlock (&conn->lock);

        ret = rpc_transport_register_notify (conn->trans, rpc_clnt_notify,
                                             conn);
        if (ret == -1) {
                gf_log (name, GF_LOG_WARNING, "registering notify failed");
                goto out;
        }

        conn->saved_frames = saved_frames_new ();
        if (!conn->saved_frames) {
                gf_log (name, GF_LOG_WARNING, "creation of saved_frames "
                        "failed");
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        if (ret) {
                pthread_mutex_lock (&conn->lock);
                {
                        trans = conn->trans;
                        conn->trans = NULL;
                }
                pthread_mutex_unlock (&conn->lock);
                if (trans)
                        rpc_transport_unref (trans);
                //conn cleanup needs to be done since we might have failed to
                // register notification.
                rpc_clnt_connection_cleanup (conn);
        }
        return ret;
}

struct rpc_clnt *
rpc_clnt_new (dict_t *options, xlator_t *owner, char *name,
              uint32_t reqpool_size)
{
        int                    ret  = -1;
        struct rpc_clnt       *rpc  = NULL;
        glusterfs_ctx_t       *ctx  = owner->ctx;


        rpc = GF_CALLOC (1, sizeof (*rpc), gf_common_mt_rpcclnt_t);
        if (!rpc) {
                goto out;
        }

        pthread_mutex_init (&rpc->lock, NULL);
        pthread_mutex_init (&rpc->notifylock, NULL);
        rpc->ctx = ctx;
        rpc->owner = owner;

        if (!reqpool_size)
                reqpool_size = RPC_CLNT_DEFAULT_REQUEST_COUNT;

        rpc->reqpool = mem_pool_new (struct rpc_req, reqpool_size);
        if (rpc->reqpool == NULL) {
                pthread_mutex_destroy (&rpc->lock);
                pthread_mutex_destroy (&rpc->notifylock);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }

        rpc->saved_frames_pool = mem_pool_new (struct saved_frame,
                                               reqpool_size);
        if (rpc->saved_frames_pool == NULL) {
                pthread_mutex_destroy (&rpc->lock);
                pthread_mutex_destroy (&rpc->notifylock);
                mem_pool_destroy (rpc->reqpool);
                GF_FREE (rpc);
                rpc = NULL;
                goto out;
        }

        ret = rpc_clnt_connection_init (rpc, ctx, options, name);
        if (ret == -1) {
                pthread_mutex_destroy (&rpc->lock);
                pthread_mutex_destroy (&rpc->notifylock);
                mem_pool_destroy (rpc->reqpool);
                mem_pool_destroy (rpc->saved_frames_pool);
                GF_FREE (rpc);
                rpc = NULL;
                if (options)
                        dict_unref (options);
                goto out;
        }

        rpc->auth_null = dict_get_str_boolean (options, "auth-null", 0);

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

        pthread_mutex_lock (&conn->lock);
        {
                rpc->disabled = 0;
        }
        pthread_mutex_unlock (&conn->lock);
        /* Corresponding unref will be either on successful timer cancel or last
         * rpc_clnt_reconnect fire event.
         */
        rpc_clnt_ref (rpc);
        rpc_clnt_reconnect (conn);

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

/* used for GF_LOG_OCCASIONALLY() */
static int gf_auth_max_groups_log = 0;

ssize_t
xdr_serialize_glusterfs_auth (char *dest, struct auth_glusterfs_parms_v2 *au)
{
        ssize_t ret = -1;
        XDR     xdr;
        u_long  ngroups = 0;
        int     max_groups = 0;

        if ((!dest) || (!au))
                return -1;

        max_groups = GF_AUTH_GLUSTERFS_MAX_GROUPS (au->lk_owner.lk_owner_len);

        xdrmem_create (&xdr, dest, GF_MAX_AUTH_BYTES, XDR_ENCODE);

        if (au->groups.groups_len > max_groups) {
                ngroups = au->groups.groups_len;
                au->groups.groups_len = max_groups;

                GF_LOG_OCCASIONALLY (gf_auth_max_groups_log,
                                     THIS->name, GF_LOG_WARNING,
                                     "too many groups, reducing %ld -> %d",
                                     ngroups, max_groups);
        }

        if (!xdr_auth_glusterfs_parms_v2 (&xdr, au)) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to encode auth glusterfs elements");
                ret = -1;
                goto ret;
        }

        ret = (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base));

ret:
        if (ngroups)
                au->groups.groups_len = ngroups;

        return ret;
}


int
rpc_clnt_fill_request (int prognum, int progver, int procnum,
                       uint64_t xid, struct auth_glusterfs_parms_v2 *au,
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

        /* TODO: Using AUTH_(GLUSTERFS/NULL) in a kludgy way for time-being.
         * Make it modular in future so it is easy to plug-in new
         * authentication schemes.
         */
        if (auth_data) {
                ret = xdr_serialize_glusterfs_auth (auth_data, au);
                if (ret == -1) {
                        gf_log ("rpc-clnt", GF_LOG_DEBUG,
                                "cannot encode credentials");
                        goto out;
                }

                request->rm_call.cb_cred.oa_flavor = AUTH_GLUSTERFS_v2;
                request->rm_call.cb_cred.oa_base   = auth_data;
                request->rm_call.cb_cred.oa_length = ret;
        } else {
                request->rm_call.cb_cred.oa_flavor = AUTH_NULL;
                request->rm_call.cb_cred.oa_base   = NULL;
                request->rm_call.cb_cred.oa_length = 0;
        }
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
                              int procnum, size_t hdrsize, uint64_t xid,
                              struct auth_glusterfs_parms_v2 *au,
                              struct iovec *recbuf)
{
        struct rpc_msg  request                      = {0, };
        struct iobuf   *request_iob                  = NULL;
        char           *record                       = NULL;
        struct iovec    recordhdr                    = {0, };
        size_t          pagesize                     = 0;
        int             ret                          = -1;
        size_t          xdr_size                     = 0;
        char            auth_data[GF_MAX_AUTH_BYTES] = {0, };

        if ((!clnt) || (!recbuf) || (!au)) {
                goto out;
        }

        /* Fill the rpc structure and XDR it into the buffer got above. */
        if (clnt->auth_null)
                ret = rpc_clnt_fill_request (prognum, progver, procnum,
                                             xid, NULL, &request, NULL);
        else
                ret = rpc_clnt_fill_request (prognum, progver, procnum,
                                             xid, au, &request, auth_data);

        if (ret == -1) {
                gf_log (clnt->conn.name, GF_LOG_WARNING,
                        "cannot build a rpc-request xid (%"PRIu64")", xid);
                goto out;
        }

        xdr_size = xdr_sizeof ((xdrproc_t)xdr_callmsg, &request);

        /* First, try to get a pointer into the buffer which the RPC
         * layer can use.
         */
        request_iob = iobuf_get2 (clnt->ctx->iobuf_pool, (xdr_size + hdrsize));
        if (!request_iob) {
                goto out;
        }

        pagesize = iobuf_pagesize (request_iob);

        record = iobuf_ptr (request_iob);  /* Now we have it. */

        recordhdr = rpc_clnt_record_build_header (record, pagesize, &request,
                                                  hdrsize);

        if (!recordhdr.iov_base) {
                gf_log (clnt->conn.name, GF_LOG_ERROR,
                        "Failed to build record header");
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
                 rpc_clnt_prog_t *prog, int procnum, size_t hdrlen,
                 struct iovec *rpchdr, uint64_t callid)
{
        struct auth_glusterfs_parms_v2  au          = {0, };
        struct iobuf                   *request_iob = NULL;
        char                            owner[4] = {0,};
        int                             max_groups = 0;
        int                             max_lkowner_len = 0;

        if (!prog || !rpchdr || !call_frame) {
                goto out;
        }

        au.pid                   = call_frame->root->pid;
        au.uid                   = call_frame->root->uid;
        au.gid                   = call_frame->root->gid;
        au.groups.groups_len     = call_frame->root->ngrps;
        au.lk_owner.lk_owner_len = call_frame->root->lk_owner.len;

        if (au.groups.groups_len)
                au.groups.groups_val = call_frame->root->groups;

        if (call_frame->root->lk_owner.len)
                au.lk_owner.lk_owner_val = call_frame->root->lk_owner.data;
        else {
                owner[0] = (char)(au.pid & 0xff);
                owner[1] = (char)((au.pid >> 8) & 0xff);
                owner[2] = (char)((au.pid >> 16) & 0xff);
                owner[3] = (char)((au.pid >> 24) & 0xff);

                au.lk_owner.lk_owner_val = owner;
                au.lk_owner.lk_owner_len = 4;
        }

        /* The number of groups and the size of lk_owner depend on oneother.
         * We can truncate the groups, but should not touch the lk_owner. */
        max_groups = GF_AUTH_GLUSTERFS_MAX_GROUPS (au.lk_owner.lk_owner_len);
        if (au.groups.groups_len > max_groups) {
                GF_LOG_OCCASIONALLY (gf_auth_max_groups_log, clnt->conn.name,
                                     GF_LOG_WARNING, "truncating grouplist "
                                     "from %d to %d", au.groups.groups_len,
                                     max_groups);

                au.groups.groups_len = max_groups;
        }

        max_lkowner_len = GF_AUTH_GLUSTERFS_MAX_LKOWNER (au.groups.groups_len);
        if (au.lk_owner.lk_owner_len > max_lkowner_len) {
                gf_log (clnt->conn.name, GF_LOG_ERROR, "lkowner field is too "
                        "big (%d), it does not fit in the rpc-header",
                        au.lk_owner.lk_owner_len);
                errno = E2BIG;
                goto out;
        }

        gf_log (clnt->conn.name, GF_LOG_TRACE, "Auth Info: pid: %u, uid: %d"
                ", gid: %d, owner: %s", au.pid, au.uid, au.gid,
                lkowner_utoa (&call_frame->root->lk_owner));

        request_iob = rpc_clnt_record_build_record (clnt, prog->prognum,
                                                    prog->progver,
                                                    procnum, hdrlen,
                                                    callid, &au,
                                                    rpchdr);
        if (!request_iob) {
                gf_log (clnt->conn.name, GF_LOG_WARNING,
                        "cannot build rpc-record");
                goto out;
        }

out:
        return request_iob;
}

int
rpcclnt_cbk_program_register (struct rpc_clnt *clnt,
                              rpcclnt_cb_program_t *program, void *mydata)
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
                gf_log_callingfn (clnt->conn.name, GF_LOG_DEBUG,
                                  "already registered");
                ret = 0;
                goto out;
        }

        tmp = GF_CALLOC (1, sizeof (*tmp),
                         gf_common_mt_rpcclnt_cb_program_t);
        if (tmp == NULL) {
                goto out;
        }

        memcpy (tmp, program, sizeof (*tmp));
        INIT_LIST_HEAD (&tmp->program);

        tmp->mydata = mydata;

        pthread_mutex_lock (&clnt->lock);
        {
                list_add_tail (&tmp->program, &clnt->programs);
        }
        pthread_mutex_unlock (&clnt->lock);

        ret = 0;
        gf_log (clnt->conn.name, GF_LOG_DEBUG,
                "New program registered: %s, Num: %d, Ver: %d",
                program->progname, program->prognum,
                program->progver);

out:
        if (ret == -1 && clnt) {
                        gf_log (clnt->conn.name, GF_LOG_ERROR,
                                        "Program registration failed:"
                                        " %s, Num: %d, Ver: %d",
                                        program->progname,
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
        gf_boolean_t           need_unref  = _gf_false;

        if (!rpc || !prog || !frame) {
                goto out;
        }

        conn = &rpc->conn;

        rpcreq = mem_get (rpc->reqpool);
        if (rpcreq == NULL) {
                goto out;
        }

        memset (rpcreq, 0, sizeof (*rpcreq));
        memset (&req, 0, sizeof (req));

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto out;
                }

                new_iobref = 1;
        }

        callid = rpc_clnt_new_callid (rpc);

        rpcreq->prog = prog;
        rpcreq->procnum = procnum;
        rpcreq->conn = conn;
        rpcreq->xid = callid;
        rpcreq->cbkfn = cbkfn;

        ret = -1;

        if (proghdr) {
                proglen += iov_length (proghdr, proghdrcount);
        }

        request_iob = rpc_clnt_record (rpc, frame, prog,
                                       procnum, proglen,
                                       &rpchdr, callid);
        if (!request_iob) {
                gf_log (conn->name, GF_LOG_WARNING,
                        "cannot build rpc-record");
                goto out;
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

        pthread_mutex_lock (&conn->lock);
        {
                if (conn->connected == 0 && !rpc->disabled) {
                        ret = rpc_transport_connect (conn->trans,
                                                     conn->config.remote_port);
                }

                ret = rpc_transport_submit_request (conn->trans, &req);
                if (ret == -1) {
                        gf_log (conn->name, GF_LOG_WARNING,
                                "failed to submit rpc-request "
                                "(XID: 0x%x Program: %s, ProgVers: %d, "
                                "Proc: %d) to rpc-transport (%s)", rpcreq->xid,
                                rpcreq->prog->progname, rpcreq->prog->progver,
                                rpcreq->procnum, conn->name);
                }

                if ((ret >= 0) && frame) {
                        /* Save the frame in queue */
                        __save_frame (rpc, frame, rpcreq);

                        /* A ref on rpc-clnt object is taken while registering
                         * call_bail to timer in __save_frame. If it fails to
                         * register, it needs an unref and should happen outside
                         * conn->lock which otherwise leads to deadlocks */
                        if (conn->timer == NULL)
                                need_unref = _gf_true;

                        conn->msgcnt++;

                        gf_log ("rpc-clnt", GF_LOG_TRACE, "submitted request "
                                "(XID: 0x%x Program: %s, ProgVers: %d, "
                                "Proc: %d) to rpc-transport (%s)", rpcreq->xid,
                                rpcreq->prog->progname, rpcreq->prog->progver,
                                rpcreq->procnum, conn->name);
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (need_unref)
                rpc_clnt_unref (rpc);

        if (ret == -1) {
                goto out;
        }

        rpc_clnt_check_and_start_ping (rpc);
        ret = 0;

out:
        if (request_iob) {
                iobuf_unref (request_iob);
        }

        if (new_iobref && iobref) {
                iobref_unref (iobref);
        }

        if (frame && (ret == -1)) {
                if (rpcreq) {
                        rpcreq->rpc_status = -1;
                        cbkfn (rpcreq, NULL, 0, frame);
                        mem_put (rpcreq);
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
rpc_clnt_trigger_destroy (struct rpc_clnt *rpc)
{
        rpc_clnt_connection_t  *conn  = NULL;
        rpc_transport_t        *trans = NULL;

        if (!rpc)
                return;

        /* reading conn->trans outside conn->lock is OK, since this is the last
         * ref*/
        conn = &rpc->conn;
        trans = conn->trans;
        rpc_clnt_disconnect (rpc);

        /* This is to account for rpc_clnt_disable that might have been called
         * before rpc_clnt_unref */
        if (trans) {
                rpc_transport_unref (trans);
                conn->trans = NULL;
        }
}

static void
rpc_clnt_destroy (struct rpc_clnt *rpc)
{
        if (!rpc)
                return;

        saved_frames_destroy (rpc->conn.saved_frames);
        pthread_mutex_destroy (&rpc->lock);
        pthread_mutex_destroy (&rpc->conn.lock);
        pthread_mutex_destroy (&rpc->notifylock);

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
                rpc_clnt_trigger_destroy (rpc);
                return NULL;
        }
        return rpc;
}


char
rpc_clnt_is_disabled (struct rpc_clnt *rpc)
{

        rpc_clnt_connection_t *conn = NULL;
        char                   disabled = 0;

        if (!rpc) {
                goto out;
        }

        conn = &rpc->conn;

        pthread_mutex_lock (&conn->lock);
        {
                disabled = rpc->disabled;
        }
        pthread_mutex_unlock (&conn->lock);

out:
        return disabled;
}

void
rpc_clnt_disable (struct rpc_clnt *rpc)
{
        rpc_clnt_connection_t *conn = NULL;
        rpc_transport_t       *trans = NULL;
        int                    unref = 0;
        int                    ret   = 0;
        gf_boolean_t           timer_unref = _gf_false;
        gf_boolean_t           reconnect_unref = _gf_false;

        if (!rpc) {
                goto out;
        }

        conn = &rpc->conn;

        pthread_mutex_lock (&conn->lock);
        {
                rpc->disabled = 1;

                if (conn->timer) {
                        ret = gf_timer_call_cancel (rpc->ctx, conn->timer);
                        /* If the event is not fired and it actually cancelled
                         * the timer, do the unref else registered call back
                         * function will take care of it.
                         */
                        if (!ret)
                                timer_unref = _gf_true;
                        conn->timer = NULL;
                }

                if (conn->reconnect) {
                        ret = gf_timer_call_cancel (rpc->ctx, conn->reconnect);
                        if (!ret)
                                reconnect_unref = _gf_true;
                        conn->reconnect = NULL;
                }
                conn->connected = 0;

                unref = rpc_clnt_remove_ping_timer_locked (rpc);
                trans = conn->trans;

        }
        pthread_mutex_unlock (&conn->lock);

        if (trans) {
                rpc_transport_disconnect (trans, _gf_true);
        }

        if (unref)
                rpc_clnt_unref (rpc);

        if (timer_unref)
                rpc_clnt_unref (rpc);

        if (reconnect_unref)
                rpc_clnt_unref (rpc);

out:
        return;
}

void
rpc_clnt_disconnect (struct rpc_clnt *rpc)
{
        rpc_clnt_connection_t *conn  = NULL;
        rpc_transport_t       *trans = NULL;
        int                    unref = 0;
        int                    ret   = 0;
        gf_boolean_t           timer_unref = _gf_false;
        gf_boolean_t           reconnect_unref = _gf_false;

        if (!rpc)
                goto out;

        conn = &rpc->conn;

        pthread_mutex_lock (&conn->lock);
        {
                rpc->disabled = 1;
                if (conn->timer) {
                        ret = gf_timer_call_cancel (rpc->ctx, conn->timer);
                        /* If the event is not fired and it actually cancelled
                         * the timer, do the unref else registered call back
                         * function will take care of unref.
                         */
                        if (!ret)
                                timer_unref = _gf_true;
                        conn->timer = NULL;
                }

                if (conn->reconnect) {
                        ret = gf_timer_call_cancel (rpc->ctx, conn->reconnect);
                        if (!ret)
                                reconnect_unref = _gf_true;
                        conn->reconnect = NULL;
                }
                conn->connected = 0;

                unref = rpc_clnt_remove_ping_timer_locked (rpc);
                trans = conn->trans;
        }
        pthread_mutex_unlock (&conn->lock);

        if (trans) {
                rpc_transport_disconnect (trans, _gf_true);
        }
        if (unref)
                rpc_clnt_unref (rpc);

        if (timer_unref)
                rpc_clnt_unref (rpc);

        if (reconnect_unref)
                rpc_clnt_unref (rpc);

out:
        return;
}


void
rpc_clnt_reconfig (struct rpc_clnt *rpc, struct rpc_clnt_config *config)
{
        if (config->ping_timeout) {
                if (config->ping_timeout != rpc->conn.ping_timeout)
                        gf_log (rpc->conn.name, GF_LOG_INFO,
                                "changing ping timeout to %d (from %d)",
                                config->ping_timeout,
                                rpc->conn.ping_timeout);

                pthread_mutex_lock (&rpc->conn.lock);
                {
                rpc->conn.ping_timeout = config->ping_timeout;
                }
                pthread_mutex_unlock (&rpc->conn.lock);

        }

        if (config->rpc_timeout) {
                if (config->rpc_timeout != rpc->conn.config.rpc_timeout)
                        gf_log (rpc->conn.name, GF_LOG_INFO,
                                "changing timeout to %d (from %d)",
                                config->rpc_timeout,
                                rpc->conn.config.rpc_timeout);
                rpc->conn.config.rpc_timeout = config->rpc_timeout;
        }

        if (config->remote_port) {
                if (config->remote_port != rpc->conn.config.remote_port)
                        gf_log (rpc->conn.name, GF_LOG_INFO,
                                "changing port to %d (from %d)",
                                config->remote_port,
                                rpc->conn.config.remote_port);

                rpc->conn.config.remote_port = config->remote_port;
        }

        if (config->remote_host) {
                if (rpc->conn.config.remote_host) {
                        if (strcmp (rpc->conn.config.remote_host,
                                    config->remote_host))
                                gf_log (rpc->conn.name, GF_LOG_INFO,
                                        "changing hostname to %s (from %s)",
                                        config->remote_host,
                                        rpc->conn.config.remote_host);
                        GF_FREE (rpc->conn.config.remote_host);
                } else {
                        gf_log (rpc->conn.name, GF_LOG_INFO,
                                "setting hostname to %s",
                                config->remote_host);
                }

                rpc->conn.config.remote_host = gf_strdup (config->remote_host);
        }
}
