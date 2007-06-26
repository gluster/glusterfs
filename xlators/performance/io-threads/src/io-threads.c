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

#include <call-stub.h>
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"

#define WORKER_INIT(worker, conf)               \
do {                                            \
  worker->next = worker;                        \
  worker->prev = worker;                        \
  worker->queue.next = &(worker->queue);        \
  worker->queue.prev = &(worker->queue);        \
  pthread_cond_init (&worker->dq_cond, NULL);   \
  worker->conf = conf;                          \
} while (0)
  

static void
iot_queue (iot_worker_t *worker,
           call_stub_t *stub);

static call_stub_t *
iot_dequeue (iot_worker_t *worker);

static iot_worker_t * 
iot_schedule (iot_conf_t *conf,
              iot_file_t *file,
              struct stat *buf)
{
  int32_t cnt = (buf->st_ino % conf->thread_count);
  iot_worker_t *trav = conf->workers.next;

  for (; cnt; cnt--)
    trav = trav->next;
  
  if (file)
    file->worker = trav;
  trav->fd_count++;
  return trav;
}

static int32_t
iot_open_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno,
              fd_t *fd)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  if (op_ret >= 0) {
    iot_file_t *file = calloc (1, sizeof (*file));

    iot_schedule (conf, file, &(fd->inode->buf));
    file->fd = fd;

    dict_set (fd->ctx,
              this->name,
              data_from_static_ptr (file));

    pthread_mutex_lock (&conf->files_lock);
    {
      file->next = &conf->files;
      file->prev = file->next->prev;
      file->next->prev = file;
      file->prev->next = file;
    }
    pthread_mutex_unlock (&conf->files_lock);
  }

  stub = fop_open_cbk_stub (frame,
			    NULL,
			    op_ret,
			    op_errno,
			    fd);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_open_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_open_wrapper (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t flags)
{
  STACK_WIND (frame,
	      iot_open_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->open,
	      loc,
	      flags);
  return 0;
}

static int32_t
iot_open (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc,
          int32_t flags)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_open_stub (frame,
			iot_open_wrapper,
			loc,
			flags);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_open call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }


  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}


static int32_t
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

    iot_schedule (conf, file, &(fd->inode->buf));
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

static int32_t 
iot_create_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    const char *pathname,
		    int32_t flags,
		    mode_t mode)
{
  STACK_WIND (frame,
              iot_create_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
              pathname,
              flags,
              mode);
  return 0;
}

static int32_t
iot_create (call_frame_t *frame,
            xlator_t *this,
            const char *pathname,
            int32_t flags,
            mode_t mode)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_create_stub (frame,
			  iot_create_wrapper,
			  pathname,
			  flags,
			  mode);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_open call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (&(conf->meta_worker), stub);

  return 0;
}

static int32_t
iot_close_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local = frame->local;
  iot_file_t *file = local->file;

  if (op_ret == 0) {
    pthread_mutex_lock (&conf->files_lock);
    {
      file->prev->next = file->next;
      file->next->prev = file->prev;
    }
    pthread_mutex_unlock (&conf->files_lock);

    file->worker->fd_count--;
    file->worker = NULL;
    free (file);
  }
  
  stub = fop_close_cbk_stub (frame,
                             NULL,
                             op_ret,
                             op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get close_cbk call stub");

    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
                             
  iot_queue (reply, stub);

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

static int32_t
iot_close (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

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


static int32_t
iot_readv_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               struct iovec *vector,
               int32_t count,
	       struct stat *stbuf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_readv_cbk_stub (frame,
                             NULL,
                             op_ret,
                             op_errno,
                             vector,
                             count,
			     stbuf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get readv_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, 0, stbuf);
    return 0;
  }
                             
  iot_queue (reply, stub);

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

static int32_t
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
    STACK_UNWIND (frame, -1, ENOMEM, NULL, 0, NULL);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t
iot_flush_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_flush_cbk_stub (frame,
                             NULL,
                             op_ret,
                             op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get flush_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);

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

static int32_t
iot_flush (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

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

static int32_t
iot_fsync_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_fsync_cbk_stub (frame,
                             NULL,
                             op_ret,
                             op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fsync_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
    
  iot_queue (reply, stub);

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

static int32_t
iot_fsync (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           int32_t datasync)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

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

static int32_t
iot_writev_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
		struct stat *stbuf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; /* hehe, caught me! */
  stub = fop_writev_cbk_stub (frame,
                              NULL,
                              op_ret,
                              op_errno,
			      stbuf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get writev_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, stbuf);
    return 0;
  }

  iot_queue (reply, stub);

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

static int32_t
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

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));

  local->frame_size = iov_length (vector, count);
  frame->local = local;
  
  stub = fop_writev_stub (frame,
                          iot_writev_wrapper,
                          fd,
                          vector,
                          count,
                          offset);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get writev call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t
