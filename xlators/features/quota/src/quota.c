/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <fnmatch.h>

#include "quota.h"
#include "common-utils.h"
#include "defaults.h"
#include "statedump.h"

int32_t
quota_check_limit (call_frame_t *frame, inode_t *inode, xlator_t *this,
                   char *name, uuid_t par);

int
quota_fill_inodectx (xlator_t *this, inode_t *inode, dict_t *dict,
                     loc_t *loc, struct iatt *buf, int32_t *op_errno);

struct volume_options options[];

static int32_t
__quota_init_inode_ctx (inode_t *inode, xlator_t *this,
                        quota_inode_ctx_t **context)
{
        int32_t            ret  = -1;
        quota_inode_ctx_t *ctx  = NULL;

        if (inode == NULL) {
                goto out;
        }

        QUOTA_ALLOC_OR_GOTO (ctx, quota_inode_ctx_t, out);

        LOCK_INIT(&ctx->lock);

        if (context != NULL) {
                *context = ctx;
        }

        INIT_LIST_HEAD (&ctx->parents);

        ret = __inode_ctx_put (inode, this, (uint64_t )(long)ctx);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set quota context in inode (gfid:%s)",
                        uuid_utoa (inode->gfid));
        }
out:
        return ret;
}


static int32_t
quota_inode_ctx_get (inode_t *inode, xlator_t *this,
                     quota_inode_ctx_t **ctx, char create_if_absent)
{
        int32_t  ret = 0;
        uint64_t ctx_int;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx_int);

                if ((ret == 0) && (ctx != NULL)) {
                        *ctx = (quota_inode_ctx_t *) (unsigned long)ctx_int;
                } else if (create_if_absent) {
                        ret = __quota_init_inode_ctx (inode, this, ctx);
                }
        }
        UNLOCK (&inode->lock);

        return ret;
}

int
quota_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int ret = -1;

        if (!loc || (inode == NULL))
                return ret;

        if (inode) {
                loc->inode = inode_ref (inode);
                uuid_copy (loc->gfid, inode->gfid);
        }

        if (parent) {
                loc->parent = inode_ref (parent);
        }

        if (path != NULL) {
                loc->path = gf_strdup (path);

                loc->name = strrchr (loc->path, '/');
                if (loc->name) {
                        loc->name++;
                }
        }

        ret = 0;

        if (ret < 0) {
                loc_wipe (loc);
        }

        return ret;
}


int
quota_inode_loc_fill (inode_t *inode, loc_t *loc)
{
        char            *resolvedpath = NULL;
        inode_t         *parent       = NULL;
        int              ret          = -1;
        xlator_t        *this         = NULL;

        if ((!inode) || (!loc)) {
                return ret;
        }

        this = THIS;

        if ((inode) && __is_root_gfid (inode->gfid)) {
                loc->parent = NULL;
                goto ignore_parent;
        }

        parent = inode_parent (inode, 0, NULL);
        if (!parent) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cannot find parent for inode (gfid:%s)",
                        uuid_utoa (inode->gfid));
        }

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cannot construct path for inode (gfid:%s)",
                        uuid_utoa (inode->gfid));
        }

        ret = quota_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "cannot fill loc");
                goto err;
        }

err:
        if (parent) {
                inode_unref (parent);
        }

        GF_FREE (resolvedpath);

        return ret;
}


int32_t
quota_local_cleanup (xlator_t *this, quota_local_t *local)
{
        if (local == NULL) {
                goto out;
        }

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);
        loc_wipe (&local->oldloc);
        loc_wipe (&local->validate_loc);

        inode_unref (local->inode);
        LOCK_DESTROY (&local->lock);

        mem_put (local);
out:
        return 0;
}


static inline quota_local_t *
quota_local_new ()
{
        quota_local_t *local = NULL;
        local = mem_get0 (THIS->local_pool);
        if (local == NULL)
                goto out;

        LOCK_INIT (&local->lock);
        local->space_available = -1;

out:
        return local;
}


quota_dentry_t *
__quota_dentry_new (quota_inode_ctx_t *ctx, char *name, uuid_t par)
{
        quota_dentry_t    *dentry = NULL;
        GF_UNUSED int32_t  ret    = 0;

        QUOTA_ALLOC_OR_GOTO (dentry, quota_dentry_t, err);

        INIT_LIST_HEAD (&dentry->next);

        dentry->name = gf_strdup (name);
        if (dentry->name == NULL) {
                GF_FREE (dentry);
                dentry = NULL;
                goto err;
        }

        uuid_copy (dentry->par, par);

        if (ctx != NULL)
                list_add_tail (&dentry->next, &ctx->parents);

err:
        return dentry;
}


void
__quota_dentry_free (quota_dentry_t *dentry)
{
        if (dentry == NULL) {
                goto out;
        }

        list_del_init (&dentry->next);

        GF_FREE (dentry->name);
        GF_FREE (dentry);
out:
        return;
}

inline void
quota_link_count_decrement (quota_local_t *local)
{
        call_stub_t *stub       = NULL;
        int          link_count = -1;

        if (local == NULL)
                goto out;

        LOCK (&local->lock);
        {
                link_count = --local->link_count;
                if (link_count == 0) {
                        stub = local->stub;
                        local->stub = NULL;
                }
        }
        UNLOCK (&local->lock);

        if (stub != NULL) {
                call_resume (stub);
        }
out:
        return;
}

inline void
quota_handle_validate_error (quota_local_t *local, int32_t op_ret,
                             int32_t op_errno)
{
        if (local == NULL)
                goto out;

        LOCK (&local->lock);
        {
                if (op_ret < 0) {
                        local->op_ret = op_ret;
                        local->op_errno = op_errno;
                }
        }
        UNLOCK (&local->lock);

        /* we abort checking limits on this path to root */
        quota_link_count_decrement (local);
out:
        return;
}

int32_t
quota_validate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        quota_local_t     *local      = NULL;
        int32_t            ret        = 0;
        quota_inode_ctx_t *ctx        = NULL;
        int64_t           *size       = 0;
        uint64_t           value      = 0;

        local = frame->local;

        if (op_ret < 0) {
                goto unwind;
        }

        GF_ASSERT (local);
        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR ("quota", this, unwind, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, xdata, unwind, op_errno,
                                        EINVAL);

        ret = inode_ctx_get (local->validate_loc.inode, this, &value);

        ctx = (quota_inode_ctx_t *)(unsigned long)value;
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context is not present in inode (gfid:%s)",
                        uuid_utoa (local->validate_loc.inode->gfid));
                op_errno = EINVAL;
                goto unwind;
        }

        ret = dict_get_bin (xdata, QUOTA_SIZE_KEY, (void **) &size);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "size key not present in dict");
                op_errno = EINVAL;
                goto unwind;
        }

        local->just_validated = 1; /* so that we don't go into infinite
                                    * loop of validation and checking
                                    * limit when timeout is zero.
                                    */
        LOCK (&ctx->lock);
        {
                ctx->size = ntoh64 (*size);
                gettimeofday (&ctx->tv, NULL);
        }
        UNLOCK (&ctx->lock);

        quota_check_limit (frame, local->validate_loc.inode, this, NULL, NULL);
        return 0;

unwind:
        quota_handle_validate_error (local, op_ret, op_errno);
        return 0;
}


static inline uint64_t
quota_time_elapsed (struct timeval *now, struct timeval *then)
{
        return (now->tv_sec - then->tv_sec);
}


int32_t
quota_timeout (struct timeval *tv, int32_t timeout)
{
        struct timeval now       = {0,};
        int32_t        timed_out = 0;

        gettimeofday (&now, NULL);

        if (quota_time_elapsed (&now, tv) >= timeout) {
                timed_out = 1;
        }

        return timed_out;
}

inline void
quota_add_parent (quota_dentry_t *dentry, struct list_head *list)
{
        quota_dentry_t *entry = NULL;
        gf_boolean_t    found = _gf_false;

        if ((dentry == NULL) || (list == NULL)) {
                goto out;
        }

        list_for_each_entry (entry, list, next) {
                if (uuid_compare (dentry->par, entry->par) == 0) {
                        found = _gf_true;
                        goto out;
                }
        }

        list_add_tail (&dentry->next, list);

out:
        return;
}

int32_t
quota_build_ancestry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          gf_dirent_t *entries, dict_t *xdata)
{
        inode_t           *parent  = NULL, *tmp_parent = NULL;
        gf_dirent_t       *entry   = NULL;
        loc_t              loc     = {0, };
        quota_dentry_t    *dentry  = NULL, *tmp = NULL;
        quota_inode_ctx_t *ctx     = NULL;
        struct list_head   parents = {0, };
        quota_local_t     *local   = NULL;

        INIT_LIST_HEAD (&parents);

        local = frame->local;
        frame->local = NULL;

        if (op_ret < 0)
                goto err;

        parent = inode_parent (local->loc.inode, 0, NULL);
        if (parent == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "parent is NULL");
                op_errno = EINVAL;
                goto err;
        }

        if ((op_ret > 0) && (entries != NULL)) {
                list_for_each_entry (entry, &entries->list, list) {
                        if (__is_root_gfid (entry->inode->gfid)) {
                                /* The list contains a sub-list for each
                                 * possible path to the target inode. Each
                                 * sub-list starts with the root entry of the
                                 * tree and is followed by the child entries
                                 * for a particular path to the target entry.
                                 * The root entry is an implied sub-list
                                 * delimiter, as it denotes we have started
                                 * processing a new path. Reset the parent
                                 * pointer and continue
                                 */

                                tmp_parent = NULL;
                        }

                        uuid_copy (loc.gfid, entry->d_stat.ia_gfid);

                        loc.inode = inode_ref (entry->inode);
                        loc.parent = inode_ref (tmp_parent);
                        loc.name = entry->d_name;

                        quota_fill_inodectx (this, entry->inode, entry->dict,
                                             &loc, &entry->d_stat, &op_errno);

                        tmp_parent = entry->inode;

                        loc_wipe (&loc);
                }
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);

        if (ctx != NULL) {
                LOCK (&ctx->lock);
                {
                        list_for_each_entry (dentry, &ctx->parents, next) {
                                /* we built ancestry for a non-directory */
                                tmp = __quota_dentry_new (NULL, dentry->name,
                                                          dentry->par);
                                quota_add_parent (tmp, &parents);

                                if (list_empty (&tmp->next)) {
                                        __quota_dentry_free (tmp);
                                        tmp = NULL;
                                }
                        }
                }
                UNLOCK (&ctx->lock);
        }

        if (list_empty (&parents)) {
                /* we built ancestry for a directory */
                list_for_each_entry (entry, &entries->list, list) {
                        if (entry->inode == local->loc.inode)
                                break;
                }

                GF_ASSERT (&entry->list != &entries->list);

                tmp = __quota_dentry_new (NULL, entry->d_name, parent->gfid);
                quota_add_parent (tmp, &parents);
        }

        local->ancestry_cbk (&parents, local->loc.inode, 0, 0,
                             local->ancestry_data);
        goto cleanup;

