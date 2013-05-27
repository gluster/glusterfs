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

// occassionally log
static int8_t  occ_log = 0;

int32_t
quota_check_limit (call_frame_t *frame, inode_t *inode, xlator_t *this,
                   char *name, uuid_t par);
struct volume_options options[];


static int32_t
__quota_init_inode_ctx (inode_t *inode, int64_t hard_lim, int64_t soft_lim,
                        xlator_t *this, dict_t *dict, struct iatt *buf,
                        quota_inode_ctx_t **context)
{
        int32_t            ret  = -1;
        int64_t           *size = 0;
        quota_inode_ctx_t *ctx  = NULL;

        if (inode == NULL) {
                goto out;
        }

        QUOTA_ALLOC_OR_GOTO (ctx, quota_inode_ctx_t, out);

        ctx->hard_lim = hard_lim;
        ctx->soft_lim = soft_lim;

        if (buf)
                ctx->buf = *buf;

        LOCK_INIT(&ctx->lock);

        if (context != NULL) {
                *context = ctx;
        }

        INIT_LIST_HEAD (&ctx->parents);

        if (dict != NULL) {
                ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
                if (ret == 0) {
                        ctx->size = ntoh64 (*size);
                        gettimeofday (&ctx->tv, NULL);
                }
        }

        ret = __inode_ctx_put (inode, this, (uint64_t )(long)ctx);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot set quota context in inode (gfid:%s)",
                        uuid_utoa (inode->gfid));
        }
out:
        return ret;
}

int
quota_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int ret = -1;

        if (!loc) {
                return ret;
        }

        if (inode) {
                loc->inode = inode_ref (inode);
        }

        if (parent) {
                loc->parent = inode_ref (parent);
        }

        loc->path = gf_strdup (path);
        if (!loc->path) {
                goto loc_wipe;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name) {
                loc->name++;
        } else {
                goto loc_wipe;
        }

        ret = 0;

loc_wipe:
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
                goto err;
        }

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cannot construct path for inode (gfid:%s)",
                        uuid_utoa (inode->gfid));
                goto err;
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
        if (local)
                LOCK_INIT (&local->lock);
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
                goto err;
        }

        uuid_copy (dentry->par, par);

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

void
quota_log_usage (xlator_t *this, inode_t *inode,
                         quota_inode_ctx_t *ctx, int64_t delta)
{
        int64_t         size            = ctx->size + delta;
        int64_t         hard_lim        = ctx->hard_lim;
        char           *path            = NULL;

        inode_path (inode, NULL, &path);
        if (size > ctx->soft_lim) {
                GF_LOG_OCCASIONALLY (occ_log, this->name, GF_LOG_WARNING,
                                     "Usage approached soft limit ((%ld)) for "
                                     "%s", ctx->soft_lim, path);

                if (((size > 0.8 * hard_lim) && (ctx->size < 0.8 * hard_lim)) ||
                    ((size > 0.9 * hard_lim) && (ctx->size < 0.9 * hard_lim)) ||
                    ((size > 0.95 * hard_lim)&& (ctx->size < 0.95 * hard_lim))) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Usage approached %ld%% of limit %ld for %s",
                                (size/ctx->hard_lim)*100, ctx->hard_lim, path);
                }
        }
        if (size > hard_lim)
                GF_LOG_OCCASIONALLY (occ_log, this->name, GF_LOG_WARNING,
                                     "Usage crossed the limit %ld for %s",
                                     ctx->hard_lim, path);
}

int32_t
quota_check_limit (call_frame_t *frame, inode_t *inode, xlator_t *this,
                   char *name, uuid_t par)
{
        inode_t              *_inode         = NULL, *parent = NULL;
        quota_inode_ctx_t    *ctx            = NULL;
        quota_local_t        *local          = NULL;
        char                  need_unwind = 0;
        int64_t               delta          = 0;
        uint64_t              value          = 0;
        uuid_t                trav_uuid      = {0,};

        GF_VALIDATE_OR_GOTO ("quota", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        delta = local->delta;

        inode_ctx_get (inode, this, &value);
        ctx = (quota_inode_ctx_t *)(unsigned long)value;

        _inode = inode_ref (inode);


        if ( par != NULL ) {
                uuid_copy (trav_uuid, par);
        }

        do {
                if (ctx != NULL) {
                        LOCK (&ctx->lock);
                        {
                                if (ctx->hard_lim >= 0) {
                                        if ((ctx->agg_size + delta)
                                                   >= ctx->hard_lim) {
                                                local->op_ret = -1;
                                                local->op_errno = EDQUOT;
                                                need_unwind = 1;
                                        }
                                        quota_log_usage (this, _inode, ctx,
                                                         delta);
                                }
                        }
                        UNLOCK (&ctx->lock);

                        if (need_unwind) {
                                break;
                        }
                }

                if (__is_root_gfid (_inode->gfid)) {
                        break;
                }

                parent = inode_parent (_inode, trav_uuid, name);

                if (name != NULL) {
                        name = NULL;
                        uuid_clear (trav_uuid);
                }

                if (parent == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "cannot find parent for inode (gfid:%s), hence "
                                "aborting enforcing quota-limits and continuing"
                                " with the fop", uuid_utoa (_inode->gfid));
                }

                inode_unref (_inode);
                _inode = parent;

                if (_inode == NULL) {
                        break;
                }

                value = 0;
                inode_ctx_get (_inode, this, &value);
                ctx = (quota_inode_ctx_t *)(unsigned long)value;
        } while (1);

        inode_unref (_inode);

out:
        return local->op_ret;
}


