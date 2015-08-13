/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
  TODO:
  - handle O_DIRECT
  - maintain offset, flush on lseek
  - ensure efficient memory management in case of random seek
*/

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "read-ahead.h"
#include "statedump.h"
#include <assert.h>
#include <sys/time.h>
#include "read-ahead-messages.h"

static void
read_ahead (call_frame_t *frame, ra_file_t *file);


int
ra_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        ra_conf_t  *conf    = NULL;
        ra_file_t  *file    = NULL;
        int         ret     = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);

        conf  = this->private;

        if (op_ret == -1) {
                goto unwind;
        }

        file = GF_CALLOC (1, sizeof (*file), gf_ra_mt_ra_file_t);
        if (!file) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        /* If O_DIRECT open, we disable caching on it */

        if ((fd->flags & O_DIRECT) || ((fd->flags & O_ACCMODE) == O_WRONLY))
                file->disabled = 1;

        file->offset = (unsigned long long) 0;
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

        file->fd = fd;
        file->page_count = conf->page_count;
        file->page_size = conf->page_size;
        pthread_mutex_init (&file->file_lock, NULL);

        if (!file->disabled) {
                file->page_count = 1;
        }

        ret = fd_ctx_set (fd, this, (uint64_t)(long)file);
        if (ret == -1) {
                gf_msg (frame->this->name, GF_LOG_WARNING,
                        0, READ_AHEAD_MSG_NO_MEMORY,
                        "cannot set read-ahead context"
                        "information in fd (%p)",
                        fd);
                ra_file_destroy (file);
                op_ret = -1;
                op_errno = ENOMEM;
        }

unwind:
        frame->local = NULL;

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
ra_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        ra_conf_t  *conf = NULL;
        ra_file_t  *file = NULL;
        int         ret  = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);

        conf  = this->private;

        if (op_ret == -1) {
                goto unwind;
        }

        file = GF_CALLOC (1, sizeof (*file), gf_ra_mt_ra_file_t);
        if (!file) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        /* If O_DIRECT open, we disable caching on it */

        if ((fd->flags & O_DIRECT) || ((fd->flags & O_ACCMODE) == O_WRONLY))
                file->disabled = 1;

        file->offset = (unsigned long long) 0;
        //file->size = fd->inode->buf.ia_size;
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

        file->fd = fd;
        file->page_count = conf->page_count;
        file->page_size = conf->page_size;
        pthread_mutex_init (&file->file_lock, NULL);

        ret = fd_ctx_set (fd, this, (uint64_t)(long)file);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING,
                        0, READ_AHEAD_MSG_NO_MEMORY,
                        "cannot set read ahead context"
                        "information in fd (%p)",
                        fd);
                ra_file_destroy (file);
                op_ret = -1;
                op_errno = ENOMEM;
        }

unwind:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);

        return 0;
}


int
ra_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, ra_open_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open,
                    loc, flags, fd, xdata);

        return 0;
}


int
ra_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, ra_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);

        return 0;
}

/* free cache pages between offset and offset+size,
   does not touch pages with frames waiting on it
*/

static void
flush_region (call_frame_t *frame, ra_file_t *file, off_t offset, off_t size,
              int for_write)
{
        ra_page_t *trav = NULL;
        ra_page_t *next = NULL;

        ra_file_lock (file);
        {
                trav = file->pages.next;
                while (trav != &file->pages
                       && trav->offset < (offset + size)) {

                        next = trav->next;
                        if (trav->offset >= offset) {
                                if (!trav->waitq) {
                                        ra_page_purge (trav);
                                }
                                else {
                                        trav->stale = 1;

                                        if (for_write) {
                                                trav->poisoned = 1;
                                        }
                                }
                        }
                        trav = next;
                }
        }
        ra_file_unlock (file);
}


int
ra_release (xlator_t *this, fd_t *fd)
{
        uint64_t tmp_file = 0;
        int      ret      = 0;

        GF_VALIDATE_OR_GOTO ("read-ahead", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &tmp_file);

        if (!ret) {
                ra_file_destroy ((ra_file_t *)(long)tmp_file);
        }

out:
        return 0;
}