err:
        local->ancestry_cbk (NULL, NULL, -1, op_errno, local->ancestry_data);

cleanup:
        STACK_DESTROY (frame->root);
        quota_local_cleanup (this, local);

        if (parent != NULL) {
                inode_unref (parent);
                parent = NULL;
        }

        list_for_each_entry_safe (dentry, tmp, &parents, next) {
                __quota_dentry_free (dentry);
        }

        return 0;
}

int32_t
quota_build_ancestry_open_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               fd_t *fd, dict_t *xdata)
{
        dict_t        *xdata_req = NULL;
        quota_local_t *local     = NULL;

        if (op_ret < 0) {
                goto err;
        }

        xdata_req = dict_new ();
        if (xdata_req == NULL) {
                op_ret = -ENOMEM;
                goto err;
        }

        op_ret = dict_set_int8 (xdata_req, QUOTA_LIMIT_KEY, 1);
        if (op_ret < 0) {
                op_errno = -op_ret;
                goto err;
        }

        op_ret = dict_set_int8 (xdata_req, GET_ANCESTRY_DENTRY_KEY, 1);
        if (op_ret < 0) {
                op_errno = -op_ret;
                goto err;
        }

        /* This would ask posix layer to construct dentry chain till root */
        STACK_WIND (frame, quota_build_ancestry_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp, fd, 0, 0, xdata_req);

        op_ret = 0;

err:
        fd_unref (fd);

        dict_unref (xdata_req);

        if (op_ret < 0) {
                local = frame->local;
                frame->local = NULL;

                local->ancestry_cbk (NULL, NULL, -1, op_errno,
                                     local->ancestry_data);
                quota_local_cleanup (this, local);
                STACK_DESTROY (frame->root);
        }

        return 0;
}

int
quota_build_ancestry (inode_t *inode, quota_ancestry_built_t ancestry_cbk,
                      void *data)
{
        loc_t          loc       = {0, };
        fd_t          *fd        = NULL;
        quota_local_t *local     = NULL;
        call_frame_t  *new_frame = NULL;
        int            op_errno  = EINVAL;
        xlator_t      *this      = NULL;

        this = THIS;

        loc.inode = inode_ref (inode);
        uuid_copy (loc.gfid, inode->gfid);

        fd = fd_create (inode, 0);

        new_frame = create_frame (this, this->ctx->pool);
        if (new_frame == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        new_frame->root->uid = new_frame->root->gid = 0;

        local = quota_local_new ();
        if (local == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        new_frame->local = local;
        local->ancestry_cbk = ancestry_cbk;
        local->ancestry_data = data;
        local->loc.inode = inode_ref (inode);

        if (IA_ISDIR (inode->ia_type)) {
                STACK_WIND (new_frame, quota_build_ancestry_open_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir, &loc, fd,
                            NULL);
        } else {
                STACK_WIND (new_frame, quota_build_ancestry_open_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open, &loc, 0, fd,
                            NULL);
        }

        loc_wipe (&loc);
        return 0;

err:
        ancestry_cbk (NULL, NULL, -1, op_errno, data);

        fd_unref (fd);

        local = new_frame->local;
        new_frame->local = NULL;

        if (local != NULL) {
                quota_local_cleanup (this, local);
        }

        if (new_frame != NULL) {
                STACK_DESTROY (new_frame->root);
        }

        loc_wipe (&loc);
        return 0;
}

int
quota_validate (call_frame_t *frame, inode_t *inode, xlator_t *this,
                fop_lookup_cbk_t cbk_fn)
{
        quota_local_t     *local = NULL;
        int                ret   = 0;
        dict_t            *xdata = NULL;
        quota_priv_t      *priv  = NULL;

        local = frame->local;
        priv = this->private;

        LOCK (&local->lock);
        {
                loc_wipe (&local->validate_loc);

                ret = quota_inode_loc_fill (inode, &local->validate_loc);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot fill loc for inode (gfid:%s), hence "
                                "aborting quota-checks and continuing with fop",
                                uuid_utoa (inode->gfid));
                }
        }
        UNLOCK (&local->lock);

        if (ret < 0) {
                ret = -ENOMEM;
                goto err;
        }

        xdata = dict_new ();
        if (xdata == NULL) {
                ret = -ENOMEM;
                goto err;
        }

        ret = dict_set_int8 (xdata, QUOTA_SIZE_KEY, 1);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "dict set failed");
                ret = -ENOMEM;
                goto err;
        }

        ret = dict_set_str (xdata, "volume-uuid", priv->volume_uuid);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "dict set failed");
                ret = -ENOMEM;
                goto err;
        }

        ret = quota_enforcer_lookup (frame, this, &local->validate_loc, xdata,
                                     cbk_fn);
        if (ret < 0) {
                ret = -ENOTCONN;
                goto err;
        }

        ret = 0;
err:
        return ret;
}

void
quota_check_limit_continuation (struct list_head *parents, inode_t *inode,
                                int32_t op_ret, int32_t op_errno, void *data)
{
        call_frame_t   *frame        = NULL;
        xlator_t       *this         = NULL;
        quota_local_t  *local        = NULL;
        quota_dentry_t *entry        = NULL;
        inode_t        *parent       = NULL;
        int             parent_count = 0;

        frame = data;
        local = frame->local;
        this = THIS;

        if ((op_ret < 0) || list_empty (parents)) {
                if (list_empty (parents)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Couldn't build ancestry for inode (gfid:%s). "
                                "Without knowing ancestors till root, quota "
                                "cannot be enforced. "
                                "Hence, failing fop with EIO",
                                uuid_utoa (inode->gfid));
                        op_errno = EIO;
                }

                quota_handle_validate_error (local, -1, op_errno);
                goto out;
        }

        list_for_each_entry (entry, parents, next) {
                parent_count++;
        }

        LOCK (&local->lock);
        {
                local->link_count += (parent_count - 1);
        }
        UNLOCK (&local->lock);

        list_for_each_entry (entry, parents, next) {
                parent = inode_find (inode->table, entry->par);

                quota_check_limit (frame, parent, this, NULL, NULL);
        }

out:
        return;
}

int32_t
quota_check_limit (call_frame_t *frame, inode_t *inode, xlator_t *this,
                   char *name, uuid_t par)
{
        int32_t            ret                 = -1, op_errno = EINVAL;
        inode_t           *_inode              = NULL, *parent = NULL;
        quota_inode_ctx_t *ctx                 = NULL;
        quota_priv_t      *priv                = NULL;
        quota_local_t     *local               = NULL;
        char               need_validate       = 0;
        gf_boolean_t       hard_limit_exceeded = 0;
        int64_t            delta               = 0, wouldbe_size = 0;
        int64_t            space_available     = 0;
        uint64_t           value               = 0;
        char               just_validated      = 0;
        uuid_t             trav_uuid           = {0,};
        uint32_t           timeout             = 0;

        GF_VALIDATE_OR_GOTO ("quota", this, err);
        GF_VALIDATE_OR_GOTO (this->name, frame, err);
        GF_VALIDATE_OR_GOTO (this->name, inode, err);

        local  = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, local, err);

        delta = local->delta;

        GF_VALIDATE_OR_GOTO (this->name, local->stub, err);
        /* Allow all the trusted clients
         * Don't block the gluster internal processes like rebalance, gsyncd,
         * self heal etc from the disk quotas.
         *
         * Method: Allow all the clients with PID negative. This is by the
         * assumption that any kernel assigned pid doesn't have the negative
         * number.
         */
        if (0 > frame->root->pid) {
                ret = 0;
                quota_link_count_decrement (local);
                goto done;
        }

        priv = this->private;

        inode_ctx_get (inode, this, &value);
        ctx = (quota_inode_ctx_t *)(unsigned long)value;

        _inode = inode_ref (inode);

        LOCK (&local->lock);
        {
                just_validated = local->just_validated;
                local->just_validated = 0;
        }
        UNLOCK (&local->lock);

        if ( par != NULL ) {
                uuid_copy (trav_uuid, par);
        }

        do {
                if (ctx != NULL && (ctx->hard_lim > 0 || ctx->soft_lim > 0)) {
                        wouldbe_size = ctx->size + delta;

                        LOCK (&ctx->lock);
                        {
                                timeout = priv->soft_timeout;

                                if ((ctx->soft_lim >= 0)
                                    && (wouldbe_size > ctx->soft_lim)) {
                                        timeout = priv->hard_timeout;
                                }

                                if (!just_validated
                                    && quota_timeout (&ctx->tv, timeout)) {
                                        need_validate = 1;
                                } else if (wouldbe_size >= ctx->hard_lim) {
                                        hard_limit_exceeded = 1;
                                }
                        }
                        UNLOCK (&ctx->lock);

                        if (need_validate) {
                                ret = quota_validate (frame, _inode, this,
                                                      quota_validate_cbk);
                                if (ret < 0) {
                                        op_errno = -ret;
                                        goto err;
                                }

                                break;
                        }

                        if (hard_limit_exceeded) {
                                local->op_ret = -1;
                                local->op_errno = EDQUOT;

                                space_available = ctx->hard_lim - ctx->size;

                                if (space_available < 0)
                                        space_available = 0;

                                if ((local->space_available < 0)
                                    || (local->space_available
                                        > space_available)){
                                        local->space_available
                                                = space_available;

                                }

                                if (space_available == 0) {
                                        op_errno = EDQUOT;
                                        goto err;
                                }
                        }

                        /* We log usage only if quota limit is configured on
                           that inode. */
                        quota_log_usage (this, ctx, _inode, delta);
                }

                if (__is_root_gfid (_inode->gfid)) {
                        quota_link_count_decrement (local);
                        break;
                }

                parent = inode_parent (_inode, trav_uuid, name);

                if (name != NULL) {
                        name = NULL;
                        uuid_clear (trav_uuid);
                }

                if (parent == NULL) {
                        ret = quota_build_ancestry (_inode,
                                                    quota_check_limit_continuation,
                                                    frame);
                        if (ret < 0) {
                                op_errno = -ret;
                                goto err;
                        }

                        break;
                }

                inode_unref (_inode);
                _inode = parent;
                just_validated = 0;

                if (_inode == NULL) {
                        break;
                }

                value = 0;
                inode_ctx_get (_inode, this, &value);
                ctx = (quota_inode_ctx_t *)(unsigned long)value;
        } while (1);

        if (_inode != NULL) {
                inode_unref (_inode);
                _inode = NULL;
        }

