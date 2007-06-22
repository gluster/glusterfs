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

/* 
   TODO:
   - handle O_DIRECT
   - maintain offset, flush on lseek
   - ensure efficient memory managment in case of random seek
*/

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "read-ahead.h"
#include <assert.h>
#include <sys/time.h>


/*
 * str_to_ptr - convert a string to pointer
 * @string: string
 *
 */
void *
str_to_ptr (char *string)
{
  void *ptr = (void *)strtoul (string, NULL, 16);
  return ptr;
}


/*
 * ptr_to_str - convert a pointer to string
 * @ptr: pointer
 *
 */
char *
ptr_to_str (void *ptr)
{
  char *str;
  asprintf (&str, "%p", ptr);
  return str;
}


static void
read_ahead (call_frame_t *frame,
            ra_file_t *file);

static int32_t
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
    char *file_str = NULL;

    file = ra_file_ref (file);
    file_str = ptr_to_str (file); 
    file->fd = fd;
    dict_set (fd->ctx,
              this->name,
              data_from_dynstr (file_str));

    /* If mandatory locking has been enabled on this file,
       we disable caching on it */

    if ((fd->inode->buf.st_mode & S_ISGID) && !(fd->inode->buf.st_mode & S_IXGRP))
      file->disabled = 1;

    /* If O_DIRECT open, we disable caching on it */

    if ((local->flags & O_DIRECT) || (local->flags & O_WRONLY))
      file->disabled = 1;

    file->offset = (unsigned long long) -1;
    file->size = fd->inode->buf.st_size;
    file->conf = conf;
    file->pages.next = &file->pages;
    file->pages.prev = &file->pages;
    file->pages.offset = (unsigned long) -1;
    file->pages.file = file;

    file->next = conf->files.next;
    conf->files.next = file;
    file->next->prev = file;
    file->prev = &conf->files;

    pthread_mutex_init (&file->file_lock, NULL);

    if (!file->disabled)
      read_ahead (frame, file);
  }

  free ((char *)local->file_loc.path);
  free (local);
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

static int32_t
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
    char *file_str = NULL;
    file = ra_file_ref (file);
    file_str = ptr_to_str (file);

    file->fd = fd;
    dict_set (fd->ctx,
              this->name,
              data_from_dynstr (file_str));

    /* If mandatory locking has been enabled on this file,
       we disable caching on it */

    if ((fd->inode->buf.st_mode & S_ISGID) && !(fd->inode->buf.st_mode & S_IXGRP))
      file->disabled = 1;

    /* If O_DIRECT open, we disable caching on it */

    if ((local->flags & O_DIRECT) || (local->flags & O_WRONLY))
      file->disabled = 1;

    file->offset = (unsigned long long) -1;
    file->size = fd->inode->buf.st_size;
    file->conf = conf;
    file->pages.next = &file->pages;
    file->pages.prev = &file->pages;
    file->pages.offset = (unsigned long) -1;
    file->pages.file = file;

    file->next = conf->files.next;
    conf->files.next = file;
    file->next->prev = file;
    file->prev = &conf->files;

    pthread_mutex_init (&file->file_lock, NULL);

    if (!file->disabled)
      read_ahead (frame, file);
  }

  free ((char *)local->file_loc.path);
  free (local);
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);

  return 0;
}

static int32_t
ra_open (call_frame_t *frame,
         xlator_t *this,
         loc_t *loc,
         int32_t flags)
{
  ra_local_t *local = calloc (1, sizeof (*local));

  //FIXME is this required? 
  /* FIXED - not required.. :O */
  local->file_loc.inode = loc->inode;
  local->file_loc.path = strdup (loc->path);

  local->flags = flags;

  frame->local = local;

  STACK_WIND (frame,
              ra_open_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->open,
              loc,
              flags);

  return 0;
}

static int32_t
ra_create (call_frame_t *frame,
           xlator_t *this,
           const char *pathname,
           int32_t flags,
           mode_t mode)
{
  ra_local_t *local = calloc (1, sizeof (*local));

  //FIXME is this required?
  /* FIXED - everything is required */
  local->file_loc.inode = NULL;
  local->file_loc.path = strdup (pathname);

  local->mode = mode;
  local->flags = 0;
  frame->local = local;

  STACK_WIND (frame,
              ra_create_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->create,
              pathname,
              flags,
              mode);

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
      trav->prev->next = trav->next;
      trav->next->prev = trav->prev;

      if (!trav->ready) {
	gf_log ("read-ahead",
		GF_LOG_DEBUG,
		"killing featus, file=%p, offset=%lld, de=%lld, a=%lld",
		file,
		trav->offset,
		offset,
		size);
      }
      ra_page_purge (trav);
    }
    trav = next;
  }

  ra_file_unlock (file);
}

static int32_t
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

