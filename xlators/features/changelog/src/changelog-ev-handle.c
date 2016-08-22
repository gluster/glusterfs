/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "changelog-ev-handle.h"
#include "changelog-rpc-common.h"
#include "changelog-helpers.h"

struct rpc_clnt_program changelog_ev_program;

#define NR_IOVEC  (MAX_IOVEC - 3)
struct ev_rpc_vec {
        int count;
        struct iovec vector[NR_IOVEC];

        /* sequence number */
        unsigned long seq;
};

struct ev_rpc {
        rbuf_list_t     *rlist;
        struct rpc_clnt *rpc;
        struct ev_rpc_vec vec;
};

/**
 * As of now this just does the minimal (retval logging). Going further
 * un-acknowledges sequence numbers can be retransmitted and other
 * intelligence can be built into the server.
 */
int
changelog_event_dispatch_cbk (struct rpc_req *req,
                              struct iovec *iov, int count, void *myframe)
{
        return 0;
}

/* dispatcher RPC */
int
changelog_dispatch_vec (call_frame_t *frame, xlator_t *this,
                        struct rpc_clnt *rpc, struct ev_rpc_vec *vec)
{
         struct timeval      tv  = {0,};
         changelog_event_req req = {0,};

         (void) gettimeofday (&tv, NULL);

         /**
          * Event dispatch RPC header contains a sequence number for each
          * dispatch. This allows the reciever to order the request before
          * processing.
          */
         req.seq     = vec->seq;
         req.tv_sec  = tv.tv_sec;
         req.tv_usec = tv.tv_usec;

         return changelog_rpc_sumbit_req (rpc, (void *)&req,
                                          frame, &changelog_ev_program,
                                          CHANGELOG_REV_PROC_EVENT,
                                          vec->vector, vec->count, NULL,
                                          this, changelog_event_dispatch_cbk,
                                          (xdrproc_t) xdr_changelog_event_req);
 }

 int
 changelog_event_dispatch_rpc (call_frame_t *frame, xlator_t *this, void *data)
 {
         int                idx      = 0;
         int                count    = 0;
         int                ret      = 0;
         unsigned long      sequence = 0;
         rbuf_iovec_t      *rvec     = NULL;
         struct ev_rpc     *erpc     = NULL;
         struct rlist_iter  riter    = {{0,},};

         /* dispatch NR_IOVEC IO vectors at a time. */

         erpc = data;
         sequence = erpc->rlist->seq[0];

         rlist_iter_init (&riter, erpc->rlist);

         rvec_for_each_entry (rvec, &riter) {
                 idx = count % NR_IOVEC;
                 if (++count == NR_IOVEC) {
                         erpc->vec.vector[idx] = rvec->iov;
                         erpc->vec.seq = sequence++;
                         erpc->vec.count = NR_IOVEC;

                         ret = changelog_dispatch_vec (frame, this,
                                                       erpc->rpc, &erpc->vec);
                         if (ret)
                                 break;
                         count = 0;
                         continue;
                 }

                 erpc->vec.vector[idx] = rvec->iov;
         }

         if (ret)
                 goto error_return;

         idx = count % NR_IOVEC;
         if (idx) {
                 erpc->vec.seq = sequence;
                 erpc->vec.count = idx;

                 ret = changelog_dispatch_vec (frame, this,
                                               erpc->rpc, &erpc->vec);
         }

 error_return:
         return ret;
}