done:
        return 0;

err:
        quota_handle_validate_error (local, -1, op_errno);

        inode_unref (_inode);
        return 0;
}

inline int
quota_get_limits (xlator_t *this, dict_t *dict, int64_t *hard_lim,
                  int64_t *soft_lim)
{
        quota_limit_t *limit            = NULL;
        quota_priv_t  *priv             = NULL;
        int64_t        soft_lim_percent = 0, *ptr = NULL;
        int            ret              = 0;

        if ((this == NULL) || (dict == NULL) || (hard_lim == NULL)
            || (soft_lim == NULL))
                goto out;

        priv = this->private;

        ret = dict_get_bin (dict, QUOTA_LIMIT_KEY, (void **) &ptr);
        limit = (quota_limit_t *)ptr;

        if (limit) {
                *hard_lim = ntoh64 (limit->hard_lim);
                soft_lim_percent = ntoh64 (limit->soft_lim_percent);
        }

        if (soft_lim_percent < 0) {
                soft_lim_percent = priv->default_soft_lim;
        }

        if ((*hard_lim > 0) && (soft_lim_percent > 0)) {
                *soft_lim = (soft_lim_percent * (*hard_lim))/100;
        }

out:
        return 0;
}

int
quota_fill_inodectx (xlator_t *this, inode_t *inode, dict_t *dict,
                     loc_t *loc, struct iatt *buf, int32_t *op_errno)
{
        int32_t            ret      = -1;
        char               found    = 0;
        quota_inode_ctx_t *ctx      = NULL;
        quota_dentry_t    *dentry   = NULL;
        uint64_t           value    = 0;
        int64_t            hard_lim = -1, soft_lim = -1;

        quota_get_limits (this, dict, &hard_lim, &soft_lim);

        inode_ctx_get (inode, this, &value);
        ctx = (quota_inode_ctx_t *)(unsigned long)value;

        if ((((ctx == NULL) || (ctx->hard_lim == hard_lim))
             && (hard_lim < 0) && !QUOTA_REG_OR_LNK_FILE (buf->ia_type))) {
                ret = 0;
                goto out;
        }

        ret = quota_inode_ctx_get (inode, this, &ctx, 1);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "cannot create quota "
                        "context in inode(gfid:%s)",
                        uuid_utoa (inode->gfid));
                ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->hard_lim = hard_lim;
                ctx->soft_lim = soft_lim;

                ctx->buf = *buf;

                if (!QUOTA_REG_OR_LNK_FILE (buf->ia_type)) {
                        goto unlock;
                }

                if (loc->name == NULL)
                        goto unlock;

                list_for_each_entry (dentry, &ctx->parents, next) {
                        if ((strcmp (dentry->name, loc->name) == 0) &&
                            (uuid_compare (loc->parent->gfid,
                                           dentry->par) == 0)) {
                                found = 1;
                                break;
                        }
                }

                if (!found) {
                        dentry = __quota_dentry_new (ctx,
                                                     (char *)loc->name,
                                                     loc->parent->gfid);
                        if (dentry == NULL) {
                                /*
                                  gf_log (this->name, GF_LOG_WARNING,
                                        "cannot create a new dentry (par:%"
                                        PRId64", name:%s) for inode(ino:%"
                                        PRId64", gfid:%s)",
                                        uuid_utoa (local->loc.inode->gfid));
                                */
                                ret = -1;
                                *op_errno = ENOMEM;
                                goto unlock;
                        }
                }
        }
unlock:
        UNLOCK (&ctx->lock);

out:
        return ret;
}

int32_t
quota_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        quota_local_t *local = NULL;

        if (op_ret < 0)
                goto unwind;

        local = frame->local;

        op_ret = quota_fill_inodectx (this, inode, dict, &local->loc, buf,
                                      &op_errno);

unwind:
        QUOTA_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                            dict, postparent);
        return 0;
}


int32_t
quota_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xattr_req)
{
        quota_priv_t  *priv             = NULL;
        int32_t        ret              = -1;
        quota_local_t *local            = NULL;

        priv = this->private;

        xattr_req = xattr_req ? dict_ref(xattr_req) : dict_new();
        if (!xattr_req)
                goto err;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;
        loc_copy (&local->loc, loc);

        ret = dict_set_int8 (xattr_req, QUOTA_LIMIT_KEY, 1);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dict set of key for hard-limit failed");
                goto err;
        }

        STACK_WIND (frame, quota_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

        ret = 0;

err:
        if (xattr_req)
                dict_unref (xattr_req);

        if (ret < 0) {
                QUOTA_STACK_UNWIND (lookup, frame, -1, ENOMEM,
                                    NULL, NULL, NULL, NULL);
        }

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->lookup, loc, xattr_req);
        return 0;
}

int32_t
quota_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        int32_t                  ret            = 0;
        uint64_t                 ctx_int        = 0;
        quota_inode_ctx_t       *ctx            = NULL;
        quota_local_t           *local          = NULL;

        local = frame->local;

        if ((op_ret < 0) || (local == NULL)) {
                goto out;
        }

        ret = inode_ctx_get (local->loc.inode, this, &ctx_int);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to get the context", local->loc.path);
                goto out;
        }

        ctx = (quota_inode_ctx_t *)(unsigned long) ctx_int;

        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in %s (gfid:%s)",
                        local->loc.path, uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *postbuf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);

        return 0;
}


int32_t
quota_writev_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     struct iovec *vector, int32_t count, off_t off,
                     uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        quota_local_t *local      = NULL;
        int32_t        op_errno   = EINVAL;
        quota_priv_t  *priv       = NULL;
        struct iovec  *new_vector = NULL;
        int32_t        new_count  = 0;

        priv = this->private;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        if (local->op_ret == -1) {
                op_errno = local->op_errno;

                if ((op_errno == EDQUOT) && (local->space_available > 0)) {
                        new_count = iov_subset (vector, count, 0,
                                                local->space_available, NULL);

                        new_vector = GF_CALLOC (new_count,
                                                sizeof (struct iovec),
                                                gf_common_mt_iovec);
                        if (new_vector == NULL) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                goto unwind;
                        }

                        new_count = iov_subset (vector, count, 0,
                                                local->space_available,
                                                new_vector);

                        vector = new_vector;
                        count = new_count;
                } else {
                        goto unwind;
                }
        }

        STACK_WIND (frame, quota_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev, fd,
                    vector, count, off, flags, iobref, xdata);

        if (new_vector != NULL)
                GF_FREE (new_vector);

        return 0;

unwind:
        QUOTA_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
quota_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t off,
              uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        quota_priv_t      *priv    = NULL;
        int32_t            ret     = -1, op_errno = EINVAL;
        int32_t            parents = 0;
        uint64_t           size    = 0;
        quota_local_t     *local   = NULL;
        quota_inode_ctx_t *ctx     = NULL;
        quota_dentry_t    *dentry  = NULL, *tmp = NULL;
        call_stub_t       *stub    = NULL;
        struct list_head   head    = {0, };

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        INIT_LIST_HEAD (&head);

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("quota", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;
        local->loc.inode = inode_ref (fd->inode);

        ret = quota_inode_ctx_get (fd->inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (fd->inode->gfid));
        }

        stub = fop_writev_stub (frame, quota_writev_helper, fd, vector, count,
                                off, flags, iobref, xdata);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, unwind);

        size = iov_length (vector, count);
        if (ctx != NULL) {
                LOCK (&ctx->lock);
                {
                        list_for_each_entry (dentry, &ctx->parents, next) {
                                tmp = __quota_dentry_new (NULL, dentry->name,
                                                          dentry->par);
                                list_add_tail (&tmp->next, &head);
                                parents++;
                        }
                }
                UNLOCK (&ctx->lock);
        }

        LOCK (&local->lock);
        {
                local->delta = size;
                local->link_count = (parents != 0) ? parents : 1;
                local->stub = stub;
        }
        UNLOCK (&local->lock);

        if (parents == 0) {
                /* nameless lookup on this inode, allow quota to reconstruct
                 * ancestry as part of check_limit.
                 */
                quota_check_limit (frame, fd->inode, this, NULL, NULL);
        } else {
                list_for_each_entry_safe (dentry, tmp, &head, next) {
                        quota_check_limit (frame, fd->inode, this, dentry->name,
                                           dentry->par);
                        __quota_dentry_free (dentry);
                }
        }

        return 0;

unwind:
        QUOTA_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->writev, fd,
                         vector, count, off, flags, iobref, xdata);
        return 0;
}


