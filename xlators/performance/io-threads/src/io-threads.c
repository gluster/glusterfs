/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This progiotm is free software; you can redistribute it and/or
  modify it under the terms of the GNU Geneiotl Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This progiotm is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied wariotnty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Geneiotl Public License for more details.
    
  You should have received a copy of the GNU Geneiotl Public
  License along with this progiotm; if not, write to the Free
  Software Foundation, Inc., 51 Fiotnklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"

static void
iot_queue (iot_worker_t *worker,
	   call_frame_t *frame);

static call_frame_t *
iot_dequeue (iot_worker_t *worker);

static void
iot_schedule_fd (iot_conf_t *conf,
		 iot_file_t *file)
{
  iot_worker_t *worker, *trav;
  int32_t min;

  worker = trav = conf->workers.next;
  min = worker->fd_count;

  while (trav != &conf->workers) {
    if (trav->fd_count < min) {
      min = trav->fd_count;
      worker = trav;
    }
    trav = trav->next;
  }

  worker->fd_count++;
  file->worker = worker;
}

static int32_t
iot_open_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      dict_t *file_ctx,
	      struct stat *buf)
{
  iot_conf_t *conf = this->private;

  if (op_ret >= 0) {
    iot_file_t *file = calloc (1, sizeof (*file));

    iot_schedule_fd (conf, file);
    file->fd = file_ctx;

    dict_set (file_ctx,
	      this->name,
	      int_to_data ((long) file));

    pthread_mutex_lock (&conf->files_lock);
    file->next = &conf->files;
    file->prev = file->next->prev;
    file->next->prev = file;
    file->prev->next = file;
    pthread_mutex_unlock (&conf->files_lock);
  }
  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, buf);
  return 0;
}

static int32_t
iot_open (call_frame_t *frame,
	  xlator_t *this,
	  const char *pathname,
	  int32_t flags,
	  mode_t mode)
{
  STACK_WIND (frame,
	      iot_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      pathname,
	      flags,
	      mode);
  return 0;
}

static int32_t
iot_create (call_frame_t *frame,
	    xlator_t *this,
	    const char *pathname,
	    mode_t mode)
{
  STACK_WIND (frame,
	      iot_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      pathname,
	      mode);
  return 0;
}


static int32_t
iot_release_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local = frame->local;
  iot_file_t *file = local->file;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  pthread_mutex_lock (&conf->files_lock);
  file->prev->next = file->next;
  file->next->prev = file->prev;
  pthread_mutex_unlock (&conf->files_lock);

  file->worker->fd_count--;
  free (file);

  iot_queue (reply, frame);

  return 0;
}

static int32_t
iot_release (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *file_ctx)
{
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->fd = file_ctx;
  local->op = IOT_OP_RELEASE;
  local->file = file;
  frame->local = local;

  iot_queue (worker, frame);

  return 0;
}


static int32_t
iot_readv_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct iovec *vector,
	       int32_t count)
{
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local = frame->local;

  /*
  local->op_ret = op_ret;
  local->op_errno = op_errno;
  local->vector = iov_dup (vector, count);
  local->count = count;

  dict_ref (frame->root->rsp_refs);

  iot_queue (reply, frame);
  */
  if (local->vector)
    free (local->vector);  
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}


static int32_t
iot_readv (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *file_ctx,
	   size_t size,
	   off_t offset)
{
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->size = size;
  local->offset = offset;
  local->fd = file_ctx;
  local->op = IOT_OP_READ;
  frame->local = local;

  iot_queue (worker, frame);

  return 0;
}

static int32_t
iot_flush_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  iot_queue (reply, frame);

  return 0;
}


static int32_t
iot_flush (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *file_ctx)
{
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->fd = file_ctx;
  local->op = IOT_OP_FLUSH;
  frame->local = local;

  iot_queue (worker, frame);

  return 0;
}

static int32_t
iot_fsync_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  iot_queue (reply, frame);

  return 0;
}

