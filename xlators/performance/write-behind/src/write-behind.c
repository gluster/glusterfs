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

#define MAX_VECTOR_COUNT 8
 
typedef struct list_head list_head_t;
struct wb_conf;
struct wb_page;
struct wb_file;


struct wb_conf {
        uint64_t aggregate_size;
        uint64_t window_size;
        gf_boolean_t flush_behind;
};


typedef struct wb_local {
        list_head_t winds;
        struct wb_file *file;
        list_head_t unwind_frames;
        int op_ret;
        int op_errno;
        call_frame_t *frame;
} wb_local_t;


typedef struct write_request {
        call_frame_t *frame;
        off_t offset;
        /*  int32_t op_ret;
            int32_t op_errno; */
        struct iovec *vector;
        int32_t count;
        dict_t *refs;
        char write_behind;
        char stack_wound;
        char got_reply;
        list_head_t list;
        list_head_t winds;
        /*  list_head_t unwinds;*/
} wb_write_request_t;


struct wb_file {
        int disabled;
        int disable_till;
        off_t offset;
        size_t window_size;
        int32_t refcount;
        int32_t op_ret;
        int32_t op_errno;
        list_head_t request;
        fd_t *fd;
        gf_lock_t lock;
        xlator_t *this;
};


typedef struct wb_conf wb_conf_t;
typedef struct wb_page wb_page_t;
typedef struct wb_file wb_file_t;


int32_t 
wb_process_queue (call_frame_t *frame, wb_file_t *file, char flush_all);

int32_t
wb_sync (call_frame_t *frame, wb_file_t *file, list_head_t *winds);

int32_t
wb_sync_all (call_frame_t *frame, wb_file_t *file);

int32_t 
__wb_mark_winds (list_head_t *list, list_head_t *winds, size_t aggregate_size);


wb_file_t *
wb_file_create (xlator_t *this,
                fd_t *fd)
{
        wb_file_t *file = NULL;

        file = calloc (1, sizeof (*file));
        INIT_LIST_HEAD (&file->request);

        /* fd_ref() not required, file should never decide the existance of
         * an fd */
        file->fd= fd;
        file->disable_till = 1 * GF_UNIT_MB; /* TODO: Hard coded value?? why? */
        file->this = this;
        file->refcount = 1;

        dict_set (fd->ctx, this->name, data_from_static_ptr (file));
        
        return file;
}

void
wb_file_destroy (wb_file_t *file)
{
        int32_t refcount = 0;

        LOCK (&file->lock);
        {
                refcount = --file->refcount;
        }
        UNLOCK (&file->lock);

        if (!refcount){
                LOCK_DESTROY (&file->lock);
                FREE (file);
        }

        return;
}


int32_t
wb_sync_cbk (call_frame_t *frame,
             void *cookie,
             xlator_t *this,
             int32_t op_ret,
             int32_t op_errno,
             struct stat *stbuf)
{
        wb_local_t *local = NULL;
        list_head_t *winds = NULL;
        wb_file_t *file = NULL;
        wb_write_request_t *request = NULL, *dummy = NULL;

        local = frame->local;
        winds = &local->winds;
        file = local->file;

        LOCK (&file->lock);
        {
                list_for_each_entry_safe (request, dummy, winds, winds) {
                        request->got_reply = 1;
                        if (!request->write_behind && (op_ret == -1)) {
                                wb_local_t *per_request_local = request->frame->local;
                                per_request_local->op_ret = op_ret;
                                per_request_local->op_errno = op_errno;
                        }

                        /*
                          request->op_ret = op_ret;
                          request->op_errno = op_errno; 
                        */
                }
        }
        UNLOCK (&file->lock);

        if (op_ret == -1)
        {
                file->op_ret = op_ret;
                file->op_errno = op_errno;
        }

        wb_process_queue (frame, file, 0);  
  
        /* safe place to do fd_unref */
        fd_unref (file->fd);

        STACK_DESTROY (frame->root);

        return 0;
}

