/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "changelog-misc.h"
#include "changelog-mem-types.h"

#include "gf-changelog-helpers.h"
#include "changelog-rpc-common.h"

/**
 * Reverse socket: actual data transfer handler. Connection
 * initiator is PROBER, data transfer is REBORP.
 */

struct rpcsvc_program *gf_changelog_reborp_programs[];

/**
 * On a reverse connection, unlink the socket file.
 */
int
gf_changelog_reborp_rpcsvc_notify (rpcsvc_t *rpc, void *mydata,
                                   rpcsvc_event_t event, void *data)
{
        int ret = 0;
        xlator_t       *this     = NULL;
        gf_private_t   *priv     = NULL;
        gf_changelog_t *entry    = NULL;
        char sock[UNIX_PATH_MAX] = {0,};

        entry = mydata;
        this = entry->this;
        priv = this->private;

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
                ret = unlink (RPC_SOCK(entry));
                if (ret != 0)
                        gf_log (this->name, GF_LOG_WARNING, "failed to unlink"
                                " reverse socket file %s", RPC_SOCK (entry));
                if (entry->connected)
                        GF_CHANGELOG_INVOKE_CBK (this, entry->connected,
                                                 entry->brick, entry->ptr);
                break;
        case RPCSVC_EVENT_DISCONNECT:
                LOCK (&priv->lock);
                {
                        list_del (&entry->list);
                }
                UNLOCK (&priv->lock);

                if (entry->disconnected)
                        GF_CHANGELOG_INVOKE_CBK (this, entry->disconnected,
                                                 entry->brick, entry->ptr);

                GF_FREE (entry);
                break;
        default:
                break;
        }

        return 0;
}

rpcsvc_t *
gf_changelog_reborp_init_rpc_listner (xlator_t *this,
                                      char *path, char *sock, void *cbkdata)
{
        CHANGELOG_MAKE_TMP_SOCKET_PATH (path, sock, UNIX_PATH_MAX);
        return changelog_rpc_server_init (this, sock, cbkdata,
                                          gf_changelog_reborp_rpcsvc_notify,
                                          gf_changelog_reborp_programs);
}

/**
 * This is dirty and painful as of now untill there is event filtering in the
 * server. The entire event buffer is scanned and interested events are picked,
 * whereas we _should_ be notified with the events we were interested in
 * (selected at the time of probe). As of now this is complete BS and needs
 * fixture ASAP. I just made it work, it needs to be better.
 *
 * @FIXME: cleanup this bugger once server filters events.
 */
inline void
gf_changelog_invoke_callback (gf_changelog_t *entry,
                              struct iovec **vec, int payloadcnt)
{
        int i = 0;
        int evsize = 0;
        xlator_t *this = NULL;
        changelog_event_t *event = NULL;

        this = entry->this;

        for (; i < payloadcnt; i++) {
                event = (changelog_event_t *)vec[i]->iov_base;
                evsize = vec[i]->iov_len / CHANGELOG_EV_SIZE;

                for (; evsize > 0; evsize--, event++) {
                        if (gf_changelog_filter_check (entry, event)) {
                                GF_CHANGELOG_INVOKE_CBK (this,
                                                         entry->callback,
                                                         entry->brick,
                                                         entry->ptr, event);
                        }
                }
        }
}

/**
 * Ordered event handler is self-adaptive.. if the event sequence number
 * is what's expected (->next_seq) there is no ordering list that's
 * maintained. On out-of-order event notifications, event buffers are
 * dynamically allocated and ordered.
 */

inline int
__is_expected_sequence (struct gf_event_list *ev, struct gf_event *event)
{
        return (ev->next_seq == event->seq);
}

inline int
__can_process_event (struct gf_event_list *ev, struct gf_event **event)
{
        *event = list_first_entry (&ev->events, struct gf_event, list);

        if (__is_expected_sequence (ev, *event)) {
                list_del (&(*event)->list);
                ev->next_seq++;
                return 1;
        }

        return 0;
}

inline void
__process_event_list (struct gf_event_list *ev, struct gf_event **event)
{
        while ( list_empty (&ev->events)
               || !__can_process_event (ev, event) )
                pthread_cond_wait (&ev->cond, &ev->lock);
}

