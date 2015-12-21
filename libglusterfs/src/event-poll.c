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
#include "syscall.h"
#include "libglusterfs-messages.h"



struct event_slot_poll {
	int fd;
	int events;
	void *data;
	event_handler_t handler;
};


static int
event_register_poll (struct event_pool *event_pool, int fd,
                     event_handler_t handler,
                     void *data, int poll_in, int poll_out);


static int
__flush_fd (int fd, int idx, void *data,
            int poll_in, int poll_out, int poll_err)
{
        char buf[64];
        int ret = -1;

        if (!poll_in)
                return ret;

        do {
                ret = sys_read (fd, buf, 64);
                if (ret == -1 && errno != EAGAIN) {
                        gf_msg ("poll", GF_LOG_ERROR, errno,
                                LG_MSG_FILE_OP_FAILED, "read on %d returned "
                                "error", fd);
                }
        } while (ret == 64);

        return ret;
}


static int
__event_getindex (struct event_pool *event_pool, int fd, int idx)
{
        int  ret = -1;
        int  i = 0;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        /* lookup in used space based on index provided */
        if (idx > -1 && idx < event_pool->used) {
                if (event_pool->reg[idx].fd == fd) {
                        ret = idx;
                        goto out;
                }
        }

        /* search in used space, if lookup fails */
        for (i = 0; i < event_pool->used; i++) {
                if (event_pool->reg[i].fd == fd) {
                        ret = i;
                        break;
                }
        }

out:
        return ret;
}


static struct event_pool *
event_pool_new_poll (int count, int eventthreadcount)
{
        struct event_pool *event_pool = NULL;
        int                ret = -1;

        event_pool = GF_CALLOC (1, sizeof (*event_pool),
                                gf_common_mt_event_pool);

        if (!event_pool)
                return NULL;

        event_pool->count = count;
        event_pool->reg = GF_CALLOC (event_pool->count,
                                     sizeof (*event_pool->reg),
                                     gf_common_mt_reg);

        if (!event_pool->reg) {
                GF_FREE (event_pool);
                return NULL;
        }

        pthread_mutex_init (&event_pool->mutex, NULL);

        ret = pipe (event_pool->breaker);

        if (ret == -1) {
                gf_msg ("poll", GF_LOG_ERROR, errno, LG_MSG_PIPE_CREATE_FAILED,
                        "pipe creation failed");
                GF_FREE (event_pool->reg);
                GF_FREE (event_pool);
                return NULL;
        }

        ret = fcntl (event_pool->breaker[0], F_SETFL, O_NONBLOCK);
        if (ret == -1) {
                gf_msg ("poll", GF_LOG_ERROR, errno, LG_MSG_SET_PIPE_FAILED,
                        "could not set pipe to non blocking mode");
                sys_close (event_pool->breaker[0]);
                sys_close (event_pool->breaker[1]);
                event_pool->breaker[0] = event_pool->breaker[1] = -1;

                GF_FREE (event_pool->reg);
                GF_FREE (event_pool);
                return NULL;
        }

        ret = fcntl (event_pool->breaker[1], F_SETFL, O_NONBLOCK);
        if (ret == -1) {
                gf_msg ("poll", GF_LOG_ERROR, errno, LG_MSG_SET_PIPE_FAILED,
                        "could not set pipe to non blocking mode");

                sys_close (event_pool->breaker[0]);
                sys_close (event_pool->breaker[1]);
                event_pool->breaker[0] = event_pool->breaker[1] = -1;

                GF_FREE (event_pool->reg);
                GF_FREE (event_pool);
                return NULL;
        }

        ret = event_register_poll (event_pool, event_pool->breaker[0],
                                   __flush_fd, NULL, 1, 0);
        if (ret == -1) {
                gf_msg ("poll", GF_LOG_ERROR, 0, LG_MSG_REGISTER_PIPE_FAILED,
                        "could not register pipe fd with poll event loop");
                sys_close (event_pool->breaker[0]);
                sys_close (event_pool->breaker[1]);
                event_pool->breaker[0] = event_pool->breaker[1] = -1;

                GF_FREE (event_pool->reg);
                GF_FREE (event_pool);
                return NULL;
        }

        if (eventthreadcount > 1) {
                gf_msg ("poll", GF_LOG_INFO, 0,
                        LG_MSG_POLL_IGNORE_MULTIPLE_THREADS, "Currently poll "
                        "does not use multiple event processing threads, "
                        "thread count (%d) ignored", eventthreadcount);
        }

        return event_pool;
}