int32_t
wb_sync_all (call_frame_t *frame, wb_file_t *file) 
{
        list_head_t winds;
        int32_t bytes = 0;

        INIT_LIST_HEAD (&winds);

        LOCK (&file->lock);
        {
                bytes = __wb_mark_winds (&file->request, &winds, 0);
        }
        UNLOCK (&file->lock);

        wb_sync (frame, file, &winds);

        return bytes;
}


int32_t
wb_sync (call_frame_t *frame, wb_file_t *file, list_head_t *winds)
{
        wb_write_request_t *dummy = NULL, *request = NULL, *first_request = NULL, *next = NULL;
        size_t total_count = 0, count = 0;
        size_t copied = 0;
        call_frame_t *sync_frame = NULL;
        dict_t *refs = NULL;
        wb_local_t *local = NULL;
        struct iovec *vector = NULL;
        int32_t bytes = 0;
        size_t bytecount = 0;

        list_for_each_entry (request, winds, winds)
        {
                total_count += request->count;
                bytes += iov_length (request->vector, request->count);
        }

        if (!total_count) {
                return 0;
        }
  
        list_for_each_entry_safe (request, dummy, winds, winds) {
                if (!vector) {
                        vector = malloc (VECTORSIZE (MAX_VECTOR_COUNT));
                        refs = get_new_dict ();
        
                        local = calloc (1, sizeof (*local));
                        INIT_LIST_HEAD (&local->winds);
            
                        first_request = request;
                }

                count += request->count;
                bytecount = VECTORSIZE (request->count);
                memcpy (((char *)vector)+copied,
                        request->vector,
                        bytecount);
                copied += bytecount;
      
                if (request->refs) {
                        dict_copy (request->refs, refs);
                }

                next = NULL;
                if (request->winds.next != winds) {    
                        next = list_entry (request->winds.next, struct write_request, winds);
                }

                list_del_init (&request->winds);
                list_add_tail (&request->winds, &local->winds);

                if (!next || ((count + next->count) > MAX_VECTOR_COUNT)) {
                        sync_frame = copy_frame (frame);  
                        sync_frame->local = local;
                        local->file = file;
                        sync_frame->root->req_refs = dict_ref (refs);
                        fd_ref (file->fd);
                        STACK_WIND (sync_frame,
                                    wb_sync_cbk,
                                    FIRST_CHILD(sync_frame->this),
                                    FIRST_CHILD(sync_frame->this)->fops->writev,
                                    file->fd, vector,
                                    count, first_request->offset);
        
                        dict_unref (refs);
                        FREE (vector);
                        first_request = NULL;
                        refs = NULL;
                        vector = NULL;
                        copied = count = 0;
                }
        }

        return bytes;
}


int32_t 
wb_stat_cbk (call_frame_t *frame,
             void *cookie,
             xlator_t *this,
             int32_t op_ret,
             int32_t op_errno,
             struct stat *buf)
{
        wb_local_t *local = NULL;
  
        local = frame->local;
  
        if (local->file)
                fd_unref (local->file->fd);

        STACK_UNWIND (frame, op_ret, op_errno, buf);

        return 0;
}


int32_t
wb_stat (call_frame_t *frame,
         xlator_t *this,
         loc_t *loc)
{
        wb_file_t *file = NULL;
        fd_t *iter_fd = NULL;
        wb_local_t *local = NULL;

        if (loc->inode)
        {
                iter_fd = fd_lookup (loc->inode, frame->root->pid);
                if (iter_fd) {
                        if (dict_get (iter_fd->ctx, this->name)) {
                                file = data_to_ptr (dict_get (iter_fd->ctx, this->name));
                        } else {
                                fd_unref (iter_fd);
                        }
                }
                if (file)
                {
                        wb_sync_all (frame, file);
                }
        }

        local = calloc (1, sizeof (*local));
        local->file = file;

        frame->local = local;

        STACK_WIND (frame, wb_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc);
        return 0;
}


