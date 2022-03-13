/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __GF_IO_COMMON_H__
#define __GF_IO_COMMON_H__

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <pthread.h>
#ifdef __FreeBSD__
#include <pthread_np.h>
#undef BIT_SET
#endif
#include <errno.h>

#include <urcu/compiler.h>

#include <glusterfs/logging.h>
#include <glusterfs/libglusterfs-messages.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/compat-errno.h>

/* A helper macro to create log messages. It requires two additional macros
 * for each message id that define the level of the message and its format
 * string. These macros must have the same name as the message id, but adding
 * the suffixes _LVL and _FMT. */
#define gf_io_log(_res, _msg, _args...)                                        \
    gf_msg("io", _msg##_LVL(_res), -(_res), _msg, _msg##_FMT, ##_args)

/* A helper macro for debug log messages. */
#define gf_io_debug(_res, _fmt, _args...)                                      \
    gf_msg_debug("io", -(_res), _fmt, ##_args)

/* The I/O framework represents errors as a negative errno values. 0 or a
 * positive number are considered successes (and possibly the return value
 * of a function). These values are referred as 'res', from 'result'.
 *
 * The following macros convert standard return values from system calls or
 * any other function call into 'res' values. The conversion is strict in
 * the sense that if an error is expected but none is found, -EUCLEAN will
 * be returned, indicating that something weird happened. */

/* From 'res' to 'res'. */
#define gf_io_convert_from_res(_res) (_res)

/* From a return code to 'res' (for example pthread_mutex_lock()). */
#define gf_io_convert_from_ret(_ret)                                           \
    ({                                                                         \
        int32_t __gf_io_convert_res = -(_ret);                                 \
        if (caa_unlikely(__gf_io_convert_res > 0)) {                           \
            __gf_io_convert_res = -EUCLEAN;                                    \
        }                                                                      \
        __gf_io_convert_res;                                                   \
    })

/* From an errno code to 'res' (for example open()). */
#define gf_io_convert_from_errno(_ret)                                         \
    ({                                                                         \
        int32_t __gf_io_convert_res = (_ret);                                  \
        if (caa_unlikely(__gf_io_convert_res < 0)) {                           \
            if (caa_unlikely((__gf_io_convert_res != -1) || (errno <= 0))) {   \
                __gf_io_convert_res = -EUCLEAN;                                \
            } else {                                                           \
                __gf_io_convert_res = -errno;                                  \
            }                                                                  \
        }                                                                      \
        __gf_io_convert_res;                                                   \
    })

/* From an errno code to 'res' when the function can only return 0 or -1
 * (for example close()). */
#define gf_io_convert_from_errno0(_ret)                                        \
    ({                                                                         \
        int32_t __gf_io_convert_res = (_ret);                                  \
        if (caa_unlikely(__gf_io_convert_res != 0)) {                          \
            if (caa_unlikely((__gf_io_convert_res != -1) || (errno <= 0))) {   \
                __gf_io_convert_res = -EUCLEAN;                                \
            } else {                                                           \
                __gf_io_convert_res = -errno;                                  \
            }                                                                  \
        }                                                                      \
        __gf_io_convert_res;                                                   \
    })

/* These macros make it possible to filter some errors to prevent them to
 * be logged. For example a read() call could fail with error EINTR. We
 * are probably interested in not writting this message to the log and
 * retry the read. */

/* All errors are logged. */
#define gf_io_filter_no_error(_res) false

/* All errors but EINTR are logged. */
#define gf_io_filter_may_intr(_res) ((_res) == -EINTR)

/* All errors but ETIMEDOUT are logged. */
#define gf_io_filter_may_timeout(_res) ((_res) == -ETIMEDOUT)

/* This macro helps combining nested errors. We only consider the first
 * error. Any other following error will be ignored and the first error
 * will be preserved. */
#define gf_io_error_combine(_current, _new)                                    \
    ({                                                                         \
        int32_t __gf_io_error_current = (_current);                            \
        int32_t __gf_io_error_new = (_new);                                    \
        if (__gf_io_error_current >= 0) {                                      \
            __gf_io_error_current = __gf_io_error_new;                         \
        }                                                                      \
        __gf_io_error_current;                                                 \
    })

/* Macro to call a function with arguments, check for errors and log a
 * message if necessary. '_filter' and '_convert' need to be the suffixes
 * of the previous macros. For example, '_filter' can be 'no_error', and
 * '_convert' can be 'from_ret'. */
#define gf_io_call(_filter, _convert, _func, _args...)                         \
    ({                                                                         \
        int32_t __gf_io_call_res = gf_io_convert_##_convert(_func(_args));     \
        if (caa_unlikely(__gf_io_call_res < 0)) {                              \
            if (caa_unlikely(!gf_io_filter_##_filter(__gf_io_call_res))) {     \
                gf_io_log(__gf_io_call_res, LG_MSG_IO_CALL_FAILED, #_func);    \
            }                                                                  \
        }                                                                      \
        __gf_io_call_res;                                                      \
    })

/* Specialized gf_io_call() macro for functions that return 'res'. */
#define gf_io_call_res(_func, _args...)                                        \
    gf_io_call(no_error, from_res, _func, ##_args)

/* Specialized gf_io_call() macro for functions that return an error code. */
#define gf_io_call_ret(_func, _args...)                                        \
    gf_io_call(no_error, from_ret, _func, ##_args)

/* Specialized gf_io_call() macro for functions that return -1 or a positive
 * number and errno */
#define gf_io_call_errno(_func, _args...)                                      \
    gf_io_call(no_error, from_errno, _func, ##_args)

/* Specialized gf_io_call() macro for functions that return -1 or 0. */
#define gf_io_call_errno0(_func, _args...)                                     \
    gf_io_call(no_error, from_errno0, _func, ##_args)

/* Macro to make sure 'res' is a success. Otherwise it aborts the program.
 * This can be used when an error should never happen but if it happens, it
 * makes it very hard or impossible to return to a stable state. */
#define gf_io_success(_res)                                                    \
    do {                                                                       \
        if (caa_unlikely((_res) < 0)) {                                        \
            GF_ABORT();                                                        \
        }                                                                      \
    } while (0)

/* Definitions of log levels and formats for each message. */
#define LG_MSG_IO_CALL_FAILED_LVL(_res) GF_LOG_ERROR
#define LG_MSG_IO_CALL_FAILED_FMT "%s() failed."

/* Structure used for synchronization between multiple workers. */
typedef struct _gf_io_sync {
    /* Mutex to control access to shared data. */
    pthread_mutex_t mutex;

    /* Condition variable to wait for the right state. */
    pthread_cond_t cond;

    /* Absolute time for a timeout, using a monotonic clock. */
    struct timespec abs_to;

    /* Opaque data that callers can use freely. */
    void *data;

    /* Number of seconds for timeout. */
    uint32_t timeout;

    /* Number of retries after a timeout. */
    uint32_t retries;

    /* Number of threads to synchronize. */
    uint32_t count;

    /* Current synchronization phase. */
    uint32_t phase;

    /* Number of threads pending to be synchronized. */
    uint32_t pending;

    /* Result of the synchronization. */
    int32_t res;
} gf_io_sync_t;

typedef struct _gf_io_thread_pool {
    /* Mutex to control additions and removals of threads. */
    pthread_mutex_t mutex;

    /* List of threads of this pool. */
    struct list_head threads;
} gf_io_thread_pool_t;

/* Structure used to keep data for each thread of a pool. */
typedef struct _gf_io_thread {
    /* Member of the gf_io_thread_pool_t.threads. */
    struct list_head list;

    /* Reference to the thread pool. */
    gf_io_thread_pool_t *pool;

    /* Id of the thread. */
    pthread_t id;

    /* Opaque data that the user can use. */
    void *data;

    /* Index of the thread. */
    uint32_t index;
} gf_io_thread_t;

/* Initialization function signature for a thread of the thread pool. */
typedef int32_t (*gf_io_thread_setup_t)(gf_io_sync_t *sync,
                                        gf_io_thread_t *thread);

/* Main function signature for a thread of the thread pool. */
typedef int32_t (*gf_io_thread_main_t)(gf_io_thread_t *thread);

/* Definition of a thread pool. */
typedef struct _gf_io_thread_pool_config {
    /* All threads will have this name as part of the name of the thread. */
    const char *name;

    /* Reference to the thread pool. */
    gf_io_thread_pool_t *pool;

#ifdef GF_LINUX_HOST_OS
    /* CPU bitmap to set affinity of each thread. If it's NULL, no affinity
     * is set. */
    cpu_set_t *cpus;
#else
    void *cpus;
#endif

    /* Function to configure each thread. */
    gf_io_thread_setup_t setup;

    /* Main function to run in each thread. */
    gf_io_thread_main_t main;

    /* Array of signals not blocked by the threads. Last item must be 0. */
    int32_t *signals;

    /* Number of threads of the thread pool. */
    uint32_t num_threads;

    /* Size of the stack for each thread. */
    uint32_t stack_size;

    /* Priority to set to each thread. 0 means default. A negative value
     * sets RR (Round-Robin) scheduler, and a positive value sets FIFO
     * scheduler. The absolute value must be between 1 and 100 and represents
     * the priority for the scheduler. */
    int32_t priority;

    /* First id to use in the name of the threads. */
    uint32_t first_id;

    /* Index used to create each thread. */
    uint32_t index;

    /* Timeout (in seconds) of an initialization attempt of the thread pool. */
    uint32_t timeout;

    /* Maximum number of retries. The maximum timeout for the initialization
     * is 'timeout' * 'retries'. */
    uint32_t retries;
} gf_io_thread_pool_config_t;

/* Initializes a sync object to synchronize 'count' entities with a maximum
 * delay of 'timeout' seconds. */
int32_t
gf_io_sync_start(gf_io_sync_t *sync, uint32_t count, uint32_t timeout,
                 uint32_t retries, void *data);

/* Notifies completion of 'count' entities. Optionally it can wait until
 * all other threads have also notified. Only one thread can wait. */
int32_t
gf_io_sync_done(gf_io_sync_t *sync, uint32_t count, int32_t res, bool wait);

/* Wait for a synchronization point. 'count' represents the number of
 * entities waiting, and 'res' the result of the operation done just
 * before synchronizing. The return value will be 0 only if all entities
 * completed without error (i.e. 'res' was >= 0 in all calls to this
 * function). */
int32_t
gf_io_sync_wait(gf_io_sync_t *sync, uint32_t count, int32_t res);

/* Start a thread pool. */
int32_t
gf_io_thread_pool_start(gf_io_thread_pool_t *pool,
                        gf_io_thread_pool_config_t *cfg);

/* Wait for termination of all threads of the thread pool. */
void
gf_io_thread_pool_wait(gf_io_thread_pool_t *pool, uint32_t timeout);

#endif /* __GF_IO_COMMON_H__ */
