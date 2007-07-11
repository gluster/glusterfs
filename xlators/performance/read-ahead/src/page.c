/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
#include "read-ahead.h"
#include <assert.h>

static void
ra_file_unref_locked (ra_file_t *file);

ra_page_t *
ra_page_get (ra_file_t *file,
	     off_t offset)
{
  ra_conf_t *conf = file->conf;
  ra_page_t *page = file->pages.next;
  off_t rounded_offset = floor (offset, conf->page_size);

  while (page != &file->pages && page->offset < rounded_offset)
    page = page->next;

  if (page == &file->pages || page->offset != rounded_offset)
    page = NULL;

  return page;
}

ra_page_t *
ra_page_create (ra_file_t *file,
    off_t offset)
{
  ra_conf_t *conf = file->conf;
  ra_page_t *page = file->pages.next;
  off_t rounded_offset = floor (offset, conf->page_size);

  while (page != &file->pages && page->offset < rounded_offset)
    page = page->next;

  if (page == &file->pages || page->offset != rounded_offset) {
    ra_page_t *newpage = calloc (1, sizeof (*newpage));

    newpage->offset = rounded_offset;
    newpage->prev = page->prev;
    newpage->next = page;
    newpage->file = file;
    page->prev->next = newpage;
    page->prev = newpage;

    page = newpage;
  }

  return page;
}

void
ra_wait_on_page (ra_page_t *page,
     call_frame_t *frame)
{
  ra_waitq_t *waitq = calloc (1, sizeof (*waitq));
  ra_local_t *local = frame->local;

  waitq->data = frame;
  waitq->next = page->waitq;
  page->waitq = waitq;

  ra_local_lock (local);
  local->wait_count++;
  ra_local_unlock (local);
}

static int32_t
fault_cbk (call_frame_t *frame,
	   void *cookie,
	   xlator_t *this,
	   int32_t op_ret,
	   int32_t op_errno,
	   struct iovec *vector,
	   int32_t count,
	   struct stat *stbuf)
{
  ra_local_t *local = frame->local;
  off_t pending_offset = local->pending_offset;
  off_t pending_size = local->pending_size;
  ra_file_t *file = local->file;
  ra_conf_t *conf = file->conf;
  ra_page_t *page;
  off_t trav_offset;
  size_t payload_size;


  trav_offset = pending_offset;  
  payload_size = op_ret;

  ra_file_lock (file);

  if (op_ret >= 0)
    file->stbuf = *stbuf;

  if (op_ret < 0) {
    while (trav_offset < (pending_offset + pending_size)) {
      page = ra_page_get (file, pending_offset);
      if (page)
	ra_page_error (page, op_ret, op_errno);
      trav_offset += conf->page_size;
    }
  } else {
    page = ra_page_get (file, pending_offset);
    if (!page) {
      /* page was flushed */
      /* some serious bug ? */
  //  trav = ra_page_create (file, trav_offset);
      gf_log ("read-ahead",
        GF_LOG_DEBUG,
        "wasted copy: %lld[+%d] file=%p", 
        pending_offset,
        conf->page_size,
        file);
    } else {
      if (page->vector) {
	dict_unref (page->ref);
	free (page->vector);
      }
      page->vector = iov_dup (vector, count);
      page->count = count;
      page->ref = dict_ref (frame->root->rsp_refs);
      page->ready = 1;
      page->size = op_ret;

      if (page->waitq) {
	ra_page_wakeup (page);
      }
    }
  }

  ra_file_unlock (file);

  ra_file_unref (local->file);
  free (frame->local);
  frame->local = NULL;
  STACK_DESTROY (frame->root);
  return 0;
}

void
ra_page_fault (ra_file_t *file,
	       call_frame_t *frame,
	       off_t offset)
{
  ra_conf_t *conf = file->conf;
  call_frame_t *fault_frame = copy_frame (frame);
  ra_local_t *fault_local = calloc (1, sizeof (ra_local_t));
    
  fault_frame->local = fault_local;
  fault_local->pending_offset = offset;
  fault_local->pending_size = conf->page_size;
  fault_local->file = ra_file_ref (file);

  STACK_WIND (fault_frame,
	      fault_cbk,
	      FIRST_CHILD(fault_frame->this),
	      FIRST_CHILD(fault_frame->this)->fops->readv,
	      file->fd,
	      conf->page_size,
	      offset);
  return;
}

void
ra_frame_fill (ra_page_t *page,
	       call_frame_t *frame)
{
  ra_local_t *local = frame->local;
  ra_fill_t *fill = &local->fill;
  off_t src_offset = 0;
  off_t dst_offset = 0;
  ssize_t copy_size;

  if (local->op_ret != -1 && page->size) {
    if (local->offset > page->offset)
      src_offset = local->offset - page->offset;
    else
      dst_offset = page->offset - local->offset;

    copy_size = min (page->size - src_offset,
		     local->size - dst_offset);

    if (copy_size < 0) {
      /* if page contains fewer bytes and the required offset
	 is beyond the page size in the page */
      copy_size = src_offset = 0;
    }

    fill = fill->next;
    while (fill != &local->fill) {
      if (fill->offset > page->offset) {
	break;
      }
      fill = fill->next;
    }
    {
      ra_fill_t *new = calloc (1, sizeof (*new));
      new->offset = page->offset;
      new->size = copy_size;
      new->refs = dict_ref (page->ref);
      new->count = iov_subset (page->vector,
			       page->count,
			       src_offset,
			       src_offset+copy_size,
			       NULL);
      new->vector = calloc (new->count, sizeof (struct iovec));
      new->count = iov_subset (page->vector,
			       page->count,
			       src_offset,
			       src_offset+copy_size,
			       new->vector);
      new->next = fill;
      new->prev = new->next->prev;
      new->next->prev = new;
      new->prev->next = new;
    }
    local->op_ret += copy_size;
  }
}


