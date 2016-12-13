/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "rpc-clnt.h"
#include "rpc-clnt-ping.h"
#include "byte-order.h"
#include "xdr-rpcclnt.h"
#include "rpc-transport.h"
#include "protocol-common.h"
#include "mem-pool.h"
#include "xdr-rpc.h"
#include "rpc-common-xdr.h"


char *clnt_ping_procs[GF_DUMP_MAXVALUE] = {
        [GF_DUMP_PING] = "NULL",
};
struct rpc_clnt_program clnt_ping_prog = {
        .progname  = "GF-DUMP",
        .prognum   = GLUSTER_DUMP_PROGRAM,
        .progver   = GLUSTER_DUMP_VERSION,
        .procnames = clnt_ping_procs,
};

/* Must be called under conn->lock */
static int
__rpc_clnt_rearm_ping_timer (struct rpc_clnt *rpc, gf_timer_cbk_t cbk)
{
        rpc_clnt_connection_t *conn    = &rpc->conn;
        rpc_transport_t       *trans   = conn->trans;
        struct timespec        timeout = {0, };
        gf_timer_t            *timer   = NULL;

        if (conn->ping_timer) {
                gf_log_callingfn ("", GF_LOG_CRITICAL,
                                  "%s: ping timer event already scheduled",
                                  conn->trans->peerinfo.identifier);
                return -1;
        }

        timeout.tv_sec = conn->ping_timeout;
        timeout.tv_nsec = 0;

        rpc_clnt_ref (rpc);
        timer = gf_timer_call_after (rpc->ctx, timeout,
                                     cbk,
                                     (void *) rpc);
        if (timer == NULL) {
                gf_log (trans->name, GF_LOG_WARNING,
                        "unable to setup ping timer");

                /* This unref can't be the last. We just took a ref few lines
                 * above. So this can be performed under conn->lock. */
                rpc_clnt_unref (rpc);
                conn->ping_started = 0;
                return -1;
        }

        conn->ping_timer = timer;
        conn->ping_started = 1;
        return 0;
}

/* Must be called under conn->lock */
int
rpc_clnt_remove_ping_timer_locked (struct rpc_clnt *rpc)
{
        rpc_clnt_connection_t *conn  = &rpc->conn;
        gf_timer_t            *timer = NULL;

        if (conn->ping_timer) {
                timer = conn->ping_timer;
                conn->ping_timer = NULL;
                gf_timer_call_cancel (rpc->ctx, timer);
                conn->ping_started = 0;
                return 1;

        }

        /* This is to account for rpc_clnt_disable that might have set
         *  conn->trans to NULL. */
        if (conn->trans)
                gf_log_callingfn ("", GF_LOG_DEBUG, "%s: ping timer event "
                                  "already removed",
                                   conn->trans->peerinfo.identifier);

        return 0;
}

static void
rpc_clnt_start_ping (void *rpc_ptr);

void
rpc_clnt_ping_timer_expired (void *rpc_ptr)
{
        struct rpc_clnt         *rpc                = NULL;
        rpc_transport_t         *trans              = NULL;
        rpc_clnt_connection_t   *conn               = NULL;
        int                      disconnect         = 0;
        int                      transport_activity = 0;
        struct timeval           current            = {0, };
        int                      unref              = 0;

        rpc = (struct rpc_clnt*) rpc_ptr;
        conn = &rpc->conn;
        trans = conn->trans;

        if (!trans) {
                gf_log ("ping-timer", GF_LOG_WARNING,
                        "transport not initialized");
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                unref = rpc_clnt_remove_ping_timer_locked (rpc);

                gettimeofday (&current, NULL);
                if (((current.tv_sec - conn->last_received.tv_sec) <
                     conn->ping_timeout)
                    || ((current.tv_sec - conn->last_sent.tv_sec) <
                        conn->ping_timeout)) {
                        transport_activity = 1;
                }

                if (transport_activity) {
                        gf_log (trans->name, GF_LOG_TRACE,
                                "ping timer expired but transport activity "
                                "detected - not bailing transport");

                        if (__rpc_clnt_rearm_ping_timer (rpc,
                                         rpc_clnt_ping_timer_expired) == -1) {
                                gf_log (trans->name, GF_LOG_WARNING,
                                        "unable to setup ping timer");
                        }

                } else {
                        conn->ping_started = 0;
                        disconnect = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (unref)
                rpc_clnt_unref (rpc);

        if (disconnect) {
                gf_log (trans->name, GF_LOG_CRITICAL,
                        "server %s has not responded in the last %d "
                        "seconds, disconnecting.",
                        trans->peerinfo.identifier,
                        conn->ping_timeout);

                rpc_transport_disconnect (conn->trans, _gf_false);
        }

out:
        return;
}

int
rpc_clnt_ping_cbk (struct rpc_req *req, struct iovec *iov, int count,
                   void *myframe)
{
        struct rpc_clnt       *rpc     = NULL;
        xlator_t              *this    = NULL;
        rpc_clnt_connection_t *conn    = NULL;
        call_frame_t          *frame   = NULL;
        int                   unref    = 0;

        if (!myframe) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "frame with the request is NULL");
                goto out;
        }

        frame = myframe;
        this = frame->this;
        rpc  = frame->local;
        frame->local = NULL; /* Prevent STACK_DESTROY from segfaulting */
        conn = &rpc->conn;

        pthread_mutex_lock (&conn->lock);
        {
                if (req->rpc_status == -1) {
                        unref = rpc_clnt_remove_ping_timer_locked (rpc);
                        if (unref) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "socket or ib related error");

                        } else {
                                /* timer expired and transport bailed out */
                                gf_log (this->name, GF_LOG_WARNING,
                                        "socket disconnected");

                        }
                        conn->ping_started = 0;
                        goto unlock;
                }

                unref = rpc_clnt_remove_ping_timer_locked (rpc);
                if (__rpc_clnt_rearm_ping_timer (rpc,
                                                 rpc_clnt_start_ping) == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set the ping timer");
                }

        }