static int32_t
ra_close (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd)
{
  char *file_str = NULL;
  ra_file_t *file;

  file_str = data_to_str (dict_get (fd->ctx, this->name));
  file = str_to_ptr (file_str);

  flush_region (frame, file, 0, file->pages.prev->offset+1);
  dict_del (fd->ctx, this->name);

  file->fd = NULL;
  ra_file_unref (file);

  STACK_WIND (frame,
              ra_close_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->close,
              fd);
  return 0;
}


static void
read_ahead (call_frame_t *frame,
            ra_file_t *file)
{
  ra_conf_t *conf = file->conf;
  off_t ra_offset;
  size_t ra_size;
  off_t trav_offset;
  ra_page_t *trav = NULL;
  off_t cap = file->size;

  ra_size = conf->page_size * conf->page_count;
  ra_offset = floor (file->offset, conf->page_size);
  cap = file->size ? file->size : file->offset + ra_size;

  while (ra_offset < min (file->offset + ra_size, file->size)) {
    ra_file_lock (file);
    trav = ra_page_get (file, ra_offset);
    ra_file_unlock (file);
    if (!trav)
      break;
    ra_offset += conf->page_size;
  }

  if (trav)
    /* comfortable enough */
    return;

  trav_offset = ra_offset;

  trav = file->pages.next;
  cap = file->size ? file->size : ra_offset + ra_size;
  while (trav_offset < min(ra_offset + ra_size, file->size)) {
    char fault = 0;
    ra_file_lock (file);
    trav = ra_page_get (file, trav_offset);
    if (!trav) {
      fault = 1;
      /*
      gf_log ("read-ahead",
        GF_LOG_DEBUG,
        "RA: file=%p %lld[+%lld]",
        file,
        trav_offset,
        conf->page_size);
      */
      trav = ra_page_create (file, trav_offset);
      trav->dirty = 1;
    }
    ra_file_unlock (file);

    if (fault)
      ra_page_fault (file, frame, trav_offset);
    trav_offset += conf->page_size;
  }
  return ;
}

static int32_t
ra_need_utime_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   struct iovec *vector,
                   int32_t count,
		   struct stat *stbuf)
{
  /*  ra_file_t *file = ((ra_local_t *)frame->local)->file;

  if (op_ret == -1) {
    file->op_ret = op_ret;
    file->op_errno = op_errno;
  }

  ((ra_local_t *)frame->local)->file = NULL;
  */
  //  ra_file_unref (file);
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
  char need_utime = 1;

  rounded_offset = floor (local->offset, conf->page_size);
  rounded_end = roof (local->offset + local->size, conf->page_size);

  trav_offset = rounded_offset;
  trav = file->pages.next;

  while (trav_offset < rounded_end) {
    char fault = 0;

    ra_file_lock (file);
    trav = ra_page_get (file, trav_offset);
    if (!trav) {
      /*
      gf_log ("read-ahead",
        GF_LOG_DEBUG,
        "MISS: file=%p %lld[+%lld]",
        file,
        trav_offset,
        conf->page_size);
      */
      trav = ra_page_create (file, trav_offset);
      fault = 1;
      need_utime = 0;
    } 

    if (trav->ready) {
      /*
      gf_log ("read-ahead",
        GF_LOG_DEBUG,
        "HIT: file=%p %lld[+%lld]",
        file,
        trav_offset,
        conf->page_size);
      */
      ra_frame_fill (trav, frame);
    } else {
      /*
      gf_log ("read-ahead",
        GF_LOG_DEBUG,
        "Partial hit: file=%p %lld[+%lld]",
        file,
        trav_offset,
        conf->page_size);
      */
      ra_wait_on_page (trav, frame);
    }
    ra_file_unlock (file);

    if (fault)
      ra_page_fault (file, frame, trav_offset);

    trav_offset += conf->page_size;
  }

  if (need_utime) {
  
    ra_frame = copy_frame (frame);
    /*    ((ra_local_t *)ra_frame->local)->file = ra_file_ref (file); */
    STACK_WIND (ra_frame, 
                ra_need_utime_cbk,
                FIRST_CHILD (frame->this), 
                FIRST_CHILD (frame->this)->fops->readv,
                file->fd,
                0,
                0);
  }

  return ;
}

static int32_t
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

