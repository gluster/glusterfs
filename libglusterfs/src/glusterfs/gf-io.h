/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __GF_IO_H__
#define __GF_IO_H__

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>

#include <urcu/uatomic.h>

#include <glusterfs/gf-io-common.h>
#include <glusterfs/syscall.h>

/* Some macros to deal with request IDs. */

/* A request ID has 3 fields:
 *
 *                   56 - N bits                8 bits       N bits
 *   +----------------------------------------+--------+----------------+
 *   |                 Counter                | Flags  |     Index      |
 *   +----------------------------------------+--------+----------------+
 *
 * N determines the total number of requests that can be in-flight at any
 * given point in time (= 2^N) and, indirectly, the maximum number of IOPS
 * that can be delivered (~ 2^N / lat, where 'lat' is the average latency
 * of each request)
 *
 * Assuming a very bad average latency of 50 ms, using N=16 we could still
 * deliver more than 1 million IOPS in theory.
 *
 * Counter: Value incremented each time the corresponding request is used.
 * (58 - N) This makes sure that the same gf_io_data_t object will have a
 *          unique id for each request.
 *
 * Index:   The index into the gf_io.data_pool of the gf_io_data_t object
 *          represented by this id.
 *
 * Flags:   Reserved bits for internal use of the engine.
 *
 * The layout has been made in a way that simply clearing the flags and
 * counter, the resulting value can be interpreted as a byte offset into
 * the gf_io.data_pool array (each gf_io_data_t is 64 bytes long). */

/* TODO: For now, N is fixed to 16. It needs to be tested whether it's a good
 *       value or not, or if it would be better to make it configurable. */

#define GF_IO_ID_REQ_BITS 16
#define GF_IO_ID_FLG_BITS 8

#define GF_IO_ID_REQ_COUNT (1ULL << GF_IO_ID_REQ_BITS)

#define GF_IO_MASK(_bits, _shift) (((1ULL << (_bits)) - 1ULL) << (_shift))

#define GF_IO_ID_REQ_MASK GF_IO_MASK(GF_IO_ID_REQ_BITS, 0)
#define GF_IO_ID_FLG_MASK GF_IO_MASK(GF_IO_ID_FLG_BITS, GF_IO_ID_REQ_BITS)

#define GF_IO_ID_FLAG_CHAIN (1ULL << (GF_IO_ID_REQ_BITS + 0))
#define GF_IO_ID_FLAG_1 (1ULL << (GF_IO_ID_REQ_BITS + 1))
#define GF_IO_ID_FLAG_2 (1ULL << (GF_IO_ID_REQ_BITS + 2))
#define GF_IO_ID_FLAG_3 (1ULL << (GF_IO_ID_REQ_BITS + 3))
#define GF_IO_ID_FLAG_4 (1ULL << (GF_IO_ID_REQ_BITS + 4))
#define GF_IO_ID_FLAG_5 (1ULL << (GF_IO_ID_REQ_BITS + 5))
#define GF_IO_ID_FLAG_6 (1ULL << (GF_IO_ID_REQ_BITS + 6))
#define GF_IO_ID_FLAG_7 (1ULL << (GF_IO_ID_REQ_BITS + 7))

#define GF_IO_ID_COUNTER_UNIT (1ULL << (GF_IO_ID_REQ_BITS + GF_IO_ID_FLG_BITS))

/* Define the initialization timeout and number of retries. */
#define GF_IO_INIT_TIMEOUT 3
#define GF_IO_INIT_RETRIES 20

/* Define the timeout and number of retries for init/fini handlers. */
#define GF_IO_HANDLER_TIMEOUT 3
#define GF_IO_HANDLER_RETRIES 20

/* Forward declaration of some structures. */

/* Data related to an operation. */
struct _gf_io_op;
typedef struct _gf_io_op gf_io_op_t;

/* This structure is used only when a request will be sent in a batch.
 * Otherwise it's not needed. */