iot_lk_cbk (call_frame_t *frame,
            void *cookie,
            xlator_t *this,
            int32_t op_ret,
            int32_t op_errno,
            struct flock *flock)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_lk_cbk_stub (frame,
                          NULL,
                          op_ret,
                          op_errno,
                          flock);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_lk_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

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

static int32_t
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


static int32_t 
iot_stat_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno,
              struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_stat_cbk_stub (frame,
			    NULL,
			    op_ret,
			    op_errno,
			    buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_stat_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (reply, stub);

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

static int32_t 
iot_stat (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_conf_t *conf;
 
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

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

 if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
 } 
 else {
   iot_worker_t *worker;
   worker = iot_schedule (conf, NULL, &(loc->inode->buf));
   iot_queue (worker, stub);
 }

  return 0;
}


static int32_t 
iot_fstat_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_fstat_cbk_stub (frame,
                             NULL,
                             op_ret,
                             op_errno,
                             buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_fstat_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (reply, stub);

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

static int32_t 
iot_fstat (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

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

static int32_t 
iot_truncate_cbk (call_frame_t *frame,
                  void *cookie,
                  xlator_t *this,
                  int32_t op_ret,
                  int32_t op_errno,
                  struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_truncate_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno,
				buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_truncate_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

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

static int32_t 
iot_truncate (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              off_t offset)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;
  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_truncate_stub (frame,
                            iot_truncate_wrapper,
                            loc,
                            offset);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_truncate call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  if (!loc->inode) {
    iot_queue (&conf->meta_worker, stub);
  } else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t 
iot_ftruncate_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_ftruncate_cbk_stub (frame,
                                 NULL,
                                 op_ret,
                                 op_errno,
                                 buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_ftruncate_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  iot_queue (reply, stub);

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

static int32_t 
iot_ftruncate (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               off_t offset)
{
  call_stub_t *stub;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

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

static int32_t 
iot_utimens_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_utimens_cbk_stub (frame,
			     NULL,
			     op_ret,
			     op_errno,
			     buf);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_utimens_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

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

static int32_t 
iot_utimens (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             struct timespec tv[2])
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

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

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t 
iot_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_lookup_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno,
			      inode,
			      buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_lookup_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  iot_queue (reply, stub);
  
  return 0;
}

static int32_t 
iot_lookup_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc)
{
  STACK_WIND (frame,
              iot_lookup_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->lookup,
              loc);
  
  return 0;
}

static int32_t 
iot_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_conf_t *conf;
  iot_worker_t *worker;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_lookup_stub (frame,
			  iot_lookup_wrapper,
			  loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_lookup call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }
  
  return 0;
}

static int32_t 
iot_forget_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_forget_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_forget_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_forget_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    inode_t *inode)
{
  STACK_WIND (frame,
              iot_forget_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->forget,
              inode);
  
  return 0;
}

static int32_t 
iot_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_forget_stub (frame,
			  iot_forget_wrapper,
			  inode);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_forget call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  worker = iot_schedule (conf, NULL, &(inode->buf));
  iot_queue (worker, stub);
  return 0;
}

static int32_t
iot_chmod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_chmod_cbk_stub (frame,
			     NULL,
			     op_ret,
			     op_errno,
			     buf);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_chmod_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_chmod_wrapper (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   mode_t mode)
{
  STACK_WIND (frame,
	      iot_chmod_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->chmod,
	      loc,
	      mode);
  return 0;
}

static int32_t
iot_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_chmod_stub (frame,
			 iot_chmod_wrapper,
			 loc,
			 mode);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_chmod call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_fchmod_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_fchmod_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno,
			      buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fchmod_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
                             
  iot_queue (reply, stub);
  return 0;
}