int32_t
quota_get_limit_value (inode_t *inode, xlator_t *this, int64_t *n)
{
        int32_t       ret        = 0;
        char         *path       = NULL;
        limits_t     *limit_node = NULL;
        quota_priv_t *priv       = NULL;

        if (inode == NULL || n == NULL) {
                ret = -1;
                goto out;
        }

        *n = 0;

        ret = inode_path (inode, NULL, &path);
        if (ret < 0) {
                ret = -1;
                goto out;
        }

        priv = this->private;

        list_for_each_entry (limit_node, &priv->limit_head, limit_list) {
                if (strcmp (limit_node->path, path) == 0) {
                        *n = limit_node->value;
                        break;
                }
        }

out:
        GF_FREE (path);

        return ret;
}


static int32_t
quota_inode_ctx_get (inode_t *inode, int64_t hard_lim, int64_t soft_lim,
                     xlator_t *this, dict_t *dict, struct iatt *buf,
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
                        ret = __quota_init_inode_ctx (inode, hard_lim, soft_lim,
                                                      this, dict, buf, ctx);
                }
        }
        UNLOCK (&inode->lock);

        return ret;
}


int32_t
quota_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int32_t            ret        = -1;
        char               found      = 0;
        quota_local_t     *local      = NULL;
        quota_inode_ctx_t *ctx        = NULL;
        quota_dentry_t    *dentry     = NULL;
        int64_t           *size       = 0;
        uint64_t           value      = 0;
        limits_t          *limit_node = NULL;
        quota_priv_t      *priv       = NULL;

        local = frame->local;

        priv = this->private;

        inode_ctx_get (inode, this, &value);
        ctx = (quota_inode_ctx_t *)(unsigned long)value;

        if ((op_ret < 0) || (local == NULL)
            || (((ctx == NULL) || (ctx->hard_lim == local->hard_lim))
                && (local->hard_lim < 0) && !((IA_ISREG (buf->ia_type))
                                           || (IA_ISLNK (buf->ia_type))))) {
                goto unwind;
        }

        LOCK (&priv->lock);
        {
                list_for_each_entry (limit_node, &priv->limit_head,
                                     limit_list) {
                        if (strcmp (local->loc.path, limit_node->path) == 0) {
                                uuid_copy (limit_node->gfid, buf->ia_gfid);
                                break;
                        }
                }
        }
        UNLOCK (&priv->lock);

        ret = quota_inode_ctx_get (local->loc.inode, local->hard_lim,
                                   local->soft_lim, this, dict, buf, &ctx, 1);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "cannot create quota "
                        "context in inode(gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        LOCK (&ctx->lock);
        {

                if (dict != NULL) {
                        ret = dict_get_bin (dict, QUOTA_SIZE_KEY,
                                            (void **) &size);
                        if (ret == 0) {
                                ctx->size = ntoh64 (*size);
                                gettimeofday (&ctx->tv, NULL);
                        }
                }

                ctx->hard_lim = local->hard_lim;
                ctx->soft_lim = local->soft_lim;

                ctx->buf = *buf;

                if (!(IA_ISREG (buf->ia_type) || IA_ISLNK (buf->ia_type))) {
                        goto unlock;
                }

                if (local->loc.name == NULL)
                        goto unlock;

                list_for_each_entry (dentry, &ctx->parents, next) {
                        if ((strcmp (dentry->name, local->loc.name) == 0) &&
                            (uuid_compare (local->loc.parent->gfid,
                                           dentry->par) == 0)) {
                                found = 1;
                                break;
                        }
                }

                if (!found) {
                        dentry = __quota_dentry_new (ctx,
                                                     (char *)local->loc.name,
                                                     local->loc.parent->gfid);
                        if (dentry == NULL) {
                                /*
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot create a new dentry (par:%"
                                        PRId64", name:%s) for inode(ino:%"
                                        PRId64", gfid:%s)",
                                        uuid_utoa (local->loc.inode->gfid));
                                */
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto unlock;
                        }
                }
        }
unlock:
        UNLOCK (&ctx->lock);

unwind:
        QUOTA_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                            dict, postparent);
        return 0;
}