unlock:
        pthread_mutex_unlock (&conn->lock);
out:
        if (unref)
                rpc_clnt_unref (rpc);

        if (frame)
                STACK_DESTROY (frame->root);
        return 0;
}

int
rpc_clnt_ping (struct rpc_clnt *rpc)
{
        call_frame_t *frame = NULL;
        int32_t       ret   = -1;
        rpc_clnt_connection_t *conn = NULL;

        conn = &rpc->conn;
        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                return ret;

        frame->local = rpc;

        ret = rpc_clnt_submit (rpc, &clnt_ping_prog,
                               GF_DUMP_PING, rpc_clnt_ping_cbk, NULL, 0,
                               NULL, 0, NULL, frame, NULL, 0, NULL, 0, NULL);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to start ping timer");
        }
        else {
                /* ping successfully queued in list of saved frames
                 * for the connection*/
                pthread_mutex_lock (&conn->lock);
                conn->pingcnt++;
                pthread_mutex_unlock (&conn->lock);
        }

        return ret;

}

static void
rpc_clnt_start_ping (void *rpc_ptr)
{
        struct rpc_clnt         *rpc         = NULL;
        rpc_clnt_connection_t   *conn        = NULL;
        int                      frame_count = 0;
        int                      unref       = 0;

        rpc = (struct rpc_clnt*) rpc_ptr;
        conn = &rpc->conn;

        if (conn->ping_timeout == 0) {
                gf_log (THIS->name, GF_LOG_DEBUG, "ping timeout is 0,"
                        " returning");
                return;
        }

        pthread_mutex_lock (&conn->lock);
        {
                unref = rpc_clnt_remove_ping_timer_locked (rpc);

                if (conn->saved_frames) {
                        GF_ASSERT (conn->saved_frames->count >= 0);
                        /* treat the case where conn->saved_frames is NULL
                           as no pending frames */
                        frame_count = conn->saved_frames->count;
                }

                if ((frame_count == 0) || !conn->connected) {
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "returning as transport is already disconnected"
                                " OR there are no frames (%d || %d)",
                                !conn->connected, frame_count);

                        pthread_mutex_unlock (&conn->lock);
                        if (unref)
                                rpc_clnt_unref (rpc);
                        return;
                }

                if (__rpc_clnt_rearm_ping_timer (rpc,
                                         rpc_clnt_ping_timer_expired) == -1) {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "unable to setup ping timer");
                        pthread_mutex_unlock (&conn->lock);
                        if (unref)
                                rpc_clnt_unref (rpc);
                        return;

                }

        }
        pthread_mutex_unlock (&conn->lock);
        if (unref)
                rpc_clnt_unref (rpc);

        rpc_clnt_ping(rpc);
}

void
rpc_clnt_check_and_start_ping (struct rpc_clnt *rpc)
{
        char start_ping = 0;

        pthread_mutex_lock (&rpc->conn.lock);
        {
                if (!rpc->conn.ping_started)
                        start_ping = 1;
        }
        pthread_mutex_unlock (&rpc->conn.lock);

        if (start_ping)
                rpc_clnt_start_ping ((void *)rpc);

        return;
}
