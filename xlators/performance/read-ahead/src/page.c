/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "read-ahead.h"
#include <assert.h>
#include "read-ahead-messages.h"

ra_page_t *
ra_page_get (ra_file_t *file, off_t offset)
{
        ra_page_t *page           = NULL;
        off_t      rounded_offset = 0;

        GF_VALIDATE_OR_GOTO ("read-ahead", file, out);

        page = file->pages.next;
        rounded_offset = floor (offset, file->page_size);

        while (page != &file->pages && page->offset < rounded_offset)
                page = page->next;

        if (page == &file->pages || page->offset != rounded_offset)
                page = NULL;

out:
        return page;
}


ra_page_t *
ra_page_create (ra_file_t *file, off_t offset)
{
        ra_page_t  *page           = NULL;
        off_t       rounded_offset = 0;
        ra_page_t  *newpage        = NULL;

        GF_VALIDATE_OR_GOTO ("read-ahead", file, out);

        page           = file->pages.next;
        rounded_offset = floor (offset, file->page_size);

        while (page != &file->pages && page->offset < rounded_offset)
                page = page->next;

        if (page == &file->pages || page->offset != rounded_offset) {
                newpage = GF_CALLOC (1, sizeof (*newpage), gf_ra_mt_ra_page_t);
                if (!newpage) {
                        goto out;
                }

                newpage->offset = rounded_offset;
                newpage->prev = page->prev;
                newpage->next = page;
                newpage->file = file;
                page->prev->next = newpage;
                page->prev = newpage;

                page = newpage;
        }

out:
        return page;
}


void
ra_wait_on_page (ra_page_t *page, call_frame_t *frame)
{
        ra_waitq_t *waitq = NULL;
        ra_local_t *local = NULL;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, page, out);

        local = frame->local;

        waitq = GF_CALLOC (1, sizeof (*waitq), gf_ra_mt_ra_waitq_t);
        if (!waitq) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto out;
        }

        waitq->data = frame;
        waitq->next = page->waitq;
        page->waitq = waitq;

        ra_local_lock (local);
        {
                local->wait_count++;
        }
        ra_local_unlock (local);

out:
        return;
}


void
ra_waitq_return (ra_waitq_t *waitq)
{
        ra_waitq_t   *trav  = NULL;
        ra_waitq_t   *next  = NULL;
        call_frame_t *frame = NULL;

        for (trav = waitq; trav; trav = next) {
                next = trav->next;

                frame = trav->data;
                ra_frame_return (frame);
                GF_FREE (trav);
        }

        return;
}


int
ra_fault_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iovec *vector,
              int32_t count, struct iatt *stbuf, struct iobref *iobref,
              dict_t *xdata)
{
        ra_local_t   *local          = NULL;
        off_t         pending_offset = 0;
        ra_file_t    *file           = NULL;
        ra_page_t    *page           = NULL;
        ra_waitq_t   *waitq          = NULL;
        fd_t         *fd             = NULL;
        uint64_t      tmp_file       = 0;

        GF_ASSERT (frame);

        local = frame->local;
        fd  = local->fd;

        fd_ctx_get (fd, this, &tmp_file);

        file = (ra_file_t *)(long)tmp_file;
        pending_offset = local->pending_offset;

        if (file == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, EBADF,
                        READ_AHEAD_MSG_FD_CONTEXT_NOT_SET,
                        "read-ahead context not set in fd (%p)", fd);
                op_ret = -1;
                op_errno = EBADF;
                goto out;
        }

        ra_file_lock (file);
        {
                if (op_ret >= 0)
                        file->stbuf = *stbuf;

                page = ra_page_get (file, pending_offset);

                if (!page) {
                        gf_msg_trace (this->name, 0,
                                      "wasted copy: "
                                      "%"PRId64"[+%"PRId64"] file=%p",
                                      pending_offset, file->page_size, file);
                        goto unlock;
                }

                /*
                 * "Dirty" means that the request was a pure read-ahead; it's
                 * set for requests we issue ourselves, and cleared when user
                 * requests are issued or put on the waitq.  "Poisoned" means
                 * that we got a write while a read was still in flight, and we
                 * couldn't stop it so we marked it instead.  If it's both
                 * dirty and poisoned by the time we get here, we cancel its
                 * effect so that a subsequent user read doesn't get data that
                 * we know is stale (because we made it stale ourselves).  We
                 * can't use ESTALE because that has special significance.
                 * ECANCELED has no such special meaning, and is close to what
                 * we're trying to indicate.
                 */
                if (page->dirty && page->poisoned) {
                        op_ret = -1;
                        op_errno = ECANCELED;
                }

                if (op_ret < 0) {
                        waitq = ra_page_error (page, op_ret, op_errno);
                        goto unlock;
                }

                if (page->vector) {
                        iobref_unref (page->iobref);
                        GF_FREE (page->vector);
                }

                page->vector = iov_dup (vector, count);
                if (page->vector == NULL) {
                        waitq = ra_page_error (page, -1, ENOMEM);
                        goto unlock;
                }

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

        mem_put (frame->local);
        frame->local = NULL;

out:
        STACK_DESTROY (frame->root);
        return 0;
}


