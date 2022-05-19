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

/* The I/O framework represents errors as a negative errno values. 0 or a
 * positive number are considered successes (and possibly the return value
 * of a function). These values are referred as 'res', from 'result'.
 *
 * The following macros convert and validate standard return values from
 * system calls or any other function call into 'res' values. The conversion
 * is strict in the sense that if an error is expected but none is found,
 * -EUCLEAN will be returned, indicating that something weird happened.
 *
 * They are implemented as macros instead of functions so that the log message
 * references the real position that triggered the error (file, function and
 * line). */

/* Validate and convert an errno value into a res. */
#define gf_errno_check(_err)                                                   \
    ({                                                                         \
        int32_t __gf_errno_check = -(_err);                                    \
        if (caa_unlikely(__gf_errno_check >= 0)) {                             \
            GF_LOG_C("io", LG_MSG_IO_BAD_ERRNO(-__gf_errno_check));            \
            __gf_errno_check = -EUCLEAN;                                       \
        }                                                                      \
        __gf_errno_check;                                                      \
    })

/* Validate the return value of an errno function and return the corresponding
 * res. */
#define gf_ret_check(_ret)                                                     \
    ({                                                                         \
        int32_t __gf_ret_check = (_ret);                                       \
        if (caa_likely(__gf_ret_check == -1)) {                                \
            __gf_ret_check = gf_errno_check(errno);                            \
        } else {                                                               \
            GF_LOG_C("io", LG_MSG_IO_BAD_RETURN(__gf_ret_check));              \
            __gf_ret_check = -EUCLEAN;                                         \
        }                                                                      \
        __gf_ret_check;                                                        \
    })

/* Convert the return value from an errno function into a res. In case of
 * success the function may return a positive value. */
#define gf_res_errno(_ret)                                                     \
    ({                                                                         \
        int32_t __gf_res_errno = (_ret);                                       \
        if (__gf_res_errno < 0) {                                              \
            __gf_res_errno = gf_ret_check(__gf_res_errno);                     \
        }                                                                      \
        __gf_res_errno;                                                        \
    })

/* Convert the return value from an errno function into a res. In case of
 * success the function always returns 0. */
#define gf_res_errno0(_ret)                                                    \
    ({                                                                         \
        int32_t __gf_res_errno0 = (_ret);                                      \
        if (__gf_res_errno0 != 0) {                                            \
            __gf_res_errno0 = gf_ret_check(__gf_res_errno0);                   \
        }                                                                      \
        __gf_res_errno0;                                                       \
    })

/* Convert an error code into a res. */
#define gf_res_err(_err)                                                       \
    ({                                                                         \
        int32_t __gf_res_err = -(_err);                                        \
        if (__gf_res_err < 0) {                                                \
            GF_LOG_E("io", LG_MSG_IO_BAD_RETURN(-__gf_res_err));               \
            __gf_res_err = -EUCLEAN;                                           \
        }                                                                      \
        __gf_res_err;                                                          \
    })

/* Convert a pointer into a res. */
#define gf_res_ptr(_pptr, _ptr, _errval...)                                    \
    ({                                                                         \
        void *__gf_res_ptr = (_ptr);                                           \
        int32_t __gf_res_ptr_res = 0;                                          \
        if (__gf_res_ptr != GLFS_DEF(NULL, ##_errval)) {                       \
            *(_pptr) = __gf_res_ptr;                                           \
        } else {                                                               \
            __gf_res_ptr_res = gf_errno_check(errno);                          \
        }                                                                      \
        __gf_res_ptr_res;                                                      \
    })

/* Helper to log a message in case of an error result. */
#define gf_check(_name, _lvl, _func, _res)                                     \
    ({                                                                         \
        int32_t __gf_check = (_res);                                           \
        if (caa_unlikely(__gf_check < 0)) {                                    \
            GF_LOG(_name, _lvl, LG_MSG_IO_CALL_FAILED(_func, __gf_check));     \
        }                                                                      \
        __gf_check;                                                            \
    })

/* Macro to make sure 'res' is a success. Otherwise it aborts the program.
 * This can be used when an error should never happen but if it happens, it
 * makes it very hard or impossible to return to a stable state. */
#define gf_succeed(_name, _func, _res)                                         \
    do {                                                                       \
        int32_t __gf_succeed = (_res);                                         \
        if (caa_unlikely((__gf_succeed) < 0)) {                                \
            gf_check(_name, GF_LOG_CRITICAL, _func, __gf_succeed);             \
            GF_ABORT();                                                        \
        }                                                                      \
    } while (0)

/* Combine two res values. If we already have a negative res, we keep its
 * value. Otherwise the new error, if any, is propagated. */
static inline int32_t
gf_res_combine(int32_t current, int32_t res)
{
    if (caa_unlikely(res < 0)) {
        if (current >= 0) {
            current = res;
        }
    }

    return current;
}

#define gf_io_lock(_mutex)                                                     \
    gf_succeed("io", "pthread_mutex_lock",                                     \
               gf_res_err(pthread_mutex_lock(_mutex)))

#define gf_io_unlock(_mutex)                                                   \
    gf_succeed("io", "pthread_mutex_unlock",                                   \
               gf_res_err(pthread_mutex_unlock(_mutex)))

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