void *
gf_changelog_callback_invoker (void *arg)
{
        int                   ret    = 0;
        xlator_t             *this   = NULL;
        gf_changelog_t       *entry  = NULL;
        struct iovec         *vec    = NULL;
        struct gf_event      *event  = NULL;
        struct gf_event_list *ev     = NULL;

        ev    = arg;
        entry = ev->entry;
        this  = entry->this;

        while (1) {
                pthread_mutex_lock (&ev->lock);
                {
                        __process_event_list (ev, &event);
                }
                pthread_mutex_unlock (&ev->lock);

                vec = (struct iovec *) &event->iov;
                gf_changelog_invoke_callback (entry, &vec, event->count);

                GF_FREE (event);
        }

        return NULL;
}

static int
orderfn (struct list_head *pos1, struct list_head *pos2)
{
        struct gf_event *event1 = NULL;
        struct gf_event *event2 = NULL;

        event1 = list_entry (pos1, struct gf_event, list);
        event2 = list_entry (pos2, struct gf_event, list);

        if  (event1->seq > event2->seq)
                return 1;
        return -1;
}

int
gf_changelog_ordered_event_handler (rpcsvc_request_t *req,
                                    xlator_t *this, gf_changelog_t *entry)
{
        int                   i          = 0;
        size_t                payloadlen = 0;
        ssize_t               len        = 0;
        int                   payloadcnt = 0;
        changelog_event_req   rpc_req    = {0,};
        changelog_event_rsp   rpc_rsp    = {0,};
        struct iovec         *vec        = NULL;
        struct gf_event      *event      = NULL;
        struct gf_event_list *ev         = NULL;

        ev = &entry->event;

        len = xdr_to_generic (req->msg[0],
                              &rpc_req, (xdrproc_t)xdr_changelog_event_req);
        if (len < 0) {
                gf_log (this->name, GF_LOG_ERROR, "xdr decoding failed");
                req->rpc_err = GARBAGE_ARGS;
                goto handle_xdr_error;
        }

        if (len < req->msg[0].iov_len) {
                payloadcnt = 1;
                payloadlen = (req->msg[0].iov_len - len);
        }
        for (i = 1; i < req->count; i++) {
                payloadcnt++;
                payloadlen += req->msg[i].iov_len;
        }

        event = GF_CALLOC (1, GF_EVENT_CALLOC_SIZE (payloadcnt, payloadlen),
                           gf_changelog_mt_libgfchangelog_event_t);
        if (!event)
                goto handle_xdr_error;
        INIT_LIST_HEAD (&event->list);

        payloadlen   = 0;
        event->seq   = rpc_req.seq;
        event->count = payloadcnt;

        /* deep copy IO vectors */
        vec = &event->iov[0];
        GF_EVENT_ASSIGN_IOVEC (vec, event,
                               (req->msg[0].iov_len - len), payloadlen);
        (void) memcpy (vec->iov_base,
                       req->msg[0].iov_base + len, vec->iov_len);

        for (i = 1; i < req->count; i++) {
                vec = &event->iov[i];
                GF_EVENT_ASSIGN_IOVEC (vec, event,
                                       req->msg[i].iov_len, payloadlen);
                (void) memcpy (event->iov[i].iov_base,
                               req->msg[i].iov_base, req->msg[i].iov_len);
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "seq: %lu [%s] (time: %lu.%lu), (vec: %d, len: %ld)",
                rpc_req.seq, entry->brick, rpc_req.tv_sec,
                rpc_req.tv_usec, payloadcnt, payloadlen);

        /* add it to the ordered event list and wake up listner(s) */
        pthread_mutex_lock (&ev->lock);
        {
                list_add_order (&event->list, &ev->events, orderfn);
                if ( (!ev->next_seq && (ev->next_seq = event->seq))
                     || (ev->next_seq == event->seq) )
                        pthread_cond_signal (&ev->cond);
        }
        pthread_mutex_unlock (&ev->lock);

        /* ack sequence number */
        rpc_rsp.op_ret = 0;
        rpc_rsp.seq    = rpc_req.seq;

        goto submit_rpc;

 handle_xdr_error:
        rpc_rsp.op_ret = -1;
        rpc_rsp.seq    = 0;     /* invalid */
 submit_rpc:
        return changelog_rpc_sumbit_reply (req, &rpc_rsp, NULL, 0, NULL,
                                           (xdrproc_t)xdr_changelog_event_rsp);
}