static int32_t 
iot_fchmod_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    mode_t mode)
{
  STACK_WIND (frame,
	      iot_fchmod_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->fchmod,
	      fd,
	      mode);
  return 0;
}

static int32_t 
iot_fchmod (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    mode_t mode)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_fchmod_stub (frame, 
			  iot_fchmod_wrapper,
			  fd,
			  mode);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fchmod call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t
iot_chown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_chown_cbk_stub (frame,
			     NULL,
			     op_ret,
			     op_errno,
			     buf);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_chown_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_chown_wrapper (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   uid_t uid,
		   gid_t gid)
{
  STACK_WIND (frame,
	      iot_chown_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->chown,
	      loc,
	      uid,
	      gid);
  return 0;
}

static int32_t
iot_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_chown_stub (frame,
			 iot_chown_wrapper,
			 loc,
			 uid,
			 gid);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_chown call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_fchown_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_fchown_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno,
			      buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fchown_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
                             
  iot_queue (reply, stub);
  return 0;
}

static int32_t
iot_fchown_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    uid_t uid,
		    gid_t gid)
{
  STACK_WIND (frame,
	      iot_fchown_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->fchown,
	      fd,
	      uid,
	      gid);
  return 0;
}

static int32_t 
iot_fchown (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    uid_t uid,
	    gid_t gid)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_fchown_stub (frame, 
			  iot_fchown_wrapper,
			  fd,
			  uid,
			  gid);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fchown call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t 
iot_access_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;
  iot_local_t *local;

  local = frame->local;

  stub = fop_access_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_access_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_access_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    int32_t mask)
{
  STACK_WIND (frame,
	      iot_access_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->access,
	      loc,
	      mask);
  return 0;
}

static int32_t
iot_access (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t mask)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_access_stub (frame,
			  iot_access_wrapper,
			  loc,
			  mask);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_access call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_readlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      const char *path)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_readlink_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno,
				path);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_readlink_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t
iot_readlink_wrapper (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      size_t size)
{
  STACK_WIND (frame,
	      iot_readlink_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->readlink,
	      loc,
	      size);
  return 0;
}

static int32_t
iot_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_readlink_stub (frame,
			    iot_readlink_wrapper,
			    loc,
			    size);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_readlink_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_mknod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_mknod_cbk_stub (frame,
			     NULL,
			     op_ret,
			     op_errno,
			     inode,
			     buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get mknod_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }
                             
  iot_queue (&(conf->reply), stub);
  return 0;
}

static int32_t 
iot_mknod_wrapper (call_frame_t *frame,
		   xlator_t *this,
		   const char *name,
		   mode_t mode,
		   dev_t rdev)
{
  STACK_WIND (frame,
	      iot_mknod_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->mknod,
	      name,
	      mode,
	      rdev);
  return 0;
}

static int32_t
iot_mknod (call_frame_t *frame,
	   xlator_t *this,
	   const char *name,
	   mode_t mode,
	   dev_t rdev)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_conf_t *conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_mknod_stub (frame, 
			 iot_mknod_wrapper,
			 name,
			 mode,
			 rdev);
    
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_mknod call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  iot_queue (&(conf->meta_worker), stub);

  return 0;
}

static int32_t
iot_mkdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_mkdir_cbk_stub (frame,
			     NULL,
			     op_ret,
			     op_errno,
			     inode,
			     buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_mkdir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }
                             
  iot_queue (&(conf->reply), stub);
  return 0;
}

static int32_t 
iot_mkdir_wrapper (call_frame_t *frame,
		   xlator_t *this,
		   const char *name,
		   mode_t mode)
{
  STACK_WIND (frame,
	      iot_mkdir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->mkdir,
	      name,
	      mode);
  return 0;
}

static int32_t
iot_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   const char *name,
	   mode_t mode)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_conf_t *conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_mkdir_stub (frame, 
			 iot_mkdir_wrapper,
			 name,
			 mode);
    
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_mkdir call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  iot_queue (&(conf->meta_worker), stub);

  return 0;
}

static int32_t
iot_unlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_unlink_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_unlink_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);
  return 0;
}

