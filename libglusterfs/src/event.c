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

#include "glusterfs/gf-event.h"
#include "glusterfs/timespec.h"
#include "glusterfs/common-utils.h"
#include "glusterfs/libglusterfs-messages.h"
#include "glusterfs/syscall.h"

struct event_pool *
gf_event_pool_new(int count, int eventthreadcount)
{
    struct event_pool *event_pool = NULL;
    extern struct event_ops event_ops_poll;

#ifdef HAVE_SYS_EPOLL_H
    extern struct event_ops event_ops_epoll;

    event_pool = event_ops_epoll.new(count, eventthreadcount);

    if (event_pool) {
        event_pool->ops = &event_ops_epoll;
    } else {
        gf_msg("event", GF_LOG_WARNING, 0, LG_MSG_FALLBACK_TO_POLL,
               "falling back to poll based event handling");
    }
#endif

    if (!event_pool) {
        event_pool = event_ops_poll.new(count, eventthreadcount);

        if (event_pool)
            event_pool->ops = &event_ops_poll;
    }

    return event_pool;
}

int
gf_event_register(struct event_pool *event_pool, int fd,
                  event_handler_t handler, void *data, int poll_in,
                  int poll_out, char notify_poller_death)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    ret = event_pool->ops->event_register(
        event_pool, fd, handler, data, poll_in, poll_out, notify_poller_death);
out:
    return ret;
}

int
gf_event_unregister(struct event_pool *event_pool, int fd, int idx)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    ret = event_pool->ops->event_unregister(event_pool, fd, idx);

out:
    return ret;
}

int
gf_event_unregister_close(struct event_pool *event_pool, int fd, int idx)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    ret = event_pool->ops->event_unregister_close(event_pool, fd, idx);

out:
    return ret;
}

int
gf_event_select_on(struct event_pool *event_pool, int fd, int idx_hint,
                   int poll_in, int poll_out)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    ret = event_pool->ops->event_select_on(event_pool, fd, idx_hint, poll_in,
                                           poll_out);
out:
    return ret;
}

int
gf_event_dispatch(struct event_pool *event_pool)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    ret = event_pool->ops->event_dispatch(event_pool);
    if (ret)
        goto out;

out:
    return ret;
}

int
gf_event_reconfigure_threads(struct event_pool *event_pool, int value)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    /* call event refresh function */
    ret = event_pool->ops->event_reconfigure_threads(event_pool, value);

out:
    return ret;
}

int
gf_event_pool_destroy(struct event_pool *event_pool)
{
    int ret = -1;
    int destroy = 0, activethreadcount = 0;

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    pthread_mutex_lock(&event_pool->mutex);
    {
        destroy = event_pool->destroy;
        activethreadcount = event_pool->activethreadcount;
    }
    pthread_mutex_unlock(&event_pool->mutex);

    if (!destroy || (activethreadcount > 0)) {
        goto out;
    }

    ret = event_pool->ops->event_pool_destroy(event_pool);
out:
    return ret;
}

void
poller_destroy_handler(int fd, int idx, int gen, void *data, int poll_out,
                       int poll_in, int poll_err, char event_thread_exit)
{
    struct event_destroy_data *destroy = NULL;
    int readfd = -1;
    char buf = '\0';

    destroy = data;
    readfd = destroy->readfd;
    if (readfd < 0) {
        goto out;
    }

    while (sys_read(readfd, &buf, 1) > 0) {
    }

out:
    gf_event_handled(destroy->pool, fd, idx, gen);

    return;
}

/* This function destroys all the poller threads.
 * Note: to be called before gf_event_pool_destroy is called.
 * The order in which cleaning is performed:
 * - Register a pipe fd(this is for waking threads in poll()/epoll_wait())
 * - Set the destroy mode, which this no new event registration will succeed
 * - Reconfigure the thread count to 0(this will succeed only in destroy mode)
 * - Wake up all the threads in poll() or epoll_wait(), so that they can
 *   destroy themselves.
 * - Wait for the thread to join(which will happen only after all the other
 *   threads are destroyed)
 */
int
gf_event_dispatch_destroy(struct event_pool *event_pool)
{
    int ret = -1, threadcount = 0;
    int fd[2] = {-1};
    int idx = -1;
    int flags = 0;
    struct timespec sleep_till = {
        0,
    };
    struct event_destroy_data data = {
        0,
    };

    GF_VALIDATE_OR_GOTO("event", event_pool, out);

    ret = pipe(fd);
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
    ret = fcntl(fd[1], F_SETFL, flags);
    if (ret < 0)
        goto out;

    data.pool = event_pool;
    data.readfd = fd[1];

    /* From the main thread register an event on the pipe fd[0],
     */
    idx = gf_event_register(event_pool, fd[0], poller_destroy_handler, &data, 1,
                            0, 0);
    if (idx < 0)
        goto out;

    /* Enter the destroy mode first, set this before reconfiguring to 0
     * threads, to prevent further reconfigure to thread count > 0.
     */
    pthread_mutex_lock(&event_pool->mutex);
    {
        threadcount = event_pool->eventthreadcount;
        event_pool->destroy = 1;
    }
    pthread_mutex_unlock(&event_pool->mutex);

    ret = gf_event_reconfigure_threads(event_pool, 0);
    if (ret < 0)
        goto out;

    /* Write something onto the write end of the pipe(fd[1]) so that
     * poll wakes up and calls the handler, poller_destroy_handler()
     */
    pthread_mutex_lock(&event_pool->mutex);
    {
        /* Write to pipe(fd[1]) and then wait for 1 second or until
         * a poller thread that is dying, broadcasts. Make sure we
         * do not loop forever by limiting to 10 retries
         */
        int retry = 0;

        while (event_pool->activethreadcount > 0 &&
               (retry++ < (threadcount + 10))) {
            if (sys_write(fd[1], "dummy", 6) == -1) {
                break;
            }
            timespec_now_realtime(&sleep_till);
            sleep_till.tv_sec += 1;
            ret = pthread_cond_timedwait(&event_pool->cond, &event_pool->mutex,
                                         &sleep_till);
            if (ret) {
                gf_msg_debug("event", 0,
                             "thread cond-timedwait failed "
                             "active-thread-count: %d, "
                             "retry: %d",
                             event_pool->activethreadcount, retry);
            }
        }
    }
    pthread_mutex_unlock(&event_pool->mutex);

    ret = gf_event_unregister(event_pool, fd[0], idx);

out:
    if (fd[0] != -1)
        sys_close(fd[0]);
    if (fd[1] != -1)
        sys_close(fd[1]);

    return ret;
}

int
gf_event_handled(struct event_pool *event_pool, int fd, int idx, int gen)
{
    int ret = 0;

    if (event_pool->ops->event_handled)
        ret = event_pool->ops->event_handled(event_pool, fd, idx, gen);

    return ret;
}
