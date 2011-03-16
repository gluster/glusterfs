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

/*TODO: check for non null wb_file_data before getting wb_file */


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "list.h"
#include "compat.h"
#include "compat-errno.h"
#include "common-utils.h"
#include "call-stub.h"
#include "statedump.h"
#include "write-behind-mem-types.h"

#define MAX_VECTOR_COUNT  8
#define WB_AGGREGATE_SIZE 131072 /* 128 KB */
#define WB_WINDOW_SIZE    1048576 /* 1MB */

typedef struct list_head list_head_t;
struct wb_conf;
struct wb_page;
struct wb_file;

typedef struct wb_file {
        int          disabled;
        uint64_t     disable_till;
        size_t       window_conf;
        size_t       window_current;
        int32_t      flags;
        size_t       aggregate_current;
        int32_t      refcount;
        int32_t      op_ret;
        int32_t      op_errno;
        list_head_t  request;
        list_head_t  passive_requests;
        fd_t        *fd;
        gf_lock_t    lock;
        xlator_t    *this;
}wb_file_t;

typedef struct wb_request {
        list_head_t     list;
        list_head_t     winds;
        list_head_t     unwinds;
        list_head_t     other_requests;
        call_stub_t    *stub;
        size_t          write_size;
        int32_t         refcount;
        wb_file_t      *file;
        glusterfs_fop_t fop;
        union {
                struct  {
                        char write_behind;
                        char stack_wound;
                        char got_reply;
                        char virgin;
                        char flush_all;     /* while trying to sync to back-end,
                                             * don't wait till a data of size
                                             * equal to configured aggregate-size
                                             * is accumulated, instead sync
                                             * whatever data currently present in
                                             * request queue.
                                             */

                }write_request;

                struct {
                        char marked_for_resume;
                }other_requests;
        }flags;
} wb_request_t;

struct wb_conf {
        uint64_t     aggregate_size;
        uint64_t     window_size;
        uint64_t     disable_till;
        gf_boolean_t enable_O_SYNC;
        gf_boolean_t flush_behind;
        gf_boolean_t enable_trickling_writes;
};

typedef struct wb_local {
        list_head_t     winds;
        int32_t         flags;
        int32_t         wbflags;
        struct wb_file *file;
        wb_request_t   *request;
        int             op_ret;
        int             op_errno;
        call_frame_t   *frame;
        int32_t         reply_count;
} wb_local_t;

typedef struct wb_conf wb_conf_t;
typedef struct wb_page wb_page_t;

int32_t
wb_process_queue (call_frame_t *frame, wb_file_t *file);

ssize_t
wb_sync (call_frame_t *frame, wb_file_t *file, list_head_t *winds);

ssize_t
__wb_mark_winds (list_head_t *list, list_head_t *winds, size_t aggregate_size,
                 char enable_trickling_writes);


static int
__wb_request_unref (wb_request_t *this)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        if (this->refcount <= 0) {
                gf_log ("wb-request", GF_LOG_WARNING,
                        "refcount(%d) is <= 0", this->refcount);
                goto out;
        }

        ret = --this->refcount;
        if (this->refcount == 0) {
                list_del_init (&this->list);
                if (this->stub && this->stub->fop == GF_FOP_WRITE) {
                        call_stub_destroy (this->stub);
                }

                GF_FREE (this);
        }

out:
        return ret;
}


static int
wb_request_unref (wb_request_t *this)
{
        wb_file_t *file = NULL;
        int        ret  = -1;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        file = this->file;

        LOCK (&file->lock);
        {
                ret = __wb_request_unref (this);
        }
        UNLOCK (&file->lock);

out:
        return ret;
}


static wb_request_t *
__wb_request_ref (wb_request_t *this)
{
        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        if (this->refcount < 0) {
                gf_log ("wb-request", GF_LOG_WARNING,
                        "refcount(%d) is < 0", this->refcount);
                this = NULL;
                goto out;
        }

        this->refcount++;

out:
        return this;
}


wb_request_t *
wb_request_ref (wb_request_t *this)
{
        wb_file_t *file = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        file = this->file;
        LOCK (&file->lock);
        {
                this = __wb_request_ref (this);
        }
        UNLOCK (&file->lock);

out:
        return this;
}


wb_request_t *
wb_enqueue (wb_file_t *file, call_stub_t *stub)
{
        wb_request_t *request = NULL, *tmp = NULL;
        call_frame_t *frame   = NULL;
        wb_local_t   *local   = NULL;
        struct iovec *vector  = NULL;
        int32_t       count   = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", file, out);
        GF_VALIDATE_OR_GOTO (file->this->name, stub, out);

        request = GF_CALLOC (1, sizeof (*request), gf_wb_mt_wb_request_t);
        if (request == NULL) {
                goto out;
        }

        INIT_LIST_HEAD (&request->list);
        INIT_LIST_HEAD (&request->winds);
        INIT_LIST_HEAD (&request->unwinds);
        INIT_LIST_HEAD (&request->other_requests);

        request->stub = stub;
        request->file = file;
        request->fop  = stub->fop;

        frame = stub->frame;
        local = frame->local;
        if (local) {
                local->request = request;
        }

        if (stub->fop == GF_FOP_WRITE) {
                vector = stub->args.writev.vector;
                count = stub->args.writev.count;

                request->write_size = iov_length (vector, count);
                if (local) {
                        local->op_ret = request->write_size;
                        local->op_errno = 0;
                }

                request->flags.write_request.virgin = 1;
        }

        LOCK (&file->lock);
        {
                list_add_tail (&request->list, &file->request);
                if (stub->fop == GF_FOP_WRITE) {
                        /* reference for stack winding */
                        __wb_request_ref (request);

                        /* reference for stack unwinding */
                        __wb_request_ref (request);

                        file->aggregate_current += request->write_size;
                } else {
                        list_for_each_entry (tmp, &file->request, list) {
                                if (tmp->stub && tmp->stub->fop
                                    == GF_FOP_WRITE) {
                                        tmp->flags.write_request.flush_all = 1;
                                }
                        }

                        /*reference for resuming */
                        __wb_request_ref (request);
                }
        }
        UNLOCK (&file->lock);

out:
        return request;
}


wb_file_t *
wb_file_create (xlator_t *this, fd_t *fd, int32_t flags)
{
        wb_file_t *file = NULL;
        wb_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        conf = this->private;

        file = GF_CALLOC (1, sizeof (*file), gf_wb_mt_wb_file_t);
        if (file == NULL) {
                goto out;
        }

        INIT_LIST_HEAD (&file->request);
        INIT_LIST_HEAD (&file->passive_requests);

        /*
          fd_ref() not required, file should never decide the existance of
          an fd
        */
        file->fd= fd;
        file->disable_till = conf->disable_till;
        file->this = this;
        file->refcount = 1;
        file->window_conf = conf->window_size;
        file->flags = flags;

        fd_ctx_set (fd, this, (uint64_t)(long)file);

out:
        return file;
}


void
wb_file_destroy (wb_file_t *file)
{
        int32_t refcount = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", file, out);

        LOCK (&file->lock);
        {
                refcount = --file->refcount;
        }
        UNLOCK (&file->lock);

        if (!refcount){
                LOCK_DESTROY (&file->lock);
                GF_FREE (file);
        }

out:
        return;
}


