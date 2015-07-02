/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "libxlator.h"
#include "common-utils.h"
#include "byte-order.h"
#include "marker-quota.h"
#include "marker-quota-helper.h"
#include "syncop.h"
#include "quota-common-utils.h"

int
mq_loc_copy (loc_t *dst, loc_t *src)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", dst, out);
        GF_VALIDATE_OR_GOTO ("marker", src, out);

        if (src->inode == NULL ||
            ((src->parent == NULL) && (gf_uuid_is_null (src->pargfid))
             && !__is_root_gfid (src->inode->gfid))) {
                gf_log ("marker", GF_LOG_WARNING,
                        "src loc is not valid");
                goto out;
        }

        ret = loc_copy (dst, src);
out:
        return ret;
}

int32_t
mq_get_local_err (quota_local_t *local,
                  int32_t *val)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", local, out);
        GF_VALIDATE_OR_GOTO ("marker", val, out);

        LOCK (&local->lock);
        {
                *val = local->err;
        }
        UNLOCK (&local->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_get_ctx_updation_status (quota_inode_ctx_t *ctx,
                            gf_boolean_t *status)
{
        int32_t   ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        LOCK (&ctx->lock);
        {
                *status = ctx->updation_status;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}


int32_t
mq_set_ctx_updation_status (quota_inode_ctx_t *ctx,
                            gf_boolean_t status)
{
        int32_t   ret = -1;

        if (ctx == NULL)
                goto out;

        LOCK (&ctx->lock);
        {
                ctx->updation_status = status;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_test_and_set_ctx_updation_status (quota_inode_ctx_t *ctx,
                                     gf_boolean_t *status)
{
        int32_t         ret     = -1;
        gf_boolean_t    temp    = _gf_false;

        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        LOCK (&ctx->lock);
        {
                temp = *status;
                *status = ctx->updation_status;
                ctx->updation_status = temp;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_set_ctx_create_status (quota_inode_ctx_t *ctx,
                          gf_boolean_t status)
{
        int32_t   ret = -1;

        if (ctx == NULL)
                goto out;

        LOCK (&ctx->lock);
        {
                ctx->create_status = status;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_test_and_set_ctx_create_status (quota_inode_ctx_t *ctx,
                                   gf_boolean_t *status)
{
        int32_t         ret     = -1;
        gf_boolean_t    temp    = _gf_false;

        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        LOCK (&ctx->lock);
        {
                temp = *status;
                *status = ctx->create_status;
                ctx->create_status = temp;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}

void
mq_assign_lk_owner (xlator_t *this, call_frame_t *frame)
{
        marker_conf_t *conf     = NULL;
        uint64_t       lk_owner = 0;

        conf = this->private;

        LOCK (&conf->lock);
        {
                if (++conf->quota_lk_owner == 0) {
                        ++conf->quota_lk_owner;
                }

                lk_owner = conf->quota_lk_owner;
        }
        UNLOCK (&conf->lock);

        set_lk_owner_from_uint64 (&frame->root->lk_owner, lk_owner);

        return;
}


int32_t
mq_loc_fill_from_name (xlator_t *this, loc_t *newloc, loc_t *oldloc,
                       uint64_t ino, char *name)
{
        int32_t   ret  = -1;
        int32_t   len  = 0;
        char     *path = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", newloc, out);
        GF_VALIDATE_OR_GOTO ("marker", oldloc, out);
        GF_VALIDATE_OR_GOTO ("marker", name, out);

        newloc->inode = inode_new (oldloc->inode->table);

        if (!newloc->inode) {
                ret = -1;
                goto out;
        }

        newloc->parent = inode_ref (oldloc->inode);
        gf_uuid_copy (newloc->pargfid, oldloc->inode->gfid);

        if (!oldloc->path) {
                ret = loc_path (oldloc, NULL);
                if (ret == -1)
                        goto out;
        }

        len = strlen (oldloc->path);

        if (oldloc->path [len - 1] == '/')
                ret = gf_asprintf ((char **) &path, "%s%s",
                                   oldloc->path, name);
        else
                ret = gf_asprintf ((char **) &path, "%s/%s",
                                   oldloc->path, name);

        if (ret < 0)
                goto out;

        newloc->path = path;

        newloc->name = strrchr (newloc->path, '/');

        if (newloc->name)
                newloc->name++;

        gf_log (this->name, GF_LOG_DEBUG, "path = %s name =%s",
                newloc->path, newloc->name);
out:
        return ret;
}

int32_t
mq_dirty_inode_updation_done (call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}

int32_t
mq_release_lock_on_dirty_inode (call_frame_t *frame, void *cookie, xlator_t *this,
                                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        struct gf_flock   lock  = {0, };
        quota_local_t    *local = NULL;
        loc_t             loc = {0, };
	int               ret = -1;

        local = frame->local;

        if (op_ret == -1) {
                local->err = -1;

                mq_dirty_inode_updation_done (frame, NULL, this, 0, 0, NULL);

                return 0;
        }

        if (op_ret == 0)
                local->ctx->dirty = 0;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        ret = loc_copy (&loc, &local->loc);
	if (ret == -1) {
                local->err = -1;
                frame->local = NULL;
                mq_dirty_inode_updation_done (frame, NULL, this, 0, 0, NULL);
                return 0;
        }

        if (local->loc.inode == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Inode is NULL, so can't stackwind.");
                goto out;
        }

        STACK_WIND (frame,
                    mq_dirty_inode_updation_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &loc, F_SETLKW, &lock, NULL);

        loc_wipe (&loc);

        return 0;
out:
        mq_dirty_inode_updation_done (frame, NULL, this, -1, 0, NULL);

        return 0;
}

int32_t
mq_mark_inode_undirty (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict,
                       dict_t *xdata)
{
        int32_t        ret     = -1;
        int64_t       *size    = NULL;
        dict_t        *newdict = NULL;
        quota_local_t *local   = NULL;

        local = (quota_local_t *) frame->local;

        if (op_ret == -1)
                goto err;

        if (!dict)
                goto wind;

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (ret)
                goto wind;

        LOCK (&local->ctx->lock);
        {
                local->ctx->size = ntoh64 (*size);
        }
        UNLOCK (&local->ctx->lock);

wind:
        newdict = dict_new ();
        if (!newdict)
                goto err;

        ret = dict_set_int8 (newdict, QUOTA_DIRTY_KEY, 0);
        if (ret)
                goto err;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        GF_UUID_ASSERT (local->loc.gfid);
        STACK_WIND (frame, mq_release_lock_on_dirty_inode,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    &local->loc, newdict, 0, NULL);
        ret = 0;

err:
        if (op_ret == -1 || ret == -1) {
                local->err = -1;

                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
mq_update_size_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, inode_t *inode,
                      struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int32_t          ret      = -1;
        dict_t          *new_dict = NULL;
        int64_t         *size     = NULL;
        int64_t         *delta    = NULL;
        quota_local_t   *local    = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto err;

        if (dict == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Dict is null while updating the size xattr %s",
                        local->loc.path?local->loc.path:"");
                goto err;
        }

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (!size) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to get the size, %s",
                        local->loc.path?local->loc.path:"");
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (delta, int64_t, ret, err);

        *delta = hton64 (local->sum - ntoh64 (*size));

        gf_log (this->name, GF_LOG_DEBUG, "calculated size = %"PRId64", "
                "original size = %"PRIu64
                " path = %s diff = %"PRIu64, local->sum, ntoh64 (*size),
                local->loc.path, ntoh64 (*delta));

        new_dict = dict_new ();
        if (!new_dict) {
		errno = ENOMEM;
		goto err;
	}

        ret = dict_set_bin (new_dict, QUOTA_SIZE_KEY, delta, 8);
        if (ret)
                goto err;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        GF_UUID_ASSERT (local->loc.gfid);

        STACK_WIND (frame, mq_mark_inode_undirty, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, &local->loc,
                    GF_XATTROP_ADD_ARRAY64, new_dict, NULL);

        ret = 0;

err:
        if (op_ret == -1 || ret == -1) {
                local->err = -1;
                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);
        }

        if (new_dict)
                dict_unref (new_dict);

        return 0;
}

int32_t
mq_test_and_set_local_err(quota_local_t *local,
                          int32_t *val)
{
        int     tmp = 0;
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", local, out);
        GF_VALIDATE_OR_GOTO ("marker", val, out);

        LOCK (&local->lock);
        {
                tmp = local->err;
                local->err = *val;
                *val = tmp;
        }
        UNLOCK (&local->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_get_dirty_inode_size (call_frame_t *frame, xlator_t *this)
{
        int32_t        ret   = -1;
        dict_t        *dict  = NULL;
        quota_local_t *local = NULL;

        local = (quota_local_t *) frame->local;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto err;
        }

        ret = dict_set_int64 (dict, QUOTA_SIZE_KEY, 0);
        if (ret)
                goto err;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        GF_UUID_ASSERT (local->loc.gfid);

        STACK_WIND (frame, mq_update_size_xattr, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->loc, dict);
        ret =0;

err:
        if (ret) {
                local->err = -1;

                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);
        }

        if (dict)
                dict_unref (dict);

        return 0;
}

int32_t
mq_get_child_contribution (call_frame_t *frame,
                           void *cookie,
                           xlator_t *this,
                           int32_t op_ret,
                           int32_t op_errno,
                           inode_t *inode,
                           struct iatt *buf,
                           dict_t *dict,
                           struct iatt *postparent)
{
        int32_t        ret                           = -1;
        int32_t        val                           = 0;
        char           contri_key[CONTRI_KEY_MAX]    = {0, };
        int64_t       *contri                        = NULL;
        quota_local_t *local                         = NULL;

        local = frame->local;

        frame->local = NULL;

        QUOTA_STACK_DESTROY (frame, this);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s",
                        strerror (op_errno));
                val = -2;
                if (!mq_test_and_set_local_err (local, &val) &&
                    val != -2)
                        mq_release_lock_on_dirty_inode (local->frame, NULL,
                                                        this, 0, 0, NULL);

                goto exit;
        }

        ret = mq_get_local_err (local, &val);
        if (!ret && val == -2)
                goto exit;

        GET_CONTRI_KEY (contri_key, local->loc.inode->gfid, ret);
        if (ret < 0)
                goto out;

        if (!dict)
                goto out;

        if (dict_get_bin (dict, contri_key, (void **) &contri) == 0)
                local->sum += ntoh64 (*contri);

out:
        LOCK (&local->lock);
        {
                val = --local->dentry_child_count;
        }
        UNLOCK (&local->lock);

        if (val == 0) {
                mq_dirty_inode_readdir (local->frame, NULL, this,
                                        0, 0, NULL, NULL);
        }
        mq_local_unref (this, local);

        return 0;
exit:
        mq_local_unref (this, local);
        return 0;
}

int32_t
mq_readdir_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                gf_dirent_t *entries, dict_t *xdata)
{
        char           contri_key[CONTRI_KEY_MAX]    = {0, };
        int32_t        ret                           = 0;
        int32_t        val                           = 0;
        off_t          offset                        = 0;
        int32_t        count                         = 0;
        dict_t        *dict                          = NULL;
        quota_local_t *local                         = NULL;
        gf_dirent_t   *entry                         = NULL;
        call_frame_t  *newframe                      = NULL;
        loc_t          loc                           = {0, };

        local = mq_local_ref (frame->local);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "readdir failed %s", strerror (op_errno));
                local->err = -1;

                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);

                goto end;
        } else if (op_ret == 0) {
                mq_get_dirty_inode_size (frame, this);

                goto end;
        }

        local->dentry_child_count =  0;

        list_for_each_entry (entry, (&entries->list), list) {
                gf_log (this->name, GF_LOG_DEBUG, "entry  = %s", entry->d_name);

                if ((!strcmp (entry->d_name, ".")) || (!strcmp (entry->d_name,
                                                                ".."))) {
                        gf_log (this->name, GF_LOG_DEBUG, "entry  = %s",
                                entry->d_name);
                        continue;
                }

                offset = entry->d_off;
                count++;
        }

        if (count == 0) {
                mq_get_dirty_inode_size (frame, this);
                goto end;

        }

        local->frame = frame;

        LOCK (&local->lock);
        {
                local->dentry_child_count = count;
                local->d_off = offset;
        }
        UNLOCK (&local->lock);


        list_for_each_entry (entry, (&entries->list), list) {
                gf_log (this->name, GF_LOG_DEBUG, "entry  = %s", entry->d_name);

                if ((!strcmp (entry->d_name, ".")) || (!strcmp (entry->d_name,
                                                                ".."))) {
                        gf_log (this->name, GF_LOG_DEBUG, "entry  = %s",
                                entry->d_name);
                        continue;
                }

                ret = mq_loc_fill_from_name (this, &loc, &local->loc,
                                             entry->d_ino, entry->d_name);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING, "Couldn't build "
                                "loc for %s/%s, returning from updation of "
                                "quota attributes",
                                uuid_utoa (local->loc.inode->gfid),
                                entry->d_name);
                        goto out;
                }

                ret = 0;

                LOCK (&local->lock);
                {
                        if (local->err != -2) {
                                newframe = copy_frame (frame);
                                if (!newframe) {
                                        ret = -1;
                                }
                        } else
                                ret = -1;
                }
                UNLOCK (&local->lock);

                if (ret == -1)
                        goto out;

                newframe->local = mq_local_ref (local);

                dict = dict_new ();
                if (!dict) {
                        ret = -1;
                        goto out;
                }

                GET_CONTRI_KEY (contri_key, local->loc.inode->gfid, ret);
                if (ret < 0)
                        goto out;

                ret = dict_set_int64 (dict, contri_key, 0);
                if (ret)
                        goto out;

                STACK_WIND (newframe,
                            mq_get_child_contribution,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup,
                            &loc, dict);

                offset = entry->d_off;
                newframe = NULL;

        out:
                loc_wipe (&loc);
                if (dict) {
                        dict_unref (dict);
                        dict = NULL;
                }

                if (ret) {
                        val = -2;
                        mq_test_and_set_local_err (local, &val);

                        if (newframe) {
                                newframe->local = NULL;
                                mq_local_unref(this, local);
                                QUOTA_STACK_DESTROY (newframe, this);
                        }

                        break;
                }
        }

        if (ret && val != -2) {
                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);
        }
