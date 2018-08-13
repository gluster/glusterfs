/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __IOT_H
#define __IOT_H

#include "compat-errno.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "list.h"
#include <stdlib.h>
#include "locking.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <semaphore.h>
#include "statedump.h"
#include "call-stub.h"

#define MAX_IDLE_SKEW 4 /* In secs */
#define skew_sec_idle_time(sec) ((sec) + (random() % MAX_IDLE_SKEW))
#define IOT_DEFAULT_IDLE 120 /* In secs. */

#define IOT_MIN_THREADS 1
#define IOT_DEFAULT_THREADS 16
#define IOT_MAX_THREADS 64

#define IOT_THREAD_STACK_SIZE ((size_t)(256 * 1024))

#define IOT_INITED 1
#define IOT_STARTED 2
#define IOT_STOPPED 4

typedef struct {
    struct list_head clients;
    struct list_head reqs;
} iot_client_ctx_t;

struct gf_iot {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    GF_REF_DECL;

    uint64_t state;
    int32_t max_count;  /* configured maximum */
    int32_t curr_count; /* actual number of threads running */
    int32_t sleep_count;

    int32_t idle_time; /* in seconds */

    struct list_head clients[GF_FOP_PRI_MAX];
    /*
     * It turns out that there are several ways a frame can get to us
     * without having an associated client (server_first_lookup was the
     * first one I hit).  Instead of trying to update all such callers,
     * we use this to queue them.
     */
    iot_client_ctx_t no_client[GF_FOP_PRI_MAX];

    int32_t ac_iot_limit[GF_FOP_PRI_MAX];
    int32_t ac_iot_count[GF_FOP_PRI_MAX];
    int queue_sizes[GF_FOP_PRI_MAX];
    int queue_size;
    pthread_attr_t w_attr;
    gf_boolean_t least_priority; /*Enable/Disable least-priority */

    size_t stack_size;
    gf_boolean_t mutex_inited;
    gf_boolean_t cond_inited;

    int32_t watchdog_secs;
    gf_boolean_t watchdog_running;
    pthread_t watchdog_thread;
    gf_boolean_t queue_marked[GF_FOP_PRI_MAX];
    gf_boolean_t cleanup_disconnected_reqs;
};

typedef struct gf_iot gf_iot_t;

int
gf_dump_iot_info(glusterfs_ctx_t *ctx);

int
gf_iot_get(xlator_t *this);

void
gf_iot_put(xlator_t *this);

int
gf_iot_reconf(xlator_t *this);

gf_iot_t *
gf_iot_defaults_init(glusterfs_ctx_t *ctx);

void
gf_iot_free(gf_iot_t *iot);

int
gf_iot_client_destroy(xlator_t *this, client_t *client);

int
gf_iot_disconnect_cbk(xlator_t *this, client_t *client);

int
gf_iot_schedule(call_frame_t *frame, xlator_t *this, call_stub_t *stub);

#define IOT_FOP(name, frame, this, args...)                                    \
    do {                                                                       \
        call_stub_t *__stub = NULL;                                            \
        int __ret = -1;                                                        \
                                                                               \
        __stub = fop_##name##_stub(frame, default_##name##_resume, args);      \
        if (!__stub) {                                                         \
            __ret = -ENOMEM;                                                   \
            goto out;                                                          \
        }                                                                      \
                                                                               \
        __ret = gf_iot_schedule(frame, this, __stub);                          \
                                                                               \
    out:                                                                       \
        if (__ret < 0) {                                                       \
            default_##name##_failure_cbk(frame, -__ret);                       \
            if (__stub != NULL) {                                              \
                call_stub_destroy(__stub);                                     \
            }                                                                  \
        }                                                                      \
    } while (0)

#endif /* __IOT_H */
