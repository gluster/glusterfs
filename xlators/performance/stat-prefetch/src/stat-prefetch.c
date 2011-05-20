/*
  Copyright (c) 2009-2010 Gluster, Inc. <http://www.gluster.com>
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

#include "stat-prefetch.h"
#include "statedump.h"

#define GF_SP_CACHE_BUCKETS 1
#define GF_SP_CACHE_ENTRIES_EXPECTED (128 * 1024) //1048576

typedef enum {
        SP_EXPECT,
        SP_DONT_EXPECT,
        SP_DONT_CARE
} sp_expect_t;


void
sp_inode_ctx_free (xlator_t *this, sp_inode_ctx_t *ctx)
{
        call_stub_t *stub = NULL, *tmp = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        LOCK (&ctx->lock);
        {
                if (!list_empty (&ctx->waiting_ops)) {
                        gf_log (this->name, GF_LOG_WARNING, "inode ctx is "
                                "being freed even when there are file "
                                "operations waiting for lookup-behind to "
                                "complete. The operations in the waiting list "
                                "are:");
                        list_for_each_entry_safe (stub, tmp, &ctx->waiting_ops,
                                                  list) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "OP (%s)", gf_fop_list[stub->fop]);

                                list_del_init (&stub->list);
                                call_stub_destroy (stub);
                        }
                }
        }
        UNLOCK (&ctx->lock);

        LOCK_DESTROY (&ctx->lock);
        GF_FREE (ctx);

out:
        return;
}


sp_inode_ctx_t *
sp_inode_ctx_init ()
{
        sp_inode_ctx_t *inode_ctx = NULL;

        inode_ctx = GF_CALLOC (1, sizeof (*inode_ctx), gf_sp_mt_sp_inode_ctx_t);
        if (inode_ctx == NULL) {
                goto out;
        }

        LOCK_INIT (&inode_ctx->lock);
        INIT_LIST_HEAD (&inode_ctx->waiting_ops);

out:
        return inode_ctx;
}


int
sp_update_inode_ctx (xlator_t *this, inode_t *inode, int32_t *op_ret,
                     int32_t *op_errno, char *lookup_in_progress,
                     char *looked_up, struct iatt *stbuf,
                     struct list_head *waiting_ops, int32_t *error)
{
        int32_t         ret       = -1;
        sp_inode_ctx_t *inode_ctx = NULL;
        uint64_t        value     = 0;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_get (inode, this, &value);
        if (ret == 0) {
                inode_ctx = (sp_inode_ctx_t *)(long)value;
        }

        if (inode_ctx == NULL) {
                ret = -1;
                if (error != NULL) {
                        *error = EINVAL;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", inode->ino,
                        uuid_utoa (inode->gfid));
                goto out;
        }

        LOCK (&inode_ctx->lock);
        {
                if (op_ret != NULL) {
                        inode_ctx->op_ret = *op_ret;
                }

                if (op_errno != NULL) {
                        inode_ctx->op_errno = *op_errno;
                }

                if (looked_up != NULL) {
                        inode_ctx->looked_up = *looked_up;
                }

                if (lookup_in_progress != NULL) {
                        inode_ctx->lookup_in_progress = *lookup_in_progress;
                }

                if ((op_ret != NULL ) && (*op_ret == 0) && (stbuf != NULL)
                    && IA_ISDIR (stbuf->ia_type)) {
                        memcpy (&inode_ctx->stbuf, stbuf, sizeof (*stbuf));
                }

                if (waiting_ops != NULL) {
                        list_splice_init (&inode_ctx->waiting_ops, waiting_ops);
                }
        }
        UNLOCK (&inode_ctx->lock);

        ret = 0;

out:
        return ret;
}


sp_inode_ctx_t *
sp_check_and_create_inode_ctx (xlator_t *this, inode_t *inode,
                               sp_expect_t expect)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &value);
                if (ret == 0) {
                        inode_ctx = (sp_inode_ctx_t *)(long)value;

                        if ((expect == SP_DONT_EXPECT) && (inode_ctx != NULL)) {
                                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                                  "stat-prefetch context is "
                                                  "present in inode "
                                                  "(ino:%"PRId64" gfid:%s) "
                                                  "when it is supposed to be "
                                                  "not present", inode->ino,
                                                  uuid_utoa (inode->gfid));
                        }
                } else {
                        if (expect == SP_EXPECT) {
                                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                                  "stat-prefetch context is "
                                                  "not present in inode "
                                                  "(ino:%"PRId64" gfid:%s)"
                                                  " when it is supposed to be "
                                                  "present", inode->ino,
                                                  uuid_utoa (inode->gfid));
                        }

                        inode_ctx = sp_inode_ctx_init ();
                        if (inode_ctx != NULL) {
                                ret = __inode_ctx_put (inode, this,
                                                       (long)inode_ctx);
                                if (ret == -1) {
                                        sp_inode_ctx_free (this, inode_ctx);
                                        inode_ctx = NULL;
                                }
                        }
                }
        }
        UNLOCK (&inode->lock);

out:
        return inode_ctx;
}


sp_cache_t *
sp_cache_ref (sp_cache_t *cache)
{
        if (cache == NULL) {
                goto out;
        }

        LOCK (&cache->lock);
        {
                cache->ref++;
        }
        UNLOCK (&cache->lock);

out:
        return cache;;
}


void
sp_cache_unref (sp_cache_t *cache)
{
        int refcount = 0;

        if (cache == NULL) {
                goto out;
        }

        LOCK (&cache->lock);
        {
                refcount = --cache->ref;
        }
        UNLOCK (&cache->lock);

        if (refcount == 0) {
                rbthash_table_destroy (cache->table);
                GF_FREE (cache);
        }

out:
        return;
}


int32_t
sp_process_inode_ctx (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      call_stub_t *stub, char *need_unwind, char *need_lookup,
                      char *can_wind, int32_t *error)
{
        int32_t         ret          = -1, op_errno = EINVAL;
        sp_local_t     *local        = NULL;
        sp_inode_ctx_t *inode_ctx    = NULL;
        uint64_t        value        = 0;

        if (need_unwind != NULL) {
                *need_unwind = 1;
        }

        GF_VALIDATE_OR_GOTO ("stat-prefetch", frame, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, need_unwind, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, need_lookup, out);
        GF_VALIDATE_OR_GOTO (frame->this->name, can_wind, out);

        inode_ctx_get (loc->inode, this, &value);

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        if (inode_ctx == NULL) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "stat-prefetch context not set in inode "
                                  "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                                  uuid_utoa (loc->inode->gfid));
                *can_wind = 1;
                *need_unwind = 0;
                op_errno = 0;
                ret = 0;
                goto out;
        }

        LOCK (&inode_ctx->lock);
        {
                if (!(inode_ctx->looked_up || inode_ctx->lookup_in_progress)) {
                        if (frame->local == NULL) {
                                local = GF_CALLOC (1, sizeof (*local),
                                                   gf_sp_mt_sp_local_t);
                                if (local == NULL) {
                                        op_errno = ENOMEM;
                                        goto unlock;
                                }

                                frame->local = local;

                                ret = loc_copy (&local->loc, loc);
                                if (ret == -1) {
                                        op_errno = errno;
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "loc_copy failed (%s)",
                                                strerror (op_errno));
                                        goto unlock;
                                }
                        }

                        *need_lookup = 1;
                        inode_ctx->lookup_in_progress = 1;
                }

                if (inode_ctx->looked_up) {
                        *can_wind = 1;
                } else {
                        list_add_tail (&stub->list, &inode_ctx->waiting_ops);
                        stub = NULL;
                }

                *need_unwind = 0;
                ret = 0;
        }
unlock:
        UNLOCK (&inode_ctx->lock);

out:
        if (stub != NULL) {
                call_stub_destroy (stub);
        }

        if (error != NULL) {
                *error = op_errno;
        }

        return ret;
}


inline uint32_t
sp_hashfn (void *data, int len)
{
        return gf_dm_hashfn ((const char *)data, len);
}


sp_cache_t *
sp_cache_init (xlator_t *this)
{
        sp_cache_t      *cache = NULL;
        sp_private_t    *priv  = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        GF_VALIDATE_OR_GOTO (this->name, priv->mem_pool, out);

        cache = GF_CALLOC (1, sizeof (*cache), gf_sp_mt_sp_cache_t);
        if (cache) {
                cache->table =
                        rbthash_table_init (GF_SP_CACHE_BUCKETS,
                                            sp_hashfn, __gf_free,
                                            0, priv->mem_pool);
                if (cache->table == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot init a new rbthash table to hold "
                                "cache");
                        GF_FREE (cache);
                        cache = NULL;
                        goto out;
                }

                LOCK_INIT (&cache->lock);
                cache->this = this;
        }

out:
        return cache;
}


void
sp_local_free (sp_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);
                GF_FREE (local);
        }
}


int32_t
sp_cache_remove_entry (sp_cache_t *cache, char *name, char remove_all)
{
        int32_t          ret   = -1;
        rbthash_table_t *table = NULL;
        xlator_t        *this  = NULL;
        sp_private_t    *priv  = NULL;
        void            *data  = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", cache, out);
        if ((name == NULL) && !remove_all) {
                gf_log ((cache->this ? cache->this->name : "stat-prefetch"),
                        GF_LOG_WARNING,
                        "request to remove a single entry from cache and is no "
                        "name passed to identify it");
                goto out;
        }

        this = cache->this;
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        priv = this->private;

        LOCK (&cache->lock);
        {
                if (remove_all) {
                        table = cache->table;
                        cache->table = rbthash_table_init (GF_SP_CACHE_BUCKETS,
                                                           sp_hashfn, __gf_free,
                                                           0, priv->mem_pool);
                        if (cache->table == NULL) {
                                cache->table = table;
                        } else {
                                rbthash_table_destroy (table);
                                ret = 0;
                                if (priv) {
                                        LOCK (&priv->lock);
                                        {
                                                priv->entries = 0;
                                        }
                                        UNLOCK (&priv->lock);
                                }
                        }
                } else {
                        data = rbthash_remove (cache->table, name,
                                               strlen (name));
                        GF_FREE (data);
                        ret = 0;
                        if (priv) {
                                LOCK (&priv->lock);
                                {
                                        priv->entries--;
                                }
                                UNLOCK (&priv->lock);
                        }
                }
        }
        UNLOCK (&cache->lock);

out:
        return ret;
}


int32_t
sp_cache_get_entry (sp_cache_t *cache, char *name, gf_dirent_t **entry)
{
        int32_t      ret = -1;
        gf_dirent_t *tmp = NULL, *new = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", cache, out);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", cache->this, out);
        GF_VALIDATE_OR_GOTO (cache->this->name, name, out);
        GF_VALIDATE_OR_GOTO (cache->this->name, entry, out);

        LOCK (&cache->lock);
        {
                tmp = rbthash_get (cache->table, name, strlen (name));
                if (tmp != NULL) {
                        new = gf_dirent_for_name (tmp->d_name);
                        if (new == NULL) {
                                gf_log (cache->this->name, GF_LOG_WARNING,
                                        "cannot create a new dentry to copy "
                                        "from cache");
                                goto unlock;
                        }

                        new->d_ino  = tmp->d_ino;
                        new->d_off  = tmp->d_off;
                        new->d_len  = tmp->d_len;
                        new->d_type = tmp->d_type;
                        new->d_stat = tmp->d_stat;

                        *entry = new;
                        ret = 0;
                }
        }
unlock:
        UNLOCK (&cache->lock);

out:
        return ret;
}


void
sp_cache_free (sp_cache_t *cache)
{
        sp_cache_remove_entry (cache, NULL, 1);
        sp_cache_unref (cache);
}


sp_cache_t *
__sp_get_cache_fd (xlator_t *this, fd_t *fd)
{
        int32_t      ret   = -1;
        sp_cache_t  *cache = NULL;
        uint64_t     value = 0;
        sp_fd_ctx_t *fd_ctx = NULL;

        ret = __fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                goto out;
        }

        fd_ctx = (void *)(long) value;

        cache = fd_ctx->cache;

out:
        return cache;
}


sp_cache_t *
sp_get_cache_fd (xlator_t *this, fd_t *fd)
{
        sp_cache_t  *cache     = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                cache = __sp_get_cache_fd (this, fd);
                if (cache != NULL) {
                        sp_cache_ref (cache);
                }
        }
        UNLOCK (&fd->lock);

out:
        return cache;
}


void
sp_fd_ctx_free (sp_fd_ctx_t *fd_ctx)
{
        if (fd_ctx == NULL) {
                goto out;
        }

        if (fd_ctx->parent_inode) {
                inode_unref (fd_ctx->parent_inode);
                fd_ctx->parent_inode = NULL;
        }

        if (fd_ctx->name) {
                GF_FREE (fd_ctx->name);
                fd_ctx->name = NULL;
        }

        if (fd_ctx->cache) {
                sp_cache_free (fd_ctx->cache);
        }

        GF_FREE (fd_ctx);
out:
        return;
}


inline sp_fd_ctx_t *
sp_fd_ctx_init (void)
{
        sp_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = GF_CALLOC (1, sizeof (*fd_ctx), gf_sp_mt_sp_fd_ctx_t);

        return fd_ctx;
}


sp_fd_ctx_t *
sp_fd_ctx_new (xlator_t *this, inode_t *parent, char *name, sp_cache_t *cache)
{
        sp_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = sp_fd_ctx_init ();
        if (fd_ctx == NULL) {
                goto out;
        }

        if (parent) {
                fd_ctx->parent_inode = inode_ref (parent);
        }

        if (name) {
                fd_ctx->name = gf_strdup (name);
                if (fd_ctx->name == NULL) {
                        sp_fd_ctx_free (fd_ctx);
                        fd_ctx = NULL;
                        goto out;
                }
        }

        fd_ctx->cache = cache;

out:
        return fd_ctx;
}


sp_cache_t *
sp_del_cache_fd (xlator_t *this, fd_t *fd)
{
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = -1;
        sp_fd_ctx_t *fd_ctx = NULL;

        if (fd == NULL) {
                goto out;
        }

        LOCK (&fd->lock);
        {
                ret = __fd_ctx_get (fd, this, &value);
                if (ret == 0) {
                        fd_ctx = (void *)(long) value;
                        cache = fd_ctx->cache;
                        fd_ctx->cache = NULL;
                }
        }
        UNLOCK (&fd->lock);

out:
        return cache;
}


sp_cache_t *
sp_get_cache_inode (xlator_t *this, inode_t *inode, int32_t pid)
{
        fd_t       *fd    = NULL;
        sp_cache_t *cache = NULL;

        if (inode == NULL) {
                goto out;
        }

        fd = fd_lookup (inode, pid);
        if (fd == NULL) {
                goto out;
        }

        cache = sp_get_cache_fd (this, fd);

        fd_unref (fd);
out:
        return cache;
}


fd_t *
_fd_ref (fd_t *fd);

void
sp_remove_caches_from_all_fds_opened (xlator_t *this, inode_t *inode,
                                      char *name)
{
        fd_t       *fd    = NULL;
        sp_cache_t *cache = NULL;
        struct fd_wrapper {
                fd_t             *fd;
                struct list_head  list;
        };

        struct fd_wrapper *wrapper    = NULL, *tmp = NULL;
        struct list_head   head       = {0, };
        char               remove_all = 0;

        wrapper = NULL;

        INIT_LIST_HEAD (&head);

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        remove_all = (name == NULL);

        LOCK (&inode->lock);
        {
                list_for_each_entry (fd, &inode->fd_list, inode_list) {
                        wrapper = GF_CALLOC (1, sizeof (*wrapper),
                                             gf_sp_mt_fd_wrapper_t);
                        if (wrapper == NULL) {
                                goto unlock;
                        }

                        INIT_LIST_HEAD (&wrapper->list);

                        wrapper->fd = _fd_ref (fd);
                        list_add_tail (&wrapper->list, &head);
                }
        }
unlock:
        UNLOCK (&inode->lock);

        list_for_each_entry_safe (wrapper, tmp, &head, list) {
                cache = sp_get_cache_fd (this, wrapper->fd);
                if (cache) {
                        sp_cache_remove_entry (cache, name, remove_all);
                        sp_cache_unref (cache);
                }

                list_del (&wrapper->list);
                fd_unref (wrapper->fd);
                GF_FREE (wrapper);
        }

out:
        return;
}


inline int32_t
__sp_put_cache (xlator_t *this, fd_t *fd, sp_cache_t *cache)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        int32_t      ret    = -1;
        uint64_t     value  = 0;

        ret = __fd_ctx_get (fd, this, &value);
        if (!ret) {
                fd_ctx = (void *)(long)value;
        } else {
                fd_ctx = sp_fd_ctx_init ();
                if (fd_ctx == NULL) {
                        ret = -1;
                        goto out;
                }

                ret = __fd_ctx_set (fd, this, (long)(void *)fd_ctx);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot set stat-prefetch context in fd (%p) "
                                "opened on inode (ino:%"PRId64" gfid:%s)",
                                fd, fd->inode->ino,
                                uuid_utoa (fd->inode->gfid));
                        sp_fd_ctx_free (fd_ctx);
                        goto out;
                }
        }

        if (fd_ctx->cache) {
                sp_cache_free (fd_ctx->cache);
        }

        fd_ctx->cache = cache;

out:
        return ret;
}


inline int32_t
sp_put_cache (xlator_t *this, fd_t *fd, sp_cache_t *cache)
{
        int32_t ret = -1;

        if (fd != NULL) {
                LOCK (&fd->lock);
                {
                        ret = __sp_put_cache (this, fd, cache);
                }
                UNLOCK (&fd->lock);
        }

        return ret;
}


int32_t
sp_cache_add_entries (sp_cache_t *cache, gf_dirent_t *entries)
{
        gf_dirent_t *entry           = NULL, *new = NULL;
        int32_t      ret             = -1;
        uint64_t     expected_offset = 0;
        xlator_t     *this           = NULL;
        sp_private_t *priv           = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", cache, out);

        this = cache->this;
        if (this && this->private) {
                priv = this->private;
        }

        LOCK (&cache->lock);
        {
                list_for_each_entry (entry, &entries->list, list) {
                        if (IA_ISDIR (entry->d_stat.ia_type)) {
                                continue;
                        }

                        if (uuid_is_null (entry->d_stat.ia_gfid))
                                continue;

                        new = gf_dirent_for_name (entry->d_name);
                        if (new == NULL) {
                                gf_log (cache->this->name, GF_LOG_WARNING,
                                        "cannot create a new dentry to store "
                                        "in cache");
                                goto unlock;
                        }

                        new->d_ino  = entry->d_ino;
                        new->d_off  = entry->d_off;
                        new->d_len  = entry->d_len;
                        new->d_type = entry->d_type;
                        new->d_stat = entry->d_stat;

                        ret = rbthash_insert (cache->table, new, new->d_name,
                                              strlen (new->d_name));
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_WARNING, "cannot "
                                        "insert dentry (name:%s) into cache",
                                        new->d_name);

                                GF_FREE (new);
                                continue;
                        }

                        expected_offset = new->d_off;
                        if (priv) {
                                LOCK (&priv->lock);
                                {
                                        priv->entries++;
                                }
                                UNLOCK (&priv->lock);
                        }
                }

                cache->expected_offset = expected_offset;

                ret = 0;
        }
unlock:
        UNLOCK (&cache->lock);

out:
        return ret;
}


int32_t
sp_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        struct list_head     waiting_ops = {0, };
        call_stub_t         *stub        = NULL, *tmp = NULL;
        sp_local_t          *local       = NULL;
        int                  need_unwind = 0;
        char                 looked_up   = 0, lookup_in_progress = 0;

        GF_ASSERT (frame);

        INIT_LIST_HEAD (&waiting_ops);

        local = frame->local;
        if (local == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING, "local is NULL, but it is "
                        "needed to find and resume operations waiting on "
                        "lookup");
                goto out;
        }

        if (this == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (frame->this ? frame->this->name : "stat-prefetch",
                        GF_LOG_WARNING, "xlator object (this) is NULL");
                goto out;
        }

        /* For '/' Entry is never cached, don't try to remove it */
        if ((op_ret == -1) && local->loc.parent) {
                sp_remove_caches_from_all_fds_opened (this, local->loc.parent,
                                                      (char *)local->loc.name);
        }

        if (local->is_lookup)
                need_unwind = 1;

        lookup_in_progress = 0;
        looked_up = 1;
        sp_update_inode_ctx (this, local->loc.inode, &op_ret, &op_errno,
                             &lookup_in_progress, &looked_up, buf,
                             &waiting_ops, &op_errno);

        list_for_each_entry_safe (stub, tmp, &waiting_ops, list) {
                list_del_init (&stub->list);
                call_resume (stub);
        }

