/*
  Copyright (c) 2009-2010 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "quick-read.h"

int32_t
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset);


static void
qr_loc_wipe (loc_t *loc)
{
        if (loc == NULL) {
                goto out;
        }

        if (loc->path) {
                FREE (loc->path);
                loc->path = NULL;
        }

        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }

        if (loc->parent) {
                inode_unref (loc->parent);
                loc->parent = NULL;
        }

out:
        return;
}


static int32_t
qr_loc_fill (loc_t *loc, inode_t *inode, char *path)
{
        int32_t  ret = -1;
        char    *parent = NULL;

        if ((loc == NULL) || (inode == NULL) || (path == NULL)) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        loc->inode = inode_ref (inode);
        loc->path = strdup (path);
        loc->ino = inode->ino;

        parent = strdup (path);
        if (parent == NULL) {
                ret = -1;
                goto out;
        }

        parent = dirname (parent);

        loc->parent = inode_from_path (inode->table, parent);
        if (loc->parent == NULL) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        loc->name = strrchr (loc->path, '/');
        ret = 0;
out:
        if (ret == -1) {
                qr_loc_wipe (loc);

        }

        if (parent) {
                FREE (parent);
        }
        
        return ret;
}


void
qr_resume_pending_ops (qr_fd_ctx_t *qr_fd_ctx)
{
        struct list_head  waiting_ops;
        call_stub_t      *stub = NULL, *tmp = NULL;  
        
        if (qr_fd_ctx == NULL) {
                goto out;
        }

        INIT_LIST_HEAD (&waiting_ops);

        LOCK (&qr_fd_ctx->lock);
        {
                list_splice_init (&qr_fd_ctx->waiting_ops,
                                  &waiting_ops);
        }
        UNLOCK (&qr_fd_ctx->lock);

        if (!list_empty (&waiting_ops)) {
                list_for_each_entry_safe (stub, tmp, &waiting_ops, list) {
                        list_del_init (&stub->list);
                        call_resume (stub);
                }
        }

out:
        return;
}


static void
qr_fd_ctx_free (qr_fd_ctx_t *qr_fd_ctx)
{
        if (qr_fd_ctx == NULL) {
                goto out;
        }

        assert (list_empty (&qr_fd_ctx->waiting_ops));

        FREE (qr_fd_ctx->path);
        FREE (qr_fd_ctx);

out:
        return;
}

        
int32_t
qr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct stat *buf, dict_t *dict)
{
        data_t    *content = NULL;
        qr_file_t *qr_file = NULL;
        uint64_t   value = 0;
        int        ret = -1;
        qr_conf_t *conf = NULL;

        if ((op_ret == -1) || (dict == NULL)) {
                goto out;
        }

        conf = this->private;

        content = dict_get (dict, GLUSTERFS_CONTENT_KEY);
        if (content == NULL) {
                goto out;
        }

        if (buf->st_size > conf->max_file_size) {
                goto out;
        }

        if (S_ISDIR (buf->st_mode)) {
                goto out;
        }

        ret = inode_ctx_get (inode, this, &value);
        if (ret == -1) {
                qr_file = CALLOC (1, sizeof (*qr_file));
                if (qr_file == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
                
                LOCK_INIT (&qr_file->lock);
                inode_ctx_put (inode, this, (uint64_t)(long)qr_file);
        } else {
                qr_file = (qr_file_t *)(long)value;
                if (qr_file == NULL) {
                        op_ret = -1;
                        op_errno = EINVAL;
                        goto out;
                }
        }

        LOCK (&qr_file->lock);
        {
                if (qr_file->xattr) {
                        dict_unref (qr_file->xattr);
                        qr_file->xattr = NULL;
                }

                qr_file->xattr = dict_ref (dict);
                qr_file->stbuf = *buf;
                gettimeofday (&qr_file->tv, NULL);
        }
        UNLOCK (&qr_file->lock);

out:
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf, dict);
        return 0;
}


int32_t
qr_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        qr_conf_t *conf = NULL;
        dict_t    *new_req_dict = NULL;
        int32_t    op_ret = -1, op_errno = -1;
        data_t    *content = NULL; 
        uint64_t   requested_size = 0, size = 0; 

        conf = this->private;
        if (conf == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        if ((xattr_req == NULL) && (conf->max_file_size > 0)) {
                new_req_dict = xattr_req = dict_new ();
                if (xattr_req == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        goto unwind;
                }
        }

        if (xattr_req) {
                content = dict_get (xattr_req, GLUSTERFS_CONTENT_KEY);
                if (content) {
                        requested_size = data_to_uint64 (content);
                }
        }

        if (((conf->max_file_size > 0) && (content == NULL))
            || (conf->max_file_size != requested_size)) {
                size = (conf->max_file_size > requested_size) ?
                        conf->max_file_size : requested_size;

                op_ret = dict_set (xattr_req, GLUSTERFS_CONTENT_KEY,
                                   data_from_uint64 (size));
                if (op_ret < 0) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }
        }

	STACK_WIND (frame, qr_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

        if (new_req_dict) {
                dict_unref (new_req_dict);
        }

        return 0;

unwind:
        STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL, NULL);

        if (new_req_dict) {
                dict_unref (new_req_dict);
        }

        return 0;
}


int32_t
qr_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd)
{
        uint64_t         value = 0;
        int32_t          ret = -1;
        struct list_head waiting_ops;
        qr_local_t      *local = NULL;
        qr_file_t       *qr_file = NULL;
        qr_fd_ctx_t     *qr_fd_ctx = NULL;
        call_stub_t     *stub = NULL, *tmp = NULL;

        local = frame->local;
        INIT_LIST_HEAD (&waiting_ops);

        ret = fd_ctx_get (fd, this, &value);
        if ((ret == -1) && (op_ret != -1)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (value) {
                qr_fd_ctx = (qr_fd_ctx_t *) (long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        qr_fd_ctx->open_in_transit = 0;

                        if (op_ret == 0) {
                                qr_fd_ctx->opened = 1;
                        }
                        list_splice_init (&qr_fd_ctx->waiting_ops,
                                          &waiting_ops);
                }
                UNLOCK (&qr_fd_ctx->lock);

                if (local && local->is_open
                    && ((local->open_flags & O_TRUNC) == O_TRUNC)) { 
                        ret = inode_ctx_get (fd->inode, this, &value);
                        if (ret == 0) {
                                qr_file = (qr_file_t *)(long) value;

                                if (qr_file) {
                                        LOCK (&qr_file->lock);
                                        {
                                                dict_unref (qr_file->xattr);
                                                qr_file->xattr = NULL;
                                        }
                                        UNLOCK (&qr_file->lock);
                                }
                        }
                }

                if (!list_empty (&waiting_ops)) {
                        list_for_each_entry_safe (stub, tmp, &waiting_ops,
                                                  list) {
                                list_del_init (&stub->list);
                                call_resume (stub);
                        }
                }
        }
out: 
        if (local && local->is_open) { 
                STACK_UNWIND (frame, op_ret, op_errno, fd);
        }

        return 0;
}


int32_t
qr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd)
{
        qr_file_t   *qr_file = NULL;
        int32_t      ret = -1;
        uint64_t     filep = 0;
        char         content_cached = 0;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      op_ret = -1, op_errno = -1;
        qr_local_t  *local = NULL;
        qr_conf_t   *conf = NULL;

        conf = this->private;

        qr_fd_ctx = CALLOC (1, sizeof (*qr_fd_ctx));
        if (qr_fd_ctx == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        LOCK_INIT (&qr_fd_ctx->lock);
        INIT_LIST_HEAD (&qr_fd_ctx->waiting_ops);

        qr_fd_ctx->path = strdup (loc->path);
        qr_fd_ctx->flags = flags;

        ret = fd_ctx_set (fd, this, (uint64_t)(long)qr_fd_ctx);
        if (ret == -1) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        local = CALLOC (1, sizeof (*local));
        if (local == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        local->is_open = 1;
        local->open_flags = flags; 
        frame->local = local;
        local = NULL;

        ret = inode_ctx_get (fd->inode, this, &filep);
        if (ret == 0) {
                qr_file = (qr_file_t *)(long) filep;
                if (qr_file) {
                        LOCK (&qr_file->lock);
                        {
                                if (qr_file->xattr) {
                                        content_cached = 1;
                                }
                        }
                        UNLOCK (&qr_file->lock);
                }
        }

        if (content_cached && ((flags & O_DIRECTORY) == O_DIRECTORY)) {
                op_ret = -1;
                op_errno = ENOTDIR;
                qr_fd_ctx = NULL;
                goto unwind;
        }

        if (!content_cached || ((flags & O_WRONLY) == O_WRONLY) 
            || ((flags & O_TRUNC) == O_TRUNC)) {
                LOCK (&qr_fd_ctx->lock);
                {
                        /*
                         * we need not set this flag, since open is not yet 
                         * unwounded.
                         */
                           
                        qr_fd_ctx->open_in_transit = 1;
                }
                UNLOCK (&qr_fd_ctx->lock);
                goto wind;
        } else {
                op_ret = 0;
                op_errno = 0;
                goto unwind;
        }