int32_t
quota_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xattr_req)
{
        quota_priv_t       *priv        = NULL;
        fop_lookup_cbk_t    cbk_fn      = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_lookup_cbk;
                goto wind;
        }

        int32_t             ret         = -1;
        limits_t           *limit_node  = NULL;
        gf_boolean_t        dict_newed  = _gf_false;
        quota_local_t      *local       = NULL;
        int64_t             hard_lim    = -1;
        int64_t             soft_lim    = -1;

        list_for_each_entry (limit_node, &priv->limit_head, limit_list) {
                if (strcmp (limit_node->path, loc->path) == 0) {
                        hard_lim = limit_node->hard_lim;
                        soft_lim = limit_node->soft_lim;
                }
        }

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                goto err;
        }

        frame->local = local;

        local->hard_lim = hard_lim;
        local->soft_lim = soft_lim;

        if (hard_lim < 0) {
                goto wind;
        }

        if (xattr_req == NULL) {
                xattr_req  = dict_new ();
                dict_newed = _gf_true;
        }

        ret = dict_set_uint64 (xattr_req, QUOTA_SIZE_KEY, 0);
        if (ret < 0) {
                goto err;
        }

        cbk_fn = quota_lookup_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

        ret = 0;

err:
        if (ret < 0) {
                QUOTA_STACK_UNWIND (lookup, frame, -1, ENOMEM,
                                    NULL, NULL, NULL, NULL);
        }

        if (dict_newed == _gf_true) {
                dict_unref (xattr_req);
        }

        return 0;
}


void
quota_update_size (xlator_t *this, inode_t *inode, char *name, uuid_t par,
                   int64_t delta)
{
        inode_t           *_inode    = NULL;
        inode_t           *parent    = NULL;
        uint64_t           value     = 0;
        quota_inode_ctx_t *ctx       = NULL;
        uuid_t             trav_uuid = {0,};

        GF_VALIDATE_OR_GOTO ("quota", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        inode_ctx_get (inode, this, &value);
        ctx = (quota_inode_ctx_t *)(unsigned long)value;

        _inode = inode_ref (inode);

        if ( par != NULL ) {
                uuid_copy (trav_uuid, par);
        }

        do {
                if ((ctx != NULL) && (ctx->hard_lim >= 0)) {
                        LOCK (&ctx->lock);
                        {
                                ctx->size += delta;
                        }
                        UNLOCK (&ctx->lock);
                }

                if (__is_root_gfid (_inode->gfid)) {
                        break;
                }

                parent = inode_parent (_inode, trav_uuid, name);
                if (parent == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "cannot find parent for inode (gfid:%s), hence "
                                "aborting size updation of parents",
                                uuid_utoa (_inode->gfid));
                }

                if (name != NULL) {
                        name = NULL;
                        uuid_clear (trav_uuid);
                }

                inode_unref (_inode);
                _inode = parent;

                if (_inode == NULL) {
                        break;
                }

                inode_ctx_get (_inode, this, &value);
                ctx = (quota_inode_ctx_t *)(unsigned long)value;
        } while (1);

out:
        return;
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
        quota_dentry_t          *dentry         = NULL;
        int64_t                  delta          = 0;

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

        list_for_each_entry (dentry, &ctx->parents, next) {
                delta = (postbuf->ia_blocks - prebuf->ia_blocks) * 512;
                quota_update_size (this, local->loc.inode, dentry->name,
                                   dentry->par, delta);
        }

out:
        QUOTA_STACK_UNWIND (writev, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);

        return 0;
}


int32_t
quota_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t off,
              uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        quota_priv_t       *priv        = NULL;
        fop_writev_cbk_t    cbk_fn      = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_writev_cbk;
                goto wind;
        }

        int32_t            ret     = -1, op_errno = EINVAL;
        int32_t            parents = 0;
        uint64_t           size    = 0;
        quota_local_t     *local   = NULL;
        quota_inode_ctx_t *ctx     = NULL;
        quota_dentry_t    *dentry  = NULL;

        GF_ASSERT (frame);
        GF_VALIDATE_OR_GOTO ("quota", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;
        local->loc.inode = inode_ref (fd->inode);

        ret = quota_inode_ctx_get (fd->inode, -1, -1, this, NULL, NULL, &ctx,
                                   0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (fd->inode->gfid));
                goto unwind;
        }

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, unwind);

        size = iov_length (vector, count);
        LOCK (&ctx->lock);
        {
                list_for_each_entry (dentry, &ctx->parents, next) {
                        parents++;
                }
        }
        UNLOCK (&ctx->lock);

        local->delta = size;
        local->link_count = parents;

        list_for_each_entry (dentry, &ctx->parents, next) {
                ret = quota_check_limit (frame, fd->inode, this, dentry->name,
                                         dentry->par);
                if (ret == -1) {
                        op_errno = EDQUOT;
                        break;
                }
        }

        cbk_fn = quota_writev_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, off,
                    flags, iobref, xdata);

        return 0;

