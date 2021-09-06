/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <limits.h>
#include <signal.h>
#include <sys/mman.h>

#include <glusterfs/gf-io-legacy.h>

#ifdef HAVE_IO_URING
#include <glusterfs/gf-io-uring.h>
#endif

#define LG_MSG_IO_CBK_SLOW_LVL(_res) GF_LOG_WARNING
#define LG_MSG_IO_CBK_SLOW_FMT                                                 \
    "Execution of '%s()' (%p) in '%s:%u' took too much time (%" PRIu64 " us)"

#define LG_MSG_IO_NO_ENGINE_LVL(_res) GF_LOG_ERROR
#define LG_MSG_IO_NO_ENGINE_FMT "No suitable I/O engine found."

/* Stack size for worker threads. */
#define GF_IO_STACK_SIZE (512 * 1024)

/* Maximum latency allowed for callback execution (in ns). */
#define GF_IO_CBK_LATENCY_THRESHOLD 100000

gf_io_t gf_io = {};
__thread gf_io_worker_t gf_io_worker = {};

static const gf_io_engine_t *gf_io_engines[] = {
#ifdef HAVE_IO_URING
    &gf_io_engine_io_uring,
#endif
    &gf_io_engine_legacy,
    NULL
};

/* Allocate an array of objects in a memory mapped area. */
#define gf_io_alloc_array(_ptr, _size)                                         \
    gf_io_alloc((void **)&(_ptr), sizeof(*(_ptr)) * (_size))

/* Release an array of objects from a memory mapped area. */
#define gf_io_free_array(_ptr, _size)                                          \
    gf_io_free(_ptr, sizeof(*(_ptr)) * (_size))

/* Allocate memory with mmap. */
static int32_t
gf_io_alloc(void **pptr, uint32_t size)
{
    void *ptr;
    int32_t res;

    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
               -1, 0);
    if (caa_unlikely(ptr == MAP_FAILED)) {
        res = -errno;
        gf_io_log(res, LG_MSG_IO_CALL_FAILED, "mmap");

        return res;
    }

    gf_io_call_errno0(mlock, ptr, size);

#ifdef GF_LINUX_HOST_OS
    gf_io_call_errno0(madvise, ptr, size, MADV_DONTFORK);
#endif

    *pptr = ptr;

    return 0;
}

/* Release mmap'd memory. */
static void
gf_io_free(void *ptr, uint32_t size)
{
    gf_io_call_errno0(munmap, ptr, size);
}

#ifdef DEBUG

static void
gf_io_time_now(struct timespec *now)
{
    gf_io_success(gf_io_call_ret(clock_gettime, CLOCK_MONOTONIC, now));
}

static void
gf_io_latency_check(const struct timespec *start, struct timespec *now,
                    const char *name, const void *ptr, const char *file,
                    uint32_t line)
{
    uint64_t elapsed;
    int64_t tv_sec, tv_nsec;

    gf_io_time_now(now);

    tv_sec = now->tv_sec - start->tv_sec;
    tv_nsec = now->tv_nsec - start->tv_nsec;
    if (caa_unlikely(tv_nsec < 0)) {
        tv_nsec += 1000000000;
        tv_sec -= 1;
    }
    if (caa_likely(tv_sec == 0)) {
        if (caa_likely(tv_nsec < GF_IO_CBK_LATENCY_THRESHOLD)) {
            return;
        }

        elapsed = tv_nsec / 1000ULL;
    } else {
        elapsed = tv_sec * 1000000ULL + tv_nsec / 1000ULL;
    }

    gf_io_log(-ETIME, LG_MSG_IO_CBK_SLOW, name, ptr, file, line, elapsed);
}

/* Run a callback for a request id and release the associated gf_io_data_t
 * afterwards. */