static int32_t 
iot_unlink_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc)
{
  STACK_WIND (frame,
	      iot_unlink_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->unlink,
	      loc);
  return 0;
}

static int32_t
iot_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_unlink_stub (frame,
			  iot_unlink_wrapper,
			  loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_unlink call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_rmdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_rmdir_cbk_stub (frame,
			     NULL,
			     op_ret,
			     op_errno);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_rmdir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t
iot_rmdir_wrapper (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc)
{
  STACK_WIND (frame,
	      iot_rmdir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->rmdir,
	      loc);
  return 0;
}

static int32_t
iot_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_rmdir_stub (frame,
			 iot_rmdir_wrapper,
			 loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_rmdir call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_symlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_symlink_cbk_stub (frame,
			       NULL,
			       op_ret,
			       op_errno,
			       inode,
			       buf);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_symlink_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }
                             
  iot_queue (&(conf->reply), stub);
  return 0;
}

static int32_t 
iot_symlink_wrapper (call_frame_t *frame,
		     xlator_t *this,
		     const char *linkpath,
		     const char *name)
{
  STACK_WIND (frame,
	      iot_symlink_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->symlink,
	      linkpath,
	      name);
  return 0;
}

static int32_t
iot_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkpath,
	     const char *name)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_conf_t *conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_symlink_stub (frame, 
			   iot_symlink_wrapper,
			   linkpath,
			   name);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_symlink call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  iot_queue (&(conf->meta_worker), stub);
  return 0;
}

static int32_t
iot_link_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_link_cbk_stub (frame,
			    NULL,
			    op_ret,
			    op_errno,
			    inode,
			    buf);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_link_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_link_wrapper (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  const char *name)
{
  STACK_WIND (frame,
	      iot_link_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->link,
	      loc,
	      name);
  return 0;
}

static int32_t
iot_link (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  const char *newname)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_link_stub (frame,
			iot_link_wrapper,
			loc,
			newname);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_link call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_opendir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  if (op_ret >= 0) {
    iot_file_t *file = calloc (1, sizeof (*file));

    iot_schedule (conf, file, &(fd->inode->buf));
    file->fd = fd;

    dict_set (fd->ctx,
              this->name,
              data_from_static_ptr (file));

    pthread_mutex_lock (&conf->files_lock);
    {
      file->next = &conf->files;
      file->prev = file->next->prev;
      file->next->prev = file;
      file->prev->next = file;
    }
    pthread_mutex_unlock (&conf->files_lock);
  }

  stub = fop_opendir_cbk_stub (frame,
			       NULL,
			       op_ret,
			       op_errno,
			       fd);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_opendir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_opendir_wrapper (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc)
{
  STACK_WIND (frame,
	      iot_opendir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->opendir,
	      loc);
  return 0;
}

static int32_t
iot_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_opendir_stub (frame,
			   iot_opendir_wrapper,
			   loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_opendir call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_readdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dir_entry_t *entries,
		 int32_t count)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_readdir_cbk_stub (frame,
			       NULL,
			       op_ret,
			       op_errno,
			       entries,
			       count);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get readdir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, 0);
    return 0;
  }
                             
  iot_queue (reply, stub);
  return 0;
}

static int32_t 
iot_readdir_wrapper (call_frame_t *frame,
		     xlator_t *this,
		     size_t size,
		     off_t offset,
		     fd_t *fd)
{
  STACK_WIND (frame,
	      iot_readdir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->readdir,
	      size,
	      offset,
	      fd);
  return 0;
}

static int32_t
iot_readdir (call_frame_t *frame,
	     xlator_t *this,
	     size_t size,
	     off_t offset,
	     fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_readdir_stub (frame, 
			   iot_readdir_wrapper,
			   size,
			   offset,
			   fd);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_readdir call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL, 0);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t
iot_closedir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;
  iot_file_t *file = local->file;

  if (op_ret == 0) {
    pthread_mutex_lock (&conf->files_lock);
    {
      file->prev->next = file->next;
      file->next->prev = file->prev;
    }
    pthread_mutex_unlock (&conf->files_lock);
    
    file->worker->fd_count--;
    file->worker = NULL;
    free (file);
  }

  stub = fop_closedir_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get closedir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
                             
  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_closedir_wrapper (call_frame_t *frame,
		      xlator_t *this,
		      fd_t *fd)
{
  STACK_WIND (frame,
	      iot_closedir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->closedir,
	      fd);
  return 0;
}