unwind:
        QUOTA_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
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
quota_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
        quota_priv_t    *priv           = NULL;
        fop_mkdir_cbk_t  cbk_fn         = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_mkdir_cbk;
                goto wind;
        }

        int32_t        ret            = 0, op_errno = 0;
        quota_local_t *local          = NULL;

        local = quota_local_new ();
        if (local == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        frame->local = local;

        local->link_count = 1;

        ret = loc_copy (&local->loc, loc);
        if (ret) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING, "loc_copy failed");
                goto err;
        }

        local->delta = 0;

        ret = quota_check_limit (frame, loc->parent, this, NULL, NULL);
        if (ret == -1) {
                op_errno = EDQUOT;
                goto err;
        }

        cbk_fn = quota_mkdir_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);

        return 0;
err:
        QUOTA_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL);

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

        ret = quota_inode_ctx_get (inode, -1, -1, this, NULL, buf, &ctx, 1);
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
quota_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_create_cbk_t         cbk_fn         = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_create_cbk;
                goto wind;
        }

        int32_t            ret            = -1;
        quota_local_t     *local          = NULL;
        int32_t            op_errno       = 0;

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

        local->delta = 0;

        ret = quota_check_limit (frame, loc->parent, this, NULL, NULL);
        if (ret == -1) {
                op_errno = EDQUOT;
                goto err;
        }

        cbk_fn = quota_create_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->create, loc, flags, mode, umask,
                    fd, xdata);

        return 0;
err:
        QUOTA_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL, NULL);

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

        quota_update_size (this, local->loc.inode, (char *)local->loc.name,
                           local->loc.parent->gfid,
                           (-(ctx->buf.ia_blocks * 512)));

out:
        QUOTA_STACK_UNWIND (unlink, frame, op_ret, op_errno, preparent,
                            postparent, xdata);
        return 0;
}


int32_t
quota_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_unlink_cbk_t         cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_unlink_cbk;
                goto wind;
        }

        int32_t        ret = 0;
        quota_local_t *local = NULL;

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

        cbk_fn = quota_unlink_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);

        ret = 0;

err:
        if (ret == -1) {
                QUOTA_STACK_UNWIND (unlink, frame, -1, 0, NULL, NULL, NULL);
        }

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

        quota_update_size (this, local->loc.parent, NULL, NULL,
                           (buf->ia_blocks * 512));

        ret = quota_inode_ctx_get (inode, -1, -1, this, NULL, NULL, &ctx, 0);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "cannot find quota "
                        "context in %s (gfid:%s)", local->loc.path,
                        uuid_utoa (inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
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
quota_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        quota_priv_t    *priv           = NULL;
        fop_link_cbk_t   cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_link_cbk;
                goto wind;
        }

        int32_t            ret   = -1, op_errno = ENOMEM;
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx = NULL;

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

        ret = quota_inode_ctx_get (oldloc->inode, -1, -1, this, NULL, NULL,
                                   &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
                        oldloc->inode ? uuid_utoa (oldloc->inode->gfid) : "0");
                op_errno = EINVAL;
                goto err;
        }

        local->delta = ctx->buf.ia_blocks * 512;

        ret = quota_check_limit (frame, newloc->parent, this, NULL, NULL);
        if (ret == -1) {
                op_errno = EDQUOT;
                goto err;
        }

        cbk_fn = quota_link_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc, xdata);

        ret = 0;
err:
        if (ret < 0) {
                QUOTA_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL,
                                    NULL, NULL, NULL);
        }

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

        if (IA_ISREG (local->oldloc.inode->ia_type)
            || IA_ISLNK (local->oldloc.inode->ia_type)) {
                size = buf->ia_blocks * 512;
        }

        if (local->oldloc.parent != local->newloc.parent) {
                quota_update_size (this, local->oldloc.parent, NULL, NULL,
                                   (-size));
                quota_update_size (this, local->newloc.parent, NULL, NULL,
                                   size);
        }

        if (!(IA_ISREG (local->oldloc.inode->ia_type)
              || IA_ISLNK (local->oldloc.inode->ia_type))) {
                goto out;
        }

        ret = quota_inode_ctx_get (local->oldloc.inode, -1, -1, this, NULL,
                                   NULL, &ctx, 0);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "quota context not"
                        "set in inode(gfid:%s)",
                        uuid_utoa (local->oldloc.inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
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
                                        "for inode(gfid:%s)", local->newloc.name,
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
quota_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
              loc_t *newloc, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_rename_cbk_t         cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_rename_cbk;
                goto wind;
        }

        int32_t            ret            = -1, op_errno = ENOMEM;
        quota_local_t     *local          = NULL;
        quota_inode_ctx_t *ctx            = NULL;

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

        if (IA_ISREG (oldloc->inode->ia_type)
            || IA_ISLNK (oldloc->inode->ia_type)) {
                ret = quota_inode_ctx_get (oldloc->inode, -1, -1, this, NULL,
                                           NULL, &ctx, 0);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota context not set in inode (gfid:%s)",
                                oldloc->inode ? uuid_utoa (oldloc->inode->gfid)
                                : "0");
                        op_errno = EINVAL;
                        goto err;
                }
                local->delta = ctx->buf.ia_blocks * 512;
        } else {
                local->delta = 0;
        }

        ret = quota_check_limit (frame, newloc->parent, this, NULL, NULL);
        if (ret == -1) {
                op_errno = EDQUOT;
                goto err;
        }

        cbk_fn = quota_rename_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);

        ret = 0;