end:
        mq_local_unref (this, local);

        return 0;
}

int32_t
mq_dirty_inode_readdir (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno,
                        fd_t *fd, dict_t *xdata)
{
        quota_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                local->err = -1;
                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);
                return 0;
        }

        if (local->fd == NULL)
                local->fd = fd_ref (fd);

        STACK_WIND (frame,
                    mq_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    local->fd, READDIR_BUF, local->d_off, xdata);

        return 0;
}

int32_t
mq_check_if_still_dirty (call_frame_t *frame,
                         void *cookie,
                         xlator_t *this,
                         int32_t op_ret,
                         int32_t op_errno,
                         inode_t *inode,
                         struct iatt *buf,
                         dict_t *dict,
                         struct iatt *postparent)
{
        int8_t           dirty          = -1;
        int32_t          ret            = -1;
        fd_t            *fd             = NULL;
        quota_local_t   *local          = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get "
                        "the dirty xattr for %s", local->loc.path);
                goto err;
        }

        if (!dict) {
                ret = -1;
                goto err;
        }

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret)
                goto err;

        //the inode is not dirty anymore
        if (dirty == 0) {
                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);

                return 0;
        }

        fd = fd_create (local->loc.inode, frame->root->pid);

        local->d_off = 0;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        GF_UUID_ASSERT (local->loc.gfid);
        STACK_WIND(frame,
                   mq_dirty_inode_readdir,
                   FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->opendir,
                   &local->loc, fd, NULL);

        ret = 0;

err:
        if (op_ret == -1 || ret == -1) {
                local->err = -1;
                mq_release_lock_on_dirty_inode (frame, NULL, this, 0, 0, NULL);
        }

        if (fd != NULL) {
                fd_unref (fd);
        }

        return 0;
}

int32_t
mq_get_dirty_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t        ret       = -1;
        dict_t        *xattr_req = NULL;
        quota_local_t *local     = NULL;

        if (op_ret == -1) {
                mq_dirty_inode_updation_done (frame, NULL, this, 0, 0, NULL);
                return 0;
        }

        local = frame->local;

        xattr_req = dict_new ();
        if (xattr_req == NULL) {
                ret = -1;
                goto err;
        }

        ret = dict_set_int8 (xattr_req, QUOTA_DIRTY_KEY, 0);
        if (ret)
                goto err;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        if (gf_uuid_is_null (local->loc.gfid)) {
                ret = -1;
                goto err;
        }

        STACK_WIND (frame,
                    mq_check_if_still_dirty,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    &local->loc,
                    xattr_req);
        ret = 0;

err:
        if (ret) {
                local->err = -1;
                mq_release_lock_on_dirty_inode(frame, NULL, this, 0, 0, NULL);
        }

        if (xattr_req)
                dict_unref (xattr_req);

        return 0;
}

/* return 1 when dirty updation started
 * 0 other wise
 */
int32_t
mq_update_dirty_inode (xlator_t *this, loc_t *loc, quota_inode_ctx_t *ctx,
                       inode_contribution_t *contribution)
{
        int32_t          ret        = -1;
        quota_local_t   *local      = NULL;
        gf_boolean_t     status     = _gf_false;
        struct gf_flock  lock       = {0, };
        call_frame_t    *frame      = NULL;

        ret = mq_get_ctx_updation_status (ctx, &status);
        if (ret == -1 || status == _gf_true) {
                ret = 0;
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL) {
                ret = -1;
                goto out;
        }

        mq_assign_lk_owner (this, frame);

        local = mq_local_new ();
        if (local == NULL)
                goto fr_destroy;

        frame->local = local;
        ret = mq_loc_copy (&local->loc, loc);
        if (ret < 0)
                goto fr_destroy;

        local->ctx = ctx;

        if (contribution) {
                local->contri = contribution;
                GF_REF_GET (local->contri);
        }

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        if (local->loc.inode == NULL) {
                ret = -1;
                gf_log (this->name, GF_LOG_WARNING,
                        "Inode is NULL, so can't stackwind.");
                goto fr_destroy;
        }

        STACK_WIND (frame,
                    mq_get_dirty_xattr,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->loc, F_SETLKW, &lock, NULL);
        return 1;

fr_destroy:
        QUOTA_STACK_DESTROY (frame, this);
out:

        return 0;
}


int32_t
mq_inode_creation_done (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        quota_local_t *local = NULL;

        if (frame == NULL)
                return 0;

        local = frame->local;

        if (local != NULL) {
                mq_initiate_quota_txn (this, &local->loc);
        }

        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}


int32_t
mq_xattr_creation_release_lock (call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, dict_t *xdata)
{
        struct gf_flock  lock  = {0, };
        quota_local_t   *local = NULL;

        local = frame->local;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    mq_inode_creation_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->loc,
                    F_SETLKW, &lock, NULL);

        return 0;
}


int32_t
mq_create_dirty_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict,
                       dict_t *xdata)
{
        int32_t          ret       = -1;
        dict_t          *newdict   = NULL;
        quota_local_t   *local     = NULL;

        if (op_ret < 0) {
                goto err;
        }

        local = frame->local;

        if (local->loc.inode->ia_type == IA_IFDIR) {
                newdict = dict_new ();
                if (!newdict) {
                        goto err;
                }

                ret = dict_set_int8 (newdict, QUOTA_DIRTY_KEY, 0);
                if (ret == -1) {
                        goto err;
                }

                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);
                GF_UUID_ASSERT (local->loc.gfid);

                STACK_WIND (frame, mq_xattr_creation_release_lock,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            &local->loc, newdict, 0, NULL);
        } else {
                mq_xattr_creation_release_lock (frame, NULL, this, 0, 0, NULL);
        }

        ret = 0;

err:
        if (ret < 0) {
                mq_xattr_creation_release_lock (frame, NULL, this, 0, 0, NULL);
        }

        if (newdict != NULL)
                dict_unref (newdict);

        return 0;
}


int32_t
mq_create_xattr (xlator_t *this, call_frame_t *frame)
{
        int32_t               ret                  = 0;
        int64_t              *value                = NULL;
        int64_t              *size                 = NULL;
        dict_t               *dict                 = NULL;
        char                  key[CONTRI_KEY_MAX]  = {0, };
        quota_local_t        *local                = NULL;
        quota_inode_ctx_t    *ctx                  = NULL;
        inode_contribution_t *contri               = NULL;

        if (frame == NULL || this == NULL)
                return 0;

        local = frame->local;

        ret = mq_inode_ctx_get (local->loc.inode, this, &ctx);
        if (ret < 0) {
                ctx = mq_inode_ctx_new (local->loc.inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "mq_inode_ctx_new failed");
                        ret = -1;
                        goto out;
                }
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        if (local->loc.inode->ia_type == IA_IFDIR) {
                QUOTA_ALLOC_OR_GOTO (size, int64_t, ret, err);
                ret = dict_set_bin (dict, QUOTA_SIZE_KEY, size, 8);
                if (ret < 0)
                        goto free_size;
        }

        if ((local->loc.path && strcmp (local->loc.path, "/") != 0)
            || (local->loc.inode && !gf_uuid_is_null (local->loc.inode->gfid) &&
                !__is_root_gfid (local->loc.inode->gfid))
            || (!gf_uuid_is_null (local->loc.gfid)
                && !__is_root_gfid (local->loc.gfid))) {
                contri = mq_add_new_contribution_node (this, ctx, &local->loc);
                if (contri == NULL)
                        goto err;

                QUOTA_ALLOC_OR_GOTO (value, int64_t, ret, err);
                GET_CONTRI_KEY (key, local->loc.parent->gfid, ret);

                ret = dict_set_bin (dict, key, value, 8);
                if (ret < 0)
                        goto free_value;
        }

        if (gf_uuid_is_null (local->loc.gfid)) {
                ret = -1;
                goto out;
        }

        STACK_WIND (frame, mq_create_dirty_xattr, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, &local->loc,
                    GF_XATTROP_ADD_ARRAY64, dict, NULL);
        ret = 0;

free_size:
        if (ret < 0) {
                GF_FREE (size);
        }

free_value:
        if (ret < 0) {
                GF_FREE (value);
        }

err:
        dict_unref (dict);

        if (contri)
                GF_REF_PUT (contri);

out:
        if (ret < 0) {
                mq_xattr_creation_release_lock (frame, NULL, this, 0, 0, NULL);
        }

        return 0;
}


int32_t
mq_check_n_set_inode_xattr (call_frame_t *frame, void *cookie,
                            xlator_t *this, int32_t op_ret, int32_t op_errno,
                            inode_t *inode, struct iatt *buf, dict_t *dict,
                            struct iatt *postparent)
{
        quota_local_t        *local                      = NULL;
        int64_t              *size                       = NULL;
        int64_t              *contri                     = NULL;
        int8_t                dirty                      = 0;
        int32_t               ret                        = 0;
        char                  contri_key[CONTRI_KEY_MAX] = {0, };

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (ret < 0)
                goto create_xattr;

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret < 0)
                goto create_xattr;

        //check contribution xattr if not root
        if ((local->loc.path && strcmp (local->loc.path, "/") != 0)
            || (!gf_uuid_is_null (local->loc.gfid)
                && !__is_root_gfid (local->loc.gfid))
            || (local->loc.inode
                && !gf_uuid_is_null (local->loc.inode->gfid)
                && !__is_root_gfid (local->loc.inode->gfid))) {
                GET_CONTRI_KEY (contri_key, local->loc.parent->gfid, ret);
                if (ret < 0)
                        goto out;

                ret = dict_get_bin (dict, contri_key, (void **) &contri);
                if (ret < 0)
                        goto create_xattr;
        }