int32_t
wb_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf)
{
        wb_local_t   *local             = NULL;
        list_head_t  *winds             = NULL;
        wb_file_t    *file              = NULL;
        wb_request_t *request           = NULL, *dummy = NULL;
        wb_local_t   *per_request_local = NULL;
        int32_t       ret               = -1;
        fd_t         *fd                = NULL;

        GF_ASSERT (frame);
        GF_ASSERT (this);

        local = frame->local;
        winds = &local->winds;

        file = local->file;
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        LOCK (&file->lock);
        {
                list_for_each_entry_safe (request, dummy, winds, winds) {
                        request->flags.write_request.got_reply = 1;

                        if (!request->flags.write_request.write_behind
                            && (op_ret == -1)) {
                                per_request_local = request->stub->frame->local;
                                per_request_local->op_ret = op_ret;
                                per_request_local->op_errno = op_errno;
                        }

                        if (request->flags.write_request.write_behind) {
                                file->window_current -= request->write_size;
                        }

                        __wb_request_unref (request);
                }

                if (op_ret == -1) {
                        file->op_ret = op_ret;
                        file->op_errno = op_errno;
                }
                fd = file->fd;
        }
        UNLOCK (&file->lock);

        ret = wb_process_queue (frame, file);
        if (ret == -1) {
                if (errno == ENOMEM) {
                        LOCK (&file->lock);
                        {
                                file->op_ret = -1;
                                file->op_errno = ENOMEM;
                        }
                        UNLOCK (&file->lock);
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "request queue processing failed");
        }

        /* safe place to do fd_unref */
        fd_unref (fd);

        STACK_DESTROY (frame->root);

out:
        return 0;
}


ssize_t
wb_sync (call_frame_t *frame, wb_file_t *file, list_head_t *winds)
{
        wb_request_t   *dummy         = NULL, *request = NULL;
        wb_request_t   *first_request = NULL, *next = NULL;
        size_t          total_count   = 0, count = 0;
        size_t          copied        = 0;
        call_frame_t   *sync_frame    = NULL;
        struct iobref  *iobref        = NULL;
        wb_local_t     *local         = NULL;
        struct iovec   *vector        = NULL;
        ssize_t         current_size  = 0, bytes = 0;
        size_t          bytecount     = 0;
        wb_conf_t      *conf          = NULL;
        fd_t           *fd            = NULL;
        int32_t         op_errno      = -1;

        GF_VALIDATE_OR_GOTO_WITH_ERROR ((file ? file->this->name
                                         : "write-behind"), frame,
                                        out, bytes, -1);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, file, out, bytes,
                                        -1);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, winds, out, bytes,
                                        -1);

        conf = file->this->private;
        list_for_each_entry (request, winds, winds) {
                total_count += request->stub->args.writev.count;
                if (total_count > 0) {
                        break;
                }
        }

        if (total_count == 0) {
                gf_log (file->this->name, GF_LOG_TRACE, "no vectors are to be"
                        "synced");
                goto out;
        }

        list_for_each_entry_safe (request, dummy, winds, winds) {
                if (!vector) {
                        vector = GF_MALLOC (VECTORSIZE (MAX_VECTOR_COUNT),
                                            gf_wb_mt_iovec);
                        if (vector == NULL) {
                                bytes = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        iobref = iobref_new ();
                        if (iobref == NULL) {
                                bytes = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        local = GF_CALLOC (1, sizeof (*local),
                                           gf_wb_mt_wb_local_t);
                        if (local == NULL) {
                                bytes = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        INIT_LIST_HEAD (&local->winds);

                        first_request = request;
                        current_size = 0;
                }

                count += request->stub->args.writev.count;
                bytecount = VECTORSIZE (request->stub->args.writev.count);
                memcpy (((char *)vector)+copied,
                        request->stub->args.writev.vector,
                        bytecount);
                copied += bytecount;

                current_size += request->write_size;

                if (request->stub->args.writev.iobref) {
                        iobref_merge (iobref,
                                      request->stub->args.writev.iobref);
                }

                next = NULL;
                if (request->winds.next != winds) {
                        next = list_entry (request->winds.next,
                                           wb_request_t, winds);
                }

                list_del_init (&request->winds);
                list_add_tail (&request->winds, &local->winds);

                if ((!next)
                    || ((count + next->stub->args.writev.count)
                        > MAX_VECTOR_COUNT)
                    || ((current_size + next->write_size)
                        > conf->aggregate_size)) {

                        sync_frame = copy_frame (frame);
                        if (sync_frame == NULL) {
                                bytes = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        sync_frame->local = local;
                        local->file = file;

                        LOCK (&file->lock);
                        {
                                fd = file->fd;
                        }
                        UNLOCK (&file->lock);

                        fd_ref (fd);

                        bytes += current_size;
                        STACK_WIND (sync_frame, wb_sync_cbk,
                                    FIRST_CHILD(sync_frame->this),
                                    FIRST_CHILD(sync_frame->this)->fops->writev,
                                    fd, vector, count,
                                    first_request->stub->args.writev.off,
                                    iobref);

                        iobref_unref (iobref);
                        GF_FREE (vector);
                        first_request = NULL;
                        iobref = NULL;
                        vector = NULL;
                        sync_frame = NULL;
                        local = NULL;
                        copied = count = 0;
                }
        }

out:
        if (sync_frame != NULL) {
                sync_frame->local = NULL;
                STACK_DESTROY (sync_frame->root);
        }

        if (local != NULL) {
                /* had we winded these requests, we would have unrefed
                 * in wb_sync_cbk.
                 */
                list_for_each_entry_safe (request, dummy, &local->winds,
                                          winds) {
                        wb_request_unref (request);
                }

                GF_FREE (local);
                local = NULL;
        }

        if (iobref != NULL) {
                iobref_unref (iobref);
        }

        if (vector != NULL) {
                GF_FREE (vector);
        }

        if (bytes == -1) {
                /*
                 * had we winded these requests, we would have unrefed
                 * in wb_sync_cbk.
                 */
                if (local) {
                        list_for_each_entry_safe (request, dummy, &local->winds,
                                                  winds) {
                                wb_request_unref (request);
                        }
                }

                if (file != NULL) {
                        LOCK (&file->lock);
                        {
                                file->op_ret = -1;
                                file->op_errno = op_errno;
                        }
                        UNLOCK (&file->lock);
                }
        }

        return bytes;
}


int32_t
wb_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *buf)
{
        wb_local_t   *local         = NULL;
        wb_request_t *request       = NULL;
        call_frame_t *process_frame = NULL;
        wb_file_t    *file          = NULL;
        int32_t       ret           = -1;
        fd_t         *fd            = NULL;

        GF_ASSERT (frame);
        GF_ASSERT (this);

        local = frame->local;
        file = local->file;

        request = local->request;
        if (request) {
                process_frame = copy_frame (frame);
                if (process_frame == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);

        if (request != NULL) {
                wb_request_unref (request);
        }

        if (process_frame != NULL) {
                ret = wb_process_queue (process_frame, file);
                if (ret == -1) {
                        if ((errno == ENOMEM) && (file != NULL)) {
                                LOCK (&file->lock);
                                {
                                        file->op_ret = -1;
                                        file->op_errno = ENOMEM;
                                }
                                UNLOCK (&file->lock);
                        }

                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }

                STACK_DESTROY (process_frame->root);
        }

        if (file) {
                LOCK (&file->lock);
                {
                        fd = file->fd;
                }
                UNLOCK (&file->lock);

                fd_unref (fd);
        }

        return 0;
}


static int32_t
wb_stat_helper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, wb_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc);
        return 0;
}


int32_t
wb_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        wb_file_t    *file     = NULL;
        fd_t         *iter_fd  = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        wb_request_t *request  = NULL;
        int32_t       ret      = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        if (loc->inode) {
                /* FIXME: fd_lookup extends life of fd till stat returns */
                iter_fd = fd_lookup (loc->inode, frame->root->pid);
                if (iter_fd) {
                        if (!fd_ctx_get (iter_fd, this, &tmp_file)) {
                                file = (wb_file_t *)(long)tmp_file;
                        } else {
                                fd_unref (iter_fd);
                                iter_fd = NULL;
                        }
                }
        }

        local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->file = file;

        frame->local = local;

        if (file) {
                stub = fop_stat_stub (frame, wb_stat_helper, loc);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_stat_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->stat, loc);
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL);

        if (stub) {
                call_stub_destroy (stub);
        }

        if (iter_fd != NULL) {
                fd_unref (iter_fd);
        }

        return 0;
}