err:
        if (ret == -1) {
                QUOTA_STACK_UNWIND (rename, frame, -1, op_errno, NULL,
                                    NULL, NULL, NULL, NULL, NULL);
        }

        return 0;
}


int32_t
quota_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        int64_t            size   = 0;
        quota_local_t     *local  = NULL;
        quota_inode_ctx_t *ctx    = NULL;
        quota_dentry_t    *dentry = NULL;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        size = buf->ia_blocks * 512;

        quota_update_size (this, local->loc.parent, NULL, NULL, size);

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 1);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
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
quota_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc, mode_t umask, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_symlink_cbk_t        cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_symlink_cbk;
                goto wind;
        }

        int32_t          ret      = -1;
        int32_t          op_errno = ENOMEM;
        quota_local_t   *local    = NULL;

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

        local->delta = strlen (linkpath);

        ret = quota_check_limit (frame, loc->parent, this, NULL, NULL);
        if (ret == -1) {
                op_errno = EDQUOT;
                goto err;
        }

        cbk_fn = quota_symlink_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkpath, loc, umask,
                    xdata);
        return 0;

err:
        QUOTA_STACK_UNWIND (symlink, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL);

        return 0;
}


int32_t
quota_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;
        int64_t            delta = 0;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        delta = (postbuf->ia_blocks - prebuf->ia_blocks) * 512;

        quota_update_size (this, local->loc.inode, NULL, NULL, delta);

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
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
        quota_priv_t            *priv           = NULL;
        fop_truncate_cbk_t       cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_truncate_cbk;
                goto wind;
        }

        int32_t          ret   = -1;
        quota_local_t   *local = NULL;

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

        cbk_fn = quota_truncate_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);

        return 0;
err:
        QUOTA_STACK_UNWIND (truncate, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
quota_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        quota_local_t     *local = NULL;
        quota_inode_ctx_t *ctx   = NULL;
        int64_t            delta = 0;

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        if (local == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "local is NULL");
                goto out;
        }

        delta = (postbuf->ia_blocks - prebuf->ia_blocks) * 512;

        quota_update_size (this, local->loc.inode, NULL, NULL, delta);

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
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
        quota_priv_t            *priv           = NULL;
        fop_ftruncate_cbk_t      cbk_fn         = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_ftruncate_cbk;
                goto wind;
        }

        quota_local_t   *local = NULL;

        local = quota_local_new ();
        if (local == NULL)
                goto err;

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        cbk_fn = quota_ftruncate_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);

        return 0;
err:
        QUOTA_STACK_UNWIND (ftruncate, frame, -1, ENOMEM, NULL, NULL, NULL);

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

        dict = dict_new ();
        if (dict == NULL) {
                ret = -1;
                goto out;
        }

dict_set:
        ret = dict_set_str (dict, (char *) name, dir_limit);
        if (ret < 0)
                goto out;

        gf_log (this->name, GF_LOG_INFO, "str = %s", dir_limit);

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
                int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
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
        quota_priv_t            *priv           = NULL;
        fop_stat_cbk_t           cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_stat_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;
        int32_t        ret   = -1;

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

        cbk_fn = quota_stat_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (stat, frame, -1, ENOMEM, NULL, NULL);
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
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
        quota_priv_t            *priv           = NULL;
        fop_fstat_cbk_t          cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_fstat_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        cbk_fn = quota_fstat_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fstat, frame, -1, ENOMEM, NULL, NULL);
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->buf = *buf;
        }
        UNLOCK (&ctx->lock);

out:
        QUOTA_STACK_UNWIND (readlink, frame, op_ret, op_errno, path, buf, xdata);
        return 0;
}


int32_t
quota_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_readlink_cbk_t       cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_readlink_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;
        int32_t        ret   = -1;

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

        cbk_fn = quota_readlink_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink, loc, size, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (readlink, frame, -1, ENOMEM, NULL, NULL, NULL);
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
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
        quota_priv_t            *priv           = NULL;
        fop_readv_cbk_t          cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_readv_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        cbk_fn = quota_readv_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                    xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (readv, frame, -1, ENOMEM, NULL, -1, NULL, NULL, NULL);
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
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
        quota_priv_t            *priv           = NULL;
        fop_fsync_cbk_t          cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_fsync_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        local->loc.inode = inode_ref (fd->inode);

        frame->local = local;

        cbk_fn = quota_fsync_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, flags, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fsync, frame, -1, ENOMEM, NULL, NULL, NULL);
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
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
        quota_priv_t            *priv           = NULL;
        fop_setattr_cbk_t        cbk_fn         = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_setattr_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;
        int32_t        ret   = -1;

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

        cbk_fn = quota_setattr_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (setattr, frame, -1, ENOMEM, NULL, NULL, NULL);
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

        quota_inode_ctx_get (local->loc.inode, -1, -1, this, NULL, NULL,
                             &ctx, 0);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "quota context not set in inode (gfid:%s)",
                        uuid_utoa (local->loc.inode->gfid));
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
        quota_priv_t            *priv           = NULL;
        fop_setattr_cbk_t        cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_fsetattr_cbk;
                goto wind;
        }

        quota_local_t *local = NULL;

        local = quota_local_new ();
        if (local == NULL) {
                goto unwind;
        }

        frame->local = local;

        local->loc.inode = inode_ref (fd->inode);

        cbk_fn = quota_fsetattr_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid, xdata);
        return 0;