out:
        if (need_unwind) {
                SP_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                                 dict, postparent);
        }

        return 0;
}


int32_t
sp_get_ancestors (char *path, char **parent, char **grand_parent)
{
        int32_t  ret = -1, i = 0;
        char    *cpy = NULL;

        if (!path || !parent || !grand_parent) {
                ret = 0;
                goto out;
        }

        for (i = 0; i < 2; i++) {
                if (!strcmp (path, "/")) {
                        break;
                }

                cpy = gf_strdup (path);
                if (cpy == NULL) {
                        ret = -errno;
                        goto out;
                }

                path = dirname (cpy);
                switch (i)
                {
                case 0:
                        *parent = gf_strdup (path);
			if (*parent == NULL)
				goto out;
                        break;
                case 1:
                        *grand_parent = gf_strdup (path);
			if (*grand_parent == NULL)
				goto out;
                        break;
                }
        }

        ret = 0;
out:
	if (cpy != NULL)
		GF_FREE(cpy);
        return ret;
}


int32_t
sp_cache_remove_parent_entry (call_frame_t *frame, xlator_t *this,
                              inode_table_t *itable, char *path)
{
        char       *parent    = NULL, *grand_parent = NULL, *cpy = NULL;
        inode_t    *inode_gp  = NULL;
        int32_t     ret       = -1;

        ret = sp_get_ancestors (path, &parent, &grand_parent);
        if (ret < 0) {
                goto out;
        }

        if (grand_parent && strcmp (grand_parent, "/")) {
                inode_gp = inode_from_path (itable, grand_parent);
                if (inode_gp) {
                        cpy = gf_strdup (parent);
                        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name,
                                                        cpy, out, ret,
                                                        -ENOMEM);
                        path = basename (cpy);
                        sp_remove_caches_from_all_fds_opened (this, inode_gp,
                                                              path);
                        GF_FREE (cpy);

                        inode_unref (inode_gp);
                }
        }

        ret = 0;
