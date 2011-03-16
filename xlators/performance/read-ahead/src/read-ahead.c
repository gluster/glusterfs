/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
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
#include "statedump.h"
#include <assert.h>
#include <sys/time.h>

static void
read_ahead (call_frame_t *frame, ra_file_t *file);


int
ra_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        ra_conf_t  *conf    = NULL;
        ra_file_t  *file    = NULL;
        int         ret     = 0;
        long        wbflags = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);

        conf  = this->private;

        if (op_ret == -1) {
                goto unwind;
        }

        wbflags = (long)frame->local;

        file = GF_CALLOC (1, sizeof (*file), gf_ra_mt_ra_file_t);
        if (!file) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        /* If O_DIRECT open, we disable caching on it */

        if ((fd->flags & O_DIRECT) || ((fd->flags & O_ACCMODE) == O_WRONLY))
                file->disabled = 1;

        if (wbflags & GF_OPEN_NOWB) {
                file->disabled = 1;
        }

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
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "cannot set read-ahead context information in fd (%p)",
                        fd);
                ra_file_destroy (file);
                op_ret = -1;
                op_errno = ENOMEM;
        }

unwind:
        frame->local = NULL;

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);

        return 0;
}


int
ra_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent)
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
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set read ahead context information in fd (%p)",
                        fd);
                ra_file_destroy (file);
                op_ret = -1;
                op_errno = ENOMEM;
        }

unwind:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);

        return 0;
}


int
ra_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        frame->local = (void *)(long)wbflags;

        STACK_WIND (frame, ra_open_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open,
                    loc, flags, fd, wbflags);

        return 0;
}


int
ra_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd, dict_t *params)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, ra_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd, params);

        return 0;
}

/* free cache pages between offset and offset+size,
   does not touch pages with frames waiting on it
*/

static void
flush_region (call_frame_t *frame, ra_file_t *file, off_t offset, off_t size)
{
        ra_page_t *trav = NULL;
        ra_page_t *next = NULL;

        ra_file_lock (file);
        {
                trav = file->pages.next;
                while (trav != &file->pages
                       && trav->offset < (offset + size)) {

                        next = trav->next;
                        if (trav->offset >= offset && !trav->waitq) {
                                ra_page_purge (trav);
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
                        gf_log (frame->this->name, GF_LOG_TRACE,
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
                   int32_t count, struct iatt *stbuf, struct iobref *iobref)
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
                                fault = 1;
                                need_atime_update = 0;
                        }

                        if (!trav) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                goto unlock;
                        }

                        if (trav->ready) {
                                gf_log (frame->this->name, GF_LOG_TRACE,
                                        "HIT at offset=%"PRId64".",
                                        trav_offset);
                                ra_frame_fill (trav, frame);
                        } else {
                                gf_log (frame->this->name, GF_LOG_TRACE,
                                        "IN-TRANSIT at offset=%"PRId64".",
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
                        gf_log (frame->this->name, GF_LOG_TRACE,
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
                            file->fd, 1, 1);
        }

out:
        return ;
}


int
ra_readv_disabled_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iovec *vector,
                       int32_t count, struct iatt *stbuf, struct iobref *iobref)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref);

        return 0;
}