unwind:
        QUOTA_STACK_UNWIND (fsetattr, frame, -1, ENOMEM, NULL, NULL, NULL);
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

        ret = quota_inode_ctx_get (inode, -1, -1, this, NULL, buf, &ctx, 1);
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
quota_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dev_t rdev, mode_t umask, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_mknod_cbk_t          cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_mknod_cbk;
                goto wind;
        }

        int32_t            ret            = -1;
        quota_local_t     *local          = NULL;
        int32_t            op_errno       = 0;

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

        local->delta = 0;

        ret = quota_check_limit (frame, loc->parent, this, NULL, NULL);
        if (ret == -1) {
                op_errno = EDQUOT;
                goto err;
        }

        cbk_fn = quota_mknod_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask,
                    xdata);


        return 0;
err:
        QUOTA_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                            NULL);

        return 0;
}

int
quota_setxattr_cbk (call_frame_t *frame, void *cookie,
                    xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        QUOTA_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
quota_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int flags, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_setxattr_cbk_t       cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_setxattr_cbk;
                goto wind;
        }

        int                      op_errno       = EINVAL;
        int                      op_ret         = -1;
        int64_t                 *size           = 0;
        uint64_t                 value          = 0;
        quota_inode_ctx_t       *ctx            = NULL;
        int                      ret            = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.quota*", dict,
                                   op_errno, err);

        ret = dict_get_bin (dict, QUOTA_UPDATE_USAGE_KEY, (void **) &size);
        if (0 == ret) {

                inode_ctx_get (loc->inode, this, &value);
                ctx = (quota_inode_ctx_t *)(unsigned long) value;
                if (NULL == ctx) {
                        gf_log (this->name, GF_LOG_ERROR, "Couldn't get the "
                                "context for %s. Usage may cross the limit.",
                                loc->path);
                        op_ret = -1;
                        goto err;
                }

                ctx->agg_size = ntoh64 (*size);

                QUOTA_STACK_UNWIND (setxattr, frame, ret, 0, xdata);
                return 0;
        }

        cbk_fn = quota_setxattr_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags, xdata);
        return 0;
err:
        QUOTA_STACK_UNWIND (setxattr, frame, op_ret, op_errno, NULL);
        return 0;
}

int
quota_fsetxattr_cbk (call_frame_t *frame, void *cookie,
                     xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        QUOTA_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
quota_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *dict, int flags, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;
        fop_fsetxattr_cbk_t      cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_fsetxattr_cbk;
                goto wind;
        }

        int32_t         op_ret   = -1;
        int32_t         op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.glusterfs.quota*", dict,
                                   op_errno, err);

        cbk_fn = quota_fsetxattr_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags, xdata);
        return 0;
 err:
        QUOTA_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, NULL);
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
        quota_priv_t            *priv           = NULL;
        fop_removexattr_cbk_t    cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_removexattr_cbk;
                goto wind;
        }

        int32_t         op_errno = EINVAL;

        VALIDATE_OR_GOTO (this, err);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.quota*",
                                 name, op_errno, err);

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (loc, err);

        cbk_fn = quota_removexattr_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
err:
        QUOTA_STACK_UNWIND (removexattr, frame, -1,  op_errno, NULL);
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
        quota_priv_t            *priv           = NULL;
        fop_fremovexattr_cbk_t   cbk_fn         = NULL;

        priv = this->private;
        if (!priv->is_quota_on) {
                cbk_fn = default_fremovexattr_cbk;
                goto wind;
        }

        int32_t         op_ret   = -1;
        int32_t         op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.quota*",
                                 name, op_errno, err);

        cbk_fn = quota_fremovexattr_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
 err:
        QUOTA_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, NULL);
        return 0;
}


