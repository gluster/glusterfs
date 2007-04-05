/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef __IOT_H
#define __IOT_H


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define roof(a,b) ((((a)+(b)-1)/(b))*(b))
#define floor(a,b) (((a)/(b))*(b))

typedef enum {
  IOT_OP_READ = 1,
  IOT_OP_WRITE,
  IOT_OP_FLUSH,
  IOT_OP_FSYNC,
  IOT_OP_RELEASE
} iot_op_t;

struct iot_conf;
struct iot_worker;
struct iot_queue;
struct iot_local;
struct iot_file;

struct iot_local {
  iot_op_t op;
  size_t size;
  struct iovec *vector;
  int32_t count;
  char *buf;
  off_t offset;
  dict_t *fd;
  int32_t op_ret;
  int32_t op_errno;
  int32_t datasync;
  struct iot_file *file;
};

struct iot_queue {
  struct iot_queue *next, *prev;
  call_frame_t *frame;
};

struct iot_worker {
  struct iot_worker *next, *prev;
  struct iot_queue queue;
  char wake_q, wake_dq;
  int64_t q,dq;
  pthread_mutex_t queue_lock;
  pthread_mutex_t q_lock;
  pthread_mutex_t dq_lock;
  int32_t fd_count;
  int32_t queue_size;
  int32_t queue_limit;
  pthread_t thread;
};

struct iot_file {
  struct iot_file *next, *prev; /* all open files via this xlator */
  struct iot_worker *worker;
  dict_t *fd;
  int32_t pending_ops;
};

struct iot_conf {
  int32_t thread_count;
  int32_t queue_limit;
  struct iot_worker workers;
  struct iot_worker reply;
  struct iot_file files;
  pthread_mutex_t files_lock;
};

typedef struct iot_file iot_file_t;
typedef struct iot_conf iot_conf_t;
typedef struct iot_local iot_local_t;
typedef struct iot_worker iot_worker_t;
typedef struct iot_queue iot_queue_t;

#endif /* __IOT_H */