void
read_ahead (call_frame_t *frame, ra_file_t *file)
{
        off_t      ra_offset   = 0;
        size_t     ra_size     = 0;
        off_t      trav_offset = 0;
        ra_page_t *trav        = NULL;
        off_t      cap         = 0;
        char       fault       = 0;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, file, out);

        if (!file->page_count) {
                goto out;
        }

        ra_size   = file->page_size * file->page_count;
        ra_offset = floor (file->offset, file->page_size);
        cap       = file->size ? file->size : file->offset + ra_size;

        while (ra_offset < min (file->offset + ra_size, cap)) {

                ra_file_lock (file);
                {
                        trav = ra_page_get (file, ra_offset);
                }
                ra_file_unlock (file);

                if (!trav)
                        break;

                ra_offset += file->page_size;
        }

        if (trav) {
                /* comfortable enough */
                goto out;
        }

        trav_offset = ra_offset;

        cap  = file->size ? file->size : ra_offset + ra_size;

        while (trav_offset < min(ra_offset + ra_size, cap)) {
                fault = 0;
                ra_file_lock (file);
                {
                        trav = ra_page_get (file, trav_offset);
                        if (!trav) {
                                fault = 1;
                                trav = ra_page_create (file, trav_offset);
                                if (trav)
                                        trav->dirty = 1;
                        }
                }
                ra_file_unlock (file);

                if (!trav) {
                        /* OUT OF MEMORY */
                        break;
                }

                if (fault) {
                        gf_msg_trace (frame->this->name, 0,
                                      "RA at offset=%"PRId64, trav_offset);
                        ra_page_fault (file, frame, trav_offset);
                }
                trav_offset += file->page_size;
        }

out:
        return;
}


int
ra_need_atime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *stbuf, struct iobref *iobref,
                   dict_t *xdata)
{
        GF_ASSERT (frame);
        STACK_DESTROY (frame->root);
        return 0;
}


static void
dispatch_requests (call_frame_t *frame, ra_file_t *file)
{
        ra_local_t   *local             = NULL;
        ra_conf_t    *conf              = NULL;
        off_t         rounded_offset    = 0;
        off_t         rounded_end       = 0;
        off_t         trav_offset       = 0;
        ra_page_t    *trav              = NULL;
        call_frame_t *ra_frame          = NULL;
        char          need_atime_update = 1;
        char          fault             = 0;

        GF_VALIDATE_OR_GOTO ("read-ahead", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, file, out);

        local = frame->local;
        conf  = file->conf;

        rounded_offset = floor (local->offset, file->page_size);
        rounded_end    = roof (local->offset + local->size, file->page_size);

        trav_offset = rounded_offset;

        while (trav_offset < rounded_end) {
                fault = 0;

                ra_file_lock (file);
                {
                        trav = ra_page_get (file, trav_offset);
                        if (!trav) {
                                trav = ra_page_create (file, trav_offset);
                                if (!trav) {
                                        local->op_ret = -1;
                                        local->op_errno = ENOMEM;
                                        goto unlock;
                                }
                                fault = 1;
                                need_atime_update = 0;
                        }
                        trav->dirty = 0;

                        if (trav->ready) {
                                gf_msg_trace (frame->this->name, 0,
                                              "HIT at offset=%"PRId64".",
                                              trav_offset);
                                ra_frame_fill (trav, frame);
                        } else {
                                gf_msg_trace (frame->this->name, 0,
                                              "IN-TRANSIT at "
                                              "offset=%"PRId64".",
                                              trav_offset);
                                ra_wait_on_page (trav, frame);
                                need_atime_update = 0;
                        }
                }
        unlock:
                ra_file_unlock (file);

                if (local->op_ret == -1) {
                        goto out;
                }

                if (fault) {
                        gf_msg_trace (frame->this->name, 0,
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
                if (ra_frame == NULL) {
                        goto out;
                }

                STACK_WIND (ra_frame, ra_need_atime_cbk,
                            FIRST_CHILD (frame->this),
                            FIRST_CHILD (frame->this)->fops->readv,
                            file->fd, 1, 1, 0, NULL);
        }

out:
        return ;
}


int
ra_readv_disabled_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iovec *vector,
                       int32_t count, struct iatt *stbuf, struct iobref *iobref,
                       dict_t *xdata)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref, xdata);

        return 0;
}


