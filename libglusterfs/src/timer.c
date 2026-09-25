/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/timer.h"
#include "glusterfs/logging.h"
#include "glusterfs/globals.h"
#include "glusterfs/timespec.h"
#include "glusterfs/libglusterfs-messages.h"

/* fwd decl */
static gf_timer_registry_t *
gf_timer_registry_init(glusterfs_ctx_t *);

gf_timer_t *
gf_timer_call_after(glusterfs_ctx_t *ctx, struct timespec delta,
                    gf_timer_cbk_t callbk, void *data)
{
    gf_timer_registry_t *reg = NULL;
    gf_timer_t *event = NULL;
    gf_timer_t *trav = NULL;
    uint64_t at = 0;

    if ((ctx == NULL) || (ctx->cleanup_started)) {
        gf_msg_callingfn("timer", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "Either ctx is NULL or"
                         " ctx cleanup started");
        return NULL;
    }

    reg = gf_timer_registry_init(ctx);

    if (!reg) {
        gf_msg_callingfn("timer", GF_LOG_ERROR, 0, LG_MSG_TIMER_REGISTER_ERROR,
                         "!reg");
        return NULL;
    }

    event = GF_CALLOC(1, sizeof(*event), gf_common_mt_gf_timer_t);
    if (!event) {
        return NULL;
    }
    timespec_now(&event->at);
    timespec_adjust_delta(&event->at, delta);
    at = TS(event->at);
    event->callbk = callbk;
    event->data = data;
    event->xl = THIS;
    pthread_mutex_lock(&reg->lock);
    {
        list_for_each_entry_reverse(trav, &reg->active, list)
        {
            if (TS(trav->at) < at)
                break;
        }
        list_add(&event->list, &trav->list);
        if (&trav->list == &reg->active) {
            pthread_cond_signal(&reg->cond);
        }
    }
    pthread_mutex_unlock(&reg->lock);
    return event;
}

/*
 * Cancel a pending timer event.
 *
 * Returns 0 when the event was still queued and has now been removed and
 * freed: its callback will never run, and whatever the event carried
 * (typically a reference on the object the callback would have acted on)
 * is the caller's to release.
 *
 * Returns -1 when this call does not prevent the callback:
 *  - the event has already been dequeued for dispatch (event->fired): the
 *    callback is running or about to run, and it owns what the event
 *    carried -- the caller must not release it;
 *  - the timer registry is being or has been destroyed (ctx->timer == NULL,
 *    set by gf_timer_registry_destroy() before it joins the timer thread):
 *    an event that had already been dequeued is being run by the thread
 *    being joined and its callback owns what it carried; an event still
 *    queued is freed by gf_timer_proc()'s shutdown loop without invoking
 *    the callback, and nobody releases what it carried. In glfs_fini()
 *    the registry is destroyed only after every xlator has been finalized,
 *    so only a cancel issued at that point meets this case; it is
 *    process-exit territory.
 *
 * ctx->cleanup_started does not change any of this. The registry stays
 * alive until gf_timer_registry_destroy(), which glfs_fini() reaches only
 * after xlator fini, so a cancel issued during teardown is an ordinary
 * cancel: the event is really removed and its callback really never runs.
 *
 * ctx->lock is held from the read of ctx->timer to the end of the
 * reg->lock section. gf_timer_registry_destroy() nulls ctx->timer under
 * ctx->lock and frees the registry only after that, so a registry seen
 * non-NULL here cannot be freed until this function has released
 * ctx->lock: a cancel concurrent with the destroy either finds ctx->timer
 * already NULL and returns -1, or completes against a live registry. The
 * lock order is ctx->lock -> reg->lock; reg->lock is private to this file
 * and is never held while ctx->lock is taken (gf_timer_proc() drops it
 * before running a callback), so the nesting adds no cycle.
 */
