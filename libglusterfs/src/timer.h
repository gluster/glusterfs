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

#include "glusterfs.h"
#include "xlator.h"
#include <sys/time.h>
#include <pthread.h>

typedef void (*gf_timer_cbk_t) (void *);

struct _gf_timer {
        struct _gf_timer *next, *prev;
        struct timespec   at;
        gf_timer_cbk_t    callbk;
        void             *data;
        xlator_t         *xl;
	gf_boolean_t      fired;
};

struct _gf_timer_registry {
        pthread_t        th;
        char             fin;
        struct _gf_timer active;
        gf_lock_t        lock;
};

typedef struct _gf_timer gf_timer_t;
typedef struct _gf_timer_registry gf_timer_registry_t;

gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,
                     struct timespec delta,
                     gf_timer_cbk_t cbk,
                     void *data);

int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
                      gf_timer_t *event);

void
gf_timer_registry_destroy (glusterfs_ctx_t *ctx);
#endif /* _TIMER_H */
