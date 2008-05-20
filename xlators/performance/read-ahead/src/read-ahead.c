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

/* 
   TODO:
   - handle O_DIRECT
   - maintain offset, flush on lseek
   - ensure efficient memory managment in case of random seek
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "read-ahead.h"
#include <assert.h>
#include <sys/time.h>


static void
read_ahead (call_frame_t *frame,
            ra_file_t *file);


int32_t
ra_open_cbk (call_frame_t *frame,
             void *cookie,
             xlator_t *this,
             int32_t op_ret,
             int32_t op_errno,
             fd_t *fd)
{
  ra_local_t *local = frame->local;
  ra_conf_t *conf = this->private;

  if (op_ret != -1) {
    ra_file_t *file = calloc (1, sizeof (*file));

    file = ra_file_ref (file);
    file->fd = fd;
    dict_set (fd->ctx, this->name,
              data_from_static_ptr (file));

    /* If mandatory locking has been enabled on this file,
       we disable caching on it */

    if ((fd->inode->st_mode & S_ISGID) && !(fd->inode->st_mode & S_IXGRP))
      file->disabled = 1;

    /* If O_DIRECT open, we disable caching on it */

    if ((local->flags & O_DIRECT) || (local->flags & O_WRONLY))
      file->disabled = 1;

    file->offset = (unsigned long long) 0;
    //    file->size = fd->inode->buf.st_size;
    file->conf = conf;
    file->pages.next = &file->pages;
    file->pages.prev = &file->pages;
    file->pages.offset = (unsigned long long) 0;
    file->pages.file = file;

    ra_conf_lock (conf);
    {
      file->next = conf->files.next;
      conf->files.next = file;
      file->next->prev = file;
      file->prev = &conf->files;
    }
    ra_conf_unlock (conf);

    file->page_count = conf->page_count;
    file->page_size = conf->page_size;
    pthread_mutex_init (&file->file_lock, NULL);

    if (!file->disabled) {
      file->page_count = 1;
      read_ahead (frame, file);
    }
  }

  FREE (local->file_loc.path);
  FREE (local);
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

int32_t
ra_create_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               fd_t *fd,
               inode_t *inode,
               struct stat *buf)
{
  ra_local_t *local = frame->local;
  ra_conf_t *conf = this->private;

  if (op_ret != -1) {
    ra_file_t *file = calloc (1, sizeof (*file));
    file = ra_file_ref (file);

    file->fd = fd;
    dict_set (fd->ctx, this->name,
              data_from_static_ptr (file));

    /* If mandatory locking has been enabled on this file,
       we disable caching on it */

    if ((fd->inode->st_mode & S_ISGID) && !(fd->inode->st_mode & S_IXGRP))
      file->disabled = 1;

    /* If O_DIRECT open, we disable caching on it */

    if ((local->flags & O_DIRECT) || (local->flags & O_WRONLY))
      file->disabled = 1;

    file->offset = (unsigned long long) 0;
    //file->size = fd->inode->buf.st_size;
    file->conf = conf;
    file->pages.next = &file->pages;
    file->pages.prev = &file->pages;
    file->pages.offset = (unsigned long long) 0;
    file->pages.file = file;

    ra_conf_lock (conf);
    {
      file->next = conf->files.next;
      conf->files.next = file;
      file->next->prev = file;
      file->prev = &conf->files;
    }
    ra_conf_unlock (conf);

    file->page_count = conf->page_count;
    file->page_size = conf->page_size;
    pthread_mutex_init (&file->file_lock, NULL);
  }

  FREE (local->file_loc.path);
  FREE (local);
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);

  return 0;
}

int32_t
ra_open (call_frame_t *frame,
         xlator_t *this,
         loc_t *loc,
         int32_t flags,
	 fd_t *fd)
{
  ra_local_t *local = calloc (1, sizeof (*local));

  local->file_loc.inode = loc->inode;
  local->file_loc.path = strdup (loc->path);

  local->flags = flags;

  frame->local = local;

  STACK_WIND (frame,
              ra_open_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->open,
              loc,
              flags,
	      fd);

  return 0;
}

int32_t
ra_create (call_frame_t *frame,
           xlator_t *this,
	   loc_t *loc,
           int32_t flags,
           mode_t mode,
	   fd_t *fd)
{
  ra_local_t *local = calloc (1, sizeof (*local));


  local->file_loc.inode = loc->inode;
  local->file_loc.path = strdup (loc->path);

  local->mode = mode;
  local->flags = 0;
  frame->local = local;

  STACK_WIND (frame,
              ra_create_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
	      loc, flags, mode, fd);

  return 0;
}

/* free cache pages between offset and offset+size,
   does not touch pages with frames waiting on it
*/