out:
        mq_xattr_creation_release_lock (frame, NULL, this, 0, 0, NULL);
        return 0;

create_xattr:
        if (gf_uuid_is_null (local->loc.gfid)) {
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);
        }

        mq_create_xattr (this, frame);
        return 0;
}


int32_t
mq_get_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dict_t        *xattr_req = NULL;
        quota_local_t *local     = NULL;
        int32_t        ret       = 0;

        if (op_ret < 0) {
                goto lock_err;
        }

        local = frame->local;

        xattr_req = dict_new ();
        if (xattr_req == NULL) {
                goto err;
        }

        ret = mq_req_xattr (this, &local->loc, xattr_req, NULL);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "cannot request xattr");
                goto err;
        }

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        if (gf_uuid_is_null (local->loc.gfid)) {
                ret = -1;
                goto err;
        }

        STACK_WIND (frame, mq_check_n_set_inode_xattr, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->loc, xattr_req);

        dict_unref (xattr_req);

        return 0;

err:
        mq_xattr_creation_release_lock (frame, NULL, this, 0, 0, NULL);

        if (xattr_req)
                dict_unref (xattr_req);
        return 0;

lock_err:
        mq_inode_creation_done (frame, NULL, this, 0, 0, NULL);
        return 0;
}


int32_t
mq_set_inode_xattr (xlator_t *this, loc_t *loc)
{
        struct gf_flock  lock  = {0, };
        quota_local_t   *local = NULL;
        int32_t          ret   = 0;
        call_frame_t    *frame = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto err;
        }

        local = mq_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret < 0) {
                goto err;
        }

        frame->local = local;

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        STACK_WIND (frame,
                    mq_get_xattr,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->loc, F_SETLKW, &lock, NULL);

        return 0;

err:
        if (frame)
                QUOTA_STACK_DESTROY (frame, this);

        return 0;
}


int32_t
mq_get_parent_inode_local (xlator_t *this, quota_local_t *local)
{
        int32_t            ret = -1;
        quota_inode_ctx_t *ctx = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", local, out);

        local->contri = NULL;

        loc_wipe (&local->loc);

        ret = mq_loc_copy (&local->loc, &local->parent_loc);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "loc copy failed");
                goto out;
        }

        loc_wipe (&local->parent_loc);

        ret = mq_inode_loc_fill (NULL, local->loc.parent,
                                 &local->parent_loc);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "failed to build parent loc of %s",
                        local->loc.path);
                goto out;
        }

        ret = mq_inode_ctx_get (local->loc.inode, this, &ctx);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "inode ctx get failed");
                goto out;
        }

        local->ctx = ctx;

        if (list_empty (&ctx->contribution_head)) {
                gf_log_callingfn (this->name, GF_LOG_ERROR,
                        "contribution node list is empty");
                ret = -1;
                goto out;
        }

        /* Earlier we used to get the next entry in the list maintained
           by the context. In a good situation it works. i.e the next
           parent in the directory hierarchy for this path is obtained.

           But consider the below situation:
           mount-point: /mnt/point
           quota enabled directories within mount point: /a, /b, /c

           Now when some file (file1) in the directory /c is written some data,
           then to update the directories, marker has to get the contribution
           object for the parent inode, i.e /c.
           Beefore, it was being done by
           local->contri = (inode_contribution_t *) ctx->contribution_head.next;
           It works in the normal situations. But suppose /c is moved to /b.
           Now /b's contribution object is added to the end of the list of
           parents that the file file1 within /b/c is maintaining. Now if
           the file /b/c/file1 is copied to /b/c/new, to update the parent in
           the order c, b and / we cannot go to the next element in the list,
           as in this case the next contribution object would be / and /b's
           contribution will be at the end of the list. So get the proper
           parent's contribution, by searching the entire list.
        */
        local->contri = mq_get_contribution_node (local->loc.parent, ctx);
        GF_ASSERT (local->contri != NULL);

        ret = 0;
out:
        return ret;
}


int32_t
mq_xattr_updation_done (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno,
                        dict_t *dict, dict_t *xdata)
{
        QUOTA_STACK_DESTROY (frame, this);
        return 0;
}


int32_t
mq_inodelk_cbk (call_frame_t *frame, void *cookie,
                xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t         ret    = 0;
        gf_boolean_t    status = _gf_false;
        quota_local_t  *local  = NULL;

        local = frame->local;

        if (op_ret == -1 || local->err) {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unlocking failed on path (%s)(%s)",
                                local->parent_loc.path, strerror (op_errno));
                }
                mq_xattr_updation_done (frame, NULL, this, 0, 0, NULL, NULL);

                return 0;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "inodelk released on %s", local->parent_loc.path);

        if ((strcmp (local->parent_loc.path, "/") == 0)
            || (local->delta == 0)) {
                mq_xattr_updation_done (frame, NULL, this, 0, 0, NULL, NULL);
        } else {
                ret = mq_get_parent_inode_local (this, local);
                if (ret < 0) {
                        mq_xattr_updation_done (frame, NULL, this, 0, 0, NULL,
                                                NULL);
                        goto out;
                }
                status = _gf_true;

                ret = mq_test_and_set_ctx_updation_status (local->ctx, &status);
                if (ret == 0 && status == _gf_false) {
                        mq_get_lock_on_parent (frame, this);
                } else {
                        mq_xattr_updation_done (frame, NULL, this, 0, 0, NULL,
                                                NULL);
                }
        }
out:
        return 0;
}


//now release lock on the parent inode
int32_t
mq_release_parent_lock (call_frame_t *frame, void *cookie,
                        xlator_t *this, int32_t op_ret,
                        int32_t op_errno, dict_t *xdata)
{
        int32_t            ret      = 0;
        quota_local_t     *local    = NULL;
        quota_inode_ctx_t *ctx      = NULL;
        struct gf_flock    lock     = {0, };

        local = frame->local;

        if (local->err != 0) {
                gf_log_callingfn (this->name,
                                  (local->err == ENOENT) ? GF_LOG_DEBUG
                                  : GF_LOG_WARNING,
                                  "An operation during quota updation "
                                  "of path (%s) failed (%s)", local->loc.path,
                                  strerror (local->err));
        }

        ret = mq_inode_ctx_get (local->parent_loc.inode, this, &ctx);
        if (ret < 0)
                goto wind;

        LOCK (&ctx->lock);
        {
                ctx->dirty = 0;
        }
        UNLOCK (&ctx->lock);

        if (local->parent_loc.inode == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Invalid parent inode.");
                goto err;
        }

wind:
        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    mq_inodelk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc,
                    F_SETLKW, &lock, NULL);

        return 0;
err:
        mq_xattr_updation_done (frame, NULL, this,
                                0, 0 , NULL, NULL);
        return 0;
}


int32_t
mq_mark_undirty (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 dict_t *dict, dict_t *xdata)
{
        int32_t            ret          = -1;
        int64_t           *size         = NULL;
        dict_t            *newdict      = NULL;
        quota_local_t     *local        = NULL;
        quota_inode_ctx_t *ctx          = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING, "%s occurred while"
                        " updating the size of %s", strerror (op_errno),
                        local->parent_loc.path);

                goto err;
        }

        //update the size of the parent inode
        if (dict != NULL) {
                ret = mq_inode_ctx_get (local->parent_loc.inode, this, &ctx);
                if (ret < 0) {
                        op_errno = EINVAL;
                        goto err;
                }

                ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
                if (ret < 0) {
                        op_errno = EINVAL;
                        goto err;
                }

                LOCK (&ctx->lock);
                {
                        if (size)
                                ctx->size = ntoh64 (*size);
                        gf_log (this->name, GF_LOG_DEBUG, "%s %"PRId64,
                                local->parent_loc.path, ctx->size);
                }
                UNLOCK (&ctx->lock);
        }

        newdict = dict_new ();
        if (!newdict) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = dict_set_int8 (newdict, QUOTA_DIRTY_KEY, 0);

        if (ret == -1) {
                op_errno = -ret;
                goto err;
        }

        gf_uuid_copy (local->parent_loc.gfid, local->parent_loc.inode->gfid);
        GF_UUID_ASSERT (local->parent_loc.gfid);

        STACK_WIND (frame, mq_release_parent_lock,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    &local->parent_loc, newdict, 0, NULL);

        ret = 0;
err:
        if (op_ret == -1 || ret == -1) {
                local->err = op_errno;

                mq_release_parent_lock (frame, NULL, this, 0, 0, NULL);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}


int32_t
mq_update_parent_size (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       dict_t *dict, dict_t *xdata)
{
        int64_t             *size       = NULL;
        int32_t              ret        = -1;
        dict_t              *newdict    = NULL;
        quota_local_t       *local      = NULL;
        quota_inode_ctx_t   *ctx        = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "xattrop call failed: %s", strerror (op_errno));

                goto err;
        }

        LOCK (&local->contri->lock);
        {
                local->contri->contribution += local->delta;
        }
        UNLOCK (&local->contri->lock);

        gf_log_callingfn (this->name, GF_LOG_DEBUG, "path: %s size: %"PRId64
                          " contribution:%"PRId64,
                          local->loc.path, local->ctx->size,
                          local->contri->contribution);

        if (dict == NULL) {
                op_errno = EINVAL;
                goto err;
        }

        ret = mq_inode_ctx_get (local->parent_loc.inode, this, &ctx);
        if (ret < 0) {
                op_errno = EINVAL;
                goto err;
        }

        newdict = dict_new ();
        if (!newdict) {
                op_errno = ENOMEM;
                ret = -1;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (size, int64_t, ret, err);

        *size = hton64 (local->delta);

        ret = dict_set_bin (newdict, QUOTA_SIZE_KEY, size, 8);
        if (ret < 0) {
                op_errno = -ret;
                goto err;
        }

        if (gf_uuid_is_null (local->parent_loc.gfid))
                        gf_uuid_copy (local->parent_loc.gfid,
                                   local->parent_loc.inode->gfid);
        GF_UUID_ASSERT (local->parent_loc.gfid);

        STACK_WIND (frame,
                    mq_mark_undirty,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    &local->parent_loc,
                    GF_XATTROP_ADD_ARRAY64,
                    newdict, NULL);
        ret = 0;
err:
        if (op_ret == -1 || ret < 0) {
                local->err = op_errno;
                mq_release_parent_lock (frame, NULL, this, 0, 0, NULL);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
mq_update_inode_contribution (call_frame_t *frame, void *cookie,
                              xlator_t *this, int32_t op_ret,
                              int32_t op_errno, inode_t *inode,
                              struct iatt *buf, dict_t *dict,
                              struct iatt *postparent)
{
        int32_t               ret                         = -1;
        int64_t              *size                        = NULL;
        int64_t               size_int                    = 0;
        int64_t               contri_int                  = 0;
        int64_t              *contri                      = NULL;
        int64_t              *delta                       = NULL;
        char                  contri_key[CONTRI_KEY_MAX]  = {0, };
        dict_t               *newdict                     = NULL;
        quota_local_t        *local                       = NULL;
        quota_inode_ctx_t    *ctx                         = NULL;
        inode_contribution_t *contribution                = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "failed to get size and contribution of path (%s)(%s)",
                        local->loc.path, strerror (op_errno));
                goto err;
        }

        ctx = local->ctx;
        contribution = local->contri;

        //prepare to update size & contribution of the inode
        GET_CONTRI_KEY (contri_key, contribution->gfid, ret);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto err;
        }

        LOCK (&ctx->lock);
        {
                if (local->loc.inode->ia_type == IA_IFDIR ) {
                        ret = dict_get_bin (dict, QUOTA_SIZE_KEY,
                                            (void **) &size);
                        if (ret < 0) {
                                op_errno = EINVAL;
                                goto unlock;
                        }

                        ctx->size = ntoh64 (*size);
                } else
                        ctx->size = buf->ia_blocks * 512;

                size_int = ctx->size;
        }
