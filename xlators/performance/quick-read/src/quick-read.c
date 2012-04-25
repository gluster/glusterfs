/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "quick-read.h"
#include "statedump.h"

#define QR_DEFAULT_CACHE_SIZE 134217728

struct volume_options options[];

void
_fd_unref (fd_t *fd);

void
qr_local_free (qr_local_t *local)
{
        if (local == NULL) {
                goto out;
        }

        if (local->stub != NULL) {
                call_stub_destroy (local->stub);
        }

        if (local->path != NULL) {
                GF_FREE (local->path);
        }

        mem_put (local);

out:
        return;
}


qr_local_t *
qr_local_new (xlator_t *this)
{
        qr_local_t *local = NULL;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                goto out;
        }

        LOCK_INIT (&local->lock);
        INIT_LIST_HEAD (&local->fd_list);
out:
        return local;
}


int32_t
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata);


static void
qr_loc_wipe (loc_t *loc)
{
        if (loc == NULL) {
                goto out;
        }

        if (loc->path) {
                GF_FREE ((char *)loc->path);
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
        int32_t  ret    = -1;

        GF_VALIDATE_OR_GOTO_WITH_ERROR ("quick-read", loc, out, errno, EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR ("quick-read", inode, out, errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR ("quick-read", path, out, errno, EINVAL);

        loc->inode = inode_ref (inode);
        uuid_copy (loc->gfid, inode->gfid);

        loc->path = gf_strdup (path);
        if (!loc->path)
                goto out;

        ret = 0;
out:
        if (ret == -1) {
                qr_loc_wipe (loc);
        }

        return ret;
}


void
qr_resume_pending_ops (qr_fd_ctx_t *qr_fd_ctx, int32_t op_ret, int32_t op_errno)
{
        call_stub_t      *stub        = NULL, *tmp = NULL;
        struct list_head  waiting_ops = {0, };

        GF_VALIDATE_OR_GOTO ("quick-read", qr_fd_ctx, out);

        INIT_LIST_HEAD (&waiting_ops);

        LOCK (&qr_fd_ctx->lock);
        {
                qr_fd_ctx->open_in_transit = 0;
                list_splice_init (&qr_fd_ctx->waiting_ops,
                                  &waiting_ops);
        }
        UNLOCK (&qr_fd_ctx->lock);

        if (!list_empty (&waiting_ops)) {
                list_for_each_entry_safe (stub, tmp, &waiting_ops, list) {
                        list_del_init (&stub->list);
                        if (op_ret < 0) {
                                qr_local_t *local = NULL;

                                local = stub->frame->local;
                                local->op_ret = op_ret;
                                local->op_errno = op_errno;
                        }

                        call_resume (stub);
                }
        }

out:
        return;
}


static void
qr_fd_ctx_free (qr_fd_ctx_t *qr_fd_ctx)
{
        GF_VALIDATE_OR_GOTO ("quick-read", qr_fd_ctx, out);

        GF_ASSERT (list_empty (&qr_fd_ctx->waiting_ops));

        LOCK (&qr_fd_ctx->fd->inode->lock);
        {
                list_del_init (&qr_fd_ctx->inode_list);
        }
        UNLOCK (&qr_fd_ctx->fd->inode->lock);

        GF_FREE (qr_fd_ctx->path);
        GF_FREE (qr_fd_ctx);

out:
        return;
}


static inline uint32_t
is_match (const char *path, const char *pattern)
{
        int32_t  ret = 0;
        uint32_t match = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", path, out);
        GF_VALIDATE_OR_GOTO ("quick-read", pattern, out);

        ret = fnmatch (pattern, path, FNM_NOESCAPE);
        match = (ret == 0);

out:
        return match;
}


uint32_t
qr_get_priority (qr_conf_t *conf, const char *path)
{
        uint32_t            priority = 0;
        struct qr_priority *curr     = NULL;

        GF_VALIDATE_OR_GOTO ("quick-read", conf, out);
        GF_VALIDATE_OR_GOTO ("quick-read", path, out);

        list_for_each_entry (curr, &conf->priority_list, list) {
                if (is_match (path, curr->pattern))
                        priority = curr->priority;
        }

out:
        return priority;
}


/* To be called with this-priv->table.lock held */
qr_inode_t *
__qr_inode_alloc (xlator_t *this, char *path, inode_t *inode)
{
        qr_inode_t    *qr_inode = NULL;
        qr_private_t  *priv     = NULL;
        int            priority = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        GF_VALIDATE_OR_GOTO (this->name, path, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        qr_inode = GF_CALLOC (1, sizeof (*qr_inode), gf_qr_mt_qr_inode_t);
        if (qr_inode == NULL) {
                goto out;
        }

        INIT_LIST_HEAD (&qr_inode->lru);
        INIT_LIST_HEAD (&qr_inode->fd_list);

        priority = qr_get_priority (&priv->conf, path);

        list_add_tail (&qr_inode->lru, &priv->table.lru[priority]);

        qr_inode->inode = inode;
        qr_inode->priority = priority;
out:
        return qr_inode;
}


/* To be called with qr_inode->table->lock held */
void
__qr_inode_free (qr_inode_t *qr_inode)
{
        qr_fd_ctx_t *fdctx  = NULL, *tmp_fdctx  = NULL;

        GF_VALIDATE_OR_GOTO ("quick-read", qr_inode, out);

        if (qr_inode->xattr) {
                dict_unref (qr_inode->xattr);
        }

        list_del (&qr_inode->lru);

        LOCK (&qr_inode->inode->lock);
        {
                list_for_each_entry_safe (fdctx, tmp_fdctx, &qr_inode->fd_list,
                                          inode_list) {
                        list_del_init (&fdctx->inode_list);
                }
        }
        UNLOCK (&qr_inode->inode->lock);

        GF_FREE (qr_inode);
out:
        return;
}

/* To be called with priv->table.lock held */
void
__qr_cache_prune (xlator_t *this)
{
        qr_private_t     *priv          = NULL;
        qr_conf_t        *conf          = NULL;
        qr_inode_table_t *table         = NULL;
        qr_inode_t        *curr         = NULL, *next = NULL;
        int32_t           index         = 0;
        uint64_t          size_to_prune = 0;
        uint64_t          size_pruned   = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        table = &priv->table;
        conf = &priv->conf;

        size_to_prune = table->cache_used - conf->cache_size;

        for (index=0; index < conf->max_pri; index++) {
                list_for_each_entry_safe (curr, next, &table->lru[index], lru) {
                        size_pruned += curr->stbuf.ia_size;
                        inode_ctx_del (curr->inode, this, NULL);
                        __qr_inode_free (curr);
                        if (size_pruned >= size_to_prune)
                                goto done;
                }
        }

done:
        table->cache_used -= size_pruned;

out:
        return;
}

/* To be called with table->lock held */
inline char
__qr_need_cache_prune (qr_conf_t *conf, qr_inode_table_t *table)
{
        char need_prune = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", conf, out);
        GF_VALIDATE_OR_GOTO ("quick-read", table, out);

        need_prune = (table->cache_used > conf->cache_size);

out:
        return need_prune;
}


int32_t
qr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        data_t           *content  = NULL;
        qr_inode_t       *qr_inode = NULL;
        uint64_t          value    = 0;
        int               ret      = -1;
        qr_conf_t        *conf     = NULL;
        qr_inode_table_t *table    = NULL;
        qr_private_t     *priv     = NULL;
        qr_local_t       *local    = NULL;

        GF_ASSERT (frame);

        if ((op_ret == -1) || (xdata == NULL)) {
                goto out;
        }

        if ((this == NULL) || (this->private == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "quick-read configuration is not found");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        priv = this->private;
        conf = &priv->conf;
        table = &priv->table;

        local = frame->local;

        if (buf->ia_size > conf->max_file_size) {
                goto out;
        }

        if (IA_ISDIR (buf->ia_type)) {
                goto out;
        }

        if (inode == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup returned a NULL inode");
                goto out;
        }

        content = dict_get (xdata, GF_CONTENT_KEY);
        if (content == NULL) {
                goto out;
        }

        LOCK (&table->lock);
        {
                ret = inode_ctx_get (inode, this, &value);
                if (ret == -1) {
                        qr_inode = __qr_inode_alloc (this, local->path, inode);
                        if (qr_inode == NULL) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unlock;
                        }

                        ret = inode_ctx_put (inode, this,
                                             (uint64_t)(long)qr_inode);
                        if (ret == -1) {
                                __qr_inode_free (qr_inode);
                                qr_inode = NULL;
                                op_ret = -1;
                                op_errno = EINVAL;
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot set quick-read context in "
                                        "inode (gfid:%s)",
                                        uuid_utoa (inode->gfid));
                                goto unlock;
                        }
                } else {
                        qr_inode = (qr_inode_t *)(long)value;
                        if (qr_inode == NULL) {
                                op_ret = -1;
                                op_errno = EINVAL;
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot find quick-read context in "
                                        "inode (gfid:%s)",
                                        uuid_utoa (inode->gfid));
                                goto unlock;
                        }
                }

		/*
		 * Create our own internal dict and migrate the file content
		 * over to it so it isn't floating around in other translator
		 * caches.
		 */
                if (qr_inode->xattr) {
                        dict_unref (qr_inode->xattr);
                        qr_inode->xattr = NULL;
                        table->cache_used -= qr_inode->stbuf.ia_size;
                }

		qr_inode->xattr = dict_new();
		if (!qr_inode->xattr) {
			op_ret = -1;
			op_errno = ENOMEM;
			goto unlock;
		}

		if (dict_set(qr_inode->xattr, GF_CONTENT_KEY, content) < 0) {
			op_ret = -1;
			op_errno = ENOMEM;
			goto unlock;
		}

		dict_del(xdata, GF_CONTENT_KEY);

                qr_inode->stbuf = *buf;
                table->cache_used += buf->ia_size;

                gettimeofday (&qr_inode->tv, NULL);
                if (__qr_need_cache_prune (conf, table)) {
                        __qr_cache_prune (this);
                }
        }
unlock:
        UNLOCK (&table->lock);

out:
        /*
         * FIXME: content size in dict can be greater than the size application
         * requested for. Applications need to be careful till this is fixed.
         */
        QR_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf, xdata,
                         postparent);

        return 0;
}