int32_t 
wb_fstat (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd)
{
        wb_file_t *file = NULL;
        wb_local_t *local = NULL;
  
        if (!dict_get (fd->ctx, this->name))
        {
                gf_log (this->name, GF_LOG_ERROR, "returning EBADFD");
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        file = data_to_ptr (dict_get (fd->ctx, this->name));

        if (file)
        {
                fd_ref (file->fd);
                wb_sync_all (frame, file);
        }

        local = calloc (1, sizeof (*local));
        local->file = file;

        frame->local = local;
  
        STACK_WIND (frame,
                    wb_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd);
        return 0;
}


int32_t
wb_truncate_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *buf)
{
        wb_local_t *local = NULL; 
  
        local = frame->local;
        if (local->file)
                fd_unref (local->file->fd);

        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}


int32_t 
wb_truncate (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             off_t offset)
{
        wb_file_t *file = NULL;
        fd_t *iter_fd = NULL;
        wb_local_t *local = NULL;

        if (loc->inode)
        {
                iter_fd = fd_lookup (loc->inode, frame->root->pid);
                if (iter_fd) {
                        if (dict_get (iter_fd->ctx, this->name)){
                                file = data_to_ptr (dict_get (iter_fd->ctx, this->name));
                        } else {
                                fd_unref (iter_fd);
                        }
                }
    
                if (file)
                {
                        wb_sync_all (frame, file);
                }
        }
  
        local = calloc (1, sizeof (*local));
        local->file = file;

        frame->local = local;

        STACK_WIND (frame,
                    wb_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc,
                    offset);
        return 0;
}


int32_t
wb_ftruncate (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              off_t offset)
{
        wb_file_t *file = NULL;
        wb_local_t *local = NULL;

        if (!dict_get (fd->ctx, this->name))
        {
                gf_log (this->name, GF_LOG_ERROR, "returning EBADFD");
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        file = data_to_ptr (dict_get (fd->ctx, this->name));

        if (file)
        {
                wb_sync_all (frame, file);
        }

        local = calloc (1, sizeof (*local));
        local->file = file;

        if (file)
        {
                fd_ref (file->fd);
        }

        frame->local = local;

        STACK_WIND (frame,
                    wb_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd,
                    offset);
        return 0;
}


int32_t 
wb_utimens_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                struct stat *buf)
{
        wb_local_t *local = NULL;       
  
        local = frame->local;
        if (local->file)
                fd_unref (local->file->fd);

        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}


int32_t 
wb_utimens (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc,
            struct timespec tv[2])
{
        wb_file_t *file = NULL;
        fd_t *iter_fd = NULL;
        wb_local_t *local = NULL;

        if (loc->inode)
        {
                iter_fd = fd_lookup (loc->inode, frame->root->pid);
                if (iter_fd) {
                        if (dict_get (iter_fd->ctx, this->name)) {
                                file = data_to_ptr (dict_get (iter_fd->ctx, this->name));
                        } else {
                                fd_unref (iter_fd);
                        }
                }

                if (file)
                {
                        wb_sync_all (frame, file);
                }
        }

        local = calloc (1, sizeof (*local));
        local->file = file;

        frame->local = local;

        STACK_WIND (frame,
                    wb_utimens_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->utimens,
                    loc,
                    tv);
        return 0;
}

int32_t
wb_open_cbk (call_frame_t *frame,
             void *cookie,
             xlator_t *this,
             int32_t op_ret,
             int32_t op_errno,
             fd_t *fd)
{
        int32_t flags = 0;
        wb_file_t *file = NULL;

        if (op_ret != -1)
        {
                file = wb_file_create (this, fd);

                /* If mandatory locking has been enabled on this file,
                   we disable caching on it */

                if ((fd->inode->st_mode & S_ISGID) && !(fd->inode->st_mode & S_IXGRP))
                        file->disabled = 1;

                /* If O_DIRECT then, we disable chaching */
                if (frame->local)
                {
                        flags = *((int32_t *)frame->local);
                        if (flags & O_DIRECT)
                                file->disabled = 1;
                }

                LOCK_INIT (&file->lock);
        }

        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
}