int
ra_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
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

        gf_log (this->name, GF_LOG_TRACE,
                "NEW REQ at offset=%"PRId64" for size=%"GF_PRI_SIZET"",
                offset, size);

        fd_ctx_get (fd, this, &tmp_file);
        file = (ra_file_t *)(long)tmp_file;

        if (file == NULL) {
                op_errno = EBADF;
                gf_log (this->name, GF_LOG_WARNING,
                        "readv received on fd (%p) with no"
                        " file set in its context", fd);
                goto unwind;
        }

        if (file->offset != offset) {
                gf_log (this->name, GF_LOG_TRACE,
                        "unexpected offset (%"PRId64" != %"PRId64") resetting",
                        file->offset, offset);

                expected_offset = file->expected = file->page_count = 0;
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "expected offset (%"PRId64") when page_count=%d",
                        offset, file->page_count);

                if (file->expected < (conf->page_size * conf->page_count)) {
                        file->expected += size;
                        file->page_count = min ((file->expected
                                                 / file->page_size),
                                                conf->page_count);
                }
        }

        if (!expected_offset) {
                flush_region (frame, file, 0, file->pages.prev->offset + 1);
        }

        if (file->disabled) {
                STACK_WIND (frame, ra_readv_disabled_cbk,
                            FIRST_CHILD (frame->this),
                            FIRST_CHILD (frame->this)->fops->readv,
                            file->fd, size, offset);
                return 0;
        }

        local = (void *) GF_CALLOC (1, sizeof (*local), gf_ra_mt_ra_local_t);
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

        flush_region (frame, file, 0, floor (offset, file->page_size));

        read_ahead (frame, file);

        ra_frame_return (frame);

        file->offset = offset + size;

        return 0;

unwind:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);

        return 0;
}


int
ra_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno)
{
        GF_ASSERT (frame);
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
        return 0;
}



int
ra_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf)
{
        GF_ASSERT (frame);
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
ra_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        ra_file_t *file     = NULL;
        uint64_t   tmp_file = 0;
        int32_t    op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        fd_ctx_get (fd, this, &tmp_file);

        file = (ra_file_t *)(long)tmp_file;
        if (file == NULL) {
                op_errno = EBADF;
                gf_log (this->name, GF_LOG_WARNING,
                        "flush received on fd (%p) with no"
                        " file set in its context", fd);
                goto unwind;
        }

        flush_region (frame, file, 0, file->pages.prev->offset+1);

        STACK_WIND (frame, ra_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd);
        return 0;

unwind:
        STACK_UNWIND_STRICT (flush, frame, -1, op_errno);
        return 0;
}


int
ra_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync)
{
        ra_file_t *file     = NULL;
        uint64_t   tmp_file = 0;
        int32_t    op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        fd_ctx_get (fd, this, &tmp_file);

        file = (ra_file_t *)(long)tmp_file;
        if (file == NULL) {
                op_errno = EBADF;
                gf_log (this->name, GF_LOG_WARNING,
                        "fsync received on fd (%p) with no"
                        " file set in its context", fd);
                goto unwind;
        }

        flush_region (frame, file, 0, file->pages.prev->offset+1);

        STACK_WIND (frame, ra_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsync, fd, datasync);
        return 0;

unwind:
        STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
ra_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf)
{
        fd_t      *fd       = NULL;
        ra_file_t *file     = NULL;
        uint64_t   tmp_file = 0;

        GF_ASSERT (frame);

        fd = frame->local;

        fd_ctx_get (fd, this, &tmp_file);
        file = (ra_file_t *)(long)tmp_file;

        if (file == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "no read-ahead context set in fd (%p)", fd);
                op_errno = EBADF;
                op_ret = -1;
                goto out;
        }

        flush_region (frame, file, 0, file->pages.prev->offset+1);

out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
ra_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, struct iobref *iobref)
{
        ra_file_t *file    = NULL;
        uint64_t  tmp_file = 0;
        int32_t   op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        fd_ctx_get (fd, this, &tmp_file);
        file = (ra_file_t *)(long)tmp_file;
        if (file == NULL) {
                op_errno = EBADF;
                gf_log (this->name, GF_LOG_WARNING, "writev received on fd with"
                        "no file set in its context");
                goto unwind;
        }

        flush_region (frame, file, 0, file->pages.prev->offset+1);

        /* reset the read-ahead counters too */
        file->expected = file->page_count = 0;

        frame->local = fd;

        STACK_WIND (frame, ra_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, iobref);

        return 0;

unwind:
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
ra_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
        return 0;
}


int
ra_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);
        return 0;
}


int
ra_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
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
                        flush_region (frame, file, 0,
                                      file->pages.prev->offset + 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_truncate_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->truncate,
                    loc, offset);
        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
ra_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
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
                                      file->pages.prev->offset + 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_attr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        return 0;

