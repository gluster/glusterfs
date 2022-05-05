/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <urcu/uatomic.h>

#include <glusterfs/list.h>
#include <glusterfs/gf-io-common.h>

#define LG_MSG_IO_THREAD_BAD_PRIORITY_LVL(_res) GF_LOG_ERROR
#define LG_MSG_IO_THREAD_BAD_PRIORITY_FMT                                      \
    "Specified priority is out of bounds (%d)."

#define LG_MSG_IO_THREAD_NO_CPU_LVL(_res) GF_LOG_ERROR
#define LG_MSG_IO_THREAD_NO_CPU_FMT                                            \
    "Cannot find a suitable CPU for a thread (%u)."

#define LG_MSG_IO_THREAD_NAME_INVALID_LVL(_res) GF_LOG_ERROR
#define LG_MSG_IO_THREAD_NAME_INVALID_FMT                                      \
    "Tried to construct and invalid name for a thread."

#define LG_MSG_IO_SYNC_TIMEOUT_LVL(_res) GF_LOG_WARNING
#define LG_MSG_IO_SYNC_TIMEOUT_FMT                                             \
    "Time out while waiting for synchronization (%u retries)."

#define LG_MSG_IO_SYNC_ABORTED_LVL(_res) GF_LOG_ERROR
#define LG_MSG_IO_SYNC_ABORTED_FMT                                             \
    "Synchronization took too much time. Aborting after %u retries."

#define LG_MSG_IO_SYNC_COMPLETED_LVL(_res) GF_LOG_INFO
#define LG_MSG_IO_SYNC_COMPLETED_FMT                                           \
    "Synchronization completed after %u retries."

static __thread gf_io_thread_t gf_io_thread = {};

/* Initialize a condition variable using a monotonic clock for timeouts. */
static int32_t
gf_io_cond_init(pthread_cond_t *cond)
{
    pthread_condattr_t attr;
    int32_t res;

    res = gf_io_call_ret(pthread_condattr_init, &attr);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    res = gf_io_call_ret(pthread_condattr_setclock, &attr, CLOCK_MONOTONIC);
    if (caa_likely(res >= 0)) {
        res = gf_io_call_ret(pthread_cond_init, cond, &attr);
    }

    gf_io_call_ret(pthread_condattr_destroy, &attr);

    return res;
}

/* Initializes a sync object to synchronize 'count' entities with a maximum
 * delay of 'timeout' seconds. */
int32_t
gf_io_sync_start(gf_io_sync_t *sync, uint32_t count, uint32_t timeout,
                 uint32_t retries, void *data)
{
    int32_t res;

    res = gf_io_call_errno0(clock_gettime, CLOCK_MONOTONIC, &sync->abs_to);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    sync->abs_to.tv_sec += timeout;
    sync->data = data;
    sync->timeout = timeout;
    sync->retries = retries;
    sync->count = count;
    sync->phase = 0;
    sync->pending = count;
    sync->res = 0;

    res = gf_io_call_ret(pthread_mutex_init, &sync->mutex, NULL);
    if (caa_likely(res >= 0)) {
        res = gf_io_cond_init(&sync->cond);
        if (caa_likely(res >= 0)) {
            return 0;
        }

        gf_io_call_ret(pthread_mutex_destroy, &sync->mutex);
    }

    return res;
}

/* Destroys a sync object. */
static void
gf_io_sync_destroy(gf_io_sync_t *sync)
{
    gf_io_call_ret(pthread_cond_destroy, &sync->cond);
    gf_io_call_ret(pthread_mutex_destroy, &sync->mutex);
}

static int32_t
gf_io_sync_wait_timeout(gf_io_sync_t *sync, int32_t retry, bool check)
{
    int32_t res;

    if (check) {
        if (retry > 0) {
            gf_io_log(0, LG_MSG_IO_SYNC_COMPLETED, retry);
        }
        return -1;
    }

    res = gf_io_call_ret(pthread_cond_timedwait, &sync->cond, &sync->mutex,
                         &sync->abs_to);
    if (caa_unlikely(res != 0)) {
        if (res != -ETIMEDOUT) {
            GF_ABORT();
        }

        retry++;

        gf_io_log(res, LG_MSG_IO_SYNC_TIMEOUT, retry);

        if (sync->retries == 0) {
            gf_io_log(res, LG_MSG_IO_SYNC_ABORTED, retry);
            GF_ABORT();
        }
        sync->retries--;

        sync->abs_to.tv_sec += sync->timeout;
    }

    return retry;
}