int32_t
quota_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        QUOTA_STACK_UNWIND (mkdir, frame, op_ret, op_errno, inode,
                            buf, preparent, postparent, xdata);
        return 0;
}


int32_t
quota_mkdir_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    mode_t mode, mode_t umask, dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        op_errno = local->op_errno;

        if (local->op_ret == -1) {
                goto unwind;
        }

        STACK_WIND (frame, quota_mkdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir, loc,
                    mode, umask, xdata);

        return 0;

unwind:
        QUOTA_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL);
        return 0;
}


int32_t
quota_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
        quota_priv_t  *priv  = NULL;
        int32_t        ret   = 0, op_errno = 0;
        quota_local_t *local = NULL;
        call_stub_t   *stub  = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        stub = fop_mkdir_stub (frame, quota_mkdir_helper, loc, mode, umask,
                               xdata);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        LOCK (&local->lock);
        {
                local->stub = stub;
                local->delta = 0;
                local->link_count = 1;
        }
        UNLOCK (&local->lock);

        quota_check_limit (frame, loc->parent, this, NULL, NULL);
        return 0;

err:
        QUOTA_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL);

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->mkdir,
                         loc, mode, umask, xdata);

        return 0;
}


int32_t
quota_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        int32_t            ret    = -1;
        quota_local_t     *local  = NULL;
        quota_inode_ctx_t *ctx    = NULL;
        quota_dentry_t    *dentry = NULL;

        local = frame->local;
        if (op_ret < 0) {
                goto unwind;
        }

        ret = quota_inode_ctx_get (inode, this, &ctx, 1);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "cannot create quota "
                        "context in inode(gfid:%s)",
                        uuid_utoa (inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *buf;

                dentry = __quota_dentry_new (ctx, (char *)local->loc.name,
                                             local->loc.parent->gfid);
                if (dentry == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot create a new dentry (name:%s) for "
                                "inode(gfid:%s)", local->loc.name,
                                uuid_utoa (local->loc.inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unlock;
                }
        }
unlock:
        UNLOCK (&ctx->lock);

unwind:
        QUOTA_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, buf,
                            preparent, postparent, xdata);
        return 0;
}


int32_t
quota_create_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
                     dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;
        quota_priv_t  *priv     = NULL;

        local = frame->local;

        priv = this->private;

        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        if (local->op_ret == -1) {
                op_errno = local->op_errno;
                goto unwind;
        }


        STACK_WIND (frame, quota_create_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->create, loc,
                    flags, mode, umask, fd, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
quota_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        quota_priv_t  *priv     = NULL;
        int32_t        ret      = -1;
        quota_local_t *local    = NULL;
        int32_t        op_errno = 0;
        call_stub_t   *stub     = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_create_stub (frame, quota_create_helper, loc, flags, mode,
                                umask, fd, xdata);
        if (stub == NULL) {
                goto err;
        }

        LOCK (&local->lock);
        {
                local->link_count = 1;
                local->stub = stub;
                local->delta = 0;
        }
        UNLOCK (&local->lock);

        quota_check_limit (frame, loc->parent, this, NULL, NULL);
        return 0;
err:
        QUOTA_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL, NULL);

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD (this)->fops->create, loc,
                         flags, mode, umask, fd, xdata);
        return 0;
}


int32_t
quota_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;
        uint64_t           value = 0;
        quota_dentry_t    *dentry = NULL;
        quota_dentry_t    *old_dentry = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = (quota_local_t *) frame->local;

        inode_ctx_get (local->loc.inode, this, &value);
        ctx = (quota_inode_ctx_t *)(unsigned long)value;

        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                list_for_each_entry (dentry, &ctx->parents, next) {
                        if ((strcmp (dentry->name, local->loc.name) == 0) &&
                            (uuid_compare (local->loc.parent->gfid,
                                           dentry->par) == 0)) {
                                old_dentry = dentry;
                                break;
                        }
                }
                if (old_dentry)
                        __quota_dentry_free (old_dentry);
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent,
                            postparent, xdata);
        return 0;
}


int32_t
quota_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        int32_t        ret = -1;
        quota_local_t *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        if (xdata && dict_get (xdata, GLUSTERFS_INTERNAL_FOP_KEY)) {
                local->skip_check = _gf_true;
        }

        ret = loc_copy (&local->loc, loc);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        STACK_WIND (frame, quota_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);

        ret = 0;

err:
        if (ret == -1) {
                QUOTA_STACK_UNWIND (unlink, frame, -1, 0, NULL, NULL, NULL);
        }

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
        return 0;
}


int32_t
quota_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        int32_t               ret          = -1;
        quota_local_t        *local        = NULL;
        quota_inode_ctx_t    *ctx          = NULL;
        quota_dentry_t       *dentry       = NULL;
        char                  found        = 0;

        if (op_ret < 0) {
                goto out;
        }

        local = (quota_local_t *) frame->local;

        if (local->skip_check)
                goto out;

        ret = quota_inode_ctx_get (inode, this, &ctx, 0);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                list_for_each_entry (dentry, &ctx->parents, next) {
                        if ((strcmp (dentry->name, local->loc.name) == 0) &&
                            (uuid_compare (local->loc.parent->gfid,
                                           dentry->par) == 0)) {
                                found = 1;
                                gf_log (this->name, GF_LOG_WARNING,
                                        "new entry being linked (name:%s) for "
                                        "inode (gfid:%s) is already present "
                                        "in inode-dentry-list", dentry->name,
                                        uuid_utoa (local->loc.inode->gfid));
                                break;
                        }
                }

                if (!found) {
                        dentry = __quota_dentry_new (ctx,
                                                     (char *)local->loc.name,
                                                     local->loc.parent->gfid);
                        if (dentry == NULL) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot create a new dentry (name:%s) "
                                        "for inode(gfid:%s)", local->loc.name,
                                        uuid_utoa (local->loc.inode->gfid));
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unlock;
                        }
                }

                ctx->buf = *buf;
        }
unlock:
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (link, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);

        return 0;
}


int32_t
quota_link_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                   loc_t *newloc, dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;
        quota_priv_t  *priv     = NULL;

        priv = this->private;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        op_errno = local->op_errno;

        if (local->op_ret == -1) {
                goto unwind;
        }

        STACK_WIND (frame, quota_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link, oldloc,
                    newloc, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL);
        return 0;
}


int32_t
quota_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        quota_priv_t      *priv  = NULL;
        int32_t            ret   = -1, op_errno = ENOMEM;
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;
        call_stub_t       *stub  = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        if (xdata && dict_get (xdata, GLUSTERFS_INTERNAL_FOP_KEY)) {
                goto off;
        }

        quota_inode_ctx_get (oldloc->inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (oldloc->inode->gfid));
        }

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = (void *) local;


        ret = loc_copy (&local->loc, newloc);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        stub = fop_link_stub (frame, quota_link_helper, oldloc, newloc, xdata);
        if (stub == NULL) {
                goto err;
        }

        LOCK (&local->lock);
        {
                local->link_count = 1;
                local->stub = stub;
                local->delta = (ctx != NULL) ? ctx->buf.ia_blocks * 512 : 0;
        }
        UNLOCK (&local->lock);

        quota_check_limit (frame, newloc->parent, this, NULL, NULL);
        return 0;

err:
        QUOTA_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->link, oldloc,
                         newloc, xdata);
        return 0;
}


int32_t
quota_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  struct iatt *preoldparent, struct iatt *postoldparent,
                  struct iatt *prenewparent, struct iatt *postnewparent,
                  dict_t *xdata)
{
        int32_t               ret              = -1;
        int64_t               size             = 0;
        quota_local_t        *local            = NULL;
        quota_inode_ctx_t    *ctx              = NULL;
        quota_dentry_t       *old_dentry       = NULL, *dentry = NULL;
        char                  new_dentry_found = 0;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        if (QUOTA_REG_OR_LNK_FILE (local->oldloc.inode->ia_type)) {
                size = buf->ia_blocks * 512;
        }

        if (!QUOTA_REG_OR_LNK_FILE (local->oldloc.inode->ia_type)) {
                goto out;
        }

        ret = quota_inode_ctx_get (local->oldloc.inode, this, &ctx, 0);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->oldloc.inode->gfid));

                goto out;
        }

        LOCK (&ctx->lock);
        {
                /* decision of whether to create a context in newloc->inode
                 * depends on fuse_rename_cbk's choice of inode it retains
                 * after rename. currently it just associates oldloc->inode
                 * with new parent and name. If this changes, following code
                 * should be changed to set a new context in newloc->inode.
                 */
                list_for_each_entry (dentry, &ctx->parents, next) {
                        if ((strcmp (dentry->name, local->oldloc.name) == 0) &&
                            (uuid_compare (local->oldloc.parent->gfid,
                                           dentry->par) == 0)) {
                                old_dentry = dentry;
                        } else if ((strcmp (dentry->name,
                                            local->newloc.name) == 0) &&
                                   (uuid_compare (local->oldloc.parent->gfid,
                                                  dentry->par) == 0)) {
                                new_dentry_found = 1;
                                gf_log (this->name, GF_LOG_WARNING,
                                        "new entry being linked (name:%s) for "
                                        "inode (gfid:%s) is already present "
                                        "in inode-dentry-list", dentry->name,
                                        uuid_utoa (local->newloc.inode->gfid));
                                break;
                        }
                }

                if (old_dentry != NULL) {
                        __quota_dentry_free (old_dentry);
                } else {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dentry corresponding to the path just renamed "
                                "(name:%s) is not present", local->oldloc.name);
                }

                if (!new_dentry_found) {
                        dentry = __quota_dentry_new (ctx,
                                                     (char *)local->newloc.name,
                                                     local->newloc.parent->gfid);
                        if (dentry == NULL) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot create a new dentry (name:%s) "
                                        "for inode(gfid:%s)",
                                        local->newloc.name,
                                        uuid_utoa (local->newloc.inode->gfid));
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unlock;
                        }
                }

                ctx->buf = *buf;
        }
unlock:
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (rename, frame, op_ret, op_errno, buf, preoldparent,
                            postoldparent, prenewparent, postnewparent, xdata);

        return 0;
}


