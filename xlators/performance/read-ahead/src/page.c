/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "read-ahead.h"
#include <assert.h>

ra_page_t *
ra_page_get (ra_file_t *file, off_t offset)
{
	ra_page_t *page = NULL;
	off_t     rounded_offset = 0;

	page = file->pages.next;
	rounded_offset = floor (offset, file->page_size);

	while (page != &file->pages && page->offset < rounded_offset)
		page = page->next;

	if (page == &file->pages || page->offset != rounded_offset)
		page = NULL;

	return page;
}


ra_page_t *
ra_page_create (ra_file_t *file, off_t offset)
{
	ra_page_t  *page      = NULL;
	off_t      rounded_offset = 0;
	ra_page_t  *newpage   = NULL;

	page           = file->pages.next;
	rounded_offset = floor (offset, file->page_size);

	while (page != &file->pages && page->offset < rounded_offset)
		page = page->next;

	if (page == &file->pages || page->offset != rounded_offset) {
		newpage = CALLOC (1, sizeof (*newpage));
		if (!newpage)
			return NULL;

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
ra_wait_on_page (ra_page_t *page, call_frame_t *frame)
{
	ra_waitq_t *waitq = NULL;
	ra_local_t *local = NULL;

	local = frame->local;
	waitq = CALLOC (1, sizeof (*waitq));
	if (!waitq) {
		gf_log (frame->this->name, GF_LOG_ERROR,
			"out of memory :(");
		return;
	}

	waitq->data = frame;
	waitq->next = page->waitq;
	page->waitq = waitq;

	ra_local_lock (local);
	{
		local->wait_count++;
	}
	ra_local_unlock (local);
}


void
ra_waitq_return (ra_waitq_t *waitq)
{
	ra_waitq_t   *trav = NULL;
	ra_waitq_t   *next = NULL;
	call_frame_t *frame = NULL;

	for (trav = waitq; trav; trav = next) {
		next = trav->next;

		frame = trav->data;
		ra_frame_return (frame);
		free (trav);
	}
}


int
ra_fault_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	      int32_t op_ret, int32_t op_errno, struct iovec *vector,
	      int32_t count, struct stat *stbuf, struct iobref *iobref)
{
	ra_local_t   *local = NULL;
	off_t        pending_offset = 0;
	ra_file_t    *file = NULL;
	ra_page_t    *page = NULL;
	off_t        trav_offset = 0;
	size_t       payload_size = 0;
	ra_waitq_t   *waitq = NULL;
	fd_t         *fd = NULL;
	int          ret = 0;
	uint64_t     tmp_file = 0;

	local = frame->local;
	fd  = local->fd;

	ret = fd_ctx_get (fd, this, &tmp_file);

	file = (ra_file_t *)(long)tmp_file;
	pending_offset = local->pending_offset;
	trav_offset    = pending_offset;  
	payload_size   = op_ret;

	ra_file_lock (file);
	{
		if (op_ret >= 0)
			file->stbuf = *stbuf;

		if (op_ret < 0) {
			page = ra_page_get (file, pending_offset);
			if (page)
				waitq = ra_page_error (page, op_ret, op_errno);
			goto unlock;
		}

		page = ra_page_get (file, pending_offset);
		if (!page) {
			gf_log (this->name, GF_LOG_DEBUG,
				"wasted copy: %"PRId64"[+%"PRId64"] file=%p", 
				pending_offset, file->page_size, file);
			goto unlock;
		}

		if (page->vector) {
			iobref_unref (page->iobref);
			free (page->vector);
		}

		page->vector = iov_dup (vector, count);
		page->count = count;
		page->iobref = iobref_ref (iobref);
		page->ready = 1;

		page->size = iov_length (vector, count);

		waitq = ra_page_wakeup (page);
	}
unlock:
	ra_file_unlock (file);

	ra_waitq_return (waitq);

	fd_unref (local->fd);

	free (frame->local);
	frame->local = NULL;

	STACK_DESTROY (frame->root);
	return 0;
}


void
ra_page_fault (ra_file_t *file, call_frame_t *frame, off_t offset)
{
	call_frame_t *fault_frame = NULL;
	ra_local_t   *fault_local = NULL;
    
	fault_frame = copy_frame (frame);
	fault_local = CALLOC (1, sizeof (ra_local_t));

	fault_frame->local = fault_local;
	fault_local->pending_offset = offset;
	fault_local->pending_size = file->page_size;

	fault_local->fd = fd_ref (file->fd);

	STACK_WIND (fault_frame, ra_fault_cbk,
		    FIRST_CHILD (fault_frame->this),
		    FIRST_CHILD (fault_frame->this)->fops->readv,
		    file->fd, file->page_size, offset);
	return;
}

void
ra_frame_fill (ra_page_t *page, call_frame_t *frame)
{
	ra_local_t *local = NULL;
	ra_fill_t  *fill = NULL;
	off_t      src_offset = 0;
	off_t      dst_offset = 0;
	ssize_t    copy_size = 0;
	ra_fill_t  *new = NULL;

	local = frame->local;
	fill  = &local->fill;

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

		new = CALLOC (1, sizeof (*new));

		new->offset = page->offset;
		new->size = copy_size;
		new->iobref = iobref_ref (page->iobref);
		new->count = iov_subset (page->vector, page->count,
					 src_offset, src_offset+copy_size,
					 NULL);
		new->vector = CALLOC (new->count, sizeof (struct iovec));

		new->count = iov_subset (page->vector, page->count,
					 src_offset, src_offset+copy_size,
					 new->vector);

		new->next = fill;
		new->prev = new->next->prev;
		new->next->prev = new;
		new->prev->next = new;

		local->op_ret += copy_size;
	}
}