int32_t
qr_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
           dict_t *xdata)
{
        qr_conf_t        *conf           = NULL;
        dict_t           *new_req_dict   = NULL;
        int32_t           op_ret         = -1, op_errno = EINVAL;
        data_t           *content        = NULL;
        uint64_t          requested_size = 0, size = 0, value = 0;
        char              cached         = 0;
        qr_inode_t       *qr_inode       = NULL;
        qr_private_t     *priv           = NULL;
        qr_inode_table_t *table          = NULL;
        qr_local_t       *local          = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (frame->this->name, priv, unwind);

        conf = &priv->conf;
        if (conf == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        table = &priv->table;

        local = qr_local_new (this);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind, op_errno,
                                        ENOMEM);

        frame->local = local;

        local->path = gf_strdup (loc->path);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind, op_errno,
                                        ENOMEM);
        LOCK (&table->lock);
        {
                op_ret = inode_ctx_get (loc->inode, this, &value);
                if (op_ret == 0) {
                        qr_inode = (qr_inode_t *)(long)value;
                        if (qr_inode != NULL) {
                                if (qr_inode->xattr) {
                                        cached = 1;
                                }
                        }
                }
        }
        UNLOCK (&table->lock);

        if ((xdata == NULL) && (conf->max_file_size > 0)) {
                new_req_dict = xdata = dict_new ();
                if (xdata == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }
        }

        if (!cached) {
                if (xdata) {
                        content = dict_get (xdata, GF_CONTENT_KEY);
                        if (content) {
                                requested_size = data_to_uint64 (content);
                        }
                }

                if ((conf->max_file_size > 0)
                    && (conf->max_file_size != requested_size)) {
                        size = (conf->max_file_size > requested_size) ?
                                conf->max_file_size : requested_size;

                        op_ret = dict_set (xdata, GF_CONTENT_KEY,
                                           data_from_uint64 (size));
                        if (op_ret < 0) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot set key in request dict to "
                                        "request file "
                                        "content during lookup cbk");
                                goto unwind;
                        }
                }
        }

        STACK_WIND (frame, qr_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);

        if (new_req_dict) {
                dict_unref (new_req_dict);
        }

        return 0;

unwind:
        QR_STACK_UNWIND (lookup, frame, op_ret, op_errno, NULL, NULL,
                         NULL, NULL);

        if (new_req_dict) {
                dict_unref (new_req_dict);
        }

        return 0;
}


int32_t
qr_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        uint64_t          value     = 0;
        int32_t           ret       = -1;
        qr_local_t       *local     = NULL;
        qr_inode_t       *qr_inode  = NULL;
        qr_fd_ctx_t      *qr_fd_ctx = NULL;
        call_stub_t      *stub      = NULL, *tmp = NULL;
        char              is_open   = 0;
        qr_private_t     *priv      = NULL;
        qr_inode_table_t *table     = NULL;
        struct list_head  waiting_ops;

        GF_ASSERT (frame);

        priv = this->private;
        table = &priv->table;

        local = frame->local;
        if (local != NULL) {
                is_open = local->is_open;
        }

        INIT_LIST_HEAD (&waiting_ops);

        ret = fd_ctx_get (fd, this, &value);
        if ((ret == -1) && (op_ret != -1)) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot find quick-read context in fd (%p) opened on "
                        "inode (gfid: %s)", fd, uuid_utoa (fd->inode->gfid));
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
                        LOCK (&table->lock);
                        {
                                ret = inode_ctx_del (fd->inode, this, &value);
                                if (ret == 0) {
                                        qr_inode = (qr_inode_t *)(long) value;

                                        if (qr_inode != NULL) {
                                                __qr_inode_free (qr_inode);
                                        }
                                }
                        }
                        UNLOCK (&table->lock);
                }

                if (!list_empty (&waiting_ops)) {
                        list_for_each_entry_safe (stub, tmp, &waiting_ops,
                                                  list) {
                                list_del_init (&stub->list);
                                if (op_ret < 0) {
                                        qr_local_t *local = NULL;

                                        local = stub->frame->local;
                                        local->op_ret = op_ret;
                                        local->op_errno = op_errno;
                                }

                                call_resume (stub);
                        }
                }
        }
out:
        if (is_open) {
                QR_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);
        } else {
                STACK_DESTROY (frame->root);
        }

        return 0;
}