struct _gf_io_request;
typedef struct _gf_io_request gf_io_request_t;

/* Enumeration of all defined engines. */
typedef enum _gf_io_mode {
    GF_IO_MODE_LEGACY,
    GF_IO_MODE_IO_URING,
    GF_IO_MODE_THREADED,
    GF_IO_MODE_COUNT
} gf_io_mode_t;

#ifdef DEBUG

typedef struct _gf_io_callback {
    void (*func)(gf_io_op_t *op, int32_t res);
    const char *name;
    const char *file;
    uint32_t line;
} *gf_io_callback_t;

typedef struct _gf_io_async {
    int32_t (*func)(gf_io_op_t *op);
    const char *name;
    const char *file;
    uint32_t line;
} *gf_io_async_t;

#define GF_IO_CBK(_name, _op, _res, _args...)                                  \
    static void __gf_io_cbk_##_name(gf_io_op_t *_op, int32_t _res);            \
    _args const gf_io_callback_t _name = &(struct _gf_io_callback){            \
        .func = __gf_io_cbk_##_name,                                           \
        .name = #_name,                                                        \
        .file = __FILE__,                                                      \
        .line = __LINE__                                                       \
    };                                                                         \
    static void __gf_io_cbk_##_name(gf_io_op_t *_op, int32_t _res)

#define GF_IO_ASYNC(_name, _op, _args...)                                      \
    static int32_t __gf_io_async_##_name(gf_io_op_t *_op);                     \
    _args const gf_io_async_t _name = &(struct _gf_io_async){                  \
        .func = __gf_io_async_##_name,                                         \
        .name = #_name,                                                        \
        .file = __FILE__,                                                      \
        .line = __LINE__                                                       \
    };                                                                         \
    static int32_t __gf_io_async_##_name(gf_io_op_t *_op)

#define GF_IO_CBK_DECLARE(_name) extern const gf_io_callback_t _name
#define GF_IO_ASYNC_DECLARE(_name) extern const gf_io_async_t _name

#else /* ! DEBUG */

typedef void (*gf_io_callback_t)(gf_io_op_t *op, int32_t res);
typedef int32_t (*gf_io_async_t)(gf_io_op_t *op);

#define GF_IO_CBK(_name, _op, _res, _args...)                                  \
    _args void _name(gf_io_op_t *_op, int32_t _res)

#define GF_IO_ASYNC(_name, _op, _args...) _args int32_t _name(gf_io_op_t *_op)

#define GF_IO_CBK_DECLARE(_name) void _name(gf_io_op_t *, int32_t)
#define GF_IO_ASYNC_DECLARE(_name) int32_t _name(gf_io_op_t *)

#endif /* DEBUG */

/* The data necessary to control a worker (i.e. a thread) processing
 * requests. */
typedef struct _gf_io_worker {
    /* Thread associated to this worker. */
    gf_io_thread_t *thread;

    /* Determines if the worker is active or not. A disabled worker won't
     * process callbacks and will terminate as soon as possible. */
    bool enabled;
} gf_io_worker_t;

/* Data related to an operation. */
struct _gf_io_op {
    union {
        /* The callback to execute once the request completes. */
        gf_io_callback_t cbk;

        /* The worker that is executing the callback. */
        gf_io_worker_t *worker;
    };

    /* Opaque data passed by the user. */
    void *data;

    /* Data specific to each request type. */
    union {
        struct {
            /* The async function to call. */
            gf_io_async_t func;

            /* The callback to process once the async function completes. */
            gf_io_callback_t cbk;

            /* Opaque data for the callback. */
            void *data;
        } async;

        struct {
            /* Id of the request to cancel. */
            uint64_t id;
        } cancel;
    };
};

/* Structure to keep a list of requests that will be sent together in a
 * single shot. */