int
gf_changelog_unordered_event_handler (rpcsvc_request_t *req,
                                      xlator_t *this, gf_changelog_t *entry)
{
        int                 i          = 0;
        int                 ret        = 0;
        ssize_t             len        = 0;
        int                 payloadcnt = 0;
        struct iovec vector[MAX_IOVEC] = {{0,}};
        changelog_event_req rpc_req    = {0,};
        changelog_event_rsp rpc_rsp    = {0,};

        len = xdr_to_generic (req->msg[0],
                              &rpc_req, (xdrproc_t)xdr_changelog_event_req);
        if (len < 0) {
                gf_log (this->name, GF_LOG_ERROR, "xdr decoding failed");
                req->rpc_err = GARBAGE_ARGS;
                goto handle_xdr_error;
        }

        /* prepare payload */
        if (len < req->msg[0].iov_len) {
                payloadcnt = 1;
                vector[0].iov_base = (req->msg[0].iov_base + len);
                vector[0].iov_len  = (req->msg[0].iov_len - len);
        }

        for (i = 1; i < req->count; i++) {
                vector[payloadcnt++] = req->msg[i];
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "seq: %lu (time: %lu.%lu), (vec: %d)",
                rpc_req.seq, rpc_req.tv_sec, rpc_req.tv_usec, payloadcnt);

        /* invoke callback */
        struct iovec *vec = (struct iovec *) &vector;
        gf_changelog_invoke_callback (entry, &vec, payloadcnt);

        /* ack sequence number */
        rpc_rsp.op_ret = 0;
        rpc_rsp.seq = rpc_req.seq;

        goto submit_rpc;

 handle_xdr_error:
        rpc_rsp.op_ret = -1;
        rpc_rsp.seq = 0; /* invalid */
 submit_rpc:
        return changelog_rpc_sumbit_reply (req, &rpc_rsp, NULL, 0, NULL,
                                           (xdrproc_t)xdr_changelog_event_rsp);
}

int
gf_changelog_reborp_handle_event (rpcsvc_request_t *req)
{
        int                  ret     = 0;
        xlator_t            *this    = NULL;
        rpcsvc_t            *svc     = NULL;
        gf_changelog_t      *entry   = NULL;

        svc = rpcsvc_request_service (req);
        entry = svc->mydata;

        this = THIS = entry->this;

        ret = GF_NEED_ORDERED_EVENTS (entry)
                ? gf_changelog_ordered_event_handler (req, this, entry)
                : gf_changelog_unordered_event_handler (req, this, entry);
        
        return ret;
}

rpcsvc_actor_t gf_changelog_reborp_actors[CHANGELOG_REV_PROC_MAX] = {
        [CHANGELOG_REV_PROC_EVENT] = {
                "CHANGELOG EVENT HANDLER", CHANGELOG_REV_PROC_EVENT,
                gf_changelog_reborp_handle_event, NULL, 0, DRC_NA
        },
};

/**
 * Do not use synctask as the RPC layer dereferences ->mydata as THIS.
 * In gf_changelog_setup_rpc(), @cbkdata is of type @gf_changelog_t,
 * and that's required to invoke the callback with the appropriate
 * brick path and it's private data.
 */
struct rpcsvc_program gf_changelog_reborp_prog = {
        .progname  = "LIBGFCHANGELOG REBORP",
        .prognum   = CHANGELOG_REV_RPC_PROCNUM,
        .progver   = CHANGELOG_REV_RPC_PROCVER,
        .numactors = CHANGELOG_REV_PROC_MAX,
        .actors    = gf_changelog_reborp_actors,
        .synctask  = _gf_false,
};

struct rpcsvc_program *gf_changelog_reborp_programs[] = {
        &gf_changelog_reborp_prog,
        NULL,
};