int32_t
qr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        qr_inode_t       *qr_inode       = NULL;
        int32_t           ret            = -1;
        uint64_t          filep          = 0;
        char              content_cached = 0;
        qr_fd_ctx_t      *qr_fd_ctx      = NULL, *tmp_fd_ctx = NULL;
        int32_t           op_ret         = -1, op_errno = EINVAL;
        qr_local_t       *local          = NULL;
        qr_private_t     *priv           = NULL;
        qr_inode_table_t *table          = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this->private, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        priv = this->private;
        table = &priv->table;

        tmp_fd_ctx = qr_fd_ctx = GF_CALLOC (1, sizeof (*qr_fd_ctx),
                                            gf_qr_mt_qr_fd_ctx_t);
        if (qr_fd_ctx == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        LOCK_INIT (&qr_fd_ctx->lock);
        INIT_LIST_HEAD (&qr_fd_ctx->waiting_ops);
        INIT_LIST_HEAD (&qr_fd_ctx->inode_list);
        INIT_LIST_HEAD (&qr_fd_ctx->tmp_list);
        qr_fd_ctx->fd = fd;

        qr_fd_ctx->path = gf_strdup (loc->path);
        if (qr_fd_ctx->path == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        qr_fd_ctx->flags = flags;

        ret = fd_ctx_set (fd, this, (uint64_t)(long)qr_fd_ctx);
        if (ret == -1) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set quick-read context in "
                        "fd (%p) opened on inode (gfid:%s)", fd,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        tmp_fd_ctx = NULL;

        local = qr_local_new (this);
        if (local == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        local->is_open = 1;
        local->open_flags = flags;
        frame->local = local;
        LOCK (&table->lock);
        {
                ret = inode_ctx_get (fd->inode, this, &filep);
                if (ret == 0) {
                        qr_inode = (qr_inode_t *)(long) filep;
                        if (qr_inode) {
                                if (qr_inode->xattr) {
                                        content_cached = 1;
                                }
                        }
                }
        }
        UNLOCK (&table->lock);

        if (content_cached && (flags & O_DIRECTORY)) {
                op_ret = -1;
                op_errno = ENOTDIR;
                gf_log (this->name, GF_LOG_WARNING,
                        "open with O_DIRECTORY flag received on non-directory");
                goto unwind;
        }

        if (!content_cached || ((flags & O_ACCMODE) == O_WRONLY)
            || ((flags & O_TRUNC) == O_TRUNC)
            || ((flags & O_DIRECT) == O_DIRECT)) {
                LOCK (&qr_fd_ctx->lock);
                {
                        /*
                         * we really need not set this flag, since open is
                         * not yet unwound.
                         */

                        qr_fd_ctx->open_in_transit = 1;
                        if ((flags & O_DIRECT) == O_DIRECT) {
                                qr_fd_ctx->disabled = 1;
                        }
                }
                UNLOCK (&qr_fd_ctx->lock);
                goto wind;
        } else {
                op_ret = 0;
                op_errno = 0;

                LOCK (&fd->inode->lock);
                {
                        list_add_tail (&qr_fd_ctx->inode_list,
                                       &qr_inode->fd_list);
                }
                UNLOCK (&fd->inode->lock);
        }

unwind:
        if (tmp_fd_ctx != NULL) {
                qr_fd_ctx_free (tmp_fd_ctx);
        }

        QR_STACK_UNWIND (open, frame, op_ret, op_errno, fd, NULL);
        return 0;

wind:
        STACK_WIND (frame, qr_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd,
                    xdata);
        return 0;
}


static inline time_t
qr_time_elapsed (struct timeval *now, struct timeval *then)
{
        time_t time_elapsed = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", now, out);
        GF_VALIDATE_OR_GOTO ("quick-read", then, out);

        time_elapsed = now->tv_sec - then->tv_sec;

out:
        return time_elapsed;
}


static inline char
qr_need_validation (qr_conf_t *conf, qr_inode_t *qr_inode)
{
        struct timeval now             = {0, };
        char           need_validation = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", conf, out);
        GF_VALIDATE_OR_GOTO ("quick-read", qr_inode, out);

        gettimeofday (&now, NULL);

        if (qr_time_elapsed (&now, &qr_inode->tv) >= conf->cache_timeout)
                need_validation = 1;

out:
        return need_validation;
}


static int32_t
qr_validate_cache_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf,
                       dict_t *xdata)
{
        qr_inode_t       *qr_inode  = NULL;
        qr_local_t       *local     = NULL;
        uint64_t          value     = 0;
        int32_t           ret       = 0;
        qr_private_t     *priv      = NULL;
        qr_inode_table_t *table     = NULL;
        call_stub_t      *stub      = NULL;

        GF_ASSERT (frame);
        if (this == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "xlator object (this) is NULL");
                goto unwind;
        }

        local = frame->local;
        if ((local == NULL) || ((local->fd) == NULL)) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (local == NULL) ? "local is NULL"
                        : "fd is not stored in local");
                goto unwind;
        }

        local->just_validated = 1;

        if (op_ret == -1) {
                goto unwind;
        }

        priv = this->private;
        table = &priv->table;

        LOCK (&table->lock);
        {
                ret = inode_ctx_get (local->fd->inode, this, &value);
                if (ret == 0) {
                        qr_inode = (qr_inode_t *)(long) value;
                }

                if (qr_inode != NULL) {
                        gettimeofday (&qr_inode->tv, NULL);

                        if ((qr_inode->stbuf.ia_mtime != buf->ia_mtime)
                            || (qr_inode->stbuf.ia_mtime_nsec
                                != buf->ia_mtime_nsec)) {
                                inode_ctx_del (local->fd->inode, this, NULL);
                                __qr_inode_free (qr_inode);
                        }
                }
        }
        UNLOCK (&table->lock);

        stub = local->stub;
        local->stub = NULL;

        call_resume (stub);

        return 0;

unwind:
        /* this is actually unwind of readv */
        QR_STACK_UNWIND (readv, frame, op_ret, op_errno, NULL, -1, NULL, NULL,
                         NULL);
        return 0;
}


int32_t
qr_validate_cache_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                          dict_t *xdata)
{
        qr_local_t *local  = NULL;
        int32_t     op_ret = -1, op_errno = -1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, out);

        local = frame->local;
        if (local == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
        } else {
                op_ret = local->op_ret;
                op_errno = local->op_errno;
        }

out:
        if (op_ret == -1) {
                qr_validate_cache_cbk (frame, NULL, this, op_ret, op_errno,
                                       NULL, NULL);
        } else {
                STACK_WIND (frame, qr_validate_cache_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd, xdata);
        }

        return 0;
}


int
qr_validate_cache (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   call_stub_t *stub)
{
        int           ret           = -1;
        int           flags         = 0;
        uint64_t      value         = 0;
        loc_t         loc           = {0, };
        char         *path          = NULL;
        qr_local_t   *local         = NULL;
        qr_fd_ctx_t  *qr_fd_ctx     = NULL;
        call_stub_t  *validate_stub = NULL;
        char          need_open     = 0, can_wind = 0, validate_cbk_called = 0;
        call_frame_t *open_frame    = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, stub, out);

        if (frame->local == NULL) {
                local = qr_local_new (this);
                if (local == NULL) {
                        goto out;
                }
        } else {
                local = frame->local;
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
                                                                fd, NULL);
                                if (validate_stub == NULL) {
                                        ret = -1;
                                        if (need_open) {
                                                qr_fd_ctx->open_in_transit = 0;
                                        }
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
                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        validate_cbk_called = 1;
                        goto out;
                }

                ret = qr_loc_fill (&loc, fd->inode, path);
                if (ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        validate_cbk_called = 1;
                        STACK_DESTROY (open_frame->root);
                        goto out;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            &loc, flags, fd, NULL);

                qr_loc_wipe (&loc);
        } else if (can_wind) {
                STACK_WIND (frame, qr_validate_cache_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd, NULL);
        }

        ret = 0;