static int32_t
iot_closedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  local->file = file;
  frame->local = local;
  
  stub = fop_closedir_stub (frame, 
			    iot_closedir_wrapper,
			    fd);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get closedir stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t
iot_fsyncdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_fsyncdir_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fsyncdir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
                             
  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_fsyncdir_wrapper (call_frame_t *frame,
		      xlator_t *this,
		      fd_t *fd,
		      int32_t flags)
{
  STACK_WIND (frame,
	      iot_fsyncdir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->fsyncdir,
	      fd,
	      flags);
  return 0;
}

static int32_t
iot_fsyncdir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_fsyncdir_stub (frame, 
			    iot_fsyncdir_wrapper,
			    fd,
			    flags);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fsyncdir stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (worker, stub);

  return 0;
}

static int32_t
iot_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *buf)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_statfs_cbk_stub (frame,
			      NULL,
			      op_ret,
			      op_errno,
			      buf);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_statfs_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);
  return 0;
}

static int32_t 
iot_statfs_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc)
{
  STACK_WIND (frame,
	      iot_statfs_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->statfs,
	      loc);
  return 0;
}

static int32_t
iot_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_statfs_stub (frame,
			  iot_statfs_wrapper,
			  loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_statfs call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  if (!loc->inode){
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_setxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_setxattr_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_setxattr_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t 
iot_setxattr_wrapper (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc,
		      dict_t *dict,
		      int32_t flags)
{
  STACK_WIND (frame,
	      iot_setxattr_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->setxattr,
	      loc,
	      dict,
	      flags);
  return 0;
}

static int32_t
iot_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      dict_t *dict,
	      int32_t flags)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_setxattr_stub (frame,
			    iot_setxattr_wrapper,
			    loc,
			    dict,
			    flags);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_setxattr call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }
  return 0;
}

static int32_t
iot_getxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *dict)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_getxattr_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno,
				dict);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_getxattr_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  iot_queue (reply, stub);

  return 0;
}

static int32_t
iot_getxattr_wrapper (call_frame_t *frame,
		      xlator_t *this,
		      loc_t *loc)
{
  STACK_WIND (frame,
	      iot_getxattr_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->getxattr,
	      loc);
  return 0;
}

static int32_t
iot_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_getxattr_stub (frame,
			    iot_getxattr_wrapper,
			    loc);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_getxattr call stub");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_removexattr_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_worker_t *reply = &conf->reply;

  stub = fop_removexattr_cbk_stub (frame,
				   NULL,
				   op_ret,
				   op_errno);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_removexattr_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (reply, stub);


  return 0;
}

static int32_t
iot_removexattr_wrapper (call_frame_t *frame,
			 xlator_t *this,
			 loc_t *loc,
			 const char *name)
{
  STACK_WIND (frame,
	      iot_removexattr_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->removexattr,
	      loc,
	      name);
  return 0;
}

static int32_t
iot_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_worker_t *worker = NULL;
  iot_conf_t *conf;
  
  conf = this->private;

  local = calloc (1, sizeof (*local));
  frame->local = local;

  stub = fop_removexattr_stub (frame,
			       iot_removexattr_wrapper,
			       loc,
			       name);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_removexattr call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  if (!loc->inode) {
    iot_queue (&(conf->meta_worker), stub);
  } 
  else {
    worker = iot_schedule (conf, NULL, &(loc->inode->buf));
    iot_queue (worker, stub);
  }

  return 0;
}

static int32_t
iot_writedir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  call_stub_t *stub;
  iot_conf_t *conf = this->private;
  iot_local_t *local = frame->local;
  iot_worker_t *reply = &conf->reply;

  local->frame_size = 0; //iov_length (vector, count);

  stub = fop_writedir_cbk_stub (frame,
				NULL,
				op_ret,
				op_errno);
  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get writedir_cbk call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }
                             
  iot_queue (reply, stub);
  return 0;

}

