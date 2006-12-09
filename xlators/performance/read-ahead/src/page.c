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
#include "read-ahead.h"
#include <assert.h>

ra_page_t *
ra_get_page (ra_file_t *file,
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
ra_create_page (ra_file_t *file,
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

  /*  if (page->waitq) {
    gf_log ("read-ahead",
	    GF_LOG_DEBUG,
	    "About to catch: %p", page->waitq);
	    }*/
  waitq->data = frame;
  waitq->next = page->waitq;
  page->waitq = waitq;
  local->wait_count++;
}

void
ra_fill_frame (ra_page_t *page,
	       call_frame_t *frame)
{
  ra_local_t *local = frame->local;
  off_t src_offset = 0;
  off_t dst_offset = 0;
  size_t copy_size;

  if (local->op_ret != -1 && page->size) {
    if (local->offset > page->offset)
      src_offset = local->offset - page->offset;
    else
      dst_offset = page->offset - local->offset;
    copy_size = min (page->size - src_offset,
		     local->size - dst_offset);

    /*    
	  I'm surprised how i manged this without drinking

	  if (local->size <= (page->size - src_offset)) { 
    */
    if (local->offset >= page->offset &&
	((local->offset + local->size) <= (page->offset + page->size))) {
      if (!local->is_static)
	free (local->ptr);
      local->ptr = &page->ptr[src_offset];
      local->is_static = 1;
    } else {
      memcpy (&local->ptr[dst_offset],
	      &page->ptr[src_offset],
	      copy_size);
    }
    local->op_ret += copy_size;
  }
}

void
ra_frame_return (call_frame_t *frame)
{
  ra_local_t *local = frame->local;

  assert (local->wait_count > 0);

  local->wait_count--;
  if (!local->wait_count) {
    frame->local = NULL;

    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->ptr);
    ra_file_unref (local->file);
    if (!local->is_static)
      free (local->ptr);
    free (local);
  }
}

void
ra_wakeup_page (ra_page_t *page)
{
  ra_waitq_t *waitq, *trav;
  call_frame_t *frame;

  waitq = page->waitq;
  page->waitq = NULL;

  trav = waitq;

  for (trav = waitq; trav; trav = trav->next) {
    frame = trav->data; /* was: frame = waitq->data :O */
    ra_fill_frame (page, frame);
    ra_frame_return (frame);
  }

  for (trav = waitq; trav;) {
    ra_waitq_t *next = trav->next;
    free (trav);
    trav = next;
  }
}

void
ra_purge_page (ra_page_t *page)
{
  page->prev->next = page->next;
  page->next->prev = page->prev;

  if (page->ref) {
    dict_unref (page->ref);
  }
  free (page);
}

void
ra_flush_page (ra_page_t *page)
{
  ra_waitq_t *waitq, *trav;
  call_frame_t *frame;

  waitq = page->waitq;
  page->waitq = NULL;

  for (trav = waitq; trav; trav = trav->next) {
    frame = trav->data;
    ra_frame_return (frame);
  }

  for (trav = waitq; trav;) {
    ra_waitq_t *next = trav->next;
    free (trav);
    trav = next;
  }
  ra_purge_page (page);
}

void
ra_error_page (ra_page_t *page,
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
  ra_purge_page (page);
}

ra_file_t *
ra_file_ref (ra_file_t *file)
{
  file->refcount++;
  return file;
}

void
ra_file_unref (ra_file_t *file)
{
  ra_page_t *trav;
  if (--file->refcount)
    return;

  file->prev->next = file->next;
  file->next->prev = file->prev;

  trav = file->pages.next;
  while (trav != &file->pages) {
    ra_error_page (trav, -1, EINVAL);
    trav = file->pages.next;
  }
  if (file->filename)
    free (file->filename);
  free (file);
}