static int
event_register_poll (struct event_pool *event_pool, int fd,
                     event_handler_t handler,
                     void *data, int poll_in, int poll_out)
{
        int idx = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                if (event_pool->count == event_pool->used)
                {
                        event_pool->count += 256;

                        event_pool->reg = GF_REALLOC (event_pool->reg,
                                                      event_pool->count *
                                                      sizeof (*event_pool->reg));
                        if (!event_pool->reg)
                                goto unlock;
                }

                idx = event_pool->used++;

                event_pool->reg[idx].fd = fd;
                event_pool->reg[idx].events = POLLPRI;
                event_pool->reg[idx].handler = handler;
                event_pool->reg[idx].data = data;

                switch (poll_in) {
                case 1:
                        event_pool->reg[idx].events |= POLLIN;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~POLLIN;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        gf_msg ("poll", GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_POLL_IN,
                                "invalid poll_in value %d", poll_in);
                        break;
                }

                switch (poll_out) {
                case 1:
                        event_pool->reg[idx].events |= POLLOUT;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~POLLOUT;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        gf_msg ("poll", GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_POLL_OUT,
                                "invalid poll_out value %d", poll_out);
                        break;
                }

                event_pool->changed = 1;

        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

out:
        return idx;
}


static int
event_unregister_poll (struct event_pool *event_pool, int fd, int idx_hint)
{
        int idx = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                idx = __event_getindex (event_pool, fd, idx_hint);

                if (idx == -1) {
                        gf_msg ("poll", GF_LOG_ERROR, 0, LG_MSG_INDEX_NOT_FOUND,
                                "index not found for fd=%d (idx_hint=%d)",
                                fd, idx_hint);
                        errno = ENOENT;
                        goto unlock;
                }

                event_pool->reg[idx] =  event_pool->reg[--event_pool->used];
                event_pool->changed = 1;
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

out:
        return idx;
}


static int
event_unregister_close_poll (struct event_pool *event_pool, int fd,
			     int idx_hint)
{
	int ret = -1;

	ret = event_unregister_poll (event_pool, fd, idx_hint);

	sys_close (fd);

        return ret;
}


static int
event_select_on_poll (struct event_pool *event_pool, int fd, int idx_hint,
                      int poll_in, int poll_out)
{
        int idx = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                idx = __event_getindex (event_pool, fd, idx_hint);

                if (idx == -1) {
                        gf_msg ("poll", GF_LOG_ERROR, 0, LG_MSG_INDEX_NOT_FOUND,
                                "index not found for fd=%d (idx_hint=%d)",
                                fd, idx_hint);
                        errno = ENOENT;
                        goto unlock;
                }

                switch (poll_in) {
                case 1:
                        event_pool->reg[idx].events |= POLLIN;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~POLLIN;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        /* TODO: log error */
                        break;
                }

                switch (poll_out) {
                case 1:
                        event_pool->reg[idx].events |= POLLOUT;
                        break;
                case 0:
                        event_pool->reg[idx].events &= ~POLLOUT;
                        break;
                case -1:
                        /* do nothing */
                        break;
                default:
                        /* TODO: log error */
                        break;
                }

                if (poll_in + poll_out > -2)
                        event_pool->changed = 1;
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

out:
        return idx;
}


static int
event_dispatch_poll_handler (struct event_pool *event_pool,
                             struct pollfd *ufds, int i)
{
        event_handler_t  handler = NULL;
        void            *data = NULL;
        int              idx = -1;
        int              ret = 0;

        handler = NULL;
        data    = NULL;