int32_t
wb_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf)
{
        wb_local_t   *local   = NULL;
        wb_request_t *request = NULL;
        wb_file_t    *file    = NULL;
        int32_t       ret     = -1;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;

        request = local->request;
        if ((file != NULL) && (request != NULL)) {
                wb_request_unref (request);
                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        if (errno == ENOMEM) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                        }

                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        }

        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);

        return 0;
}


int32_t
wb_fstat_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, wb_fstat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd);
        return 0;
}


int32_t
wb_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        wb_file_t    *file     = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        wb_request_t *request  = NULL;
        int32_t       ret      = -1;
        int           op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if ((!IA_ISDIR (fd->inode->ia_type))
            && fd_ctx_get (fd, this, &tmp_file)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "write behind file pointer is"
                        " not stored in context of fd(%p), returning EBADFD",
                        fd);
                op_errno = EBADFD;
                goto unwind;
        }

        file = (wb_file_t *)(long)tmp_file;
        local = GF_CALLOC (1, sizeof (*local),
                           gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->file = file;

        frame->local = local;

        if (file) {
                stub = fop_fstat_stub (frame, wb_fstat_helper, fd);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                /*
                  FIXME:should the request queue be emptied in case of error?
                */
                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_fstat_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fstat, fd);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (fstat, frame, -1, op_errno, NULL);

        if (stub) {
                call_stub_destroy (stub);
        }

        return 0;
}


int32_t
wb_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf)
{
        wb_local_t   *local         = NULL;
        wb_request_t *request       = NULL;
        wb_file_t    *file          = NULL;
        call_frame_t *process_frame = NULL;
        int32_t       ret           = -1;
        fd_t         *fd            = NULL;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;
        request = local->request;

        if ((request != NULL) && (file != NULL)) {
                process_frame = copy_frame (frame);
                if (process_frame == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf);

        if (request) {
                wb_request_unref (request);
        }

        if (process_frame != NULL) {
                ret = wb_process_queue (process_frame, file);
                if (ret == -1) {
                        if ((errno == ENOMEM) && (file != NULL)) {
                                LOCK (&file->lock);
                                {
                                        file->op_ret = -1;
                                        file->op_errno = ENOMEM;
                                }
                                UNLOCK (&file->lock);
                        }

                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }

                STACK_DESTROY (process_frame->root);
        }

        if (file) {
                LOCK (&file->lock);
                {
                        fd = file->fd;
                }
                UNLOCK (&file->lock);

                fd_unref (fd);
        }

        return 0;
}


static int32_t
wb_truncate_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, wb_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset);

        return 0;
}


int32_t
wb_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        wb_file_t    *file     = NULL;
        fd_t         *iter_fd  = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        wb_request_t *request  = NULL;
        int32_t       ret      = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        if (loc->inode) {
                /*
                  FIXME: fd_lookup extends life of fd till the execution of
                  truncate_cbk
                */
                iter_fd = fd_lookup (loc->inode, frame->root->pid);
                if (iter_fd) {
                        if (!fd_ctx_get (iter_fd, this, &tmp_file)){
                                file = (wb_file_t *)(long)tmp_file;
                        } else {
                                fd_unref (iter_fd);
                        }
                }
        }

        local = GF_CALLOC (1, sizeof (*local),
                           gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->file = file;

        frame->local = local;
        if (file) {
                stub = fop_truncate_stub (frame, wb_truncate_helper, loc,
                                          offset);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_truncate_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc, offset);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL);

        if (stub) {
                call_stub_destroy (stub);
        }

        return 0;
}


int32_t
wb_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf)
{
        wb_local_t   *local   = NULL;
        wb_request_t *request = NULL;
        wb_file_t    *file    = NULL;
        int32_t       ret     = -1;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;
        request = local->request;

        if ((request != NULL) && (file != NULL)) {
                wb_request_unref (request);
                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        if (errno == ENOMEM) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                        }

                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        }

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf);

        return 0;
}


static int32_t
wb_ftruncate_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, wb_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        return 0;
}


int32_t
wb_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        wb_file_t    *file     = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        wb_request_t *request  = NULL;
        int32_t       ret      = -1;
        int           op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if ((!IA_ISDIR (fd->inode->ia_type))
            && fd_ctx_get (fd, this, &tmp_file)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "write behind file pointer is"
                        " not stored in context of fd(%p), returning EBADFD",
                        fd);
                op_errno = EBADFD;
                goto unwind;
        }

        file = (wb_file_t *)(long)tmp_file;

        local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->file = file;

        frame->local = local;

        if (file) {
                stub = fop_ftruncate_stub (frame, wb_ftruncate_helper, fd,
                                           offset);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_ftruncate_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (ftruncate, frame, -1, op_errno, NULL, NULL);

        if (stub) {
                call_stub_destroy (stub);
        }

        return 0;
}


int32_t
wb_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                struct iatt *statpost)
{
        wb_local_t   *local         = NULL;
        wb_request_t *request       = NULL;
        call_frame_t *process_frame = NULL;
        wb_file_t    *file          = NULL;
        int32_t       ret           = -1;
        fd_t         *fd            = NULL;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;
        request = local->request;

        if (request) {
                process_frame = copy_frame (frame);
                if (process_frame == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, statpre,
                             statpost);

        if (request) {
                wb_request_unref (request);
        }

        if (request && (process_frame != NULL)) {
                ret = wb_process_queue (process_frame, file);
                if (ret == -1) {
                        if ((errno == ENOMEM) && (file != NULL)) {
                                LOCK (&file->lock);
                                {
                                        file->op_ret = -1;
                                        file->op_errno = ENOMEM;
                                }
                                UNLOCK (&file->lock);
                        }

                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }

                STACK_DESTROY (process_frame->root);
        }

        if (file) {
                LOCK (&file->lock);
                {
                        fd = file->fd;
                }
                UNLOCK (&file->lock);

                fd_unref (fd);
        }

        return 0;
}


static int32_t
wb_setattr_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid)
{
        GF_ASSERT (frame);
        GF_ASSERT (this);

        STACK_WIND (frame, wb_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid);
        return 0;
}


int32_t
wb_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid)
{
        wb_file_t    *file     = NULL;
        fd_t         *iter_fd  = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        wb_request_t *request  = NULL;
        int32_t       ret      = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;

        if (!(valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME))) {
                STACK_WIND (frame, wb_setattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr, loc, stbuf,
                            valid);
                goto out;
        }

        if (loc->inode) {
                /*
                  FIXME: fd_lookup extends life of fd till the execution
                  of wb_utimens_cbk
                */
                iter_fd = fd_lookup (loc->inode, frame->root->pid);
                if (iter_fd) {
                        if (!fd_ctx_get (iter_fd, this, &tmp_file)) {
                                file = (wb_file_t *)(long)tmp_file;
                        } else {
                                fd_unref (iter_fd);
                        }
                }

        }

        local->file = file;

        if (file) {
                stub = fop_setattr_stub (frame, wb_setattr_helper, loc, stbuf,
                                         valid);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_setattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setattr, loc, stbuf,
                            valid);
        }

        return 0;
unwind:
        STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL);

        if (stub) {
                call_stub_destroy (stub);
        }
out:
        return 0;
}


int32_t
wb_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd)
{
        int32_t     wbflags = 0, flags = 0;
        wb_file_t  *file    = NULL;
        wb_conf_t  *conf    = NULL;
        wb_local_t *local   = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, this, out, op_errno,
                                        EINVAL);

        conf = this->private;

        local = frame->local;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        EINVAL);

        flags = local->flags;
        wbflags = local->wbflags;

        if (op_ret != -1) {
                file = wb_file_create (this, fd, flags);
                if (file == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                /* If O_DIRECT then, we disable chaching */
                if (((flags & O_DIRECT) == O_DIRECT)
                    || ((flags & O_ACCMODE) == O_RDONLY)
                    || (((flags & O_SYNC) == O_SYNC)
                        && conf->enable_O_SYNC == _gf_true)) {
                        file->window_conf = 0;
                }

                if (wbflags & GF_OPEN_NOWB) {
                        file->disabled = 1;
                }

                LOCK_INIT (&file->lock);
        }

out:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
        return 0;
}