out:
        if ((ret < 0) && !validate_cbk_called) {
                if (frame->local == NULL) {
                        call_stub_destroy (stub);
                }

                qr_validate_cache_cbk (frame, NULL, this, -1, errno, NULL, NULL);
        }
        return ret;
}


int32_t
qr_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
        GF_ASSERT (frame);

        QR_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count,
                         stbuf, iobref, xdata);
        return 0;
}


int32_t
qr_readv_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset, uint32_t flags, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        int32_t      op_errno = EINVAL, ret = 0;
        uint64_t     value    = 0;
        qr_fd_ctx_t *fdctx   = NULL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding read call",
                        fdctx ? fdctx->path : NULL, strerror (errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv, fd, size, offset, flags,
                    xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);
        return 0;
}


int32_t
qr_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t read_flags, dict_t *xdata)
{
        qr_inode_t        *qr_inode       = NULL;
        int32_t            ret            = -1, op_ret = -1, op_errno = -1;
        uint64_t           value          = 0;
        int                count          = -1, flags = 0, i = 0;
        char               content_cached = 0, need_validation = 0;
        char               need_open      = 0, can_wind = 0, need_unwind = 0;
        struct iobuf      *iobuf          = NULL;
        struct iobref     *iobref         = NULL;
        struct iatt        stbuf          = {0, };
        data_t            *content        = NULL;
        qr_fd_ctx_t       *qr_fd_ctx      = NULL;
        call_stub_t       *stub           = NULL;
        loc_t              loc            = {0, };
        qr_conf_t         *conf           = NULL;
        struct iovec      *vector         = NULL;
        char              *path           = NULL;
        off_t              start          = 0, end = 0;
        size_t             len            = 0;
        struct iobuf_pool *iobuf_pool     = NULL;
        qr_local_t        *local          = NULL;
        char               just_validated = 0;
        qr_private_t      *priv           = NULL;
        qr_inode_table_t  *table          = NULL;
        call_frame_t      *open_frame     = NULL;

        op_ret = 0;

        priv = this->private;
        conf = &priv->conf;
        table = &priv->table;

        local = frame->local;

        if (local != NULL) {
                just_validated = local->just_validated;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
                if (qr_fd_ctx != NULL) {
                        if (qr_fd_ctx->disabled) {
                                goto out;
                        }
                }
        }

        iobuf_pool = this->ctx->iobuf_pool;

        LOCK (&table->lock);
        {
                ret = inode_ctx_get (fd->inode, this, &value);
                if (ret)
                        goto unlock;

                qr_inode = (qr_inode_t *)(long)value;
                if (!qr_inode || !qr_inode->xattr)
                        goto unlock;

                if (!just_validated
                    && qr_need_validation (conf, qr_inode)) {
                        need_validation = 1;
                        goto unlock;
                }

                content = dict_get (qr_inode->xattr, GF_CONTENT_KEY);

                stbuf = qr_inode->stbuf;
                content_cached = 1;
                list_move_tail (&qr_inode->lru,
                                &table->lru[qr_inode->priority]);

                if (offset > content->len) {
                        op_ret = 0;
                        end = content->len;
                } else {
                        if ((offset + size) > content->len) {
                                op_ret = content->len - offset;
                                end = content->len;
                        } else {
                                op_ret = size;
                                end =  offset + size;
                        }
                }

                count = (op_ret  / iobuf_pool->default_page_size);
                if ((op_ret % iobuf_pool->default_page_size) != 0) {
                        count++;
                }

                if (count == 0) {
                        op_ret = 0;
                        goto unlock;
                }

                vector = GF_CALLOC (count, sizeof (*vector), gf_qr_mt_iovec);
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
                        /* TODO: Now that we have support for variable
                           io-buf-sizes, i guess we need to get rid of
                           default size here */
                        iobuf = iobuf_get (iobuf_pool);
                        if (iobuf == NULL) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                need_unwind = 1;
                                goto unlock;
                        }

                        start = offset + (iobuf_pool->default_page_size * i);

                        if (start > end) {
                                len = 0;
                        } else {
                                len = (iobuf_pool->default_page_size >
                                       ((end - start)) ? (end - start) :
                                       iobuf_pool->default_page_size);

                                memcpy (iobuf->ptr, content->data + start, len);
                        }

                        iobref_add (iobref, iobuf);
                        iobuf_unref (iobuf);

                        vector[i].iov_base = iobuf->ptr;
                        vector[i].iov_len = len;
                }
        }
unlock:
        UNLOCK (&table->lock);

out:
        if (content_cached || need_unwind) {
                QR_STACK_UNWIND (readv, frame, op_ret, op_errno, vector,
                                 count, &stbuf, iobref, NULL);

        } else if (need_validation) {
                stub = fop_readv_stub (frame, qr_readv, fd, size, offset,
                                       read_flags, xdata);
                if (stub == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                qr_validate_cache (frame, this, fd, stub);
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
                                        if (frame->local == NULL) {
                                                frame->local
                                                        = qr_local_new (this);
                                                if (frame->local == NULL) {
                                                        op_ret = -1;
                                                        op_errno = ENOMEM;
                                                        need_unwind = 1;
                                                        qr_fd_ctx->open_in_transit = 0;
                                                        goto fdctx_unlock;
                                                }
                                        }

                                        stub = fop_readv_stub (frame,
                                                               qr_readv_helper,
                                                               fd, size,
                                                               offset,
                                                               read_flags, xdata);
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
                                qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                                goto ret;
                        }

                        open_frame = create_frame (this, this->ctx->pool);
                        if (open_frame == NULL) {
                                qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                                qr_loc_wipe (&loc);
                                goto ret;
                        }

                        STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->open,
                                    &loc, flags, fd, NULL);

                        qr_loc_wipe (&loc);
                } else if (can_wind) {
                        STACK_WIND (frame, qr_readv_cbk, FIRST_CHILD (this),
                                    FIRST_CHILD (this)->fops->readv, fd, size,
                                    offset, read_flags, xdata);
                }
        }

ret:
        if (vector) {
                GF_FREE (vector);
        }

        if (iobref) {
                iobref_unref (iobref);
        }

        return 0;
}


int32_t
qr_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf,
                         xdata);
        return 0;
}