int32_t
wb_open (call_frame_t *frame,
         xlator_t *this,
         loc_t *loc,
         int32_t flags,
         fd_t *fd)
{
        frame->local = calloc (1, sizeof(int32_t));
        *((int32_t *)frame->local) = flags;

        STACK_WIND (frame,
                    wb_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc, flags, fd);
        return 0;
}


int32_t
wb_create_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               fd_t *fd,
               inode_t *inode,
               struct stat *buf)
{
        wb_file_t *file = NULL;

        if (op_ret != -1)
        {
                file = wb_file_create (this, fd);
                /* 
                 * If mandatory locking has been enabled on this file,
                 * we disable caching on it
                 */
                if ((fd->inode->st_mode & S_ISGID) && 
                    !(fd->inode->st_mode & S_IXGRP))
                {
                        file->disabled = 1;
                }

                LOCK_INIT (&file->lock);
        }

        STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
        return 0;
}


int32_t
wb_create (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           int32_t flags,
           mode_t mode,
           fd_t *fd)
{
        STACK_WIND (frame,
                    wb_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd);
        return 0;
}


int32_t 
__wb_cleanup_queue (wb_file_t *file)
{
        wb_write_request_t *request = NULL, *dummy = NULL;
        int32_t bytes = 0;

        list_for_each_entry_safe (request, dummy, &file->request, list)
        {
                if (request->got_reply && request->write_behind)
                {
                        bytes += iov_length (request->vector, request->count);
                        list_del_init (&request->list);

                        FREE (request->vector);
                        dict_unref (request->refs);
      
                        FREE (request);
                }
        }

        return bytes;
}


int32_t 
__wb_mark_wind_all (list_head_t *list, list_head_t *winds)
{
        wb_write_request_t *request = NULL;
        size_t size = 0;

        list_for_each_entry (request, list, list)
        {
                if (!request->stack_wound)
                {
                        size += iov_length (request->vector, request->count);
                        request->stack_wound = 1;
                        list_add_tail (&request->winds, winds);
                }
        }
  
        return size;
}


size_t 
__wb_get_aggregate_size (list_head_t *list)
{
        wb_write_request_t *request = NULL;
        size_t size = 0;

        list_for_each_entry (request, list, list)
        {
                if (!request->stack_wound)
                {
                        size += iov_length (request->vector, request->count);
                }
        }

        return size;
}

uint32_t
__wb_get_incomplete_writes (list_head_t *list)
{
        wb_write_request_t *request = NULL;
        uint32_t count = 0;

        list_for_each_entry (request, list, list)
        {
                if (request->stack_wound && !request->got_reply)
                {
                        count++;
                }
        }

        return count;
}

int32_t
__wb_mark_winds (list_head_t *list, list_head_t *winds, size_t aggregate_conf)
{
        size_t aggregate_current = 0;
        uint32_t incomplete_writes = 0;

        incomplete_writes = __wb_get_incomplete_writes (list); 

        aggregate_current = __wb_get_aggregate_size (list);

        if ((incomplete_writes == 0) || (aggregate_current >= aggregate_conf))
        {
                __wb_mark_wind_all (list, winds);
        }

        return aggregate_current;
}


size_t
__wb_get_window_size (list_head_t *list)
{
        wb_write_request_t *request = NULL;
        size_t size = 0;

        list_for_each_entry (request, list, list)
        {
                if (request->write_behind && !request->got_reply)
                {
                        size += iov_length (request->vector, request->count);
                }
        }

        return size;
}


size_t 
__wb_mark_unwind_till (list_head_t *list, list_head_t *unwinds, size_t size)
{
        size_t written_behind = 0;
        wb_write_request_t *request = NULL;

        list_for_each_entry (request, list, list)
        {
                if (written_behind <= size)
                {
                        if (!request->write_behind)
                        {
                                wb_local_t *local = request->frame->local;
                                written_behind += iov_length (request->vector, request->count);
                                request->write_behind = 1;
                                list_add_tail (&local->unwind_frames, unwinds);
                        }
                }
                else
                {
                        break;
                }
        }

        return written_behind;
}