unlock:
        UNLOCK  (&ctx->lock);

        if (ret < 0) {
                goto err;
        }

        ret = dict_get_bin (dict, contri_key, (void **) &contri);

        LOCK (&contribution->lock);
        {
                if (ret < 0)
                        contribution->contribution = 0;
                else
                        contribution->contribution = ntoh64 (*contri);

                contri_int = contribution->contribution;
        }
        UNLOCK (&contribution->lock);

        gf_log (this->name, GF_LOG_DEBUG, "%s %"PRId64 " %"PRId64,
                local->loc.path, size_int, contri_int);

        local->delta = size_int - contri_int;

        if (local->delta == 0) {
                mq_mark_undirty (frame, NULL, this, 0, 0, NULL, NULL);
                return 0;
        }

        newdict = dict_new ();
        if (newdict == NULL) {
                op_errno = ENOMEM;
                ret = -1;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (delta, int64_t, ret, err);

        *delta = hton64 (local->delta);

        ret = dict_set_bin (newdict, contri_key, delta, 8);
        if (ret < 0) {
                op_errno = -ret;
                ret = -1;
                goto err;
        }

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        GF_UUID_ASSERT (local->loc.gfid);

        STACK_WIND (frame,
                    mq_update_parent_size,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    &local->loc,
                    GF_XATTROP_ADD_ARRAY64,
                    newdict, NULL);
        ret = 0;

err:
        if (op_ret == -1 || ret < 0) {
                local->err = op_errno;

                mq_release_parent_lock (frame, NULL, this, 0, 0, NULL);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
mq_fetch_child_size_and_contri (call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, dict_t *xdata)
{
        int32_t            ret                         = -1;
        char               contri_key[CONTRI_KEY_MAX]  = {0, };
        dict_t            *newdict                     = NULL;
        quota_local_t     *local                       = NULL;
        quota_inode_ctx_t *ctx                         = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, (op_errno == ENOENT) ? GF_LOG_DEBUG
                        : GF_LOG_WARNING,
                        "couldnt mark inode corresponding to path (%s) dirty "
                        "(%s)", local->parent_loc.path, strerror (op_errno));
                goto err;
        }

        VALIDATE_OR_GOTO (local->ctx, err);
        VALIDATE_OR_GOTO (local->contri, err);

        gf_log (this->name, GF_LOG_DEBUG, "%s marked dirty",
                local->parent_loc.path);

        //update parent ctx
        ret = mq_inode_ctx_get (local->parent_loc.inode, this, &ctx);
        if (ret == -1) {
                op_errno = EINVAL;
                goto err;
        }

        LOCK (&ctx->lock);
        {
                ctx->dirty = 1;
        }
        UNLOCK (&ctx->lock);

        newdict = dict_new ();
        if (newdict == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        if (local->loc.inode->ia_type == IA_IFDIR) {
                ret = dict_set_int64 (newdict, QUOTA_SIZE_KEY, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dict_set failed.");
                        goto err;
                }
        }

        GET_CONTRI_KEY (contri_key, local->contri->gfid, ret);
        if (ret < 0) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = dict_set_int64 (newdict, contri_key, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dict_set failed.");
                goto err;
        }

        mq_set_ctx_updation_status (local->ctx, _gf_false);

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        GF_UUID_ASSERT (local->loc.gfid);

        STACK_WIND (frame, mq_update_inode_contribution, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->loc, newdict);

        ret = 0;

err:
        if ((op_ret == -1) || (ret < 0)) {
                local->err = op_errno;

                mq_set_ctx_updation_status (local->ctx, _gf_false);

                mq_release_parent_lock (frame, NULL, this, 0, 0, NULL);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
mq_markdirty (call_frame_t *frame, void *cookie,
              xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t        ret      = -1;
        dict_t        *dict     = NULL;
        quota_local_t *local    = NULL;

        local = frame->local;

        if (op_ret == -1){
                gf_log (this->name, (op_errno == ENOENT) ? GF_LOG_DEBUG
                        : GF_LOG_WARNING, "acquiring locks failed on %s (%s)",
                        local->parent_loc.path, strerror (op_errno));

                local->err = op_errno;

                mq_set_ctx_updation_status (local->ctx, _gf_false);

                mq_inodelk_cbk (frame, NULL, this, 0, 0, NULL);

                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "inodelk succeeded on  %s", local->parent_loc.path);

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto err;
        }

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, 1);
        if (ret == -1)
                goto err;

        gf_uuid_copy (local->parent_loc.gfid,
                   local->parent_loc.inode->gfid);
        GF_UUID_ASSERT (local->parent_loc.gfid);

        STACK_WIND (frame, mq_fetch_child_size_and_contri,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    &local->parent_loc, dict, 0, NULL);

        ret = 0;
err:
        if (ret == -1) {
                local->err = 1;

                mq_set_ctx_updation_status (local->ctx, _gf_false);

                mq_release_parent_lock (frame, NULL, this, 0, 0, NULL);
        }

        if (dict)
                dict_unref (dict);

        return 0;
}


int32_t
mq_get_lock_on_parent (call_frame_t *frame, xlator_t *this)
{
        struct gf_flock  lock  = {0, };
        quota_local_t   *local = NULL;

        GF_VALIDATE_OR_GOTO ("marker", frame, fr_destroy);

        local = frame->local;
        gf_log (this->name, GF_LOG_DEBUG, "taking lock on %s",
                local->parent_loc.path);

        if (local->parent_loc.inode == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "parent inode is not valid, aborting "
                        "transaction.");
                goto fr_destroy;
        }

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        STACK_WIND (frame,
                    mq_markdirty,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc, F_SETLKW, &lock, NULL);

        return 0;

fr_destroy:
        QUOTA_STACK_DESTROY (frame, this);

        return -1;
}

int
mq_prepare_txn_frame (xlator_t *this, loc_t *loc,
                      quota_inode_ctx_t *ctx,
                      inode_contribution_t *contri,
                      call_frame_t **new_frame)
{
        call_frame_t    *frame = NULL;
        int              ret   = -1;
        quota_local_t   *local = NULL;

        if (!this || !loc || !new_frame)
                goto err;

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL)
                goto err;

        mq_assign_lk_owner (this, frame);

        local = mq_local_new ();
        if (local == NULL)
                goto fr_destroy;

        frame->local = local;

        ret = mq_loc_copy (&local->loc, loc);
        if (ret < 0)
                goto fr_destroy;

        ret = mq_inode_loc_fill (NULL, local->loc.parent,
                                 &local->parent_loc);
        if (ret < 0)
                goto fr_destroy;

        local->ctx = ctx;
        if (contri) {
                local->contri = contri;
                GF_REF_GET (local->contri);
        }

        ret = 0;
        *new_frame = frame;

        return ret;

fr_destroy:
        QUOTA_STACK_DESTROY (frame, this);
err:
        return ret;
}

int
mq_start_quota_txn (xlator_t *this, loc_t *loc,
                    quota_inode_ctx_t *ctx,
                    inode_contribution_t *contri)
{
        int32_t        ret      = -1;
        call_frame_t  *frame    = NULL;

        ret = mq_prepare_txn_frame (this, loc, ctx,
                                    contri, &frame);
        if (ret)
                goto err;

        ret = mq_get_lock_on_parent (frame, this);
        if (ret == -1)
                goto err;

        return 0;

err:
        mq_set_ctx_updation_status (ctx, _gf_false);

        return -1;
}

int32_t
_quota_dict_get_meta (xlator_t *this, dict_t *dict, char *key,
                      quota_meta_t *meta, ia_type_t ia_type,
                      gf_boolean_t add_delta)
{
        int32_t             ret     = 0;
        marker_conf_t      *priv    = NULL;

        priv = this->private;

        ret = quota_dict_get_meta (dict, key, meta);
        if (ret == -2 && (priv->feature_enabled & GF_INODE_QUOTA) == 0) {
                /* quota_dict_get_meta returns -2 if
                 * inode quota xattrs are not present.
                 * if inode quota self heal is turned off,
                 * then we should skip healing inode quotas
                 */

                gf_log (this->name, GF_LOG_DEBUG, "inode quota disabled. "
                        "inode quota self heal will not be performed");
                ret = 0;
                if (add_delta) {
                        if (ia_type == IA_IFDIR)
                                meta->dir_count = 1;
                        else
                                meta->file_count = 1;
                }
        }

        return ret;
}

void
mq_compute_delta (quota_meta_t *delta, const quota_meta_t *op1,
                  const quota_meta_t *op2)
{
        delta->size       = op1->size - op2->size;
        delta->file_count = op1->file_count - op2->file_count;
        delta->dir_count  = op1->dir_count - op2->dir_count;
}

void
mq_add_meta (quota_meta_t *dst, const quota_meta_t *src)
{
        dst->size       += src->size;
        dst->file_count += src->file_count;
        dst->dir_count  += src->dir_count;
}

void
mq_sub_meta (quota_meta_t *dst, const quota_meta_t *src)
{
        if (src == NULL) {
                dst->size       = -dst->size;
                dst->file_count = -dst->file_count;
                dst->dir_count  = -dst->dir_count;
        } else {
                dst->size       = src->size - dst->size;
                dst->file_count = src->file_count - dst->file_count;
                dst->dir_count  = src->dir_count - dst->dir_count;
        }
}

gf_boolean_t
quota_meta_is_null (const quota_meta_t *meta)
{
        if (meta->size == 0 &&
            meta->file_count == 0 &&
            meta->dir_count == 0)
                return _gf_true;

        return _gf_false;
}