typedef struct _gf_io_batch {
    /* List of requests in this batch. */
    struct list_head requests;

    /* Total number of requests. */
    uint32_t count;
} gf_io_batch_t;

typedef uint64_t (*gf_io_engine_op_t)(uint64_t seq, uint64_t id, gf_io_op_t *op,
                                      uint32_t count);

/* This structure is used only when a request will be sent in a batch.
 * Otherwise it's not needed. */
struct _gf_io_request {
    /* Basic data for the operation. */
    gf_io_op_t op;

    /* Member of the batch. */
    struct list_head list;

    /* Engine function to call to submit the request. */
    gf_io_engine_op_t submit;

    /* Next request in a chain. Chained requests are executed sequentially
     * when processed, even if they are submitted simultaneously. All
     * requests in a chain must belong to the same batch. */
    gf_io_request_t *chain;

    /* Location where the 'id' of this request will be stored once the
     * batch containing it is submitted. It can be NULL if it's not needed. */
    uint64_t *id;

#ifdef DEBUG
    /* In debug builds, this field is used to do strict checks that all
     * requests are correctly added to the same batch. */
    gf_io_batch_t *batch;
#endif
};

/* Handlers to initialization and termination functions for the other
 * components of the system. */
typedef struct _gf_io_handlers {
    /* Function to execute as the first thing once an I/O engine is started. */
    gf_io_async_t setup;

    /* Function to execute just before stopping the I/O engine. */
    gf_io_async_t cleanup;
} gf_io_handlers_t;

/* Definition of an I/O engine. */
typedef struct _gf_io_engine {
    /* Name of the engine. */
    const char *name;

    /* Function to initialize and prepare the engine. */
    int32_t (*setup)(void);

    /* Function to release resources once the engine has stopped. */
    void (*cleanup)(void);

    /* Function that will only return when the engine has stopped. */
    int32_t (*wait)(void);

    /* Function to initialize and prepare a worker. */
    int32_t (*worker_setup)(gf_io_worker_t *worker);

    /* Function to release resources of a worker once it has stopped. */
    void (*worker_cleanup)(gf_io_worker_t *worker);

    /* Function to force termination of a worker. */
    void (*worker_stop)(gf_io_worker_t *worker);

    /* Main function of a worker. It's the core processor of I/O requests. */
    int32_t (*worker)(gf_io_worker_t *worker);

    /* Function to force processing pending requests. */
    void (*flush)(void);

    /* Function to cancel a previous request. */
    gf_io_engine_op_t cancel;

    /* Function to call a callback in the background. */
    gf_io_engine_op_t callback;

    /* Mode of operation of the engine. */
    gf_io_mode_t mode;
} gf_io_engine_t;

/* Global core structure containing the most critical data for the I/O
 * framework. */
typedef struct _gf_io {
    /* The active engine. */
    gf_io_engine_t engine;

    /* Pool of gf_io_data_t objects to use for requests. The size of this
     * pool determines the maximum number of requests that can be in flight
     * simultaneously. Except for some long lasting operations (for example
     * waiting for connections) the vast majority of requests will be
     * processed very fast. Assuming a very bad average latency of 50 ms
     * per request, a pool of only 65536 objects would allow more than 1.3
     * million IOPS. */
    gf_io_op_t *op_pool;

    /* Array of offsets (in bytes) to available gf_io_data_t objects in the
     * 'data_pool'. It has the same size as the 'data_pool' and it works as
     * a circular queue. Next available items are taken from 'data_seq'
     * offset, and newly released items are added to a position controlled
     * by the engine. */
    uint64_t *op_map;

    /* Current request sequence number. Used to index 'data_map'. */
    uint64_t op_seq;

    /* Number of running workers. */
    uint32_t num_workers;

    /* Set when the I/O framework is stopping. */
    bool shutdown;
} gf_io_t;

/* Global I/O object. */
extern gf_io_t gf_io;

/* Worker object. */
extern __thread gf_io_worker_t gf_io_worker;