int32_t
quota_rename_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                     loc_t *newloc, dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;
        quota_priv_t  *priv     = NULL;

        priv = this->private;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        op_errno = local->op_errno;

        if (local->op_ret == -1) {
                goto unwind;
        }

        STACK_WIND (frame, quota_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename, oldloc,
                    newloc, xdata);

        return 0;

unwind:
        QUOTA_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL, NULL);
        return 0;
}


static int32_t
quota_rename_get_size_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *buf, dict_t *xdata,
                           struct iatt *postparent)
{
        quota_local_t     *local      = NULL;
        int32_t            ret        = 0;
        int64_t           *size       = 0;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR ("quota", this, out, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, xdata, out, op_errno,
                                        EINVAL);
        local = frame->local;
        GF_ASSERT (local);
        local->link_count = 1;

        if (op_ret < 0)
                goto out;


        ret = dict_get_bin (xdata, QUOTA_SIZE_KEY, (void **) &size);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "size key not present in dict");
                op_errno = EINVAL;
                goto out;
        }
        local->delta = ntoh64 (*size);
        quota_check_limit (frame, local->newloc.parent, this,
                           NULL, NULL);
        return 0;

out:
        quota_handle_validate_error (local, -1, op_errno);
        return 0;
}

int32_t
quota_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
              loc_t *newloc, dict_t *xdata)
{
        quota_priv_t      *priv  = NULL;
        int32_t            ret   = -1, op_errno = ENOMEM;
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;
        call_stub_t       *stub  = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->oldloc, oldloc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        ret = loc_copy (&local->newloc, newloc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        stub = fop_rename_stub (frame, quota_rename_helper, oldloc, newloc,
                                xdata);
        if (stub == NULL) {
                goto err;
        }

        LOCK (&local->lock);
        {
                local->link_count = 1;
                local->stub = stub;
        }
        UNLOCK (&local->lock);

        if (QUOTA_REG_OR_LNK_FILE (oldloc->inode->ia_type)) {
                ret = quota_inode_ctx_get (oldloc->inode, this, &ctx, 0);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota context not set in inode (gfid:%s), "
                                "considering file size as zero while enforcing "
                                "quota on new ancestry",
                                oldloc->inode ? uuid_utoa (oldloc->inode->gfid)
                                : "0");
                        local->delta = 0;

                } else {

                    /* FIXME: We need to account for the size occupied by this
                     * inode on the target directory. To avoid double
                     * accounting, we need to modify enforcer to perform
                     * quota_check_limit only uptil the least common ancestor
                     * directory inode*/

                    /* FIXME: The following code assumes that regular files and
                     *linkfiles are present, in their entirety, in a single
                     brick. This *assumption is invalid in the case of
                     stripe.*/

                    local->delta = ctx->buf.ia_blocks * 512;
                }

        } else if (IA_ISDIR (oldloc->inode->ia_type)) {
                        ret = quota_validate (frame, oldloc->inode, this,
                                              quota_rename_get_size_cbk);
                        if (ret){
                                op_errno = -ret;
                                goto err;
                        }

                        return 0;
        }

        quota_check_limit (frame, newloc->parent, this, NULL, NULL);
        return 0;

err:
        QUOTA_STACK_UNWIND (rename, frame, -1, op_errno, NULL,
                            NULL, NULL, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->rename, oldloc,
                         newloc, xdata);

        return 0;
}


int32_t
quota_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        quota_local_t     *local  = NULL;
        quota_inode_ctx_t *ctx    = NULL;
        quota_dentry_t    *dentry = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 1);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));

                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *buf;

                dentry = __quota_dentry_new (ctx, (char *)local->loc.name,
                                             local->loc.parent->gfid);
                if (dentry == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot create a new dentry (name:%s) for "
                                "inode(gfid:%s)", local->loc.name,
                                uuid_utoa (local->loc.inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (symlink, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);

        return 0;
}


int
quota_symlink_helper (call_frame_t *frame, xlator_t *this, const char *linkpath,
                      loc_t *loc, mode_t umask, dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;
        quota_priv_t  *priv     = NULL;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        priv = this->private;

        if (local->op_ret == -1) {
                op_errno = local->op_errno;
                goto unwind;
        }

        STACK_WIND (frame, quota_symlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc, umask, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (symlink, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL);
        return 0;
}


int
quota_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc, mode_t umask, dict_t *xdata)
{
        quota_priv_t  *priv     = NULL;
        int32_t        ret      = -1;
        int32_t        op_errno = ENOMEM;
        quota_local_t *local    = NULL;
        call_stub_t   *stub     = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        stub = fop_symlink_stub (frame, quota_symlink_helper, linkpath, loc,
                                 umask, xdata);
        if (stub == NULL) {
                goto err;
        }

        LOCK (&local->lock);
        {
                local->stub = stub;
                local->delta = strlen (linkpath);
                local->link_count = 1;
        }
        UNLOCK (&local->lock);

        quota_check_limit (frame, loc->parent, this, NULL, NULL);
        return 0;

err:
        QUOTA_STACK_UNWIND (symlink, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL);

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->symlink,
                         linkpath, loc, umask, xdata);
        return 0;
}


int32_t
quota_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *postbuf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (truncate, frame, op_ret, op_errno, prebuf,
                            postbuf, xdata);
        return 0;
}


int32_t
quota_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        int32_t          ret   = -1;
        quota_local_t   *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        ret =  loc_copy (&local->loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        STACK_WIND (frame, quota_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);

        return 0;

err:
        QUOTA_STACK_UNWIND (truncate, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;
}


int32_t
quota_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *postbuf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (ftruncate, frame, op_ret, op_errno, prebuf,
                            postbuf, xdata);
        return 0;
}


int32_t
quota_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t   *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL)
                goto err;

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        STACK_WIND (frame, quota_ftruncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate, fd,
                    offset, xdata);

        return 0;
err:
        QUOTA_STACK_UNWIND (ftruncate, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->ftruncate, fd,
                         offset, xdata);
        return 0;
}


int32_t
quota_send_dir_limit_to_cli (call_frame_t *frame, xlator_t *this,
                             inode_t *inode, const char *name)
{
        int32_t            ret               = 0;
        char               dir_limit [1024]  = {0, };
        dict_t            *dict              = NULL;
        quota_inode_ctx_t *ctx               = NULL;
        uint64_t           value             = 0;
        quota_priv_t      *priv              = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                snprintf (dir_limit, 1024, "Quota is disabled please turn on");
                goto dict_set;
        }

        ret = inode_ctx_get (inode, this, &value);
        if (ret < 0)
                goto out;

        ctx = (quota_inode_ctx_t *)(unsigned long)value;
        snprintf (dir_limit, 1024, "%"PRId64",%"PRId64, ctx->size,
                  ctx->hard_lim);

dict_set:
        dict = dict_new ();
        if (dict == NULL) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, (char *) name, dir_limit);
        if (ret < 0)
                goto out;

        gf_log (this->name, GF_LOG_DEBUG, "str = %s", dir_limit);

        QUOTA_STACK_UNWIND (getxattr, frame, 0, 0, dict, NULL);

        ret = 0;

out:
        return ret;
}


int32_t
quota_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        int32_t ret     = 0;

        if (name && strcasecmp (name, "trusted.limit.list") == 0) {
                ret = quota_send_dir_limit_to_cli (frame, this, fd->inode,
                                                   name);
                if (ret == 0) {
                        return 0;
                }
        }

        STACK_WIND (frame, default_fgetxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);
        return 0;
}


int32_t
quota_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        int32_t ret     = 0;

        if ((name != NULL) && strcasecmp (name, "trusted.limit.list") == 0) {
                ret = quota_send_dir_limit_to_cli (frame, this, loc->inode,
                                                   name);
                if (ret == 0)
                        return 0;
        }

        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
        return 0;
}


int32_t
quota_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                if (!IA_ISDIR (buf->ia_type)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "quota context is NULL on "
                                "inode (%s). "
                                "If quota is not enabled recently and crawler "
                                "has finished crawling, its an error",
                                uuid_utoa (local->loc.inode->gfid));
                }

                goto out;
        }

        LOCK (&ctx->lock);
        {
                if (buf)
                        ctx->buf = *buf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int32_t
quota_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;
        int32_t        ret   = -1;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;
        ret = loc_copy (&local->loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto unwind;
        }

        STACK_WIND (frame, quota_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc,
                    xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (stat, frame, -1, ENOMEM, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->stat, loc,
                         xdata);
        return 0;
}


int32_t
quota_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf,
                 dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                if (!IA_ISDIR (buf->ia_type)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "quota context is NULL on "
                                "inode (%s). "
                                "If quota is not enabled recently and crawler "
                                "has finished crawling, its an error",
                                uuid_utoa (local->loc.inode->gfid));
                }

                goto out;
        }

        LOCK (&ctx->lock);
        {
                if (buf)
                        ctx->buf = *buf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int32_t
quota_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        STACK_WIND (frame, quota_fstat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fstat, fd,
                    xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fstat, frame, -1, ENOMEM, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fstat, fd,
                         xdata);
        return 0;
}


int32_t
quota_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, const char *path,
                    struct iatt *buf, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *buf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (readlink, frame, op_ret, op_errno, path, buf,
                            xdata);
        return 0;
}


int32_t
quota_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;
        int32_t        ret   = -1;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto unwind;
        }

        STACK_WIND (frame, quota_readlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readlink, loc,
                    size, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (readlink, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->readlink, loc,
                         size, xdata);
        return 0;
}


int32_t
quota_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iovec *vector,
                 int32_t count, struct iatt *buf, struct iobref *iobref,
                 dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *buf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count,
                            buf, iobref, xdata);
        return 0;
}


int32_t
quota_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, uint32_t flags, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        STACK_WIND (frame, quota_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv, fd,
                    size, offset, flags, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (readv, frame, -1, ENOMEM, NULL, -1, NULL, NULL,
                            NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->readv, fd,
                         size, offset, flags, xdata);
        return 0;
}