static int32_t
ra_readv (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd,
          size_t size,
          off_t offset)
{
  /* TODO: do something about atime update on server */
  char *file_str = NULL;
  ra_file_t *file;
  ra_local_t *local;
  ra_conf_t *conf;

  file_str = data_to_str (dict_get (fd->ctx, this->name));
  file = str_to_ptr (file_str);

  if (file->disabled) {
    STACK_WIND (frame, ra_readv_disabled_cbk,
    FIRST_CHILD (frame->this), 
    FIRST_CHILD (frame->this)->fops->readv,
    file->fd, size, offset);
    return 0;
  }

  call_frame_t *ra_frame = copy_frame (frame);

  /*
  gf_log ("read-ahead",
    GF_LOG_DEBUG,
    "read: %lld[+%d] file=%p", offset, size, file);
  */
  conf = file->conf;

  local = (void *) calloc (1, sizeof (*local));
  local->offset = offset;
  local->size = size;
  local->file = ra_file_ref (file);
  local->wait_count = 1; /* for synchronous STACK_UNWIND from protocol
          in case of error */
  local->fill.next = &local->fill;
  local->fill.prev = &local->fill;
  pthread_mutex_init (&local->local_lock, NULL);

  frame->local = local;

  dispatch_requests (frame, file);
  file->offset = offset;

  flush_region (frame, file, 0, floor (offset, conf->page_size));

  ra_frame_return (frame);

  read_ahead (ra_frame, file);

  STACK_DESTROY (ra_frame->root);

  return 0;
}

static int32_t
ra_flush_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
ra_flush (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd)
{
  char *file_str = NULL;
  ra_file_t *file;

  file_str = data_to_str (dict_get (fd->ctx, this->name));
  file = str_to_ptr (file_str);

  flush_region (frame, file, 0, file->pages.prev->offset+1);

  STACK_WIND (frame,
              ra_flush_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->flush,
              fd);
  return 0;
}

static int32_t
ra_fsync (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd,
          int32_t datasync)
{
  char *file_str = NULL;
  ra_file_t *file;

  file_str = data_to_str (dict_get (fd->ctx, this->name));
  file = str_to_ptr (file_str);

  flush_region (frame, file, 0, file->pages.prev->offset+1);

  STACK_WIND (frame,
              ra_flush_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->fsync,
              fd,
              datasync);
  return 0;
}

static int32_t
ra_writev_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
	       struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
ra_writev (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           struct iovec *vector,
           int32_t count,
           off_t offset)
{
  char *file_str = NULL;
  ra_file_t *file;

  file_str = data_to_str (dict_get (fd->ctx, this->name));
  file = str_to_ptr (file_str);

  flush_region (frame, file, 0, file->pages.prev->offset+1);

  STACK_WIND (frame,
              ra_writev_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->writev,
              fd,
              vector,
              count,
              offset);

  return 0;
}

static int32_t 
ra_truncate_cbk (call_frame_t *frame,
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
ra_truncate (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             off_t offset)
{
  fd_t *iter_fd;

  list_for_each_entry (iter_fd, &(loc->inode->fds), inode_list) {
    char *iter_file_str = NULL;
    ra_file_t *iter_file;
    iter_file_str = data_to_str (dict_get (iter_fd->ctx, this->name));
    iter_file = str_to_ptr (iter_file_str);

    if (iter_file->pages.prev->offset > offset)
      flush_region (frame, iter_file, offset, iter_file->pages.prev->offset + 1);
  }

  STACK_WIND (frame,
              ra_truncate_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->truncate,
              loc,
              offset);
  return 0;
}

static int32_t
ra_ftruncate (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              off_t offset)
{
  char *file_str = NULL;
  ra_file_t *file;
  fd_t *iter_fd;
  
  file_str = data_to_str (dict_get (fd->ctx, this->name));
  file = str_to_ptr (file_str);
  list_for_each_entry (iter_fd, &(file->fd->inode->fds), inode_list) {
    char *iter_file_str = NULL;
    ra_file_t *iter_file = NULL;
    iter_file_str = data_to_str (dict_get (iter_fd->ctx, this->name));
    iter_file = str_to_ptr (iter_file_str);
    
    if (iter_file->pages.prev->offset > offset)
      flush_region (frame, iter_file, offset, iter_file->pages.prev->offset + 1);

  }

  STACK_WIND (frame,
              ra_truncate_cbk,
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
    gf_log ("read-ahead",
      GF_LOG_ERROR,
      "FATAL: read-ahead not configured with exactly one child");
    return -1;
  }

  conf = (void *) calloc (1, sizeof (*conf));
  conf->page_size = 1024 * 256;
  conf->page_count = 16;

  if (dict_get (options, "page-size")) {
    conf->page_size = data_to_int32 (dict_get (options,
               "page-size"));
    gf_log ("read-ahead",
      GF_LOG_DEBUG,
      "Using conf->page_size = 0x%x",
      conf->page_size);
  }

  if (dict_get (options, "page-count")) {
    conf->page_count = data_to_int32 (dict_get (options,
                "page-count"));
    gf_log ("read-ahead",
      GF_LOG_DEBUG,
      "Using conf->page_count = 0x%x",
      conf->page_count);
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
  free (conf);

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
};

struct xlator_mops mops = {
};