int
changelog_rpc_notify (struct rpc_clnt *rpc,
                      void *mydata, rpc_clnt_event_t event, void *data)
{
        xlator_t                *this      = NULL;
        changelog_rpc_clnt_t    *crpc      = NULL;
        changelog_clnt_t        *c_clnt    = NULL;
        changelog_priv_t        *priv      = NULL;
        changelog_ev_selector_t *selection = NULL;

        crpc = mydata;
        this = crpc->this;
        c_clnt = crpc->c_clnt;

        priv = this->private;

        switch (event) {
        case RPC_CLNT_CONNECT:
                rpc_clnt_set_connected (&rpc->conn);
                selection = &priv->ev_selection;

                LOCK (&c_clnt->wait_lock);
                {
                        LOCK (&c_clnt->active_lock);
                        {
                                changelog_select_event (this, selection,
                                                        crpc->filter);
                                list_move_tail (&crpc->list, &c_clnt->active);
                        }
                        UNLOCK (&c_clnt->active_lock);
                }
                UNLOCK (&c_clnt->wait_lock);

                break;
        case RPC_CLNT_DISCONNECT:
                rpc_clnt_disable (crpc->rpc);

                /* rpc_clnt_disable doesn't unref the rpc. It just marks
                 * the rpc as disabled and cancels reconnection timer.
                 * Hence unref the rpc object to free it.
                 */
                rpc_clnt_unref (crpc->rpc);

                selection = &priv->ev_selection;

                LOCK (&crpc->lock);
                {
                        changelog_deselect_event (this, selection,
                                                  crpc->filter);
                        changelog_set_disconnect_flag (crpc, _gf_true);
                }
                UNLOCK (&crpc->lock);

                break;
        case RPC_CLNT_MSG:
        case RPC_CLNT_DESTROY:
                /* Free up mydata */
                changelog_rpc_clnt_unref (crpc);
                break;
        }

        return 0;
}

void *
changelog_ev_connector (void *data)
{
        xlator_t             *this   = NULL;
        changelog_clnt_t     *c_clnt = NULL;
        changelog_rpc_clnt_t *crpc   = NULL;

        c_clnt = data;
        this = c_clnt->this;

        while (1) {
                pthread_mutex_lock (&c_clnt->pending_lock);
                {
                        while (list_empty (&c_clnt->pending))
                                pthread_cond_wait (&c_clnt->pending_cond,
                                                   &c_clnt->pending_lock);
                        crpc = list_first_entry (&c_clnt->pending,
                                                 changelog_rpc_clnt_t, list);
                        crpc->rpc =
                                changelog_rpc_client_init (this, crpc,
                                                           crpc->sock,
                                                           changelog_rpc_notify);
                        if (!crpc->rpc) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CHANGELOG_MSG_RPC_CONNECT_ERROR,
                                        "failed to connect back.. <%s>",
                                        crpc->sock);
                                crpc->cleanup (crpc);
                                goto mutex_unlock;
                        }

                        LOCK (&c_clnt->wait_lock);
                        {
                                list_move_tail (&crpc->list, &c_clnt->waitq);
                        }
                        UNLOCK (&c_clnt->wait_lock);
                }
        mutex_unlock:
                pthread_mutex_unlock (&c_clnt->pending_lock);
        }

        return NULL;
}

void
changelog_ev_cleanup_connections (xlator_t *this, changelog_clnt_t *c_clnt)
{
        changelog_rpc_clnt_t *crpc = NULL;

        /* cleanup active connections */
        LOCK (&c_clnt->active_lock);
        {
                list_for_each_entry (crpc, &c_clnt->active, list) {
                        rpc_clnt_disable (crpc->rpc);
                }
        }
        UNLOCK (&c_clnt->active_lock);
}

/**
 * TODO: granularize lock
 *
 * If we have multiple threads dispatching events, doing it this way is
 * a performance bottleneck.
 */

static changelog_rpc_clnt_t *
get_client (changelog_clnt_t *c_clnt, struct list_head **next)
{
        changelog_rpc_clnt_t *crpc = NULL;

        LOCK (&c_clnt->active_lock);
        {
                if (*next == &c_clnt->active)
                        goto unblock;
                crpc = list_entry (*next, changelog_rpc_clnt_t, list);
                /* ref rpc as DISCONNECT might unref the rpc asynchronously */
                changelog_rpc_clnt_ref (crpc);
                rpc_clnt_ref (crpc->rpc);
                *next = (*next)->next;
        }
 unblock:
        UNLOCK (&c_clnt->active_lock);

        return crpc;
}