int32_t
mq_are_xattrs_set (xlator_t *this, loc_t *loc, gf_boolean_t *result,
                   gf_boolean_t *objects)
{
        int32_t        ret                         = -1;
        char           contri_key[CONTRI_KEY_MAX]  = {0, };
        quota_meta_t   meta                        = {0, };
        struct iatt    stbuf                       = {0,};
        dict_t        *dict                        = NULL;
        dict_t        *rsp_dict                    = NULL;

        dict = dict_new ();
        if (dict == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        ret = mq_req_xattr (this, loc, dict, contri_key);
        if (ret < 0)
                goto out;

        ret = syncop_lookup (FIRST_CHILD(this), loc, &stbuf, NULL,
                             dict, &rsp_dict);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "lookup failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        if (rsp_dict == NULL) {
                *result = _gf_false;
                goto out;
        }

        *result = _gf_true;
        if (loc->inode->ia_type == IA_IFDIR) {
                ret = _quota_dict_get_meta (this, rsp_dict, QUOTA_SIZE_KEY,
                                            &meta, IA_IFDIR, _gf_false);
                if (ret < 0 || meta.dir_count == 0) {
                        ret = 0;
                        *result = _gf_false;
                        goto out;
                }
                *objects = _gf_true;
        }

        if (!loc_is_root(loc)) {
                ret = _quota_dict_get_meta (this, rsp_dict, contri_key,
                                            &meta, IA_IFREG, _gf_false);
                if (ret < 0) {
                        ret = 0;
                        *result = _gf_false;
                        goto out;
                }
        }

out:
        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

int32_t
mq_create_xattrs (xlator_t *this, quota_inode_ctx_t *ctx, loc_t *loc,
                  gf_boolean_t objects)
{
        quota_meta_t           size                 = {0, };
        quota_meta_t           contri               = {0, };
        int32_t                ret                  = -1;
        char                   key[CONTRI_KEY_MAX]  = {0, };
        dict_t                *dict                 = NULL;
        inode_contribution_t  *contribution         = NULL;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -1;
                goto out;
        }

        if (loc->inode->ia_type == IA_IFDIR) {
                if (objects == _gf_false) {
                        /* Initial object count of a directory is 1 */
                        size.dir_count = 1;
                }
                ret = quota_dict_set_meta (dict, QUOTA_SIZE_KEY, &size,
                                           IA_IFDIR);
                if (ret < 0)
                        goto out;
        }

        if (!loc_is_root (loc)) {
                contribution = mq_add_new_contribution_node (this, ctx, loc);
                if (contribution == NULL) {
                        ret = -1;
                        goto out;
                }

                GET_CONTRI_KEY (key, contribution->gfid, ret);
                ret = quota_dict_set_meta (dict, key, &contri,
                                           loc->inode->ia_type);
                if (ret < 0)
                        goto out;
        }

        ret = syncop_xattrop(FIRST_CHILD(this), loc, GF_XATTROP_ADD_ARRAY64,
                             dict, NULL, NULL);

        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "xattrop failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

out:
        if (dict)
                dict_unref (dict);

        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_lock (xlator_t *this, loc_t *loc, short l_type)
{
        struct gf_flock  lock  = {0, };
        int32_t          ret   = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        gf_log (this->name, GF_LOG_DEBUG, "set lock type %d on %s",
                l_type, loc->path);

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = l_type;
        lock.l_whence = SEEK_SET;

        ret = syncop_inodelk (FIRST_CHILD(this), this->name, loc, F_SETLKW,
                              &lock, NULL, NULL);
        if (ret < 0)
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "inodelk failed "
                        "for %s: %s", loc->path, strerror (-ret));

out:

        return ret;
}

int32_t
mq_get_dirty (xlator_t *this, loc_t *loc, int32_t *dirty)
{
        int32_t        ret              = -1;
        int8_t         value            = 0;
        dict_t        *dict             = NULL;
        dict_t        *rsp_dict         = NULL;
        struct iatt    stbuf            = {0,};

        dict = dict_new ();
        if (dict == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        ret = dict_set_int64 (dict, QUOTA_DIRTY_KEY, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "dict set failed");
                goto out;
        }

        ret = syncop_lookup (FIRST_CHILD(this), loc, &stbuf, NULL,
                             dict, &rsp_dict);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "lookup failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        ret = dict_get_int8 (rsp_dict, QUOTA_DIRTY_KEY, &value);
        if (ret < 0)
                goto out;

        *dirty = value;

out:
        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

int32_t
mq_mark_dirty (xlator_t *this, loc_t *loc, int32_t dirty)
{
        int32_t            ret      = -1;
        dict_t            *dict     = NULL;
        quota_inode_ctx_t *ctx      = NULL;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, dirty);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "dict_set failed");
                goto out;
        }

        ret = syncop_setxattr (FIRST_CHILD(this), loc, dict, 0, NULL, NULL);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "setxattr dirty = %d "
                        "failed for %s: %s", dirty, loc->path, strerror (-ret));
                goto out;
        }

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode ctx for "
                        "%s", loc->path);
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->dirty = dirty;
        }
        UNLOCK (&ctx->lock);

out:
        if (dict)
                dict_unref (dict);

        return ret;
}

int32_t
_mq_get_metadata (xlator_t *this, loc_t *loc, quota_meta_t *contri,
                  quota_meta_t *size, uuid_t contri_gfid)
{
        int32_t            ret                         = -1;
        quota_meta_t       meta                        = {0, };
        char               contri_key[CONTRI_KEY_MAX]  = {0, };
        dict_t            *dict                        = NULL;
        dict_t            *rsp_dict                    = NULL;
        struct iatt        stbuf                       = {0,};

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        if (size == NULL && contri == NULL)
                goto out;

        dict = dict_new ();
        if (dict == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        if (size && loc->inode->ia_type == IA_IFDIR) {
                ret = dict_set_int64 (dict, QUOTA_SIZE_KEY, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "dict_set failed.");
                        goto out;
                }
        }

        if (contri && !loc_is_root(loc)) {
                ret = mq_dict_set_contribution (this, dict, loc, contri_gfid,
                                                contri_key);
                if (ret < 0)
                        goto out;
        }

        ret = syncop_lookup (FIRST_CHILD(this), loc, &stbuf, NULL,
                             dict, &rsp_dict);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "lookup failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        if (size) {
                if (loc->inode->ia_type == IA_IFDIR) {
                        ret = _quota_dict_get_meta (this, rsp_dict,
                                                    QUOTA_SIZE_KEY, &meta,
                                                    IA_IFDIR, _gf_true);
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "dict_get failed.");
                                goto out;
                        }

                        size->size = meta.size;
                        size->file_count = meta.file_count;
                        size->dir_count = meta.dir_count;
                } else {
                        size->size = stbuf.ia_blocks * 512;
                        size->file_count = 1;
                        size->dir_count = 0;
                }
        }

        if (contri && !loc_is_root(loc)) {
                ret = _quota_dict_get_meta (this, rsp_dict, contri_key, &meta,
                                            loc->inode->ia_type, _gf_false);
                if (ret < 0) {
                        contri->size = 0;
                        contri->file_count = 0;
                        contri->dir_count = 0;
                } else {
                        contri->size = meta.size;
                        contri->file_count = meta.file_count;
                        contri->dir_count = meta.dir_count;
                }
        }

        ret = 0;

out:
        if (dict)
                dict_unref (dict);

        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}

int32_t
mq_get_metadata (xlator_t *this, loc_t *loc, quota_meta_t *contri,
                 quota_meta_t *size, quota_inode_ctx_t *ctx,
                 inode_contribution_t *contribution)
{
        int32_t         ret      = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", contribution, out);

        if (size == NULL && contri == NULL) {
                ret = 0;
                goto out;
        }

        ret = _mq_get_metadata (this, loc, contri, size, contribution->gfid);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Failed to get "
                                  "metadata for %s", loc->path);
                goto out;
        }

        if (size) {
                LOCK (&ctx->lock);
                {
                        ctx->size = size->size;
                        ctx->file_count = size->file_count;
                        ctx->dir_count = size->dir_count;
                }
                UNLOCK  (&ctx->lock);
        }

        if (contri) {
                LOCK (&contribution->lock);
                {
                        contribution->contribution = contri->size;
                        contribution->file_count = contri->file_count;
                        contribution->dir_count = contri->dir_count;
                }
                UNLOCK (&contribution->lock);
        }

out:
        return ret;
}

int32_t
mq_get_size (xlator_t *this, loc_t *loc, quota_meta_t *size)
{
        int32_t         ret      = -1;

        ret = _mq_get_metadata (this, loc, NULL, size, 0);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Failed to get "
                                  "metadata for %s", loc->path);
                goto out;
        }

out:
        return ret;
}

int32_t
mq_get_contri (xlator_t *this, loc_t *loc, quota_meta_t *contri,
               uuid_t contri_gfid)
{
        int32_t         ret      = -1;

        ret = _mq_get_metadata (this, loc, contri, NULL, contri_gfid);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Failed to get "
                                  "metadata for %s", loc->path);
                goto out;
        }

out:
        return ret;
}

int32_t
mq_get_delta (xlator_t *this, loc_t *loc, quota_meta_t *delta,
              quota_inode_ctx_t *ctx, inode_contribution_t *contribution)
{
        int32_t         ret      = -1;
        quota_meta_t    size     = {0, };
        quota_meta_t    contri   = {0, };

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", contribution, out);

        ret = mq_get_metadata (this, loc, &contri, &size, ctx, contribution);
        if (ret < 0)
                goto out;

        mq_compute_delta (delta, &size, &contri);

out:
        return ret;
}

int32_t
mq_remove_contri (xlator_t *this, loc_t *loc, quota_inode_ctx_t *ctx,
                  inode_contribution_t *contri, quota_meta_t *delta,
                  gf_boolean_t remove_xattr)
{
        int32_t              ret                         = -1;
        char                 contri_key[CONTRI_KEY_MAX]  = {0, };

        if (remove_xattr == _gf_false)
                goto done;

        GET_CONTRI_KEY (contri_key, contri->gfid, ret);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "get contri_key "
                        "failed for %s", uuid_utoa(contri->gfid));
                goto out;
        }

        ret = syncop_removexattr (FIRST_CHILD(this), loc, contri_key, 0, NULL);
        if (ret < 0) {
                if (-ret == ENOENT || -ret == ESTALE || -ret == ENODATA ||
                    -ret == ENOATTR) {
                        /* Remove contri in done when unlink operation is
                         * performed, so return success on ENOENT/ESTSLE
                         * rename operation removes xattr earlier,
                         * so return success on ENODATA
                         */
                        ret = 0;
                } else {
                        gf_log (this->name, GF_LOG_ERROR, "removexattr %s "
                                "failed for %s: %s", contri_key, loc->path,
                                strerror (-ret));
                        goto out;
                }
        }

done:
        LOCK (&contri->lock);
        {
                contri->contribution += delta->size;
                contri->file_count += delta->file_count;
                contri->dir_count += delta->dir_count;
        }
        UNLOCK (&contri->lock);

        ret = 0;

out:
        QUOTA_FREE_CONTRIBUTION_NODE (ctx, contri);

        return ret;
}

int32_t
mq_update_contri (xlator_t *this, loc_t *loc, inode_contribution_t *contri,
                  quota_meta_t *delta)
{
        int32_t              ret                         = -1;
        char                 contri_key[CONTRI_KEY_MAX]  = {0, };
        dict_t              *dict                        = NULL;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);
        GF_VALIDATE_OR_GOTO ("marker", delta, out);
        GF_VALIDATE_OR_GOTO ("marker", contri, out);

        if (quota_meta_is_null (delta)) {
                ret = 0;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -1;
                goto out;
        }

        GET_CONTRI_KEY (contri_key, contri->gfid, ret);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "get contri_key "
                        "failed for %s", uuid_utoa(contri->gfid));
                goto out;
        }

        ret = quota_dict_set_meta (dict, contri_key, delta,
                                   loc->inode->ia_type);
        if (ret < 0)
                goto out;

        ret = syncop_xattrop(FIRST_CHILD(this), loc, GF_XATTROP_ADD_ARRAY64,
                             dict, NULL, NULL);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "xattrop failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        LOCK (&contri->lock);
        {
                contri->contribution += delta->size;
                contri->file_count += delta->file_count;
                contri->dir_count += delta->dir_count;
        }
        UNLOCK (&contri->lock);

out:
        if (dict)
                dict_unref (dict);

        return ret;
}

int32_t
mq_update_size (xlator_t *this, loc_t *loc, quota_meta_t *delta)
{
        int32_t              ret              = -1;
        quota_inode_ctx_t   *ctx              = NULL;
        dict_t              *dict             = NULL;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);
        GF_VALIDATE_OR_GOTO ("marker", delta, out);

        if (quota_meta_is_null (delta)) {
                ret = 0;
                goto out;
        }

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode ctx for "
                        "%s", loc->path);
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -1;
                goto out;
        }

        ret = quota_dict_set_meta (dict, QUOTA_SIZE_KEY, delta,
                                   loc->inode->ia_type);
        if (ret < 0)
                goto out;

        ret = syncop_xattrop(FIRST_CHILD(this), loc, GF_XATTROP_ADD_ARRAY64,
                             dict, NULL, NULL);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "xattrop failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->size += delta->size;
                ctx->file_count += delta->file_count;
                ctx->dir_count += delta->dir_count;
        }
        UNLOCK (&ctx->lock);