int32_t
qr_writev_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iovec *vector, int32_t count, off_t off,
                  uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding write call",
                        fdctx ? fdctx->path : NULL, strerror (errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_writev_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev, fd, vector, count, off,
                    flags, iobref, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
qr_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, uint32_t wr_flags, struct iobref *iobref,
           dict_t *xdata)
{
        uint64_t          value      = 0;
        int               flags      = 0;
        call_stub_t      *stub       = NULL;
        char             *path       = NULL;
        loc_t             loc        = {0, };
        qr_inode_t       *qr_inode   = NULL;
        qr_fd_ctx_t      *qr_fd_ctx  = NULL;
        int32_t           op_ret     = -1, op_errno = -1, ret = -1;
        char              can_wind   = 0, need_unwind = 0, need_open = 0;
        qr_private_t     *priv       = NULL;
        qr_inode_table_t *table      = NULL;
        call_frame_t     *open_frame = NULL;

        priv = this->private;
        table = &priv->table;

        ret = fd_ctx_get (fd, this, &value);

        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
        }

        LOCK (&table->lock);
        {
                ret = inode_ctx_get (fd->inode, this, &value);
                if (ret == 0) {
                        qr_inode = (qr_inode_t *)(long)value;
                        if (qr_inode != NULL) {
                                inode_ctx_del (fd->inode, this, NULL);
                                __qr_inode_free (qr_inode);
                        }
                }
        }
        UNLOCK (&table->lock);

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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_writev_stub (frame, qr_writev_helper,
                                                        fd, vector, count, off,
                                                        wr_flags, iobref, xdata);
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

        if (need_unwind) {
                QR_STACK_UNWIND (writev, frame, op_ret, op_errno, NULL, NULL, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_writev_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->writev, fd, vector, count,
                            off, wr_flags, iobref, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        QR_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int32_t
qr_fstat_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding fstat call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (fstat, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
qr_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        uint64_t      value      = 0;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        call_stub_t  *stub       = NULL;
        loc_t         loc        = {0, };
        char         *path       = NULL;
        int           flags      = 0;
        call_frame_t *open_frame = NULL;

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto unwind;
        }

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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_fstat_stub (frame, qr_fstat_helper,
                                                       fd, xdata);
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

unwind:
        if (need_unwind) {
                QR_STACK_UNWIND (fstat, frame, op_ret, op_errno, NULL, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fstat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, preop, postop,
                         xdata);
        return 0;
}


int32_t
qr_fsetattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding fsetattr "
                        "call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_fsetattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr, fd, stbuf,
                    valid, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
qr_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        uint64_t      value      = 0;
        int           flags      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL" :
                        "fd is NULL");
                need_unwind = 1;
                goto out;
        }

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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_fsetattr_stub (frame,
                                                          qr_fsetattr_helper,
                                                          fd, stbuf, valid, xdata);
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
                QR_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, NULL, NULL,
                                 NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsetattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetattr, fd, stbuf,
                            valid, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int32_t
qr_fsetxattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     dict_t *dict, int32_t flags, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding fsetxattr "
                        "call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags,
                    xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
qr_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        int           open_flags = 0;
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) "
                        "is NULL" : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_fsetxattr_stub (frame,
                                                           qr_fsetxattr_helper,
                                                           fd, dict, flags, xdata);
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
                QR_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetxattr, fd, dict,
                            flags, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, open_flags,
                            fd, NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int32_t
qr_fgetxattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding fgetxattr "
                        "call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (fgetxattr, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
qr_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
              dict_t *xdata)
{
        int           flags      = 0;
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        /*
         * FIXME: Can quick-read use the extended attributes stored in the
         * cache? this needs to be discussed.
         */

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL" :
                        "fd is NULL");
                need_unwind = 1;
                goto out;
        }

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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_fgetxattr_stub (frame,
                                                           qr_fgetxattr_helper,
                                                           fd, name, xdata);
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
                QR_STACK_UNWIND (open, frame, op_ret, op_errno, NULL, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fgetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (flush, frame, op_ret, op_errno, xdata);
        return 0;
}


int32_t
qr_flush_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding flush call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (flush, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
qr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        uint64_t     value     = 0;
        call_stub_t *stub      = NULL;
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret       = -1, op_ret = -1, op_errno = EINVAL;
        char         can_wind  = 0, need_unwind = 0;

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        if (qr_fd_ctx) {
                LOCK (&qr_fd_ctx->lock);
                {
                        if (qr_fd_ctx->opened) {
                                can_wind = 1;
                        } else if (qr_fd_ctx->open_in_transit) {
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_flush_stub (frame, qr_flush_helper,
                                                       fd, xdata);
                                if (stub == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                list_add_tail (&stub->list,
                                               &qr_fd_ctx->waiting_ops);
                        } else {
                                op_ret = 0;
                                need_unwind = 1;
                        }
                }
        unlock:
                UNLOCK (&qr_fd_ctx->lock);
        } else {
                can_wind = 1;
        }

out:
        if (need_unwind) {
                QR_STACK_UNWIND (flush, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_flush_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->flush, fd, xdata);
        }

        return 0;
}


int32_t
qr_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (fentrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int32_t
qr_fentrylk_helper (call_frame_t *frame, xlator_t *this, const char *volume,
                    fd_t *fd, const char *basename, entrylk_cmd cmd,
                    entrylk_type type, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding fentrylk "
                        "call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND(frame, qr_fentrylk_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fentrylk, volume, fd, basename,
                   cmd, type, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (fentrylk, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
qr_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
        int           flags      = 0;
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_fentrylk_stub (frame,
                                                          qr_fentrylk_helper,
                                                          volume, fd, basename,
                                                          cmd, type, xdata);
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
                QR_STACK_UNWIND (fentrylk, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fentrylk_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fentrylk, volume, fd,
                            basename, cmd, type, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int32_t
qr_finodelk_helper (call_frame_t *frame, xlator_t *this, const char *volume,
                    fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding finodelk "
                        "call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_finodelk_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->finodelk, volume, fd, cmd, lock,
                    xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (finodelk, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
qr_finodelk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        int           flags      = 0;
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_finodelk_stub (frame,
                                                          qr_finodelk_helper,
                                                          volume, fd, cmd,
                                                          lock, xdata);
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
                QR_STACK_UNWIND (finodelk, frame, op_ret, op_errno, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_finodelk_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->finodelk, volume, fd,
                            cmd, lock, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
              dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}


int32_t
qr_fsync_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
                 dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding fsync call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->fsync, fd, flags, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
qr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
          dict_t *xdata)
{
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        int           open_flags = 0;
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_fsync_stub (frame, qr_fsync_helper,
                                                       fd, flags, xdata);
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
                QR_STACK_UNWIND (fsync, frame, op_ret, op_errno, NULL, NULL, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_fsync_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsync, fd, flags, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, open_flags,
                            fd, NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        int32_t           ret      = 0;
        uint64_t          value    = 0;
        qr_inode_t       *qr_inode = NULL;
        qr_local_t       *local    = NULL;
        qr_private_t     *priv     = NULL;
        qr_inode_table_t *table    = NULL;

        GF_ASSERT (frame);

        if (op_ret == -1) {
                goto out;
        }

        local = frame->local;
        if ((local == NULL) || (local->fd == NULL)
            || (local->fd->inode == NULL)) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (frame->this->name, GF_LOG_WARNING, "cannot get inode");
                goto out;
        }

        if ((this == NULL) || (this->private == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "cannot get quick read configuration from xlator "
                        "object");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        priv = this->private;
        table = &priv->table;

        LOCK (&table->lock);
        {
                ret = inode_ctx_get (local->fd->inode, this, &value);
                if (ret == 0) {
                        qr_inode = (qr_inode_t *)(long) value;

                        if (qr_inode) {
                                if (qr_inode->stbuf.ia_size != postbuf->ia_size)
                                {
                                        inode_ctx_del (local->fd->inode, this,
                                                       NULL);
                                        __qr_inode_free (qr_inode);
                                }
                        }
                }
        }
        UNLOCK (&table->lock);

out:
        QR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, prebuf,
                         postbuf, xdata);
        return 0;
}


int32_t
qr_ftruncate_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding ftruncate "
                        "call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;

unwind:
        QR_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
qr_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
        int           flags      = 0;
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        qr_local_t   *local      = NULL;
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
        }

        local = qr_local_new (this);
        if (local == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                need_unwind = 1;
                goto out;
        }

        local->fd = fd;
        frame->local = local;

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
                                stub = fop_ftruncate_stub (frame,
                                                           qr_ftruncate_helper,
                                                           fd, offset, xdata);
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
                QR_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, NULL,
                                 NULL, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_ftruncate_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
           int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        GF_ASSERT (frame);
        QR_STACK_UNWIND (lk, frame, op_ret, op_errno, lock, xdata);
        return 0;
}


int32_t
qr_lk_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
              struct gf_flock *lock, dict_t *xdata)
{
        qr_local_t  *local    = NULL;
        qr_fd_ctx_t *fdctx    = NULL;
        uint64_t     value    = 0;
        int32_t      ret      = 0;
        int32_t      op_errno = EINVAL;

        GF_ASSERT (frame);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, fd, unwind);

        if (local->op_ret < 0) {
                op_errno = local->op_errno;

                ret = fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fdctx = (qr_fd_ctx_t *)(long) value;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "open failed on path (%s) (%s), unwinding lk call",
                        fdctx ? fdctx->path : NULL, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, qr_lk_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk, fd, cmd, lock, xdata);

        return 0;

unwind:
        QR_STACK_UNWIND (lk, frame, -1, op_errno, lock, xdata);
        return 0;
}


int32_t
qr_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
       struct gf_flock *lock, dict_t *xdata)
{
        int           flags      = 0;
        uint64_t      value      = 0;
        call_stub_t  *stub       = NULL;
        char         *path       = NULL;
        loc_t         loc        = {0, };
        qr_fd_ctx_t  *qr_fd_ctx  = NULL;
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL;
        char          need_open  = 0, can_wind = 0, need_unwind = 0;
        call_frame_t *open_frame = NULL;

        GF_ASSERT (frame);
        if ((this == NULL) || (fd == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "fd is NULL");
                need_unwind = 1;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long)value;
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
                                frame->local = qr_local_new (this);
                                if (frame->local == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        need_unwind = 1;
                                        qr_fd_ctx->open_in_transit = 0;
                                        goto unlock;
                                }

                                stub = fop_lk_stub (frame, qr_lk_helper, fd,
                                                    cmd, lock, xdata);
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
                QR_STACK_UNWIND (lk, frame, op_ret, op_errno, NULL, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, qr_lk_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lk, fd, cmd, lock, xdata);
        } else if (need_open) {
                op_ret = qr_loc_fill (&loc, fd->inode, path);
                if (op_ret == -1) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, errno);
                        goto ret;
                }

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (qr_fd_ctx, -1, ENOMEM);
                        qr_loc_wipe (&loc);
                        goto ret;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, flags, fd,
                            NULL);

                qr_loc_wipe (&loc);
        }

