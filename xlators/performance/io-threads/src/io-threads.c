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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "call-stub.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"

static void
iot_queue (iot_worker_t *worker,
           call_stub_t *stub);

static call_stub_t *
iot_dequeue (iot_worker_t *worker);

static iot_worker_t * 
iot_schedule (iot_conf_t *conf,
              iot_file_t *file,
              ino_t ino)
{
  int32_t cnt = (ino % conf->thread_count);
  iot_worker_t *trav = conf->workers.next;

  for (; cnt; cnt--)
    trav = trav->next;
  
  if (file)
    file->worker = trav;
  trav->fd_count++;
  return trav;
}

int32_t
iot_open_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno,
              fd_t *fd)
{
  iot_conf_t *conf = this->private;

  if (op_ret >= 0) {
    iot_file_t *file = calloc (1, sizeof (*file));

    iot_schedule (conf, file, fd->inode->ino);
    file->fd = fd;

    dict_set (fd->ctx,
              this->name,
              data_from_static_ptr (file));

    pthread_mutex_lock (&conf->files_lock);
    file->next = &conf->files;
    file->prev = file->next->prev;
    file->next->prev = file;
    file->prev->next = file;
    pthread_mutex_unlock (&conf->files_lock);
  }
  STACK_UNWIND (frame, op_ret, op_errno, fd);
  return 0;
}

int32_t
iot_open (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc,
          int32_t flags,
	  fd_t *fd)
{
  STACK_WIND (frame,
              iot_open_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->open,
              loc,
              flags,
	      fd);
  return 0;
}


int32_t
iot_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd,
		inode_t *inode,
		struct stat *stbuf)
{
  iot_conf_t *conf = this->private;

  if (op_ret >= 0) {
    iot_file_t *file = calloc (1, sizeof (*file));

    iot_schedule (conf, file, fd->inode->ino);
    file->fd = fd;

    dict_set (fd->ctx,
              this->name,
              data_from_static_ptr (file));

    pthread_mutex_lock (&conf->files_lock);
    file->next = &conf->files;
    file->prev = file->next->prev;
    file->next->prev = file;
    file->prev->next = file;
    pthread_mutex_unlock (&conf->files_lock);
  }
  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);
  return 0;
}

int32_t
iot_create (call_frame_t *frame,
            xlator_t *this,
	    loc_t *loc,
            int32_t flags,
            mode_t mode,
	    fd_t *fd)
{
  STACK_WIND (frame,
              iot_create_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
	      loc,
              flags,
              mode,
	      fd);
  return 0;
}

int32_t
iot_close_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_file_t *file = local->file;

  pthread_mutex_lock (&conf->files_lock);
  {
    file->prev->next = file->next;
    file->next->prev = file->prev;
  }
  pthread_mutex_unlock (&conf->files_lock);

  file->worker->fd_count--;
  file->worker = NULL;
  freee (file);
  
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

static int32_t
iot_close_wrapper (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd)
{
  STACK_WIND (frame,
              iot_close_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->close,
              fd);
  return 0;
}

int32_t
iot_close (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));

  local->file = file;
  frame->local = local;
  
  stub = fop_close_stub (frame,
                         iot_close_wrapper,
                         fd);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get close call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}


int32_t
iot_readv_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               struct iovec *vector,
               int32_t count,
	       struct stat *stbuf)
{
  iot_local_t *local = frame->local;

  local->frame_size = 0; //iov_length (vector, count);

  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		vector,
		count,
		stbuf);

  return 0;
}

static int32_t
iot_readv_wrapper (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   size_t size,
                   off_t offset)
{
  STACK_WIND (frame,
              iot_readv_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->readv,
              fd,
              size,
              offset);
  return 0;
}

int32_t
iot_readv (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           size_t size,
           off_t offset)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_readv_stub (frame, 
                         iot_readv_wrapper,
                         fd,
                         size,
                         offset);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get readv call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, 0);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

int32_t
iot_flush_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

static int32_t
iot_flush_wrapper (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd)
{
  STACK_WIND (frame,
              iot_flush_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->flush,
              fd);
  return 0;
}

int32_t
iot_flush (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));

  frame->local = local;
  
  stub = fop_flush_stub (frame,
                         iot_flush_wrapper,
                         fd);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get flush_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
  iot_queue (worker, stub);

  return 0;
}

int32_t
iot_fsync_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

static int32_t
iot_fsync_wrapper (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   int32_t datasync)
{
  STACK_WIND (frame,
              iot_fsync_cbk,
              FIRST_CHILD (this),
              FIRST_CHILD (this)->fops->fsync,
              fd,
              datasync);
  return 0;
}

int32_t
iot_fsync (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           int32_t datasync)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));

  frame->local = local;
  
  stub = fop_fsync_stub (frame,
                         iot_fsync_wrapper,
                         fd,
                         datasync);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fsync_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
  iot_queue (worker, stub);

  return 0;
}

int32_t
iot_writev_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
		struct stat *stbuf)
{
  iot_local_t *local = frame->local;

  local->frame_size = 0; /* hehe, caught me! */

  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		stbuf);
  return 0;
}