unwind:
        if (op_ret == -1) {
                if (qr_fd_ctx != NULL) {
                        qr_fd_ctx_free (qr_fd_ctx);
                }

                if (local != NULL) {
                        FREE (local);
                }
        }

        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;

wind:
        STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd);
        return 0;
}


static inline char
qr_time_elapsed (struct timeval *now, struct timeval *then)
{
        return now->tv_sec - then->tv_sec;
}


static inline char
qr_need_validation (qr_conf_t *conf, qr_file_t *file)
{
        struct timeval now = {0, };
        char           need_validation = 0;
        
        gettimeofday (&now, NULL);

        if (qr_time_elapsed (&now, &file->tv) >= conf->cache_timeout)
                need_validation = 1;

        return need_validation;
}


static int32_t
qr_validate_cache_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        qr_file_t  *qr_file = NULL;
        qr_local_t *local = NULL;
        uint64_t    value = 0;
        int32_t     ret = 0;

        if (op_ret == -1) {
                goto unwind;
        }

        local = frame->local; 
        if ((local == NULL) || ((local->fd) == NULL)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }
         
        ret = inode_ctx_get (local->fd->inode, this, &value);
        if (ret == -1) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        qr_file = (qr_file_t *)(long) value;
        if (qr_file == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        LOCK (&qr_file->lock);
        {
                if (qr_file->stbuf.st_mtime != buf->st_mtime) {
                        dict_unref (qr_file->xattr);
                        qr_file->xattr = NULL;
                }

                gettimeofday (&qr_file->tv, NULL);
        }
        UNLOCK (&qr_file->lock);

        frame->local = NULL;

        call_resume (local->stub);
        
        FREE (local);
        return 0;

unwind:
        /* this is actually unwind of readv */
        STACK_UNWIND (frame, op_ret, op_errno, NULL, -1, NULL, NULL);
        return 0;
}