int
ra_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
        ra_file_t   *file            = NULL;
        ra_local_t  *local           = NULL;
        ra_conf_t   *conf            = NULL;
        int          op_errno        = EINVAL;
        char         expected_offset = 1;
        uint64_t     tmp_file        = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        conf = this->private;

        gf_msg_trace (this->name, 0,
                      "NEW REQ at offset=%"PRId64" for size=%"GF_PRI_SIZET"",
                      offset, size);

        fd_ctx_get (fd, this, &tmp_file);
        file = (ra_file_t *)(long)tmp_file;

        if (!file || file->disabled) {
                goto disabled;
        }

        if (file->offset != offset) {
                gf_msg_trace (this->name, 0,
                              "unexpected offset (%"PRId64" != %"PRId64") "
                              "resetting",
                              file->offset, offset);

                expected_offset = file->expected = file->page_count = 0;
        } else {
                gf_msg_trace (this->name, 0,
                              "expected offset (%"PRId64") when page_count=%d",
                              offset, file->page_count);

                if (file->expected < (file->page_size * conf->page_count)) {
                        file->expected += size;
                        file->page_count = min ((file->expected
                                                 / file->page_size),
                                                conf->page_count);
                }
        }

        if (!expected_offset) {
                flush_region (frame, file, 0, file->pages.prev->offset + 1, 0);
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->fd         = fd;
        local->offset     = offset;
        local->size       = size;
        local->wait_count = 1;

        local->fill.next  = &local->fill;
        local->fill.prev  = &local->fill;

        pthread_mutex_init (&local->local_lock, NULL);

        frame->local = local;

        dispatch_requests (frame, file);

        flush_region (frame, file, 0, floor (offset, file->page_size), 0);

        read_ahead (frame, file);

        ra_frame_return (frame);

        file->offset = offset + size;

        return 0;

unwind:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL,
                             NULL);

        return 0;

disabled:
        STACK_WIND (frame, ra_readv_disabled_cbk,
                    FIRST_CHILD (frame->this),
                    FIRST_CHILD (frame->this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;
}


int
ra_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, dict_t *xdata)
{
        GF_ASSERT (frame);
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);
        return 0;
}



int
ra_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
              dict_t *xdata)
{
        GF_ASSERT (frame);
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}


int
ra_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        ra_file_t *file     = NULL;
        uint64_t   tmp_file = 0;
        int32_t    op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        fd_ctx_get (fd, this, &tmp_file);

        file = (ra_file_t *)(long)tmp_file;
        if (file) {
                flush_region (frame, file, 0, file->pages.prev->offset+1, 0);
        }

        STACK_WIND (frame, ra_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (flush, frame, -1, op_errno, NULL);
        return 0;
}