out:
        if (parent) {
                GF_FREE (parent);
        }

        if (grand_parent) {
                GF_FREE (grand_parent);
        }

        return ret;
}


void
sp_is_empty (dict_t *this, char *key, data_t *value, void *data)
{
        char *ptr = data;

        if (strcmp (key, "gfid-req") == 0)
                return;

        if (ptr && *ptr) {
                *ptr = 0;
        }
}


int32_t
sp_lookup_helper (call_frame_t *frame,xlator_t *this, loc_t *loc,
                  dict_t *xattr_req)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;
        call_stub_t    *stub      = NULL;
        char            can_wind  = 0;

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                op_errno = EINVAL;
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, inode_ctx, unwind, op_errno,
                                        EINVAL);

        stub = fop_lookup_stub (frame, sp_lookup_helper, loc, xattr_req);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, stub, unwind,
                                        op_errno, ENOMEM);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
                if (op_ret == 0) {
                        if (!inode_ctx->lookup_in_progress) {
                                inode_ctx->lookup_in_progress = 1;
                                can_wind = 1;
                        } else {
                                list_add_tail (&stub->list,
                                               &inode_ctx->waiting_ops);
                                stub = NULL;
                        }
                }
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                goto unwind;
        }

        if (can_wind) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc,
                            xattr_req);
        }

        if (stub != NULL) {
                call_stub_destroy (stub);
        }

        return 0;