static int32_t 
iot_writedir_wrapper (call_frame_t *frame,
		      xlator_t *this,
		      fd_t *fd,
		      int32_t flags,
		      dir_entry_t *entries,
		      int32_t count)
{
  STACK_WIND (frame,
	      iot_writedir_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->writedir,
	      fd,
	      flags,
	      entries,
	      count);
  return 0;
}

static int32_t
iot_writedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags,
	      dir_entry_t *entries,
	      int32_t count)
{
  call_stub_t *stub;
  iot_local_t *local = NULL;
  iot_file_t *file = NULL;
  iot_worker_t *worker = NULL;

  file = data_to_ptr (dict_get (fd->ctx, this->name));
  worker = file->worker;

  local = calloc (1, sizeof (*local));
  frame->local = local;
  
  stub = fop_writedir_stub (frame, 
			    iot_writedir_wrapper,
			    fd,
			    flags,
			    entries,
			    count);

  if (!stub) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "cannot get fop_writedir call stub");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  iot_queue (worker, stub);
  return 0;
}

/* FIXME: call-stub not implemented for rename */
#if 0
static int32_t
iot_rename_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  return 0;
}

static int32_t 
iot_rename_wrapper (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *oldloc,
		    loc_t *newloc)
{
  STACK_WIND (frame,
	      iot_rename_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->rename,
	      oldloc,
	      newloc);
  return 0;
}

static int32_t
iot_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
  return 0;
}
#endif

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

  free (queue);

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

static void *
iot_reply (void *arg)
{
  iot_worker_t *reply = arg;

  while (1) {
    call_stub_t *stub;

    stub = iot_dequeue (reply);
    free (stub->frame->local);
    stub->frame->local = NULL;
    call_resume (stub);
  }
}

static void
workers_init (iot_conf_t *conf)
{
  int i;
  iot_worker_t *reply = &conf->reply;
  iot_worker_t *meta_worker = &conf->meta_worker;
  char *name;
  WORKER_INIT (reply, conf);
  WORKER_INIT (meta_worker, conf);

  name = calloc (1, sizeof ("reply"));
  strcpy (name, "reply");
  reply->name = name;

  name = calloc (1, sizeof ("meta"));
  strcpy (name, "meta");
  meta_worker->name = name;
  pthread_create (&reply->thread, NULL, iot_reply, reply);
  pthread_create (&meta_worker->thread, NULL, iot_worker, meta_worker);

  conf->workers.next = &conf->workers;
  conf->workers.prev = &conf->workers;

  for (i=0; i<conf->thread_count; i++) {

    iot_worker_t *worker = calloc (1, sizeof (*worker));
    name = calloc(1, sizeof("worker") + 3);
    sprintf (name, "worker %d", i);
    worker->name = name;
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

  conf->cache_size = 64 * 1024 * 1024; /* 64 MB */

  if (dict_get (options, "cache-size")) {
    conf->cache_size = data_to_int64 (dict_get (options,
						"cache-size"));
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

  free (conf);
  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .lookup = iot_lookup,
  .forget = iot_forget,
  .stat = iot_stat,
  .fstat = iot_fstat,
  .chmod = iot_chmod,
  .fchmod = iot_fchmod,
  .chown = iot_chown,
  .fchown = iot_fchown,
  .truncate = iot_truncate,
  .ftruncate = iot_ftruncate,
  .utimens = iot_utimens,
  .access = iot_access,
  .readlink = iot_readlink,
  .mknod = iot_mknod,
  .mkdir = iot_mkdir,
  .unlink = iot_unlink,
  .rmdir = iot_rmdir,
  .symlink = iot_symlink,
  //  .rename = iot_rename,
  .link = iot_link,
  .create = iot_create,
  .open = iot_open,
  .readv = iot_readv,
  .writev = iot_writev,
  .flush = iot_flush,
  .close = iot_close,
  .fsync = iot_fsync,
  .opendir = iot_opendir,
  .readdir = iot_readdir,
  .closedir = iot_closedir,
  .fsyncdir = iot_fsyncdir,
  .statfs = iot_statfs,
  .setxattr = iot_setxattr,
  .getxattr = iot_getxattr,
  .removexattr = iot_removexattr,
  .lk = iot_lk,
  .writedir = iot_writedir

};

struct xlator_mops mops = {
};
