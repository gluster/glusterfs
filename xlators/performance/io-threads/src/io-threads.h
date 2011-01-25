/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
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
#include "locking.h"
#include "iot-mem-types.h"
#include <semaphore.h>


struct iot_conf;

#define MAX_IDLE_SKEW                   4       /* In secs */
#define skew_sec_idle_time(sec)         ((sec) + (random () % MAX_IDLE_SKEW))
#define IOT_DEFAULT_IDLE                120     /* In secs. */

#define IOT_MIN_THREADS         1
#define IOT_DEFAULT_THREADS     16
#define IOT_MAX_THREADS         64


#define IOT_THREAD_STACK_SIZE   ((size_t)(1024*1024))


typedef enum {
        IOT_PRI_HI = 0, /* low latency */
        IOT_PRI_NORMAL, /* normal */
        IOT_PRI_LO,     /* bulk */
        IOT_PRI_MAX,
} iot_pri_t;


struct iot_conf {
        pthread_mutex_t      mutex;
        pthread_cond_t       cond;

        int32_t              max_count;   /* configured maximum */
        int32_t              curr_count;  /* actual number of threads running */
        int32_t              sleep_count;

        int32_t              idle_time;   /* in seconds */

        struct list_head     reqs[IOT_PRI_MAX];

        int                  queue_size;
        pthread_attr_t       w_attr;

        xlator_t            *this;
};

typedef struct iot_conf iot_conf_t;

#endif /* __IOT_H */