int32_t
quota_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is NULL on "
                        "inode (%s). "
                        "If quota is not enabled recently and crawler has "
                        "finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *postbuf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}


int32_t
quota_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        local->loc.inode = inode_ref (fd->inode);

        frame->local = local;

        STACK_WIND (frame, quota_fsync_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd,
                    flags, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fsync, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fsync, fd,
                         flags, xdata);
        return 0;
}


int32_t
quota_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                   struct iatt *statpost, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                if (!IA_ISDIR (statpost->ia_type)) {
                        gf_log (this->name, GF_LOG_DEBUG, "quota context is "
                                "NULL on inode (%s). "
                                "If quota is not enabled recently and crawler "
                                "has finished crawling, its an error",
                                uuid_utoa (local->loc.inode->gfid));
                }

                goto out;
        }

        LOCK (&ctx->lock);
        {
                if (statpost)
                        ctx->buf = *statpost;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (setattr, frame, op_ret, op_errno, statpre,
                            statpost, xdata);
        return 0;
}


int32_t
quota_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;
        int32_t        ret   = -1;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto unwind;
        }

        STACK_WIND (frame, quota_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc,
                    stbuf, valid, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (setattr, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD (this)->fops->setattr, loc,
                         stbuf, valid, xdata);
        return 0;
}


int32_t
quota_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                    struct iatt *statpost, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        quota_inode_ctx_get (local->loc.inode, this, &ctx, 0);
        if (ctx == NULL) {
                if (!IA_ISDIR (statpost->ia_type)) {
                        gf_log (this->name, GF_LOG_DEBUG, "quota context is "
                                "NULL on inode (%s). "
                                "If quota is not enabled recently and crawler "
                                "has finished crawling, its an error",
                                uuid_utoa (local->loc.inode->gfid));
                }

                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *statpost;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, statpre,
                            statpost, xdata);
        return 0;
}


int32_t
quota_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        quota_local_t *local = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        STACK_WIND (frame, quota_fsetattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetattr, fd,
                    stbuf, valid, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fsetattr, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD (this)->fops->fsetattr, fd,
                         stbuf, valid, xdata);
        return 0;
}


int32_t
quota_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        int32_t            ret    = -1;
        quota_local_t     *local  = NULL;
        quota_inode_ctx_t *ctx    = NULL;
        quota_dentry_t    *dentry = NULL;

        local = frame->local;
        if (op_ret < 0) {
                goto unwind;
        }

        ret = quota_inode_ctx_get (inode, this, &ctx, 1);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "cannot create quota "
                        "context in inode (gfid:%s)", uuid_utoa (inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *buf;

                dentry = __quota_dentry_new (ctx, (char *)local->loc.name,
                                             local->loc.parent->gfid);
                if (dentry == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot create a new dentry (name:%s) for "
                                "inode(gfid:%s)", local->loc.name,
                                uuid_utoa (local->loc.inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unlock;
                }
        }
unlock:
        UNLOCK (&ctx->lock);

unwind:
        QUOTA_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode,
                            buf, preparent, postparent, xdata);
        return 0;
}


int
quota_mknod_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;
        quota_priv_t  *priv     = NULL;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        priv = this->private;

        if (local->op_ret == -1) {
                op_errno = local->op_errno;
                goto unwind;
        }

        STACK_WIND (frame, quota_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod, loc,
                    mode, rdev, umask, xdata);

        return 0;

unwind:
        QUOTA_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL,
                            NULL, NULL, NULL);
        return 0;
}


int
quota_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dev_t rdev, mode_t umask, dict_t *xdata)
{
        quota_priv_t  *priv     = NULL;
        int32_t        ret      = -1;
        quota_local_t *local    = NULL;
        call_stub_t   *stub     = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        stub = fop_mknod_stub (frame, quota_mknod_helper, loc, mode, rdev,
                               umask, xdata);
        if (stub == NULL) {
                goto err;
        }

        LOCK (&local->lock);
        {
                local->link_count = 1;
                local->stub = stub;
                local->delta = 0;
        }
        UNLOCK (&local->lock);

        quota_check_limit (frame, loc->parent, this, NULL, NULL);
        return 0;

err:
        QUOTA_STACK_UNWIND (mknod, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                            NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->mknod, loc,
                         mode, rdev, umask, xdata);
        return 0;
}

int
quota_setxattr_cbk (call_frame_t *frame, void *cookie,
                    xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;
        int                ret   = 0;

        local = frame->local;
        if (!local)
                goto out;

        ret = quota_inode_ctx_get (local->loc.inode, this, &ctx, 1);
        if ((ret < 0) || (ctx == NULL)) {
                op_errno = ENOMEM;
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->hard_lim = local->limit.hard_lim;
                ctx->soft_lim = local->limit.soft_lim_percent;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
quota_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int flags, dict_t *xdata)
{
        quota_priv_t  *priv     = NULL;
        int            op_errno = EINVAL;
        int            op_ret   = -1;
        int64_t        hard_lim = -1, soft_lim = -1;
        quota_local_t *local    = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        if (frame->root->pid >= 0) {
                GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.quota*", dict,
                                           op_errno, err);
                GF_IF_INTERNAL_XATTR_GOTO ("trusted.pgfid*", dict, op_errno,
                                           err);
        }

        quota_get_limits (this, dict, &hard_lim, &soft_lim);

        if (hard_lim > 0) {
                local = quota_local_new ();
                if (local == NULL) {
                        op_errno = ENOMEM;
                        goto err;
                }

                frame->local = local;
                loc_copy (&local->loc, loc);

                local->limit.hard_lim = hard_lim;
                local->limit.soft_lim_percent = soft_lim;
        }

        STACK_WIND (frame, quota_setxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr, loc,
                    dict, flags, xdata);
        return 0;
err:
        QUOTA_STACK_UNWIND (setxattr, frame, op_ret, op_errno, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->setxattr, loc,
                         dict, flags, xdata);
        return 0;
}

int
quota_fsetxattr_cbk (call_frame_t *frame, void *cookie,
                     xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        quota_inode_ctx_t *ctx   = NULL;
        quota_local_t     *local = NULL;

        local = frame->local;
        if (!local)
                goto out;

        op_ret = quota_inode_ctx_get (local->loc.inode, this, &ctx, 1);
        if ((op_ret < 0) || (ctx == NULL)) {
                op_errno = ENOMEM;
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->hard_lim = local->limit.hard_lim;
                ctx->soft_lim = local->limit.soft_lim_percent;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
quota_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *dict, int flags, dict_t *xdata)
{
        quota_priv_t  *priv     = NULL;
        int32_t        op_ret   = -1;
        int32_t        op_errno = EINVAL;
        quota_local_t *local    = NULL;
        int64_t        hard_lim = -1, soft_lim = -1;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        if (0 <= frame->root->pid) {
                GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.quota*",
                                            dict, op_errno, err);
                GF_IF_INTERNAL_XATTR_GOTO ("trusted.pgfid*", dict,
                                            op_errno, err);
        }

        quota_get_limits (this, dict, &hard_lim, &soft_lim);

        if (hard_lim > 0) {
                local = quota_local_new ();
                frame->local = local;
                local->loc.inode = inode_ref (fd->inode);

                local->limit.hard_lim = hard_lim;
                local->limit.soft_lim_percent = soft_lim;
        }

        STACK_WIND (frame, quota_fsetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetxattr, fd,
                    dict, flags, xdata);
        return 0;
err:
        QUOTA_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fsetxattr, fd,
                         dict, flags, xdata);
        return 0;
}


int
quota_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        QUOTA_STACK_UNWIND (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
quota_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        int32_t         op_errno = EINVAL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        VALIDATE_OR_GOTO (this, err);

        /* all quota xattrs can be cleaned up by doing setxattr on special key.
         * Hence its ok that we don't allow removexattr on quota keys here.
         */
        if (frame->root->pid >= 0) {
                GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.quota*",
                                         name, op_errno, err);
                GF_IF_NATIVE_XATTR_GOTO ("trusted.pgfid*", name,
                                         op_errno, err);
        }

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (loc, err);

        STACK_WIND (frame, quota_removexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;

err:
        QUOTA_STACK_UNWIND (removexattr, frame, -1,  op_errno, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->removexattr,
                         loc, name, xdata);
        return 0;
}


int
quota_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        QUOTA_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
quota_fremovexattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, const char *name, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        int32_t         op_ret   = -1;
        int32_t         op_errno = EINVAL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        if (frame->root->pid >= 0) {
                GF_IF_NATIVE_XATTR_GOTO ("trusted.glusterfs.quota*",
                                         name, op_errno, err);
                GF_IF_NATIVE_XATTR_GOTO ("trusted.pgfid*", name,
                                         op_errno, err);
        }
        STACK_WIND (frame, quota_fremovexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
err:
        QUOTA_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fremovexattr,
                         fd, name, xdata);
        return 0;
}