int32_t
wb_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        wb_local_t *local    = NULL;
        int32_t     op_errno = EINVAL;

        local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->flags = flags;
        local->wbflags = wbflags;

        frame->local = local;

        STACK_WIND (frame, wb_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);
        return 0;

unwind:
        STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
wb_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent)
{
        long       flags = 0;
        wb_file_t *file  = NULL;
        wb_conf_t *conf  = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, this, out,
                                        op_errno, EINVAL);

        conf = this->private;
        if (op_ret != -1) {
                if (frame->local) {
                        flags = (long) frame->local;
                }

                file = wb_file_create (this, fd, flags);
                if (file == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                /* If O_DIRECT then, we disable chaching */
                if (frame->local) {
                        if (((flags & O_DIRECT) == O_DIRECT)
                            || ((flags & O_ACCMODE) == O_RDONLY)
                            || (((flags & O_SYNC) == O_SYNC)
                                && (conf->enable_O_SYNC == _gf_true))) {
                                file->window_conf = 0;
                        }
                }

                LOCK_INIT (&file->lock);
        }

        frame->local = NULL;

out:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);
        return 0;
}


int32_t
wb_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd, dict_t *params)
{
        int32_t op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        frame->local = (void *)(long)flags;

        STACK_WIND (frame, wb_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd, params);
        return 0;

unwind:
        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);
        return 0;
}


/* Mark all the contiguous write requests for winding starting from head of
 * request list. Stops marking at the first non-write request found. If
 * file is opened with O_APPEND, make sure all the writes marked for winding
 * will fit into a single write call to server.
 */
size_t
__wb_mark_wind_all (wb_file_t *file, list_head_t *list, list_head_t *winds)
{
        wb_request_t *request         = NULL;
        size_t        size            = 0;
        char          first_request   = 1;
        off_t         offset_expected = 0;
        wb_conf_t    *conf            = NULL;
        int           count           = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", file, out);
        GF_VALIDATE_OR_GOTO (file->this->name, list, out);
        GF_VALIDATE_OR_GOTO (file->this->name, winds, out);

        conf = file->this->private;

        list_for_each_entry (request, list, list)
        {
                if ((request->stub == NULL)
                    || (request->stub->fop != GF_FOP_WRITE)) {
                        break;
                }

                if (!request->flags.write_request.stack_wound) {
                        if (first_request) {
                                first_request = 0;
                                offset_expected
                                        = request->stub->args.writev.off;
                        }

                        if (request->stub->args.writev.off != offset_expected) {
                                break;
                        }

                        if ((file->flags & O_APPEND)
                            && (((size + request->write_size)
                                 > conf->aggregate_size)
                                || ((count + request->stub->args.writev.count)
                                    > MAX_VECTOR_COUNT))) {
                                break;
                        }

                        size += request->write_size;
                        offset_expected += request->write_size;
                        file->aggregate_current -= request->write_size;
                        count += request->stub->args.writev.count;

                        request->flags.write_request.stack_wound = 1;
                        list_add_tail (&request->winds, winds);
                }
        }

out:
        return size;
}


int32_t
__wb_can_wind (list_head_t *list, char *other_fop_in_queue,
               char *non_contiguous_writes, char *incomplete_writes,
               char *wind_all)
{
        wb_request_t *request         = NULL;
        char          first_request   = 1;
        off_t         offset_expected = 0;
        int32_t       ret             = -1;

        GF_VALIDATE_OR_GOTO ("write-behind", list, out);

        list_for_each_entry (request, list, list)
        {
                if ((request->stub == NULL)
                    || (request->stub->fop != GF_FOP_WRITE)) {
                        if (request->stub && other_fop_in_queue) {
                                *other_fop_in_queue = 1;
                        }
                        break;
                }

                if (request->flags.write_request.stack_wound
                    && !request->flags.write_request.got_reply
                    && (incomplete_writes != NULL)) {
                        *incomplete_writes = 1;
                        break;
                }

                if (!request->flags.write_request.stack_wound) {
                        if (first_request) {
                                char flush = 0;
                                first_request = 0;
                                offset_expected
                                        = request->stub->args.writev.off;

                                flush = request->flags.write_request.flush_all;
                                if (wind_all != NULL) {
                                        *wind_all = flush;
                                }
                        }

                        if (offset_expected != request->stub->args.writev.off) {
                                if (non_contiguous_writes) {
                                        *non_contiguous_writes = 1;
                                }
                                break;
                        }

                        offset_expected += request->write_size;
                }
        }

        ret = 0;
out:
        return ret;
}


ssize_t
__wb_mark_winds (list_head_t *list, list_head_t *winds, size_t aggregate_conf,
                 char enable_trickling_writes)
{
        size_t        size                   = 0;
        char          other_fop_in_queue     = 0;
        char          incomplete_writes      = 0;
        char          non_contiguous_writes  = 0;
        wb_request_t *request                = NULL;
        wb_file_t    *file                   = NULL;
        char          wind_all               = 0;
        int32_t       ret                    = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", list, out);
        GF_VALIDATE_OR_GOTO ("write-behind", winds, out);

        if (list_empty (list)) {
                goto out;
        }

        request = list_entry (list->next, typeof (*request), list);
        file = request->file;

        ret = __wb_can_wind (list, &other_fop_in_queue,
                             &non_contiguous_writes, &incomplete_writes,
                             &wind_all);
        if (ret == -1) {
                gf_log (file->this->name, GF_LOG_WARNING,
                        "cannot decide whether to wind or not");
                goto out;
        }

        if (!incomplete_writes && ((enable_trickling_writes)
                                   || (wind_all) || (non_contiguous_writes)
                                   || (other_fop_in_queue)
                                   || (file->aggregate_current
                                       >= aggregate_conf))) {
                size = __wb_mark_wind_all (file, list, winds);
        }

out:
        return size;
}


size_t
__wb_mark_unwind_till (list_head_t *list, list_head_t *unwinds, size_t size)
{
        size_t        written_behind = 0;
        wb_request_t *request        = NULL;
        wb_file_t    *file           = NULL;

        if (list_empty (list)) {
                goto out;
        }

        request = list_entry (list->next, typeof (*request), list);
        file = request->file;

        list_for_each_entry (request, list, list)
        {
                if ((request->stub == NULL)
                    || (request->stub->fop != GF_FOP_WRITE)) {
                        continue;
                }

                if (written_behind <= size) {
                        if (!request->flags.write_request.write_behind) {
                                written_behind += request->write_size;
                                request->flags.write_request.write_behind = 1;
                                list_add_tail (&request->unwinds, unwinds);

                                if (!request->flags.write_request.got_reply) {
                                        file->window_current
                                                += request->write_size;
                                }
                        }
                } else {
                        break;
                }
        }

out:
        return written_behind;
}


void
__wb_mark_unwinds (list_head_t *list, list_head_t *unwinds)
{
        wb_request_t *request = NULL;
        wb_file_t    *file    = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", list, out);
        GF_VALIDATE_OR_GOTO ("write-behind", unwinds, out);

        if (list_empty (list)) {
                goto out;
        }

        request = list_entry (list->next, typeof (*request), list);
        file = request->file;

        if (file->window_current <= file->window_conf) {
                __wb_mark_unwind_till (list, unwinds,
                                       file->window_conf
                                       - file->window_current);
        }

out:
        return;
}


uint32_t
__wb_get_other_requests (list_head_t *list, list_head_t *other_requests)
{
        wb_request_t *request = NULL;
        uint32_t      count   = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", list, out);
        GF_VALIDATE_OR_GOTO ("write-behind", other_requests, out);

        list_for_each_entry (request, list, list) {
                if ((request->stub == NULL)
                    || (request->stub->fop == GF_FOP_WRITE)) {
                        break;
                }

                if (!request->flags.other_requests.marked_for_resume) {
                        request->flags.other_requests.marked_for_resume = 1;
                        list_add_tail (&request->other_requests,
                                       other_requests);
                        count++;
                }
        }

out:
        return count;
}