out:
        if (dict)
                dict_unref (dict);

        return ret;
}

int
mq_synctask_cleanup (int ret, call_frame_t *frame, void *opaque)
{
        quota_synctask_t       *args         = NULL;

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc_wipe (&args->loc);

        if (!args->is_static)
                GF_FREE (args);

        return 0;
}

int
mq_synctask (xlator_t *this, synctask_fn_t task, gf_boolean_t spawn, loc_t *loc,
             int64_t contri)
{
        int32_t              ret         = -1;
        quota_synctask_t    *args        = NULL;
        quota_synctask_t     static_args = {0, };

        if (spawn) {
                QUOTA_ALLOC_OR_GOTO (args, quota_synctask_t, ret, out);
                args->is_static = _gf_false;
        } else {
                args = &static_args;
                args->is_static = _gf_true;
        }

        args->this = this;
        loc_copy (&args->loc, loc);
        args->contri = contri;

        if (spawn) {
                ret = synctask_new1 (this->ctx->env, 1024 * 16, task,
                                      mq_synctask_cleanup, NULL, args);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to spawn "
                                "new synctask");
                        mq_synctask_cleanup (ret, NULL, args);
                }
        } else {
                ret = task (args);
                mq_synctask_cleanup (ret, NULL, args);
        }

out:
        return ret;
}

int32_t
mq_prevalidate_txn (xlator_t *this, loc_t *origin_loc, loc_t *loc,
                    quota_inode_ctx_t **ctx)
{
        int32_t               ret     = -1;
        quota_inode_ctx_t    *ctxtmp  = NULL;

        if (origin_loc == NULL || origin_loc->inode == NULL ||
            (gf_uuid_is_null(origin_loc->gfid) &&
             gf_uuid_is_null(origin_loc->inode->gfid)))
                goto out;

        loc_copy (loc, origin_loc);

        if (gf_uuid_is_null (loc->gfid))
                gf_uuid_copy (loc->gfid, loc->inode->gfid);

        if (ctx)
                ret = mq_inode_ctx_get (loc->inode, this, ctx);
        else
                ret = mq_inode_ctx_get (loc->inode, this, &ctxtmp);

        if (ret < 0) {
                if (ctx) {
                        *ctx = mq_inode_ctx_new (loc->inode, this);
                        if (*ctx == NULL) {
                                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                                  "mq_inode_ctx_new failed for "
                                                  "%s", loc->path);
                                ret = -1;
                                goto out;
                        }
                } else {
                        gf_log_callingfn (this->name, GF_LOG_WARNING, "ctx for "
                                          "is NULL for %s", loc->path);
                }
        }

        ret = 0;
out:
        return ret;
}

int
mq_start_quota_txn_v2 (xlator_t *this, loc_t *loc, quota_inode_ctx_t *ctx,
                       inode_contribution_t *contri)
{
        int32_t            ret        = -1;
        loc_t              child_loc  = {0,};
        loc_t              parent_loc = {0,};
        gf_boolean_t       locked     = _gf_false;
        gf_boolean_t       dirty      = _gf_false;
        gf_boolean_t       status     = _gf_false;
        quota_meta_t       delta      = {0, };

        GF_VALIDATE_OR_GOTO ("marker", contri, out);
        GF_REF_GET (contri);

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        ret = mq_loc_copy (&child_loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "loc copy failed");
                goto out;
        }

        while (!__is_root_gfid (child_loc.gfid)) {
                /* To improve performance, abort current transaction
                 * if one is already in progress for same inode
                 */
                if (status == _gf_true) {
                        /* status will alreday set before txn start,
                         * so it should not be set in first
                         * loop iteration
                         */
                        ret = mq_test_and_set_ctx_updation_status (ctx,
                                                                   &status);
                        if (ret < 0 || status == _gf_true)
                                goto out;
                }

                ret = mq_inode_loc_fill (NULL, child_loc.parent, &parent_loc);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "loc fill failed");
                        goto out;
                }

                ret = mq_lock (this, &parent_loc, F_WRLCK);
                if (ret < 0)
                        goto out;
                locked = _gf_true;

                mq_set_ctx_updation_status (ctx, _gf_false);
                status = _gf_true;

                ret = mq_get_delta (this, &child_loc, &delta, ctx, contri);
                if (ret < 0)
                        goto out;

                if (quota_meta_is_null (&delta))
                        goto out;

                ret = mq_mark_dirty (this, &parent_loc, 1);
                if (ret < 0)
                        goto out;
                dirty = _gf_true;

                ret = mq_update_contri (this, &child_loc, contri, &delta);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "contri "
                                "update failed for %s", child_loc.path);
                        goto out;
                }

                ret = mq_update_size (this, &parent_loc, &delta);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING, "rollback "
                                "contri updation");
                        mq_sub_meta (&delta, NULL);
                        mq_update_contri (this, &child_loc, contri, &delta);
                        goto out;
                }

                ret = mq_mark_dirty (this, &parent_loc, 0);
                dirty = _gf_false;

                ret = mq_lock (this, &parent_loc, F_UNLCK);
                locked = _gf_false;

                if (__is_root_gfid (parent_loc.gfid))
                        break;

                /* Repeate above steps upwards till the root */
                loc_wipe (&child_loc);
                ret = mq_loc_copy (&child_loc, &parent_loc);
                if (ret < 0)
                        goto out;
                loc_wipe (&parent_loc);

                ret = mq_inode_ctx_get (child_loc.inode, this, &ctx);
                if (ret < 0)
                        goto out;

                if (list_empty (&ctx->contribution_head)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "contribution node list is empty (%s)",
                                uuid_utoa(child_loc.inode->gfid));
                        ret = -1;
                        goto out;
                }

                GF_REF_PUT (contri);
                contri = mq_get_contribution_node (child_loc.parent, ctx);
                GF_ASSERT (contri != NULL);
        }

out:
        if (ret >= 0 && dirty)
                ret = mq_mark_dirty (this, &parent_loc, 0);

        if (locked)
                ret = mq_lock (this, &parent_loc, F_UNLCK);

        if (ctx && status == _gf_false)
                mq_set_ctx_updation_status (ctx, _gf_false);

        loc_wipe (&child_loc);
        loc_wipe (&parent_loc);
        if (contri)
                GF_REF_PUT (contri);

        return 0;
}

int
mq_create_xattrs_task (void *opaque)
{
        int32_t                  ret        = -1;
        gf_boolean_t             locked     = _gf_false;
        gf_boolean_t             xattrs_set = _gf_false;
        gf_boolean_t             objects    = _gf_false;
        gf_boolean_t             need_txn   = _gf_false;
        quota_synctask_t        *args       = NULL;
        quota_inode_ctx_t       *ctx        = NULL;
        xlator_t                *this       = NULL;
        loc_t                   *loc        = NULL;
        gf_boolean_t             status     = _gf_false;

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        this = args->this;
        THIS = this;

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to"
                        "get inode ctx, aborting quota create txn");
                goto out;
        }

        if (loc->inode->ia_type == IA_IFDIR) {
                /* lock not required for files */
                ret = mq_lock (this, loc, F_WRLCK);
                if (ret < 0)
                        goto out;
                locked = _gf_true;
        }

        ret = mq_are_xattrs_set (this, loc, &xattrs_set, &objects);
        if (ret < 0 || xattrs_set)
                goto out;

        mq_set_ctx_create_status (ctx, _gf_false);
        status = _gf_true;

        ret = mq_create_xattrs (this, ctx, loc, objects);
        if (ret < 0)
                goto out;

        need_txn = _gf_true;
out:
        if (locked)
                ret = mq_lock (this, loc, F_UNLCK);

        if (status == _gf_false)
                mq_set_ctx_create_status (ctx, _gf_false);

        if (need_txn)
                ret = mq_initiate_quota_blocking_txn (this, loc);

        return ret;
}

static int
_mq_create_xattrs_txn (xlator_t *this, loc_t *origin_loc, gf_boolean_t spawn)
{
        int32_t                  ret        = -1;
        quota_inode_ctx_t       *ctx        = NULL;
        gf_boolean_t             status     = _gf_true;
        loc_t                    loc        = {0, };

        ret = mq_prevalidate_txn (this, origin_loc, &loc, &ctx);
        if (ret < 0)
                goto out;

        ret = mq_test_and_set_ctx_create_status (ctx, &status);
        if (ret < 0 || status == _gf_true)
                goto out;

        ret = mq_synctask (this, mq_create_xattrs_task, spawn, &loc, 0);
out:
        if (ret < 0 && status == _gf_false)
                mq_set_ctx_create_status (ctx, _gf_false);

        loc_wipe (&loc);
        return ret;
}

int
mq_create_xattrs_txn (xlator_t *this, loc_t *loc)
{
        int32_t                  ret        = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_create_xattrs_txn (this, loc, _gf_true);
out:
        return ret;
}

int
mq_create_xattrs_blocking_txn (xlator_t *this, loc_t *loc)
{
        int32_t                  ret        = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_create_xattrs_txn (this, loc, _gf_false);
out:
        return ret;
}

int32_t
mq_reduce_parent_size_task (void *opaque)
{
        int32_t                  ret           = -1;
        quota_inode_ctx_t       *ctx           = NULL;
        inode_contribution_t    *contribution  = NULL;
        quota_meta_t             delta         = {0, };
        loc_t                    parent_loc    = {0,};
        gf_boolean_t             locked        = _gf_false;
        gf_boolean_t             dirty         = _gf_false;
        quota_synctask_t        *args          = NULL;
        xlator_t                *this          = NULL;
        loc_t                   *loc           = NULL;
        int64_t                  contri        = 0;
        gf_boolean_t             remove_xattr  = _gf_true;

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        contri = args->contri;
        this = args->this;
        THIS = this;

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING, "ctx for"
                                  " the node %s is NULL", loc->path);
                goto out;
        }

        contribution = mq_get_contribution_node (loc->parent, ctx);
        if (contribution == NULL) {
                ret = -1;
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "contribution for the node %s is NULL",
                                  loc->path);
                goto out;
        }

        if (contri >= 0) {
                /* contri paramater is supplied only for rename operation.
                 * remove xattr is alreday performed, we need to skip
                 * removexattr for rename operation
                 */
                remove_xattr = _gf_false;
                delta.size = contri;
                delta.file_count = 1;
                delta.dir_count = 0;
        }

        ret = mq_inode_loc_fill (NULL, loc->parent, &parent_loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "loc fill failed");
                goto out;
        }

        ret = mq_lock (this, &parent_loc, F_WRLCK);
        if (ret < 0)
                goto out;
        locked = _gf_true;

        if (contri < 0) {
                LOCK (&contribution->lock);
                {
                        delta.size = contribution->contribution;
                        delta.file_count = contribution->file_count;
                        delta.dir_count = contribution->dir_count;
                }
                UNLOCK (&contribution->lock);
        }

        /* TODO: Handle handlinks with better approach
           Iterating dentry_list without a lock is not a good idea
        if (loc->inode->ia_type != IA_IFDIR) {
                list_for_each_entry (dentry, &inode->dentry_list, inode_list) {
                        if (loc->parent == dentry->parent) {
                                * If the file has another link within the same
                                * directory, we should not be reducing the size
                                * of parent
                                *
                                delta = 0;
                                idelta = 0;
                                break;
                        }
                }
        }
        */

        if (quota_meta_is_null (&delta))
                goto out;

        ret = mq_mark_dirty (this, &parent_loc, 1);
        if (ret < 0)
                goto out;
        dirty = _gf_true;

        mq_sub_meta (&delta, NULL);

        ret = mq_remove_contri (this, loc, ctx, contribution, &delta,
                                remove_xattr);
        if (ret < 0)
                goto out;

        ret = mq_update_size (this, &parent_loc, &delta);
        if (ret < 0)
                goto out;