void
gf_io_cbk(gf_io_worker_t *worker, uint64_t seq, uint64_t id, int32_t res)
{
    struct timespec t1, t2;
    gf_io_op_t *op;
    gf_io_callback_t cbk;

    op = &gf_io.op_pool[id & GF_IO_ID_REQ_MASK];

    cbk = op->cbk;
    op->worker = worker;

    /* In debug builds, we check that callbacks are fast enough. */

    gf_io_time_now(&t1);
    cbk->func(op, res);
    gf_io_latency_check(&t1, &t2, cbk->name, cbk->func, cbk->file, cbk->line);

    gf_io_put(seq, id);
}

/* Process an async request and call the callback. */
GF_IO_CBK(gf_io_async_handler, op, res)
{
    struct timespec t1, t2;
    gf_io_async_t async;
    gf_io_callback_t cbk;

    gf_io_time_now(&t1);
    async = op->async.func;
    res = async->func(op);
    gf_io_latency_check(&t1, &t2, async->name, async->func, async->file,
                        async->line);
    cbk = op->async.cbk;
    cbk->func(op, res);
    gf_io_latency_check(&t2, &t1, cbk->name, cbk->func, cbk->file, cbk->line);
}

#else /* ! DEBUG */

/* Process an async request and call the callback. */
GF_IO_CBK(gf_io_async_handler, op, res)
{
    op->async.cbk(op, op->async.func(op));
}

#endif /* DEBUG */

/* Submit a batch of requests. */
void
gf_io_batch_submit(gf_io_batch_t *batch)
{
    gf_io_request_t *req, *next;
    gf_io_op_t *op;
    uint64_t seq, id;
    uint32_t delta;

    seq = gf_io_reserve(batch->count);
    delta = 0;
    while (!list_empty(&batch->requests)) {
        next = list_first_entry(&batch->requests, gf_io_request_t, list);

        do {
            req = next;
            list_del_init(&req->list);
            next = req->chain;

            id = gf_io_get(seq + delta);
            op = &gf_io.op_pool[id & GF_IO_ID_REQ_MASK];
            delta++;

            *op = req->op;
            if (next != NULL) {
                id |= GF_IO_ID_FLAG_CHAIN;
            }
            id = req->submit(seq, id, op, delta);
            if (req->id != NULL) {
                *req->id = id;
            }
        } while (next != NULL);
    }
}

/* Wait until the specified gf_io_data_t is available. */
uint64_t
gf_io_data_wait(uint32_t idx)
{
    gf_io_worker_t *worker = &gf_io_worker;
    uint64_t value;

    value = gf_io_data_read(idx);
    while (caa_unlikely((value & GF_IO_ID_FLAG_CHAIN) != 0)) {
        if (caa_likely(worker->enabled)) {
            gf_io.engine.worker(worker);
        } else {
            gf_io.engine.flush();
        }

        value = gf_io_data_read(idx);
    }

    return value;
}

/* Callback to stop the current worker. */
GF_IO_CBK(gf_io_worker_stop, op, res, static)
{
    gf_io_worker_t *worker;

    worker = op->worker;

    worker->enabled = false;
    gf_io.engine.worker_stop(worker);
}

/* Terminate the current worker. */
static void
gf_io_worker_stopped(gf_io_worker_t *worker)
{
    if (uatomic_sub_return(&gf_io.num_workers, 1) > 0) {
        gf_io_callback(gf_io_worker_stop, NULL);
        gf_io.engine.flush();
    }
}

/* Prepare the threads of the thread pool. */
static int32_t
gf_io_worker_setup(gf_io_sync_t *sync, gf_io_thread_t *thread)
{
    gf_io_worker_t *worker;
    int32_t res;

    if (thread == NULL) {
        return gf_io_sync_wait(sync, 1, 0);
    }

    worker = &gf_io_worker;
    worker->thread = thread;
    worker->enabled = true;

    thread->data = worker;

    res = gf_io.engine.worker_setup(worker);

    return gf_io_sync_wait(sync, 1, res);
}