int32_t
quota_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                  dict_t *xdata)
{
	inode_t           *inode  = NULL;
        uint64_t           value  = 0;
	int64_t            usage  = -1;
	int64_t            avail  = -1;
        int64_t            blocks = 0;
        quota_inode_ctx_t *ctx    = NULL;
        int                ret    = 0;
        gf_boolean_t       dict_created = _gf_false;

        inode = cookie;

        /* This fop will fail mostly in case of client disconnect's,
         * which is already logged. Hence, not logging here */
        if (op_ret == -1)
                goto unwind;
	/*
	 * We should never get here unless quota_statfs (below) sent us a
	 * cookie, and it would only do so if the value was non-NULL.  This
	 * check is therefore just routine defensive coding.
	 */
	if (!inode) {
		gf_log(this->name,GF_LOG_WARNING,
		       "null inode, cannot adjust for quota");
		goto unwind;
	}

        inode_ctx_get (inode, this, &value);
	if (!value) {
		goto unwind;
	}

        /* if limit is set on this inode, report statfs based on this inode
         * else report based on root.
         */
        ctx = (quota_inode_ctx_t *)(unsigned long)value;
        if (ctx->hard_lim <= 0) {
                inode_ctx_get (inode->table->root, this, &value);
                ctx = (quota_inode_ctx_t *)(unsigned long) value;
                if (!ctx || ctx->hard_lim < 0)
                        goto unwind;
        }

        { /* statfs is adjusted in this code block */
                usage = (ctx->size) / buf->f_bsize;

                blocks = ctx->hard_lim / buf->f_bsize;
                buf->f_blocks = blocks;

                avail = buf->f_blocks - usage;
                avail = max (avail, 0);

                buf->f_bfree = avail;
                /*
                 * We have to assume that the total assigned quota
                 * won't cause us to dip into the reserved space,
                 * because dealing with the overcommitted cases is
                 * just too hairy (especially when different bricks
                 * might be using different reserved percentages and
                 * such).
                 */
                buf->f_bavail = buf->f_bfree;
        }

        if (!xdata) {
                xdata = dict_new ();
                if (!xdata)
                        goto unwind;
                dict_created = _gf_true;
        }

        ret = dict_set_int8 (xdata, "quota-deem-statfs", 1);
        if (-1 == ret)
                gf_log (this->name, GF_LOG_ERROR, "Dict set failed, "
                        "deem-statfs option may have no effect");

unwind:
        QUOTA_STACK_UNWIND (statfs, frame, op_ret, op_errno, buf, xdata);

        if (dict_created)
                dict_unref (xdata);
        return 0;
}


int32_t
quota_statfs_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     dict_t *xdata)
{
        quota_local_t   *local          = NULL;
        int              op_errno       = EINVAL;

        GF_VALIDATE_OR_GOTO ("quota", (local = frame->local), err);

        if (-1 == local->op_ret) {
                op_errno = local->op_errno;
                goto err;
        }

        STACK_WIND_COOKIE (frame, quota_statfs_cbk, loc->inode,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->statfs, loc, xdata);
        return 0;
err:
        QUOTA_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
quota_statfs_validate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *buf, dict_t *xdata,
                           struct iatt *postparent)
{
        quota_local_t     *local      = NULL;
        int32_t            ret        = 0;
        quota_inode_ctx_t *ctx        = NULL;
        int64_t           *size       = 0;
        uint64_t           value      = 0;

        local = frame->local;

        if (op_ret < 0)
                goto resume;

        GF_ASSERT (local);
        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO_WITH_ERROR ("quota", this, resume, op_errno,
                                        EINVAL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, xdata, resume, op_errno,
                                        EINVAL);

        ret = inode_ctx_get (local->validate_loc.inode, this, &value);

        ctx = (quota_inode_ctx_t *)(unsigned long)value;
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context is not present in inode (gfid:%s)",
                        uuid_utoa (local->validate_loc.inode->gfid));
                op_errno = EINVAL;
                goto resume;
        }

        ret = dict_get_bin (xdata, QUOTA_SIZE_KEY, (void **) &size);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "size key not present in dict");
                op_errno = EINVAL;
                goto resume;
        }

        LOCK (&ctx->lock);
        {
                ctx->size = ntoh64 (*size);
                gettimeofday (&ctx->tv, NULL);
        }
        UNLOCK (&ctx->lock);

resume:
        quota_link_count_decrement (local);
        return 0;
}

int32_t
quota_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        quota_local_t   *local  = NULL;
        int              op_errno       = 0;
        call_stub_t     *stub           = NULL;
        quota_priv_t *priv  = NULL;
        int           ret       = 0;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

	if (priv->consider_statfs && loc->inode) {
                local = quota_local_new ();
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                frame->local = local;

                stub = fop_statfs_stub (frame, quota_statfs_helper, loc, xdata);
                if (!stub) {
                        op_errno = ENOMEM;
                        goto err;
                }

                LOCK (&local->lock);
                {
                        local->inode = inode_ref (loc->inode);
                        local->link_count = 1;
                        local->stub = stub;
                }
                UNLOCK (&local->lock);

                ret = quota_validate (frame, local->inode, this,
                                      quota_statfs_validate_cbk);
                if (0 > ret) {
                        quota_handle_validate_error (local, -1, -ret);
                }

                return 0;
	}

        /*
         * We have to make sure that we never get to quota_statfs_cbk
         * with a cookie that points to something other than an inode,
         * which is exactly what would happen with STACK_UNWIND using
         * that as a callback.  Therefore, use default_statfs_cbk in
         * this case instead.
         *
         * Also if the option deem-statfs is not set to "on" don't
         * bother calculating quota limit on / in statfs_cbk.
         */
        if (priv->consider_statfs)
                gf_log (this->name,GF_LOG_WARNING,
                        "missing inode, cannot adjust for quota");

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->statfs, loc, xdata);
        return 0;

err:
        STACK_UNWIND_STRICT (statfs, frame, -1, op_errno, NULL, NULL);

        if (local)
                quota_local_cleanup (this, local);
        return 0;
}

int
quota_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, gf_dirent_t *entries,
                    dict_t *xdata)
{
        gf_dirent_t   *entry = NULL;
        quota_local_t *local = NULL;
        loc_t          loc   = {0, };

        if (op_ret <= 0)
                goto unwind;

        local = frame->local;

        list_for_each_entry (entry, &entries->list, list) {
                if ((strcmp (entry->d_name, ".") == 0)
                    || (strcmp (entry->d_name, "..") == 0))
                        continue;

                uuid_copy (loc.gfid, entry->d_stat.ia_gfid);
                loc.inode = inode_ref (entry->inode);
                loc.parent = inode_ref (local->loc.inode);
                uuid_copy (loc.pargfid, loc.parent->gfid);
                loc.name = entry->d_name;

                quota_fill_inodectx (this, entry->inode, entry->dict,
                                     &loc, &entry->d_stat, &op_errno);

                loc_wipe (&loc);
        }

unwind:
        QUOTA_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries, xdata);

        return 0;
}

int
quota_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset, dict_t *dict)
{
        quota_priv_t  *priv     = NULL;
        int            ret      = 0;
        gf_boolean_t   new_dict = _gf_false;
        quota_local_t *local    = NULL;

        priv = this->private;

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        local = quota_local_new ();

        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        if (dict == NULL) {
                dict = dict_new ();
                new_dict = _gf_true;
        }

        if (dict) {
                ret = dict_set_int8 (dict, QUOTA_LIMIT_KEY, 1);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dict set of key for hard-limit failed");
                        goto err;
                }
        }

        STACK_WIND (frame, quota_readdirp_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdirp, fd,
                    size, offset, dict);

        if (new_dict) {
                dict_unref (dict);
        }

        return 0;
err:
        STACK_UNWIND_STRICT (readdirp, frame, -1, EINVAL, NULL, NULL);

        if (new_dict) {
                dict_unref (dict);
        }

        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->readdirp, fd,
                         size, offset, dict);
        return 0;
}

int32_t
quota_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
        int32_t                  ret            = 0;
        uint64_t                 ctx_int        = 0;
        quota_inode_ctx_t       *ctx            = NULL;
        quota_local_t           *local          = NULL;

        local = frame->local;

        if ((op_ret < 0) || (local == NULL)) {
                goto out;
        }

        ret = inode_ctx_get (local->loc.inode, this, &ctx_int);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to get the context", local->loc.path);
                goto out;
        }

        ctx = (quota_inode_ctx_t *)(unsigned long) ctx_int;

        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in %s (gfid:%s)",
                        local->loc.path, uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *postbuf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (fallocate, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);

        return 0;
}


int32_t
quota_fallocate_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                        int32_t mode, off_t offset, size_t len, dict_t *xdata)
{
        quota_local_t *local    = NULL;
        int32_t        op_errno = EINVAL;
        quota_priv_t  *priv     = NULL;

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto unwind;
        }

        priv = this->private;

        if (local->op_ret == -1) {
                op_errno = local->op_errno;
                goto unwind;
        }

        STACK_WIND (frame, quota_fallocate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fallocate, fd, mode, offset, len,
		    xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
quota_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
		off_t offset, size_t len, dict_t *xdata)
{
        int32_t            ret     = -1, op_errno = EINVAL;
        int32_t            parents = 0;
        quota_local_t     *local   = NULL;
        quota_inode_ctx_t *ctx     = NULL;
        quota_priv_t      *priv    = NULL;
        quota_dentry_t    *dentry  = NULL;
        call_stub_t       *stub    = NULL;

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, unwind);

        WIND_IF_QUOTAOFF (priv->is_quota_on, off);

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("quota", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;
        local->loc.inode = inode_ref (fd->inode);

        ret = quota_inode_ctx_get (fd->inode, this, &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG, "quota context is "
                        "NULL on inode (%s). "
                        "If quota is not enabled recently and crawler "
                        "has finished crawling, its an error",
                        uuid_utoa (local->loc.inode->gfid));
        }

        stub = fop_fallocate_stub(frame, quota_fallocate_helper, fd, mode,
                                  offset, len, xdata);
        if (stub == NULL) {
                op_errno = ENOMEM;
                goto unwind;
        }

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, unwind);

        if (ctx != NULL) {
                LOCK (&ctx->lock);
                {
                        list_for_each_entry (dentry, &ctx->parents, next) {
                                parents++;
                        }
                }
                UNLOCK (&ctx->lock);
        }

	/*
	 * Note that by using len as the delta we're assuming the range from
	 * offset to offset+len has not already been allocated. This can result
	 * in ENOSPC errors attempting to allocate an already allocated range.
	 */
        local->delta = len;
        local->stub = stub;
        local->link_count = parents;

        if (parents == 0) {
                local->link_count = 1;
                quota_check_limit (frame, fd->inode, this, NULL, NULL);
        } else {
                list_for_each_entry (dentry, &ctx->parents, next) {
                        quota_check_limit (frame, fd->inode, this, dentry->name,
                                           dentry->par);
                }
        }

        return 0;

unwind:
        QUOTA_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;

off:
        STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fallocate, fd, mode, offset,
                         len, xdata);
        return 0;
}

