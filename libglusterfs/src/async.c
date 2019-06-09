/*
  Copyright (c) 2019 Red Hat, Inc <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* To implement an efficient thread pool with minimum contention we have used
 * the following ideas:
 *
 *    - The queue of jobs has been implemented using a Wait-Free queue provided
 *      by the userspace-rcu library. This queue requires a mutex when multiple
 *      consumers can be extracting items from it concurrently, but the locked
 *      region is very small, which minimizes the chances of contention. To
 *      further minimize contention, the number of active worker threads that
 *      are accessing the queue is dynamically adjusted so that we always have
 *      the minimum required amount of workers contending for the queue. Adding
 *      new items can be done with a single atomic operation, without locks.
 *
 *    - All queue management operations, like creating more threads, enabling
 *      sleeping ones, etc. are done by a single thread. This makes it possible
 *      to manage all scaling related information and workers lists without
 *      locks. This functionality is implemented as a role that can be assigned
 *      to any of the worker threads, which avoids that some lengthy operations
 *      could interfere with this task.
 *
 *    - Management is based on signals. We used signals for management tasks to
 *      avoid multiple system calls for each request (with signals we can wait
 *      for multiple events and get some additional data for each request in a
 *      single call, instead of first polling and then reading).
 *
 * TODO: There are some other changes that can take advantage of this new
 *       thread pool.
 *
 *          - Use this thread pool as the core threading model for synctasks. I
 *            think this would improve synctask performance because I think we
 *            currently have some contention there for some workloads.
 *
 *          - Implement a per thread timer that will allow adding and removing
 *            timers without using mutexes.
 *
 *          - Integrate with userspace-rcu library in QSBR mode, allowing
 *            other portions of code to be implemented using RCU-based
 *            structures with a extremely fast read side without contention.
 *
 *          - Integrate I/O into the thread pool so that the thread pool is
 *            able to efficiently manage all loads and scale dynamically. This
 *            could make it possible to minimize context switching when serving
 *            requests from fuse or network.
 *
 *          - Dynamically scale the number of workers based on system load.
 *            This will make it possible to reduce contention when system is
 *            heavily loaded, improving performance under these circumstances
 *            (or minimizing performance loss). This will also make it possible
 *            that gluster can coexist with other processes that also consume
 *            CPU, with minimal interference from each other.
 */

#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "glusterfs/list.h"
#include "glusterfs/mem-types.h"
#include "glusterfs/async.h"

/* These macros wrap a simple system/library call to check the returned error
 * and log a message in case of failure. */