unwind:
        SP_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        if (stub != NULL) {
                call_stub_destroy (stub);
        }

        return 0;
}


/*
 * TODO: implement sending lookups for every fop done on this path. As of now
 * lookup on the path is sent only for the first fop on this path.
 */
int32_t
sp_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        gf_dirent_t    *dirent          = NULL;
        char            entry_cached    = 0;
        uint64_t        value           = 0;
        char            xattr_req_empty = 1, can_wind = 0;
        sp_cache_t     *cache           = NULL;
        struct iatt     postparent      = {0, }, buf = {0, };
        int32_t         ret             = -1, op_ret = -1, op_errno = EINVAL;
        sp_inode_ctx_t *inode_ctx       = NULL, *parent_inode_ctx = NULL;
        sp_local_t     *local           = NULL;
        call_stub_t    *stub            = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc->inode, unwind);

        inode_ctx = sp_check_and_create_inode_ctx (this, loc->inode,
                                                   SP_DONT_CARE);
        if (inode_ctx == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot create stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s)(%s)", loc->inode->ino,
                        loc->inode->gfid, strerror (op_errno));
                goto unwind;
        }

        if ((loc->parent == NULL) || (loc->name == NULL)) {
                goto wind;
        }

        if (xattr_req != NULL) {
                dict_foreach (xattr_req, sp_is_empty, &xattr_req_empty);
        }

        if (!xattr_req_empty) {
                goto wind;
        }

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                ret = sp_cache_get_entry (cache, (char *)loc->name, &dirent);
                if (ret == 0) {
                        ret = inode_ctx_get (loc->parent, this, &value);
                        if ((ret == 0) && (value != 0)) {
                                parent_inode_ctx = (void *)(long)value;
                                postparent = parent_inode_ctx->stbuf;
                                buf = dirent->d_stat;
                                op_ret = 0;
                                op_errno = 0;
                                entry_cached = 1;
                        }

                        GF_FREE (dirent);
                }
        } else if (IA_ISDIR (loc->inode->ia_type)) {
                cache = sp_get_cache_inode (this, loc->inode, frame->root->pid);
                if (cache) {
                        ret = sp_cache_get_entry (cache, ".", &dirent);
                        if (ret == 0) {
                                ret = inode_ctx_get (loc->parent, this, &value);
                                if ((ret == 0) && (value != 0)) {
                                        parent_inode_ctx = (void *)(long)value;
                                        postparent = parent_inode_ctx->stbuf;
                                        buf = dirent->d_stat;
                                        op_ret = 0;
                                        op_errno = 0;
                                        entry_cached = 1;
                                }

                                GF_FREE (dirent);
                        }
                }
        }

wind:
        if (entry_cached) {
                if (cache) {
                        cache->hits++;
                        sp_cache_unref (cache);
                }
        } else {
                if (cache) {
                        cache->miss++;
                        sp_cache_unref (cache);
                }

                stub = fop_lookup_stub (frame, sp_lookup_helper, loc,
                                        xattr_req);
                GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, stub, unwind,
                                                op_errno, ENOMEM);

                local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
                GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind,
                                                op_errno, ENOMEM);

                frame->local = local;

                ret = loc_copy (&local->loc, loc);
                if (ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_WARNING,
                                "loc_copy failed (%s)", strerror (op_errno));
                        goto unwind;
                }

                local->is_lookup = 1;

                LOCK (&inode_ctx->lock);
                {
                        if (inode_ctx->lookup_in_progress) {
                                list_add_tail (&stub->list,
                                               &inode_ctx->waiting_ops);
                                stub = NULL;
                        } else {
                                can_wind = 1;
                                inode_ctx->lookup_in_progress = 1;
                        }
                }
                UNLOCK (&inode_ctx->lock);

                if (can_wind) {
                        STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->lookup, loc,
                                    xattr_req);
                }

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }

                return 0;
        }

unwind:
        SP_STACK_UNWIND (lookup, frame, op_ret, op_errno, (loc)?loc->inode:NULL,
                         &buf, NULL, &postparent);

        return 0;
}


int32_t
sp_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        sp_local_t      *local       = NULL;
        sp_cache_t      *cache       = NULL;
        fd_t            *fd          = NULL;
        int32_t          ret         = 0;
        char             was_present = 1;
        sp_private_t    *priv        = NULL;

        GF_ASSERT (frame);
        if (op_ret == -1) {
                goto out;
        }

        if ((this == NULL) || (this->private == NULL)) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        (this == NULL) ? "xlator object (this) is NULL"
                        : "stat-prefetch configuration (this->private) is "
                        "NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (frame->this->name, GF_LOG_WARNING, "local is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        fd = local->fd;

        priv = this->private;

        LOCK (&priv->lock);
        {
                if (!priv->mem_pool)
                        priv->mem_pool =  mem_pool_new (rbthash_entry_t,
                                                        GF_SP_CACHE_ENTRIES_EXPECTED);
        }
        UNLOCK (&priv->lock);

        if (!priv->mem_pool)
                goto out;

        LOCK (&fd->lock);
        {
                cache = __sp_get_cache_fd (this, fd);
                if (cache == NULL) {
                        was_present = 0;
                        cache = sp_cache_init (this);
                        if (cache == NULL) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "creation of stat-prefetch cache "
                                        "for fd (%p) opened on inode "
                                        "(ino:%"PRId64", gfid:%s) failed", fd,
                                        fd->inode->ino,
                                        uuid_utoa (fd->inode->gfid));
                                goto unlock;
                        }

                        ret = __sp_put_cache (this, fd, cache);
                        if (ret == -1) {
                                sp_cache_free (cache);
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot store cache in fd (%p) opened "
                                        "on inode (ino:%"PRId64", gfid:%s)", fd,
                                        fd->inode->ino,
                                        uuid_utoa (fd->inode->gfid));
                                goto unlock;
                        }
                }

                sp_cache_ref (cache);
        }
unlock:
        UNLOCK (&fd->lock);

        if (cache != NULL) {
                sp_cache_add_entries (cache, entries);
                if (was_present) {
                        sp_cache_unref (cache);
                }
        }

out:
        SP_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries);
        return 0;
}


int32_t
sp_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t off)
{
        sp_cache_t *cache    = NULL;
        sp_local_t *local    = NULL;
        char       *path     = NULL;
        int32_t     ret      = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        cache = sp_get_cache_fd (this, fd);
        if (cache) {
                if (off != cache->expected_offset) {
                        sp_cache_remove_entry (cache, NULL, 1);
                }

                sp_cache_unref (cache);
        }

        ret = inode_path (fd->inode, NULL, &path);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING, "cannot construct path on "
                        "which fd (%p) is opened (fd.inode.ino = %"PRId64", "
                        "fd.inode.gfid = %s) (%s)", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid), strerror (op_errno));
                goto unwind;
        }

        ret = sp_cache_remove_parent_entry (frame, this, fd->inode->table,
                                            path);

        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache"
                        " for path %s", path);
                goto unwind;
        }

        GF_FREE (path);

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        if (local) {
                local->fd = fd;
                frame->local = local;
        }

        STACK_WIND (frame, sp_readdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp, fd, size, off);

        return 0;

unwind:
        if (path != NULL) {
                GF_FREE (path);
        }

        SP_STACK_UNWIND (readdir, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
sp_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf)
{
        GF_ASSERT (frame);

        SP_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}



int32_t
sp_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf,
               struct iatt *preoldparent, struct iatt *postoldparent,
               struct iatt *prenewparent, struct iatt *postnewparent)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (rename, frame, op_ret, op_errno, buf, preoldparent,
                         postoldparent, prenewparent, postnewparent);
        return 0;
}