out:
        if (dirty && ret >= 0)
                ret = mq_mark_dirty (this, &parent_loc, 0);

        if (locked)
                ret = mq_lock (this, &parent_loc, F_UNLCK);

        if (ret >= 0)
                ret = mq_initiate_quota_blocking_txn (this, &parent_loc);

        loc_wipe (&parent_loc);

        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_reduce_parent_size_txn (xlator_t *this, loc_t *origin_loc, int64_t contri)
{
        int32_t                  ret           = -1;
        loc_t                    loc           = {0, };

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", origin_loc, out);

        ret = mq_prevalidate_txn (this, origin_loc, &loc, NULL);
        if (ret < 0)
                goto out;

        if (loc_is_root(&loc)) {
                ret = 0;
                goto out;
        }

        if (loc.parent == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "parent is NULL for %s, "
                        "aborting reduce parent size txn", loc.path);
                goto out;
        }

        ret = mq_synctask (this, mq_reduce_parent_size_task, _gf_true, &loc,
                           contri);
out:
        loc_wipe (&loc);
        return ret;
}

int
mq_initiate_quota_task (void *opaque)
{
        int32_t                 ret          = -1;
        quota_inode_ctx_t      *ctx          = NULL;
        inode_contribution_t   *contribution = NULL;
        quota_synctask_t       *args         = NULL;
        xlator_t               *this         = NULL;
        loc_t                  *loc          = NULL;

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        this = args->this;
        THIS = this;

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "inode ctx get failed, aborting quota txn");
                goto out;
        }

        /* Create the contribution node if its absent. Is it right to
           assume that if the contribution node is not there, then
           create one and proceed instead of returning?
           Reason for this assumption is for hard links. Suppose
           hard link for a file f1 present in a directory d1 is
           created in the directory d2 (as f2). Now, since d2's
           contribution is not there in f1's inode ctx, d2's
           contribution xattr wont be created and will create problems
           for quota operations.
        */
        contribution = mq_get_contribution_node (loc->parent, ctx);
        if (!contribution) {
                if (!loc_is_root(loc))
                        gf_log_callingfn (this->name, GF_LOG_TRACE,
                                          "contribution node for the "
                                          "path (%s) with parent (%s) "
                                          "not found", loc->path,
                                          loc->parent ?
                                          uuid_utoa (loc->parent->gfid) :
                                          NULL);

                contribution = mq_add_new_contribution_node (this, ctx, loc);
                if (!contribution) {
                        if (!loc_is_root(loc))
                                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                                  "could not allocate "
                                                  " contribution node for (%s) "
                                                  "parent: (%s)", loc->path,
                                                  loc->parent ?
                                                 uuid_utoa (loc->parent->gfid) :
                                                  NULL);
                        ret = -1;
                        goto out;
                }
        }

        mq_start_quota_txn_v2 (this, loc, ctx, contribution);

        ret = 0;
out:
        if (contribution)
                GF_REF_PUT (contribution);

        if (ctx && ret < 0)
                mq_set_ctx_updation_status (ctx, _gf_false);

        return ret;
}

int
_mq_initiate_quota_txn (xlator_t *this, loc_t *origin_loc, gf_boolean_t spawn)
{
        int32_t                 ret          = -1;
        quota_inode_ctx_t      *ctx          = NULL;
        gf_boolean_t            status       = _gf_true;
        loc_t                   loc          = {0,};

        ret = mq_prevalidate_txn (this, origin_loc, &loc, &ctx);
        if (ret < 0)
                goto out;

        if (loc_is_root(&loc)) {
                ret = 0;
                goto out;
        }

        if (loc.parent == NULL) {
                gf_log (this->name, GF_LOG_WARNING, "parent is NULL for %s, "
                        "aborting updation txn", loc.path);
                goto out;
        }

        ret = mq_test_and_set_ctx_updation_status (ctx, &status);
        if (ret < 0 || status == _gf_true)
                goto out;

        ret = mq_synctask (this, mq_initiate_quota_task, spawn, &loc, 0);

out:
        if (ret < 0 && status == _gf_false)
                mq_set_ctx_updation_status (ctx, _gf_false);

        loc_wipe (&loc);
        return ret;
}

int
mq_initiate_quota_txn (xlator_t *this, loc_t *loc)
{
        int32_t                 ret          = -1;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_initiate_quota_txn (this, loc, _gf_true);
out:
        return ret;
}

int
mq_initiate_quota_blocking_txn (xlator_t *this, loc_t *loc)
{
        int32_t                 ret          = -1;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_initiate_quota_txn (this, loc, _gf_false);
out:
        return ret;
}

int
mq_update_dirty_inode_task (void *opaque)
{
        int32_t               ret            = -1;
        fd_t                 *fd             = NULL;
        off_t                 offset         = 0;
        loc_t                 child_loc      = {0, };
        gf_dirent_t           entries;
        gf_dirent_t          *entry          = NULL;
        gf_boolean_t          locked         = _gf_false;
        gf_boolean_t          free_entries   = _gf_false;
        gf_boolean_t          updated        = _gf_false;
        int32_t               dirty          = 0;
        quota_meta_t          contri         = {0, };
        quota_meta_t          size           = {0, };
        quota_meta_t          contri_sum     = {0, };
        quota_meta_t          delta          = {0, };
        quota_synctask_t     *args           = NULL;
        xlator_t             *this           = NULL;
        loc_t                *loc            = NULL;

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        this = args->this;
        THIS = this;

        ret = mq_lock (this, loc, F_WRLCK);
        if (ret < 0)
                goto out;
        locked = _gf_true;

        ret = mq_get_dirty (this, loc, &dirty);
        if (ret < 0 || dirty == 0) {
                ret = 0;
                goto out;
        }

        fd = fd_create (loc->inode, 0);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to create fd");
                ret = -1;
                goto out;
        }

        ret = syncop_opendir (this, loc, fd, NULL, NULL);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "opendir failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        INIT_LIST_HEAD (&entries.list);
        while ((ret = syncop_readdirp (this, fd, 131072, offset, &entries,
                                       NULL, NULL)) != 0) {
                if (ret < 0) {
                        gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                                ? GF_LOG_DEBUG:GF_LOG_ERROR, "readdirp failed "
                                "for %s: %s", loc->path, strerror (-ret));
                        goto out;
                }

                if (list_empty (&entries.list))
                        break;

                free_entries = _gf_true;
                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        ret = loc_build_child (&child_loc, loc, entry->d_name);
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Couldn't build loc for %s/%s "
                                        "returning from updation of dirty "
                                        "inode", loc->path, entry->d_name);
                                goto out;
                        }

                        ret = mq_get_contri (this, &child_loc, &contri,
                                             loc->gfid);
                        if (ret < 0)
                                goto out;

                        mq_add_meta (&contri_sum, &contri);
                        loc_wipe (&child_loc);
                }

                gf_dirent_free (&entries);
                free_entries = _gf_false;
        }
        /* Inculde for self */
        contri_sum.dir_count++;

        ret = mq_get_size (this, loc, &size);
        if (ret < 0)
                goto out;

        mq_compute_delta (&delta, &contri_sum, &size);

        if (quota_meta_is_null (&delta))
                goto out;

        gf_log (this->name, GF_LOG_INFO, "calculated size = %"PRId64
                ", original size = %"PRIu64 ", diff = %"PRIu64
                ", path = %s ", contri_sum.size, size.size, delta.size,
                loc->path);

        gf_log (this->name, GF_LOG_INFO, "calculated f_count = %"PRId64
                ", original f_count = %"PRIu64 ", diff = %"PRIu64
                ", path = %s ", contri_sum.file_count, size.file_count,
                delta.file_count, loc->path);

        gf_log (this->name, GF_LOG_INFO, "calculated d_count = %"PRId64
                ", original d_count = %"PRIu64 ", diff = %"PRIu64
                ", path = %s ", contri_sum.dir_count, size.dir_count,
                delta.dir_count, loc->path);


        ret = mq_update_size (this, loc, &delta);
        if (ret < 0)
                goto out;

        updated = _gf_true;

out:
        if (free_entries)
                gf_dirent_free (&entries);

        if (fd)
                fd_unref (fd);

        if (ret >= 0 && dirty)
                mq_mark_dirty (this, loc, 0);

        if (locked)
                mq_lock (this, loc, F_UNLCK);

        loc_wipe(&child_loc);

        if (updated)
                mq_initiate_quota_blocking_txn (this, loc);

        return ret;
}

int32_t
mq_update_dirty_inode_txn (xlator_t *this, loc_t *loc)
{
        int32_t          ret            = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = mq_synctask (this, mq_update_dirty_inode_task, _gf_true,
                           loc, 0);
out:
        return ret;
}

int32_t
mq_inspect_directory_xattr (xlator_t *this, loc_t *loc, quota_inode_ctx_t *ctx,
                            dict_t *dict, struct iatt buf)
{
        int32_t               ret                          = 0;
        int8_t                dirty                        = -1;
        quota_meta_t          size                         = {0, };
        quota_meta_t          contri                       = {0, };
        quota_meta_t          delta                        = {0, };
        char                  contri_key[CONTRI_KEY_MAX]   = {0, };
        inode_contribution_t *contribution                 = NULL;

        if (!loc_is_root(loc)) {
                contribution = mq_add_new_contribution_node (this, ctx, loc);
                if (contribution == NULL) {
                        if (!gf_uuid_is_null (loc->inode->gfid))
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "cannot add a new contribution node "
                                        "(%s)", uuid_utoa (loc->inode->gfid));
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret < 0) {
                /* dirty is set only on the first file write operation
                 * so ignore this error
                 */
                ret = 0;
                dirty = 0;
        }

        ret = _quota_dict_get_meta (this, dict, QUOTA_SIZE_KEY, &size,
                                    IA_IFDIR, _gf_false);
        if (ret < 0)
                goto create_xattr;

        if (!loc_is_root(loc)) {
                GET_CONTRI_KEY (contri_key, contribution->gfid, ret);
                if (ret < 0)
                        goto out;

                ret = _quota_dict_get_meta (this, dict, contri_key, &contri,
                                            IA_IFDIR, _gf_false);
                if (ret < 0)
                        goto create_xattr;

                LOCK (&contribution->lock);
                {
                        contribution->contribution = contri.size;
                        contribution->file_count = contri.file_count;
                        contribution->dir_count = contri.dir_count;
                }
                UNLOCK (&contribution->lock);
        }

        LOCK (&ctx->lock);
        {
                ctx->size = size.size;
                ctx->file_count = size.file_count;
                ctx->dir_count = size.dir_count;
        }
        UNLOCK (&ctx->lock);

        mq_compute_delta (&delta, &size, &contri);

        if (dirty) {
                if (ctx->dirty == 0)
                        ret = mq_update_dirty_inode_txn (this, loc);

                goto out;
        }

        if (!loc_is_root(loc) &&
            !quota_meta_is_null (&delta))
                mq_initiate_quota_txn (this, loc);

        ret = 0;

create_xattr:
        if (ret < 0)
                ret = mq_create_xattrs_txn (this, loc);

out:
        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_inspect_file_xattr (xlator_t *this, quota_inode_ctx_t *ctx, loc_t *loc,
                       dict_t *dict, struct iatt buf)
{
        int32_t               ret                          = -1;
        quota_meta_t          size                         = {0, };
        quota_meta_t          contri                       = {0, };
        quota_meta_t          delta                        = {0, };
        char                  contri_key[CONTRI_KEY_MAX]   = {0, };
        inode_contribution_t *contribution                 = NULL;

        contribution = mq_add_new_contribution_node (this, ctx, loc);
        if (contribution == NULL) {
                gf_log_callingfn (this->name, GF_LOG_DEBUG, "cannot allocate "
                                  "contribution node (path:%s)", loc->path);
                ret = -1;
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->size = 512 * buf.ia_blocks;
                ctx->file_count = 1;
                ctx->dir_count = 0;

                size.size = ctx->size;
                size.file_count = ctx->file_count;
                size.dir_count = ctx->dir_count;
        }
        UNLOCK (&ctx->lock);

        GET_CONTRI_KEY (contri_key, contribution->gfid, ret);
        if (ret < 0)
                goto out;

        ret = _quota_dict_get_meta (this, dict, contri_key, &contri,
                                    IA_IFREG, _gf_true);
        if (ret < 0) {
                ret = mq_create_xattrs_txn (this, loc);
        } else {
                LOCK (&contribution->lock);
                {
                        contribution->contribution = contri.size;
                        contribution->file_count = contri.file_count;
                        contribution->dir_count = contri.dir_count;
                }
                UNLOCK (&contribution->lock);

                mq_compute_delta (&delta, &size, &contri);
                if (!quota_meta_is_null (&delta))
                        mq_initiate_quota_txn (this, loc);
        }
        /* TODO: revist this code when fixing hardlinks */

out:
        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_xattr_state (xlator_t *this, loc_t *origin_loc, dict_t *dict,
                struct iatt buf)
{
        int32_t               ret     = -1;
        quota_inode_ctx_t    *ctx     = NULL;
        loc_t                 loc     = {0, };

        ret = mq_prevalidate_txn (this, origin_loc, &loc, &ctx);
        if (ret < 0)
                goto out;

        if (((buf.ia_type == IA_IFREG) && !dht_is_linkfile (&buf, dict))
            || (buf.ia_type == IA_IFLNK))  {
                mq_inspect_file_xattr (this, ctx, &loc, dict, buf);
        } else if (buf.ia_type == IA_IFDIR)
                mq_inspect_directory_xattr (this, &loc, ctx, dict, buf);

out:
        loc_wipe (&loc);
        return ret;
}

int32_t
mq_req_xattr (xlator_t *this, loc_t *loc, dict_t *dict,
              char *contri_key)
{
        int32_t               ret       = -1;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", dict, out);

        if (!loc_is_root(loc)) {
                ret = mq_dict_set_contribution (this, dict, loc, NULL,
                                                contri_key);
                if (ret < 0)
                        goto out;
        }

        ret = dict_set_uint64 (dict, QUOTA_SIZE_KEY, 0);
        if (ret < 0)
                goto out;

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, 0);

out:
        if (ret < 0)
                gf_log_callingfn (this->name, GF_LOG_ERROR, "dict set failed");

        return ret;
}


int32_t
mq_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}

