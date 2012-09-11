/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
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


#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>


static int
__event_getindex (struct event_pool *event_pool, int fd, int idx)
{
        int  ret = -1;
        int  i = 0;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        if (idx > -1 && idx < event_pool->used) {
                if (event_pool->reg[idx].fd == fd)
                        ret = idx;
        }

        for (i=0; ret == -1 && i<event_pool->used; i++) {
                if (event_pool->reg[i].fd == fd) {
                        ret = i;
                        break;
                }
        }

out:
        return ret;
}


static struct event_pool *
event_pool_new_epoll (int count)
{
        struct event_pool *event_pool = NULL;
        int                epfd = -1;

        event_pool = GF_CALLOC (1, sizeof (*event_pool),
                                gf_common_mt_event_pool);

        if (!event_pool)
                goto out;

        event_pool->count = count;
        event_pool->reg = GF_CALLOC (event_pool->count,
                                     sizeof (*event_pool->reg),
                                     gf_common_mt_reg);

        if (!event_pool->reg) {
                GF_FREE (event_pool);
                event_pool = NULL;
                goto out;
        }

        epfd = epoll_create (count);

        if (epfd == -1) {
                gf_log ("epoll", GF_LOG_ERROR, "epoll fd creation failed (%s)",
                        strerror (errno));
                GF_FREE (event_pool->reg);
                GF_FREE (event_pool);
                event_pool = NULL;
                goto out;
        }

        event_pool->fd = epfd;

        event_pool->count = count;

        pthread_mutex_init (&event_pool->mutex, NULL);
        pthread_cond_init (&event_pool->cond, NULL);

out:
        return event_pool;
}


int
event_register_epoll (struct event_pool *event_pool, int fd,
                      event_handler_t handler,
                      void *data, int poll_in, int poll_out)
{
        int                 idx = -1;
        int                 ret = -1;
        struct epoll_event  epoll_event = {0, };
        struct event_data  *ev_data = (void *)&epoll_event.data;


        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                if (event_pool->count == event_pool->used) {
                        event_pool->count *= 2;

                        event_pool->reg = GF_REALLOC (event_pool->reg,
                                                      event_pool->count *
                                                      sizeof (*event_pool->reg));

                        if (!event_pool->reg) {
                                gf_log ("epoll", GF_LOG_ERROR,
                                        "event registry re-allocation failed");
                                goto unlock;
                        }
                }

                idx = event_pool->used;
                event_pool->used++;

                event_pool->reg[idx].fd = fd;
                event_pool->reg[idx].events = EPOLLPRI;
                event_pool->reg[idx].handler = handler;
                event_pool->reg[idx].data = data;

                switch (poll_in) {
                case 1:
                        event_pool->reg[idx].events |= EPOLLIN;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~EPOLLIN;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        gf_log ("epoll", GF_LOG_ERROR,
                                "invalid poll_in value %d", poll_in);
                        break;
                }

                switch (poll_out) {
                case 1:
                        event_pool->reg[idx].events |= EPOLLOUT;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~EPOLLOUT;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        gf_log ("epoll", GF_LOG_ERROR,
                                "invalid poll_out value %d", poll_out);
                        break;
                }

                event_pool->changed = 1;

                epoll_event.events = event_pool->reg[idx].events;
                ev_data->fd = fd;
                ev_data->idx = idx;

                ret = epoll_ctl (event_pool->fd, EPOLL_CTL_ADD, fd,
                                 &epoll_event);

                if (ret == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "failed to add fd(=%d) to epoll fd(=%d) (%s)",
                                fd, event_pool->fd, strerror (errno));
                        goto unlock;
                }

                pthread_cond_broadcast (&event_pool->cond);
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

out:
        return ret;
}


static int
event_unregister_epoll (struct event_pool *event_pool, int fd, int idx_hint)
{
        int  idx = -1;
        int  ret = -1;

        struct epoll_event epoll_event = {0, };
        struct event_data *ev_data = (void *)&epoll_event.data;
        int                lastidx = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                idx = __event_getindex (event_pool, fd, idx_hint);

                if (idx == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "index not found for fd=%d (idx_hint=%d)",
                                fd, idx_hint);
                        errno = ENOENT;
                        goto unlock;
                }

                ret = epoll_ctl (event_pool->fd, EPOLL_CTL_DEL, fd, NULL);

                /* if ret is -1, this array member should never be accessed */
                /* if it is 0, the array member might be used by idx_cache
                 * in which case the member should not be accessed till
                 * it is reallocated
                 */

                event_pool->reg[idx].fd = -1;

                if (ret == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "fail to del fd(=%d) from epoll fd(=%d) (%s)",
                                fd, event_pool->fd, strerror (errno));
                        goto unlock;
                }

                lastidx = event_pool->used - 1;
                if (lastidx == idx) {
                        event_pool->used--;
                        goto unlock;
                }

                epoll_event.events = event_pool->reg[lastidx].events;
                ev_data->fd = event_pool->reg[lastidx].fd;
                ev_data->idx = idx;

                ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, ev_data->fd,
                                 &epoll_event);
                if (ret == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "fail to modify fd(=%d) index %d to %d (%s)",
                                ev_data->fd, event_pool->used, idx,
                                strerror (errno));
                        goto unlock;
                }

                /* just replace the unregistered idx by last one */
                event_pool->reg[idx] = event_pool->reg[lastidx];
                event_pool->used--;
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