int32_t
qr_validate_cache_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        STACK_WIND (frame, qr_validate_cache_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        return 0;
}


int
qr_validate_cache (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   call_stub_t *stub)
{
        int          ret = -1;
        int          flags = 0;
        uint64_t     value = 0; 
        loc_t        loc = {0, };
        char        *path = NULL;
        qr_local_t  *local = NULL;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        call_stub_t *validate_stub = NULL;
        char         need_open = 0, can_wind = 0;

        local = CALLOC (1, sizeof (*local));
        if (local == NULL) {
                goto out;
        }

        local->fd = fd;
        local->stub = stub;
        frame->local = local;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        } 

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                validate_stub = fop_fstat_stub (frame,
                                                                qr_validate_cache_helper,
                                                                fd);
                                if (validate_stub == NULL) {
                                        ret = -1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }
                                
                                list_add_tail (&validate_stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        } 
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);

                if (ret == -1) {
                        goto out;
                }
        } else {
                can_wind = 1;
        }

        if (need_open) {
                ret = qr_loc_fill (&loc, fd->inode, path);
                if (ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            &loc, flags, fd);
                        
                qr_loc_wipe (&loc);
        } else if (can_wind) {
                STACK_WIND (frame, qr_validate_cache_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd);
        }

        ret = 0;
