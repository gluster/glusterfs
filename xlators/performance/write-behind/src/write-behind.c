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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"


struct wb_conf;
struct wb_page;
struct wb_file;

struct wb_conf {
  size_t aggregate_size;
};

struct wb_page {
  struct wb_page *next;
  struct wb_page *prev;
  struct wb_file *file;
  off_t offset;
  struct iovec *vector;
  int32_t count;
  dict_t *refs;
};

struct wb_file {
  off_t offset;
  size_t size;
  int32_t refcount;
  int32_t op_ret;
  int32_t op_errno;
  struct wb_page pages;
  dict_t *file_ctx;
  pthread_mutex_t lock;
};

typedef struct wb_conf wb_conf_t;
typedef struct wb_page wb_page_t;
typedef struct wb_file wb_file_t;

static wb_file_t *
wb_file_ref (wb_file_t *file)
{
  pthread_mutex_lock (&file->lock);
  file->refcount++;
  pthread_mutex_unlock (&file->lock);
  return file;
}

static void
wb_file_unref (wb_file_t *file)
{
  int32_t refcount;

  pthread_mutex_lock (&file->lock);
  refcount = --file->refcount;
  pthread_mutex_unlock (&file->lock);

  if (!refcount) {
    wb_page_t *page = file->pages.next;

    while (page != &file->pages) {
      wb_page_t *next = page->next;

      page->prev->next = page->next;
      page->next->prev = page->prev;

      if (page->vector)
	free (page->vector);
      free (page);

      page = next;
    }

    pthread_mutex_destroy (&file->lock);
    free (file);
  }
}

static int32_t
wb_sync_cbk (call_frame_t *frame,
	     call_frame_t *prev_frame,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno)
{
  wb_file_t *file = frame->local;

  if (op_ret == -1) {
    file->op_ret = op_ret;
    file->op_errno = op_errno;
  }

  frame->local = NULL;

  wb_file_unref (file);

  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
wb_sync (call_frame_t *frame,
	 wb_file_t *file)
{
  size_t total_count = 0;
  size_t copied = 0;
  wb_page_t *page = file->pages.next;
  struct iovec *vector;
  call_frame_t *wb_frame;
  off_t offset;
  dict_t *refs;

  while (page != &file->pages) {
    total_count += page->count;
    page = page->next;
  }

  if (!total_count)
    return 0;

  vector = malloc (VECTORSIZE (total_count));

  page = file->pages.next;
  offset = file->pages.next->offset;

  refs = get_new_dict ();
  refs->lock = calloc (1, sizeof (pthread_mutex_t));
  pthread_mutex_init (refs->lock, NULL);

  while (page != &file->pages) {
    wb_page_t *next = page->next;
    size_t bytecount = VECTORSIZE (page->count);

    memcpy (((char *)vector)+copied,
	    page->vector,
	    bytecount);
    copied += bytecount;

    page->prev->next = page->next;
    page->next->prev = page->prev;

    dict_copy (page->refs, refs);
    dict_unref (page->refs);
    free (page->vector);
    free (page);

    page = next;
  }

  wb_frame = copy_frame (frame);
  wb_frame->local = wb_file_ref (file);
  wb_frame->root->req_refs = dict_ref (refs);

  STACK_WIND (wb_frame,
	      wb_sync_cbk,
	      FIRST_CHILD(wb_frame->this),
	      FIRST_CHILD(wb_frame->this)->fops->writev,
	      file->file_ctx,
	      vector,
	      total_count,
	      offset);

  dict_unref (refs);

  file->offset = 0;
  file->size = 0;

  free (vector);
  return 0;
}

static int32_t
wb_open_cbk (call_frame_t *frame,
	     call_frame_t *prev_frame,
	     xlator_t *this,
	     int32_t op_ret,
	     int32_t op_errno,
	     dict_t *file_ctx,
	     struct stat *buf)
{
  if (op_ret != -1) {
    wb_file_t *file = calloc (1, sizeof (*file));

    file->pages.next = &file->pages;
    file->pages.prev = &file->pages;
    file->file_ctx = file_ctx;

    dict_set (file_ctx,
	      this->name,
	      int_to_data ((long) ((void *) file)));
    pthread_mutex_init (&file->lock, NULL);
    wb_file_ref (file);
  }
  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, buf);
  return 0;
}

static int32_t
wb_open (call_frame_t *frame,
	 xlator_t *this,
	 const char *path,
	 int32_t flags,
	 mode_t mode)
{
  STACK_WIND (frame,
	      wb_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      path,
	      flags,
	      mode);
  return 0;
}

static int32_t
wb_create (call_frame_t *frame,
	   xlator_t *this,
	   const char *path,
	   mode_t mode)
{
  STACK_WIND (frame,
	      wb_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      path,
	      mode);
  return 0;
}