int32_t 
__wb_mark_unwinds (list_head_t *list, list_head_t *unwinds, size_t window_conf)
{
        size_t window_current = 0;

        window_current = __wb_get_window_size (list);
        if (window_current <= window_conf)
        {
                window_current += __wb_mark_unwind_till (list, unwinds,
                                                         window_conf - window_current);
        }

        return window_current;
}


int32_t
wb_stack_unwind (list_head_t *unwinds)
{
        struct stat buf = {0,};
        wb_local_t *local = NULL, *dummy = NULL;

        list_for_each_entry_safe (local, dummy, unwinds, unwind_frames)
        {
                list_del_init (&local->unwind_frames);
                STACK_UNWIND (local->frame, local->op_ret, local->op_errno, &buf);
        }

        return 0;
}


int32_t
wb_do_ops (call_frame_t *frame, wb_file_t *file, list_head_t *winds, list_head_t *unwinds)
{
        /* copy the frame before calling wb_stack_unwind, since this request containing current frame might get unwound */
        /*  call_frame_t *sync_frame = copy_frame (frame); */
 
        wb_stack_unwind (unwinds);
        wb_sync (frame, file, winds);

        return 0;
}


int32_t 
wb_process_queue (call_frame_t *frame, wb_file_t *file, char flush_all) 
{
        list_head_t winds, unwinds;
        size_t size = 0;
        wb_conf_t *conf = file->this->private;

        INIT_LIST_HEAD (&winds);
        INIT_LIST_HEAD (&unwinds);

        if (!file)
        {
                return -1;
        }

        size = flush_all ? 0 : conf->aggregate_size;
        LOCK (&file->lock);
        {
                __wb_cleanup_queue (file);
                __wb_mark_winds (&file->request, &winds, size);
                __wb_mark_unwinds (&file->request, &unwinds, conf->window_size);
        }
        UNLOCK (&file->lock);

        wb_do_ops (frame, file, &winds, &unwinds);
        return 0;
}


wb_write_request_t *
wb_enqueue (wb_file_t *file, 
            call_frame_t *frame,
            struct iovec *vector,
            int32_t count,
            off_t offset)
{
        wb_write_request_t *request = NULL;
        wb_local_t *local = calloc (1, sizeof (*local));

        request = calloc (1, sizeof (*request));

        INIT_LIST_HEAD (&request->list);
        INIT_LIST_HEAD (&request->winds);

        request->frame = frame;
        request->vector = iov_dup (vector, count);
        request->count = count;
        request->offset = offset;
        request->refs = dict_ref (frame->root->req_refs);

        frame->local = local;
        local->frame = frame;
        local->op_ret = iov_length (vector, count);
        local->op_errno = 0;
        INIT_LIST_HEAD (&local->unwind_frames);

        LOCK (&file->lock);
        {
                list_add_tail (&request->list, &file->request);
                file->offset = offset + iov_length (vector, count);
        }
        UNLOCK (&file->lock);

        return request;
}


