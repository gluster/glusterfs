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

#ifndef __AIO_H
#define __AIO_H


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define roof(a,b) ((((a)+(b)-1)/(b))*(b))
#define floor(a,b) (((a)/(b))*(b))

typedef enum {
  AIO_OP_READ,
  AIO_OP_WRITE
} aio_op_t;

struct aio_conf;
struct aio_worker;
struct aio_queue;
struct aio_local;
struct aio_file;

struct aio_local {
  aio_op_t op;
  size_t size;
  struct iovec *vector;
  int32_t count;
  char *buf;
  off_t offset;
  dict_t *fd;
  int32_t op_ret;
  int32_t op_errno;
};

struct aio_queue {
  struct aio_queue *next, *prev;
  call_frame_t *frame;
};

struct aio_worker {
  struct aio_worker *next, *prev;
  struct aio_queue queue;
  int64_t q,dq;
  pthread_mutex_t queue_lock;
  pthread_mutex_t sleep_lock;
  int32_t fd_count;
  int32_t queue_size;
  int32_t queue_limit;
  pthread_t thread;
};

struct aio_file {
  struct aio_file *next, *prev; /* all open files via this xlator */
  struct aio_worker *worker;
  dict_t *fd;
  int32_t pending_ops;
};

struct aio_conf {
  int32_t thread_count;
  int32_t queue_limit;
  struct aio_worker workers;
  struct aio_worker reply;
  struct aio_file files;
  pthread_mutex_t files_lock;
};

typedef struct aio_file aio_file_t;
typedef struct aio_conf aio_conf_t;
typedef struct aio_local aio_local_t;
typedef struct aio_worker aio_worker_t;
typedef struct aio_queue aio_queue_t;

#endif /* __AIO_H */