static int32_t
iot_writev_wrapper (call_frame_t *frame,
                    xlator_t *this,
                    fd_t *fd,
                    struct iovec *vector,
                    int32_t count,
                    off_t offset)
{
  STACK_WIND (frame,
              iot_writev_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->writev,
              fd,
              vector,
              count,
              offset);
  return 0;
}

int32_t
iot_writev (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            struct iovec *vector,
            int32_t count,
            off_t offset)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));

  if (frame->root->req_refs)
    local->frame_size = dict_serialized_length (frame->root->req_refs);
  else
    local->frame_size = iov_length (vector, count);
  frame->local = local;
  
  stub = fop_writev_stub (frame, iot_writev_wrapper,
                          fd, vector, count, offset);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get writev call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

int32_t
iot_lk_cbk (call_frame_t *frame,
            void *cookie,
            xlator_t *this,
            int32_t op_ret,
            int32_t op_errno,
            struct flock *flock)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		flock);
  return 0;
}

static int32_t
iot_lk_wrapper (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd,
                int32_t cmd,
                struct flock *flock)
{
  STACK_WIND (frame,
              iot_lk_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->lk,
              fd,
              cmd,
              flock);
  return 0;
}

int32_t
iot_lk (call_frame_t *frame,
        xlator_t *this,
        fd_t *fd,
        int32_t cmd,
        struct flock *flock)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  stub = fop_lk_stub (frame,
                      iot_lk_wrapper,
                      fd,
                      cmd,
                      flock);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_lk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
    
  iot_queue (worker, stub);

  return 0;
}


int32_t 
iot_stat_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno,
              struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}


static int32_t 
iot_stat_wrapper (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc)
{
  STACK_WIND (frame,
              iot_stat_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->stat,
              loc);
  return 0;
}

int32_t 
iot_stat (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  char fd_list_empty = 0;
 
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  LOCK (&(loc->inode->lock));
  {
    if (list_empty (&(loc->inode->fds)))
      fd_list_empty = 1;
  }
  UNLOCK (&(loc->inode->lock));

  if (fd_list_empty) {
    STACK_WIND(frame,
               iot_stat_cbk,
               FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->stat,
               loc);
    return 0;
  } 

  worker = iot_schedule (conf, NULL, loc->inode->ino);

  stub = fop_stat_stub (frame,
                        iot_stat_wrapper,
                        loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_stat call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (worker, stub);

  return 0;
}


int32_t 
iot_fstat_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

static int32_t 
iot_fstat_wrapper (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd)
{
  STACK_WIND (frame,
              iot_fstat_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->fstat,
              fd);
  return 0;
}

int32_t 
iot_fstat (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  stub = fop_fstat_stub (frame,
                         iot_fstat_wrapper,
                         fd);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_fstat call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

int32_t 
iot_truncate_cbk (call_frame_t *frame,
                  void *cookie,
                  xlator_t *this,
                  int32_t op_ret,
                  int32_t op_errno,
                  struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
iot_truncate_wrapper (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc,
                      off_t offset)
{
  STACK_WIND (frame,
              iot_truncate_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->truncate,
              loc,
              offset);
  return 0;
}

int32_t 
iot_truncate (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              off_t offset)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  char fd_list_empty = 0;
  
  conf = this->private;
  local = calloc (1, sizeof (*local));
  frame->local = local;

  LOCK (&loc->inode->lock);
  {
    if (list_empty (&loc->inode->fds))
      fd_list_empty = 1;
  }
  UNLOCK (&loc->inode->lock);

  if (fd_list_empty) {
    STACK_WIND(frame,
               iot_truncate_cbk,
               FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate,
               loc,
               offset);
    return 0;
  } 

  worker = iot_schedule (conf, NULL, loc->inode->ino);

  stub = fop_truncate_stub (frame,
                            iot_truncate_wrapper,
                            loc,
                            offset);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_stat call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (worker, stub);

  return 0;
}

int32_t 
iot_ftruncate_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

static int32_t 
iot_ftruncate_wrapper (call_frame_t *frame,
                       xlator_t *this,
                       fd_t *fd,
                       off_t offset)
{
  STACK_WIND (frame,
              iot_ftruncate_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->ftruncate,
              fd,
              offset);
  return 0;
}

int32_t 
iot_ftruncate (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               off_t offset)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  if (!dict_get (fd->ctx, this->name)) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_ftruncate_stub (frame,
                             iot_ftruncate_wrapper,
                             fd,
                             offset);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_ftruncate call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (worker, stub);

  return 0;
}

int32_t 
iot_utimens_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t 
iot_utimens_wrapper (call_frame_t *frame,
                     xlator_t *this,
                     loc_t *loc,
                     struct timespec tv[2])
{
  STACK_WIND (frame,
              iot_utimens_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->utimens,
              loc,
              tv);
  
  return 0;
}

int32_t 
iot_utimens (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             struct timespec tv[2])
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  char fd_list_empty = 0;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  LOCK (&(loc->inode->lock));
  {
    if (list_empty (&(loc->inode->fds)))
	fd_list_empty = 1;
  }
  UNLOCK (&(loc->inode->lock));

  if (fd_list_empty) {
    STACK_WIND(frame,
               iot_utimens_cbk,
               FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->utimens,
               loc,
               tv);
    return 0;
  } 

  worker = iot_schedule (conf, NULL, loc->inode->ino);

  stub = fop_utimens_stub (frame,
			   iot_utimens_wrapper,
			   loc,
			   tv);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_utimens call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (worker, stub);

  return 0;
}


