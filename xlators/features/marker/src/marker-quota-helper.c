/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "locking.h"
#include "marker-quota.h"
#include "marker-common.h"
#include "marker-quota-helper.h"
#include "marker-mem-types.h"

int
mq_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", inode, out);
        GF_VALIDATE_OR_GOTO ("marker", path, out);
        /* Not checking for parent because while filling
         * loc of root, parent will be NULL
         */

        if (inode) {
                loc->inode = inode_ref (inode);
        }

        if (parent)
                loc->parent = inode_ref (parent);

        if (!gf_uuid_is_null (inode->gfid))
                gf_uuid_copy (loc->gfid, inode->gfid);

        loc->path = gf_strdup (path);
        if (!loc->path) {
                gf_log ("loc fill", GF_LOG_ERROR, "strdup failed");
                goto out;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;
        else
                goto out;

        ret = 0;

out:
        if (ret < 0)
                loc_wipe (loc);

        return ret;
}


int32_t
mq_inode_loc_fill (const char *parent_gfid, inode_t *inode, loc_t *loc)
{
        char                *resolvedpath = NULL;
        inode_t             *parent       = NULL;
        quota_inode_ctx_t   *ctx          = NULL;
        xlator_t            *this         = NULL;
        int                  ret          = -1;

        this = THIS;

        if (inode == NULL) {
                gf_log_callingfn ("marker", GF_LOG_ERROR, "loc fill failed, "
                                  "inode is NULL");
                return ret;
        }

        if (loc == NULL)
                return ret;

        if ((inode) && __is_root_gfid (inode->gfid)) {
                loc->parent = NULL;
                goto ignore_parent;
        }

        if (parent_gfid == NULL)
                parent = inode_parent (inode, 0, NULL);
        else
                parent = inode_find (inode->table,
                                     (unsigned char *) parent_gfid);

        if (parent == NULL) {
                gf_log ("marker", GF_LOG_ERROR, "parent is NULL for %s",
                        uuid_utoa(inode->gfid));
                goto err;
        }

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0) {
                gf_log ("marker", GF_LOG_ERROR, "failed to resolve path for %s",
                        uuid_utoa(inode->gfid));
                goto err;
        }

        ret = mq_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0)
                goto err;

        ret = mq_inode_ctx_get (inode, this, &ctx);
        if (ret < 0 || ctx == NULL)
                ctx = mq_inode_ctx_new (inode, this);
        if (ctx == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                        "failed for %s", uuid_utoa (inode->gfid));
                ret = -1;
                goto err;
        }
        ret = 0;

err:
        if (parent)
                inode_unref (parent);

        GF_FREE (resolvedpath);

        return ret;
}


quota_inode_ctx_t *
mq_alloc_inode_ctx ()
{
        int32_t                  ret    = -1;
        quota_inode_ctx_t       *ctx    = NULL;

        QUOTA_ALLOC (ctx, quota_inode_ctx_t, ret);
        if (ret == -1)
                goto out;

        ctx->size = 0;
        ctx->dirty = 0;
        ctx->updation_status = _gf_false;
        LOCK_INIT (&ctx->lock);
        INIT_LIST_HEAD (&ctx->contribution_head);
out:
        return ctx;
}

void
mq_contri_fini (void *data)
{
        inode_contribution_t *contri = data;

        LOCK_DESTROY (&contri->lock);
        GF_FREE (contri);
}

inode_contribution_t*
mq_contri_init (inode_t *inode)
{
        inode_contribution_t *contri   = NULL;
        int32_t               ret      = 0;

        QUOTA_ALLOC (contri, inode_contribution_t, ret);
        if (ret == -1)
                goto out;

        GF_REF_INIT (contri, mq_contri_fini);

        contri->contribution = 0;
        contri->file_count = 0;
        contri->dir_count = 0;
        gf_uuid_copy (contri->gfid, inode->gfid);

        LOCK_INIT (&contri->lock);
        INIT_LIST_HEAD (&contri->contri_list);

out:
        return contri;
}

inode_contribution_t *
mq_get_contribution_node (inode_t *inode, quota_inode_ctx_t *ctx)
{
        inode_contribution_t    *contri = NULL;
        inode_contribution_t    *temp   = NULL;

        if (!inode || !ctx)
                goto out;

        LOCK (&ctx->lock);
        {
                if (list_empty (&ctx->contribution_head))
                        goto unlock;

                list_for_each_entry (temp, &ctx->contribution_head,
                                     contri_list) {
                        if (gf_uuid_compare (temp->gfid, inode->gfid) == 0) {
                                contri = temp;
                                GF_REF_GET (contri);
                                break;
                        }
                }
        }
unlock:
        UNLOCK (&ctx->lock);

out:
        return contri;
}

inode_contribution_t *
__mq_add_new_contribution_node (xlator_t *this, quota_inode_ctx_t *ctx,
                                loc_t *loc)
{
        inode_contribution_t *contribution = NULL;

        if (!loc->parent) {
                if (!gf_uuid_is_null (loc->pargfid))
                        loc->parent = inode_find (loc->inode->table,
                                                  loc->pargfid);

                if (!loc->parent)
                        loc->parent = inode_parent (loc->inode, loc->pargfid,
                                                    loc->name);
                if (!loc->parent)
                        goto out;
        }

        list_for_each_entry (contribution, &ctx->contribution_head,
                             contri_list) {
                if (loc->parent &&
                    gf_uuid_compare (contribution->gfid, loc->parent->gfid) == 0) {
                        goto out;
                }
        }

        contribution = mq_contri_init (loc->parent);
        if (contribution == NULL)
                goto out;

        list_add_tail (&contribution->contri_list, &ctx->contribution_head);

out:
        return contribution;
}