static void
flush_region (call_frame_t *frame,
              ra_file_t *file,
              off_t offset,
              off_t size)
{
  ra_page_t *trav;

  ra_file_lock (file);

  trav = file->pages.next;
  while (trav != &file->pages && trav->offset < (offset + size)) {
    ra_page_t *next = trav->next;
    if (trav->offset >= offset && !trav->waitq) {

      if (!trav->ready) {
	gf_log (frame->this->name, GF_LOG_DEBUG,
		"killing featus, file=%p, offset=%lld, de=%lld, a=%lld",
		file, trav->offset, offset, size);
      }
      ra_page_purge (trav);
    }
    trav = next;
  }

  ra_file_unlock (file);
}


int32_t
ra_close_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno)
{
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
ra_close (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_file_t *file = NULL;

  if (file_data) {
    file = data_to_ptr (file_data);
    
    flush_region (frame, file, 0, file->pages.prev->offset+1);
    dict_del (fd->ctx, this->name);
    
    file->fd = NULL;
    ra_file_unref (file);
  }

  STACK_WIND (frame,
              ra_close_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->close,
              fd);
  return 0;
}


void
read_ahead (call_frame_t *frame,
            ra_file_t *file)
{
  off_t ra_offset;
  size_t ra_size;
  off_t trav_offset;
  ra_page_t *trav = NULL;
  off_t cap = file->size;

  if (!file->page_count)
    return;

  ra_size = file->page_size * file->page_count;
  ra_offset = floor (file->offset, file->page_size);
  cap = file->size ? file->size : file->offset + ra_size;

  while (ra_offset < min (file->offset + ra_size, cap)) {
    ra_file_lock (file);
    trav = ra_page_get (file, ra_offset);
    ra_file_unlock (file);
    if (!trav)
      break;
    ra_offset += file->page_size;
  }

  if (trav)
    /* comfortable enough */
    return;

  trav_offset = ra_offset;

  trav = file->pages.next;
  cap = file->size ? file->size : ra_offset + ra_size;
  while (trav_offset < min(ra_offset + ra_size, cap)) {
    char fault = 0;
    ra_file_lock (file);
    trav = ra_page_get (file, trav_offset);
    if (!trav) {
      fault = 1;
      trav = ra_page_create (file, trav_offset);
      trav->dirty = 1;
    }
    ra_file_unlock (file);

    if (fault) {
      gf_log (frame->this->name, GF_LOG_DEBUG,
	      "RA at offset=%"PRId64, trav_offset);
      ra_page_fault (file, frame, trav_offset);
    }
    trav_offset += file->page_size;
  }
  return ;
}

int32_t
ra_need_atime_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   struct iovec *vector,
                   int32_t count,
		   struct stat *stbuf)
{
  STACK_DESTROY (frame->root);
  return 0;
}

static void
dispatch_requests (call_frame_t *frame,
                   ra_file_t *file)
{
  ra_local_t *local = frame->local;
  ra_conf_t *conf = file->conf;
  off_t rounded_offset;
  off_t rounded_end;
  off_t trav_offset;
  ra_page_t *trav;
  call_frame_t *ra_frame;
  char need_atime_update = 1;

  rounded_offset = floor (local->offset, file->page_size);
  rounded_end = roof (local->offset + local->size, file->page_size);

  trav_offset = rounded_offset;
  trav = file->pages.next;

  while (trav_offset < rounded_end) {
    char fault = 0;

    ra_file_lock (file);
    trav = ra_page_get (file, trav_offset);
    if (!trav) {
      trav = ra_page_create (file, trav_offset);
      fault = 1;
      need_atime_update = 0;
    } 

    if (trav->ready) {
      gf_log (frame->this->name, GF_LOG_DEBUG,
	      "HIT at offset=%"PRId64".",
	      trav_offset);
      ra_frame_fill (trav, frame);
    } else {
      gf_log (frame->this->name, GF_LOG_DEBUG,
	      "IN-TRANSIT at offset=%"PRId64".",
	      trav_offset);
      ra_wait_on_page (trav, frame);
      need_atime_update = 0;
    }
    ra_file_unlock (file);

    if (fault) {
      gf_log (frame->this->name, GF_LOG_DEBUG,
	      "MISS at offset=%"PRId64".",
	      trav_offset);
      ra_page_fault (file, frame, trav_offset);
    }

    trav_offset += file->page_size;
  }

  if (need_atime_update && conf->force_atime_update) {
    /* TODO: use untimens() since readv() can confuse underlying
       io-cache and others */
    ra_frame = copy_frame (frame);
    STACK_WIND (ra_frame, 
                ra_need_atime_cbk,
                FIRST_CHILD (frame->this), 
                FIRST_CHILD (frame->this)->fops->readv,
                file->fd, 1, 1);
  }

  return ;
}