int32_t
wb_stack_unwind (list_head_t *unwinds)
{
        struct iatt   buf     = {0,};
        wb_request_t *request = NULL, *dummy = NULL;
        call_frame_t *frame   = NULL;
        wb_local_t   *local   = NULL;
        int           ret     = 0, write_requests_removed = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", unwinds, out);

        list_for_each_entry_safe (request, dummy, unwinds, unwinds) {
                frame = request->stub->frame;
                local = frame->local;

                STACK_UNWIND (frame, local->op_ret, local->op_errno, &buf,
                              &buf);

                ret = wb_request_unref (request);
                if (ret == 0) {
                        write_requests_removed++;
                }
        }

out:
        return write_requests_removed;
}


int32_t
wb_resume_other_requests (call_frame_t *frame, wb_file_t *file,
                          list_head_t *other_requests)
{
        int32_t       ret          = -1;
        wb_request_t *request      = NULL, *dummy = NULL;
        int32_t       fops_removed = 0;
        char          wind         = 0;
        call_stub_t  *stub         = NULL;

        GF_VALIDATE_OR_GOTO ((file ? file->this->name : "write-behind"), frame,
                             out);
        GF_VALIDATE_OR_GOTO (frame->this->name, file, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, other_requests, out);

        if (list_empty (other_requests)) {
                ret = 0;
                goto out;
        }

        list_for_each_entry_safe (request, dummy, other_requests,
                                  other_requests) {
                wind = request->stub->wind;
                stub = request->stub;

                LOCK (&file->lock);
                {
                        request->stub = NULL;
                }
                UNLOCK (&file->lock);

                if (!wind) {
                        wb_request_unref (request);
                        fops_removed++;
                }

                call_resume (stub);
        }

        ret = 0;

        if (fops_removed > 0) {
                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (frame->this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        }

out:
        return ret;
}


int32_t
wb_do_ops (call_frame_t *frame, wb_file_t *file, list_head_t *winds,
           list_head_t *unwinds, list_head_t *other_requests)
{
        int32_t ret = -1, write_requests_removed = 0;

        GF_VALIDATE_OR_GOTO ((file ? file->this->name : "write-behind"),
                             frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, file, out);

        ret = wb_stack_unwind (unwinds);

        write_requests_removed = ret;

        ret = wb_sync (frame, file, winds);
        if (ret == -1) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "syncing of write requests failed");
        }

        ret = wb_resume_other_requests (frame, file, other_requests);
        if (ret == -1) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "cannot resume non-write requests in request queue");
        }

        /* wb_stack_unwind does wb_request_unref after unwinding a write
         * request. Hence if a write-request was just freed in wb_stack_unwind,
         * we have to process request queue once again to unblock requests
         * blocked on the writes just unwound.
         */
        if (write_requests_removed > 0) {
                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (frame->this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        }

out:
        return ret;
}


inline int
__wb_copy_into_holder (wb_request_t *holder, wb_request_t *request)
{
        char          *ptr    = NULL;
        struct iobuf  *iobuf  = NULL;
        struct iobref *iobref = NULL;
        int            ret    = -1;

        if (holder->flags.write_request.virgin) {
                iobuf = iobuf_get (request->file->this->ctx->iobuf_pool);
                if (iobuf == NULL) {
                        goto out;
                }

                iobref = iobref_new ();
                if (iobref == NULL) {
                        iobuf_unref (iobuf);
                        goto out;
                }

                ret = iobref_add (iobref, iobuf);
                if (ret != 0) {
                        iobuf_unref (iobuf);
                        iobref_unref (iobref);
                        gf_log (request->file->this->name, GF_LOG_WARNING,
                                "cannot add iobuf (%p) into iobref (%p)",
                                iobuf, iobref);
                        goto out;
                }

                iov_unload (iobuf->ptr, holder->stub->args.writev.vector,
                            holder->stub->args.writev.count);
                holder->stub->args.writev.vector[0].iov_base = iobuf->ptr;

                iobref_unref (holder->stub->args.writev.iobref);
                holder->stub->args.writev.iobref = iobref;

                iobuf_unref (iobuf);

                holder->flags.write_request.virgin = 0;
        }

        ptr = holder->stub->args.writev.vector[0].iov_base + holder->write_size;

        iov_unload (ptr, request->stub->args.writev.vector,
                    request->stub->args.writev.count);

        holder->stub->args.writev.vector[0].iov_len += request->write_size;
        holder->write_size += request->write_size;

        request->flags.write_request.stack_wound = 1;
        list_move_tail (&request->list, &request->file->passive_requests);

        ret = 0;
out:
        return ret;
}


/* this procedure assumes that write requests have only one vector to write */
void
__wb_collapse_write_bufs (list_head_t *requests, size_t page_size)
{
        off_t         offset_expected = 0;
        size_t        space_left      = 0;
        wb_request_t *request         = NULL, *tmp = NULL, *holder = NULL;
        int           ret             = 0;

        GF_VALIDATE_OR_GOTO ("write-behind", requests, out);

        list_for_each_entry_safe (request, tmp, requests, list) {
                if ((request->stub == NULL)
                    || (request->stub->fop != GF_FOP_WRITE)
                    || (request->flags.write_request.stack_wound)) {
                        holder = NULL;
                        continue;
                }

                if (request->flags.write_request.write_behind) {
                        if (holder == NULL) {
                                holder = request;
                                continue;
                        }

                        offset_expected = holder->stub->args.writev.off
                                + holder->write_size;

                        if (request->stub->args.writev.off != offset_expected) {
                                holder = request;
                                continue;
                        }

                        space_left = page_size - holder->write_size;

                        if (space_left >= request->write_size) {
                                ret = __wb_copy_into_holder (holder, request);
                                if (ret != 0) {
                                        break;
                                }

                                __wb_request_unref (request);
                        } else {
                                holder = request;
                        }
                } else {
                        break;
                }
        }

out:
        return;
}


int32_t
wb_process_queue (call_frame_t *frame, wb_file_t *file)
{
        list_head_t winds  = {0, }, unwinds = {0, }, other_requests = {0, };
        size_t      size   = 0;
        wb_conf_t  *conf   = NULL;
        uint32_t    count  = 0;
        int32_t     ret    = -1;

        INIT_LIST_HEAD (&winds);
        INIT_LIST_HEAD (&unwinds);
        INIT_LIST_HEAD (&other_requests);

        GF_VALIDATE_OR_GOTO ((file ? file->this->name : "write-behind"), frame,
                             out);
        GF_VALIDATE_OR_GOTO (file->this->name, frame, out);

        conf = file->this->private;
        GF_VALIDATE_OR_GOTO (file->this->name, conf, out);

        size = conf->aggregate_size;
        LOCK (&file->lock);
        {
                /*
                 * make sure requests are marked for unwinding and adjacent
                 * continguous write buffers (each of size less than that of
                 * an iobuf) are packed properly so that iobufs are filled to
                 * their maximum capacity, before calling __wb_mark_winds.
                 */
                __wb_mark_unwinds (&file->request, &unwinds);

                __wb_collapse_write_bufs (&file->request,
                                          file->this->ctx->page_size);

                count = __wb_get_other_requests (&file->request,
                                                 &other_requests);

                if (count == 0) {
                        __wb_mark_winds (&file->request, &winds, size,
                                         conf->enable_trickling_writes);
                }

        }
        UNLOCK (&file->lock);

        ret = wb_do_ops (frame, file, &winds, &unwinds, &other_requests);

out:
        return ret;
}