int32_t
_mq_inode_remove_done (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t        ret                           = 0;
        char           contri_key[CONTRI_KEY_MAX]    = {0, };
        quota_local_t *local                         = NULL;
        inode_t       *inode                         = NULL;
        dentry_t      *tmp                           = NULL;
        gf_boolean_t  last_dentry                    = _gf_true;
        loc_t         loc                            = {0, };
        dentry_t      *other_dentry                  = NULL;
        gf_boolean_t  remove                         = _gf_false;

        local = (quota_local_t *) frame->local;

        if (op_ret == -1 || local->err == -1) {
                mq_removexattr_cbk (frame, NULL, this, -1, 0, NULL);
                return 0;
        }

        frame->local = NULL;

        GET_CONTRI_KEY (contri_key, local->contri->gfid, ret);

        if (!local->loc.inode)
                inode = inode_grep (local->loc.parent->table, local->loc.parent,
                                    local->loc.name);
        else
                inode = inode_ref (local->loc.inode);

        /* Suppose there are 2 directories dir1 and dir2. Quota limit is set on
           both the directories. There is a file (f1) in dir1. A hark link is
           created for that file inside the directory dir2 (say f2). Now one
           more xattr is set in the inode as a new hard link is created in a
           separate directory.
           i.e trusted.glusterfs.quota.<gfid of dir2>.contri=<contribution>

           Now when the hardlink f2 is removed, then the new xattr added (i.e
           the xattr indicating its contribution to ITS parent directory) should
           be removed (IFF there is not another hardlink for that file in the
           same directory).

           To do that upon getting unlink first check whether any other hard
           links for the same inode exists in the same directory. If so do not
           do anything and proceed for quota transaction.
           Otherwise, if the removed entry was the only link for that inode
           within that directory, then get another dentry for the inode
           (by traversing the list of dentries for the inode) and using the
           the dentry's parent and name, send removexattr so that the xattr
           is removed.

           If it is not done, then if the volume is restarted or the brick
           process is restarted, then wrong quota usage will be shown for the
           directory dir2.
        */
        if (inode) {
                tmp = NULL;
                list_for_each_entry (tmp, &inode->dentry_list, inode_list) {
                        if (local->loc.parent == tmp->parent) {
                                if (strcmp (local->loc.name, local->loc.name)) {
                                        last_dentry = _gf_false;
                                        break;
                                }
                        }
                }
                remove = last_dentry;
        }

        if (remove) {
                if (!other_dentry)    {
                        list_for_each_entry (tmp, &inode->dentry_list,
                                             inode_list) {
                                if (local->loc.parent != tmp->parent) {
                                        other_dentry = tmp;
                                        break;
                                }
                        }
                }

                if (!other_dentry)
                        mq_removexattr_cbk (frame, NULL, this, 0, 0, NULL);
                else {
                        loc.parent = inode_ref (other_dentry->parent);
                        loc.name = gf_strdup (other_dentry->name);
                        gf_uuid_copy (loc.pargfid , other_dentry->parent->gfid);
                        loc.inode = inode_ref (inode);
                        gf_uuid_copy (loc.gfid, inode->gfid);
                        inode_path (other_dentry->parent, other_dentry->name,
                                    (char **)&loc.path);

                        STACK_WIND (frame, mq_removexattr_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->removexattr,
                                    &loc, contri_key, NULL);
                }
        } else
                mq_removexattr_cbk (frame, NULL, this, 0, 0, NULL);

        ret = 0;

        if (strcmp (local->parent_loc.path, "/") != 0) {
                ret = mq_get_parent_inode_local (this, local);
                if (ret < 0)
                        goto out;

                mq_start_quota_txn (this, &local->loc, local->ctx, local->contri);
        }
out:
        mq_local_unref (this, local);

        loc_wipe (&loc);
        inode_unref (inode);
        return 0;
}

int32_t
mq_inode_remove_done (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
        int32_t            ret   = -1;
        struct gf_flock    lock  = {0, };
        quota_inode_ctx_t *ctx   = NULL;
        quota_local_t     *local = NULL;
        int64_t contribution = 0;

        local = frame->local;
        if (op_ret == -1)
                local->err = -1;

        ret = mq_inode_ctx_get (local->parent_loc.inode, this, &ctx);

        LOCK (&local->contri->lock);
        {
                contribution = local->contri->contribution;
        }
        UNLOCK (&local->contri->lock);

        if (contribution == local->size) {
                if (ret == 0) {
                        LOCK (&ctx->lock);
                        {
                                ctx->size -= contribution;
                        }
                        UNLOCK (&ctx->lock);

                        LOCK (&local->contri->lock);
                        {
                                local->contri->contribution = 0;
                        }
                        UNLOCK (&local->contri->lock);
                }
        }

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    _mq_inode_remove_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc,
                    F_SETLKW, &lock, NULL);
        return 0;
}

int32_t
mq_reduce_parent_size_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t                  ret               = -1;
        int64_t                 *size              = NULL;
        dict_t                  *dict              = NULL;
        quota_local_t           *local             = NULL;

        local = frame->local;
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "inodelk set failed on %s", local->parent_loc.path);
                QUOTA_STACK_DESTROY (frame, this);
                return 0;
        }

        VALIDATE_OR_GOTO (local->contri, err);

        dict = dict_new ();
        if (dict == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -1;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (size, int64_t, ret, err);

        *size = hton64 (-local->size);

        ret = dict_set_bin (dict, QUOTA_SIZE_KEY, size, 8);
        if (ret < 0)
                goto err;

        gf_uuid_copy (local->parent_loc.gfid,
                   local->parent_loc.inode->gfid);
        GF_UUID_ASSERT (local->parent_loc.gfid);

        STACK_WIND (frame, mq_inode_remove_done, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, &local->parent_loc,
                    GF_XATTROP_ADD_ARRAY64, dict, NULL);
        dict_unref (dict);
        return 0;

err:
        local->err = 1;
        mq_inode_remove_done (frame, NULL, this, -1, 0, NULL, NULL);
        if (dict)
                dict_unref (dict);
        return 0;
}

int32_t
mq_reduce_parent_size (xlator_t *this, loc_t *loc, int64_t contri)
{
        int32_t                  ret           = -1;
        struct gf_flock          lock          = {0,};
        call_frame_t            *frame         = NULL;
        quota_local_t           *local         = NULL;
        quota_inode_ctx_t       *ctx           = NULL;
        inode_contribution_t    *contribution  = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0)
                goto out;

        contribution = mq_get_contribution_node (loc->parent, ctx);
        if (contribution == NULL) {
                gf_log_callingfn (this->name, GF_LOG_WARNING, "contribution for"
                                  " the node %s is NULL", loc->path);
                goto out;
        }

        local = mq_local_new ();
        if (local == NULL) {
                ret = -1;
                goto out;
        }

        if (contri >= 0) {
                local->size = contri;
        } else {
                LOCK (&contribution->lock);
                {
                        local->size = contribution->contribution;
                }
                UNLOCK (&contribution->lock);
        }

        if (local->size == 0) {
                gf_log_callingfn (this->name, GF_LOG_TRACE,
                                  "local->size is 0 " "path: (%s)", loc->path);
                ret = 0;
                goto out;
        }

        ret = mq_loc_copy (&local->loc, loc);
        if (ret < 0)
                goto out;

        local->ctx = ctx;
        if (contribution) {
                local->contri = contribution;
                GF_REF_GET (local->contri);
        }

        ret = mq_inode_loc_fill (NULL, loc->parent, &local->parent_loc);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_INFO, "building parent loc"
                                  " failed. (gfid: %s)",
                                  uuid_utoa (loc->parent->gfid));
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        mq_assign_lk_owner (this, frame);

        frame->local = local;

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        if (local->parent_loc.inode == NULL) {
                ret = -1;
                gf_log (this->name, GF_LOG_DEBUG,
                        "Inode is NULL, so can't stackwind.");
                goto out;
        }

        STACK_WIND (frame,
                    mq_reduce_parent_size_xattr,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc, F_SETLKW, &lock, NULL);
        local = NULL;
        ret = 0;

out:
        if (local != NULL)
                mq_local_unref (this, local);

        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}


int32_t
init_quota_priv (xlator_t *this)
{
        return 0;
}


int32_t
mq_rename_update_newpath (xlator_t *this, loc_t *loc)
{
        int32_t               ret          = -1;
        quota_inode_ctx_t    *ctx          = NULL;
        inode_contribution_t *contribution = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0)
                goto out;

        contribution = mq_add_new_contribution_node (this, ctx, loc);
        if (contribution == NULL) {
                ret = -1;
                goto out;
        }

        mq_initiate_quota_txn (this, loc);
out:

        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_forget (xlator_t *this, quota_inode_ctx_t *ctx)
{
        inode_contribution_t *contri = NULL;
        inode_contribution_t *next   = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        list_for_each_entry_safe (contri, next, &ctx->contribution_head,
                                  contri_list) {
                list_del (&contri->contri_list);
                GF_REF_PUT (contri);
        }

        LOCK_DESTROY (&ctx->lock);
        GF_FREE (ctx);
out:
        return 0;
}
