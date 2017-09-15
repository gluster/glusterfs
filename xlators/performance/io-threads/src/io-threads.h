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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "compat-errno.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "list.h"
#include <stdlib.h>
#include "locking.h"
#include "iot-mem-types.h"
#include <semaphore.h>
#include "statedump.h"

#define GF_IO_THREADS "io-threads"
const char *_IOT_NAMESPACE_CONF = TOSTRING (GLUSTERD_WORKDIR) "/namespaces.conf";

struct iot_conf;

#define MAX_IDLE_SKEW                   4       /* In secs */
#define skew_sec_idle_time(sec)         ((sec) + (random () % MAX_IDLE_SKEW))
#define IOT_DEFAULT_IDLE                120     /* In secs. */

#define IOT_MIN_THREADS         1
#define IOT_DEFAULT_THREADS     16
#define IOT_MAX_THREADS         256
#define IOT_MIN_FOP_PER_THREAD  0
#define IOT_MAX_FOP_PER_THREAD  2000

/* A queue (well, typically a set of IOT_PRI_MAX queues) that corresponds to
 * a namespace and a priority level. */
typedef struct _iot_ns_queue {
        uint32_t         hash;   /* Hash of namespace queue corresponds to. */
        uint32_t         weight; /* Weight this queue should have in the clock. */
        double           percentage; /* Percentage of total weight given to this queue. */
        uint32_t         slots; /* Temp variable for number of slots to allocate to this queue. */
        uint32_t         size; /* Number of reqs in the queue currently. */
        struct list_head reqs;   /* Queue of fop call-stubs (requests) */
} iot_ns_queue_t;

/* A circular buffer which points to multiple queues which approximate a
 * weighted round robin serving requests. */
typedef struct _iot_ns_clock {
        unsigned int     idx;   /* Current idx of position on clock. */
        size_t           size;  /* Size of clock. */
        iot_ns_queue_t **slots; /* Circular buffer of clock slots. */
} iot_ns_clock_t;

#define IOT_THREAD_STACK_SIZE   ((size_t)(1024*1024*8))

struct iot_conf {
        pthread_mutex_t      mutex;
        pthread_cond_t       cond;

        gf_boolean_t         iambrickd;

        int32_t              max_count;   /* configured maximum */
        int32_t              fops_per_thread_ratio;
        int32_t              curr_count;  /* actual number of threads running */
        int32_t              sleep_count;

        int32_t              idle_time;   /* in seconds */

        gf_boolean_t         ns_weighted_queueing;
        uint32_t             ns_default_weight;
        double               ns_weight_tolerance;
        uint32_t             ns_conf_reinit_secs;
        time_t               ns_conf_mtime;
        dict_t              *ns_queues; /* Queue for requsts for namespaces that were parsed. */
        dict_t              *hash_to_ns; /* Hash key (string) -> Namespace string */
        iot_ns_clock_t       ns_clocks[IOT_PRI_MAX]; /* Weighted Round Robin clocks (per priority). */
        iot_ns_queue_t       ns_unknown_queue[IOT_PRI_MAX];  /* Queue for untagged requests (per priority). */

        gf_boolean_t         reinit_ns_conf_thread_running;
        pthread_t            reinit_ns_conf_thread;

        int32_t              ac_iot_limit[IOT_PRI_MAX];
        int32_t              ac_iot_count[IOT_PRI_MAX];
        int                  queue_sizes[IOT_PRI_MAX];
        int                  queue_size;
        pthread_attr_t       w_attr;
        gf_boolean_t         least_priority; /* Enable/Disable least-priority */

        xlator_t           *this;
        size_t              stack_size;

        int32_t             watchdog_secs;
        gf_boolean_t        watchdog_running;
        pthread_t           watchdog_thread;
        gf_boolean_t        queue_marked[IOT_PRI_MAX];

	gf_boolean_t        cleanup_disconnected_reqs;
};

typedef struct iot_conf iot_conf_t;

iot_pri_t iot_fop_to_pri (glusterfs_fop_t fop);

#endif /* __IOT_H */
