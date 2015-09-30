/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/poll.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "logging.h"
#include "event.h"
#include "mem-pool.h"
#include "common-utils.h"

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif



struct event_pool *
event_pool_new (int count)
{
        struct event_pool *event_pool = NULL;
        extern struct event_ops event_ops_poll;

#ifdef HAVE_SYS_EPOLL_H
        extern struct event_ops event_ops_epoll;

        event_pool = event_ops_epoll.new (count);

        if (event_pool) {
                event_pool->ops = &event_ops_epoll;
        } else {
                gf_log ("event", GF_LOG_WARNING,
                        "falling back to poll based event handling");
        }
#endif

        if (!event_pool) {
                event_pool = event_ops_poll.new (count);

                if (event_pool)
                        event_pool->ops = &event_ops_poll;
        }

        return event_pool;
}


int
event_register (struct event_pool *event_pool, int fd,
                event_handler_t handler,
                void *data, int poll_in, int poll_out)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        ret = event_pool->ops->event_register (event_pool, fd, handler, data,
                                               poll_in, poll_out);
out:
        return ret;
}


int
event_unregister (struct event_pool *event_pool, int fd, int idx)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        ret = event_pool->ops->event_unregister (event_pool, fd, idx);

out:
        return ret;
}


int
event_select_on (struct event_pool *event_pool, int fd, int idx_hint,
                 int poll_in, int poll_out)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        ret = event_pool->ops->event_select_on (event_pool, fd, idx_hint,
                                                poll_in, poll_out);
out:
        return ret;
}


int
event_dispatch (struct event_pool *event_pool)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        ret = event_pool->ops->event_dispatch (event_pool);

out:
        return ret;
}