int32_t
wb_writev (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           struct iovec *vector,
           int32_t count,
           off_t offset)
{
        wb_file_t *file = NULL;
        char offset_expected = 1; 
        call_frame_t *process_frame = NULL;

        process_frame = copy_frame (frame);

        if (!dict_get (fd->ctx, this->name))
        {
                gf_log (this->name, GF_LOG_ERROR, "returning EBADFD");
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        file = data_to_ptr (dict_get (fd->ctx, this->name));

        if (!file)
        {
                gf_log (this->name, GF_LOG_ERROR,
                        "wb_file not found for fd %p", fd);
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        LOCK (&file->lock);
        {
                if (file->offset != offset)
                        offset_expected = 0;
        }
        UNLOCK (&file->lock);

        if (!offset_expected)
        {
                wb_process_queue (process_frame, file, 1);
        }

        wb_enqueue (file, frame, vector, count, offset);
        wb_process_queue (process_frame, file, 0);

        STACK_DESTROY (process_frame->root);

        return 0;
}


int32_t
wb_readv_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno,
              struct iovec *vector,
              int32_t count,
              struct stat *stbuf)
{
        wb_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
        return 0;
}


int32_t
wb_readv (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd,
          size_t size,
          off_t offset)
{
        wb_file_t *file = NULL;
        wb_local_t *local = NULL;

        if (!dict_get (fd->ctx, this->name))
        {
                gf_log (this->name, GF_LOG_ERROR, "returning EBADFD");
                STACK_UNWIND (frame, -1, EBADFD, NULL);
                return 0;
        }

        file = data_to_ptr (dict_get (fd->ctx, this->name));

        if (file)
        {
                wb_sync_all (frame, file);
        }

        local = calloc (1, sizeof (*local));
        local->file = file;

        frame->local = local;

        STACK_WIND (frame,
                    wb_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset);

        return 0;
}


int32_t
wb_ffr_bg_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno)
{
        wb_local_t *local = NULL;
        wb_file_t *file = NULL;

        local = frame->local;
        file = local->file;

        if (file) {
                fd_unref (file->fd);
        }

        if (file->op_ret == -1)
        {
                op_ret = file->op_ret;
                op_errno = file->op_errno;

                file->op_ret = 0;
        }
  
        STACK_DESTROY (frame->root);
        return 0;
}


