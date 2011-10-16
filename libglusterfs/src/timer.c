/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "timer.h"
#include "logging.h"
#include "common-utils.h"
#include "globals.h"

#define TS(tv) ((((unsigned long long) tv.tv_sec) * 1000000) + (tv.tv_usec))

gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,
                     struct timeval delta,
                     gf_timer_cbk_t callbk,
                     void *data)
{
        gf_timer_registry_t *reg = NULL;
        gf_timer_t *event = NULL;
        gf_timer_t *trav = NULL;
        unsigned long long at = 0L;

        if (ctx == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }

        reg = gf_timer_registry_init (ctx);

        if (!reg) {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "!reg");
                return NULL;
        }

        event = GF_CALLOC (1, sizeof (*event), gf_common_mt_gf_timer_t);
        if (!event) {
                return NULL;
        }
        gettimeofday (&event->at, NULL);
        event->at.tv_usec = ((event->at.tv_usec + delta.tv_usec) % 1000000);
        event->at.tv_sec += ((event->at.tv_usec + delta.tv_usec) / 1000000);
        event->at.tv_sec += delta.tv_sec;
        at = TS (event->at);
        event->callbk = callbk;
        event->data = data;
        event->xl = THIS;
        pthread_mutex_lock (&reg->lock);
        {
                trav = reg->active.prev;
                while (trav != &reg->active) {
                        if (TS (trav->at) < at)
                                break;
                        trav = trav->prev;
                }
                event->prev = trav;
                event->next = event->prev->next;
                event->prev->next = event;
                event->next->prev = event;
        }
        pthread_mutex_unlock (&reg->lock);
        return event;
}

int32_t
gf_timer_call_stale (gf_timer_registry_t *reg,
                     gf_timer_t *event)
{
        if (reg == NULL || event == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return 0;
        }

        event->next->prev = event->prev;
        event->prev->next = event->next;
        event->next = &reg->stale;
        event->prev = event->next->prev;
        event->next->prev = event;
        event->prev->next = event;

        return 0;
}

int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
                      gf_timer_t *event)
{
        gf_timer_registry_t *reg = NULL;

        if (ctx == NULL || event == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return 0;
        }

        reg = gf_timer_registry_init (ctx);
        if (!reg) {
                gf_log ("timer", GF_LOG_ERROR, "!reg");
                GF_FREE (event);
                return 0;
        }

        pthread_mutex_lock (&reg->lock);
        {
                event->next->prev = event->prev;
                event->prev->next = event->next;
        }
        pthread_mutex_unlock (&reg->lock);

        GF_FREE (event);
        return 0;
}

void *
gf_timer_proc (void *ctx)
{
        gf_timer_registry_t *reg = NULL;
	const struct timespec sleepts = {.tv_sec = 1, .tv_nsec = 0, };

        if (ctx == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }

        reg = gf_timer_registry_init (ctx);
        if (!reg) {
                gf_log ("timer", GF_LOG_ERROR, "!reg");
                return NULL;
        }

        while (!reg->fin) {
                unsigned long long now;
                struct timeval now_tv;
                gf_timer_t *event = NULL;

                gettimeofday (&now_tv, NULL);
                now = TS (now_tv);
                while (1) {
                        unsigned long long at;
                        char need_cbk = 0;

                        pthread_mutex_lock (&reg->lock);
                        {
                                event = reg->active.next;
                                at = TS (event->at);
                                if (event != &reg->active && now >= at) {
                                        need_cbk = 1;
                                        gf_timer_call_stale (reg, event);
                                }
                        }
                        pthread_mutex_unlock (&reg->lock);
                        if (event->xl)
                                THIS = event->xl;
                        if (need_cbk)
                                event->callbk  (event->data);

                        else
                                break;
                }
                nanosleep (&sleepts, NULL);
        }

        pthread_mutex_lock (&reg->lock);
        {
                while (reg->active.next != &reg->active) {
                        gf_timer_call_cancel (ctx, reg->active.next);
                }

                while (reg->stale.next != &reg->stale) {
                        gf_timer_call_cancel (ctx, reg->stale.next);
                }
        }
        pthread_mutex_unlock (&reg->lock);
        pthread_mutex_destroy (&reg->lock);
        GF_FREE (((glusterfs_ctx_t *)ctx)->timer);

        return NULL;
}

gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *ctx)
{
        if (ctx == NULL) {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }

        if (!ctx->timer) {
                gf_timer_registry_t *reg = NULL;

                reg = GF_CALLOC (1, sizeof (*reg),
                                 gf_common_mt_gf_timer_registry_t);
                if (!reg)
                        goto out;

                pthread_mutex_init (&reg->lock, NULL);
                reg->active.next = &reg->active;
                reg->active.prev = &reg->active;
                reg->stale.next = &reg->stale;
                reg->stale.prev = &reg->stale;

                ctx->timer = reg;
                pthread_create (&reg->th, NULL, gf_timer_proc, ctx);
        }
out:
        return ctx->timer;
}
