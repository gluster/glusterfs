/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "event.h"
#include "rpcsvc.h"

void
rpcsvc_autoscale_threads (glusterfs_ctx_t *ctx, rpcsvc_t *rpc, int incr)
{
        struct event_pool       *pool           = ctx->event_pool;
        int                      thread_count   = pool->eventthreadcount;

        pool->auto_thread_count += incr;
        (void) event_reconfigure_threads (pool, thread_count+incr);
        rpcsvc_ownthread_reconf (rpc, pool->eventthreadcount);
}
