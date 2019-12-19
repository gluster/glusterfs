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
#include "glusterfs/common-utils.h"
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

    if (ctx->cleanup_started) {
        gf_msg_callingfn("timer", GF_LOG_INFO, 0, LG_MSG_CTX_CLEANUP_STARTED,
                         "ctx cleanup started");
        return -1;
    }

    LOCK(&ctx->lock);
    {
        reg = ctx->timer;
    }
    UNLOCK(&ctx->lock);

    if (!reg) {
        /* This can happen when cleanup may have just started and
         * gf_timer_registry_destroy() sets ctx->timer to NULL.
         * gf_timer_proc() takes care of cleaning up the events.
         */
        return -1;
    }

    pthread_mutex_lock(&reg->lock);
    {
        fired = event->fired;
        if (fired)
            goto unlock;
        list_del(&event->list);
    }
unlock:
    pthread_mutex_unlock(&reg->lock);

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
        /* TODO Possible resource leak
         * Before freeing the event, we need to call the respective
         * event functions and free any resources.
         * For example, In case of rpc_clnt_reconnect, we need to
         * unref rpc object which was taken when added to timer
         * wheel.
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