int32_t
sp_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
           int32_t op_errno, fd_t *fd)
{
        sp_local_t  *local = NULL;
        sp_fd_ctx_t *fd_ctx = NULL;

        GF_ASSERT (frame);

        if (op_ret == -1) {
                goto out;
        }

        if (this == NULL) {
                gf_log (frame->this ? frame->this->name : "stat-prefetch",
                        GF_LOG_WARNING, "xlator object (this) is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (fd == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "fd is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        fd_ctx = sp_fd_ctx_new (this, local->loc.parent,
                                (char *)local->loc.name, NULL);
        if (fd_ctx == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        op_ret = fd_ctx_set (fd, this, (long)(void *)fd_ctx);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set stat-prefetch context in fd (%p) opened on "
                        "inode (ino:%"PRId64", gfid:%s)", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                sp_fd_ctx_free (fd_ctx);
                op_errno = ENOMEM;
        }

out:
        SP_STACK_UNWIND (open, frame, op_ret, op_errno, fd);
        return 0;
}


int32_t
sp_open_helper (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                fd_t *fd, int32_t wbflags)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, unwind);
        GF_VALIDATE_OR_GOTO (frame->this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                op_errno = EINVAL;
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, inode_ctx, unwind, op_errno,
                                        EINVAL);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if ((op_ret == -1) && ((op_errno != ENOENT)
                               || !((op_errno == ENOENT)
                                    && (flags & O_CREAT)))) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding open call waiting on "
                        "it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_fd_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);

        return 0;

unwind:
        SP_STACK_UNWIND (open, frame, -1, op_errno, fd);
        return 0;
}


int32_t
sp_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int wbflags)
{
        call_stub_t    *stub         = NULL;
        sp_local_t     *local        = NULL;
        int32_t         op_errno     = EINVAL, ret = -1;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        stub = fop_open_stub (frame, sp_open_helper, loc, flags, fd, wbflags);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);
out:
        if (need_unwind) {
                SP_STACK_UNWIND (open, frame, -1, op_errno, fd);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_fd_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, loc, flags, fd,
                            wbflags);
        }

        return 0;

}

static int32_t
sp_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent)
{
        sp_local_t     *local              = NULL;
        sp_fd_ctx_t    *fd_ctx             = NULL;
        char            lookup_in_progress = 0, looked_up = 0;

        GF_ASSERT (frame);

        if (op_ret == -1) {
                goto out;
        }

        if (this == NULL) {
                gf_log (frame->this ? frame->this->name : "stat-prefetch",
                        GF_LOG_WARNING, "xlator object (this) is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        looked_up = 1;
        op_ret = sp_update_inode_ctx (this, local->loc.inode, &op_ret,
                                      &op_errno, &lookup_in_progress,
                                      &looked_up, buf, NULL, &op_errno);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "updating stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s) (path: %s) failed (%s)",
                        local->loc.inode->ino,
                        uuid_utoa (local->loc.inode->gfid), local->loc.path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = sp_update_inode_ctx (this, local->loc.parent, NULL, NULL, NULL,
                                      NULL, postparent, NULL, &op_errno);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "updating stat-prefetch context in parent inode failed "
                        "for path (%s)(%s)", local->loc.path,
                        strerror (op_errno));
                goto out;
        }

        fd_ctx = sp_fd_ctx_new (this, local->loc.parent,
                                (char *)local->loc.name, NULL);
        if (fd_ctx == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        op_ret = fd_ctx_set (fd, this, (long)(void *)fd_ctx);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set stat-prefetch context in fd (%p) opened on "
                        "inode (ino:%"PRId64", gfid:%s)", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                sp_fd_ctx_free (fd_ctx);
                op_errno = ENOMEM;
        }

out:
        SP_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, buf,
                         preparent, postparent);
        return 0;
}


int32_t
sp_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd, dict_t *params)
{
        sp_local_t        *local        = NULL;
        int32_t            op_errno     = -1, ret = -1;
        char               need_unwind  = 1;
        sp_inode_ctx_t    *inode_ctx    = NULL;

        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, loc, out, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, loc->path, out, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, loc->name, out, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, loc->inode, out,
                                        op_errno, EINVAL);

        ret = sp_cache_remove_parent_entry (frame, this, loc->inode->table,
                                            (char *)loc->path);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "loc_copy failed (%s)", strerror (op_errno));
                goto out;
        }

        inode_ctx = sp_check_and_create_inode_ctx (this, loc->inode,
                                                   SP_DONT_EXPECT);
        if (inode_ctx == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot create stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s)(%s)", loc->inode->ino,
                        loc->inode->gfid, strerror (op_errno));
                goto out;
        }

        need_unwind = 0;
out:
        if (need_unwind) {
                SP_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                                 NULL, NULL);
        } else {
                STACK_WIND (frame, sp_create_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->create, loc, flags,
                            mode, fd, params);
        }
        return 0;
}


int32_t
sp_opendir_helper (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding opendir call waiting "
                        "on it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_fd_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd);

        return 0;

unwind:
        SP_STACK_UNWIND (opendir, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
sp_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        sp_local_t     *local        = NULL;
        call_stub_t    *stub         = NULL;
        int32_t         op_errno     = EINVAL, ret = -1;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        stub = fop_opendir_stub (frame, sp_opendir_helper, loc, fd);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (opendir, frame, -1, op_errno, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_fd_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir, loc, fd);
        }

        return 0;
}


int32_t
sp_new_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        sp_local_t *local              = NULL;
        char        lookup_in_progress = 0, looked_up = 0;

        GF_ASSERT (frame);

        if (op_ret == -1) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (frame->this->name, GF_LOG_WARNING, "local is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (this == NULL) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "xlator object (this) is NULL");
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        looked_up = 1;
        op_ret = sp_update_inode_ctx (this, local->loc.inode, &op_ret,
                                      &op_errno, &lookup_in_progress,
                                      &looked_up, buf, NULL, &op_errno);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "updating stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s) (path: %s) failed (%s)",
                        local->loc.inode->ino,
                        uuid_utoa (local->loc.inode->gfid), local->loc.path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = sp_update_inode_ctx (this, local->loc.parent, NULL, NULL, NULL,
                                      NULL, postparent, NULL, &op_errno);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "updating stat-prefetch context in parent inode failed "
                        "for path (%s)(%s)", local->loc.path,
                        strerror (op_errno));
        }

out:
        SP_STACK_UNWIND (mkdir, frame, op_ret, op_errno, inode, buf, preparent,
                         postparent);
        return 0;
}


int
sp_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dict_t *params)
{
        int32_t         ret          = -1, op_errno = EINVAL;
        char            need_unwind  = 1;
        sp_inode_ctx_t *inode_ctx    = NULL;
        sp_local_t     *local        = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = sp_cache_remove_parent_entry (frame, this, loc->inode->table,
                                            (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", loc->path);
                op_errno = ENOMEM;
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        inode_ctx = sp_check_and_create_inode_ctx (this, loc->inode,
                                                   SP_DONT_EXPECT);
        if (inode_ctx == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot create stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s)(%s)", loc->inode->ino,
                        loc->inode->gfid, strerror (op_errno));
                goto out;
        }

        need_unwind = 0;
out:
        if (need_unwind) {
                SP_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                                 NULL);
        } else {
                STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir, loc, mode, params);
        }

        return 0;
}


int
sp_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, dict_t *params)
{
        int32_t         op_errno     = EINVAL, ret = -1;
        char            need_unwind  = 1;
        sp_inode_ctx_t *inode_ctx    = NULL;
        sp_local_t     *local        = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = sp_cache_remove_parent_entry (frame, this, loc->inode->table,
                                            (char *)loc->path);
        if (ret == -1) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", loc->path);
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        inode_ctx = sp_check_and_create_inode_ctx (this, loc->inode,
                                                   SP_DONT_EXPECT);
        if (inode_ctx == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot create stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s)(%s)", loc->inode->ino,
                        loc->inode->gfid, strerror (op_errno));
                goto out;
        }

        need_unwind = 0;
out:
        if (need_unwind) {
                SP_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL,
                                 NULL);
        } else {
                STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mknod, loc, mode,
                            rdev, params);
        }

        return 0;
}