out:
        return ret; 
}


int32_t
qr_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct stat *stbuf, struct iobref *iobref)
{
	STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf, iobref);
	return 0;
}


int32_t
qr_readv_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset)
{
        STACK_WIND (frame, qr_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv, fd, size, offset);
        return 0;
}


int32_t
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
{
        qr_file_t       *file = NULL;
        int32_t          ret = -1, op_ret = -1, op_errno = -1;
        uint64_t         value = 0;
        int              count = -1, flags = 0, i = 0;
        char             content_cached = 0, need_validation = 0;
        char             need_open = 0, can_wind = 0, need_unwind = 0;
        struct iobuf    *iobuf = NULL;
        struct iobref   *iobref = NULL; 
        struct stat      stbuf = {0, }; 
        data_t          *content = NULL;
        qr_fd_ctx_t     *qr_fd_ctx = NULL; 
        call_stub_t     *stub = NULL;
        loc_t            loc = {0, };
        qr_conf_t       *conf = NULL;
        struct iovec    *vector = NULL;
        char            *path = NULL;
        glusterfs_ctx_t *ctx = NULL;
        off_t            start = 0, end = 0;
        size_t           len = 0;

        op_ret = 0;
        conf = this->private;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        ret = inode_ctx_get (fd->inode, this, &value);
        if (ret == 0) {
                file = (qr_file_t *)(long)value;
                if (file) {
                        LOCK (&file->lock);
                        {
                                if (file->xattr){
                                        if (qr_need_validation (conf,file)) {
                                                need_validation = 1;
                                                goto unlock;
                                        }

                                        content = dict_get (file->xattr,
                                                            GLUSTERFS_CONTENT_KEY);

                                        content_cached = 1;
                                        if (offset > content->len) {
                                                op_ret = 0;
                                                end = content->len;
                                        } else {
                                                if ((offset + size)
                                                    > content->len) {
                                                        op_ret = content->len - offset;
                                                        end = content->len;
                                                } else {
                                                        op_ret = size;
                                                        end =  offset + size;
                                                }
                                        }

                                        ctx = glusterfs_ctx_get ();
                                        count = (op_ret / ctx->page_size) + 1; 
                                        vector = CALLOC (count,
                                                         sizeof (*vector));
                                        if (vector == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                need_unwind = 1;
                                                goto unlock;
                                        }

                                        iobref = iobref_new ();
                                        if (iobref == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                need_unwind = 1;
                                                goto unlock;
                                        }

                                        for (i = 0; i < count; i++) {
                                                iobuf = iobuf_get (this->ctx->iobuf_pool);
                                                if (iobuf == NULL) {
                                                        op_ret = -1;
                                                        op_errno = ENOMEM;
                                                        need_unwind = 1;
                                                        goto unlock;
                                                }
                                        
                                                start = offset + ctx->page_size * i;
                                                if (start > end) {
                                                        len = 0;
                                                } else {
                                                        len = (ctx->page_size
                                                               > (end - start))
                                                                ? (end - start)
                                                                : ctx->page_size;

                                                        memcpy (iobuf->ptr,
                                                                content->data + start,
                                                                len);
                                                }

                                                iobref_add (iobref, iobuf);
                                                iobuf_unref (iobuf);

                                                vector[i].iov_base = iobuf->ptr;
                                                vector[i].iov_len = len;
                                        }
                                        
                                        stbuf = file->stbuf;
                                }
                        }
                unlock:
                        UNLOCK (&file->lock);
                }
        }

out:
        if (content_cached || need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno, vector, count, &stbuf,
                              iobref);

        } else if (need_validation) {
                stub = fop_readv_stub (frame, qr_readv, fd, size, offset);
                if (stub == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                op_ret = qr_validate_cache (frame, this, fd, stub);
                if (op_ret == -1) {
                        need_unwind = 1;
                        op_errno = errno;
                        call_stub_destroy (stub);
                        goto out;
                }
        } else {
                if (qr_fd_ctx) {
                        LOCK (&qr_fd_ctx->lock);
                        {
                                path = qr_fd_ctx->path;
                                flags = qr_fd_ctx->flags;

                                if (!(qr_fd_ctx->opened
                                      || qr_fd_ctx->open_in_transit)) {
                                        need_open = 1;
                                        qr_fd_ctx->open_in_transit = 1;
                                } 

                                if (qr_fd_ctx->opened) {
                                        can_wind = 1;
                                } else {
                                        stub = fop_readv_stub (frame,
                                                               qr_readv_helper,
                                                               fd, size,
                                                               offset);
                                        if (stub == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                need_unwind = 1;
                                                qr_fd_ctx->open_in_transit = 0;
                                                goto fdctx_unlock;
                                        }
                                
                                        list_add_tail (&stub->list,
                                                       &qr_fd_ctx->waiting_ops);
                                } 
                        }
                fdctx_unlock:
                        UNLOCK (&qr_fd_ctx->lock);
                        
                        if (op_ret == -1) {
                                need_unwind = 1;
                                goto out;
                        }
                } else {
                        can_wind = 1;
                }

                if (need_open) {
                        op_ret = qr_loc_fill (&loc, fd->inode, path);
                        if (op_ret == -1) {
                                qr_resume_pending_ops (qr_fd_ctx);
                                goto out;
                        }

                        STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->open,
                                    &loc, flags, fd);
                        
                        qr_loc_wipe (&loc);
                } else if (can_wind) {
                        STACK_WIND (frame, qr_readv_cbk,
                                    FIRST_CHILD (this),
                                    FIRST_CHILD (this)->fops->readv, fd, size,
                                    offset);
                }

        }

        if (vector) {
                FREE (vector);
        }

        if (iobref) {
                iobref_unref (iobref);
        }

        return 0;
}


