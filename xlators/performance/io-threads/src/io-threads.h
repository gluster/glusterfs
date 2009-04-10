/*
   Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
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

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

struct iot_conf;
struct iot_worker;
struct iot_request;

struct iot_request {
  struct list_head list;        /* Attaches this request to the list of
                                   requests.
                                   */
  call_stub_t *stub;
};

#define IOT_STATE_ACTIVE        1
#define IOT_STATE_DEAD          2
#define iot_worker_active(wrk)  ((wrk)->state == IOT_STATE_ACTIVE)

#define MAX_IDLE_SKEW                   1000    /* usecs */
#define skew_usec_idle_time(usec)       ((usec) + (random () % MAX_IDLE_SKEW))
#define IOT_DEFAULT_IDLE                180     /* In secs. */

#define IOT_MIN_THREADS         32
#define IOT_MAX_THREADS         512

#define IOT_SCALING_OFF                 _gf_false
#define IOT_SCALING_ON                  _gf_true
#define iot_ordered_scaling_on(conf)    ((conf)->o_scaling == IOT_SCALING_ON)
#define iot_unordered_scaling_on(conf)  ((conf)->u_scaling == IOT_SCALING_ON)

#define IOT_THREAD_STACK_SIZE   ((size_t)(1024*1024))

struct iot_worker {
  struct list_head rqlist;      /* List of requests assigned to me. */
  struct iot_conf *conf;
  int64_t q,dq;
  pthread_cond_t dq_cond;
  pthread_mutex_t qlock;
  int32_t queue_size;
  pthread_t thread;
  int state;            /* What state is the thread in. */
  int thread_idx;       /* Thread's index into the worker array. Since this
                         will be thread local data, for ensuring that number
                         of threads dont fall below a minimum, we just dont
                         allow threads with specific indices to exit.
                         Helps us in eliminating one place where otherwise
                         a lock would have been required to update centralized
                         state inside conf.
                         */
};

struct iot_conf {
  int32_t thread_count;
  struct iot_worker ** workers;

  xlator_t *this;
  /* Config state for ordered threads. */
  pthread_mutex_t otlock;       /* Used to sync any state that needs to be
                                   changed by the ordered threads.
                                   */

  int max_o_threads;            /* Max. number of ordered threads */
  int min_o_threads;            /* Min. number of ordered threads. Ordered
                                   thread count never falls below this
                                   threshold.
                                   */

  int o_idle_time;              /* in Secs. The idle time after which an
                                   ordered thread exits.
                                   */
  gf_boolean_t o_scaling;       /* Set to IOT_SCALING_OFF if user does not want
                                   thread scaling on ordered threads.
                                   If scaling is off, io-threads maintains
                                   at least min_o_threads number of threads
                                   and never lets any thread exit.
                                   */
  struct iot_worker **oworkers; /* Ordered thread pool. */


  /* Config state for unordered threads */
  pthread_mutex_t utlock;       /* Used for scaling un-ordered threads. */
  struct iot_worker **uworkers; /* Un-ordered thread pool. */
  int max_u_threads;            /* Number of unordered threads will not be
                                   higher than this.
                                   */
  int min_u_threads;            /* Number of unordered threads should not
                                   fall below this value. */
  int u_idle_time;              /* If an unordered thread does not get a
                                   request for this amount of secs, it should
                                   try to die.
                                   */
  gf_boolean_t u_scaling;       /* Set to IOT_SCALING_OFF if user does not want
                                   thread scaling on unordered threads.
                                   If scaling is off, io-threads maintains
                                   at least min_u_threads number of threads
                                   and never lets any thread exit.
                                   */

  pthread_attr_t w_attr;        /* Used to reduce the stack size of the
                                   pthread worker down from the default of
                                   8MiB.
                                   */
};

typedef struct iot_conf iot_conf_t;
typedef struct iot_worker iot_worker_t;
typedef struct iot_request iot_request_t;

#endif /* __IOT_H */