int32_t
ra_readv_disabled_cbk (call_frame_t *frame, 
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct iovec *vector,
                       int32_t count,
		       struct stat *stbuf)
{
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (vector);

  STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
  return 0;
}

int32_t
ra_readv (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd,
          size_t size,
          off_t offset)
{
  ra_file_t *file;
  ra_local_t *local;
  ra_conf_t *conf = this->private;


  gf_log (this->name, GF_LOG_DEBUG,
	  "NEW REQ at offset=%"PRId64" for size=%d",
	  offset, size);

  file = data_to_ptr (dict_get (fd->ctx, this->name));

  if (file->offset != offset) {
    gf_log (this->name, GF_LOG_DEBUG,
	    "received unexpected offset (%"PRId64" != %"PRId64"), resetting page_count to 0",
	    file->offset, offset);
    file->expected = file->page_count = 0;
  } else {
    gf_log (this->name, GF_LOG_DEBUG,
	    "received expected offset (%"PRId64") when page_count=%d",
	    offset, file->page_count);
    if (file->expected < (conf->page_size * conf->page_count)) {
      file->expected += size;
      file->page_count = min ((file->expected / file->page_size),
			      conf->page_count);
    }
  }

  if (file->disabled) {
    STACK_WIND (frame, 
		ra_readv_disabled_cbk,
		FIRST_CHILD (frame->this), 
		FIRST_CHILD (frame->this)->fops->readv,
		file->fd, size, offset);
    return 0;
  }

  //  if (fd->inode->buf.st_mtime != file->stbuf.st_mtime)
    /* flush the whole read-ahead cache */
    //flush_region (frame, file, 0, file->pages.prev->offset + 1);

  call_frame_t *ra_frame = copy_frame (frame);


  local = (void *) calloc (1, sizeof (*local));
  local->offset = offset;
  local->size = size;
  local->file = ra_file_ref (file);
  local->wait_count = 1; /* for synchronous STACK_UNWIND from protocol in case of error */
  local->fill.next = &local->fill;
  local->fill.prev = &local->fill;
  pthread_mutex_init (&local->local_lock, NULL);

  frame->local = local;

  dispatch_requests (frame, file);

  flush_region (frame, file, 0, floor (offset, file->page_size));

  ra_frame_return (frame);

  read_ahead (ra_frame, file);

  file->offset = offset + size;

  STACK_DESTROY (ra_frame->root);

  return 0;
}

int32_t
ra_flush_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
ra_flush (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_file_t *file = NULL;

  if (file_data) {
    file = data_to_ptr (file_data);
    
    flush_region (frame, file, 0, file->pages.prev->offset+1);
  }

  STACK_WIND (frame,
	      ra_flush_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->flush,
	      fd);
  return 0;
}

int32_t
ra_fsync (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd,
          int32_t datasync)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_file_t *file = NULL;

  if (file_data) {
    file = data_to_ptr (file_data);
    flush_region (frame, file, 0, file->pages.prev->offset+1);
  }
  STACK_WIND (frame,
              ra_flush_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->fsync,
              fd,
              datasync);
  return 0;
}

int32_t
ra_writev_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
	       struct stat *stbuf)
{
  fd_t *fd = frame->local;
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_file_t *file = NULL;

  if (file_data) {
    file = data_to_ptr (file_data);
    flush_region (frame, file, 0, file->pages.prev->offset+1);
  }
  frame->local = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

int32_t
ra_writev (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           struct iovec *vector,
           int32_t count,
           off_t offset)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_file_t *file = NULL;

  frame->local = (void *) fd;
  if (file_data) {
    file = data_to_ptr (file_data);    
    flush_region (frame, file, 0, file->pages.prev->offset+1);

    /* reset the read-ahead counters too */
    file->expected = file->page_count = 0;
  }

  STACK_WIND (frame,
              ra_writev_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->writev,
              fd, vector, count, offset);

  return 0;
}

int32_t 
ra_truncate_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

int32_t 
ra_truncate (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             off_t offset)
{
  ra_file_t *file = NULL;
  fd_t *iter_fd = NULL;

  if (loc->inode) {
    LOCK (&(loc->inode->lock));
    {
      list_for_each_entry (iter_fd, &(loc->inode->fds), inode_list) {
	if (dict_get (iter_fd->ctx, this->name)) {
	  file = data_to_ptr (dict_get (iter_fd->ctx, this->name));
	  flush_region (frame, file, 0, file->pages.prev->offset + 1);
	}
      }
    }
    UNLOCK (&(loc->inode->lock));
  }

  STACK_WIND (frame,
              ra_truncate_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->truncate,
              loc, offset);
  return 0;
}