int32_t
quota_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                  dict_t *xdata)
{
	inode_t           *root_inode = NULL;
        quota_priv_t      *priv       = NULL;
        uint64_t           value      = 0;
        quota_inode_ctx_t *ctx        = NULL;
        limits_t          *limit_node = NULL;
	int64_t            usage      = -1;
	int64_t            avail      = -1;
        int64_t            blocks     = 0;

	root_inode = cookie;

        /* This fop will fail mostly in case of client disconnect's,
         * which is already logged. Hence, not logging here */
        if (op_ret == -1)
                goto unwind;
	/*
	 * We should never get here unless quota_statfs (below) sent us a
	 * cookie, and it would only do so if the value was non-NULL.  This
	 * check is therefore just routine defensive coding.
	 */
	if (!root_inode) {
		gf_log(this->name,GF_LOG_WARNING,
		       "null inode, cannot adjust for quota");
		goto unwind;
	}
	if (!root_inode->table || (root_inode != root_inode->table->root)) {
		gf_log(this->name,GF_LOG_WARNING,
		       "non-root inode, cannot adjust for quota");
		goto unwind;
	}

        inode_ctx_get (root_inode, this, &value);
	if (!value) {
		goto unwind;
	}
        ctx = (quota_inode_ctx_t *)(unsigned long)value;
	usage = (ctx->size) / buf->f_bsize;
        priv = this->private;

        list_for_each_entry (limit_node, &priv->limit_head, limit_list) {
		/* Notice that this only works for volume-level quota. */
                if (strcmp (limit_node->path, "/") == 0) {
                        blocks = limit_node->value / buf->f_bsize;
                        if (usage > blocks) {
                                break;
                        }

			buf->f_blocks = blocks;
			avail = buf->f_blocks - usage;
			if (buf->f_bfree > avail) {
				buf->f_bfree = avail;
			}
			/*
			 * We have to assume that the total assigned quota
			 * won't cause us to dip into the reserved space,
			 * because dealing with the overcommitted cases is
			 * just too hairy (especially when different bricks
			 * might be using different reserved percentages and
			 * such).
			 */
			buf->f_bavail = buf->f_bfree;
			break;
                }
        }

unwind:
	if (root_inode) {
		inode_unref(root_inode);
	}
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int32_t
quota_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        quota_priv_t            *priv           = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                goto wind;
        }

	inode_t *root_inode = NULL;

	if (priv->consider_statfs && loc->inode) {
		root_inode = loc->inode->table->root;
		inode_ref(root_inode);
		STACK_WIND_COOKIE (frame, quota_statfs_cbk, root_inode,
				   FIRST_CHILD(this),
				   FIRST_CHILD(this)->fops->statfs, loc, xdata);
	}
	else {
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
                        gf_log(this->name,GF_LOG_WARNING,
                               "missing inode, cannot adjust for quota");
wind:
		STACK_WIND (frame, default_statfs_cbk, FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->statfs, loc, xdata);
	}
        return 0;
}


int
quota_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, gf_dirent_t *entries,
                    dict_t *xdata)
{
        gf_dirent_t *entry = NULL;

        if (op_ret <= 0)
                goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                /* TODO: fill things */
        }

