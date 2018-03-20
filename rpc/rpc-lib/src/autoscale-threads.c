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
#include "server.h"

void
rpcsvc_autoscale_threads (glusterfs_ctx_t *ctx, int incr, xlator_t *this)
{
        struct event_pool       *pool           = ctx->event_pool;
        server_conf_t           *conf           = this->private;
        int                      thread_count   = pool->eventthreadcount;

        pool->auto_thread_count += incr;
        (void) event_reconfigure_threads (pool, thread_count+incr);
        rpcsvc_ownthread_reconf (conf->rpc, pool->eventthreadcount);
}