int32_t
wb_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf)
{
        GF_ASSERT (frame);

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int32_t
wb_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, struct iobref *iobref)
{
        wb_file_t    *file          = NULL;
        char          wb_disabled   = 0;
        call_frame_t *process_frame = NULL;
        size_t        size          = 0;
        uint64_t      tmp_file      = 0;
        call_stub_t  *stub          = NULL;
        wb_local_t   *local         = NULL;
        wb_request_t *request       = NULL;
        int32_t       ret           = -1;
        int32_t       op_ret        = -1, op_errno = EINVAL;

        GF_ASSERT (frame);

        GF_VALIDATE_OR_GOTO_WITH_ERROR ("write-behind", this, unwind, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, fd, unwind, op_errno,
                                        EINVAL);

        if (vector != NULL)
                size = iov_length (vector, count);

        if ((!IA_ISDIR (fd->inode->ia_type))
            && fd_ctx_get (fd, this, &tmp_file)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "write behind file pointer is"
                        " not stored in context of fd(%p), returning EBADFD",
                        fd);

                op_errno = EBADFD;
                goto unwind;
        }

        file = (wb_file_t *)(long)tmp_file;
        if ((!IA_ISDIR (fd->inode->ia_type)) && (file == NULL)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "wb_file not found for fd %p", fd);
                op_errno = EBADFD;
                goto unwind;
        }

        if (file != NULL) {
                LOCK (&file->lock);
                {
                        op_ret = file->op_ret;
                        op_errno = file->op_errno;

                        file->op_ret = 0;

                        if ((op_ret == 0)
                            && (file->disabled || file->disable_till)) {
                                if (size > file->disable_till) {
                                        file->disable_till = 0;
                                } else {
                                        file->disable_till -= size;
                                }
                                wb_disabled = 1;
                        }
                }
                UNLOCK (&file->lock);
        } else {
                wb_disabled = 1;
        }

        if (op_ret == -1) {
                STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, NULL,
                                     NULL);
                return 0;
        }

        if (wb_disabled) {
                STACK_WIND (frame, wb_writev_cbk, FIRST_CHILD (frame->this),
                            FIRST_CHILD (frame->this)->fops->writev,
                            fd, vector, count, offset, iobref);
                return 0;
        }

        process_frame = copy_frame (frame);
        if (process_frame == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local = GF_CALLOC (1, sizeof (*local),
                           gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        frame->local = local;
        local->file = file;

        stub = fop_writev_stub (frame, NULL, fd, vector, count, offset, iobref);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        request = wb_enqueue (file, stub);
        if (request == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        ret = wb_process_queue (process_frame, file);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "request queue processing failed");
        }

        STACK_DESTROY (process_frame->root);

        return 0;

unwind:
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);

        if (process_frame) {
                STACK_DESTROY (process_frame->root);
        }

        if (stub) {
                call_stub_destroy (stub);
        }

        return 0;
}


int32_t
wb_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct iatt *stbuf, struct iobref *iobref)
{
        wb_local_t   *local   = NULL;
        wb_file_t    *file    = NULL;
        wb_request_t *request = NULL;
        int32_t       ret     = 0;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;
        request = local->request;

        if ((request != NULL) && (file != NULL)) {
                wb_request_unref (request);

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        if (errno == ENOMEM) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                        }

                        gf_log (frame->this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        }

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref);

        return 0;
}


static int32_t
wb_readv_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset)
{
        STACK_WIND (frame, wb_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset);

        return 0;
}


int32_t
wb_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
{
        wb_file_t    *file     = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        int32_t       ret      = -1, op_errno = 0;
        wb_request_t *request  = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, this, unwind,
                                        op_errno, EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, fd, unwind, op_errno,
                                        EINVAL);

        if ((!IA_ISDIR (fd->inode->ia_type))
            && fd_ctx_get (fd, this, &tmp_file)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "write behind file pointer is"
                        " not stored in context of fd(%p), returning EBADFD",
                        fd);
                op_errno = EBADFD;
                goto unwind;
        }

        file = (wb_file_t *)(long)tmp_file;

        local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->file = file;

        frame->local = local;
        if (file) {
                stub = fop_readv_stub (frame, wb_readv_helper, fd, size,
                                       offset);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        call_stub_destroy (stub);
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_readv_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readv,
                            fd, size, offset);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}


int32_t
wb_ffr_bg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno)
{
        STACK_DESTROY (frame->root);
        return 0;
}


int32_t
wb_ffr_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno)
{
        wb_local_t *local = NULL;
        wb_file_t  *file  = NULL;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;

        if (file != NULL) {
                LOCK (&file->lock);
                {
                        if (file->op_ret == -1) {
                                op_ret = file->op_ret;
                                op_errno = file->op_errno;

                                file->op_ret = 0;
                        }
                }
                UNLOCK (&file->lock);
        }

        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);

        return 0;
}


int32_t
wb_flush_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        wb_conf_t    *conf        = NULL;
        wb_local_t   *local       = NULL;
        wb_file_t    *file        = NULL;
        call_frame_t *flush_frame = NULL, *process_frame = NULL;
        int32_t       op_ret      = -1, op_errno = -1, ret = -1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, this, unwind,
                                        op_errno, EINVAL);

        conf = this->private;

        local = frame->local;
        file = local->file;

        LOCK (&file->lock);
        {
                op_ret = file->op_ret;
                op_errno = file->op_errno;
        }
        UNLOCK (&file->lock);

        if (local && local->request) {
                process_frame = copy_frame (frame);
                if (process_frame == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                wb_request_unref (local->request);
        }

        if (conf->flush_behind) {
                flush_frame = copy_frame (frame);
                if (flush_frame == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                STACK_WIND (flush_frame, wb_ffr_bg_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush, fd);
        } else {
                STACK_WIND (frame, wb_ffr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush, fd);
        }

        if (process_frame != NULL) {
                ret = wb_process_queue (process_frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }

                STACK_DESTROY (process_frame->root);
        }

        if (conf->flush_behind) {
                STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (flush, frame, -1, op_errno);
        return 0;
}


int32_t
wb_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        wb_conf_t    *conf        = NULL;
        wb_file_t    *file        = NULL;
        wb_local_t   *local       = NULL;
        uint64_t      tmp_file    = 0;
        call_stub_t  *stub        = NULL;
        call_frame_t *flush_frame = NULL;
        wb_request_t *request     = NULL;
        int32_t       ret         = 0, op_errno = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, this, unwind,
                                        op_errno, EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, fd, unwind, op_errno,
                                        EINVAL);

        conf = this->private;

        if ((!IA_ISDIR (fd->inode->ia_type))
            && fd_ctx_get (fd, this, &tmp_file)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "write behind file pointer is"
                        " not stored in context of fd(%p), returning EBADFD",
                        fd);

                op_errno = EBADFD;
                goto unwind;
        }

        file = (wb_file_t *)(long)tmp_file;

        if (file != NULL) {
                local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
                if (local == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                local->file = file;

                frame->local = local;

                stub = fop_flush_stub (frame, wb_flush_helper, fd);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        call_stub_destroy (stub);
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                if (conf->flush_behind) {
                        flush_frame = copy_frame (frame);
                        if (flush_frame == NULL) {
                                op_errno = ENOMEM;
                                goto unwind;
                        }

                        STACK_UNWIND_STRICT (flush, frame, 0, 0);

                        STACK_WIND (flush_frame, wb_ffr_bg_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->flush, fd);
                } else {
                        STACK_WIND (frame, wb_ffr_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->flush, fd);
                }
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (flush, frame, -1, op_errno);
        return 0;
}


static int32_t
wb_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf)
{
        wb_local_t   *local   = NULL;
        wb_file_t    *file    = NULL;
        wb_request_t *request = NULL;
        int32_t       ret     = -1;

        GF_ASSERT (frame);

        local = frame->local;
        file = local->file;
        request = local->request;

        if (file != NULL) {
                LOCK (&file->lock);
                {
                        if (file->op_ret == -1) {
                                op_ret = file->op_ret;
                                op_errno = file->op_errno;

                                file->op_ret = 0;
                        }
                }
                UNLOCK (&file->lock);

                if (request) {
                        wb_request_unref (request);
                        ret = wb_process_queue (frame, file);
                        if (ret == -1) {
                                if (errno == ENOMEM) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                }

                                gf_log (this->name, GF_LOG_WARNING,
                                        "request queue processing failed");
                        }
                }

        }

        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