ret:
        return 0;
}


int32_t
qr_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        QR_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent,
                         postparent, xdata);
        return 0;
}


int32_t
qr_unlink_helper (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                  dict_t *xdata)
{
        qr_local_t  *local      = NULL;
        uint32_t     open_count = 0;
        qr_fd_ctx_t *fdctx      = NULL, *tmp = NULL;

        local = frame->local;

        LOCK (&local->lock);
        {
                open_count = --local->open_count;
        }
        UNLOCK (&local->lock);

        if (open_count > 0) {
                goto out;
        }

        list_for_each_entry_safe (fdctx, tmp, &local->fd_list, tmp_list) {
                fd_unref (fdctx->fd);
        }

        STACK_WIND (frame, qr_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);

out:
        return 0;
}


int32_t
qr_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
           dict_t *xdata)
{
        int32_t           op_errno   = -1, ret = -1, op_ret = -1;
        uint64_t          value      = 0;
        struct list_head  fd_list    = {0, };
        char              need_open  = 0;
        qr_local_t       *local      = NULL;
        qr_fd_ctx_t      *fdctx      = NULL, *tmp = NULL;
        call_frame_t     *open_frame = NULL;
        call_stub_t      *stub       = NULL;
        qr_inode_t       *qr_inode   = NULL;
        uint32_t          open_count = 0;
        char              ignore     = 0;

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == 0) {
                qr_inode = (qr_inode_t *)(unsigned long)value;
        }

        if (qr_inode == NULL) {
                goto wind;
        }

        INIT_LIST_HEAD (&fd_list);

        local = qr_local_new (this);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind, op_errno,
                                        ENOMEM);

        frame->local = local;

        LOCK (&loc->inode->lock);
        {
                list_for_each_entry (fdctx, &qr_inode->fd_list, inode_list) {
                        __fd_ref (fdctx->fd);
                        list_add_tail (&fdctx->tmp_list, &fd_list);
                }
        }
        UNLOCK (&loc->inode->lock);

        op_ret = 0;

        LOCK (&local->lock);
        {
                list_for_each_entry_safe (fdctx, tmp, &fd_list, tmp_list) {
                        need_open = 0;
                        ignore = 0;

                        LOCK (&fdctx->lock);
                        {
                                if (qr_inode->stbuf.ia_nlink == 1) {
                                        fdctx->disabled = 1;
                                }

                                if ((fdctx->opened)
                                    || (strcmp (loc->path, fdctx->path) != 0)) {
                                        list_del (&fdctx->tmp_list);
                                        __fd_unref (fdctx->fd);
                                        ignore = 1;
                                        goto unlock;
                                }

                                if (!(fdctx->opened
                                      || fdctx->open_in_transit)) {
                                        need_open = 1;
                                        fdctx->open_in_transit = 1;
                                }

                                if (!fdctx->opened) {
                                        stub = fop_unlink_stub (frame,
                                                                qr_unlink_helper,
                                                                loc, xflag,
                                                                xdata);
                                        if (stub == NULL) {
                                                op_ret = -1;
                                                op_errno = ENOMEM;
                                                fdctx->open_in_transit = 0;
                                                goto unlock;
                                        }

                                        list_add_tail (&stub->list,
                                                       &fdctx->waiting_ops);
                                }

                                local->open_count++;
                        }
                unlock:
                        UNLOCK (&fdctx->lock);

                        if (op_ret == -1) {
                                break;
                        }

                        if (!need_open && !ignore) {
                                list_move_tail (&fdctx->tmp_list,
                                                &local->fd_list);
                        }
                }

                open_count = local->open_count;
        }
        UNLOCK (&local->lock);

        if (op_ret == -1) {
                goto unwind;
        }

        if (open_count == 0) {
                goto wind;
        }

        list_for_each_entry_safe (fdctx, tmp, &fd_list, tmp_list) {
                LOCK (&local->lock);
                {
                        list_move_tail (&fdctx->tmp_list, &local->fd_list);
                }
                UNLOCK (&local->lock);

                open_frame = create_frame (this, this->ctx->pool);
                if (open_frame == NULL) {
                        qr_resume_pending_ops (fdctx, -1, ENOMEM);
                        continue;
                }

                STACK_WIND (open_frame, qr_open_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            loc, fdctx->flags, fdctx->fd, fdctx->xdata);
        }

        return 0;