#define GF_ASYNC_CHECK(_func, _args...)                                        \
    ({                                                                         \
        int32_t __async_error = -_func(_args);                                 \
        if (caa_unlikely(__async_error != 0)) {                                \
            gf_async_error(__async_error, #_func "() failed.");                \
        }                                                                      \
        __async_error;                                                         \
    })

#define GF_ASYNC_CHECK_ERRNO(_func, _args...)                                  \
    ({                                                                         \
        int32_t __async_error = _func(_args);                                  \
        if (caa_unlikely(__async_error < 0)) {                                 \
            __async_error = -errno;                                            \
            gf_async_error(__async_error, #_func "() failed.");                \
        }                                                                      \
        __async_error;                                                         \
    })

/* These macros are used when, based on POSIX documentation, the function
 * should never fail under the conditions we are using it. So any unexpected
 * error will be handled as a fatal event. It probably means a critical bug
 * or memory corruption. In both cases we consider that stopping the process
 * is safer (otherwise it could cause more corruption with unknown effects
 * that could be worse). */
#define GF_ASYNC_CANTFAIL(_func, _args...)                                     \
    do {                                                                       \
        int32_t __async_error = -_func(_args);                                 \
        if (caa_unlikely(__async_error != 0)) {                                \
            gf_async_fatal(__async_error, #_func "() failed");                 \
        }                                                                      \
    } while (0)

#define GF_ASYNC_CANTFAIL_ERRNO(_func, _args...)                               \
    ({                                                                         \
        int32_t __async_error = _func(_args);                                  \
        if (caa_unlikely(__async_error < 0)) {                                 \
            __async_error = -errno;                                            \
            gf_async_fatal(__async_error, #_func "() failed");                 \
        }                                                                      \
        __async_error;                                                         \
    })

/* TODO: for now we allocate a static array of workers. There's an issue if we
 *       try to use dynamic memory since these workers are initialized very
 *       early in the process startup and it seems that sometimes not all is
 *       ready to use dynamic memory. */
static gf_async_worker_t gf_async_workers[GF_ASYNC_MAX_THREADS];

/* This is the only global variable needed to manage the entire framework. */
gf_async_control_t gf_async_ctrl = {};

static __thread gf_async_worker_t *gf_async_current_worker = NULL;

/* The main function of the worker threads. */
static void *
gf_async_worker(void *arg);

static void
gf_async_sync_init(void)
{
    GF_ASYNC_CANTFAIL(pthread_barrier_init, &gf_async_ctrl.sync, NULL, 2);
}

static void
gf_async_sync_now(void)
{
    int32_t ret;

    ret = pthread_barrier_wait(&gf_async_ctrl.sync);
    if (ret == PTHREAD_BARRIER_SERIAL_THREAD) {
        GF_ASYNC_CANTFAIL(pthread_barrier_destroy, &gf_async_ctrl.sync);
        ret = 0;
    }
    if (caa_unlikely(ret != 0)) {
        gf_async_fatal(-ret, "pthread_barrier_wait() failed");
    }
}

static void
gf_async_sigmask_empty(sigset_t *mask)
{
    GF_ASYNC_CANTFAIL_ERRNO(sigemptyset, mask);
}

static void
gf_async_sigmask_add(sigset_t *mask, int32_t signal)
{
    GF_ASYNC_CANTFAIL_ERRNO(sigaddset, mask, signal);
}

static void
gf_async_sigmask_set(int32_t mode, sigset_t *mask, sigset_t *old)
{
    GF_ASYNC_CANTFAIL(pthread_sigmask, mode, mask, old);
}

static void
gf_async_sigaction(int32_t signum, const struct sigaction *action,
                   struct sigaction *old)
{
    GF_ASYNC_CANTFAIL_ERRNO(sigaction, signum, action, old);
}

static int32_t
gf_async_sigwait(sigset_t *set)
{
    int32_t ret, signum;

    do {
        ret = sigwait(set, &signum);
    } while (caa_unlikely((ret < 0) && (errno == EINTR)));

    if (caa_unlikely(ret < 0)) {
        ret = -errno;
        gf_async_fatal(ret, "sigwait() failed");
    }

    return signum;
}

static int32_t
gf_async_sigtimedwait(sigset_t *set, struct timespec *timeout)
{
    int32_t ret;

    do {
        ret = sigtimedwait(set, NULL, timeout);
    } while (caa_unlikely((ret < 0) && (errno == EINTR)));
    if (caa_unlikely(ret < 0)) {
        ret = -errno;
        /* EAGAIN means that the timeout has expired, so we allow this error.
         * Any other error shouldn't happen. */
        if (caa_unlikely(ret != -EAGAIN)) {
            gf_async_fatal(ret, "sigtimedwait() failed");
        }
        ret = 0;
    }

    return ret;
}

static void
gf_async_sigbroadcast(int32_t signum)
{
    GF_ASYNC_CANTFAIL_ERRNO(kill, gf_async_ctrl.pid, signum);
}

static void
gf_async_signal_handler(int32_t signum)
{
    /* We should never handle a signal in this function. */
    gf_async_fatal(-EBUSY,
                   "Unexpected processing of signal %d through a handler.",
                   signum);
}

static void
gf_async_signal_setup(void)
{
    struct sigaction action;

    /* We configure all related signals so that we can detect threads using an
     * invalid signal mask that doesn't block our critical signal. */
    memset(&action, 0, sizeof(action));
    action.sa_handler = gf_async_signal_handler;

    gf_async_sigaction(GF_ASYNC_SIGCTRL, &action, &gf_async_ctrl.handler_ctrl);

    gf_async_sigaction(GF_ASYNC_SIGQUEUE, &action,
                       &gf_async_ctrl.handler_queue);
}

static void
gf_async_signal_restore(void)
{
    /* Handlers we have previously changed are restored back to their original
     * value. */

    if (gf_async_ctrl.handler_ctrl.sa_handler != gf_async_signal_handler) {
        gf_async_sigaction(GF_ASYNC_SIGCTRL, &gf_async_ctrl.handler_ctrl, NULL);
    }

    if (gf_async_ctrl.handler_queue.sa_handler != gf_async_signal_handler) {
        gf_async_sigaction(GF_ASYNC_SIGQUEUE, &gf_async_ctrl.handler_queue,
                           NULL);
    }
}

static void
gf_async_signal_flush(void)
{
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 0;

    /* We read all pending signals so that they don't trigger once the signal
     * mask of some thread is changed. */
    while (gf_async_sigtimedwait(&gf_async_ctrl.sigmask_ctrl, &delay) > 0) {
    }
    while (gf_async_sigtimedwait(&gf_async_ctrl.sigmask_queue, &delay) > 0) {
    }
}

static int32_t
gf_async_thread_create(pthread_t *thread, int32_t id, void *data)
{
    int32_t ret;

    ret = gf_thread_create(thread, NULL, gf_async_worker, data,
                           GF_ASYNC_THREAD_NAME "%u", id);
    if (caa_unlikely(ret < 0)) {
        /* TODO: gf_thread_create() should return a more specific error
         *       code. */
        return -ENOMEM;
    }

    return 0;
}

static void
gf_async_thread_wait(pthread_t thread)
{
    /* TODO: this is a blocking call executed inside one of the workers of the
     *       thread pool. This is bad, but this is only executed once we have
     *       received a notification from the thread that it's terminating, so
     *       this should return almost immediately. However, to be more robust
     *       it would be better to use pthread_timedjoin_np() (or even a call
     *       to pthread_tryjoin_np() followed by a delayed recheck if it
     *       fails), but they are not portable. We should see how to do this
     *       in other platforms. */
    GF_ASYNC_CANTFAIL(pthread_join, thread, NULL);
}

static int32_t
gf_async_worker_create(void)
{
    struct cds_wfs_node *node;
    gf_async_worker_t *worker;
    uint32_t counts, running, max;
    int32_t ret;

    node = __cds_wfs_pop_blocking(&gf_async_ctrl.available);
    if (caa_unlikely(node == NULL)) {
        /* There are no more available workers. We have all threads running. */
        return 1;
    }
    cds_wfs_node_init(node);

    ret = 1;

    counts = uatomic_read(&gf_async_ctrl.counts);
    max = uatomic_read(&gf_async_ctrl.max_threads);
    running = GF_ASYNC_COUNT_RUNNING(counts);
    if (running < max) {
        uatomic_add(&gf_async_ctrl.counts, GF_ASYNC_COUNTS(1, 0));

        worker = caa_container_of(node, gf_async_worker_t, stack);

        ret = gf_async_thread_create(&worker->thread, worker->id, worker);
        if (caa_likely(ret >= 0)) {
            return 0;
        }

        uatomic_add(&gf_async_ctrl.counts, GF_ASYNC_COUNTS(-1, 0));
    }

    cds_wfs_push(&gf_async_ctrl.available, node);

    return ret;
}

static void
gf_async_worker_enable(void)
{
    /* This will wake one of the spare workers. If all workers are busy now,
     * the signal will be queued so that the first one that completes its
     * work will become the leader. */
    gf_async_sigbroadcast(GF_ASYNC_SIGCTRL);

    /* We have consumed a spare worker. We create another one for future
     * needs. */
    gf_async_worker_create();
}

static void
gf_async_worker_wait(void)
{
    int32_t signum;

    signum = gf_async_sigwait(&gf_async_ctrl.sigmask_ctrl);
    if (caa_unlikely(signum != GF_ASYNC_SIGCTRL)) {
        gf_async_fatal(-EINVAL, "Worker received an unexpected signal (%d)",
                       signum);
    }
}

static void
gf_async_leader_wait(void)
{
    int32_t signum;

    signum = gf_async_sigwait(&gf_async_ctrl.sigmask_queue);
    if (caa_unlikely(signum != GF_ASYNC_SIGQUEUE)) {
        gf_async_fatal(-EINVAL, "Leader received an unexpected signal (%d)",
                       signum);
    }
}

static void
gf_async_run(struct cds_wfcq_node *node)
{
    gf_async_t *async;

    /* We've just got work from the queue. Process it. */
    async = caa_container_of(node, gf_async_t, queue);
    /* TODO: remove dependency from THIS and xl. */
    THIS = async->xl;
    async->cbk(async->xl, async);
}

static void
gf_async_worker_run(void)
{
    struct cds_wfcq_node *node;

    do {
        /* We keep executing jobs from the queue while it's not empty. Note
         * that while we do this, we are ignoring any stop request. That's
         * fine, since we need to process our own 'join' messages to fully
         * terminate all threads. Note that normal jobs should have already
         * completed once a stop request is received. */
        node = cds_wfcq_dequeue_blocking(&gf_async_ctrl.queue.head,
                                         &gf_async_ctrl.queue.tail);
        if (node != NULL) {
            gf_async_run(node);
        }
    } while (node != NULL);

    /* TODO: I've tried to keep the worker looking at the queue for some small
     *       amount of time in a busy loop to see if more jobs come soon. With
     *       this I attempted to avoid the overhead of signal management if
     *       jobs come fast enough. However experimental results seem to
     *       indicate that doing this, CPU utilization grows and performance
     *       is actually reduced. We need to see if that's because I used bad
     *       parameters or it's really better to do it as it's done now. */
}

static void
gf_async_leader_run(void)
{
    struct cds_wfcq_node *node;

    node = cds_wfcq_dequeue_blocking(&gf_async_ctrl.queue.head,
                                     &gf_async_ctrl.queue.tail);
    while (caa_unlikely(node == NULL)) {
        gf_async_leader_wait();

        node = cds_wfcq_dequeue_blocking(&gf_async_ctrl.queue.head,
                                         &gf_async_ctrl.queue.tail);
    }

    /* Activate the next available worker thread. It will become the new
     * leader. */
    gf_async_worker_enable();

    gf_async_run(node);
}

static uint32_t
gf_async_stop_check(gf_async_worker_t *worker)
{
    uint32_t counts, old, running, max;

    /* First we check if we should stop without doing any costly atomic
     * operation. */
    old = uatomic_read(&gf_async_ctrl.counts);
    max = uatomic_read(&gf_async_ctrl.max_threads);
    running = GF_ASYNC_COUNT_RUNNING(old);
    while (running > max) {
        /* There are too many threads. We try to stop the current worker. */
        counts = uatomic_cmpxchg(&gf_async_ctrl.counts, old,
                                 old + GF_ASYNC_COUNTS(-1, 1));
        if (old != counts) {
            /* Another thread has just updated the counts. We need to retry. */
            old = counts;
            running = GF_ASYNC_COUNT_RUNNING(old);

            continue;
        }

        running--;
        worker->running = false;
    }

    return running;
}

static void
gf_async_stop_all(xlator_t *xl, gf_async_t *async)
{
    if (gf_async_stop_check(gf_async_current_worker) > 0) {
        /* There are more workers running. We propagate the stop request to
         * them. */
        gf_async(async, xl, gf_async_stop_all);
    }
}

static void
gf_async_join(xlator_t *xl, gf_async_t *async)
{
    gf_async_worker_t *worker;

    worker = caa_container_of(async, gf_async_worker_t, async);

    gf_async_thread_wait(worker->thread);

    cds_wfs_push(&gf_async_ctrl.available, &worker->stack);
}

static void
gf_async_terminate(gf_async_worker_t *worker)
{
    uint32_t counts;

    counts = uatomic_add_return(&gf_async_ctrl.counts, GF_ASYNC_COUNTS(0, -1));
    if (counts == 0) {
        /* This is the termination of the last worker thread. We need to
         * synchronize the main thread that is waiting for all workers to
         * finish. */
        gf_async_ctrl.sync_thread = worker->thread;

        gf_async_sync_now();
    } else {
        /* Force someone else to join this thread to release resources. */
        gf_async(&worker->async, THIS, gf_async_join);
    }
}

static void *
gf_async_worker(void *arg)
{
    gf_async_worker_t *worker;

    worker = (gf_async_worker_t *)arg;
    gf_async_current_worker = worker;

    worker->running = true;
    do {
        /* This thread does nothing until someone enables it to become a
         * leader. */
        gf_async_worker_wait();

        /* This thread is now a leader. It will process jobs from the queue
         * and, if necessary, enable another worker and transfer leadership
         * to it. */
        gf_async_leader_run();

        /* This thread is not a leader anymore. It will continue processing
         * queued jobs until it becomes empty. */
        gf_async_worker_run();

        /* Stop the current thread if there are too many threads running. */
        gf_async_stop_check(worker);
    } while (worker->running);

    gf_async_terminate(worker);

    return NULL;
}

static void
gf_async_cleanup(void)
{
    /* We do some basic initialization of the global variable 'gf_async_ctrl'
     * so that it's put into a relatively consistent state. */

    gf_async_ctrl.enabled = false;

    gf_async_ctrl.pid = 0;
    gf_async_sigmask_empty(&gf_async_ctrl.sigmask_ctrl);
    gf_async_sigmask_empty(&gf_async_ctrl.sigmask_queue);

    /* This is used to later detect if the handler of these signals have been
     * changed or not. */
    gf_async_ctrl.handler_ctrl.sa_handler = gf_async_signal_handler;
    gf_async_ctrl.handler_queue.sa_handler = gf_async_signal_handler;

    gf_async_ctrl.table = NULL;
    gf_async_ctrl.max_threads = 0;
    gf_async_ctrl.counts = 0;
}

void
gf_async_fini(void)
{
    gf_async_t async;

    if (uatomic_read(&gf_async_ctrl.counts) != 0) {
        /* We ensure that all threads will quit on the next check. */
        gf_async_ctrl.max_threads = 0;

        /* Send the stop request to the thread pool. This will cause the
         * execution of gf_async_stop_all() by one of the worker threads which,
         * eventually, will terminate all worker threads. */
        gf_async(&async, THIS, gf_async_stop_all);

        /* We synchronize here with the last thread. */
        gf_async_sync_now();

        /* We have just synchronized with the latest thread. Now just wait for
         * it to terminate. */
        gf_async_thread_wait(gf_async_ctrl.sync_thread);

        gf_async_signal_flush();
    }

    gf_async_signal_restore();

    gf_async_cleanup();
}

void
gf_async_adjust_threads(int32_t threads)
{
    if (threads == 0) {
        /* By default we allow a maximum of 2 * #cores worker threads. This
         * value is to try to accommodate threads that will do some I/O. Having
         * more threads than cores we can keep CPU busy even if some threads
         * are blocked for I/O. In the most efficient case, we can have #cores
         * computing threads and #cores blocked threads on I/O. However this is
         * hard to achieve because we can end with more than #cores computing
         * threads, which won't provide a real benefit and will increase
         * contention.
         *
         * TODO: implement a more intelligent dynamic maximum based on CPU
         *       usage and/or system load. */
        threads = sysconf(_SC_NPROCESSORS_ONLN) * 2;
        if (threads < 0) {
            /* If we can't get the current number of processors, we pick a
             * random number. */
            threads = 16;
        }
    }
    if (threads > GF_ASYNC_MAX_THREADS) {
        threads = GF_ASYNC_MAX_THREADS;
    }
    uatomic_set(&gf_async_ctrl.max_threads, threads);
}

int32_t
gf_async_init(glusterfs_ctx_t *ctx)
{
    sigset_t set;
    gf_async_worker_t *worker;
    uint32_t i;
    int32_t ret;
    bool running;

    gf_async_cleanup();

    if (!ctx->cmd_args.global_threading ||
        (ctx->process_mode == GF_GLUSTERD_PROCESS)) {
        return 0;
    }

    /* At the init time, the maximum number of threads has not yet been
     * configured. We use a small starting value that will be layer dynamically
     * adjusted when ctx->config.max_threads is updated. */
    gf_async_adjust_threads(GF_ASYNC_SPARE_THREADS + 1);

    gf_async_ctrl.pid = getpid();

    __cds_wfs_init(&gf_async_ctrl.available);
    cds_wfcq_init(&gf_async_ctrl.queue.head, &gf_async_ctrl.queue.tail);

    gf_async_sync_init();

    /* TODO: it would be cleaner to use dynamic memory, but at this point some
     *       memory management resources are not yet initialized. */
    gf_async_ctrl.table = gf_async_workers;

    /* We keep all workers in a stack. It will be used when a new thread needs
     * to be created. */
    for (i = GF_ASYNC_MAX_THREADS; i > 0; i--) {
        worker = &gf_async_ctrl.table[i - 1];

        worker->id = i - 1;
        cds_wfs_node_init(&worker->stack);
        cds_wfs_push(&gf_async_ctrl.available, &worker->stack);
    }

    /* Prepare the signal mask for regular workers and the leader. */
    gf_async_sigmask_add(&gf_async_ctrl.sigmask_ctrl, GF_ASYNC_SIGCTRL);
    gf_async_sigmask_add(&gf_async_ctrl.sigmask_queue, GF_ASYNC_SIGQUEUE);

    /* TODO: this is needed to block our special signals in the current thread
     *       and all children that it starts. It would be cleaner to do it when
     *       signals are initialized, but there doesn't seem to be a unique
     *       place to do that, so for now we do it here. */
    gf_async_sigmask_empty(&set);
    gf_async_sigmask_add(&set, GF_ASYNC_SIGCTRL);
    gf_async_sigmask_add(&set, GF_ASYNC_SIGQUEUE);
    gf_async_sigmask_set(SIG_BLOCK, &set, NULL);

    /* Configure the signal handlers. This is mostly for safety, not really
     * needed, but it doesn't hurt. Note that the caller must ensure that the
     * signals we need to run are already blocked in any thread already
     * started. Otherwise this won't work. */
    gf_async_signal_setup();

    running = false;

    /* We start the spare workers + 1 for the leader. */
    for (i = 0; i < GF_ASYNC_SPARE_THREADS; i++) {
        ret = gf_async_worker_create();
        if (caa_unlikely(ret < 0)) {
            /* This is the initial start up so we enforce that the spare
             * threads are created. If this fails at the beginning, it's very
             * unlikely that the async workers could do its job, so we abort
             * the initialization. */
            goto out;
        }

        /* Once the first thread is started, we can enable it to become the
         * initial leader. */
        if ((ret == 0) && !running) {
            running = true;
            gf_async_worker_enable();
        }
    }

    if (caa_unlikely(!running)) {
        gf_async_fatal(-ENOMEM, "No worker thread has started");
    }

    gf_async_ctrl.enabled = true;

    ret = 0;

out:
    if (ret < 0) {
        gf_async_error(ret, "Unable to initialize the thread pool.");
        gf_async_fini();
    }

    return ret;
}