/* Notifies completion of 'count' entities. Optionally it can wait until
 * all other threads have also notified. Only one thread can wait. */
int32_t
gf_io_sync_done(gf_io_sync_t *sync, uint32_t count, int32_t res, bool wait)
{
    int32_t retry;

    gf_io_success(gf_io_call_ret(pthread_mutex_lock, &sync->mutex));

    sync->pending -= count;
    if (!wait) {
        if (caa_unlikely(res < 0) && (sync->res >= 0)) {
            sync->res = res;
        }

        if (sync->pending == 0) {
            gf_io_success(gf_io_call_ret(pthread_cond_signal, &sync->cond));
        }

        gf_io_success(gf_io_call_ret(pthread_mutex_unlock, &sync->mutex));

        return 0;
    }

    retry = 0;
    do {
        retry = gf_io_sync_wait_timeout(sync, retry, sync->pending == 0);
    } while (retry >= 0);

    res = sync->res;

    gf_io_success(gf_io_call_ret(pthread_mutex_unlock, &sync->mutex));

    gf_io_sync_destroy(sync);

    return res;
}

/* Wait for a synchronization point. 'count' represents the number of
 * entities waiting, and 'res' the result of the operation done just
 * before synchronizing. The return value will be 0 only if all entities
 * completed without error (i.e. 'res' was >= 0 in all calls to this
 * function). */
int32_t
gf_io_sync_wait(gf_io_sync_t *sync, uint32_t count, int32_t res)
{
    uint32_t phase;
    int32_t retry;

    gf_io_success(gf_io_call_ret(pthread_mutex_lock, &sync->mutex));

    if (caa_unlikely(res < 0) && (sync->res >= 0)) {
        sync->res = res;
    }

    sync->pending -= count;
    if (sync->pending == 0) {
        sync->pending = sync->count;
        sync->phase++;

        gf_io_success(gf_io_call_ret(pthread_cond_broadcast, &sync->cond));
    } else {
        phase = sync->phase;

        retry = 0;
        do {
            retry = gf_io_sync_wait_timeout(sync, retry, sync->phase != phase);
        } while (retry >= 0);
    }

    res = sync->res;

    gf_io_success(gf_io_call_ret(pthread_mutex_unlock, &sync->mutex));

    return res;
}

/* Sets the name of the thread. */
static int32_t
gf_io_thread_name(pthread_t id, const char *code, uint32_t index)
{
    char name[GF_THREAD_NAME_LIMIT];
    int32_t len;

    len = snprintf(name, sizeof(name), GF_THREAD_NAME_PREFIX "%s/%u", code,
                   index);
    if (caa_unlikely((len < 0) || (len >= sizeof(name)))) {
        gf_io_log(-EINVAL, LG_MSG_IO_THREAD_NAME_INVALID);

        return -EINVAL;
    }

    return __gf_thread_set_name(id, name);
}

/* Sets the signal mask of the thread. */
static int32_t
gf_io_thread_mask(int32_t *signals)
{
    sigset_t set;
    int32_t i, res;

    res = gf_io_call_errno0(sigfillset, &set);
    for (i = 0; caa_likely(res >= 0) && (signals[i] != 0); i++) {
        res = gf_io_call_errno0(sigdelset, &set, signals[i]);
    }

    if (caa_likely(res >= 0)) {
        res = gf_io_call_ret(pthread_sigmask, SIG_BLOCK, &set, NULL);
    }

    return res;
}

#ifdef GF_LINUX_HOST_OS

/* Sets the affinity of the thread. */
static int32_t
gf_io_thread_affinity(pthread_t id, cpu_set_t *cpus, uint32_t index)
{
    cpu_set_t affinity;
    uint32_t i, current;

    if (cpus == NULL) {
        return 0;
    }

    current = 0;
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, cpus)) {
            if (current == index) {
                break;
            }
            current++;
        }
    }
    if (caa_unlikely(i >= CPU_SETSIZE)) {
        gf_io_log(-ENODEV, LG_MSG_IO_THREAD_NO_CPU, index);
        return -ENODEV;
    }

    CPU_ZERO(&affinity);
    CPU_SET(i, &affinity);

    return gf_io_call_ret(pthread_setaffinity_np, id, sizeof(affinity),
                          &affinity);
}

#endif