unwind:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);

        return 0;
}
int
quota_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset, dict_t *dict)
{
        quota_priv_t            *priv           = NULL;
        fop_readdirp_cbk_t       cbk_fn         = NULL;

        priv = this->private;

        if (!priv->is_quota_on) {
                cbk_fn = default_readdirp_cbk;
                goto wind;
        }

        int ret = 0;

        if (dict) {
                ret = dict_set_uint64 (dict, QUOTA_SIZE_KEY, 0);
                if (ret < 0) {
                        goto err;
                }
        }

        cbk_fn = quota_readdirp_cbk;
wind:
        STACK_WIND (frame, cbk_fn, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset, dict);
        return 0;
err:
        STACK_UNWIND_STRICT (readdirp, frame, -1, EINVAL, NULL, NULL);
        return 0;
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
        quota_priv_t    *priv   = this->private;
        if (!priv->is_quota_on)
                return 0;

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


int
quota_parse_limits (quota_priv_t *priv, xlator_t *this, dict_t *xl_options,
                    struct list_head *old_list)
{
        int32_t       ret       = -1;
        char         *str       = NULL;
        char         *str_val   = NULL;
        char         *path      = NULL, *saveptr = NULL;
        uint64_t      value     = 0;
        limits_t     *quota_lim = NULL, *old = NULL;
        char         *last_colon= NULL;

        ret = dict_get_str (xl_options, "limit-set", &str);

        if (str) {
                path = strtok_r (str, ",", &saveptr);

                while (path) {
                        QUOTA_ALLOC_OR_GOTO (quota_lim, limits_t, err);

                        last_colon = strrchr (path, ':');
                        *last_colon = '\0';
                        str_val = last_colon + 1;

                        ret = gf_string2bytesize (str_val, &value);
                        if (ret != 0)
                                goto err;

                        quota_lim->hard_lim = value;

                        last_colon = strrchr (path, ':');
                        *last_colon = '\0';
                        str_val = last_colon + 1;

                        ret = gf_string2bytesize (str_val, &value);
                        if (ret != 0)
                                goto err;

                        quota_lim->soft_lim = value;

                        quota_lim->path = path;

                        gf_log (this->name, GF_LOG_INFO, "%s:%"PRId64,
                                quota_lim->path, quota_lim->value);

                        if (old_list != NULL) {
                                list_for_each_entry (old, old_list,
                                                     limit_list) {
                                        if (strcmp (old->path, quota_lim->path)
                                            == 0) {
                                                uuid_copy (quota_lim->gfid,
                                                           old->gfid);
                                                break;
                                        }
                                }
                        }

                        LOCK (&priv->lock);
                        {
                                list_add_tail (&quota_lim->limit_list,
                                               &priv->limit_head);
                        }
                        UNLOCK (&priv->lock);

                        path = strtok_r (NULL, ",", &saveptr);
                }
        } else {
                gf_log (this->name, GF_LOG_INFO,
                        "no \"limit-set\" option provided");
        }

        LOCK (&priv->lock);
        {
                list_for_each_entry (quota_lim, &priv->limit_head, limit_list) {
                        gf_log (this->name, GF_LOG_INFO, "%s:%"PRId64,
                                quota_lim->path, quota_lim->value);
                }
        }
        UNLOCK (&priv->lock);

        ret = 0;
err:
        return ret;
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

        INIT_LIST_HEAD (&priv->limit_head);

        LOCK_INIT (&priv->lock);

        this->private = priv;

        ret = quota_parse_limits (priv, this, this->options, NULL);

        if (ret) {
                goto err;
        }

        GF_OPTION_INIT ("deem-statfs", priv->consider_statfs, bool, err);
        GF_OPTION_INIT ("quota", priv->is_quota_on, bool, err);

        this->local_pool = mem_pool_new (quota_local_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto err;
        }

        ret = 0;
err:
        return ret;
}


void
__quota_reconfigure_inode_ctx (xlator_t *this, inode_t *inode, limits_t *limit)
{
        int                ret = -1;
        quota_inode_ctx_t *ctx = NULL;

        GF_VALIDATE_OR_GOTO ("quota", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, limit, out);

        ret = quota_inode_ctx_get (inode, limit->hard_lim, limit->soft_lim,
                                   this, NULL, NULL, &ctx, 1);
        if ((ret == -1) || (ctx == NULL)) {
                gf_log (this->name, GF_LOG_WARNING, "cannot create quota "
                        "context in inode(gfid:%s)",
                        uuid_utoa (inode->gfid));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->hard_lim = limit->hard_lim;
                ctx->soft_lim = limit->soft_lim;
        }
        UNLOCK (&ctx->lock);

out:
        return;
}


void
__quota_reconfigure (xlator_t *this, inode_table_t *itable, limits_t *limit)
{
        inode_t *inode = NULL;

        if ((this == NULL) || (itable == NULL) || (limit == NULL)) {
                goto out;
        }

        if (!uuid_is_null (limit->gfid)) {
                inode = inode_find (itable, limit->gfid);
        } else {
                inode = inode_resolve (itable, limit->path);
        }

        if (inode != NULL) {
                __quota_reconfigure_inode_ctx (this, inode, limit);
        }

out:
        return;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t           ret   = -1;
        quota_priv_t     *priv  = NULL;
        limits_t         *limit = NULL, *next = NULL, *new = NULL;
        struct list_head  head  = {0, };
        xlator_t         *top   = NULL;
        char              found = 0;

        priv = this->private;

        INIT_LIST_HEAD (&head);

        LOCK (&priv->lock);
        {
                list_splice_init (&priv->limit_head, &head);
        }
        UNLOCK (&priv->lock);

        ret = quota_parse_limits (priv, this, options, &head);
        if (ret == -1) {
                gf_log ("quota", GF_LOG_WARNING,
                        "quota reconfigure failed, "
                        "new changes will not take effect");
                goto out;
        }

        LOCK (&priv->lock);
        {
                top = ((glusterfs_ctx_t *)this->ctx)->active->top;
                GF_ASSERT (top);

                list_for_each_entry (limit, &priv->limit_head, limit_list) {
                        __quota_reconfigure (this, top->itable, limit);
                }

                list_for_each_entry_safe (limit, next, &head, limit_list) {
                        found = 0;
                        list_for_each_entry (new, &priv->limit_head,
                                             limit_list) {
                                if (strcmp (new->path, limit->path) == 0) {
                                        found = 1;
                                        break;
                                }
                        }

                        if (!found) {
                                limit->value = -1;
                                __quota_reconfigure (this, top->itable, limit);
                        }

                        list_del_init (&limit->limit_list);
                        GF_FREE (limit);
                }
        }
        UNLOCK (&priv->lock);

        GF_OPTION_RECONF ("deem-statfs", priv->consider_statfs, options, bool,
                          out);
        GF_OPTION_RECONF ("quota", priv->is_quota_on, options, bool, out);

        ret = 0;
out:
        return ret;
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
};

struct xlator_cbks cbks = {
        .forget = quota_forget
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
        {.key = {"quota"},
         .type = GF_OPTION_TYPE_BOOL,
         .default_value = "off",
         .description = "Add something here"
        },
        {.key = {NULL}}
};