out:
        return ret;
}


static int
event_select_on_epoll (struct event_pool *event_pool, int fd, int idx_hint,
                       int poll_in, int poll_out)
{
        int idx = -1;
        int ret = -1;

        struct epoll_event epoll_event = {0, };
        struct event_data *ev_data = (void *)&epoll_event.data;


        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                idx = __event_getindex (event_pool, fd, idx_hint);

                if (idx == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "index not found for fd=%d (idx_hint=%d)",
                                fd, idx_hint);
                        errno = ENOENT;
                        goto unlock;
                }

                switch (poll_in) {
                case 1:
                        event_pool->reg[idx].events |= EPOLLIN;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~EPOLLIN;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        gf_log ("epoll", GF_LOG_ERROR,
                                "invalid poll_in value %d", poll_in);
                        break;
                }

                switch (poll_out) {
                case 1:
                        event_pool->reg[idx].events |= EPOLLOUT;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~EPOLLOUT;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        gf_log ("epoll", GF_LOG_ERROR,
                                "invalid poll_out value %d", poll_out);
                        break;
                }

                epoll_event.events = event_pool->reg[idx].events;
                ev_data->fd = fd;
                ev_data->idx = idx;

                ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, fd,
                                 &epoll_event);
                if (ret == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "failed to modify fd(=%d) events to %d",
                                fd, epoll_event.events);
                }
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

out:
        return ret;
}


static int
event_dispatch_epoll_handler (struct event_pool *event_pool,
                              struct epoll_event *events, int i)
{
        struct event_data  *event_data = NULL;
        event_handler_t     handler = NULL;
        void               *data = NULL;
        int                 idx = -1;
        int                 ret = -1;


        event_data = (void *)&events[i].data;
        handler = NULL;
        data = NULL;

        pthread_mutex_lock (&event_pool->mutex);
        {
                idx = __event_getindex (event_pool, event_data->fd,
                                        event_data->idx);

                if (idx == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "index not found for fd(=%d) (idx_hint=%d)",
                                event_data->fd, event_data->idx);
                        goto unlock;
                }

                handler = event_pool->reg[idx].handler;
                data = event_pool->reg[idx].data;
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

        if (handler)
                ret = handler (event_data->fd, event_data->idx, data,
                               (events[i].events & (EPOLLIN|EPOLLPRI)),
                               (events[i].events & (EPOLLOUT)),
                               (events[i].events & (EPOLLERR|EPOLLHUP)));
        return ret;
}


static int
event_dispatch_epoll (struct event_pool *event_pool)
{
        struct epoll_event *events = NULL;
        int                 size = 0;
        int                 i = 0;
        int                 ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        while (1) {
                pthread_mutex_lock (&event_pool->mutex);
                {
                        while (event_pool->used == 0)
                                pthread_cond_wait (&event_pool->cond,
                                                   &event_pool->mutex);

                        if (event_pool->used > event_pool->evcache_size) {
                                GF_FREE (event_pool->evcache);

                                event_pool->evcache = events = NULL;

                                event_pool->evcache_size =
                                        event_pool->used + 256;

                                events = GF_CALLOC (event_pool->evcache_size,
                                                    sizeof (struct epoll_event),
                                                    gf_common_mt_epoll_event);
                                if (!events)
                                        break;

                                event_pool->evcache = events;
                        }
                }
                pthread_mutex_unlock (&event_pool->mutex);

                ret = epoll_wait (event_pool->fd, event_pool->evcache,
                                  event_pool->evcache_size, -1);

                if (ret == 0)
                        /* timeout */
                        continue;

                if (ret == -1 && errno == EINTR)
                        /* sys call */
                        continue;

                size = ret;

                for (i = 0; i < size; i++) {
                        if (!events || !events[i].events)
                                continue;

                        ret = event_dispatch_epoll_handler (event_pool,
                                                            events, i);
                }
        }

out:
        return ret;
}


struct event_ops event_ops_epoll = {
        .new              = event_pool_new_epoll,
        .event_register   = event_register_epoll,
        .event_select_on  = event_select_on_epoll,
        .event_unregister = event_unregister_epoll,
        .event_dispatch   = event_dispatch_epoll
};

#endif