unwind:
        QR_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;

wind:
        STACK_WIND (frame, qr_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
        return 0;
}


int32_t
qr_release (xlator_t *this, fd_t *fd)
{
        qr_fd_ctx_t *qr_fd_ctx = NULL;
        int32_t      ret       = 0;
        uint64_t     value     = 0;

        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &value);
        if (ret == 0) {
                qr_fd_ctx = (qr_fd_ctx_t *)(long) value;
                if (qr_fd_ctx) {
                        qr_fd_ctx_free (qr_fd_ctx);
                }
        }

out:
        return 0;
}


int32_t
qr_forget (xlator_t *this, inode_t *inode)
{
        qr_inode_t   *qr_inode = NULL;
        uint64_t      value     = 0;
        int32_t       ret       = -1;
        qr_private_t *priv      = NULL;

        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        priv = this->private;

        LOCK (&priv->table.lock);
        {
                ret = inode_ctx_del (inode, this, &value);
                if (ret == 0) {
                        qr_inode = (qr_inode_t *)(long) value;
                        __qr_inode_free (qr_inode);
                }
        }
        UNLOCK (&priv->table.lock);

out:
        return 0;
}


int32_t
qr_inodectx_dump (xlator_t *this, inode_t *inode)
{
        qr_inode_t *qr_inode = NULL;
        uint64_t    value    = 0;
        int32_t     ret      = -1;
        char        key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        char        buf[256]                        = {0, };
        struct tm  *tm                              = NULL;
        ret = inode_ctx_get (inode, this, &value);
        if (ret != 0) {
                goto out;
        }

        qr_inode = (qr_inode_t *)(long)value;
        if (qr_inode == NULL) {
                goto out;
        }

        gf_proc_dump_build_key (key_prefix, "xlator.performance.quick-read",
                                "inodectx");
        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("entire-file-cached", "%s", qr_inode->xattr ? "yes" : "no");

        if (qr_inode->tv.tv_sec) {
                tm = localtime (&qr_inode->tv.tv_sec);
                strftime (buf, 256, "%Y-%m-%d %H:%M:%S", tm);
                snprintf (buf + strlen (buf), 256 - strlen (buf),
                          ".%"GF_PRI_SUSECONDS, qr_inode->tv.tv_usec);

                gf_proc_dump_write ("last-cache-validation-time", "%s", buf);
        }

        ret = 0;
out:
        return ret;
}

int32_t
qr_fdctx_dump (xlator_t *this, fd_t *fd)
{
        qr_fd_ctx_t *fdctx = NULL;
        uint64_t     value = 0;
        int32_t      ret   = 0, i = 0;
        char         key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char         key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        call_stub_t *stub                            = NULL;

        ret = fd_ctx_get (fd, this, &value);
        if (ret != 0) {
                goto out;
        }

        fdctx = (qr_fd_ctx_t *)(long)value;
        if (fdctx == NULL) {
                goto out;
        }

        gf_proc_dump_build_key (key_prefix, "xlator.performance.quick-read",
                                "fdctx");
        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("fd", "%p", fd);

        gf_proc_dump_write ("path", "%s", fdctx->path);

        LOCK (&fdctx->lock);
        {
                gf_proc_dump_write ("opened", "%s", fdctx->opened ? "yes" : "no");

                gf_proc_dump_write ("open-in-progress", "%s", fdctx->open_in_transit ?
                                    "yes" : "no");

                gf_proc_dump_write ("caching disabled (for this fd)", "%s",
                                    fdctx->disabled ? "yes" : "no");

                gf_proc_dump_write ("flags", "%d", fdctx->flags);

                list_for_each_entry (stub, &fdctx->waiting_ops, list) {
                        gf_proc_dump_build_key (key, "",
                                                "waiting-ops[%d].frame", i);
                        gf_proc_dump_write (key, "%"PRId64,
                                            stub->frame->root->unique);

                        gf_proc_dump_build_key (key, "",
                                                "waiting-ops[%d].fop", i);
                        gf_proc_dump_write (key, "%s", gf_fop_list[stub->fop]);

                        i++;
                }
        }
        UNLOCK (&fdctx->lock);

        ret = 0;
out:
        return ret;
}

int
qr_priv_dump (xlator_t *this)
{
        qr_conf_t        *conf       = NULL;
        qr_private_t     *priv       = NULL;
        qr_inode_table_t *table      = NULL;
        uint32_t          file_count = 0;
        uint32_t          i          = 0;
        qr_inode_t       *curr       = NULL;
        uint64_t          total_size = 0;
        char              key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this) {
                return -1;
        }

        priv = this->private;
        conf = &priv->conf;

        if (!conf) {
                gf_log (this->name, GF_LOG_WARNING, "conf null in xlator");
                return -1;
        }

        table = &priv->table;


        gf_proc_dump_build_key (key_prefix, "xlator.performance.quick-read",
                                "priv");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("max_file_size", "%d", conf->max_file_size);
        gf_proc_dump_write ("cache_timeout", "%d", conf->cache_timeout);

        if (!table) {
                gf_log (this->name, GF_LOG_WARNING, "table is NULL");
                goto out;
        } else {
                for (i = 0; i < conf->max_pri; i++) {
                        list_for_each_entry (curr, &table->lru[i], lru) {
                                file_count++;
                                total_size += curr->stbuf.ia_size;
                        }
                }
        }

        gf_proc_dump_write ("total_files_cached", "%d", file_count);
        gf_proc_dump_write ("total_cache_used", "%d", total_size);

out:
        return 0;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_qr_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                return ret;
        }

        return ret;
}

gf_boolean_t
check_cache_size_ok (xlator_t *this, int64_t cache_size)
{
        int                     ret = _gf_true;
        uint64_t                total_mem = 0;
        uint64_t                max_cache_size = 0;
        volume_option_t         *opt = NULL;

        GF_ASSERT (this);
        opt = xlator_volume_option_get (this, "cache-size");
        if (!opt) {
                ret = _gf_false;
                gf_log (this->name, GF_LOG_ERROR,
                        "could not get cache-size option");
                goto out;
        }

        total_mem = get_mem_size ();
        if (-1 == total_mem)
                max_cache_size = opt->max;
        else
                max_cache_size = total_mem;

        gf_log (this->name, GF_LOG_INFO, "Max cache size is %"PRIu64,
                max_cache_size);
        if (cache_size > max_cache_size) {
                ret = _gf_false;
                gf_log (this->name, GF_LOG_ERROR, "Cache size %"PRIu64
                        " is greater than the max size of %"PRIu64,
                        cache_size, max_cache_size);
                goto out;
        }
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t       ret            = -1;
        qr_private_t *priv           = NULL;
        qr_conf_t    *conf           = NULL;
        uint64_t       cache_size_new = 0;
        GF_VALIDATE_OR_GOTO ("quick-read", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);

        priv = this->private;

        conf = &priv->conf;
        if (!conf) {
                goto out;
        }

        GF_OPTION_RECONF ("cache-timeout", conf->cache_timeout, options, int32,
                          out);

        GF_OPTION_RECONF ("cache-size", cache_size_new, options, size, out);
        if (!check_cache_size_ok (this, cache_size_new)) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Not reconfiguring cache-size");
                goto out;
        }
        conf->cache_size = cache_size_new;

        ret = 0;