        pthread_mutex_lock (&event_pool->mutex);
        {
                idx = __event_getindex (event_pool, ufds[i].fd, i);

                if (idx == -1) {
                        gf_msg ("poll", GF_LOG_ERROR, 0,
                                LG_MSG_INDEX_NOT_FOUND, "index not found for "
                                "fd=%d (idx_hint=%d)", ufds[i].fd, i);
                        goto unlock;
                }

                handler = event_pool->reg[idx].handler;
                data = event_pool->reg[idx].data;
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

        if (handler)
                ret = handler (ufds[i].fd, idx, data,
                               (ufds[i].revents & (POLLIN|POLLPRI)),
                               (ufds[i].revents & (POLLOUT)),
                               (ufds[i].revents & (POLLERR|POLLHUP|POLLNVAL)));

        return ret;
}


static int
event_dispatch_poll_resize (struct event_pool *event_pool,
                            struct pollfd *ufds, int size)
{
        int              i = 0;

        pthread_mutex_lock (&event_pool->mutex);
        {
                if (event_pool->changed == 0) {
                        goto unlock;
                }

                if (event_pool->used > event_pool->evcache_size) {
                        GF_FREE (event_pool->evcache);

                        event_pool->evcache = ufds = NULL;

                        event_pool->evcache_size = event_pool->used;

                        ufds = GF_CALLOC (sizeof (struct pollfd),
                                          event_pool->evcache_size,
                                          gf_common_mt_pollfd);
                        if (!ufds)
                                goto unlock;
                        event_pool->evcache = ufds;
                }

                for (i = 0; i < event_pool->used; i++) {
                        ufds[i].fd = event_pool->reg[i].fd;
                        ufds[i].events = event_pool->reg[i].events;
                        ufds[i].revents = 0;
                }

                size = i;
        }
unlock:
        pthread_mutex_unlock (&event_pool->mutex);

        return size;
}


static int
event_dispatch_poll (struct event_pool *event_pool)
{
        struct pollfd   *ufds = NULL;
        int              size = 0;
        int              i = 0;
        int              ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                event_pool->activethreadcount = 1;
        }
        pthread_mutex_unlock (&event_pool->mutex);

        while (1) {
                pthread_mutex_lock (&event_pool->mutex);
                {
                        if (event_pool->destroy == 1) {
                                event_pool->activethreadcount = 0;
                                pthread_cond_broadcast (&event_pool->cond);
                                pthread_mutex_unlock (&event_pool->mutex);
                                return 0;
                        }
                }
                pthread_mutex_unlock (&event_pool->mutex);

                size = event_dispatch_poll_resize (event_pool, ufds, size);
                ufds = event_pool->evcache;

                ret = poll (ufds, size, 1);

                if (ret == 0)
                        /* timeout */
                        continue;

                if (ret == -1 && errno == EINTR)
                        /* sys call */
                        continue;

                for (i = 0; i < size; i++) {
                        if (!ufds[i].revents)
                                continue;

                        event_dispatch_poll_handler (event_pool, ufds, i);
                }
        }

out:
        return -1;
}

int
event_reconfigure_threads_poll (struct event_pool *event_pool, int value)
{
        /* No-op for poll */

        return 0;
}

/* This function is the destructor for the event_pool data structure
 * Should be called only after poller_threads_destroy() is called,
 * else will lead to crashes.
 */
static int
event_pool_destroy_poll (struct event_pool *event_pool)
{
        int ret = 0;

        ret = sys_close (event_pool->breaker[0]);
        if (ret)
                return ret;

        ret = sys_close (event_pool->breaker[1]);
        if (ret)
                return ret;

        event_pool->breaker[0] = event_pool->breaker[1] = -1;

        GF_FREE (event_pool->reg);
        GF_FREE (event_pool);

        return ret;
}

struct event_ops event_ops_poll = {
        .new                    = event_pool_new_poll,
        .event_register         = event_register_poll,
        .event_select_on        = event_select_on_poll,
        .event_unregister       = event_unregister_poll,
        .event_unregister_close = event_unregister_close_poll,
        .event_dispatch         = event_dispatch_poll,
        .event_reconfigure_threads = event_reconfigure_threads_poll,
        .event_pool_destroy     = event_pool_destroy_poll
};
