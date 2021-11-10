/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _TIMER_H
#define _TIMER_H

#include "glusterfs/glusterfs.h"
#include "glusterfs/xlator.h"
#include <sys/time.h>

#if defined(GF_TIMERFD_TIMERS)
#include <sys/timerfd.h>
#else /* not GF_TIMERFD_TIMERS */
#include <pthread.h>
#endif /* GF_TIMERFD_TIMERS */

typedef void (*gf_timer_cbk_t)(void *);

#if defined(GF_TIMERFD_TIMERS)

struct _gf_timer {
    gf_timer_cbk_t callbk;
    glusterfs_ctx_t *ctx;
    gf_boolean_t fired;
    xlator_t *xl;
    void *data;
    int idx;
    int fd;
};

typedef struct _gf_timer gf_timer_t;

#else /* not GF_TIMERFD_TIMERS */

struct _gf_timer {
    union {
        struct list_head list;
        struct {
            struct _gf_timer *next;
            struct _gf_timer *prev;
        };
    };
    struct timespec at;
    gf_timer_cbk_t callbk;
    void *data;
    xlator_t *xl;
    gf_boolean_t fired;
};

struct _gf_timer_registry {
    struct list_head active;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t th;
    char fin;
};

typedef struct _gf_timer gf_timer_t;
typedef struct _gf_timer_registry gf_timer_registry_t;

#endif /* GF_TIMERFD_TIMERS */

gf_timer_t *
gf_timer_call_after(glusterfs_ctx_t *ctx, struct timespec delta,
                    gf_timer_cbk_t cbk, void *data);

int32_t
gf_timer_call_cancel(glusterfs_ctx_t *ctx, gf_timer_t *event);

void
gf_timer_registry_destroy(glusterfs_ctx_t *ctx);

#endif /* _TIMER_H */