static inline gf_io_mode_t
gf_io_mode(void)
{
    return gf_io.engine.mode;
}

/* Main entry point to the I/O framework. It starts everything and controls
 * execution. It doesn't return until the process is going to terminate. */
int32_t
gf_io_run(const char *name, gf_io_handlers_t *handlers, void *data);

/* Get the current worker. */
static inline gf_io_worker_t *
gf_io_worker_get(void)
{
    gf_io_worker_t *worker;

    worker = &gf_io_worker;
    if (worker->enabled) {
        return worker;
    }

    return NULL;
}

/* Reserves some sequence numbers to be used for requests. */
static inline uint64_t
gf_io_reserve(uint32_t nr)
{
    return uatomic_add_return(&gf_io.op_seq, nr) - nr;
}

/* Wait for a particular entry in gf_io.data_map to be available. */
uint64_t
gf_io_data_wait(uint32_t idx);

/* Load the index of a gf_io_data_t object from gf_io.data_pool. */
static inline uint64_t
gf_io_data_read(uint32_t idx)
{
    return CMM_LOAD_SHARED(gf_io.op_map[idx]);
}

/* Store the index of a free gf_io_data_t from gf_io.data_pool. */
static inline void
gf_io_data_write(uint32_t idx, uint64_t value)
{
    CMM_STORE_SHARED(gf_io.op_map[idx], value);
}

/* Return an available gf_io_data_t object from a sequence number. */
static inline uint64_t
gf_io_get(uint64_t seq)
{
    uint64_t value;
    uint32_t idx;

    idx = seq & GF_IO_ID_REQ_MASK;
    value = gf_io_data_read(idx);
    if (caa_unlikely((value & GF_IO_ID_FLAG_CHAIN) != 0)) {
        value = gf_io_data_wait(idx);
    }
    gf_io_data_write(idx, value | GF_IO_ID_FLAG_CHAIN);

    cmm_smp_rmb();

    return value;
}

/* Add a free gf_io_data_t object to the queue. */
static inline void
gf_io_put(uint64_t seq, uint64_t id)
{
    id = (id & ~GF_IO_ID_FLG_MASK) + GF_IO_ID_COUNTER_UNIT;

    cmm_smp_wmb();

    gf_io_data_write(seq & GF_IO_ID_REQ_MASK, id);
}

#ifdef DEBUG

/* Run a callback for a request id and release the associated gf_io_data_t
 * afterwards. */
void
gf_io_cbk(gf_io_worker_t *worker, uint64_t seq, uint64_t id, int32_t res);

#else /* ! DEBUG */

/* Run a callback for a request id and release the associated gf_io_data_t
 * afterwards. */
static inline void
gf_io_cbk(gf_io_worker_t *worker, uint64_t seq, uint64_t id, int32_t res)
{
    gf_io_op_t *op;
    gf_io_callback_t cbk;

    op = &gf_io.op_pool[id & GF_IO_ID_REQ_MASK];

    cbk = op->cbk;
    op->worker = worker;

    cbk(op, res);

    gf_io_put(seq, id);
}

#endif /* DEBUG */

/* Prepare a batch of requests. */
static inline void
gf_io_batch_init(gf_io_batch_t *batch)
{
    INIT_LIST_HEAD(&batch->requests);
    batch->count = 0;
}

/* Add a request to a batch. */
static inline void
gf_io_batch_add(gf_io_batch_t *batch, gf_io_request_t *req, uint64_t *id)
{
#ifdef DEBUG
    if (caa_unlikely(req->batch != NULL)) {
        GF_ABORT();
    }
    req->batch = batch;
#endif

    req->id = id;

    list_add_tail(&req->list, &batch->requests);
    batch->count++;
}

/* Submit a batch of requests. */
void
gf_io_batch_submit(gf_io_batch_t *batch);

/* Chain two requests so that they will be executed sequentially. Both must
 * belong to the same batch. */