out:
        return ret;
}


int32_t
qr_get_priority_list (const char *opt_str, struct list_head *first)
{
        int32_t              max_pri      = 1;
        char                *tmp_str      = NULL;
        char                *tmp_str1     = NULL;
        char                *tmp_str2     = NULL;
        char                *dup_str      = NULL;
        char                *priority_str = NULL;
        char                *pattern      = NULL;
        char                *priority     = NULL;
        char                *string       = NULL;
        struct qr_priority  *curr         = NULL, *tmp = NULL;

        GF_VALIDATE_OR_GOTO ("quick-read", opt_str, out);
        GF_VALIDATE_OR_GOTO ("quick-read", first, out);

        string = gf_strdup (opt_str);
        if (string == NULL) {
                max_pri = -1;
                goto out;
        }

        /* Get the pattern for cache priority.
         * "option priority *.jpg:1,abc*:2" etc
         */
        /* TODO: inode_lru in table is statically hard-coded to 5,
         * should be changed to run-time configuration
         */
        priority_str = strtok_r (string, ",", &tmp_str);
        while (priority_str) {
                curr = GF_CALLOC (1, sizeof (*curr), gf_qr_mt_qr_priority_t);
                if (curr == NULL) {
                        max_pri = -1;
                        goto out;
                }

                list_add_tail (&curr->list, first);

                dup_str = gf_strdup (priority_str);
                if (dup_str == NULL) {
                        max_pri = -1;
                        goto out;
                }

                pattern = strtok_r (dup_str, ":", &tmp_str1);
                if (!pattern) {
                        max_pri = -1;
                        goto out;
                }

                priority = strtok_r (NULL, ":", &tmp_str1);
                if (!priority) {
                        max_pri = -1;
                        goto out;
                }

                gf_log ("quick-read", GF_LOG_TRACE,
                        "quick-read priority : pattern %s : priority %s",
                        pattern,
                        priority);

                curr->pattern = gf_strdup (pattern);
                if (curr->pattern == NULL) {
                        max_pri = -1;
                        goto out;
                }

                curr->priority = strtol (priority, &tmp_str2, 0);
                if (tmp_str2 && (*tmp_str2)) {
                        max_pri = -1;
                        goto out;
                } else {
                        max_pri = max (max_pri, curr->priority);
                }

                GF_FREE (dup_str);
                dup_str = NULL;

                priority_str = strtok_r (NULL, ",", &tmp_str);
        }
out:
        if (string != NULL) {
                GF_FREE (string);
        }

        if (dup_str != NULL) {
                GF_FREE (dup_str);
        }

        if (max_pri == -1) {
                list_for_each_entry_safe (curr, tmp, first, list) {
                        list_del_init (&curr->list);
                        GF_FREE (curr->pattern);
                        GF_FREE (curr);
                }
        }

        return max_pri;
}


int32_t
init (xlator_t *this)
{
        int32_t       ret  = -1, i = 0;
        qr_private_t *priv = NULL;
        qr_conf_t    *conf = NULL;

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

        priv = GF_CALLOC (1, sizeof (*priv), gf_qr_mt_qr_private_t);
        if (priv == NULL) {
                ret = -1;
                goto out;
        }

        LOCK_INIT (&priv->table.lock);
        conf = &priv->conf;

        GF_OPTION_INIT ("max-file-size", conf->max_file_size, size, out);

        GF_OPTION_INIT ("cache-timeout", conf->cache_timeout, int32, out);

        GF_OPTION_INIT ("cache-size", conf->cache_size, size, out);
        if (!check_cache_size_ok (this, conf->cache_size)) {
                ret = -1;
                goto out;
        }

        INIT_LIST_HEAD (&conf->priority_list);
        conf->max_pri = 1;
        if (dict_get (this->options, "priority")) {
                char *option_list = data_to_str (dict_get (this->options,
                                                           "priority"));
                gf_log (this->name, GF_LOG_TRACE,
                        "option path %s", option_list);
                /* parse the list of pattern:priority */
                conf->max_pri = qr_get_priority_list (option_list,
                                                      &conf->priority_list);

                if (conf->max_pri == -1) {
                        goto out;
                }
                conf->max_pri ++;
        }

        priv->table.lru = GF_CALLOC (conf->max_pri, sizeof (*priv->table.lru),
                                     gf_common_mt_list_head);
        if (priv->table.lru == NULL) {
                ret = -1;
                goto out;
        }

        for (i = 0; i < conf->max_pri; i++) {
                INIT_LIST_HEAD (&priv->table.lru[i]);
        }

        this->local_pool = mem_pool_new (qr_local_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto out;
        }

        ret = 0;

        this->private = priv;
out:
        if ((ret == -1) && priv) {
                GF_FREE (priv);
        }

        return ret;
}


void
qr_inode_table_destroy (qr_private_t *priv)
{
        int        i    = 0;
        qr_conf_t *conf = NULL;

        conf = &priv->conf;

        for (i = 0; i < conf->max_pri; i++) {
                GF_ASSERT (list_empty (&priv->table.lru[i]));
        }

        LOCK_DESTROY (&priv->table.lock);

        return;
}


void
qr_conf_destroy (qr_conf_t *conf)
{
        struct qr_priority *curr = NULL, *tmp = NULL;

        list_for_each_entry_safe (curr, tmp, &conf->priority_list, list) {
                list_del (&curr->list);
                GF_FREE (curr->pattern);
                GF_FREE (curr);
        }

        return;
}


void
fini (xlator_t *this)
{
        qr_private_t *priv = NULL;

        if (this == NULL) {
                goto out;
        }

        priv = this->private;
        if (priv == NULL) {
                goto out;
        }

        qr_inode_table_destroy (priv);
        qr_conf_destroy (&priv->conf);

        this->private = NULL;

        GF_FREE (priv);
out:
        return;
}

struct xlator_fops fops = {
        .lookup      = qr_lookup,
        .open        = qr_open,
        .readv       = qr_readv,
        .writev      = qr_writev,
        .fstat       = qr_fstat,
        .fsetxattr   = qr_fsetxattr,
        .fgetxattr   = qr_fgetxattr,
        .flush       = qr_flush,
        .fentrylk    = qr_fentrylk,
        .finodelk    = qr_finodelk,
        .fsync       = qr_fsync,
        .ftruncate   = qr_ftruncate,
        .lk          = qr_lk,
        .fsetattr    = qr_fsetattr,
        .unlink      = qr_unlink,
};

struct xlator_cbks cbks = {
        .forget  = qr_forget,
        .release = qr_release,
};

struct xlator_dumpops dumpops = {
        .priv      =  qr_priv_dump,
        .inodectx  =  qr_inodectx_dump,
        .fdctx     =  qr_fdctx_dump
};

struct volume_options options[] = {
        { .key  = {"priority"},
          .type = GF_OPTION_TYPE_ANY
        },
        { .key  = {"cache-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 0,
          .max  = 32 * GF_UNIT_GB,
          .default_value = "128MB",
          .description = "Size of the read cache."
        },
        { .key  = {"cache-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = 60,
          .default_value = "1",
        },
        { .key  = {"max-file-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 0,
          .max  = 1 * GF_UNIT_KB * 1000,
          .default_value = "64KB",
        },
};