static int32_t
iot_fsync (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *file_ctx,
	   int32_t datasync)
{
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->fd = file_ctx;
  local->op = IOT_OP_FSYNC;
  local->datasync = datasync;
  frame->local = local;

  iot_queue (worker, frame);


  return 0;
}

static int32_t
iot_writev_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  /*  iot_queue (reply, frame);
   */
  if (local->vector)
    free (local->vector);
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

static int32_t
iot_writev (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *file_ctx,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->vector = iov_dup (vector, count);
  local->count = count;
  local->offset = offset;
  local->fd = file_ctx;
  local->op = IOT_OP_WRITE;
  frame->local = local;

  dict_ref (frame->root->req_refs);

  iot_queue (worker, frame);

  return 0;
}

static void
iot_queue (iot_worker_t *worker,
	   call_frame_t *frame)
{
  char need_sleep = 0;
  char need_wake = 0;
  char done = 0;
  iot_queue_t *queue;

  queue = calloc (1, sizeof (*queue));
  queue->frame = frame;

  while (!done) {
    need_sleep = need_wake = 0;

    pthread_mutex_lock (&worker->queue_lock);

    if (worker->wake_dq) {
      worker->wake_dq--;
      need_wake = 1;
    }

    if (worker->queue_size < worker->queue_limit) {
      done = 1;
      queue->next = &worker->queue;
      queue->prev = worker->queue.prev;

      queue->next->prev = queue;
      queue->prev->next = queue;
      worker->queue_size++;
      worker->q++;
    }

    if (worker->queue_size == worker->queue_limit) {
      worker->wake_q++;
      need_sleep = 1;
    }

    pthread_mutex_unlock (&worker->queue_lock);

    if (need_wake)
      pthread_mutex_unlock (&worker->dq_lock);

    if (need_sleep)
      pthread_mutex_lock (&worker->q_lock);
  }
}

static call_frame_t *
iot_dequeue (iot_worker_t *worker)
{
  call_frame_t *frame = NULL;
  iot_queue_t *queue = NULL;
  char need_sleep = 0;
  char need_wake = 0;

  while (!queue) {
    need_sleep = need_wake = 0;
    pthread_mutex_lock (&worker->queue_lock);

    if (worker->wake_q) {
      worker->wake_q--;
      need_wake = 1;
    }

    if (!worker->queue_size) {
      worker->wake_dq++;
      need_sleep = 1;
    }

    if (worker->queue_size) {
      queue = worker->queue.next;

      queue->next->prev = queue->prev;
      queue->prev->next = queue->next;

      worker->queue_size--;
      worker->dq++;
    }

    pthread_mutex_unlock (&worker->queue_lock);

    if (need_wake)
      pthread_mutex_unlock (&worker->q_lock);

    if (need_sleep)
      pthread_mutex_lock (&worker->dq_lock);
  }

  frame = queue->frame;
  free (queue);

  return frame;
}

static void
iot_handle_frame (call_frame_t *frame)
{
  iot_local_t *local = frame->local;
  xlator_t *this = frame->this;
  dict_t *refs = frame->root->req_refs;
  struct iovec *vector;

  switch (local->op) {
  case IOT_OP_READ:
    STACK_WIND (frame,
		iot_readv_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->readv,
		local->fd,
		local->size,
		local->offset);
    break;
  case IOT_OP_WRITE:
    vector = iov_dup (local->vector, local->count);
    STACK_WIND (frame,
		iot_writev_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->writev,
		local->fd,
		vector,
		local->count,
		local->offset);
    free (vector);
    dict_unref (refs);
    break;
  case IOT_OP_FLUSH:
    STACK_WIND (frame,
		iot_flush_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->flush,
		local->fd);
    break;
  case IOT_OP_FSYNC:
    STACK_WIND (frame,
		iot_fsync_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->fsync,
		local->fd,
		local->datasync);
    break;
  case IOT_OP_RELEASE:
    STACK_WIND (frame,
		iot_release_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->release,
		local->fd);
    break;
  }
}

static void *
iot_worker (void *arg)
{
  iot_worker_t *worker = arg;

  while (1) {
    call_frame_t *frame;

    frame = iot_dequeue (worker);
    iot_handle_frame (frame);
  }
}