int32_t
qr_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
               int32_t op_errno, struct stat *stbuf)
{
	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}


int32_t
qr_writev_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iovec *vector, int32_t count, off_t off,
                  struct iobref *iobref)
{
        STACK_WIND (frame, qr_writev_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev, fd, vector, count, off,
                    iobref);
        return 0;
}


int32_t
qr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, struct iobref *iobref)
{
        uint64_t     value = 0;
        int          flags = 0;
        call_stub_t *stub = NULL; 
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_file_t   *qr_file = NULL;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      op_ret = -1, op_errno = -1, ret = -1;
        char         can_wind = 0, need_unwind = 0, need_open = 0; 
        
        ret = fd_ctx_get (fd, this, &value);

        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        ret = inode_ctx_get (fd->inode, this, &value);
        if (ret == 0) {
                qr_file = (qr_file_t *)(long)value;
        }

        if (qr_file) {
                LOCK (&qr_file->lock);
                {
                        if (qr_file->xattr) {
                                dict_unref (qr_file->xattr);
                                qr_file->xattr = NULL;
                        }
                }
                UNLOCK (&qr_file->lock);
        }
            
        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;
                
                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_writev_stub (frame, qr_writev_helper,
                                                        fd, vector, count, off,
                                                        iobref);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_writev_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->writev, fd, vector, count,
                            off, iobref);
        } else if (need_open) { 
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd);

                qr_loc_wipe (&loc);
        }

        return 0;
}


int32_t
qr_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
		   int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int32_t
qr_fstat_helper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        STACK_WIND (frame, qr_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        return 0;
}


int32_t
qr_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        char         need_open = 0, can_wind = 0, need_unwind = 0;
        uint64_t     value = 0;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        call_stub_t *stub = NULL;  
        loc_t        loc = {0, };
        char        *path = NULL; 
        int          flags = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fstat_stub (frame, qr_fstat_helper,
                                                       fd);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fstat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}


static int32_t
qr_fchown_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int32_t
qr_fchown_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, uid_t uid,
                  gid_t gid)
{
        STACK_WIND (frame, qr_fchown_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fchown, fd, uid, gid);
        return 0;
}


int32_t
qr_fchown (call_frame_t *frame, xlator_t *this,	fd_t *fd, uid_t uid, gid_t gid)
{
        uint64_t     value = 0;
        int          flags = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fchown_stub (frame, qr_fchown_helper,
                                                        fd, uid, gid);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fchown_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fchown, fd, uid, gid);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}