static void
ra_frame_unwind (call_frame_t *frame)
{
  ra_local_t *local = frame->local;
  ra_fill_t *fill = local->fill.next;
  int32_t count = 0;
  struct iovec *vector;
  int32_t copied = 0;
  dict_t *refs = get_new_dict ();

  frame->local = NULL;

  while (fill != &local->fill) {
    count += fill->count;
    fill = fill->next;
  }

  vector = calloc (count, sizeof (*vector));

  fill = local->fill.next;

  while (fill != &local->fill) {
    ra_fill_t *next = fill->next;

    memcpy (((char *)vector) + copied,
	    fill->vector,
	    fill->count * sizeof (*vector));
    copied += (fill->count * sizeof (*vector));
    dict_copy (fill->refs, refs);

    fill->next->prev = fill->prev;
    fill->prev->next = fill->prev;

    dict_unref (fill->refs);
    free (fill->vector);
    free (fill);

    fill = next;
  }

  frame->root->rsp_refs = dict_ref (refs);

  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		vector,
		count,
		&local->file->stbuf);
  
  dict_unref (refs);
  ra_file_unref_locked (local->file);
  pthread_mutex_destroy (&local->local_lock);
  free (local);
  free (vector);

  return;
}

/*
 * ra_frame_return -
 * @frame:
 *
 */
void
ra_frame_return (call_frame_t *frame)
{
  ra_local_t *local = frame->local;
  int32_t wait_count;
  assert (local->wait_count > 0);

  ra_local_lock (local);
  wait_count = --local->wait_count;
  ra_local_unlock (local);

  if (!wait_count)
    ra_frame_unwind (frame);

  return;
}

/* 
 * ra_page_wakeup -
 * @page:
 *
 */
void
ra_page_wakeup (ra_page_t *page)
{
  ra_waitq_t *waitq, *trav;
  call_frame_t *frame;

  waitq = page->waitq;
  page->waitq = NULL;

  trav = waitq;

  for (trav = waitq; trav; trav = trav->next) {
    frame = trav->data; /* was: frame = waitq->data :O */
    ra_frame_fill (page, frame);
    ra_frame_return (frame);
  }

  for (trav = waitq; trav;) {
    ra_waitq_t *next = trav->next;
    free (trav);
    trav = next;
  }
}

/*
 * ra_page_purge -
 * @page:
 *
 */
void
ra_page_purge (ra_page_t *page)
{
  page->prev->next = page->next;
  page->next->prev = page->prev;

  if (page->ref) {
    dict_unref (page->ref);
  }
  free (page->vector);
  free (page);
}

/*
 * ra_page_error -
 * @page:
 * @op_ret:
 * @op_errno:
 *
 */
void
ra_page_error (ra_page_t *page,
	       int32_t op_ret,
	       int32_t op_errno)
{
  ra_waitq_t *waitq, *trav;
  call_frame_t *frame;

  waitq = page->waitq;
  page->waitq = NULL;

  for (trav = waitq; trav; trav = trav->next) {
    ra_local_t *local;

    frame = trav->data;
    local = frame->local;
    if (local->op_ret != -1) {
      local->op_ret = op_ret;
      local->op_errno = op_errno;
    }
    ra_frame_return (frame);
  }

  for (trav = waitq; trav;) {
    ra_waitq_t *next = trav->next;
    free (trav);
    trav = next;
  }
  ra_page_purge (page);
}

/*
 * ra_file_ref - ra file ref
 * @file:
 *
 */
ra_file_t *
ra_file_ref (ra_file_t *file)
{
  ra_file_lock (file);
  file->refcount++;
  ra_file_unlock (file);
  return file;
}

/* 
 * ra_file_destroy -
 * @file:
 *
 */
static void
ra_file_destroy (ra_file_t *file)
{
  ra_conf_t *conf = file->conf;
  ra_page_t *trav;

  ra_conf_lock (conf);
  file->prev->next = file->next;
  file->next->prev = file->prev;
  ra_conf_unlock (conf);

  trav = file->pages.next;
  while (trav != &file->pages) {
    ra_page_error (trav, -1, EINVAL);
    trav = file->pages.next;
  }

  pthread_mutex_destroy (&file->file_lock);
  free (file);
}

/*
 * ra_file_unref_locked -
 * @file:
 *
 */
static void
ra_file_unref_locked (ra_file_t *file)
{
  int32_t refcount;

  refcount = --file->refcount;

  if (refcount)
    return;

  ra_file_destroy (file);
}

/*
 * ra_file_unref - unref a file
 * @file:
 *
 */
void
ra_file_unref (ra_file_t *file)
{
  int32_t refcount;

  ra_file_lock (file);
  refcount = --file->refcount;
  ra_file_unlock (file);

  if (refcount)
    return;

  ra_file_destroy (file);
}