static int32_t 
wb_writev (call_frame_t *frame,
	   xlator_t *this,
	   dict_t *file_ctx,
	   struct iovec *vector,
	   int32_t count,
	   off_t offset)
{
  wb_file_t *file;
  wb_conf_t *conf = this->private;
  call_frame_t *wb_frame;
  dict_t *ref = NULL;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));

  if (offset != file->offset)
    /* detect lseek() */
    wb_sync (frame, file);

  if (file->op_ret == -1) {
    /* delayed error delivery */
    STACK_UNWIND (frame, -1, file->op_errno);
    file->op_ret = 0;
    return 0;
  }

  wb_frame = copy_frame (frame);
  ref = dict_ref (frame->root->req_refs);
  STACK_UNWIND (frame, iov_length (vector, count), 0); /* liar! liar! :O */
  file->offset = (offset + iov_length (vector, count));

  {
    wb_page_t *page = calloc (1, sizeof (*page));

    page->vector = iov_dup (vector, count);
    page->count = count;
    page->offset = offset;
    page->refs = ref;

    page->next = &file->pages;
    page->prev = file->pages.prev;
    page->next->prev = page;
    page->prev->next = page;

    file->size += iov_length (vector, count);
  }

  if (file->size >= conf->aggregate_size)
    wb_sync (wb_frame, file);

  STACK_DESTROY (wb_frame->root);
  return 0;
}


static int32_t
wb_readv_cbk (call_frame_t *frame,
	      call_frame_t *prev_frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct iovec *vector,
	      int32_t count)
{
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t
wb_readv (call_frame_t *frame,
	  xlator_t *this,
	  dict_t *file_ctx,
	  size_t size,
	  off_t offset)
{
  wb_file_t *file;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));

  wb_sync (frame, file);

  STACK_WIND (frame,
	      wb_readv_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->readv,
	      file_ctx,
	      size,
	      offset);

  return 0;
}

static int32_t
wb_ffr_cbk (call_frame_t *frame,
	    call_frame_t *prev_frame,
	    xlator_t *this,
	    int32_t op_ret,
	    int32_t op_errno)
{
  wb_file_t *file = frame->local;

  if (file->op_ret == -1) {
    op_ret = file->op_ret;
    op_errno = file->op_errno;

    file->op_ret = 0;
  }

  frame->local = NULL;
  wb_file_unref (file);
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
wb_flush (call_frame_t *frame,
	  xlator_t *this,
	  dict_t *file_ctx)
{
  wb_file_t *file;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  wb_sync (frame, file);

  frame->local = wb_file_ref (file);

  STACK_WIND (frame,
	      wb_ffr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->flush,
	      file_ctx);
  return 0;
}

static int32_t
wb_fsync (call_frame_t *frame,
	  xlator_t *this,
	  dict_t *file_ctx,
	  int32_t datasync)
{
  wb_file_t *file;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  wb_sync (frame, file);

  frame->local = wb_file_ref (file);

  STACK_WIND (frame,
	      wb_ffr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fsync,
	      file_ctx,
	      datasync);
  return 0;
}

static int32_t
wb_release (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *file_ctx)
{
  wb_file_t *file;

  file = (void *) ((long) data_to_int (dict_get (file_ctx,
						 this->name)));
  wb_sync (frame, file);

  frame->local = wb_file_ref (file);

  dict_del (file_ctx, this->name);
  wb_file_unref (file);


  STACK_WIND (frame,
	      wb_ffr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->release,
	      file_ctx);
  return 0;
}

int32_t 
init (struct xlator *this)
{
  dict_t *options = this->options;
  wb_conf_t *conf;

  if (!this->children || this->children->next) {
    gf_log ("write-back",
	    GF_LOG_ERROR,
	    "FATAL: write-back (%s) not configured with exactly one child",
	    this->name);
    return -1;
  }

  conf = calloc (1, sizeof (*conf));

  conf->aggregate_size = 131072;

  if (dict_get (options, "aggregate-size")) {
    conf->aggregate_size = data_to_int (dict_get (options,
						  "aggregate-size"));
  }
  gf_log ("write-back",
	  GF_LOG_DEBUG,
	  "using aggregate-size = %d", conf->aggregate_size);

  this->private = conf;
  return 0;
}

void
fini (struct xlator *this)
{
  return;
}

struct xlator_fops fops = {
  .writev      = wb_writev,
  .open        = wb_open,
  .create      = wb_create,
  .readv       = wb_readv,
  .flush       = wb_flush,
  .fsync       = wb_fsync,
  .release     = wb_release
};

struct xlator_mops mops = {
};
