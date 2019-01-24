/*
  Copyright (c) 2019 Red Hat, Inc <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __GLUSTERFS_ASYNC_H__
#define __GLUSTERFS_ASYNC_H__

#define _LGPL_SOURCE

#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#ifdef URCU_OLD

/* TODO: Fix the include paths. Since this is a .h included from many places
 *       it makes no sense to append a '-I$(CONTRIBDIR)/userspace-rcu/' to each
 *       Makefile.am. I've also seen some problems with CI builders (they
 *       failed to find the include files, but the same source on another setup
 *       is working fine). */
#include "wfcqueue.h"
#include "wfstack.h"

#else /* !URCU_OLD */

#include <urcu/wfcqueue.h>
#include <urcu/wfstack.h>

#endif /* URCU_OLD */

#include "glusterfs/xlator.h"
#include "glusterfs/common-utils.h"
#include "glusterfs/list.h"
#include "glusterfs/libglusterfs-messages.h"

/* This is the name prefix that all worker threads will have. A number will
 * be added to differentiate them. */
#define GF_ASYNC_THREAD_NAME "tpw"

/* This value determines the maximum number of threads that are allowed. */
#define GF_ASYNC_MAX_THREADS 128

/* This value determines how many additional threads will be started but will
 * remain inactive until they are explicitly activated by the leader. This is
 * useful to react faster to bursts of load, but at the same time we minimize
 * contention if they are not really needed to handle current load.
 *
 * TODO: Instead of a fixed number, it would probably be better to use a
 *       prcentage of the available cores. */
#define GF_ASYNC_SPARE_THREADS 2

/* This value determines the signal used to wake the leader when new work has
 * been added to the queue. To do so we reuse SIGALRM, since the most logical
 * candidates (SIGUSR1/SIGUSR2) are already used. This signal must not be used
 * by anything else in the process. */
#define GF_ASYNC_SIGQUEUE SIGALRM

/* This value determines the signal that will be used to transfer leader role
 * to other workers. */
#define GF_ASYNC_SIGCTRL SIGVTALRM

#define gf_async_warning(_err, _msg, _args...)                                 \
    gf_msg("async", GF_LOG_WARNING, -(_err), LG_MSG_ASYNC_WARNING, _msg,       \
           ##_args)

#define gf_async_error(_err, _msg, _args...)                                   \
    gf_msg("async", GF_LOG_ERROR, -(_err), LG_MSG_ASYNC_FAILURE, _msg, ##_args)

#define gf_async_fatal(_err, _msg, _args...)                                   \
    do {                                                                       \
        GF_ABORT("Critical error in async module. Unable to continue. (" _msg  \
                 "). Error %d.",                                               \
                 ##_args, -(_err));                                            \
    } while (0)

struct _gf_async;
typedef struct _gf_async gf_async_t;

struct _gf_async_worker;
typedef struct _gf_async_worker gf_async_worker_t;

struct _gf_async_queue;
typedef struct _gf_async_queue gf_async_queue_t;

struct _gf_async_control;
typedef struct _gf_async_control gf_async_control_t;

typedef void (*gf_async_callback_f)(xlator_t *xl, gf_async_t *async);

struct _gf_async {
    /* TODO: remove dependency on xl/THIS. */
    xlator_t *xl;
    gf_async_callback_f cbk;
    struct cds_wfcq_node queue;
};

struct _gf_async_worker {
    /* Used to send asynchronous jobs related to the worker. */
    gf_async_t async;

    /* Member of the available workers stack. */
    struct cds_wfs_node stack;

    /* Thread object of the current worker. */
    pthread_t thread;

    /* Unique identifier of this worker. */
    int32_t id;

    /* Indicates if this worker is enabled. */
    bool running;
};

struct _gf_async_queue {
    /* Structures needed to manage a wait-free queue. For better performance
     * they are placed in two different cache lines, as recommended by URCU
     * documentation, even though in our case some threads will be producers
     * and consumers at the same time. */
    struct cds_wfcq_head head __attribute__((aligned(64)));
    struct cds_wfcq_tail tail __attribute__((aligned(64)));
};

#define GF_ASYNC_COUNTS(_run, _stop) (((uint32_t)(_run) << 16) + (_stop))
#define GF_ASYNC_COUNT_RUNNING(_count) ((_count) >> 16)
#define GF_ASYNC_COUNT_STOPPING(_count) ((_count)&65535)

struct _gf_async_control {
    gf_async_queue_t queue;

    /* Stack of unused workers. */
    struct __cds_wfs_stack available;

    /* Array of preallocated worker structures. */
    gf_async_worker_t *table;

    /* Used to synchronize main thread with workers on termination. */
    pthread_barrier_t sync;

    /* The id of the last thread that will be used for synchronization. */
    pthread_t sync_thread;

    /* Signal mask to wait for control signals from leader. */
    sigset_t sigmask_ctrl;

    /* Signal mask to wait for queued items. */
    sigset_t sigmask_queue;

    /* Saved signal handlers. */
    struct sigaction handler_ctrl;
    struct sigaction handler_queue;

    /* PID of the current process. */
    pid_t pid;

    /* Maximum number of allowed threads. */
    uint32_t max_threads;

    /* Current number of running and stopping workers. This value is split
     * into 2 16-bits fields to track both counters atomically at the same
     * time. */
    uint32_t counts;

    /* It's used to control whether the asynchronous infrastructure is used
     * or not. */
    bool enabled;
};

extern gf_async_control_t gf_async_ctrl;

int32_t
gf_async_init(glusterfs_ctx_t *ctx);

void
gf_async_fini(void);

void
gf_async_adjust_threads(int32_t threads);

static inline void
gf_async(gf_async_t *async, xlator_t *xl, gf_async_callback_f cbk)
{
    if (!gf_async_ctrl.enabled) {
        cbk(xl, async);
        return;
    }

    async->xl = xl;
    async->cbk = cbk;
    cds_wfcq_node_init(&async->queue);
    if (caa_unlikely(!cds_wfcq_enqueue(&gf_async_ctrl.queue.head,
                                       &gf_async_ctrl.queue.tail,
                                       &async->queue))) {
        /* The queue was empty, so the leader could be sleeping. We need to
         * wake it so that the new item can be processed. If the queue was not
         * empty, we don't need to do anything special since the leader will
         * take care of it. */
        if (caa_unlikely(kill(gf_async_ctrl.pid, GF_ASYNC_SIGQUEUE) < 0)) {
            gf_async_fatal(errno, "Unable to wake leader worker.");
        };
    }
}

#endif /* !__GLUSTERFS_ASYNC_H__ */