static int32_t
wb_fsync_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 int32_t datasync)
{
        STACK_WIND (frame, wb_fsync_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, datasync);
        return 0;
}


int32_t
wb_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync)
{
        wb_file_t    *file     = NULL;
        wb_local_t   *local    = NULL;
        uint64_t      tmp_file = 0;
        call_stub_t  *stub     = NULL;
        wb_request_t *request  = NULL;
        int32_t       ret      = -1, op_errno = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, this, unwind,
                                        op_errno, EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (frame->this->name, fd, unwind,
                                        op_errno, EINVAL);

        if ((!IA_ISDIR (fd->inode->ia_type))
            && fd_ctx_get (fd, this, &tmp_file)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "write behind file pointer is"
                        " not stored in context of fd(%p), returning EBADFD",
                        fd);
                op_errno = EBADFD;
                goto unwind;
        }

        file = (wb_file_t *)(long)tmp_file;

        local = GF_CALLOC (1, sizeof (*local), gf_wb_mt_wb_local_t);
        if (local == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        local->file = file;

        frame->local = local;

        if (file) {
                stub = fop_fsync_stub (frame, wb_fsync_helper, fd, datasync);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                request = wb_enqueue (file, stub);
                if (request == NULL) {
                        op_errno = ENOMEM;
                        call_stub_destroy (stub);
                        goto unwind;
                }

                ret = wb_process_queue (frame, file);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "request queue processing failed");
                }
        } else {
                STACK_WIND (frame, wb_fsync_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsync, fd, datasync);
        }

        return 0;

unwind:
        STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
wb_release (xlator_t *this, fd_t *fd)
{
        uint64_t   file_ptr = 0;
        wb_file_t *file     = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        fd_ctx_get (fd, this, &file_ptr);
        file = (wb_file_t *) (long) file_ptr;

        if (file != NULL) {
                LOCK (&file->lock);
                {
                        GF_ASSERT (list_empty (&file->request));
                }
                UNLOCK (&file->lock);

                wb_file_destroy (file);
        }

out:
        return 0;
}


int
wb_priv_dump (xlator_t *this)
{
        wb_conf_t      *conf                            = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char            key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        int             ret                             = -1;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        gf_proc_dump_build_key (key_prefix, "xlator.performance.write-behind",
                                "priv");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_build_key (key, key_prefix, "aggregate_size");
        gf_proc_dump_write (key, "%d", conf->aggregate_size);
        gf_proc_dump_build_key (key, key_prefix, "window_size");
        gf_proc_dump_write (key, "%d", conf->window_size);
        gf_proc_dump_build_key (key, key_prefix, "disable_till");
        gf_proc_dump_write (key, "%d", conf->disable_till);
        gf_proc_dump_build_key (key, key_prefix, "enable_O_SYNC");
        gf_proc_dump_write (key, "%d", conf->enable_O_SYNC);
        gf_proc_dump_build_key (key, key_prefix, "flush_behind");
        gf_proc_dump_write (key, "%d", conf->flush_behind);
        gf_proc_dump_build_key (key, key_prefix, "enable_trickling_writes");
        gf_proc_dump_write (key, "%d", conf->enable_trickling_writes);

        ret = 0;
out:
        return ret;
}


void
__wb_dump_requests (struct list_head *head, char *prefix, char passive)
{
        char          key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char          key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, }, flag = 0;
        wb_request_t *request                         = NULL;

        list_for_each_entry (request, head, list) {
                gf_proc_dump_build_key (key, prefix, passive ? "passive-request"
                                        : "active-request");
                gf_proc_dump_build_key (key_prefix, key,
                                        gf_fop_list[request->fop]);

                gf_proc_dump_add_section(key_prefix);

                gf_proc_dump_build_key (key, key_prefix, "request-ptr");
                gf_proc_dump_write (key, "%p", request);

                gf_proc_dump_build_key (key, key_prefix, "refcount");
                gf_proc_dump_write (key, "%d", request->refcount);

                if (request->fop == GF_FOP_WRITE) {
                        flag = request->flags.write_request.stack_wound;
                        gf_proc_dump_build_key (key, key_prefix, "stack_wound");
                        gf_proc_dump_write (key, "%d", flag);

                        gf_proc_dump_build_key (key, key_prefix, "size");
                        gf_proc_dump_write (key, "%"GF_PRI_SIZET,
                                            request->write_size);

                        gf_proc_dump_build_key (key, key_prefix, "offset");
                        gf_proc_dump_write (key, "%"PRId64,
                                            request->stub->args.writev.off);

                        flag = request->flags.write_request.write_behind;
                        gf_proc_dump_build_key (key, key_prefix,
                                                "write_behind");
                        gf_proc_dump_write (key, "%d", flag);

                        flag = request->flags.write_request.got_reply;
                        gf_proc_dump_build_key (key, key_prefix, "got_reply");
                        gf_proc_dump_write (key, "%d", flag);

                        flag = request->flags.write_request.virgin;
                        gf_proc_dump_build_key (key, key_prefix, "virgin");
                        gf_proc_dump_write (key, "%d", flag);

                        flag = request->flags.write_request.flush_all;
                        gf_proc_dump_build_key (key, key_prefix, "flush_all");
                        gf_proc_dump_write (key, "%d", flag);
                } else {
                        flag = request->flags.other_requests.marked_for_resume;
                        gf_proc_dump_build_key (key, key_prefix,
                                                "marked_for_resume");
                        gf_proc_dump_write (key, "%d", flag);
                }
        }
}


int
wb_file_dump (xlator_t *this, fd_t *fd)
{
        wb_file_t *file                            = NULL;
        uint64_t   tmp_file                        = 0;
        int32_t    ret                             = -1;
        char       key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char       key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };

        if ((fd == NULL) || (this == NULL)) {
                ret = 0;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &tmp_file);
        if (ret == -1) {
                ret = 0;
                goto out;
        }

        file = (wb_file_t *)(long)tmp_file;
        if (file == NULL) {
                ret = 0;
                goto out;
        }

        gf_proc_dump_build_key (key_prefix, "xlator.performance.write-behind",
                                "file");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_build_key (key, key_prefix, "fd");
        gf_proc_dump_write (key, "%p", fd);

        gf_proc_dump_build_key (key, key_prefix, "disabled");
        gf_proc_dump_write (key, "%d", file->disabled);

        gf_proc_dump_build_key (key, key_prefix, "disable_till");
        gf_proc_dump_write (key, "%lu", file->disable_till);

        gf_proc_dump_build_key (key, key_prefix, "window_conf");
        gf_proc_dump_write (key, "%"GF_PRI_SIZET, file->window_conf);

        gf_proc_dump_build_key (key, key_prefix, "window_current");
        gf_proc_dump_write (key, "%"GF_PRI_SIZET, file->window_current);

        gf_proc_dump_build_key (key, key_prefix, "flags");
        gf_proc_dump_write (key, "%s", (file->flags & O_APPEND) ? "O_APPEND"
                            : "!O_APPEND");

        gf_proc_dump_build_key (key, key_prefix, "aggregate_current");
        gf_proc_dump_write (key, "%"GF_PRI_SIZET, file->aggregate_current);

        gf_proc_dump_build_key (key, key_prefix, "refcount");
        gf_proc_dump_write (key, "%d", file->refcount);

        gf_proc_dump_build_key (key, key_prefix, "op_ret");
        gf_proc_dump_write (key, "%d", file->op_ret);

        gf_proc_dump_build_key (key, key_prefix, "op_errno");
        gf_proc_dump_write (key, "%d", file->op_errno);

        LOCK (&file->lock);
        {
                if (!list_empty (&file->request)) {
                        __wb_dump_requests (&file->request, key_prefix, 0);
                }

                if (!list_empty (&file->passive_requests)) {
                        __wb_dump_requests (&file->passive_requests, key_prefix,
                                            1);
                }
        }
        UNLOCK (&file->lock);

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

        ret = xlator_mem_acct_init (this, gf_wb_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
        }