void
ra_page_fault (ra_file_t *file, call_frame_t *frame, off_t offset)
{
        call_frame_t *fault_frame = NULL;
        ra_local_t   *fault_local = NULL;
        ra_page_t    *page        = NULL;
        ra_waitq_t   *waitq       = NULL;
        int32_t       op_ret      = -1, op_errno = -1;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, file, out);

        fault_frame = copy_frame (frame);
        if (fault_frame == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        fault_local = mem_get0 (THIS->local_pool);
        if (fault_local == NULL) {
                STACK_DESTROY (fault_frame->root);
                op_ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        fault_frame->local = fault_local;
        fault_local->pending_offset = offset;
        fault_local->pending_size = file->page_size;

        fault_local->fd = fd_ref (file->fd);

        STACK_WIND (fault_frame, ra_fault_cbk,
                    FIRST_CHILD (fault_frame->this),
                    FIRST_CHILD (fault_frame->this)->fops->readv,
                    file->fd, file->page_size, offset, 0, NULL);

        return;

err:
        ra_file_lock (file);
        {
                page = ra_page_get (file, offset);
                if (page)
                        waitq = ra_page_error (page, op_ret,
                                               op_errno);
        }
        ra_file_unlock (file);

        if (waitq != NULL) {
                ra_waitq_return (waitq);
        }

out:
        return;
}


void
ra_frame_fill (ra_page_t *page, call_frame_t *frame)
{
        ra_local_t *local      = NULL;
        ra_fill_t  *fill       = NULL;
        off_t       src_offset = 0;
        off_t       dst_offset = 0;
        ssize_t     copy_size  = 0;
        ra_fill_t  *new        = NULL;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, page, out);

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

                new = GF_CALLOC (1, sizeof (*new), gf_ra_mt_ra_fill_t);
                if (new == NULL) {
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        goto out;
                }

                new->offset = page->offset;
                new->size = copy_size;
                new->iobref = iobref_ref (page->iobref);
                new->count = iov_subset (page->vector, page->count,
                                         src_offset, src_offset+copy_size,
                                         NULL);
                new->vector = GF_CALLOC (new->count, sizeof (struct iovec),
                                         gf_ra_mt_iovec);
                if (new->vector == NULL) {
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        GF_FREE (new);
                        goto out;
                }

                new->count = iov_subset (page->vector, page->count,
                                         src_offset, src_offset+copy_size,
                                         new->vector);

                new->next = fill;
                new->prev = new->next->prev;
                new->next->prev = new;
                new->prev->next = new;

                local->op_ret += copy_size;
        }

out:
        return;
}