int32_t
qr_fchmod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int32_t
qr_fchmod_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, mode_t mode)
{
        STACK_WIND(frame, qr_fchmod_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fchmod, fd, mode);
        return 0;
}


int32_t
qr_fchmod (call_frame_t *frame, xlator_t *this,	fd_t *fd, mode_t mode)
{
        uint64_t     value = 0;
        int          flags = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;
                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fchmod_stub (frame, qr_fchmod_helper,
                                                        fd, mode);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fchmod_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fchmod, fd, mode);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd);

                qr_loc_wipe (&loc);
        }

        return 0;
}


int32_t
qr_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
qr_fsetxattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     dict_t *dict, int32_t flags)
{
        STACK_WIND (frame, qr_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags);
        return 0;
}


int32_t
qr_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags)
{
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        int          open_flags = 0;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        open_flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fsetxattr_stub (frame,
                                                           qr_fsetxattr_helper,
                                                           fd, dict, flags);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetxattr, fd, dict,
                            flags);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }

                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, open_flags,
                            fd);

                qr_loc_wipe (&loc);
        } 
        
        return 0;
}


int32_t
qr_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}


int32_t
qr_fgetxattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name)
{
        STACK_WIND (frame, qr_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name);
        return 0;
}


int32_t
qr_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name)
{
        int          flags = 0;
        uint64_t     value = 0;
        call_stub_t *stub = NULL;  
        char        *path = NULL;
        loc_t        loc = {0, };
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret = -1, op_ret = -1, op_errno = -1;
        char         need_open = 0, can_wind = 0, need_unwind = 0;

        /*
         * FIXME: Can quick-read use the extended attributes stored in the
         * cache? this needs to be discussed.
         */

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        path = qr_fd_ctx->path;
                        flags = qr_fd_ctx->flags;

                        if (!(qr_fd_ctx->opened
                              || qr_fd_ctx->open_in_transit)) {
                                need_open = 1;
                                qr_fd_ctx->open_in_transit = 1;
                        }

                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else {
                                stub = fop_fgetxattr_stub (frame,
                                                           qr_fgetxattr_helper,
                                                           fd, name);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                STACK_UNWIND (frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fgetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fgetxattr, fd, name);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx);
                        goto out;
                }
                
                STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd);

                qr_loc_wipe (&loc);
        }
        
        return 0;
}


int32_t 
init (xlator_t *this)
{
	char      *str = NULL;
        int32_t    ret = -1;
        qr_conf_t *conf = NULL;
 
        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: volume (%s) not configured with exactly one "
			"child", this->name);
                return -1;
        }

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        conf = CALLOC (1, sizeof (*conf));
        if (conf == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (this->options, "max-file-size", 
                            &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &conf->max_file_size);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "invalid number format \"%s\" of \"option "
                                "max-file-size\"", 
                                str);
                        ret = -1;
                        goto out;
                }
        }

        conf->cache_timeout = -1;
        ret = dict_get_str (this->options, "cache-timeout", &str);
        if (ret == 0) {
                ret = gf_string2uint_base10 (str, 
                                             (unsigned int *)&conf->cache_timeout);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid cache-timeout value %s", str);
                        ret = -1;
                        goto out;
                } 
        }

        this->private = conf;
out:
        if ((ret == -1) && conf) {
                FREE (conf);
        }

        return ret;
}


void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
	.lookup      = qr_lookup,
        .open        = qr_open,
        .readv       = qr_readv,
        .writev      = qr_writev,
        .fstat       = qr_fstat,
        .fchown      = qr_fchown,
        .fchmod      = qr_fchmod,
        .fsetxattr   = qr_fsetxattr,
        .fgetxattr   = qr_fgetxattr,
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {"cache-timeout"}, 
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = 60
        },
        { .key  = {"max-file-size"}, 
          .type = GF_OPTION_TYPE_SIZET, 
          .min  = 0,
          .max  = 1 * GF_UNIT_MB 
        },
};