/* Logs if
*  i.   Usage crossed soft limit
*  ii.  Usage above soft limit and alert-time elapsed
*/
void
quota_log_usage (xlator_t *this, quota_inode_ctx_t *ctx, inode_t *inode,
                 int64_t delta)
{
        struct timeval           cur_time       = {0,};
        char                    *usage_str      = NULL;
        char                    size_str[32]    = {0};
        char                    *path           = NULL;
        int64_t                  cur_size       = 0;
        quota_priv_t            *priv           = NULL;
        gf_boolean_t            dyn_mem         = _gf_true;

        priv = this->private;
        if ((ctx->soft_lim <= 0) || (timerisset (&ctx->prev_log) &&
                                     !quota_timeout (&ctx->prev_log,
                                                     priv->log_timeout))) {
                return;
        }


        cur_size = ctx->size + delta;
        usage_str = gf_uint64_2human_readable (cur_size);
        if (!usage_str) {
                snprintf (size_str, sizeof (size_str), "%"PRId64, cur_size);
                usage_str = (char*) size_str;
                dyn_mem = _gf_false;
        }
        inode_path (inode, NULL, &path);
        if (!path)
                path = uuid_utoa (inode->gfid);

        gettimeofday (&cur_time, NULL);
        /* Usage crossed/reached soft limit */
        if (DID_REACH_LIMIT (ctx->soft_lim, ctx->size, cur_size)) {

                gf_log (this->name, GF_LOG_ALERT, "Usage crossed "
                        "soft limit: %s used by %s", usage_str, path);
                ctx->prev_log = cur_time;
        }
        /* Usage is above soft limit */
        else if (cur_size > ctx->soft_lim){
                gf_log (this->name, GF_LOG_ALERT, "Usage is above "
                        "soft limit: %s used by %s", usage_str, path);
                ctx->prev_log = cur_time;
        }

        if (dyn_mem)
                GF_FREE (usage_str);
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_quota_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        "init failed");
                return ret;
        }

        return ret;
}


int32_t
quota_forget (xlator_t *this, inode_t *inode)
{
        int32_t               ret     = 0;
        uint64_t              ctx_int = 0;
        quota_inode_ctx_t    *ctx     = NULL;
        quota_dentry_t       *dentry  = NULL, *tmp;

        ret = inode_ctx_del (inode, this, &ctx_int);

        if (ret < 0) {
                return 0;
        }

        ctx = (quota_inode_ctx_t *) (long)ctx_int;

        LOCK (&ctx->lock);
        {
                list_for_each_entry_safe (dentry, tmp, &ctx->parents, next) {
                        __quota_dentry_free (dentry);
                }
        }
        UNLOCK (&ctx->lock);

        LOCK_DESTROY (&ctx->lock);

        GF_FREE (ctx);

        return 0;
}

int32_t
init (xlator_t *this)
{
        int32_t       ret       = -1;
        quota_priv_t *priv      = NULL;

        if ((this->children == NULL)
            || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: quota (%s) not configured with "
                        "exactly one child", this->name);
                return -1;
        }

        if (this->parents == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile");
        }

        QUOTA_ALLOC_OR_GOTO (priv, quota_priv_t, err);

        LOCK_INIT (&priv->lock);

        this->private = priv;

        GF_OPTION_INIT ("deem-statfs", priv->consider_statfs, bool, err);
        GF_OPTION_INIT ("server-quota", priv->is_quota_on, bool, err);
        GF_OPTION_INIT ("default-soft-limit", priv->default_soft_lim, percent,
                        err);
        GF_OPTION_INIT ("soft-timeout", priv->soft_timeout, time, err);
        GF_OPTION_INIT ("hard-timeout", priv->hard_timeout, time, err);
        GF_OPTION_INIT ("alert-time", priv->log_timeout, time, err);
        GF_OPTION_INIT ("volume-uuid", priv->volume_uuid, str, err);

        this->local_pool = mem_pool_new (quota_local_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto err;
        }

        if (priv->is_quota_on) {
                priv->rpc_clnt = quota_enforcer_init (this, this->options);
                if (priv->rpc_clnt == NULL) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota enforcer rpc init failed");
                        goto err;
                }
        }

        ret = 0;
err:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t       ret      = -1;
        quota_priv_t *priv     = NULL;
        gf_boolean_t  quota_on = _gf_false;

        priv = this->private;

        GF_OPTION_RECONF ("deem-statfs", priv->consider_statfs, options, bool,
                          out);
        GF_OPTION_RECONF ("server-quota", quota_on, options, bool,
                          out);
        GF_OPTION_RECONF ("default-soft-limit", priv->default_soft_lim,
                          options, percent, out);
        GF_OPTION_RECONF ("alert-time", priv->log_timeout, options,
                          time, out);
        GF_OPTION_RECONF ("soft-timeout", priv->soft_timeout, options,
                          time, out);
        GF_OPTION_RECONF ("hard-timeout", priv->hard_timeout, options,
                          time, out);

        if (quota_on) {
                priv->rpc_clnt = quota_enforcer_init (this,
                                                      this->options);
                if (priv->rpc_clnt == NULL) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota enforcer rpc init failed");
                        goto out;
                }

        } else {
                if (priv->rpc_clnt) {
                        // Quotad is shutdown when there is no started volume
                        // which has quota enabled. So, we should disable the
                        // enforcer client when quota is disabled on a volume,
                        // to avoid spurious reconnect attempts to a service
                        // (quotad), that is known to be down.
                        rpc_clnt_disable (priv->rpc_clnt);
                }
        }

        priv->is_quota_on = quota_on;

        ret = 0;
out:
        return ret;
}

int32_t
quota_priv_dump (xlator_t *this)
{
        quota_priv_t *priv = NULL;
        int32_t       ret  = -1;


        GF_ASSERT (this);

        priv = this->private;

        gf_proc_dump_add_section ("xlators.features.quota.priv", this->name);

        ret = TRY_LOCK (&priv->lock);
        if (ret)
             goto out;
        else {
                gf_proc_dump_write("soft-timeout", "%d", priv->soft_timeout);
                gf_proc_dump_write("hard-timeout", "%d", priv->hard_timeout);
                gf_proc_dump_write("alert-time", "%d", priv->log_timeout);
                gf_proc_dump_write("quota-on", "%d", priv->is_quota_on);
                gf_proc_dump_write("statfs", "%d", priv->consider_statfs);
                gf_proc_dump_write("volume-uuid", "%s", priv->volume_uuid);
                gf_proc_dump_write("validation-count", "%ld",
                                    priv->validation_count);
        }
        UNLOCK (&priv->lock);

out:
        return 0;
}

void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
        .statfs       = quota_statfs,
        .lookup       = quota_lookup,
        .writev       = quota_writev,
        .create       = quota_create,
        .mkdir        = quota_mkdir,
        .truncate     = quota_truncate,
        .ftruncate    = quota_ftruncate,
        .unlink       = quota_unlink,
        .symlink      = quota_symlink,
        .link         = quota_link,
        .rename       = quota_rename,
        .getxattr     = quota_getxattr,
        .fgetxattr    = quota_fgetxattr,
        .stat         = quota_stat,
        .fstat        = quota_fstat,
        .readlink     = quota_readlink,
        .readv        = quota_readv,
        .fsync        = quota_fsync,
        .setattr      = quota_setattr,
        .fsetattr     = quota_fsetattr,
        .mknod        = quota_mknod,
        .setxattr     = quota_setxattr,
        .fsetxattr    = quota_fsetxattr,
        .removexattr  = quota_removexattr,
        .fremovexattr = quota_fremovexattr,
        .readdirp     = quota_readdirp,
	.fallocate    = quota_fallocate,
};

struct xlator_cbks cbks = {
        .forget = quota_forget
};

struct xlator_dumpops dumpops = {
        .priv    = quota_priv_dump,
};
struct volume_options options[] = {
        {.key = {"limit-set"}},
        {.key = {"deem-statfs"},
         .type = GF_OPTION_TYPE_BOOL,
         .default_value = "off",
         .description = "If set to on, it takes quota limits into"
                        "consideration while estimating fs size. (df command)"
                        " (Default is off)."
        },
        {.key = {"server-quota"},
         .type = GF_OPTION_TYPE_BOOL,
         .default_value = "off",
         .description = "Skip the quota enforcement if the feature is"
                        " not turned on. This is not a user exposed option."
        },
        {.key = {"default-soft-limit"},
         .type = GF_OPTION_TYPE_PERCENT,
         .default_value = "80%",
        },
        {.key = {"soft-timeout"},
         .type = GF_OPTION_TYPE_TIME,
         .min = 0,
         .max = 1800,
         .default_value = "60",
         .description = "quota caches the directory sizes on client. "
                        "soft-timeout indicates the timeout for the validity of"
                        " cache before soft-limit has been crossed."
        },
        {.key = {"hard-timeout"},
         .type = GF_OPTION_TYPE_TIME,
         .min = 0,
         .max = 60,
         .default_value = "5",
         .description = "quota caches the directory sizes on client. "
                        "hard-timeout indicates the timeout for the validity of"
                        " cache after soft-limit has been crossed."
        },
        { .key   = {"username"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"password"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"transport-type"},
          .value = {"tcp", "socket", "ib-verbs", "unix", "ib-sdp",
                    "tcp/client", "ib-verbs/client", "rdma"},
          .type  = GF_OPTION_TYPE_STR,
        },
        { .key   = {"remote-host"},
          .type  = GF_OPTION_TYPE_INTERNET_ADDRESS,
        },
        { .key   = {"remote-port"},
          .type  = GF_OPTION_TYPE_INT,
        },
        { .key  = {"volume-uuid"},
          .type = GF_OPTION_TYPE_STR,
          .description = "uuid of the volume this brick is part of."
        },
        { .key  = {"alert-time"},
          .type = GF_OPTION_TYPE_TIME,
          .min = 0,
          .max = 7*86400,
          .default_value = "86400",
        },
        {.key = {NULL}}
};