int
sp_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, dict_t *params)
{
        int32_t         ret          = -1, op_errno = EINVAL;
        char            need_unwind  = 1;
        sp_inode_ctx_t *inode_ctx    = NULL;
        sp_local_t     *local        = NULL;

        GF_ASSERT (frame);

        GF_VALIDATE_OR_GOTO ((frame->this ? frame->this->name
                              : "stat-prefetch"),
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = sp_cache_remove_parent_entry (frame, this, loc->inode->table,
                                            (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", loc->path);
                op_errno = ENOMEM;
                goto out;
        }

        local = GF_CALLOC (1, sizeof (*local), gf_sp_mt_sp_local_t);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, op_errno,
                                        ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        inode_ctx = sp_check_and_create_inode_ctx (this, loc->inode,
                                                   SP_DONT_EXPECT);
        if (inode_ctx == NULL) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot create stat-prefetch context in inode (ino:%"
                        PRId64", gfid:%s)(%s)", loc->inode->ino,
                        loc->inode->gfid, strerror (op_errno));
                goto out;
        }

        need_unwind = 0;
out:
        if (need_unwind) {
                SP_STACK_UNWIND (symlink, frame, -1, op_errno, NULL, NULL, NULL,
                                 NULL);
        } else {
                STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->symlink, linkpath, loc,
                            params);
        }

        return 0;
}


int32_t
sp_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, inode_t *inode,
             struct iatt *buf, struct iatt *preparent,
             struct iatt *postparent)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (link, frame, op_ret, op_errno, inode, buf, preparent,
                         postparent);
        return 0;
}


int32_t
sp_link_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, oldloc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc, unwind);

        ret = inode_ctx_get (oldloc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", oldloc->inode->ino,
                        uuid_utoa (oldloc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, inode_ctx, unwind, op_errno,
                                        EINVAL);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding link call waiting on "
                        "it", oldloc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc);

        return 0;

unwind:
        SP_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
sp_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t *stub         = NULL;
        int32_t      ret          = 0, op_errno = EINVAL;
        char         can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc->path, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc->name, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->name, out);

        ret = sp_cache_remove_parent_entry (frame, this, newloc->parent->table,
                                            (char *)newloc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", newloc->path);
                op_errno = ENOMEM;
                goto out;
        }

        sp_remove_caches_from_all_fds_opened (this, oldloc->parent,
                                              (char *)oldloc->name);

        stub = fop_link_stub (frame, sp_link_helper, oldloc, newloc);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, oldloc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL,
                                 NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, oldloc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_link_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->link, oldloc, newloc);
        }

        return 0;
}


int32_t
sp_truncate_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, inode_ctx, unwind, op_errno,
                                        EINVAL);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding truncate call "
                        "waiting on it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset);

        return 0;

unwind:
        SP_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
sp_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        stub = fop_truncate_stub (frame, sp_truncate_helper, loc, offset);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_truncate_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc, offset);
        }

        return 0;
}


int32_t
sp_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0, op_errno = EINVAL;
        inode_t     *parent = NULL;
        char        *name   = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "stat-prefetch context not "
                        "set in fd (%p) opened on inode (ino:%"PRId64", "
                        "gfid:%s", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        sp_remove_caches_from_all_fds_opened (this, parent, (char *)name);

        STACK_WIND (frame, sp_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        return 0;

unwind:
        SP_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
sp_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iatt *prestat, struct iatt *poststat)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (setattr, frame, op_ret, op_errno, prestat, poststat);
        return 0;
}


int
sp_setattr_helper (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, struct iatt *buf, int32_t valid)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                op_errno = EINVAL;
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding setattr call "
                        "waiting on it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, buf, valid);

        return 0;

unwind:
        SP_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
sp_setattr (call_frame_t *frame, xlator_t *this,
            loc_t *loc, struct iatt *buf, int32_t valid)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        stub = fop_setattr_stub (frame, sp_setattr_helper, loc, buf, valid);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_setattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setattr, loc, buf, valid);
        }

        return 0;
}


int32_t
sp_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *path,
                 struct iatt *buf)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (readlink, frame, op_ret, op_errno, path, buf);
        return 0;
}


int32_t
sp_readlink_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    size_t size)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding readlink call "
                        "waiting on it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_readlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink, loc, size);

        return 0;

unwind:
        SP_STACK_UNWIND (readlink, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
sp_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        stub = fop_readlink_stub (frame, sp_readlink_helper, loc, size);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (readlink, frame, -1, op_errno, NULL, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_readlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readlink, loc, size);
        }

        return 0;
}


int32_t
sp_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent,
                         postparent);
        return 0;
}



int32_t
sp_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (setxattr, frame, op_ret, op_errno);
        return 0;
}


int32_t
sp_unlink_helper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding unlink call "
                        "waiting on it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc);

        return 0;

unwind:
        SP_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
sp_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int32_t         ret          = -1, op_errno = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        ret = sp_cache_remove_parent_entry (frame, this, loc->parent->table,
                                            (char *)loc->path);
        if (ret == -1) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", loc->path);
                goto out;
        }

        stub = fop_unlink_stub (frame, sp_unlink_helper, loc);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc);
        }

        return 0;
}


int
sp_rmdir_helper (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "lookup-behind has failed "
                        "for path (%s)(%s), unwinding rmdir call "
                        "waiting on it", loc->path, strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc, flags);

        return 0;

unwind:
        SP_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int
sp_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        int32_t         ret          = -1, op_errno = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        sp_remove_caches_from_all_fds_opened (this, loc->inode, NULL);

        ret = sp_cache_remove_parent_entry (frame, this, loc->inode->table,
                                            (char *)loc->path);
        if (ret == -1) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", loc->path);
                goto out;
        }

        stub = fop_rmdir_stub (frame, sp_rmdir_helper, loc, flags);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rmdir, loc, flags);
        }

        return 0;
}


int32_t
sp_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct iatt *stbuf, struct iobref *iobref)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count, stbuf,
                         iobref);
        return 0;
}


int32_t
sp_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0, op_errno = EINVAL;
        inode_t     *parent = NULL;
        char        *name   = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "stat-prefetch context not "
                        "set in fd (%p) opened on inode (ino:%"PRId64", "
                        "gfid:%s", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        sp_remove_caches_from_all_fds_opened (this, parent, (char *)name);

        STACK_WIND (frame, sp_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset);
        return 0;

unwind:
        SP_STACK_UNWIND (readv, frame, -1, op_errno, NULL, -1, NULL, NULL);
        return 0;
}


int32_t
sp_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, struct iobref *iobref)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0, op_errno = EINVAL;
        inode_t     *parent = NULL;
        char        *name   = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "stat-prefetch context not "
                        "set in fd (%p) opened on inode (ino:%"PRId64", "
                        "gfid:%s", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        sp_remove_caches_from_all_fds_opened (this, parent, (char *)name);

        STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, off,
                    iobref);
        return 0;

unwind:
        SP_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
sp_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0, op_errno = EINVAL;
        inode_t     *parent = NULL;
        char        *name   = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "stat-prefetch context not "
                        "set in fd (%p) opened on inode (ino:%"PRId64", "
                        "gfid:%s", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        sp_remove_caches_from_all_fds_opened (this, parent, (char *)name);

        STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, flags);
        return 0;

unwind:
        SP_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