void
ra_frame_unwind (call_frame_t *frame)
{
	ra_local_t    *local = NULL;
	ra_fill_t     *fill = NULL;
	int32_t       count = 0;
	struct iovec  *vector;
	int32_t       copied = 0;
        struct iobref *iobref = NULL;
	ra_fill_t     *next = NULL;
	fd_t          *fd = NULL;
	ra_file_t     *file = NULL;
	int           ret = 0;
	uint64_t      tmp_file = 0;

	local = frame->local;
	fill  = local->fill.next;

	iobref  = iobref_new ();

	frame->local = NULL;

	while (fill != &local->fill) {
		count += fill->count;
		fill = fill->next;
	}

	vector = CALLOC (count, sizeof (*vector));

	fill = local->fill.next;

	while (fill != &local->fill) {
		next = fill->next;

		memcpy (((char *)vector) + copied, fill->vector,
			fill->count * sizeof (*vector));

		copied += (fill->count * sizeof (*vector));
		iobref_merge (iobref, fill->iobref);

		fill->next->prev = fill->prev;
		fill->prev->next = fill->prev;

		iobref_unref (fill->iobref);
		free (fill->vector);
		free (fill);

		fill = next;
	}

	fd = local->fd;
	ret = fd_ctx_get (fd, frame->this, &tmp_file);
	file = (ra_file_t *)(long)tmp_file;
	
	STACK_UNWIND (frame, local->op_ret, local->op_errno,
		      vector, count, &file->stbuf, iobref);
  
	iobref_unref (iobref);
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
	ra_local_t  *local = NULL;
	int32_t     wait_count = 0;

	local = frame->local;
	assert (local->wait_count > 0);

	ra_local_lock (local);
	{
		wait_count = --local->wait_count;
	}
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
ra_waitq_t *
ra_page_wakeup (ra_page_t *page)
{
	ra_waitq_t *waitq = NULL, *trav = NULL;
	call_frame_t *frame;

	waitq = page->waitq;
	page->waitq = NULL;

	trav = waitq;
	for (trav = waitq; trav; trav = trav->next) {
		frame = trav->data;
		ra_frame_fill (page, frame);
	}

	return waitq;
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

	if (page->iobref) {
		iobref_unref (page->iobref);
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
ra_waitq_t *
ra_page_error (ra_page_t *page, int32_t op_ret, int32_t op_errno)
{

	ra_waitq_t   *waitq = NULL;
	ra_waitq_t   *trav = NULL;
	call_frame_t *frame = NULL;
	ra_local_t   *local = NULL;

	waitq = page->waitq;
	page->waitq = NULL;

	trav = waitq;
	for (trav = waitq; trav; trav = trav->next) {
		frame = trav->data;

		local = frame->local;
		if (local->op_ret != -1) {
			local->op_ret   = op_ret;
			local->op_errno = op_errno;
		}
	}

	ra_page_purge (page);

	return waitq;
}

/* 
 * ra_file_destroy -
 * @file:
 *
 */
void
ra_file_destroy (ra_file_t *file)
{
	ra_conf_t *conf = NULL;
	ra_page_t *trav = NULL;

	conf = file->conf;

	ra_conf_lock (conf);
	{
		file->prev->next = file->next;
		file->next->prev = file->prev;
	}
	ra_conf_unlock (conf);

	trav = file->pages.next;
	while (trav != &file->pages) {
		ra_page_error (trav, -1, EINVAL);
		trav = file->pages.next;
	}

	pthread_mutex_destroy (&file->file_lock);
	free (file);
}
