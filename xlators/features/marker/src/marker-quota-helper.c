/*Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "locking.h"
#include "marker-quota.h"
#include "marker-common.h"
#include "marker-quota-helper.h"
#include "marker-mem-types.h"

int
quota_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int ret = -1;

        if (!loc)
                return ret;

        if (inode) {
                loc->inode = inode_ref (inode);
                loc->ino = inode->ino;
        }

        if (parent)
                loc->parent = inode_ref (parent);

        loc->path = gf_strdup (path);
        if (!loc->path) {
                gf_log ("loc fill", GF_LOG_ERROR, "strdup failed");
                goto loc_wipe;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;
        else
                goto loc_wipe;

        ret = 0;
loc_wipe:
        if (ret < 0)
                loc_wipe (loc);

        return ret;
}


int32_t
quota_inode_loc_fill (const char *parent_gfid, inode_t *inode, loc_t *loc)
{
        char            *resolvedpath = NULL;
        inode_t         *parent       = NULL;
        int              ret          = -1;

        if ((!inode) || (!loc))
                return ret;

        if ((inode) && (inode->ino == 1)) {
                loc->parent = NULL;
                goto ignore_parent;
        }

        if (parent_gfid == NULL)
                parent = inode_parent (inode, 0, NULL);
        else
                parent = inode_find (inode->table,
                                     (unsigned char *) parent_gfid);

        if (parent == NULL)
                goto err;

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0)
                goto err;

        ret = quota_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0)
                goto err;

err:
        if (parent)
                inode_unref (parent);

        GF_FREE (resolvedpath);

        return ret;
}


quota_inode_ctx_t *
quota_alloc_inode_ctx ()
{
        int32_t                  ret    = -1;
        quota_inode_ctx_t       *ctx    = NULL;

        QUOTA_ALLOC (ctx, quota_inode_ctx_t, ret);
        if (ret == -1)
                goto out;

        ctx->size = 0;
        ctx->dirty = 0;
        LOCK_INIT (&ctx->lock);
        INIT_LIST_HEAD (&ctx->contribution_head);
out:
        return ctx;
}

inode_contribution_t *
get_contribution_node (inode_t *inode, quota_inode_ctx_t *ctx)
{
        inode_contribution_t    *contri = NULL;
        inode_contribution_t    *temp   = NULL;

        GF_VALIDATE_OR_GOTO ("marker", inode, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        list_for_each_entry (temp, &ctx->contribution_head, contri_list) {
                if (uuid_compare (temp->gfid, inode->gfid) == 0) {
                        contri = temp;
                        goto out;
                }
        }
out:
        return contri;
}


int32_t
delete_contribution_node (dict_t *dict, char *key,
                          inode_contribution_t *contribution)
{
        if (dict_get (dict, key) != NULL)
                goto out;

        QUOTA_FREE_CONTRIBUTION_NODE (contribution);
out:
        return 0;
}


inode_contribution_t *
__add_new_contribution_node (xlator_t *this, quota_inode_ctx_t *ctx, loc_t *loc)
{
        int32_t ret = 0;
        inode_contribution_t *contribution = NULL;

        list_for_each_entry (contribution, &ctx->contribution_head, contri_list) {
                if (uuid_compare (contribution->gfid, loc->parent->gfid) == 0) {
                        goto out;
                }
        }

        QUOTA_ALLOC (contribution, inode_contribution_t, ret);
        if (ret == -1)
                goto out;

        contribution->contribution = 0;

        uuid_copy (contribution->gfid, loc->parent->gfid);

	LOCK_INIT (&contribution->lock);

        list_add_tail (&contribution->contri_list, &ctx->contribution_head);

out:
        return contribution;
}


inode_contribution_t *
add_new_contribution_node (xlator_t *this, quota_inode_ctx_t *ctx, loc_t *loc)
{
        inode_contribution_t *contribution = NULL;

        if ((ctx == NULL) || (loc == NULL))
                return NULL;

        if (strcmp (loc->path, "/") == 0)
                return NULL;

        LOCK (&ctx->lock);
        {
                contribution = __add_new_contribution_node (this, ctx, loc);
        }
        UNLOCK (&ctx->lock);

        return contribution;
}


int32_t
dict_set_contribution (xlator_t *this, dict_t *dict,
                       loc_t *loc)
{
        int32_t ret              = -1;
        char    contri_key [512] = {0, };

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", dict, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);

        GET_CONTRI_KEY (contri_key, loc->parent->gfid, ret);
        if (ret < 0) {
                ret = -1;
                goto out;
        }

        ret = dict_set_int64 (dict, contri_key, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "unable to set dict value on %s.",
                        loc->path);
                goto out;
        }

        ret = 0;
out:
        return ret;
}


int32_t
quota_inode_ctx_get (inode_t *inode, xlator_t *this,
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
__quota_inode_ctx_new (inode_t *inode, xlator_t *this)
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
                        quota_ctx = quota_alloc_inode_ctx ();
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
quota_inode_ctx_new (inode_t * inode, xlator_t *this)
{
        return __quota_inode_ctx_new (inode, this);
}

quota_local_t *
quota_local_new ()
{
        int32_t         ret     = -1;
        quota_local_t  *local   = NULL;

        QUOTA_ALLOC (local, quota_local_t, ret);
        if (ret < 0)
                goto out;

        local->ref = 1;
        local->delta = 0;
        local->err = 0;
        LOCK_INIT (&local->lock);

        memset (&local->loc, 0, sizeof (loc_t));
        memset (&local->parent_loc, 0, sizeof (loc_t));

        local->ctx = NULL;
        local->contri = NULL;

out:
        return local;
}

quota_local_t *
quota_local_ref (quota_local_t *local)
{
        LOCK (&local->lock);
        {
                local->ref ++;
        }
        UNLOCK (&local->lock);

        return local;
}


int32_t
quota_local_unref (xlator_t *this, quota_local_t *local)
{
        int32_t ref = 0;
        if (local == NULL)
                goto out;

        QUOTA_SAFE_DECREMENT (&local->lock, local->ref, ref);

        if (ref > 0)
                goto out;

        if (local->fd != NULL)
                fd_unref (local->fd);

        loc_wipe (&local->loc);

        loc_wipe (&local->parent_loc);

        LOCK_DESTROY (&local->lock);
out:
        return 0;
}


inode_contribution_t *
get_contribution_from_loc (xlator_t *this, loc_t *loc)
{
        int32_t               ret          = 0;
        quota_inode_ctx_t    *ctx          = NULL;
        inode_contribution_t *contribution = NULL;

        ret = quota_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "cannot get marker-quota context from inode "
                                  "(ino: %"PRId64", gfid:%s, path:%s)",
                                  loc->inode->ino,
                                  uuid_utoa (loc->inode->gfid),
                                  loc->path);
                goto err;
        }

        contribution = get_contribution_node (loc->parent, ctx);
        if (contribution == NULL) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "inode (ino:%"PRId64", gfid:%s, path:%s ) has"
                                  " no contribution towards parent (ino:%"PRId64
                                  ", gfid:%s)", loc->inode->ino,
                                  uuid_utoa (loc->inode->gfid),
                                  loc->path, loc->parent->ino,
                                  uuid_utoa (loc->parent->gfid));
                goto err;
        }

err:
        return contribution;
}
