/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This progaiom is free software; you can redistribute it and/or
  modify it under the terms of the GNU Geneaiol Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This progaiom is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied waraionty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Geneaiol Public License for more details.
    
  You should have received a copy of the GNU Geneaiol Public
  License along with this progaiom; if not, write to the Free
  Software Foundation, Inc., 51 Faionklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "aio.h"

static void
aio_queue (aio_worker_t *worker,
	   call_frame_t *frame);

static call_frame_t *
aio_dequeue (aio_worker_t *worker);

static void
aio_schedule_fd (aio_conf_t *conf,
		 aio_file_t *file)
{
  aio_worker_t *worker, *trav;
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
aio_open_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      dict_t *file_ctx,
	      struct stat *buf)
{
  aio_conf_t *conf = this->private;

  if (op_ret >= 0) {
    aio_file_t *file = calloc (1, sizeof (*file));

    aio_schedule_fd (conf, file);
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
aio_open (call_frame_t *frame,
	  xlator_t *this,
	  const char *pathname,
	  int32_t flags,
	  mode_t mode)
{
  STACK_WIND (frame,
	      aio_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      pathname,
	      flags,
	      mode);
  return 0;
}

static int32_t
aio_create (call_frame_t *frame,
	    xlator_t *this,
	    const char *pathname,
	    mode_t mode)
{
  STACK_WIND (frame,
	      aio_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      pathname,
	      mode);
  return 0;
}


static int32_t
aio_release_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  aio_conf_t *conf = this->private;
  aio_worker_t *reply = &conf->reply;
  aio_local_t *local = frame->local;
  aio_file_t *file = local->file;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  pthread_mutex_lock (&conf->files_lock);
  file->prev->next = file->prev;
  file->next->prev = file->next;
  pthread_mutex_unlock (&conf->files_lock);

  free (file);

  aio_queue (reply, frame);

  return 0;
}

static int32_t
aio_release (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *file_ctx)
{
  aio_local_t *local = NULL;
  aio_file_t *file = NULL;
  aio_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->fd = file_ctx;
  local->op = AIO_OP_RELEASE;
  local->file = file;
  frame->local = local;

  aio_queue (worker, frame);

  return 0;
}


static int32_t
aio_read_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      char *buf)
{
  aio_conf_t *conf = this->private;
  aio_worker_t *reply = &conf->reply;
  aio_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;
  local->buf = buf;

  dict_ref (frame->root->rsp_refs);

  aio_queue (reply, frame);

  return 0;
}


static int32_t
aio_read (call_frame_t *frame,
	  xlator_t *this,
	  dict_t *file_ctx,
	  size_t size,
	  off_t offset)
{
  aio_local_t *local = NULL;
  aio_file_t *file = NULL;
  aio_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->size = size;
  local->offset = offset;
  local->fd = file_ctx;
  local->op = AIO_OP_READ;
  frame->local = local;

  aio_queue (worker, frame);

  return 0;
}

static int32_t
aio_flush_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  aio_conf_t *conf = this->private;
  aio_worker_t *reply = &conf->reply;
  aio_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  aio_queue (reply, frame);

  return 0;
}


static int32_t
aio_flush (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *file_ctx)
{
  aio_local_t *local = NULL;
  aio_file_t *file = NULL;
  aio_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->fd = file_ctx;
  local->op = AIO_OP_FLUSH;
  frame->local = local;

  aio_queue (worker, frame);

  return 0;
}

static int32_t
aio_fsync_cbk (call_frame_t *frame,
	       call_frame_t *prev_frame,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  aio_conf_t *conf = this->private;
  aio_worker_t *reply = &conf->reply;
  aio_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  aio_queue (reply, frame);

  return 0;
}

static int32_t
aio_fsync (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *file_ctx,
	   int32_t datasync)
{
  aio_local_t *local = NULL;
  aio_file_t *file = NULL;
  aio_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->fd = file_ctx;
  local->op = AIO_OP_FSYNC;
  local->datasync = datasync;
  frame->local = local;

  aio_queue (worker, frame);


  return 0;
}

static int32_t
aio_writev_cbk (call_frame_t *frame,
		call_frame_t *prev_frame,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  aio_conf_t *conf = this->private;
  aio_worker_t *reply = &conf->reply;
  aio_local_t *local = frame->local;

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  aio_queue (reply, frame);

  return 0;
}

static int32_t
aio_writev (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *file_ctx,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  aio_local_t *local = NULL;
  aio_file_t *file = NULL;
  aio_worker_t *worker = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->vector = iov_dup (vector, count);
  local->count = count;
  local->offset = offset;
  local->fd = file_ctx;
  local->op = AIO_OP_WRITE;
  frame->local = local;

  dict_ref (frame->root->req_refs);

  aio_queue (worker, frame);

  return 0;
}

static void
aio_queue (aio_worker_t *worker,
	   call_frame_t *frame)
{
  char need_sleep = 0;
  char need_wake = 0;
  char done = 0;
  aio_queue_t *queue;

  queue = calloc (1, sizeof (*queue));
  queue->frame = frame;

  while (!done) {
    need_sleep = need_wake = 0;

    pthread_mutex_lock (&worker->queue_lock);

    if (!worker->queue_size)
      need_wake = 1;

    if (worker->queue_size < worker->queue_limit) {
      done = 1;
      queue->next = &worker->queue;
      queue->prev = worker->queue.prev;

      queue->next->prev = queue;
      queue->prev->next = queue;
      worker->queue_size++;
      worker->q++;
    }

    if (worker->queue_size == worker->queue_limit)
      need_sleep = 1;

    pthread_mutex_unlock (&worker->queue_lock);

    if (need_wake)
      pthread_mutex_unlock (&worker->sleep_lock);

    if (need_sleep)
      pthread_mutex_lock (&worker->sleep_lock);
  }
}

