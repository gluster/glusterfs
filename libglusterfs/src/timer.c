/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "timer.h"
#include "logging.h"
#include "common-utils.h"
#include "globals.h"
#include "timespec.h"
#include "libglusterfs-messages.h"

/* fwd decl */
static gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *);

gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,
                     struct timespec delta,
                     gf_timer_cbk_t callbk,
                     void *data)
{
        gf_timer_registry_t *reg = NULL;
        gf_timer_t *event = NULL;
        gf_timer_t *trav = NULL;
        uint64_t at = 0;

        if ((ctx == NULL) || (ctx->cleanup_started))
        {
                gf_msg_callingfn ("timer", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "Either ctx is NULL or"
                                  " ctx cleanup started");
                return NULL;
        }

        reg = gf_timer_registry_init (ctx);

        if (!reg) {
                gf_msg_callingfn ("timer", GF_LOG_ERROR, 0,
                                  LG_MSG_TIMER_REGISTER_ERROR, "!reg");
                return NULL;
        }

        event = GF_CALLOC (1, sizeof (*event), gf_common_mt_gf_timer_t);
        if (!event) {
                return NULL;
        }
        timespec_now (&event->at);
        timespec_adjust_delta (&event->at, delta);
        at = TS (event->at);
        event->callbk = callbk;
        event->data = data;
        event->xl = THIS;
        LOCK (&reg->lock);
        {
                list_for_each_entry_reverse (trav, &reg->active, list) {
                        if (TS (trav->at) < at)
                                break;
                }
                list_add (&event->list, &trav->list);
        }
        UNLOCK (&reg->lock);
        return event;
}


int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
                      gf_timer_t *event)
{
        gf_timer_registry_t *reg = NULL;
        gf_boolean_t fired = _gf_false;

        if (ctx == NULL || event == NULL)
        {
                gf_msg_callingfn ("timer", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return 0;
        }

        if (ctx->cleanup_started) {
                gf_msg_callingfn ("timer", GF_LOG_INFO, 0,
                                  LG_MSG_CTX_CLEANUP_STARTED,
                                  "ctx cleanup started");
                return 0;
        }

        LOCK (&ctx->lock);
        {
                reg = ctx->timer;
        }
        UNLOCK (&ctx->lock);

        if (!reg) {
                /* This can happen when cleanup may have just started and
                 * gf_timer_registry_destroy() sets ctx->timer to NULL.
                 * Just bail out as success as gf_timer_proc() takes
                 * care of cleaning up the events.
                 */
                return 0;
        }

        LOCK (&reg->lock);
        {
                fired = event->fired;
                if (fired)
                        goto unlock;
                list_del (&event->list);
        }
unlock:
        UNLOCK (&reg->lock);

        if (!fired) {
                GF_FREE (event);
                return 0;
        }
        return -1;
}


static void *
gf_timer_proc (void *data)
{
        gf_timer_registry_t *reg = data;
        struct timespec sleepts;
        gf_timer_t *event = NULL;
        gf_timer_t *tmp = NULL;
        xlator_t   *old_THIS = NULL;

        while (!reg->fin) {
                uint64_t now;
                struct timespec now_ts;

                timespec_now (&now_ts);
                now = TS (now_ts);
                while (1) {
                        uint64_t at;
                        char need_cbk = 0;

                        /*
                         * This will be overridden with a shorter interval if
                         * there's an event scheduled sooner. That makes the
                         * system more responsive in most cases, but doesn't
                         * include the case where a timer is added while we're
                         * asleep. It's tempting to use pthread_cond_timedwait,
                         * with the caveat that we'd be relying on system time
                         * instead of monotonic time. That's a mess when the
                         * system time is adjusted.  Another alternative might
                         * be to use pthread_kill, but that will remain TBD for
                         * now.
                         */
                        sleepts.tv_sec = 1;
                        sleepts.tv_nsec = 0;

                        LOCK (&reg->lock);
                        {
                                /*
                                 * Using list_for_each and then always breaking
                                 * after the first iteration might seem strange,
                                 * but (unlike alternatives) is independent of
                                 * the underlying list implementation.
                                 */
                                list_for_each_entry_safe (event,
                                             tmp, &reg->active, list) {
                                        at = TS (event->at);
                                        if (now >= at) {
                                                need_cbk = 1;
                                                event->fired = _gf_true;
                                                list_del (&event->list);
                                        } else {
                                                uint64_t diff = now - at;

                                                if (diff < 1000000000) {
                                                        sleepts.tv_sec = 0;
                                                        sleepts.tv_nsec = diff;
                                                }
                                        }
                                        break;
                                }
                        }
                        UNLOCK (&reg->lock);
                        if (need_cbk) {
                                old_THIS = NULL;
                                if (event->xl) {
                                        old_THIS = THIS;
                                        THIS = event->xl;
                                }
                                event->callbk (event->data);
                                GF_FREE (event);
                                if (old_THIS) {
                                        THIS = old_THIS;
                                }
                        } else {
                                break;
                        }
                }
                nanosleep (&sleepts, NULL);
        }

        LOCK (&reg->lock);
        {
                /* Do not call gf_timer_call_cancel(),
                 * it will lead to deadlock
                 */
                list_for_each_entry_safe (event, tmp, &reg->active, list) {
                        list_del (&event->list);
                        GF_FREE (event);
                }
        }
        UNLOCK (&reg->lock);
        LOCK_DESTROY (&reg->lock);

        return NULL;
}


static gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *ctx)
{
        gf_timer_registry_t *reg = NULL;
        int ret = -1;

        LOCK (&ctx->lock);
        {
                reg = ctx->timer;
                if (reg) {
                        UNLOCK (&ctx->lock);
                        goto out;
                }
                reg = GF_CALLOC (1, sizeof (*reg),
                              gf_common_mt_gf_timer_registry_t);
                if (!reg) {
                        UNLOCK (&ctx->lock);
                        goto out;
                }
                ctx->timer = reg;
                LOCK_INIT (&reg->lock);
                INIT_LIST_HEAD (&reg->active);
        }
        UNLOCK (&ctx->lock);
        ret = gf_thread_create (&reg->th, NULL, gf_timer_proc, reg, "timer");
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, ret,
                        LG_MSG_PTHREAD_FAILED,
                        "Thread creation failed");
        }

out:
        return reg;
}


void
gf_timer_registry_destroy (glusterfs_ctx_t *ctx)
{
        pthread_t thr_id;
        gf_timer_registry_t *reg = NULL;

        if (ctx == NULL)
                return;

        LOCK (&ctx->lock);
        {
                reg = ctx->timer;
                ctx->timer = NULL;
        }
        UNLOCK (&ctx->lock);

        if (!reg)
                return;

        thr_id = reg->th;
        reg->fin = 1;
        pthread_join (thr_id, NULL);
        GF_FREE (reg);
}
