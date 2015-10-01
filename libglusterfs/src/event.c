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
#include "libglusterfs-messages.h"
#include "syscall.h"



struct event_pool *
event_pool_new (int count, int eventthreadcount)
{
        struct event_pool *event_pool = NULL;
	extern struct event_ops event_ops_poll;

#ifdef HAVE_SYS_EPOLL_H
	extern struct event_ops event_ops_epoll;

        event_pool = event_ops_epoll.new (count, eventthreadcount);

        if (event_pool) {
                event_pool->ops = &event_ops_epoll;
        } else {
                gf_msg ("event", GF_LOG_WARNING, 0, LG_MSG_FALLBACK_TO_POLL,
                        "falling back to poll based event handling");
        }
#endif

        if (!event_pool) {
                event_pool = event_ops_poll.new (count, eventthreadcount);

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
event_unregister_close (struct event_pool *event_pool, int fd, int idx)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        ret = event_pool->ops->event_unregister_close (event_pool, fd, idx);

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
        if (ret)
                goto out;

out:
        return ret;
}

int
event_reconfigure_threads (struct event_pool *event_pool, int value)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        /* call event refresh function */
        ret = event_pool->ops->event_reconfigure_threads (event_pool,
                                                          value);

out:
        return ret;
}

int
event_pool_destroy (struct event_pool *event_pool)
{
        int ret = -1;
        int destroy = 0, activethreadcount = 0;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        pthread_mutex_lock (&event_pool->mutex);
        {
                destroy = event_pool->destroy;
                activethreadcount = event_pool->activethreadcount;
        }
        pthread_mutex_unlock (&event_pool->mutex);

        if (!destroy || (activethreadcount > 0))
                goto out;

        ret = event_pool->ops->event_pool_destroy (event_pool);
out:
        return ret;
}

int
poller_destroy_handler (int fd, int idx, void *data,
                       int poll_out, int poll_in, int poll_err)
{
        int readfd = -1;
        char buf = '\0';

        readfd = *(int *)data;
        if (readfd < 0)
                return -1;

        while (sys_read (readfd, &buf, 1) > 0) {
        }
        return 0;
}

/* This function destroys all the poller threads.
 * Note: to be called before event_pool_destroy is called.
 * The order in which cleaning is performed:
 * - Register a pipe fd(this is for waking threads in poll()/epoll_wait())
 * - Set the destroy mode, which this no new event registration will succede
 * - Reconfigure the thread count to 0(this will succede only in destroy mode)
 * - Wake up all the threads in poll() or epoll_wait(), so that they can
 *   destroy themselves.
 * - Wait for the thread to join(which will happen only after all the other
 *   threads are destroyed)
 */
int
event_dispatch_destroy (struct event_pool *event_pool)
{
        int  ret     = -1;
        int  fd[2]   = {-1};
        int  idx     = -1;
        int  flags   = 0;
        struct timespec   sleep_till = {0, };

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

        ret = pipe (fd);
        if (ret < 0)
                goto out;

        /* Make the read end of the pipe nonblocking */
        flags = fcntl(fd[0], F_GETFL);
        flags |= O_NONBLOCK;
        ret = fcntl(fd[0], F_SETFL, flags);
        if (ret < 0)
                goto out;

        /* Make the write end of the pipe nonblocking */
        flags = fcntl(fd[1], F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(fd[1], F_SETFL, flags);
        if (ret < 0)
                goto out;

        /* From the main thread register an event on the pipe fd[0],
         */
        idx = event_register (event_pool, fd[0], poller_destroy_handler,
                              &fd[1], 1, 0);
        if (idx < 0)
                goto out;

        /* Enter the destroy mode first, set this before reconfiguring to 0
         * threads, to prevent further reconfigure to thread count > 0.
         */
        pthread_mutex_lock (&event_pool->mutex);
        {
                event_pool->destroy = 1;
        }
        pthread_mutex_unlock (&event_pool->mutex);

        ret = event_reconfigure_threads (event_pool, 0);
        if (ret < 0)
                goto out;

        /* Write something onto the write end of the pipe(fd[1]) so that
         * poll wakes up and calls the handler, poller_destroy_handler()
         */
        pthread_mutex_lock (&event_pool->mutex);
        {
                /* Write to pipe(fd[1]) and then wait for 1 second or until
                 * a poller thread that is dying, broadcasts. Make sure we
                 * do not loop forever by limiting to 10 retries
                 */
                int retry = 0;

                while (event_pool->activethreadcount > 0 && retry++ < 10) {
                        if (sys_write (fd[1], "dummy", 6) == -1)
                                break;
                        sleep_till.tv_sec = time (NULL) + 1;
                        ret = pthread_cond_timedwait (&event_pool->cond,
                                                      &event_pool->mutex,
                                                      &sleep_till);
                }
        }
        pthread_mutex_unlock (&event_pool->mutex);

        ret = event_unregister (event_pool, fd[0], idx);

 out:
        if (fd[0] != -1)
                sys_close (fd[0]);
        if (fd[1] != -1)
                sys_close (fd[1]);

        return ret;
}