sp_rename_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                  loc_t *newloc)
{
        uint64_t        value               = 0;
        char            need_unwind         = 0;
        char            can_wind            = 0;
        int32_t         ret                 = 0, op_errno = EINVAL;
        int32_t         old_op_ret          = -1, old_op_errno = -1;
        int32_t         new_op_ret          = -1, new_op_errno = -1;
        char            old_inode_looked_up = 0, new_inode_looked_up = 0;
        sp_inode_ctx_t *old_inode_ctx       = NULL, *new_inode_ctx = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, oldloc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc, unwind);

        ret = inode_ctx_get (oldloc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", oldloc->inode->ino,
                        uuid_utoa (oldloc->inode->gfid));
                goto unwind;
        }

        old_inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, old_inode_ctx, unwind);

        LOCK (&old_inode_ctx->lock);
        {
                old_inode_looked_up = old_inode_ctx->looked_up;
                old_op_ret = old_inode_ctx->op_ret;
                old_op_errno = old_inode_ctx->op_errno;
                need_unwind = old_inode_ctx->need_unwind;
        }
        UNLOCK (&old_inode_ctx->lock);

        if (need_unwind) {
                /* there was an error while queuing up lookup stub for newloc */
                gf_log (this->name, GF_LOG_WARNING,
                        "could not queue lookup stub for path (%s)",
                        newloc->path);
                goto unwind;
        }

        if (newloc->inode != NULL) {
                ret = inode_ctx_get (newloc->inode, this, &value);
                if (ret == 0) {
                        new_inode_ctx = (sp_inode_ctx_t *)(long)value;
                        if (new_inode_ctx != NULL) {
                                LOCK (&new_inode_ctx->lock);
                                {
                                        new_inode_looked_up
                                                = new_inode_ctx->looked_up;
                                        new_op_ret = new_inode_ctx->op_ret;
                                        new_op_errno = new_inode_ctx->op_errno;
                                }
                                UNLOCK (&new_inode_ctx->lock);
                        }
                }
        }

        if (new_inode_ctx == NULL) {
                if (old_op_ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "lookup-behind has failed "
                                "for path (%s)(%s), unwinding rename call "
                                "waiting on it", oldloc->path,
                                strerror (old_op_errno));

                        op_errno = old_op_errno;
                        goto unwind;
                } else {
                        can_wind = 1;
                }
        } else {
                if (new_inode_looked_up && old_inode_looked_up) {
                        if ((old_op_ret == -1)
                            || ((new_op_ret == -1)
                                && (new_op_errno != ENOENT))) {
                                if (old_op_ret == -1) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "lookup-behind has failed "
                                                "for path (%s)(%s), unwinding "
                                                "rename call waiting on it",
                                                oldloc->path,
                                                strerror (old_op_errno));
                                        op_errno = old_op_errno;
                                } else {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "lookup-behind has failed "
                                                "for path (%s)(%s), unwinding "
                                                "rename call waiting on it",
                                                newloc->path,
                                                strerror (new_op_errno));
                                        op_errno = new_op_errno;
                                }

                                goto unwind;
                        } else {
                                can_wind = 1;
                        }
                }
        }

        if (can_wind) {
                STACK_WIND (frame, sp_rename_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename, oldloc, newloc);
        }

        return 0;

unwind:
        SP_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                         NULL);
        return 0;
}


int32_t
sp_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,loc_t *newloc)
{
        char            need_unwind           = 1;
        uint64_t        value                 = 0;
        call_stub_t    *stub                  = NULL;
        sp_inode_ctx_t *inode_ctx             = NULL;
        int32_t         ret                   = -1, op_errno = EINVAL;
        char            old_inode_can_wind    = 0, new_inode_can_wind = 0;
        char            old_inode_need_lookup = 0, new_inode_need_lookup = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (frame->this ? frame->this->name : "stat-prefetch",
                             this, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->path, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->name, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->inode, out);

        GF_VALIDATE_OR_GOTO (this->name, newloc, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc->path, out);

        sp_remove_caches_from_all_fds_opened (this, oldloc->parent,
                                              (char *)oldloc->name);

        sp_remove_caches_from_all_fds_opened (this, newloc->parent,
                                              (char *)newloc->name);

        ret = sp_cache_remove_parent_entry (frame, this, oldloc->parent->table,
                                            (char *)oldloc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", oldloc->path);
                goto out;
        }

        ret = sp_cache_remove_parent_entry (frame, this, newloc->parent->table,
                                            (char *)newloc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot remove parent entry from grand-parent's cache "
                        "for path (%s)", newloc->path);
                goto out;
        }

        if (IA_ISDIR (oldloc->inode->ia_type)) {
                sp_remove_caches_from_all_fds_opened (this, oldloc->inode,
                                                      NULL);
        }

        stub = fop_rename_stub (frame, sp_rename_helper, oldloc, newloc);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        ret = sp_process_inode_ctx (frame, this, oldloc, stub, &need_unwind,
                                    &old_inode_need_lookup, &old_inode_can_wind,
                                    &op_errno);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "processing stat-prefetch "
                        "context in inode (ino:%"PRId64", gfid:%s) (path:%s) "
                        "failed (%s)", oldloc->inode->ino,
                        uuid_utoa (oldloc->inode->gfid), oldloc->path,
                        strerror (op_errno));
                goto out;
        }

        if (newloc->inode != NULL) {
                stub = fop_rename_stub (frame, sp_rename_helper, oldloc,
                                        newloc);
                if (stub == NULL) {
                        op_errno = ENOMEM;
                        goto out;
                }

                ret = sp_process_inode_ctx (frame, this, newloc, stub,
                                            &need_unwind,
                                            &new_inode_need_lookup,
                                            &new_inode_can_wind, &op_errno);
                if (ret == -1) {
                        ret = inode_ctx_get (oldloc->inode, this, &value);

                        inode_ctx = (sp_inode_ctx_t *)(long)value;
                        if (inode_ctx == NULL) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "stat-prefetch context not set in inode"
                                        " (ino:%"PRId64", gfid:%s) (path:%s)",
                                        oldloc->inode->ino,
                                        uuid_utoa (oldloc->inode->gfid),
                                        oldloc->path);
                                goto out;
                        }

                        LOCK (&inode_ctx->lock);
                        {
                                if (!inode_ctx->looked_up) {
                                        /* unwind in sp_rename_helper */
                                        need_unwind = 0;
                                        inode_ctx->need_unwind = 1;
                                }
                        }
                        UNLOCK (&inode_ctx->lock);
                }

        } else {
                new_inode_can_wind = 1;
        }

out:
        if (need_unwind) {
                SP_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL,
                                 NULL, NULL);
        } else if (old_inode_need_lookup || new_inode_need_lookup) {
                if (old_inode_need_lookup) {
                        STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->lookup, oldloc,
                                    NULL);
                }

                if (new_inode_need_lookup) {
                        STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->lookup, newloc,
                                    NULL);
                }
        } else if (old_inode_can_wind && new_inode_can_wind) {
                STACK_WIND (frame, sp_rename_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename, oldloc, newloc);
        }

        return 0;
}


int32_t
sp_setxattr_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *dict, int32_t flags)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding setxattr call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict,
                    flags);

        return 0;

unwind:
        SP_STACK_UNWIND (setxattr, frame, -1, op_errno);
        return 0;
}


int32_t
sp_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        stub = fop_setxattr_stub (frame, sp_setxattr_helper, loc, dict, flags);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (setxattr, frame, -1, op_errno);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr, loc, dict,
                            flags);
        }

        return 0;
}


int32_t
sp_removexattr_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                       const char *name)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding setxattr call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name);

        return 0;

unwind:
        SP_STACK_UNWIND (removexattr, frame, -1, op_errno);
        return 0;
}


int32_t
sp_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        stub = fop_removexattr_stub (frame, sp_removexattr_helper, loc, name);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (removexattr, frame, -1, op_errno);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr, loc, name);
        }

        return 0;
}


int32_t
sp_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int32_t
sp_getxattr_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding getxattr call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name);

        return 0;

unwind:
        SP_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
sp_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        stub = fop_getxattr_stub (frame, sp_getxattr_helper, loc, name);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_getxattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->getxattr, loc, name);
        }

        return 0;
}


int32_t
sp_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (xattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int32_t
sp_xattrop_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   gf_xattrop_flags_t flags, dict_t *dict)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding xattrop call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_xattrop_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, loc, flags, dict);

        return 0;

unwind:
        SP_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
sp_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        sp_remove_caches_from_all_fds_opened (this, loc->parent,
                                              (char *)loc->name);

        stub = fop_xattrop_stub (frame, sp_xattrop_helper, loc, flags, dict);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_xattrop_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->xattrop, loc, flags, dict);
        }

        return 0;
}


int32_t
sp_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
             gf_xattrop_flags_t flags, dict_t *dict)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0, op_errno = EINVAL;
        inode_t     *parent = NULL;
        char        *name   = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "stat-prefetch context not "
                        "set in fd (%p) opened on inode (ino:%"PRId64", "
                        "gfid:%s", fd, fd->inode->ino,
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        sp_remove_caches_from_all_fds_opened (this, parent, name);

        STACK_WIND (frame, sp_xattrop_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop, fd, flags, dict);
        return 0;