/* Main function of the worker threads. */
static int32_t
gf_io_worker_main(gf_io_thread_t *thread)
{
    gf_io_worker_t *worker;

    worker = thread->data;

    while (worker->enabled) {
        gf_io.engine.worker(worker);
    }

    gf_io.engine.worker_cleanup(worker);

    if (caa_unlikely(!gf_io.shutdown)) {
        return -EPIPE;
    }

    gf_io_worker_stopped(worker);

    return 0;
}

/* Stop the workers. */
static void
gf_io_workers_stop(void)
{
    gf_io.shutdown = true;

    gf_io_callback(gf_io_worker_stop, NULL);
    gf_io.engine.flush();
}

/* Callback to wake the calling thread. */
GF_IO_CBK(gf_io_sync_wake, op, res, static)
{
    gf_io_sync_done(op->data, 1, res, false);
}

/* Execute a function in the background and wait for completion. */
static int32_t
gf_io_sync(gf_io_async_t func, void *data, uint32_t timeout, uint32_t retries)
{
    gf_io_sync_t sync;
    int32_t res;

    res = gf_io_sync_start(&sync, 2, timeout, retries, NULL);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    gf_io_async(func, data, gf_io_sync_wake, &sync);
    gf_io.engine.flush();

    return gf_io_sync_done(&sync, 1, 0, true);
}

#if 0

#define TEST_POWER 14

static gf_io_sync_t gf_io_test_sync;

GF_IO_CBK(gf_io_test_callback, op, res, static)
{
    uintptr_t value = (uintptr_t)op->data;

    value++;
    if (value >= 1000) {
        gf_io_sync_done(&gf_io_test_sync, 1, 0, false);
        return;
    }

    if (value <= TEST_POWER) {
        gf_io_callback(gf_io_test_callback, (void *)value);
    }

    gf_io_callback(gf_io_test_callback, (void *)value);

    for (int32_t i = 0; i < 100; i++) {
        caa_cpu_relax();
    }
}

GF_IO_CBK(gf_io_test_timer, op, res, static)
{
    gf_io_sync_done(&gf_io_test_sync, 1, 0, false);

    gf_io_debug(0, "Timer called (%d)", -res);
}

GF_IO_CBK(gf_io_test_cancel, op, res, static)
{
    gf_io_sync_done(&gf_io_test_sync, 1, 0, false);

    gf_io_debug(0, "Cancel called (%d)", -res);
}

#endif

/* Wait until shutdown is initiated and stop all workers. */
static int32_t
gf_io_main(uint32_t workers, gf_io_handlers_t *handlers, void *data)
{
    static int32_t signals[] = {
        SIGSEGV, SIGBUS, SIGILL, SIGSYS, SIGFPE, SIGABRT, SIGCONT, 0
    };
    gf_io_thread_pool_t pool;
    gf_io_thread_pool_config_t cfg;
    int32_t res;

    if (workers > 0) {
        cfg.name = "worker";
        cfg.cpus = NULL;
        cfg.signals = signals;
        cfg.num_threads = workers;
        cfg.stack_size = GF_IO_STACK_SIZE;
        cfg.priority = 0;
        cfg.first_id = 1;
        cfg.timeout = GF_IO_INIT_TIMEOUT;
        cfg.retries = GF_IO_INIT_RETRIES;
        cfg.setup = gf_io_worker_setup;
        cfg.main = gf_io_worker_main;

        res = gf_io_thread_pool_start(&pool, &cfg);
        if (caa_unlikely(res < 0)) {
            return res;
        }
    }

#if 0
    {
        uint64_t timer;

        gf_io_sync_start(&gf_io_test_sync, (1 << TEST_POWER) + 1, 3600, NULL);
        gf_io_debug(0, "TEST:CALLBACK:BEGIN");
        gf_io_callback(gf_io_test_callback, (void *)0);
        gf_io.engine.flush();
        gf_io_sync_done(&gf_io_test_sync, 1, 0, true);
        gf_io_debug(0, "TEST:CALLBACK:END");

        gf_io_sync_start(&gf_io_test_sync, 2, 60, NULL);
        gf_io_debug(0, "TEST:TIMER:BEGIN");
        gf_io_delay_rel_ms(gf_io_test_timer, 10000, NULL);
        gf_io.engine.flush();
        gf_io_sync_done(&gf_io_test_sync, 1, 0, true);
        gf_io_debug(0, "TEST:TIMER:END");

        gf_io_sync_start(&gf_io_test_sync, 3, 60, NULL);
        gf_io_debug(0, "TEST:CANCEL:BEGIN");
        timer = gf_io_delay_rel_ms(gf_io_test_timer, 10000, NULL);
        gf_io.engine.flush();
        sleep(5);
        gf_io_cancel(gf_io_test_cancel, timer, NULL);
        gf_io.engine.flush();
        gf_io_sync_done(&gf_io_test_sync, 1, 0, true);
        gf_io_debug(0, "TEST:CANCEL:END");
    }

#endif

    res = gf_io_sync(handlers->setup, data, GF_IO_HANDLER_TIMEOUT,
                     GF_IO_HANDLER_RETRIES);
    if (caa_likely(res >= 0)) {
        res = gf_io.engine.wait();

        gf_io_sync(handlers->cleanup, data, GF_IO_HANDLER_TIMEOUT,
                   GF_IO_HANDLER_RETRIES);
    }

    if (workers > 0) {
        gf_io_workers_stop();
        gf_io_thread_pool_wait(&pool, GF_IO_INIT_TIMEOUT);
    }

    return res;
}