out:
        return ret;
}


int
validate_options (xlator_t *this, dict_t *options, char **op_errstr)
{
        char         *str          = NULL;
        uint64_t      window_size  = 0;;
        gf_boolean_t  flush_behind = 0;
        int           ret          = 0;

        ret = dict_get_str (options, "cache-size", &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &window_size);
                if (ret != 0) {
                        gf_log(this->name, GF_LOG_WARNING, "Validation"
                               "'option cache-size %s failed , Invalid"
                               " number format, ", str);
                        *op_errstr = gf_strdup ("Error, Invalid num format");
                        ret = -1;
                        goto out;
                }

                if (window_size < (512 * GF_UNIT_KB)) {
                        gf_log(this->name, GF_LOG_WARNING, "Validation"
                               "'option cache-size %s' failed , Min value"
                               "should be 512KiB ", str);
                        *op_errstr = gf_strdup ("Error, Should be min 512KB");
                        ret = -1;
                        goto out;
                }

                if (window_size > (1 * GF_UNIT_GB)) {
                        gf_log(this->name, GF_LOG_WARNING, "Reconfiguration"
                               "'option cache-size %s' failed , Max value"
                               "can be 1 GiB", str);
                        *op_errstr = gf_strdup ("Error, Max Value is 1GB");
                        ret = -1;
                        goto out;
                }

                gf_log(this->name, GF_LOG_WARNING,
                       "validated 'option cache-size %s '", str);
        }

        ret = dict_get_str (options, "flush-behind", &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, &flush_behind);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "'flush-behind' takes only boolean arguments");
                        *op_errstr = gf_strdup ("Error, should be boolean");
                        ret = -1;
                        goto out;
                }
        }

        ret =0;
out:
        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
        char      *str         = NULL;
        uint64_t   window_size = 0;
        wb_conf_t *conf        = NULL;
        int        ret         = 0;

        conf = this->private;

        ret = dict_get_str (options, "cache-size", &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &window_size);
                if (ret != 0) {
                        gf_log(this->name, GF_LOG_ERROR, "Reconfiguration"
                               "'option cache-size %s failed , Invalid"
                               " number format, Defaulting to old value "
                               "(%"PRIu64")", str, conf->window_size);
                        ret = -1;
                        goto out;
                }

                if (window_size < (512 * GF_UNIT_KB)) {
                        gf_log(this->name, GF_LOG_ERROR, "Reconfiguration"
                               "'option cache-size %s' failed , Max value"
                               "can be 512KiB, Defaulting to old value "
                               "(%"PRIu64")", str, conf->window_size);
                        ret = -1;
                        goto out;
                }

                if (window_size > (2 * GF_UNIT_GB)) {
                        gf_log(this->name, GF_LOG_ERROR, "Reconfiguration"
                               "'option cache-size %s' failed , Max value"
                               "can be 1 GiB, Defaulting to old value "
                               "(%"PRIu64")", str, conf->window_size);
                        ret = -1;
                        goto out;
                }

                conf->window_size = window_size;
                gf_log(this->name, GF_LOG_WARNING, "Reconfiguring "
                       "'option cache-size %s ' to %"PRIu64, str,
                       conf->window_size);
        } else {
                conf->window_size = WB_WINDOW_SIZE;
        }

        ret = dict_get_str (options, "flush-behind", &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, &conf->flush_behind);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'flush-behind' takes only boolean arguments");
                        conf->flush_behind = 1;
                        return -1;
                }

                if (conf->flush_behind) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "enabling flush-behind");
                } else {
                        gf_log (this->name, GF_LOG_WARNING,
                                "disabling flush-behind");
                }
        }

out:
        return 0;
}


int32_t
init (xlator_t *this)
{
        dict_t    *options = NULL;
        wb_conf_t *conf    = NULL;
        char      *str     = NULL;
        int32_t    ret     = -1;

        if ((this->children == NULL)
            || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: write-behind (%s) not configured with exactly "
                        "one child", this->name);
                goto out;
        }

        if (this->parents == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile");
        }

        options = this->options;

        conf = GF_CALLOC (1, sizeof (*conf), gf_wb_mt_wb_conf_t);
        if (conf == NULL) {
                goto out;
        }

        conf->enable_O_SYNC = _gf_false;
        ret = dict_get_str (options, "enable-O_SYNC", &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, &conf->enable_O_SYNC);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'enable-O_SYNC' takes only boolean arguments");
                        goto out;
                }
        }

        /* configure 'options aggregate-size <size>' */
        conf->aggregate_size = WB_AGGREGATE_SIZE;
        conf->disable_till = 0;
        ret = dict_get_str (options, "disable-for-first-nbytes", &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &conf->disable_till);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid number format \"%s\" of \"option "
                                "disable-for-first-nbytes\"",
                                str);
                        goto out;
                }
        }

        gf_log (this->name, GF_LOG_WARNING,
                "disabling write-behind for first %"PRIu64" bytes",
                conf->disable_till);

        /* configure 'option window-size <size>' */
        conf->window_size = WB_WINDOW_SIZE;
        ret = dict_get_str (options, "cache-size", &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &conf->window_size);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid number format \"%s\" of \"option "
                                "window-size\"", str);
                        GF_FREE (conf);
                        goto out;
                }
        }

        if (!conf->window_size && conf->aggregate_size) {
                gf_log (this->name, GF_LOG_WARNING,
                        "setting window-size to be equal to "
                        "aggregate-size(%"PRIu64")",
                        conf->aggregate_size);
                conf->window_size = conf->aggregate_size;
        }

        if (conf->window_size < conf->aggregate_size) {
                gf_log (this->name, GF_LOG_ERROR,
                        "aggregate-size(%"PRIu64") cannot be more than "
                        "window-size(%"PRIu64")", conf->aggregate_size,
                        conf->window_size);
                GF_FREE (conf);
                goto out;
        }

        /* configure 'option flush-behind <on/off>' */
        conf->flush_behind = 1;
        ret = dict_get_str (options, "flush-behind", &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, &conf->flush_behind);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'flush-behind' takes only boolean arguments");
                        goto out;
                }

                if (conf->flush_behind) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "enabling flush-behind");
                }
        }

        conf->enable_trickling_writes = _gf_true;
        ret = dict_get_str (options, "enable-trickling-writes", &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, &conf->enable_trickling_writes);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'enable-trickling_writes' takes only boolean"
                                " arguments");
                        goto out;
                }
        }

        this->private = conf;
        ret = 0;

out:
        return ret;
}


void
fini (xlator_t *this)
{
        wb_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        conf = this->private;
        if (!conf) {
                goto out;
        }

        this->private = NULL;
        GF_FREE (conf);

out:
        return;
}


struct xlator_fops fops = {
        .writev      = wb_writev,
        .open        = wb_open,
        .create      = wb_create,
        .readv       = wb_readv,
        .flush       = wb_flush,
        .fsync       = wb_fsync,
        .stat        = wb_stat,
        .fstat       = wb_fstat,
        .truncate    = wb_truncate,
        .ftruncate   = wb_ftruncate,
        .setattr     = wb_setattr,
};

struct xlator_cbks cbks = {
        .release  = wb_release
};

struct xlator_dumpops dumpops = {
        .priv      =  wb_priv_dump,
        .fdctx     =  wb_file_dump,
};

struct volume_options options[] = {
        { .key  = {"flush-behind"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key  = {"cache-size", "window-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 512 * GF_UNIT_KB,
          .max  = 1 * GF_UNIT_GB
        },
        { .key = {"disable-for-first-nbytes"},
          .type = GF_OPTION_TYPE_SIZET,
          .min = 1,
          .max = 1 * GF_UNIT_MB,
        },
        { .key = {"enable-O_SYNC"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key = {"enable-trickling-writes"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key = {NULL} },
};