static call_frame_t *
aio_dequeue (aio_worker_t *worker)
{
  call_frame_t *frame = NULL;
  aio_queue_t *queue = NULL;
  char need_sleep = 0;
  char need_wake = 0;

  while (!queue) {
    need_sleep = need_wake = 0;
    pthread_mutex_lock (&worker->queue_lock);

    if (worker->queue_size == worker->queue_limit)
      need_wake = 1;

    if (!worker->queue_size) {
      need_sleep = 1;
    } else {
      queue = worker->queue.next;

      queue->next->prev = queue->prev;
      queue->prev->next = queue->next;

      worker->queue_size--;
      worker->dq++;
    }
    pthread_mutex_unlock (&worker->queue_lock);

    if (need_wake)
      pthread_mutex_unlock (&worker->sleep_lock);

    if (need_sleep)
      pthread_mutex_lock (&worker->sleep_lock);
  }

  frame = queue->frame;
  free (queue);

  return frame;
}

static void
aio_handle_frame (call_frame_t *frame)
{
  aio_local_t *local = frame->local;
  xlator_t *this = frame->this;
  dict_t *refs = frame->root->req_refs;

  switch (local->op) {
  case AIO_OP_READ:
    STACK_WIND (frame,
		aio_read_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->read,
		local->fd,
		local->size,
		local->offset);
    break;
  case AIO_OP_WRITE:
    STACK_WIND (frame,
		aio_writev_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->writev,
		local->fd,
		local->vector,
		local->count,
		local->offset);
    dict_unref (refs);
    break;
  case AIO_OP_FLUSH:
    STACK_WIND (frame,
		aio_flush_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->flush,
		local->fd);
    break;
  case AIO_OP_FSYNC:
    STACK_WIND (frame,
		aio_fsync_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->fsync,
		local->fd,
		local->datasync);
    break;
  case AIO_OP_RELEASE:
    STACK_WIND (frame,
		aio_release_cbk,
		FIRST_CHILD(this),
		FIRST_CHILD(this)->fops->release,
		local->fd);
    break;
  }
}

static void *
aio_worker (void *arg)
{
  aio_worker_t *worker = arg;

  while (1) {
    call_frame_t *frame;

    frame = aio_dequeue (worker);
    aio_handle_frame (frame);
  }
}

static void
aio_reply_frame (call_frame_t *frame)
{
  aio_local_t *local = frame->local;
  xlator_t *this = frame->this;
  dict_t *refs;

  frame->local = NULL;
  refs = frame->root->rsp_refs;

  switch (local->op) {
  case AIO_OP_READ:
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->buf);
    dict_unref (refs);
    break;
  case AIO_OP_WRITE:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  case AIO_OP_FLUSH:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  case AIO_OP_FSYNC:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  case AIO_OP_RELEASE:
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
    break;
  }

  if (local->vector)
    free (local->vector);
  free (local);
}

static void *
aio_reply (void *arg)
{
  aio_worker_t *reply = arg;

  while (1) {
    call_frame_t *frame;

    frame = aio_dequeue (reply);
    aio_reply_frame (frame);
  }
}

static void
workers_init (aio_conf_t *conf)
{
  int i;
  aio_worker_t *reply = &conf->reply;

  reply->next = reply;
  reply->prev = reply;
  reply->queue.next = &reply->queue;
  reply->queue.prev = &reply->queue;
  reply->queue_limit = conf->queue_limit;

  pthread_mutex_init (&reply->queue_lock, NULL);
  pthread_mutex_init (&reply->sleep_lock, NULL);
  pthread_mutex_lock (&reply->sleep_lock);
  
  pthread_create (&reply->thread, NULL, aio_reply, reply);

  conf->workers.next = &conf->workers;
  conf->workers.prev = &conf->workers;

  for (i=0; i<conf->thread_count; i++) {

    aio_worker_t *worker = calloc (1, sizeof (*worker));

    worker->next = &conf->workers;
    worker->prev = conf->workers.prev;
    worker->next->prev = worker;
    worker->prev->next = worker;

    worker->queue.next = &worker->queue;
    worker->queue.prev = &worker->queue;

    pthread_mutex_init (&worker->queue_lock, NULL);
    pthread_mutex_init (&worker->sleep_lock, NULL);
    pthread_mutex_lock (&worker->sleep_lock);

    worker->queue_limit = conf->queue_limit;

    pthread_create (&worker->thread, NULL, aio_worker, worker);
  }

}

int32_t 
init (struct xlator *this)
{
  aio_conf_t *conf;
  dict_t *options = this->options;

  if (!this->children || this->children->next) {
    gf_log ("aio",
	    GF_LOG_ERROR,
	    "FATAL: aio not configured with exactly one child");
    return -1;
  }

  conf = (void *) calloc (1, sizeof (*conf));
  conf->thread_count = 4;
  conf->queue_limit = 64;

  if (dict_get (options, "thread-count")) {
    conf->thread_count = data_to_int (dict_get (options,
						"thread-count"));
    gf_log ("aio",
	    GF_LOG_DEBUG,
	    "Using conf->thread_count = %d",
	    conf->thread_count);
  }

  if (dict_get (options, "queue-limit")) {
    conf->queue_limit = data_to_int (dict_get (options,
					       "queue-limit"));
    gf_log ("aio",
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
  aio_conf_t *conf = this->private;

  free (conf);

  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .open        = aio_open,
  .create      = aio_create,
  .read        = aio_read,
  .writev      = aio_writev,
  .flush       = aio_flush,
  .fsync       = aio_fsync,
  .release     = aio_release,
};

struct xlator_mops mops = {
};