static void
put_client (changelog_clnt_t *c_clnt, changelog_rpc_clnt_t *crpc)
{
        LOCK (&c_clnt->active_lock);
        {
                rpc_clnt_unref (crpc->rpc);
                changelog_rpc_clnt_unref (crpc);
        }
        UNLOCK (&c_clnt->active_lock);
}

void
_dispatcher (rbuf_list_t *rlist, void *arg)
{
        xlator_t             *this   = NULL;
        changelog_clnt_t     *c_clnt = NULL;
        changelog_rpc_clnt_t *crpc   = NULL;
        struct ev_rpc         erpc   = {0,};
        struct list_head     *next   = NULL;

        c_clnt = arg;
        this = c_clnt->this;

        erpc.rlist = rlist;
        next = c_clnt->active.next;

        while (1) {
                crpc = get_client (c_clnt, &next);
                if (!crpc)
                        break;
                erpc.rpc = crpc->rpc;
                (void) changelog_invoke_rpc (this, crpc->rpc,
                                             &changelog_ev_program,
                                             CHANGELOG_REV_PROC_EVENT, &erpc);
                put_client (c_clnt, crpc);
        }
}

/** this is called under rotbuff's lock */
void
sequencer (rbuf_list_t *rlist, void *mydata)
{
        unsigned long     range  = 0;
        changelog_clnt_t *c_clnt = 0;

        c_clnt = mydata;

        range = (RLIST_ENTRY_COUNT (rlist)) / NR_IOVEC;
        if ((RLIST_ENTRY_COUNT (rlist)) % NR_IOVEC)
                range++;
        RLIST_STORE_SEQ (rlist, c_clnt->sequence, range);

        c_clnt->sequence += range;
}

void *
changelog_ev_dispatch (void *data)
{
        int                   ret    = 0;
        void                 *opaque = NULL;
        xlator_t             *this   = NULL;
        changelog_clnt_t     *c_clnt = NULL;
        struct timeval        tv     = {0,};

        c_clnt = data;
        this = c_clnt->this;

        while (1) {
                /* TODO: change this to be pthread cond based.. later */
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                select (0, NULL, NULL, NULL, &tv);

                ret = rbuf_get_buffer (c_clnt->rbuf,
                                       &opaque, sequencer, c_clnt);
                if (ret != RBUF_CONSUMABLE) {
                        if (ret != RBUF_EMPTY)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        CHANGELOG_MSG_BUFFER_STARVATION_ERROR,
                                        "Failed to get buffer for RPC dispatch "
                                        "[rbuf retval: %d]", ret);
                        continue;
                }

                ret = rbuf_wait_for_completion (c_clnt->rbuf,
                                                opaque, _dispatcher, c_clnt);
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                CHANGELOG_MSG_PUT_BUFFER_FAILED,
                                "failed to put buffer after consumption");
        }

        return NULL;
}

void
changelog_ev_queue_connection (changelog_clnt_t *c_clnt,
                               changelog_rpc_clnt_t *crpc)
{
        pthread_mutex_lock (&c_clnt->pending_lock);
        {
                list_add_tail (&crpc->list, &c_clnt->pending);
                pthread_cond_signal (&c_clnt->pending_cond);
        }
        pthread_mutex_unlock (&c_clnt->pending_lock);
}

struct rpc_clnt_procedure changelog_ev_procs[CHANGELOG_REV_PROC_MAX] = {
        [CHANGELOG_REV_PROC_NULL]  = {"NULL", NULL},
        [CHANGELOG_REV_PROC_EVENT] = {
                "EVENT DISPATCH", changelog_event_dispatch_rpc
        },
};

struct rpc_clnt_program changelog_ev_program = {
        .progname  = "CHANGELOG EVENT DISPATCHER",
        .prognum   = CHANGELOG_REV_RPC_PROCNUM,
        .progver   = CHANGELOG_REV_RPC_PROCVER,
        .numproc   = CHANGELOG_REV_PROC_MAX,
        .proctable = changelog_ev_procs,
};