static void
iot_reply_frame (call_frame_t *frame)
{
  iot_local_t *local = frame->local;
  dict_t *refs;

  frame->local = NULL;
  refs = frame->root->rsp_refs;

  switch (local->op) {
  case IOT_OP_READ:
    STACK_UNWIND (frame, local->op_ret, local->op_errno,
		  local->vector, local->count);
    dict_unref (refs);
    break;
  case IOT_OP_WRITE:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  case IOT_OP_FLUSH:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  case IOT_OP_FSYNC:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  case IOT_OP_RELEASE:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  }

  if (local->vector)
    free (local->vector);
  free (local);
}

static void *
iot_reply (void *arg)
{
  iot_worker_t *reply = arg;

  while (1) {
    call_frame_t *frame;

    frame = iot_dequeue (reply);
    iot_reply_frame (frame);
  }
}

static void
workers_init (iot_conf_t *conf)
{
  int i;
  iot_worker_t *reply = &conf->reply;

  reply->next = reply;
  reply->prev = reply;
  reply->queue.next = &reply->queue;
  reply->queue.prev = &reply->queue;
  reply->queue_limit = conf->queue_limit;

  pthread_mutex_init (&reply->queue_lock, NULL);
  pthread_mutex_init (&reply->q_lock, NULL);
  pthread_mutex_lock (&reply->q_lock);
  pthread_mutex_init (&reply->dq_lock, NULL);
  pthread_mutex_lock (&reply->dq_lock);
  
  pthread_create (&reply->thread, NULL, iot_reply, reply);

  conf->workers.next = &conf->workers;
  conf->workers.prev = &conf->workers;

  for (i=0; i<conf->thread_count; i++) {

    iot_worker_t *worker = calloc (1, sizeof (*worker));

    worker->next = &conf->workers;
    worker->prev = conf->workers.prev;
    worker->next->prev = worker;
    worker->prev->next = worker;

    worker->queue.next = &worker->queue;
    worker->queue.prev = &worker->queue;

    pthread_mutex_init (&worker->queue_lock, NULL);
    pthread_mutex_init (&worker->q_lock, NULL);
    pthread_mutex_lock (&worker->q_lock);
    pthread_mutex_init (&worker->dq_lock, NULL);
    pthread_mutex_lock (&worker->dq_lock);

    worker->queue_limit = conf->queue_limit;

    pthread_create (&worker->thread, NULL, iot_worker, worker);
  }

}

int32_t 
init (struct xlator *this)
{
  iot_conf_t *conf;
  dict_t *options = this->options;

  if (!this->children || this->children->next) {
    gf_log ("io-threads",
	    GF_LOG_ERROR,
	    "FATAL: iot not configured with exactly one child");
    return -1;
  }

  conf = (void *) calloc (1, sizeof (*conf));
  conf->thread_count = 4;
  conf->queue_limit = 64;

  if (dict_get (options, "thread-count")) {
    conf->thread_count = data_to_int (dict_get (options,
						"thread-count"));
    gf_log ("io-threads",
	    GF_LOG_DEBUG,
	    "Using conf->thread_count = %d",
	    conf->thread_count);
  }

  if (dict_get (options, "queue-limit")) {
    conf->queue_limit = data_to_int (dict_get (options,
					       "queue-limit"));
    gf_log ("io-threads",
	    GF_LOG_DEBUG,
	    "Using conf->queue_limit = %d",
	    conf->queue_limit);
  }

  conf->files.next = &conf->files;
  conf->files.prev = &conf->files;
  pthread_mutex_init (&conf->files_lock, NULL);

  workers_init (conf);

  this->private = conf;
  return 0;
}

void
fini (struct xlator *this)
{
  iot_conf_t *conf = this->private;

  free (conf);

  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .open        = iot_open,
  .create      = iot_create,
  .readv       = iot_readv,
  .writev      = iot_writev,
  .flush       = iot_flush,
  .fsync       = iot_fsync,
  .release     = iot_release,
};

struct xlator_mops mops = {
};