int32_t
gf_timer_call_cancel(glusterfs_ctx_t *ctx, gf_timer_t *event)
{
    gf_timer_registry_t *reg = NULL;
    gf_boolean_t fired = _gf_false;

    if (ctx == NULL || event == NULL) {
        gf_msg_callingfn("timer", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return -1;
    }

    LOCK(&ctx->lock);
    {
        reg = ctx->timer;
        if (!reg) {
            /* gf_timer_registry_destroy() has taken the registry away
             * (ctx->timer is nulled under ctx->lock); the event is, or
             * will be, freed by gf_timer_proc() -- by its shutdown loop
             * if still queued, after its callback if already dequeued --
             * and must not be touched here.
             */
            UNLOCK(&ctx->lock);
            return -1;
        }

        /* Keep ctx->lock while working under reg->lock: the destroy
         * cannot free reg before we release ctx->lock (see above).
         */
        pthread_mutex_lock(&reg->lock);
        {
            fired = event->fired;
            if (!fired)
                list_del(&event->list);
        }
        pthread_mutex_unlock(&reg->lock);
    }
    UNLOCK(&ctx->lock);

    if (!fired) {
        GF_FREE(event);
        return 0;
    }
    return -1;
}

static void *
gf_timer_proc(void *data)
{
    gf_timer_registry_t *reg = data;
    gf_timer_t *event = NULL;
    gf_timer_t *tmp = NULL;
    xlator_t *old_THIS = NULL;

    pthread_mutex_lock(&reg->lock);

    while (!reg->fin) {
        if (list_empty(&reg->active)) {
            pthread_cond_wait(&reg->cond, &reg->lock);
        } else {
            struct timespec now;

            timespec_now(&now);
            event = list_first_entry(&reg->active, gf_timer_t, list);
            if (TS(now) < TS(event->at)) {
                now = event->at;
                pthread_cond_timedwait(&reg->cond, &reg->lock, &now);
            } else {
                event->fired = _gf_true;
                list_del_init(&event->list);

                pthread_mutex_unlock(&reg->lock);

                old_THIS = NULL;
                if (event->xl) {
                    old_THIS = THIS;
                    THIS = event->xl;
                }
                event->callbk(event->data);
                GF_FREE(event);
                if (old_THIS) {
                    THIS = old_THIS;
                }

                pthread_mutex_lock(&reg->lock);
            }
        }
    }

    /* Do not call gf_timer_call_cancel(),
     * it will lead to deadlock
     */
    list_for_each_entry_safe(event, tmp, &reg->active, list)
    {
        list_del(&event->list);
        /* Events still queued here belong to owners that never cancelled
         * them. Their callbacks are deliberately not run: the ctx is being
         * destroyed and the objects they would act on are gone or going.
         * Whatever such an event carried (e.g. the rpc_clnt ref taken by
         * rpc_clnt_reconnect / call_bail when arming) is released by
         * nobody -- a pre-existing leak that only a cancel issued after
         * gf_timer_registry_destroy() can produce (in glfs_fini() nothing
         * cancels after that point; in a daemon this is process exit).
         * TODO(timer): a per-event release hook would let this loop release
         * what a queued event carries. Owners cancel during their fini, while
         * the registry is alive, and get a truthful 0 back.
         */
        GF_FREE(event);
    }

    pthread_mutex_unlock(&reg->lock);

    return NULL;
}

static gf_timer_registry_t *
gf_timer_registry_init(glusterfs_ctx_t *ctx)
{
    gf_timer_registry_t *reg = NULL;
    int ret = -1;
    pthread_condattr_t attr;

    LOCK(&ctx->lock);
    {
        reg = ctx->timer;
        if (reg) {
            UNLOCK(&ctx->lock);
            goto out;
        }
        reg = GF_CALLOC(1, sizeof(*reg), gf_common_mt_gf_timer_registry_t);
        if (!reg) {
            UNLOCK(&ctx->lock);
            goto out;
        }
        ctx->timer = reg;
        pthread_mutex_init(&reg->lock, NULL);
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&reg->cond, &attr);
        INIT_LIST_HEAD(&reg->active);
    }
    UNLOCK(&ctx->lock);
    ret = gf_thread_create(&reg->th, NULL, gf_timer_proc, reg, "timer");
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, ret, LG_MSG_PTHREAD_FAILED,
               "Thread creation failed");
    }

out:
    return reg;
}

void
gf_timer_registry_destroy(glusterfs_ctx_t *ctx)
{
    pthread_t thr_id;
    gf_timer_registry_t *reg = NULL;

    if (ctx == NULL)
        return;

    LOCK(&ctx->lock);
    {
        reg = ctx->timer;
        ctx->timer = NULL;
    }
    UNLOCK(&ctx->lock);

    if (!reg)
        return;

    thr_id = reg->th;

    pthread_mutex_lock(&reg->lock);

    reg->fin = 1;
    pthread_cond_signal(&reg->cond);

    pthread_mutex_unlock(&reg->lock);

    pthread_join(thr_id, NULL);

    pthread_cond_destroy(&reg->cond);
    pthread_mutex_destroy(&reg->lock);

    GF_FREE(reg);
}