/* Adds a thread to the thread pool. */
static int32_t
gf_io_thread_add(gf_io_thread_pool_t *pool, gf_io_thread_t *thread)
{
    int32_t res;

    thread->pool = pool;

    res = gf_io_call_ret(pthread_mutex_lock, &pool->mutex);
    if (caa_likely(res >= 0)) {
        list_add_tail(&thread->list, &pool->threads);

        gf_io_success(gf_io_call_ret(pthread_mutex_unlock, &pool->mutex));
    }

    return res;
}

/* Initialize a thread. */
static gf_io_thread_main_t
gf_io_thread_init(gf_io_sync_t *sync, gf_io_thread_t *thread)
{
    gf_io_thread_pool_config_t *cfg;
    gf_io_thread_pool_t *pool;
    gf_io_thread_main_t start;
    int32_t res;

    cfg = sync->data;
    pool = cfg->pool;
    start = NULL;

    /* Sync phase 0: Creation of all threads. */

    res = gf_io_thread_add(pool, thread);
    if (caa_unlikely(gf_io_sync_wait(sync, 1, res) < 0)) {
        goto done;
    }

    /* Sync phase 1: Configuration of each thread. */

    thread->id = pthread_self();
    thread->index = uatomic_add_return(&cfg->index, 1) - 1;

    res = gf_io_thread_name(thread->id, cfg->name,
                            thread->index + cfg->first_id);
    if (caa_likely(res >= 0)) {
        res = gf_io_thread_mask(cfg->signals);
    }

#ifdef GF_LINUX_HOST_OS
    if (caa_likely(res >= 0)) {
        res = gf_io_thread_affinity(thread->id, cfg->cpus, thread->index);
    }
#endif

    if (caa_unlikely(gf_io_sync_wait(sync, 1, res) < 0)) {
        goto done;
    }

    /* Sync phase 2: Specific initialization. */

    thread->data = NULL;

    res = cfg->setup(sync, thread);
    if (caa_unlikely(res < 0)) {
        goto done;
    }

    start = cfg->main;

done:
    gf_io_sync_done(sync, 1, 0, false);

    return start;
}

/* Thread main function. */
static void *
gf_io_thread_main(void *data)
{
    gf_io_thread_t *thread;
    gf_io_thread_main_t start;
    int32_t res;

    thread = &gf_io_thread;

    start = gf_io_thread_init(data, thread);
    if (caa_likely(start != NULL)) {
        res = start(thread);
        if (caa_unlikely(res < 0)) {
            GF_ABORT();
        }
    }

    return NULL;
}

/* Add scheduler/priority configuration to a thread attr. */
static int32_t
gf_io_thread_attr_priority(gf_io_thread_pool_config_t *cfg,
                           pthread_attr_t *attr)
{
    struct sched_param param;
    int32_t policy, priority, min, max, res;

    priority = cfg->priority;

    if (priority == 0) {
        return 0;
    }

    policy = SCHED_FIFO;
    if (priority < 0) {
        policy = SCHED_RR;
        priority = -priority;
    }
    if (priority > 100) {
        gf_io_log(-EINVAL, LG_MSG_IO_THREAD_BAD_PRIORITY, cfg->priority);

        return -EINVAL;
    }

    min = gf_io_call_errno(sched_get_priority_min, policy);
    if (caa_unlikely(min < 0)) {
        return min;
    }
    max = gf_io_call_errno(sched_get_priority_max, policy);
    if (caa_unlikely(max < 0)) {
        return max;
    }

    memset(&param, 0, sizeof(param));
    param.sched_priority = min + priority * (max - min) / 100;

    res = gf_io_call_ret(pthread_attr_setschedpolicy, attr, policy);
    if (caa_likely(res >= 0)) {
        res = gf_io_call_ret(pthread_attr_setschedparam, attr, &param);
    }
    if (caa_likely(res >= 0)) {
        res = gf_io_call_ret(pthread_attr_setinheritsched, attr,
                             PTHREAD_EXPLICIT_SCHED);
    }

    return res;
}

/* Prepare the attrs for a new thread. */
static int32_t
gf_io_thread_attr(gf_io_thread_pool_config_t *cfg, pthread_attr_t *attr)
{
    int32_t res;

    res = gf_io_call_ret(pthread_attr_init, attr);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    res = gf_io_call_ret(pthread_attr_setstacksize, attr, cfg->stack_size);
    if (caa_likely(res >= 0)) {
        res = gf_io_thread_attr_priority(cfg, attr);
    }

    if (caa_unlikely(res < 0)) {
        gf_io_call_ret(pthread_attr_destroy, attr);
    }

    return res;
}

