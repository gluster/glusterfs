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
#include "changelog-lib-messages.h"

#include "syscall.h"

/**
 * Reverse socket: actual data transfer handler. Connection
 * initiator is PROBER, data transfer is REBORP.
 */

struct rpcsvc_program *gf_changelog_reborp_programs[];

void *
gf_changelog_connection_janitor (void *arg)
{
        int32_t ret = 0;
        xlator_t *this = NULL;
        gf_private_t *priv = NULL;
        gf_changelog_t *entry = NULL;
        struct gf_event *event = NULL;
        struct gf_event_list *ev = NULL;
        unsigned long drained = 0;

        this = arg;
        THIS = this;

        priv = this->private;

        while (1) {
                pthread_mutex_lock (&priv->lock);
                {
                        while (list_empty (&priv->cleanups))
                                pthread_cond_wait (&priv->cond, &priv->lock);

                        entry = list_first_entry (&priv->cleanups,
                                                  gf_changelog_t, list);
                        list_del_init (&entry->list);
                }
                pthread_mutex_unlock (&priv->lock);

                drained = 0;
                ev = &entry->event;

                gf_msg (this->name, GF_LOG_INFO, 0,
                        CHANGELOG_LIB_MSG_CLEANING_BRICK_ENTRY_INFO,
                        "Cleaning brick entry for brick %s", entry->brick);

                /* 0x0: disbale rpc-clnt */
                rpc_clnt_disable (RPC_PROBER (entry));

                /* 0x1: cleanup callback invoker thread */
                ret = gf_cleanup_event (this, ev);
                if (ret)
                        continue;

                /* 0x2: drain pending events */
                while (!list_empty (&ev->events)) {
                        event = list_first_entry (&ev->events,
                                                  struct gf_event, list);
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                CHANGELOG_LIB_MSG_DRAINING_EVENT_INFO,
                                "Draining event [Seq: %lu, Payload: %d]",
                                event->seq, event->count);

                        GF_FREE (event);
                        drained++;
                }

                gf_msg (this->name, GF_LOG_INFO, 0,
                        CHANGELOG_LIB_MSG_DRAINING_EVENT_INFO,
                        "Drained %lu events", drained);

                /* 0x3: freeup brick entry */
                gf_msg (this->name, GF_LOG_INFO, 0,
                        CHANGELOG_LIB_MSG_FREEING_ENTRY_INFO,
                        "freeing entry %p", entry);
                LOCK_DESTROY (&entry->statelock);
                GF_FREE (entry);
        }

        return NULL;
}

int
gf_changelog_reborp_rpcsvc_notify (rpcsvc_t *rpc, void *mydata,
                                   rpcsvc_event_t event, void *data)
{
        int             ret      = 0;
        xlator_t       *this     = NULL;
        gf_changelog_t *entry    = NULL;

        if (!(event == RPCSVC_EVENT_ACCEPT ||
              event == RPCSVC_EVENT_DISCONNECT))
                return 0;

        entry = mydata;
        this = entry->this;

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
                ret = sys_unlink (RPC_SOCK(entry));
                if (ret != 0)
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                CHANGELOG_LIB_MSG_UNLINK_FAILED,
                                "failed to unlink "
                                "reverse socket %s", RPC_SOCK (entry));
                if (entry->connected)
                        GF_CHANGELOG_INVOKE_CBK (this, entry->connected,
                                                 entry->brick, entry->ptr);
                break;
        case RPCSVC_EVENT_DISCONNECT:
                if (entry->disconnected)
                        GF_CHANGELOG_INVOKE_CBK (this, entry->disconnected,
                                                 entry->brick, entry->ptr);
                /* passthrough */
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
void
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

int
__is_expected_sequence (struct gf_event_list *ev, struct gf_event *event)
{
        return (ev->next_seq == event->seq);
}

int
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

void
pick_event_ordered (struct gf_event_list *ev, struct gf_event **event)
{
        pthread_mutex_lock (&ev->lock);
        {
                while (list_empty (&ev->events)
                       || !__can_process_event (ev, event))
                        pthread_cond_wait (&ev->cond, &ev->lock);
        }
        pthread_mutex_unlock (&ev->lock);
}

void
pick_event_unordered (struct gf_event_list *ev, struct gf_event **event)
{
        pthread_mutex_lock (&ev->lock);
        {
                while (list_empty (&ev->events))
                        pthread_cond_wait (&ev->cond, &ev->lock);
                *event = list_first_entry (&ev->events, struct gf_event, list);
                list_del (&(*event)->list);
        }
        pthread_mutex_unlock (&ev->lock);
}

void *
gf_changelog_callback_invoker (void *arg)
{
        xlator_t             *this   = NULL;
        gf_changelog_t       *entry  = NULL;
        struct iovec         *vec    = NULL;
        struct gf_event      *event  = NULL;
        struct gf_event_list *ev     = NULL;

        ev    = arg;
        entry = ev->entry;
        THIS = this = entry->this;

        while (1) {
                entry->pickevent (ev, &event);

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

void
queue_ordered_event (struct gf_event_list *ev, struct gf_event *event)
{
        /* add event to the ordered event list and wake up listner(s) */
        pthread_mutex_lock (&ev->lock);
        {
                list_add_order (&event->list, &ev->events, orderfn);
                if (!ev->next_seq)
                        ev->next_seq = event->seq;
                if (ev->next_seq == event->seq)
                        pthread_cond_signal (&ev->cond);
        }
        pthread_mutex_unlock (&ev->lock);
}

void
queue_unordered_event (struct gf_event_list *ev, struct gf_event *event)
{
        /* add event to the tail of the queue and wake up listener(s) */
        pthread_mutex_lock (&ev->lock);
        {
                list_add_tail (&event->list, &ev->events);
                pthread_cond_signal (&ev->cond);
        }
        pthread_mutex_unlock (&ev->lock);
}

int
gf_changelog_event_handler (rpcsvc_request_t *req,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_LIB_MSG_XDR_DECODING_FAILED,
                        "xdr decoding failed");
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

        gf_msg_debug (this->name, 0,
                      "seq: %lu [%s] (time: %lu.%lu), (vec: %d, len: %zd)",
                      rpc_req.seq, entry->brick, rpc_req.tv_sec,
                      rpc_req.tv_usec, payloadcnt, payloadlen);

        /* dispatch event */
        entry->queueevent (ev, event);

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
gf_changelog_reborp_handle_event (rpcsvc_request_t *req)
{
        xlator_t       *this  = NULL;
        rpcsvc_t       *svc   = NULL;
        gf_changelog_t *entry = NULL;

        svc = rpcsvc_request_service (req);
        entry = svc->mydata;

        this = THIS = entry->this;

        return gf_changelog_event_handler (req, this, entry);
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