inode_contribution_t *
mq_add_new_contribution_node (xlator_t *this, quota_inode_ctx_t *ctx,
                              loc_t *loc)
{
        inode_contribution_t *contribution = NULL;

        if ((ctx == NULL) || (loc == NULL))
                return NULL;

        if (((loc->path) && (strcmp (loc->path, "/") == 0))
            || (!loc->path && gf_uuid_is_null (loc->pargfid)))
                return NULL;

        LOCK (&ctx->lock);
        {
                contribution = __mq_add_new_contribution_node (this, ctx, loc);
                if (contribution)
                        GF_REF_GET (contribution);
        }
        UNLOCK (&ctx->lock);

        return contribution;
}


int32_t
mq_dict_set_contribution (xlator_t *this, dict_t *dict, loc_t *loc,
                          uuid_t gfid, char *contri_key)
{
        int32_t ret                  = -1;
        char    key[QUOTA_KEY_MAX]   = {0, };

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", dict, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);

        if (gfid && !gf_uuid_is_null(gfid)) {
                GET_CONTRI_KEY (this, key, gfid, ret);
        } else if (loc->parent) {
                GET_CONTRI_KEY (this, key, loc->parent->gfid, ret);
        } else {
                /* nameless lookup, fetch contributions to all parents */
                GET_CONTRI_KEY (this, key, NULL, ret);
        }

        if (ret < 0)
                goto out;

        ret = dict_set_int64 (dict, key, 0);
        if (ret < 0)
                goto out;

        if (contri_key)
                strncpy (contri_key, key, QUOTA_KEY_MAX);

out:
        if (ret < 0)
                gf_log_callingfn (this->name, GF_LOG_ERROR, "dict set failed");

        return ret;
}


int32_t
mq_inode_ctx_get (inode_t *inode, xlator_t *this,
                  quota_inode_ctx_t **ctx)
{
        int32_t             ret      = -1;
        uint64_t            ctx_int  = 0;
        marker_inode_ctx_t *mark_ctx = NULL;

        GF_VALIDATE_OR_GOTO ("marker", inode, out);
        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        ret = inode_ctx_get (inode, this, &ctx_int);
        if (ret < 0) {
                ret = -1;
                *ctx = NULL;
                goto out;
        }

        mark_ctx = (marker_inode_ctx_t *) (unsigned long)ctx_int;
        if (mark_ctx->quota_ctx == NULL) {
                ret = -1;
                goto out;
        }

        *ctx = mark_ctx->quota_ctx;

        ret = 0;

out:
        return ret;
}


quota_inode_ctx_t *
__mq_inode_ctx_new (inode_t *inode, xlator_t *this)
{
        int32_t               ret        = -1;
        quota_inode_ctx_t    *quota_ctx  = NULL;
        marker_inode_ctx_t   *mark_ctx   = NULL;

        ret = marker_force_inode_ctx_get (inode, this, &mark_ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "marker_force_inode_ctx_get() failed");
                goto out;
        }

        LOCK (&inode->lock);
        {
                if (mark_ctx->quota_ctx == NULL) {
                        quota_ctx = mq_alloc_inode_ctx ();
                        if (quota_ctx == NULL) {
                                ret = -1;
                                goto unlock;
                        }
                        mark_ctx->quota_ctx = quota_ctx;
                } else {
                        quota_ctx = mark_ctx->quota_ctx;
                }

                ret = 0;
        }
unlock:
        UNLOCK (&inode->lock);
out:
        return quota_ctx;
}


quota_inode_ctx_t *
mq_inode_ctx_new (inode_t * inode, xlator_t *this)
{
        return __mq_inode_ctx_new (inode, this);
}

quota_local_t *
mq_local_new ()
{
        quota_local_t  *local   = NULL;

        local = mem_get0 (THIS->local_pool);
        if (!local)
                goto out;

        local->ref = 1;
        LOCK_INIT (&local->lock);

        local->ctx = NULL;
        local->contri = NULL;

out:
        return local;
}

quota_local_t *
mq_local_ref (quota_local_t *local)
{
        LOCK (&local->lock);
        {
                local->ref ++;
        }
        UNLOCK (&local->lock);

        return local;
}


int32_t
mq_local_unref (xlator_t *this, quota_local_t *local)
{
        int32_t ref = 0;
        if (local == NULL)
                goto out;

        QUOTA_SAFE_DECREMENT (&local->lock, local->ref, ref);

        if (ref != 0)
                goto out;

        if (local->fd != NULL)
                fd_unref (local->fd);

        if (local->contri)
                GF_REF_PUT (local->contri);

        if (local->xdata)
                dict_unref (local->xdata);

        loc_wipe (&local->loc);

        loc_wipe (&local->parent_loc);

        LOCK_DESTROY (&local->lock);

        mem_put (local);
out:
        return 0;
}


inode_contribution_t *
mq_get_contribution_from_loc (xlator_t *this, loc_t *loc)
{
        int32_t               ret          = 0;
        quota_inode_ctx_t    *ctx          = NULL;
        inode_contribution_t *contribution = NULL;

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "cannot get marker-quota context from inode "
                                  "(gfid:%s, path:%s)",
                                  uuid_utoa (loc->inode->gfid), loc->path);
                goto err;
        }

        contribution = mq_get_contribution_node (loc->parent, ctx);
        if (contribution == NULL) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "inode (gfid:%s, path:%s) has "
                                  "no contribution towards parent (gfid:%s)",
                                  uuid_utoa (loc->inode->gfid),
                                  loc->path, uuid_utoa (loc->parent->gfid));
                goto err;
        }

err:
        return contribution;
}