static int32_t
gf_io_setup(void)
{
    int32_t res;

    res = gf_io_alloc_array(gf_io.op_map, GF_IO_ID_REQ_COUNT);
    if (caa_unlikely(res < 0)) {
        return res;
    }

    res = gf_io_alloc_array(gf_io.op_pool, GF_IO_ID_REQ_COUNT);
    if (caa_unlikely(res < 0)) {
        gf_io_free_array(gf_io.op_map, GF_IO_ID_REQ_COUNT);
    }

    return res;
}

static void
gf_io_cleanup(void)
{
    gf_io_free_array(gf_io.op_pool, GF_IO_ID_REQ_COUNT);
    gf_io_free_array(gf_io.op_map, GF_IO_ID_REQ_COUNT);
}

static void
gf_io_init(const gf_io_engine_t *engine, uint32_t workers)
{
    uint32_t i;

    gf_io.engine = *engine;
    gf_io.num_workers = workers;
    gf_io.shutdown = false;
    gf_io.op_seq = 0;

    for (i = 0; i < GF_IO_ID_REQ_COUNT; i++) {
        gf_io.op_map[i] = i;
    }
}

/* Main entry function. */
int32_t
gf_io_run(const char *name, gf_io_handlers_t *handlers, void *data)
{
    const gf_io_engine_t *engine;
    uint32_t i;
    int32_t res;

    memset(&gf_io, 0, sizeof(gf_io));

    res = gf_io_setup();
    if (caa_unlikely(res < 0)) {
        return res;
    }

    i = 0;
    for (i = 0; gf_io_engines[i] != NULL; i++) {
        engine = gf_io_engines[i];
        if ((name != NULL) && (strcmp(engine->name, name) != 0)) {
            continue;
        }

        gf_io_debug(0, "Trying I/O engine '%s'", engine->name);

        res = engine->setup();
        if (res >= 0) {
            gf_io_debug(0, "I/O engine '%s' is ready", engine->name);

            gf_io_init(engine, res);

            res = gf_io_main(res, handlers, data);

            engine->cleanup();

            if (caa_likely(res >= 0)) {
                gf_io_cleanup();

                return res;
            }
        }

        gf_io_debug(res, "Unable to use I/O engine '%s'", engine->name);
    }

    gf_io_log(-ENXIO, LG_MSG_IO_NO_ENGINE);

    gf_io_cleanup();

    return -ENXIO;
}