unwind:
        SP_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL);
        return 0;
}

int32_t
sp_stbuf_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf)
{
        GF_ASSERT (frame);
        SP_STACK_UNWIND (stat, frame, op_ret, op_errno, buf);
        return 0;
}


int32_t
sp_stat_helper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding stat call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_stbuf_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc);

        return 0;

unwind:
        SP_STACK_UNWIND (stat, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
sp_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int32_t         op_errno     = -1;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        stub = fop_stat_stub (frame, sp_stat_helper, loc);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (stat, frame, -1, op_errno, NULL);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_stbuf_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->stat, loc);
        }

        return 0;
}


int32_t
sp_access_helper (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding access call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access, loc, mask);

        return 0;

unwind:
        SP_STACK_UNWIND (access, frame, -1, op_errno);
        return 0;
}


int32_t
sp_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        int32_t         op_errno     = -1;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        stub = fop_access_stub (frame, sp_access_helper, loc, mask);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (access, frame, -1, op_errno);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->access, loc, mask);
        }

        return 0;
}


int32_t
sp_inodelk_helper (call_frame_t *frame, xlator_t *this, const char *volume,
                   loc_t *loc, int32_t cmd, struct gf_flock *lock)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding inodelk call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk, volume, loc, cmd, lock);

        return 0;

unwind:
        SP_STACK_UNWIND (inodelk, frame, -1, op_errno);
        return 0;
}


int32_t
sp_inodelk (call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            int32_t cmd, struct gf_flock *lock)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        stub = fop_inodelk_stub (frame, sp_inodelk_helper, volume, loc, cmd,
                                 lock);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (inodelk, frame, -1, op_errno);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->inodelk, volume, loc, cmd,
                            lock);
        }

        return 0;
}


int32_t
sp_entrylk_helper (call_frame_t *frame, xlator_t *this, const char *volume,
                   loc_t *loc, const char *basename, entrylk_cmd cmd,
                   entrylk_type type)
{
        uint64_t        value     = 0;
        sp_inode_ctx_t *inode_ctx = NULL;
        int32_t         ret       = 0, op_ret = -1, op_errno = EINVAL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);

        ret = inode_ctx_get (loc->inode, this, &value);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "stat-prefetch context not set in inode "
                        "(ino:%"PRId64" gfid:%s)", loc->inode->ino,
                        uuid_utoa (loc->inode->gfid));
                goto unwind;
        }

        inode_ctx = (sp_inode_ctx_t *)(long) value;
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, unwind);

        LOCK (&inode_ctx->lock);
        {
                op_ret = inode_ctx->op_ret;
                op_errno = inode_ctx->op_errno;
        }
        UNLOCK (&inode_ctx->lock);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "lookup-behind has failed for path (%s)(%s), "
                        "unwinding entrylk call waiting on it", loc->path,
                        strerror (op_errno));
                goto unwind;
        }

        STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->entrylk, volume, loc, basename,
                    cmd, type);

        return 0;

unwind:
        SP_STACK_UNWIND (entrylk, frame, -1, op_errno);
        return 0;
}


int32_t
sp_entrylk (call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            const char *basename, entrylk_cmd cmd, entrylk_type type)
{
        int32_t         op_errno     = EINVAL;
        call_stub_t    *stub         = NULL;
        char            can_wind     = 0, need_lookup = 0, need_unwind = 1;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        stub = fop_entrylk_stub (frame, sp_entrylk_helper, volume, loc,
                                 basename, cmd, type);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto out;
        }

        sp_process_inode_ctx (frame, this, loc, stub, &need_unwind,
                              &need_lookup, &can_wind, &op_errno);

out:
        if (need_unwind) {
                SP_STACK_UNWIND (entrylk, frame, -1, op_errno);
        } else if (need_lookup) {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, NULL);
        } else if (can_wind) {
                STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->entrylk, volume, loc,
                            basename, cmd, type);
        }

        return 0;
}


int32_t
sp_forget (xlator_t *this, inode_t *inode)
{
        struct iatt *buf   = NULL;
        uint64_t     value = 0;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        inode_ctx_del (inode, this, &value);

        if (value) {
                buf = (void *)(long)value;
                GF_FREE (buf);
        }

out:
        return 0;
}


int32_t
sp_release (xlator_t *this, fd_t *fd)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0;
        sp_cache_t  *cache  = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        ret = fd_ctx_del (fd, this, &value);
        if (!ret) {
                fd_ctx = (void *)(long) value;
                cache = fd_ctx->cache;
                if (cache) {
                        gf_log (this->name, GF_LOG_TRACE, "cache hits: %lu, "
                                "cache miss: %lu", cache->hits, cache->miss);
                }

                sp_fd_ctx_free (fd_ctx);
        }

out:
        return 0;
}


int
sp_priv_dump (xlator_t *this)
{
        sp_private_t            *priv         = NULL;
        uint32_t                total_entries = 0;
        uint32_t                ret           = -1;
        char                    key[GF_DUMP_MAX_BUF_LEN];
        char                    key_prefix[GF_DUMP_MAX_BUF_LEN];

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        priv = this->private;

        total_entries = priv->entries;

        gf_proc_dump_build_key (key_prefix, "xlator.performance.stat-prefetch",
                                "priv");
        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_build_key (key, key_prefix, "max_allowed_entries");
        gf_proc_dump_write (key, "%lu", GF_SP_CACHE_ENTRIES_EXPECTED);
        gf_proc_dump_build_key (key, key_prefix, "num_entries_cached");
        gf_proc_dump_write (key, "%lu",(unsigned long)total_entries);
        ret = 0;

out:
        return ret;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);

        ret = xlator_mem_acct_init (this, gf_sp_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                goto out;
        }

out:
        return ret;
}


int32_t
init (xlator_t *this)
{
        int32_t         ret  = -1;
        sp_private_t   *priv = NULL;

        GF_VALIDATE_OR_GOTO ("stat-prefetch", this, out);

        if (!this->children || this->children->next) {
                gf_log ("stat-prefetch",
                        GF_LOG_ERROR,
                        "FATAL: translator %s does not have exactly one child "
                        "node", this->name);
                goto out;
        }

        priv = GF_CALLOC (1, sizeof(sp_private_t), gf_sp_mt_sp_private_t);
        LOCK_INIT (&priv->lock);

        this->private = priv;

        ret = 0;
out:
        return ret;
}

void
fini (xlator_t *this)
{
        sp_private_t  *priv = NULL;

        if (!this)
                goto out;
        else {
                priv = this->private;
                if (priv) {
                        if (priv->mem_pool)
                                mem_pool_destroy (priv->mem_pool);
                        LOCK_DESTROY (&priv->lock);
                        GF_FREE (priv);
                        this->private = NULL;
                }
        }
out:
        return;
}


struct xlator_fops fops = {
        .lookup      = sp_lookup,
        .readdir     = sp_readdir,
        .readdirp    = sp_readdir,
        .open        = sp_open,
        .create      = sp_create,
        .opendir     = sp_opendir,
        .mkdir       = sp_mkdir,
        .mknod       = sp_mknod,
        .symlink     = sp_symlink,
        .link        = sp_link,
        .truncate    = sp_truncate,
        .ftruncate   = sp_ftruncate,
        .readlink    = sp_readlink,
        .unlink      = sp_unlink,
        .rmdir       = sp_rmdir,
        .readv       = sp_readv,
        .writev      = sp_writev,
        .fsync       = sp_fsync,
        .rename      = sp_rename,
        .setxattr    = sp_setxattr,
        .removexattr = sp_removexattr,
        .xattrop     = sp_xattrop,
        .fxattrop    = sp_fxattrop,
        .setattr     = sp_setattr,
        .stat        = sp_stat,
        .access      = sp_access,
        .getxattr    = sp_getxattr,
        .inodelk     = sp_inodelk,
        .entrylk     = sp_entrylk,
};

struct xlator_cbks cbks = {
        .forget     = sp_forget,
        .release    = sp_release,
        .releasedir = sp_release
};

struct xlator_dumpops dumpops = {
        .priv = sp_priv_dump,
};