static inline void
gf_io_request_chain(gf_io_request_t *req, gf_io_request_t *next)
{
#ifdef DEBUG
    if (caa_unlikely(req->batch == NULL) ||
        caa_unlikely(req->batch != next->batch) ||
        caa_unlikely(req->chain != NULL)) {
        GF_ABORT();
    }
#endif

    req->chain = next;
}

/* Common initialization done to all requests that will be sent through a
 * batch. */
static inline void
gf_io_prepare_common(gf_io_request_t *req, gf_io_engine_op_t op,
                     gf_io_callback_t cbk, void *data)
{
    req->op.cbk = cbk;
    req->op.data = data;

    req->submit = op;
    req->chain = NULL;

#ifdef DEBUG
    INIT_LIST_HEAD(&req->list);
    req->batch = NULL;
#endif
}

/* Common initialization done to all requests sent individually. */
static inline gf_io_op_t *
gf_io_single_common(uint64_t id, gf_io_callback_t cbk, void *data)
{
    gf_io_op_t *op;

    op = &gf_io.op_pool[id & GF_IO_ID_REQ_MASK];
    op->cbk = cbk;
    op->data = data;

    return op;
}

/* Operation 'cancel' */

static inline void
gf_io_cancel_common(gf_io_op_t *op, uint64_t id)
{
    op->cancel.id = id;
}

static inline uint64_t
gf_io_cancel(gf_io_callback_t cbk, uint64_t ref, void *data)
{
    gf_io_op_t *op;
    uint64_t seq, id;

    seq = gf_io_reserve(1);
    id = gf_io_get(seq);
    op = gf_io_single_common(id, cbk, data);
    gf_io_cancel_common(op, ref);

    return gf_io.engine.cancel(seq, id, op, 1);
}

static inline void
gf_io_cancel_prepare(gf_io_request_t *req, gf_io_callback_t cbk, uint64_t id,
                     void *data)
{
    gf_io_prepare_common(req, gf_io.engine.cancel, cbk, data);
    gf_io_cancel_common(&req->op, id);
}

/* Operation 'callback' */

static inline uint64_t
gf_io_callback(gf_io_callback_t cbk, void *data)
{
    gf_io_op_t *op;
    uint64_t seq, id;

    seq = gf_io_reserve(1);
    id = gf_io_get(seq);
    op = gf_io_single_common(id, cbk, data);

    return gf_io.engine.callback(seq, id, op, 1);
}

static inline void
gf_io_callback_prepare(gf_io_request_t *req, gf_io_callback_t cbk, void *data)
{
    gf_io_prepare_common(req, gf_io.engine.callback, cbk, data);
}

/* Operation 'async' */

GF_IO_CBK_DECLARE(gf_io_async_handler);

static inline void
gf_io_async_common(gf_io_op_t *op, gf_io_async_t async, gf_io_callback_t cbk,
                   void *data)
{
    op->async.func = async;
    op->async.cbk = cbk;
    op->async.data = data;
}

static inline uint64_t
gf_io_async(gf_io_async_t async, void *data, gf_io_callback_t cbk,
            void *cbk_data)
{
    gf_io_op_t *op;
    uint64_t seq, id;

    seq = gf_io_reserve(1);
    id = gf_io_get(seq);
    op = gf_io_single_common(id, gf_io_async_handler, cbk_data);
    gf_io_async_common(op, async, cbk, data);

    return gf_io.engine.callback(seq, id, op, 1);
}

static inline void
gf_io_async_prepare(gf_io_request_t *req, gf_io_async_t async, void *data,
                    gf_io_callback_t cbk, void *cbk_data)
{
    gf_io_prepare_common(req, gf_io.engine.callback, gf_io_async_handler,
                         cbk_data);
    gf_io_async_common(&req->op, async, cbk, data);
}

#endif /* __GF_IO_H__ */
