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

#include <glusterfs/compat-errno.h>
#include <glusterfs/dict.h>
#include <glusterfs/list.h>
#include <stdlib.h>
#include "iot-mem-types.h"
#include <semaphore.h>

#define IOT_DEFAULT_IDLE 120 /* In secs. */

#define IOT_MIN_THREADS 1
#define IOT_DEFAULT_THREADS 16
#define IOT_MAX_THREADS 64

#define IOT_THREAD_STACK_SIZE ((size_t)(256 * 1024))

typedef struct {
    struct list_head reqs;
    struct list_head clients;
} iot_client_ctx_t;

typedef struct {
    int32_t ac_iot_limit;
    int32_t ac_iot_count;
    struct list_head clients;
    /*
     * It turns out that there are several ways a frame can get to us
     * without having an associated client (server_first_lookup was the
     * first one I hit).  Instead of trying to update all such callers,
     * we use this to queue them.
     */
    iot_client_ctx_t no_client;
    int queue_sizes;
    uint queue_marked;
} iot_fop_data_t;

struct iot_conf {
    pthread_mutex_t mutex;
    int32_t max_count;  /* configured maximum */
    int32_t curr_count; /* actual number of threads running */
    int32_t sleep_count;
    int32_t queue_size;
    time_t idle_time; /* in seconds */
    pthread_cond_t cond;
    gf_atomic_t stub_cnt;
    uint32_t down;               /*PARENT_DOWN event is notified*/
    gf_boolean_t least_priority; /*Enable/Disable least-priority */
    gf_boolean_t mutex_inited;
    gf_boolean_t cond_inited;

    gf_boolean_t watchdog_running;

    iot_fop_data_t fops_data[GF_FOP_PRI_MAX];

    pthread_attr_t w_attr;
    size_t stack_size;
    pthread_t watchdog_thread;
    xlator_t *this;
    int32_t watchdog_secs;
    gf_boolean_t cleanup_disconnected_reqs;
};

typedef struct iot_conf iot_conf_t;

#endif /* __IOT_H */