int
ra_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
          dict_t *xdata)
{
        ra_file_t *file     = NULL;
        uint64_t   tmp_file = 0;
        int32_t    op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        fd_ctx_get (fd, this, &tmp_file);

        file = (ra_file_t *)(long)tmp_file;
        if (file) {
                flush_region (frame, file, 0, file->pages.prev->offset+1, 0);
        }

        STACK_WIND (frame, ra_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsync, fd, datasync, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int
ra_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        ra_file_t *file     = NULL;

        GF_ASSERT (frame);

        file = frame->local;

        if (file) {
                flush_region (frame, file, 0, file->pages.prev->offset+1, 1);
        }

        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}


int
ra_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
        ra_file_t *file    = NULL;
        uint64_t  tmp_file = 0;
        int32_t   op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        fd_ctx_get (fd, this, &tmp_file);
        file = (ra_file_t *)(long)tmp_file;
        if (file) {
                flush_region (frame, file, 0, file->pages.prev->offset+1, 1);
                frame->local = file;
                /* reset the read-ahead counters too */
                file->expected = file->page_count = 0;
        }

        STACK_WIND (frame, ra_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, flags, iobref, xdata);

        return 0;

unwind:
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int
ra_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


int
ra_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
ra_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
        ra_file_t *file     = NULL;
        fd_t      *iter_fd  = NULL;
        inode_t   *inode    = NULL;
        uint64_t   tmp_file = 0;
        int32_t    op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        inode = loc->inode;

        LOCK (&inode->lock);
        {
                list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                        fd_ctx_get (iter_fd, this, &tmp_file);
                        file = (ra_file_t *)(long)tmp_file;

                        if (!file)
                                continue;
                        /*
                         * Truncation invalidates reads just like writing does.
                         * TBD: this seems to flush more than it should.  The
                         * only time we should flush at all is when we're
                         * shortening (not lengthening) the file, and then only
                         * from new EOF to old EOF.  The same problem exists in
                         * ra_ftruncate.
                         */
                        flush_region (frame, file, 0,
                                      file->pages.prev->offset + 1, 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_truncate_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->truncate,
                    loc, offset, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


void
ra_page_dump (struct ra_page *page)
{
        int           i                        = 0;
        call_frame_t *frame                    = NULL;
        char          key[GF_DUMP_MAX_BUF_LEN] = {0, };
        ra_waitq_t   *trav                     = NULL;

        if (page == NULL) {
                goto out;
        }

        gf_proc_dump_write ("offset", "%"PRId64, page->offset);

        gf_proc_dump_write ("size", "%"PRId64, page->size);

        gf_proc_dump_write ("dirty", "%s", page->dirty ? "yes" : "no");

        gf_proc_dump_write ("poisoned", "%s", page->poisoned ? "yes" : "no");

        gf_proc_dump_write ("ready", "%s", page->ready ? "yes" : "no");

        for (trav = page->waitq; trav; trav = trav->next) {
		frame = trav->data;
                sprintf (key, "waiting-frame[%d]", i++);
                gf_proc_dump_write (key, "%"PRId64, frame->root->unique);
	}

out:
        return;
}

int32_t
ra_fdctx_dump (xlator_t *this, fd_t *fd)
{
	ra_file_t    *file     = NULL;
        ra_page_t    *page     = NULL;
        int32_t       ret      = 0, i = 0;
        uint64_t      tmp_file = 0;
        char         *path     = NULL;
        char          key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char          key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };

	fd_ctx_get (fd, this, &tmp_file);
	file = (ra_file_t *)(long)tmp_file;

        if (file == NULL) {
                ret = 0;
                goto out;
        }

        gf_proc_dump_build_key (key_prefix,
                                "xlator.performance.read-ahead",
                                "file");

        gf_proc_dump_add_section (key_prefix);

        ret = __inode_path (fd->inode, NULL, &path);
        if (path != NULL) {
                gf_proc_dump_write ("path", "%s", path);
                GF_FREE (path);
        }

        gf_proc_dump_write ("fd", "%p", fd);

        gf_proc_dump_write ("disabled", "%s", file->disabled ? "yes" : "no");

        if (file->disabled) {
                ret = 0;
                goto out;
        }

        gf_proc_dump_write ("page-size", "%"PRId64, file->page_size);

        gf_proc_dump_write ("page-count", "%u", file->page_count);

        gf_proc_dump_write ("next-expected-offset-for-sequential-reads",
                            "%"PRId64, file->offset);

        for (page = file->pages.next; page != &file->pages;
             page = page->next) {
                sprintf (key, "page[%d]", i);
                gf_proc_dump_write (key, "%p", page[i++]);
		ra_page_dump (page);
        }

        ret = 0;
out:
        return ret;
}

int
ra_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        ra_file_t *file     = NULL;
        fd_t      *iter_fd  = NULL;
        inode_t   *inode    = NULL;
        uint64_t   tmp_file = 0;
        int32_t    op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        inode = fd->inode;

        LOCK (&inode->lock);
        {
                list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                        fd_ctx_get (iter_fd, this, &tmp_file);
                        file = (ra_file_t *)(long)tmp_file;

                        if (!file)
                                continue;
                        flush_region (frame, file, 0,
                                      file->pages.prev->offset + 1, 0);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_attr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
ra_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
        ra_file_t *file    = NULL;
        fd_t      *iter_fd = NULL;
        inode_t   *inode   = NULL;
        uint64_t  tmp_file = 0;
        int32_t   op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        inode = fd->inode;

        LOCK (&inode->lock);
        {
                list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                        fd_ctx_get (iter_fd, this, &tmp_file);
                        file = (ra_file_t *)(long)tmp_file;
                        if (!file)
                                continue;
                        /*
                         * Truncation invalidates reads just like writing does.
                         * TBD: this seems to flush more than it should.  The
                         * only time we should flush at all is when we're
                         * shortening (not lengthening) the file, and then only
                         * from new EOF to old EOF.  The same problem exists in
                         * ra_truncate.
                         */
                        flush_region (frame, file, 0,
                                      file->pages.prev->offset + 1, 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_truncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ftruncate, fd, offset, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int
ra_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (discard, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}

static int
ra_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	     size_t len, dict_t *xdata)
{
        ra_file_t *file    = NULL;
        fd_t      *iter_fd = NULL;
        inode_t   *inode   = NULL;
        uint64_t  tmp_file = 0;
        int32_t   op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        inode = fd->inode;

        LOCK (&inode->lock);
        {
                list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                        fd_ctx_get (iter_fd, this, &tmp_file);
                        file = (ra_file_t *)(long)tmp_file;
                        if (!file)
                                continue;

                        flush_region(frame, file, offset, len, 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_discard_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->discard, fd, offset, len, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (discard, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int
ra_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (zerofill, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}

static int
ra_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
        ra_file_t *file    = NULL;
        fd_t      *iter_fd = NULL;
        inode_t   *inode   = NULL;
        uint64_t  tmp_file = 0;
        int32_t   op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        inode = fd->inode;

        LOCK (&inode->lock);
        {
                list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                        fd_ctx_get (iter_fd, this, &tmp_file);
                        file = (ra_file_t *)(long)tmp_file;
                        if (!file)
                                continue;

                        flush_region(frame, file, offset, len, 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_zerofill_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->zerofill, fd,
                    offset, len, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT (zerofill, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int
ra_priv_dump (xlator_t *this)
{
        ra_conf_t       *conf                           = NULL;
        int             ret                             = -1;
        char            key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        gf_boolean_t    add_section                     = _gf_false;

        if (!this) {
                goto out;
        }

        conf = this->private;
        if (!conf) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        READ_AHEAD_MSG_XLATOR_CONF_NULL,
                        "conf null in xlator");
                goto out;
        }

        gf_proc_dump_build_key (key_prefix, "xlator.performance.read-ahead",
                                "priv");

        gf_proc_dump_add_section (key_prefix);
        add_section = _gf_true;

        ret = pthread_mutex_trylock (&conf->conf_lock);
        if (ret)
                goto out;
        {
                gf_proc_dump_write ("page_size", "%d", conf->page_size);
                gf_proc_dump_write ("page_count", "%d", conf->page_count);
                gf_proc_dump_write ("force_atime_update", "%d",
                                    conf->force_atime_update);
        }
        pthread_mutex_unlock (&conf->conf_lock);

        ret = 0;
out:
        if (ret && conf) {
                if (add_section == _gf_false)
                        gf_proc_dump_add_section (key_prefix);

                gf_proc_dump_write ("Unable to dump priv",
                                    "(Lock acquisition failed) %s", this->name);
        }
        return ret;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this) {
                goto out;
        }

        ret = xlator_mem_acct_init (this, gf_ra_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        READ_AHEAD_MSG_NO_MEMORY, "Memory accounting init"
                        "failed");
        }

out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        ra_conf_t      *conf = NULL;
        int             ret = -1;

        GF_VALIDATE_OR_GOTO ("read-ahead", this, out);
        GF_VALIDATE_OR_GOTO ("read-ahead", this->private, out);

        conf = this->private;

        GF_OPTION_RECONF ("page-count", conf->page_count, options, uint32, out);

        GF_OPTION_RECONF ("page-size", conf->page_size, options, size_uint64,
                          out);

        ret = 0;
 out:
        return ret;
}

int
init (xlator_t *this)
{
        ra_conf_t *conf              = NULL;
        int32_t    ret               = -1;

        GF_VALIDATE_OR_GOTO ("read-ahead", this, out);

        if (!this->children || this->children->next) {
                gf_msg (this->name,  GF_LOG_ERROR, 0,
                        READ_AHEAD_MSG_XLATOR_CHILD_MISCONFIGURED,
                        "FATAL: read-ahead not configured with exactly one"
                        " child");
                goto out;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        READ_AHEAD_MSG_VOL_MISCONFIGURED,
                        "dangling volume. check volfile ");
        }

        conf = (void *) GF_CALLOC (1, sizeof (*conf), gf_ra_mt_ra_conf_t);
        if (conf == NULL) {
                goto out;
        }

        conf->page_size = this->ctx->page_size;

        GF_OPTION_INIT ("page-size", conf->page_size, size_uint64, out);

        GF_OPTION_INIT ("page-count", conf->page_count, uint32, out);

        GF_OPTION_INIT ("force-atime-update", conf->force_atime_update, bool, out);

        conf->files.next = &conf->files;
        conf->files.prev = &conf->files;

        pthread_mutex_init (&conf->conf_lock, NULL);

        this->local_pool = mem_pool_new (ra_local_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, READ_AHEAD_MSG_NO_MEMORY,
                        "failed to create local_t's memory pool");
                goto out;
        }

        this->private = conf;
        ret = 0;

out:
        if (ret == -1) {
                GF_FREE (conf);
        }

        return ret;
}


void
fini (xlator_t *this)
{
        ra_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("read-ahead", this, out);

        conf = this->private;
        if (conf == NULL) {
                goto out;
        }

        this->private = NULL;

        /* The files structures allocated in open and create are not deleted.
         * until that is freed, marking the below assert as warning.
        GF_ASSERT ((conf->files.next == &conf->files)
                   && (conf->files.prev == &conf->files));
        */
        if (!((conf->files.next == &conf->files)
                   && (conf->files.prev == &conf->files))) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        READ_AHEAD_MSG_UNDESTROYED_FILE_FOUND,
                       "undestroyed read ahead file structures found");
        }

        pthread_mutex_destroy (&conf->conf_lock);
        GF_FREE (conf);

out:
        return;
}

struct xlator_fops fops = {
        .open        = ra_open,
        .create      = ra_create,
        .readv       = ra_readv,
        .writev      = ra_writev,
        .flush       = ra_flush,
        .fsync       = ra_fsync,
        .truncate    = ra_truncate,
        .ftruncate   = ra_ftruncate,
        .fstat       = ra_fstat,
	.discard     = ra_discard,
        .zerofill    = ra_zerofill,
};

struct xlator_cbks cbks = {
        .release       = ra_release,
};

struct xlator_dumpops dumpops = {
        .priv      =  ra_priv_dump,
        .fdctx     =  ra_fdctx_dump,
};

struct volume_options options[] = {
        { .key  = {"force-atime-update"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false"
        },
        { .key  = {"page-count"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 16,
          .default_value = "4",
          .description = "Number of pages that will be pre-fetched"
        },
	{ .key = {"page-size"},
	  .type = GF_OPTION_TYPE_SIZET,
	  .min = 4096,
	  .max = 1048576 * 64,
	  .default_value = "131072",
	  .description = "Page size with which read-ahead performs server I/O"
	},
        { .key = {NULL} },
};
