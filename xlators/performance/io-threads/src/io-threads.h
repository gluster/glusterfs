/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

struct iot_conf;
struct iot_worker;
struct iot_queue;
struct iot_local;
struct iot_file;

struct iot_local {
  struct iot_file *file;
  size_t frame_size;
};

struct iot_queue {
  struct iot_queue *next, *prev;
  call_stub_t *stub;
};

struct iot_worker {
  struct iot_worker *next, *prev;
  struct iot_queue queue;
  struct iot_conf *conf;
  int64_t q,dq;
  pthread_cond_t dq_cond;
  /*
    pthread_cond_t q_cond;
    pthread_mutex_t lock;
  */
  int32_t fd_count;
  int32_t queue_size;
  /*
    int32_t queue_limit;
  */
  pthread_t thread;
};

struct iot_file {
  struct iot_file *next, *prev; /* all open files via this xlator */
  struct iot_worker *worker;
  fd_t *fd;
  int32_t pending_ops;
};

struct iot_conf {
  int32_t thread_count;
  int32_t misc_thread_index;  /* Used to schedule the miscellaneous calls like checksum */
  struct iot_worker workers;
  struct iot_file files;
  pthread_mutex_t files_lock;

  uint64_t cache_size;
  off_t current_size;
  pthread_cond_t q_cond;
  pthread_mutex_t lock;
};

typedef struct iot_file iot_file_t;
typedef struct iot_conf iot_conf_t;
typedef struct iot_local iot_local_t;
typedef struct iot_worker iot_worker_t;
typedef struct iot_queue iot_queue_t;

#endif /* __IOT_H */