int32_t
wb_ffr_cbk (call_frame_t *frame,
            void *cookie,
            xlator_t *this,
            int32_t op_ret,
            int32_t op_errno)
{
        wb_local_t *local = NULL;
        wb_file_t *file = NULL;

        local = frame->local;
        file = local->file;
        if (file) {
                /* corresponds to the fd_ref() done during wb_file_create() */
                fd_unref (file->fd);
        }

        if (file->op_ret == -1)
        {
                op_ret = file->op_ret;
                op_errno = file->op_errno;

                file->op_ret = 0;
        }

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t
wb_flush (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd)
{
        wb_conf_t *conf = NULL;
        wb_file_t *file = NULL;
        call_frame_t *flush_frame = NULL;
        wb_local_t *local = NULL;

        conf = this->private;

        if (!dict_get (fd->ctx, this->name))
        {
                gf_log (this->name, GF_LOG_ERROR, "returning EBADFD");
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        file = data_to_ptr (dict_get (fd->ctx, this->name));
        local = calloc (1, sizeof (*local));
        local->file = file;
        if (file)
                fd_ref (file->fd);

        if (&file->request != file->request.next)
        {
                gf_log (this->name, GF_LOG_DEBUG,
                        "request queue is not empty, it has to be synced");
        }

        if (conf->flush_behind && (!file->disabled)) {
                flush_frame = copy_frame (frame);     
                STACK_UNWIND (frame, file->op_ret, file->op_errno); // liar! liar! :O

                flush_frame->local = local;
                wb_sync_all (flush_frame, file);

                STACK_WIND (flush_frame,
                            wb_ffr_bg_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush,
                            fd);

        } else {
                wb_sync_all (frame, file);

                frame->local = local;
                STACK_WIND (frame,
                            wb_ffr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush,
                            fd);
        }

        return 0;
}

int32_t
wb_fsync_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno)
{
        wb_local_t *local = NULL;
        wb_file_t *file = NULL;

        local = frame->local;
        file = local->file;

        if (file->op_ret == -1)
        {
                op_ret = file->op_ret;
                op_errno = file->op_errno;

                file->op_ret = 0;
        }

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
wb_fsync (call_frame_t *frame,
          xlator_t *this,
          fd_t *fd,
          int32_t datasync)
{
        wb_file_t *file = NULL;
        wb_local_t *local = NULL;

        if (!dict_get (fd->ctx, this->name))
        {
                gf_log (this->name, GF_LOG_ERROR, "returning EBADFD");
                STACK_UNWIND (frame, -1, EBADFD);
                return 0;
        }

        file = data_to_ptr (dict_get (fd->ctx, this->name));

        if (file)
        {
                wb_sync_all (frame, file);
        }

        local = calloc (1, sizeof (*local));
        local->file = file;


        frame->local = local;

        STACK_WIND (frame,
                    wb_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd, datasync);
        return 0;
}


int32_t
wb_release (xlator_t *this,
            fd_t *fd)
{
        wb_file_t *file = NULL;

        file = data_to_ptr (dict_get (fd->ctx, this->name));
  
        wb_file_destroy (file);

        return 0;
}


int32_t 
init (xlator_t *this)
{
        dict_t *options = this->options;
        wb_conf_t *conf = NULL;
        char *aggregate_size_string = NULL;
        char *window_size_string = NULL;
        data_t *data = NULL;

        if (!this->children || this->children->next)
        {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: write-behind (%s) not configured with exactly one child",
                        this->name);
                return -1;
        }

        conf = calloc (1, sizeof (*conf));

        conf->aggregate_size = 0;

        if (dict_get (options, "aggregate-size"))
                aggregate_size_string = data_to_str (dict_get (options, 
                                                               "aggregate-size"));

        if (aggregate_size_string)
        {
                if (gf_string2bytesize (aggregate_size_string, &conf->aggregate_size) != 0)
                {
                        gf_log ("write-behind", 
                                GF_LOG_ERROR, 
                                "invalid number format \"%s\" of \"option aggregate-size\"", 
                                aggregate_size_string);
                        return -1;
                }
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "using aggregate-size = %"PRIu64"", conf->aggregate_size);
  
        conf->window_size = 0;

        if (dict_get (options, "window-size"))
                window_size_string = data_to_str (dict_get (options, 
                                                            "window-size"));
        if (window_size_string)
        {
                if (gf_string2bytesize (window_size_string, &conf->window_size) != 0)
                {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "invalid number format \"%s\" of \"option window-size\"", 
                                window_size_string);
                        FREE (conf);
                        return -1;
                }
        }

        if (!conf->window_size && conf->aggregate_size)
        {
                gf_log (this->name, GF_LOG_WARNING,
                        "setting window-size to be equal to aggregate-size(%"PRIu64")",
                        conf->aggregate_size);
                conf->window_size = conf->aggregate_size;
        }

        if (conf->window_size < conf->aggregate_size)
        {
                gf_log (this->name, GF_LOG_ERROR,
                        "aggregate-size(%"PRIu64") cannot be more than window-size"
                        "(%"PRIu64")", conf->window_size, conf->aggregate_size);
                FREE (conf);
                return -1;
        }


        conf->flush_behind = 0;
  
        if (dict_get (options, "flush-behind")) {
                data = dict_get (options, "flush-behind");
                if (gf_string2boolean (data->data, &conf->flush_behind) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'flush-behind' takes only boolean arguments");
                        return -1;
                }
                if (conf->flush_behind) {
                        if (conf->aggregate_size != 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "aggregate-size is not zero, disabling flush-behind");
                                conf->flush_behind = 0;
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "enabling flush-behind");
                        }
                }
        }
        this->private = conf;
        return 0;
}


void
fini (xlator_t *this)
{
        wb_conf_t *conf = this->private;

        FREE (conf);
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
        .utimens     = wb_utimens,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
        .release  = wb_release
};

struct xlator_options options[] = {
        { "flush-behind", GF_OPTION_TYPE_BOOL, 0, 0, 0 },
        { "aggregate-size", GF_OPTION_TYPE_SIZET, 0, 128 * GF_UNIT_KB, 4 * GF_UNIT_MB },
        { "window-size", GF_OPTION_TYPE_SIZET, 0, 1 * GF_UNIT_MB, 16 * GF_UNIT_GB },
        { NULL, 0, },
};