static void
iot_queue (iot_worker_t *worker,
           call_stub_t *stub)
{
  iot_queue_t *queue;
  iot_conf_t *conf = worker->conf;
  iot_local_t *local = stub->frame->local;
  size_t frame_size = local->frame_size;

  queue = calloc (1, sizeof (*queue));
  queue->stub = stub;

  pthread_mutex_lock (&conf->lock);

  /*
    while (worker->queue_size >= worker->queue_limit)
      pthread_cond_wait (&worker->q_cond, &worker->lock);
  */
  while (frame_size && (conf->current_size >= conf->cache_size))
    pthread_cond_wait (&conf->q_cond, &conf->lock);


  queue->next = &worker->queue;
  queue->prev = worker->queue.prev;

  queue->next->prev = queue;
  queue->prev->next = queue;

  /* dq_cond */
  worker->queue_size++;
  worker->q++;

  conf->current_size += local->frame_size;

  pthread_cond_broadcast (&worker->dq_cond);

  pthread_mutex_unlock (&conf->lock);
}

static call_stub_t *
iot_dequeue (iot_worker_t *worker)
{
  call_stub_t *stub = NULL;
  iot_queue_t *queue = NULL;
  iot_conf_t *conf = worker->conf;
  iot_local_t *local = NULL;


  pthread_mutex_lock (&conf->lock);

  while (!worker->queue_size)
    /*
      pthread_cond_wait (&worker->dq_cond, &worker->lock);
    */
    pthread_cond_wait (&worker->dq_cond, &conf->lock);

  queue = worker->queue.next;

  queue->next->prev = queue->prev;
  queue->prev->next = queue->next;

  stub = queue->stub;
  local = stub->frame->local;

  worker->queue_size--;
  worker->dq++;

  /* q_cond */
  conf->current_size -= local->frame_size;

  pthread_cond_broadcast (&conf->q_cond);

  pthread_mutex_unlock (&conf->lock);

  freee (queue);

  return stub;
}

static void *
iot_worker (void *arg)
{
  iot_worker_t *worker = arg;

  while (1) {
    call_stub_t *stub;

    stub = iot_dequeue (worker);
    call_resume (stub);
  }
}

#if 0
static void *
iot_reply (void *arg)
{
  iot_worker_t *reply = arg;

  while (1) {
    call_stub_t *stub;

    stub = iot_dequeue (reply);
    freee (stub->frame->local);
    stub->frame->local = NULL;
    call_resume (stub);
  }
}
#endif

static void
workers_init (iot_conf_t *conf)
{
  int i;

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

    /*
      pthread_mutex_init (&worker->lock, NULL);
      pthread_cond_init (&worker->q_cond, NULL);
    */
    pthread_cond_init (&worker->dq_cond, NULL);

    /*
      worker->queue_limit = conf->queue_limit;
    */

    worker->conf = conf;

    pthread_create (&worker->thread, NULL, iot_worker, worker);
  }
}

int32_t 
init (xlator_t *this)
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

  conf->thread_count = 1;

  if (dict_get (options, "thread-count")) {
    conf->thread_count = data_to_int32 (dict_get (options,
            "thread-count"));
    gf_log ("io-threads",
      GF_LOG_DEBUG,
      "Using conf->thread_count = %d",
      conf->thread_count);
  }

  /*
  conf->queue_limit = 64;

  if (dict_get (options, "queue-limit")) {
    conf->queue_limit = data_to_int (dict_get (options,
                 "queue-limit"));
    gf_log ("io-threads",
      GF_LOG_DEBUG,
      "Using conf->queue_limit = %d",
      conf->queue_limit);
  }
  */

  conf->cache_size = 1048576 * 64;

  if (dict_get (options, "cache-size")) {
    conf->cache_size = gf_str_to_long_long (data_to_str (dict_get (options,
                "cache-size")));
    gf_log ("io-threads",
      GF_LOG_DEBUG,
      "Using conf->cache_size = %lld",
      conf->cache_size);
  }
  pthread_mutex_init (&conf->lock, NULL);
  pthread_cond_init (&conf->q_cond, NULL);

  conf->files.next = &conf->files;
  conf->files.prev = &conf->files;
  pthread_mutex_init (&conf->files_lock, NULL);

  workers_init (conf);

  this->private = conf;
  return 0;
}

void
fini (xlator_t *this)
{
  iot_conf_t *conf = this->private;

  freee (conf);

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
  .lk          = iot_lk,
  .stat        = iot_stat,
  .fstat       = iot_fstat,
  .truncate    = iot_truncate,
  .ftruncate   = iot_ftruncate,
  .utimens     = iot_utimens,
  .close       = iot_close,
};

struct xlator_mops mops = {
};