int32_t 
ra_ftruncate_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *buf)
{
  ra_local_t *local;
  ra_file_t *file;

  local = frame->local;
  file = local->file;

  frame->local = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, buf);

  ra_file_unref (file);
  free (local);
  return 0;
}

int32_t
ra_fstat_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *buf)
{
  ra_local_t *local;
  ra_file_t *file;

  local = frame->local;
  file = local->file;

  if ((op_ret == 0) && (file->stbuf.st_mtime != buf->st_mtime))
    flush_region (frame, file, 0, file->pages.prev->offset + 1);

  frame->local = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, buf);

  if (file)
    ra_file_unref (file);
  free (local);
  return 0;
}

int32_t 
ra_fstat (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_local_t *local;
  ra_file_t *file = NULL;

  if (file_data) {
    file = data_to_ptr (file_data);
  }

  local = calloc (1, sizeof (*local));
  if (file)
    local->file = ra_file_ref (file);
  frame->local = local;

  STACK_WIND (frame,
	      ra_fstat_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->fstat,
	      fd);
  return 0;
}

int32_t
ra_fchown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  ra_local_t *local;
  ra_file_t *file;

  local = frame->local;
  file = local->file;

  if ((op_ret == 0) && (file->stbuf.st_mtime != buf->st_mtime))
    flush_region (frame, file, 0, file->pages.prev->offset + 1);

  frame->local = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, buf);

  if (file)
    ra_file_unref (file);

  free (local);
  return 0;
}

int32_t
ra_fchown (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   uid_t uid,
	   gid_t gid)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_local_t *local;
  ra_file_t *file = NULL;

  if (file_data) {
    file = data_to_ptr (file_data);
  }

  local = calloc (1, sizeof (*local));

  if (file)
    local->file = ra_file_ref (file);

  frame->local = local;

  STACK_WIND (frame,
	      ra_fchown_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->fchown,
	      fd,
	      uid,
	      gid);
  return 0;
}

int32_t
ra_ftruncate (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              off_t offset)
{
  data_t *file_data = dict_get (fd->ctx, this->name);
  ra_file_t *file = NULL;
  ra_local_t *local = calloc (1, sizeof (*local));
  
  if (file_data) {
    file = data_to_ptr (file_data);
    flush_region (frame, file, 0, file->pages.prev->offset + 1);
  }

  local->file = ra_file_ref (file);
  frame->local = local;
  STACK_WIND (frame,
              ra_ftruncate_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->ftruncate,
              fd,
              offset);
  return 0;
}

int32_t 
init (xlator_t *this)
{
  ra_conf_t *conf;
  dict_t *options = this->options;

  if (!this->children || this->children->next) {
    gf_log (this->name,  GF_LOG_ERROR,
	    "FATAL: read-ahead not configured with exactly one child");
    return -1;
  }

  conf = (void *) calloc (1, sizeof (*conf));
  conf->page_size = 256 * 1024;
  conf->page_count = 2;

  if (dict_get (options, "page-size")) {
    conf->page_size = gf_str_to_long_long (data_to_str (dict_get (options,
								  "page-size")));
    gf_log (this->name, GF_LOG_DEBUG, "Using conf->page_size = 0x%x",
	    conf->page_size);
  }

  if (dict_get (options, "page-count")) {
    conf->page_count = gf_str_to_long_long (data_to_str (dict_get (options,
								   "page-count")));
    gf_log (this->name, GF_LOG_DEBUG, "Using conf->page_count = 0x%x",
	    conf->page_count);
  }

  if (dict_get (options, "force-atime-update")) {
    char *force_atime_update_str = data_to_str (dict_get (options,
							  "force-atime-update"));
    if ((!strcasecmp (force_atime_update_str, "on")) ||
	(!strcasecmp (force_atime_update_str, "yes"))) {
      conf->force_atime_update = 1;
      gf_log (this->name, GF_LOG_DEBUG, "Forcing atime updates on cache hit");
    }
  }

  conf->files.next = &conf->files;
  conf->files.prev = &conf->files;

  pthread_mutex_init (&conf->conf_lock, NULL);
  this->private = conf;
  return 0;
}

void
fini (xlator_t *this)
{
  ra_conf_t *conf = this->private;

  pthread_mutex_destroy (&conf->conf_lock);
  FREE (conf);

  this->private = NULL;
  return;
}

struct xlator_fops fops = {
  .open        = ra_open,
  .create      = ra_create,
  .readv       = ra_readv,
  .writev      = ra_writev,
  .flush       = ra_flush,
  .fsync       = ra_fsync,
  .close       = ra_close,
  .truncate    = ra_truncate,
  .ftruncate   = ra_ftruncate,
  .fstat       = ra_fstat,
  .fchown      = ra_fchown,
};

struct xlator_mops mops = {
};
