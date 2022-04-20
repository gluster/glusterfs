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

#define _LGPL_SOURCE

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

#include <glusterfs/compat-errno.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/list.h>
#include <stdlib.h>
#include <glusterfs/locking.h>
#include "iot-mem-types.h"
#include <semaphore.h>
#include <glusterfs/statedump.h>
#include "glusterfs/list.h"

struct iot_conf;

#define MAX_IDLE_SKEW 4 /* In secs */
#define skew_sec_idle_time(sec) ((sec) + (random() % MAX_IDLE_SKEW))
#define IOT_DEFAULT_IDLE 120 /* In secs. */

#define IOT_MIN_THREADS 1
#define IOT_DEFAULT_THREADS 16
#define IOT_MAX_THREADS 256

#define IOT_THREAD_STACK_SIZE ((size_t)(256 * 1024))

struct iot_work {
    xlator_t *xl;
    call_stub_t *stub;
    gf_fop_pri_t pri;
    struct cds_wfcq_node node;
};
typedef struct iot_work iot_worker_t;

struct iot_client {
    sem_t sem;
    client_t *client;
    struct cds_wfcq_node node;
};
typedef struct iot_client iot_client_t;

struct iot_queues {
    /* Structures needed to manage a wait-free queue. For better performance
     * they are placed in two different cache lines, as recommended by URCU
     * documentation, even though in our case some threads will be producers
     * and consumers at the same time. */

    struct cds_wfcq_head head __attribute__((aligned(64)));
    struct cds_wfcq_tail tail __attribute__((aligned(64)));
};
typedef struct iot_queues iot_queues_t;

struct iot_conf {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    sem_t sem;

    uint32_t max_count;  /* configured maximum */
    uint32_t curr_count; /* actual number of threads running */

    int32_t idle_time; /* in seconds */

    int32_t ac_iot_limit[GF_FOP_PRI_MAX];
    int32_t ac_iot_count[GF_FOP_PRI_MAX];
    int32_t queue_sizes[GF_FOP_PRI_MAX];
    iot_queues_t client_queues[GF_FOP_PRI_MAX];
    iot_queues_t disconnect_queue;
    struct __cds_wfs_stack available;

    uint32_t queue_size;
    uint32_t stub_cnt;
    pthread_attr_t w_attr;
    gf_boolean_t least_priority; /*Enable/Disable least-priority */

    xlator_t *this;

    size_t stack_size;
    gf_boolean_t down; /*PARENT_DOWN event is notified*/
    gf_boolean_t mutex_inited;
    gf_boolean_t cond_inited;

    int32_t watchdog_secs;
    gf_boolean_t watchdog_running;
    pthread_t watchdog_thread;
    gf_boolean_t queue_marked[GF_FOP_PRI_MAX];
    gf_boolean_t cleanup_disconnected_reqs;
};

typedef struct iot_conf iot_conf_t;

struct iot_threads {
    iot_conf_t *conf;
    struct cds_wfs_node node;
    iot_worker_t *job;
    sem_t sem;
    gf_boolean_t running;
};

typedef struct iot_threads iot_threads_t;

#endif /* __IOT_H */