unwind:
        STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL);
        return 0;
}


int
ra_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
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
                        flush_region (frame, file, 0,
                                      file->pages.prev->offset + 1);
                }
        }
        UNLOCK (&inode->lock);

        STACK_WIND (frame, ra_truncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ftruncate, fd, offset);
        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
ra_priv_dump (xlator_t *this)
{
        ra_conf_t       *conf                           = NULL;
        int             ret                             = -1;
        char            key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char            key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };

        if (!this) {
                goto out;
        }

        conf = this->private;
        if (!conf) {
                gf_log (this->name, GF_LOG_WARNING, "conf null in xlator");
                goto out;
        }

        ret = pthread_mutex_trylock (&conf->conf_lock);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Unable to lock client %s "
                        "(%s)", this->name, strerror (ret));
                ret = -1;
                goto out;
        }

        gf_proc_dump_build_key (key_prefix, "xlator.performance.read-ahead",
                                "priv");

        gf_proc_dump_add_section (key_prefix);
        gf_proc_dump_build_key (key, key_prefix, "page_size");
        gf_proc_dump_write (key, "%d", conf->page_size);
        gf_proc_dump_build_key (key, key_prefix, "page_count");
        gf_proc_dump_write (key, "%d", conf->page_count);
        gf_proc_dump_build_key (key, key_prefix, "force_atime_update");
        gf_proc_dump_write (key, "%d", conf->force_atime_update);

        pthread_mutex_unlock (&conf->conf_lock);

        ret = 0;
out:
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
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
        }

out:
        return ret;
}

int
init (xlator_t *this)
{
        ra_conf_t *conf              = NULL;
        dict_t    *options           = NULL;
        char      *page_count_string = NULL;
        int32_t    ret               = -1;

        GF_VALIDATE_OR_GOTO ("read-ahead", this, out);

        options = this->options;
        if (!this->children || this->children->next) {
                gf_log (this->name,  GF_LOG_ERROR,
                        "FATAL: read-ahead not configured with exactly one"
                        " child");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        conf = (void *) GF_CALLOC (1, sizeof (*conf), gf_ra_mt_ra_conf_t);
        if (conf == NULL) {
                goto out;
        }

        conf->page_size = this->ctx->page_size;
        conf->page_count = 4;

        if (dict_get (options, "page-count")) {
                page_count_string = data_to_str (dict_get (options,
                                                           "page-count"));
        }

        if (page_count_string) {
                if (gf_string2uint_base10 (page_count_string, &conf->page_count)
                    != 0) {
                        gf_log ("read-ahead", GF_LOG_ERROR,
                                "invalid number format \"%s\" of \"option "
                                "page-count\"",
                                page_count_string);
                        goto out;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "Using conf->page_count = %u", conf->page_count);
        }

        if (dict_get (options, "force-atime-update")) {
                char *force_atime_update_str = NULL;

                force_atime_update_str
                        = data_to_str (dict_get (options,
                                                 "force-atime-update"));

                if (gf_string2boolean (force_atime_update_str,
                                       &conf->force_atime_update) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'force-atime-update' takes only boolean "
                                "options");
                        goto out;
                }

                if (conf->force_atime_update) {
                        gf_log (this->name, GF_LOG_WARNING, "Forcing atime "
                                "updates on cache hit");
                }
        }

        conf->files.next = &conf->files;
        conf->files.prev = &conf->files;

        pthread_mutex_init (&conf->conf_lock, NULL);
        this->private = conf;
        ret = 0;

out:
        if (ret == -1) {
                if (conf != NULL) {
                        GF_FREE (conf);
                }
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

        pthread_mutex_destroy (&conf->conf_lock);
        GF_FREE (conf);

        this->private = NULL;

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
};

struct xlator_cbks cbks = {
        .release       = ra_release,
};

struct xlator_dumpops dumpops = {
        .priv      =  ra_priv_dump,
};

struct volume_options options[] = {
        { .key  = {"force-atime-update"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key  = {"page-count"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 16
        },
        { .key = {NULL} },
};