/* Create threads. */
static int32_t
gf_io_thread_create(gf_io_thread_pool_config_t *cfg, pthread_t *ids,
                    uint32_t *created, void *(*main)(void *), void *data)
{
    pthread_attr_t attr;
    uint32_t i;
    int32_t res;

    i = 0;

    res = gf_io_thread_attr(cfg, &attr);
    if (caa_likely(res >= 0)) {
        while (i < cfg->num_threads) {
            res = gf_io_call_ret(pthread_create, &ids[i], &attr, main, data);
            if (caa_unlikely(res < 0)) {
                break;
            }

            i++;
        }

        gf_io_call_ret(pthread_attr_destroy, &attr);
    }

    *created = i;

    return res;
}

/* Join a thread. */
static void
gf_io_thread_join(pthread_t thread, struct timespec *timeout)
{
#ifdef GF_LINUX_HOST_OS
    if (timeout != NULL) {
        gf_io_success(
            gf_io_call_ret(pthread_timedjoin_np, thread, NULL, timeout));

        return;
    }
#endif /* GF_LINUX_HOST_OS */

    gf_io_success(gf_io_call_ret(pthread_join, thread, NULL));
}

/* Initializes as thread pool object. */
static int32_t
gf_io_thread_pool_init(gf_io_thread_pool_t *pool,
                       gf_io_thread_pool_config_t *cfg)
{
    INIT_LIST_HEAD(&pool->threads);
    cfg->pool = pool;
    cfg->index = 0;

    return gf_io_call_ret(pthread_mutex_init, &pool->mutex, NULL);
}

/* Destroys a thread pool object. */
static void
gf_io_thread_pool_destroy(gf_io_thread_pool_t *pool)
{
    gf_io_call_ret(pthread_mutex_destroy, &pool->mutex);
}

/* Start a thread pool. */
int32_t
gf_io_thread_pool_start(gf_io_thread_pool_t *pool,
                        gf_io_thread_pool_config_t *cfg)
{
    pthread_t ids[cfg->num_threads];
    gf_io_sync_t sync;
    struct timespec to;
    uint32_t created, pending;
    int32_t res;

    res = gf_io_thread_pool_init(pool, cfg);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    created = 0;

    res = gf_io_sync_start(&sync, cfg->num_threads + 1, cfg->timeout,
                           cfg->retries, cfg);
    if (caa_unlikely(res < 0)) {
        goto done;
    }

    /* Sync phase 0: Creation of all threads. */

    res = gf_io_thread_create(cfg, ids, &created, gf_io_thread_main, &sync);
    pending = cfg->num_threads - created + 1;
    if (caa_unlikely(res < 0)) {
        goto done_sync;
    }

    res = gf_io_sync_wait(&sync, pending, res);
    if (caa_unlikely(res < 0)) {
        goto done_sync;
    }

    /* Sync phase 1: Configuration of each thread. */

    res = gf_io_sync_wait(&sync, 1, 0);
    if (caa_unlikely(res < 0)) {
        goto done_sync;
    }

    /* Sync phase 2: Specific initialization. */

    res = cfg->setup(&sync, NULL);

done_sync:
    gf_io_sync_done(&sync, pending, 0, true);

done:
    if (caa_unlikely(res < 0)) {
        gf_io_success(gf_io_call_ret(clock_gettime, CLOCK_REALTIME, &to));
        to.tv_sec += sync.timeout;

        while (created > 0) {
            gf_io_thread_join(ids[--created], &to);
        }

        gf_io_thread_pool_destroy(pool);
    }

    return res;
}

/* Wait for thread pool termination and destroy it. */
void
gf_io_thread_pool_wait(gf_io_thread_pool_t *pool, uint32_t timeout)
{
    struct timespec to;
    gf_io_thread_t *thread;

    gf_io_success(gf_io_call_ret(clock_gettime, CLOCK_REALTIME, &to));
    to.tv_sec += timeout;

    /* The list of threads is accessed concurrently only during creation of
     * the thread pool. Once created, no one will touch the list, and only
     * a single caller to gf_io_thread_pool_wait() is allowed, so it's safe
     * to modify the list without taking the lock. */

    while (!list_empty(&pool->threads)) {
        thread = list_first_entry(&pool->threads, gf_io_thread_t, list);
        list_del_init(&thread->list);

        gf_io_thread_join(thread->id, &to);
    }

    gf_io_thread_pool_destroy(pool);
}