void
ra_frame_unwind (call_frame_t *frame)
{
        ra_local_t    *local    = NULL;
        ra_fill_t     *fill     = NULL;
        int32_t        count    = 0;
        struct iovec  *vector   = NULL;
        int32_t        copied   = 0;
        struct iobref *iobref   = NULL;
        ra_fill_t     *next     = NULL;
        fd_t          *fd       = NULL;
        ra_file_t     *file     = NULL;
        uint64_t       tmp_file = 0;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);

        local = frame->local;
        fill  = local->fill.next;

        iobref  = iobref_new ();
        if (iobref == NULL) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
        }

        frame->local = NULL;

        while (fill != &local->fill) {
                count += fill->count;
                fill = fill->next;
        }

        vector = GF_CALLOC (count, sizeof (*vector), gf_ra_mt_iovec);
        if (vector == NULL) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                iobref_unref (iobref);
                iobref = NULL;
        }

        fill = local->fill.next;

        while (fill != &local->fill) {
                next = fill->next;

                if ((vector != NULL) && (iobref != NULL)) {
                        memcpy (((char *)vector) + copied, fill->vector,
                                fill->count * sizeof (*vector));

                        copied += (fill->count * sizeof (*vector));
                        if (iobref_merge (iobref, fill->iobref)) {
				local->op_ret = -1;
				local->op_errno = ENOMEM;
				iobref_unref (iobref);
				iobref = NULL;
			}
                }

                fill->next->prev = fill->prev;
                fill->prev->next = fill->prev;

                iobref_unref (fill->iobref);
                GF_FREE (fill->vector);
                GF_FREE (fill);

                fill = next;
        }

        fd = local->fd;
        fd_ctx_get (fd, frame->this, &tmp_file);
        file = (ra_file_t *)(long)tmp_file;

        STACK_UNWIND_STRICT (readv, frame, local->op_ret, local->op_errno,
                             vector, count, &file->stbuf, iobref, NULL);

        iobref_unref (iobref);
        pthread_mutex_destroy (&local->local_lock);
        mem_put (local);
        GF_FREE (vector);

out:
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
        ra_local_t  *local      = NULL;
        int32_t      wait_count = 0;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);

        local = frame->local;
        GF_ASSERT (local->wait_count > 0);

        ra_local_lock (local);
        {
                wait_count = --local->wait_count;
        }
        ra_local_unlock (local);

        if (!wait_count)
                ra_frame_unwind (frame);

out:
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
        ra_waitq_t   *waitq = NULL, *trav = NULL;
        call_frame_t *frame = NULL;

        GF_VALIDATE_OR_GOTO ("read-ahead", page, out);

        waitq = page->waitq;
        page->waitq = NULL;

        for (trav = waitq; trav; trav = trav->next) {
                frame = trav->data;
                ra_frame_fill (page, frame);
        }

        if (page->stale) {
                ra_page_purge (page);
        }
out:
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
        GF_VALIDATE_OR_GOTO ("read-ahead", page, out);

        page->prev->next = page->next;
        page->next->prev = page->prev;

        if (page->iobref) {
                iobref_unref (page->iobref);
        }

        GF_FREE (page->vector);
        GF_FREE (page);

out:
        return;
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
        ra_waitq_t   *trav  = NULL;
        call_frame_t *frame = NULL;
        ra_local_t   *local = NULL;

        GF_VALIDATE_OR_GOTO ("read-ahead", page, out);

        waitq = page->waitq;
        page->waitq = NULL;

        for (trav = waitq; trav; trav = trav->next) {
                frame = trav->data;

                local = frame->local;
                if (local->op_ret != -1) {
                        local->op_ret   = op_ret;
                        local->op_errno = op_errno;
                }
        }

        ra_page_purge (page);

out:
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

        GF_VALIDATE_OR_GOTO ("read-ahead", file, out);

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
        GF_FREE (file);

out:
        return;
}
