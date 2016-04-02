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

static void
mq_set_ctx_status (quota_inode_ctx_t *ctx, gf_boolean_t *flag,
                   gf_boolean_t status)
{
        LOCK (&ctx->lock);
        {
                *flag = status;
        }
        UNLOCK (&ctx->lock);
}

static void
mq_test_and_set_ctx_status (quota_inode_ctx_t *ctx, gf_boolean_t *flag,
                            gf_boolean_t *status)
{
        gf_boolean_t    temp    = _gf_false;

        LOCK (&ctx->lock);
        {
                temp = *status;
                *status = *flag;
                *flag = temp;
        }
        UNLOCK (&ctx->lock);
}

static void
mq_get_ctx_status (quota_inode_ctx_t *ctx, gf_boolean_t *flag,
                   gf_boolean_t *status)
{
        LOCK (&ctx->lock);
        {
                *status = *flag;
        }
        UNLOCK (&ctx->lock);
}

int32_t
mq_get_ctx_updation_status (quota_inode_ctx_t *ctx,
                            gf_boolean_t *status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        mq_get_ctx_status (ctx, &ctx->updation_status, status);
        return 0;
out:
        return -1;
}

int32_t
mq_set_ctx_updation_status (quota_inode_ctx_t *ctx,
                            gf_boolean_t status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        mq_set_ctx_status (ctx, &ctx->updation_status, status);
        return 0;
out:
        return -1;
}

int32_t
mq_test_and_set_ctx_updation_status (quota_inode_ctx_t *ctx,
                                     gf_boolean_t *status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        mq_test_and_set_ctx_status (ctx, &ctx->updation_status, status);
        return 0;
out:
        return -1;
}

int32_t
mq_set_ctx_create_status (quota_inode_ctx_t *ctx,
                          gf_boolean_t status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        mq_set_ctx_status (ctx, &ctx->create_status, status);
        return 0;
out:
        return -1;
}

int32_t
mq_test_and_set_ctx_create_status (quota_inode_ctx_t *ctx,
                                   gf_boolean_t *status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        mq_test_and_set_ctx_status (ctx, &ctx->create_status, status);
        return 0;
out:
        return -1;
}

int32_t
mq_set_ctx_dirty_status (quota_inode_ctx_t *ctx,
                         gf_boolean_t status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        mq_set_ctx_status (ctx, &ctx->dirty_status, status);
        return 0;
out:
        return -1;
}

int32_t
mq_test_and_set_ctx_dirty_status (quota_inode_ctx_t *ctx,
                                  gf_boolean_t *status)
{
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        mq_test_and_set_ctx_status (ctx, &ctx->dirty_status, status);
        return 0;
out:
        return -1;
}

int
mq_build_ancestry (xlator_t *this, loc_t *loc)
{
        int32_t               ret            = -1;
        fd_t                 *fd             = NULL;
        gf_dirent_t           entries;
        gf_dirent_t          *entry          = NULL;
        dict_t               *xdata          = NULL;
        inode_t              *tmp_parent     = NULL;
        inode_t              *tmp_inode      = NULL;
        inode_t              *linked_inode   = NULL;
        quota_inode_ctx_t    *ctx            = NULL;

        INIT_LIST_HEAD (&entries.list);

        xdata = dict_new ();
        if (xdata == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -ENOMEM;
                goto out;
        }

        ret = dict_set_int8 (xdata, GET_ANCESTRY_DENTRY_KEY, 1);
        if (ret < 0)
                goto out;

        fd = fd_anonymous (loc->inode);
        if (fd == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "fd creation failed");
                ret = -ENOMEM;
                goto out;
        }

        fd_bind (fd);

        ret = syncop_readdirp (this, fd, 131072, 0, &entries, xdata, NULL);
        if (ret < 0) {
                gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "readdirp failed "
                        "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        if (list_empty (&entries.list)) {
                ret = -1;
                goto out;
        }

        list_for_each_entry (entry, &entries.list, list) {
                if (__is_root_gfid (entry->inode->gfid)) {
                        tmp_parent = NULL;
                } else {
                        linked_inode = inode_link (entry->inode, tmp_parent,
                                                   entry->d_name,
                                                   &entry->d_stat);
                        if (linked_inode) {
                                tmp_inode = entry->inode;
                                entry->inode = linked_inode;
                                inode_unref (tmp_inode);
                        } else {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "inode link failed");
                                ret = -EINVAL;
                                goto out;
                        }
                }

                ctx = mq_inode_ctx_new (entry->inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s",
                                uuid_utoa (entry->inode->gfid));
                        ret = -ENOMEM;
                        goto out;
                }

                tmp_parent = entry->inode;
        }

        if (loc->parent)
                inode_unref (loc->parent);

        loc->parent = inode_parent (loc->inode, 0, NULL);
        if (loc->parent == NULL) {
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        gf_dirent_free (&entries);

        if (fd)
                fd_unref (fd);

        if (xdata)
                dict_unref (xdata);

        return ret;
}


/* This function should be used only in inspect_directory and inspect_file
 * function to heal quota xattrs.
 * Inode quota feature is introduced in 3.7.
 * If gluster setup is upgraded from 3.6 to 3.7, there can be a
 * getxattr and setxattr spikes with quota heal as inode quota is missing.
 * So this wrapper function is to avoid xattrs spikes during upgrade.
 * This function returns success even is inode-quota xattrs are missing and
 * hence no healing performed.
 */
int32_t
_quota_dict_get_meta (xlator_t *this, dict_t *dict, char *key,
                      quota_meta_t *meta, ia_type_t ia_type,
                      gf_boolean_t add_delta)
{
        int32_t             ret     = 0;
        marker_conf_t      *priv    = NULL;

        priv = this->private;

        ret = quota_dict_get_inode_meta (dict, key, meta);
        if (ret == -2 && (priv->feature_enabled & GF_INODE_QUOTA) == 0) {
                /* quota_dict_get_inode_meta returns -2 if
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

int32_t
quota_dict_set_size_meta (xlator_t *this, dict_t *dict,
                          const quota_meta_t *meta)
{
        int32_t         ret                        = -ENOMEM;
        quota_meta_t   *value                      = NULL;
        char            size_key[QUOTA_KEY_MAX]    = {0, };

        value = GF_CALLOC (2, sizeof (quota_meta_t), gf_common_quota_meta_t);
        if (value == NULL) {
                goto out;
        }
        value[0].size = hton64 (meta->size);
        value[0].file_count = hton64 (meta->file_count);
        value[0].dir_count = hton64 (meta->dir_count);

        value[1].size = 0;
        value[1].file_count = 0;
        value[1].dir_count = hton64 (1);

        GET_SIZE_KEY (this, size_key, ret);
        if (ret < 0)
                goto out;
        ret = dict_set_bin (dict, size_key, value,
                            (sizeof (quota_meta_t) * 2));
        if (ret < 0) {
                gf_log_callingfn ("quota", GF_LOG_ERROR, "dict set failed");
                GF_FREE (value);
        }
out:
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

int32_t
mq_are_xattrs_set (xlator_t *this, loc_t *loc, gf_boolean_t *contri_set,
                   gf_boolean_t *size_set)
{
        int32_t        ret                         = -1;
        char           contri_key[QUOTA_KEY_MAX]   = {0, };
        char           size_key[QUOTA_KEY_MAX]     = {0, };
        quota_meta_t   meta                        = {0, };
        struct iatt    stbuf                       = {0,};
        dict_t        *dict                        = NULL;
        dict_t        *rsp_dict                    = NULL;

        dict = dict_new ();
        if (dict == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        ret = mq_req_xattr (this, loc, dict, contri_key, size_key);
        if (ret < 0)
                goto out;

        ret = syncop_lookup (FIRST_CHILD(this), loc, &stbuf, NULL,
                             dict, &rsp_dict);
        if (ret < 0) {
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
                                  ? GF_LOG_DEBUG:GF_LOG_ERROR, "lookup failed "
                                  "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        if (rsp_dict == NULL)
                goto out;

        *contri_set = _gf_true;
        *size_set = _gf_true;
        if (loc->inode->ia_type == IA_IFDIR) {
                ret = quota_dict_get_inode_meta (rsp_dict, size_key, &meta);
                if (ret < 0 || meta.dir_count == 0)
                        *size_set = _gf_false;
        }

        if (!loc_is_root(loc)) {
                ret = quota_dict_get_inode_meta (rsp_dict, contri_key, &meta);
                if (ret < 0)
                        *contri_set = _gf_false;
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
mq_create_size_xattrs (xlator_t *this, quota_inode_ctx_t *ctx, loc_t *loc)
{
        int32_t                ret                  = -1;
        quota_meta_t           size                 = {0, };
        dict_t                *dict                 = NULL;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        if (loc->inode->ia_type != IA_IFDIR) {
                ret = 0;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -1;
                goto out;
        }

        ret = quota_dict_set_size_meta (this, dict, &size);
        if (ret < 0)
                goto out;

        ret = syncop_xattrop (FIRST_CHILD(this), loc,
                              GF_XATTROP_ADD_ARRAY64_WITH_DEFAULT, dict, NULL,
                              NULL);

        if (ret < 0) {
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
                                  ? GF_LOG_DEBUG:GF_LOG_ERROR, "xattrop failed "
                                  "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

out:
        if (dict)
                dict_unref (dict);

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
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
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
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
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
mq_get_set_dirty (xlator_t *this, loc_t *loc, int32_t dirty,
                  int32_t *prev_dirty)
{
        int32_t              ret              = -1;
        int8_t               value            = 0;
        quota_inode_ctx_t   *ctx              = NULL;
        dict_t              *dict             = NULL;
        dict_t              *rsp_dict         = NULL;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);
        GF_VALIDATE_OR_GOTO ("marker", prev_dirty, out);

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

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, dirty);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "dict_set failed");
                goto out;
        }

        ret = syncop_xattrop (FIRST_CHILD(this), loc, GF_XATTROP_GET_AND_SET,
                              dict, NULL, &rsp_dict);
        if (ret < 0) {
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
                          ? GF_LOG_DEBUG:GF_LOG_ERROR, "xattrop failed "
                          "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        *prev_dirty = 0;
        if (rsp_dict) {
                ret = dict_get_int8 (rsp_dict, QUOTA_DIRTY_KEY, &value);
                if (ret == 0)
                        *prev_dirty = value;
        }

        LOCK (&ctx->lock);
        {
                ctx->dirty = dirty;
        }
        UNLOCK (&ctx->lock);
        ret = 0;
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

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode ctx for "
                        "%s", loc->path);
                ret = 0;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
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
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
                        ? GF_LOG_DEBUG:GF_LOG_ERROR, "setxattr dirty = %d "
                        "failed for %s: %s", dirty, loc->path, strerror (-ret));
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
        char               contri_key[QUOTA_KEY_MAX]   = {0, };
        char               size_key[QUOTA_KEY_MAX]     = {0, };
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
                GET_SIZE_KEY (this, size_key, ret);
                if (ret < 0)
                        goto out;
                ret = dict_set_int64 (dict, size_key, 0);
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
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
                                  ? GF_LOG_DEBUG:GF_LOG_ERROR, "lookup failed "
                                  "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        if (size) {
                if (loc->inode->ia_type == IA_IFDIR) {
                        ret = quota_dict_get_meta (rsp_dict, size_key,
                                                   &meta);
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
                ret = quota_dict_get_meta (rsp_dict, contri_key, &meta);
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
        if (ret < 0)
                goto out;

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
        return _mq_get_metadata (this, loc, NULL, size, 0);
}

int32_t
mq_get_contri (xlator_t *this, loc_t *loc, quota_meta_t *contri,
               uuid_t contri_gfid)
{
        return _mq_get_metadata (this, loc, contri, NULL, contri_gfid);
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
                  uint32_t nlink)
{
        int32_t              ret                         = -1;
        char                 contri_key[QUOTA_KEY_MAX]   = {0, };

        if (nlink == 1) {
                /*File was a last link and has been deleted */
                ret = 0;
                goto done;
        }

        GET_CONTRI_KEY (this, contri_key, contri->gfid, ret);
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
                        gf_log_callingfn (this->name, GF_LOG_ERROR,
                                          "removexattr %s failed for %s: %s",
                                          contri_key, loc->path,
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
        char                 contri_key[QUOTA_KEY_MAX]   = {0, };
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

        GET_CONTRI_KEY (this, contri_key, contri->gfid, ret);
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
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
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

        ret = quota_dict_set_size_meta (this, dict, delta);
        if (ret < 0)
                goto out;

        ret = syncop_xattrop(FIRST_CHILD(this), loc,
                             GF_XATTROP_ADD_ARRAY64_WITH_DEFAULT, dict, NULL,
                             NULL);
        if (ret < 0) {
                gf_log_callingfn (this->name, (-ret == ENOENT || -ret == ESTALE)
                                  ? GF_LOG_DEBUG:GF_LOG_ERROR, "xattrop failed "
                                  "for %s: %s", loc->path, strerror (-ret));
                goto out;
        }

        LOCK (&ctx->lock);
        {
                ctx->size += delta->size;
                ctx->file_count += delta->file_count;
                if (ctx->dir_count == 0)
                        ctx->dir_count += delta->dir_count + 1;
                else
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

        if (args->stub)
                call_resume (args->stub);

        if (!args->is_static)
                GF_FREE (args);

        return 0;
}

int
mq_synctask1 (xlator_t *this, synctask_fn_t task, gf_boolean_t spawn,
              loc_t *loc, quota_meta_t *contri, uint32_t nlink,
              call_stub_t *stub)
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
        args->stub = stub;
        loc_copy (&args->loc, loc);
        args->ia_nlink = nlink;

        if (contri) {
                args->contri = *contri;
        } else {
                args->contri.size = -1;
                args->contri.file_count = -1;
                args->contri.dir_count = -1;
        }

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

int
mq_synctask (xlator_t *this, synctask_fn_t task, gf_boolean_t spawn, loc_t *loc)
{
        return mq_synctask1 (this, task, spawn, loc, NULL, -1, NULL);
}

int32_t
mq_prevalidate_txn (xlator_t *this, loc_t *origin_loc, loc_t *loc,
                    quota_inode_ctx_t **ctx, struct iatt *buf)
{
        int32_t               ret     = -1;
        quota_inode_ctx_t    *ctxtmp  = NULL;

        if (buf) {
                if (buf->ia_type == IA_IFREG && IS_DHT_LINKFILE_MODE(buf))
                        goto out;

                if (buf->ia_type != IA_IFREG && buf->ia_type != IA_IFLNK &&
                    buf->ia_type != IA_IFDIR)
                        goto out;
        }

        if (origin_loc == NULL || origin_loc->inode == NULL ||
            gf_uuid_is_null(origin_loc->inode->gfid))
                goto out;

        loc_copy (loc, origin_loc);

        if (gf_uuid_is_null (loc->gfid))
                gf_uuid_copy (loc->gfid, loc->inode->gfid);

        if (!loc_is_root(loc) && loc->parent == NULL)
                loc->parent = inode_parent (loc->inode, 0, NULL);

        ret = mq_inode_ctx_get (loc->inode, this, &ctxtmp);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING, "inode ctx for "
                                  "is NULL for %s", loc->path);
                goto out;
        }
        if (ctx)
                *ctx = ctxtmp;

        ret = 0;
out:
        return ret;
}

int
mq_create_xattrs_task (void *opaque)
{
        int32_t                  ret        = -1;
        gf_boolean_t             locked     = _gf_false;
        gf_boolean_t             contri_set = _gf_false;
        gf_boolean_t             size_set   = _gf_false;
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

        ret = mq_are_xattrs_set (this, loc, &contri_set, &size_set);
        if (ret < 0 || (contri_set && size_set))
                goto out;

        mq_set_ctx_create_status (ctx, _gf_false);
        status = _gf_true;

        if (loc->inode->ia_type == IA_IFDIR && size_set == _gf_false) {
                ret = mq_create_size_xattrs (this, ctx, loc);
                if (ret < 0)
                        goto out;
        }

        need_txn = _gf_true;
out:
        if (locked)
                ret = mq_lock (this, loc, F_UNLCK);

        if (status == _gf_false)
                mq_set_ctx_create_status (ctx, _gf_false);

        if (need_txn)
                ret = mq_initiate_quota_blocking_txn (this, loc, NULL);

        return ret;
}

static int
_mq_create_xattrs_txn (xlator_t *this, loc_t *origin_loc, struct iatt *buf,
                       gf_boolean_t spawn)
{
        int32_t                  ret          = -1;
        quota_inode_ctx_t       *ctx          = NULL;
        gf_boolean_t             status       = _gf_true;
        loc_t                    loc          = {0, };
        inode_contribution_t    *contribution = NULL;

        ret = mq_prevalidate_txn (this, origin_loc, &loc, &ctx, buf);
        if (ret < 0)
                goto out;

        ret = mq_test_and_set_ctx_create_status (ctx, &status);
        if (ret < 0 || status == _gf_true)
                goto out;

        if (!loc_is_root(&loc) && loc.parent) {
                contribution = mq_add_new_contribution_node (this, ctx, &loc);
                if (contribution == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "cannot add a new contribution node "
                                "(%s)", uuid_utoa (loc.gfid));
                        ret = -1;
                        goto out;
                } else {
                        GF_REF_PUT (contribution);
                }
        }

        ret = mq_synctask (this, mq_create_xattrs_task, spawn, &loc);
out:
        if (ret < 0 && status == _gf_false)
                mq_set_ctx_create_status (ctx, _gf_false);

        loc_wipe (&loc);
        return ret;
}

int
mq_create_xattrs_txn (xlator_t *this, loc_t *loc, struct iatt *buf)
{
        int32_t                  ret        = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_create_xattrs_txn (this, loc, buf, _gf_true);
out:
        return ret;
}

int
mq_create_xattrs_blocking_txn (xlator_t *this, loc_t *loc, struct iatt *buf)
{
        int32_t                  ret        = -1;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_create_xattrs_txn (this, loc, buf, _gf_false);
out:
        return ret;
}

int32_t
mq_reduce_parent_size_task (void *opaque)
{
        int32_t                  ret           = -1;
        int32_t                  prev_dirty    = 0;
        quota_inode_ctx_t       *ctx           = NULL;
        quota_inode_ctx_t       *parent_ctx    = NULL;
        inode_contribution_t    *contribution  = NULL;
        quota_meta_t             delta         = {0, };
        quota_meta_t             contri        = {0, };
        loc_t                    parent_loc    = {0,};
        gf_boolean_t             locked        = _gf_false;
        gf_boolean_t             dirty         = _gf_false;
        quota_synctask_t        *args          = NULL;
        xlator_t                *this          = NULL;
        loc_t                   *loc           = NULL;
        gf_boolean_t             remove_xattr  = _gf_true;
        uint32_t                 nlink         = 0;

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        contri = args->contri;
        nlink = args->ia_nlink;
        this = args->this;
        THIS = this;

        ret = mq_inode_loc_fill (NULL, loc->parent, &parent_loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "parent_loc fill failed for "
                        "child inode %s: ", uuid_utoa (loc->inode->gfid));
                goto out;
        }

        ret = mq_lock (this, &parent_loc, F_WRLCK);
        if (ret < 0)
                goto out;
        locked = _gf_true;

        if (contri.size >= 0) {
                /* contri paramater is supplied only for rename operation.
                 * remove xattr is alreday performed, we need to skip
                 * removexattr for rename operation
                 */
                remove_xattr = _gf_false;
                delta.size = contri.size;
                delta.file_count = contri.file_count;
                delta.dir_count = contri.dir_count;
        } else {
                remove_xattr = _gf_true;

                ret = mq_inode_ctx_get (loc->inode, this, &ctx);
                if (ret < 0) {
                        gf_log_callingfn (this->name, GF_LOG_WARNING, "ctx for"
                                          " the node %s is NULL", loc->path);
                        goto out;
                }

                contribution = mq_get_contribution_node (loc->parent, ctx);
                if (contribution == NULL) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "contribution for the node %s is NULL",
                                loc->path);
                        goto out;
                }

                LOCK (&contribution->lock);
                {
                        delta.size = contribution->contribution;
                        delta.file_count = contribution->file_count;
                        delta.dir_count = contribution->dir_count;
                }
                UNLOCK (&contribution->lock);
        }

        ret = mq_get_set_dirty (this, &parent_loc, 1, &prev_dirty);
        if (ret < 0)
                goto out;
        dirty = _gf_true;

        mq_sub_meta (&delta, NULL);

        if (remove_xattr) {
                ret = mq_remove_contri (this, loc, ctx, contribution, &delta,
                                        nlink);
                if (ret < 0)
                        goto out;
        }

        if (quota_meta_is_null (&delta))
                goto out;

        ret = mq_update_size (this, &parent_loc, &delta);
        if (ret < 0)
                goto out;

out:
        if (dirty) {
                if (ret < 0 || prev_dirty) {
                        /* On failure clear dirty status flag.
                         * In the next lookup inspect_directory_xattr
                         * can set the status flag and fix the
                         * dirty directory.
                         * Do the same if dir was dirty before
                         * the txn
                         */
                        ret = mq_inode_ctx_get (parent_loc.inode, this,
                                                &parent_ctx);
                        if (ret == 0)
                                mq_set_ctx_dirty_status (parent_ctx, _gf_false);
                } else {
                        ret = mq_mark_dirty (this, &parent_loc, 0);
                }
        }

        if (locked)
                ret = mq_lock (this, &parent_loc, F_UNLCK);

        if (ret >= 0)
                ret = mq_initiate_quota_blocking_txn (this, &parent_loc, NULL);

        loc_wipe (&parent_loc);

        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_reduce_parent_size_txn (xlator_t *this, loc_t *origin_loc,
                           quota_meta_t *contri, uint32_t nlink,
                           call_stub_t *stub)
{
        int32_t                  ret           = -1;
        loc_t                    loc           = {0, };
        gf_boolean_t             resume_stub   = _gf_true;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", origin_loc, out);

        ret = mq_prevalidate_txn (this, origin_loc, &loc, NULL, NULL);
        if (ret < 0)
                goto out;

        if (loc_is_root(&loc)) {
                ret = 0;
                goto out;
        }

        resume_stub = _gf_false;
        ret = mq_synctask1 (this, mq_reduce_parent_size_task, _gf_true, &loc,
                            contri, nlink, stub);
out:
        loc_wipe (&loc);

        if (resume_stub && stub)
                call_resume (stub);

        if (ret)
                gf_log_callingfn (this->name, GF_LOG_ERROR,
                                  "mq_reduce_parent_size_txn failed");

        return ret;
}

int
mq_initiate_quota_task (void *opaque)
{
        int32_t                ret        = -1;
        int32_t                prev_dirty = 0;
        loc_t                  child_loc  = {0,};
        loc_t                  parent_loc = {0,};
        gf_boolean_t           locked     = _gf_false;
        gf_boolean_t           dirty      = _gf_false;
        gf_boolean_t           status     = _gf_false;
        quota_meta_t           delta      = {0, };
        quota_synctask_t      *args       = NULL;
        xlator_t              *this       = NULL;
        loc_t                 *loc        = NULL;
        inode_contribution_t  *contri     = NULL;
        quota_inode_ctx_t     *ctx        = NULL;
        quota_inode_ctx_t     *parent_ctx = NULL;
        inode_t               *tmp_parent = NULL;

        GF_VALIDATE_OR_GOTO ("marker", opaque, out);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        this = args->this;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        THIS = this;

        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = mq_loc_copy (&child_loc, loc);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "loc copy failed");
                goto out;
        }

        while (!__is_root_gfid (child_loc.gfid)) {

                ret = mq_inode_ctx_get (child_loc.inode, this, &ctx);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "inode ctx get failed for %s, "
                                "aborting update txn", child_loc.path);
                        goto out;
                }

                /* To improve performance, abort current transaction
                 * if one is already in progress for same inode
                 */
                if (status == _gf_true) {
                        /* status will already set before txn start,
                         * so it should not be set in first
                         * loop iteration
                         */
                        ret = mq_test_and_set_ctx_updation_status (ctx,
                                                                   &status);
                        if (ret < 0 || status == _gf_true)
                                goto out;
                }

                if (child_loc.parent == NULL) {
                        ret = mq_build_ancestry (this, &child_loc);
                        if (ret < 0 || child_loc.parent == NULL) {
                                /* If application performs parallel remove
                                 * operations on same set of files/directories
                                 * then we may get ENOENT/ESTALE
                                 */
                                gf_log (this->name,
                                        (-ret == ENOENT || -ret == ESTALE)
                                        ? GF_LOG_DEBUG:GF_LOG_ERROR,
                                        "build ancestry failed for inode %s",
                                        uuid_utoa (child_loc.inode->gfid));
                                ret = -1;
                                goto out;
                        }
                }

                ret = mq_inode_loc_fill (NULL, child_loc.parent, &parent_loc);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "parent_loc fill "
                                "failed for child inode %s: ",
                                uuid_utoa (child_loc.inode->gfid));
                        goto out;
                }

                ret = mq_lock (this, &parent_loc, F_WRLCK);
                if (ret < 0)
                        goto out;
                locked = _gf_true;

                mq_set_ctx_updation_status (ctx, _gf_false);
                status = _gf_true;

                /* Contribution node can be NULL in below scenarios and
                   create if needed:

                   Scenario 1)
                   In this case create a new contribution node
                   Suppose hard link for a file f1 present in a directory d1 is
                   created in the directory d2 (as f2). Now, since d2's
                   contribution is not there in f1's inode ctx, d2's
                   contribution xattr wont be created and will create problems
                   for quota operations.

                   Don't create contribution if parent has been changed after
                   taking a lock, this can happen when rename is performed
                   and writes is still in-progress for the same file

                   Scenario 2)
                   When a rename operation is performed, contribution node
                   for olp path will be removed.

                   Create contribution node only if oldparent is same as
                   newparent.
                   Consider below example
                   1) rename FOP invoked on file 'x'
                   2) write is still in progress for file 'x'
                   3) rename takes a lock on old-parent
                   4) write-update txn blocked on old-parent to acquire lock
                   5) in rename_cbk, contri xattrs are removed and contribution
                      is deleted and lock is released
                   6) now write-update txn gets the lock and updates the
                      wrong parent as it was holding lock on old parent
                      so validate parent once the lock is acquired

                     For more information on this problem, please see
                     doc for marker_rename in file marker.c
                */
                contri = mq_get_contribution_node (child_loc.parent, ctx);
                if (contri == NULL) {
                        tmp_parent = inode_parent (child_loc.inode, 0, NULL);
                        if (tmp_parent == NULL) {
                                /* This can happen if application performs
                                 * parallel remove operations on same set
                                 * of files/directories
                                 */
                                gf_log (this->name, GF_LOG_WARNING, "parent is "
                                        "NULL for inode %s",
                                        uuid_utoa (child_loc.inode->gfid));
                                ret = -1;
                                goto out;
                        }
                        if (gf_uuid_compare(tmp_parent->gfid,
                                            parent_loc.gfid)) {
                                /* abort txn if parent has changed */
                                ret = 0;
                                goto out;
                        }

                        inode_unref (tmp_parent);
                        tmp_parent = NULL;

                        contri = mq_add_new_contribution_node (this, ctx,
                                                               &child_loc);
                        if (contri == NULL) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "create contribution node for %s, "
                                        "abort update txn", child_loc.path);
                                ret = -1;
                                goto out;
                        }
                }

                ret = mq_get_delta (this, &child_loc, &delta, ctx, contri);
                if (ret < 0)
                        goto out;

                if (quota_meta_is_null (&delta))
                        goto out;

                ret = mq_get_set_dirty (this, &parent_loc, 1, &prev_dirty);
                if (ret < 0)
                        goto out;
                dirty = _gf_true;

                ret = mq_update_contri (this, &child_loc, contri, &delta);
                if (ret < 0)
                        goto out;

                ret = mq_update_size (this, &parent_loc, &delta);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG, "rollback "
                                "contri updation");
                        mq_sub_meta (&delta, NULL);
                        mq_update_contri (this, &child_loc, contri, &delta);
                        goto out;
                }

                if (prev_dirty == 0) {
                        ret = mq_mark_dirty (this, &parent_loc, 0);
                } else {
                        ret = mq_inode_ctx_get (parent_loc.inode, this,
                                                &parent_ctx);
                        if (ret == 0)
                                mq_set_ctx_dirty_status (parent_ctx, _gf_false);
                }
                dirty = _gf_false;
                prev_dirty = 0;

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
                GF_REF_PUT (contri);
                contri = NULL;
        }

out:
        if (dirty) {
                if (ret < 0 || prev_dirty) {
                        /* On failure clear dirty status flag.
                         * In the next lookup inspect_directory_xattr
                         * can set the status flag and fix the
                         * dirty directory.
                         * Do the same if the dir was dirty before
                         * txn
                         */
                        ret = mq_inode_ctx_get (parent_loc.inode, this,
                                                &parent_ctx);
                        if (ret == 0)
                                mq_set_ctx_dirty_status (parent_ctx, _gf_false);
                } else {
                        ret = mq_mark_dirty (this, &parent_loc, 0);
                }
        }

        if (locked)
                ret = mq_lock (this, &parent_loc, F_UNLCK);

        if (ctx && status == _gf_false)
                mq_set_ctx_updation_status (ctx, _gf_false);

        loc_wipe (&child_loc);
        loc_wipe (&parent_loc);

        if (tmp_parent)
                inode_unref (tmp_parent);

        if (contri)
                GF_REF_PUT (contri);

        return 0;
}

int
_mq_initiate_quota_txn (xlator_t *this, loc_t *origin_loc, struct iatt *buf,
                        gf_boolean_t spawn)
{
        int32_t                 ret          = -1;
        quota_inode_ctx_t      *ctx          = NULL;
        gf_boolean_t            status       = _gf_true;
        loc_t                   loc          = {0,};

        ret = mq_prevalidate_txn (this, origin_loc, &loc, &ctx, buf);
        if (ret < 0)
                goto out;

        if (loc_is_root(&loc)) {
                ret = 0;
                goto out;
        }

        ret = mq_test_and_set_ctx_updation_status (ctx, &status);
        if (ret < 0 || status == _gf_true)
                goto out;

        ret = mq_synctask (this, mq_initiate_quota_task, spawn, &loc);

out:
        if (ret < 0 && status == _gf_false)
                mq_set_ctx_updation_status (ctx, _gf_false);

        loc_wipe (&loc);
        return ret;
}

int
mq_initiate_quota_txn (xlator_t *this, loc_t *loc, struct iatt *buf)
{
        int32_t                 ret          = -1;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_initiate_quota_txn (this, loc, buf, _gf_true);
out:
        return ret;
}

int
mq_initiate_quota_blocking_txn (xlator_t *this, loc_t *loc, struct iatt *buf)
{
        int32_t                 ret          = -1;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = _mq_initiate_quota_txn (this, loc, buf, _gf_false);
out:
        return ret;
}

int
mq_update_dirty_inode_task (void *opaque)
{
        int32_t               ret                         = -1;
        fd_t                 *fd                          = NULL;
        off_t                 offset                      = 0;
        gf_dirent_t           entries;
        gf_dirent_t          *entry                       = NULL;
        gf_boolean_t          locked                      = _gf_false;
        gf_boolean_t          updated                     = _gf_false;
        int32_t               dirty                       = 0;
        quota_meta_t          contri                      = {0, };
        quota_meta_t          size                        = {0, };
        quota_meta_t          contri_sum                  = {0, };
        quota_meta_t          delta                       = {0, };
        quota_synctask_t     *args                        = NULL;
        xlator_t             *this                        = NULL;
        loc_t                *loc                         = NULL;
        quota_inode_ctx_t    *ctx                         = NULL;
        dict_t               *xdata                       = NULL;
        char                  contri_key[QUOTA_KEY_MAX]   = {0, };

        GF_ASSERT (opaque);

        args = (quota_synctask_t *) opaque;
        loc = &args->loc;
        this = args->this;
        THIS = this;
        INIT_LIST_HEAD (&entries.list);

        ret = mq_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0)
                goto out;

        GET_CONTRI_KEY (this, contri_key, loc->gfid, ret);
        if (ret < 0)
                goto out;

        xdata = dict_new ();
        if (xdata == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                ret = -1;
                goto out;
        }

        ret = dict_set_int64 (xdata, contri_key, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "dict_set failed");
                goto out;
        }

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

        fd_bind (fd);
        while ((ret = syncop_readdirp (this, fd, 131072, offset, &entries,
                                       xdata, NULL)) != 0) {
                if (ret < 0) {
                        gf_log (this->name, (-ret == ENOENT || -ret == ESTALE)
                                ? GF_LOG_DEBUG:GF_LOG_ERROR, "readdirp failed "
                                "for %s: %s", loc->path, strerror (-ret));
                        goto out;
                }

                if (list_empty (&entries.list))
                        break;

                list_for_each_entry (entry, &entries.list, list) {
                        offset = entry->d_off;

                        if (!strcmp (entry->d_name, ".") ||
                            !strcmp (entry->d_name, ".."))
                                continue;

                        memset (&contri, 0, sizeof (contri));
                        quota_dict_get_meta (entry->dict, contri_key, &contri);
                        if (quota_meta_is_null (&contri))
                                continue;

                        mq_add_meta (&contri_sum, &contri);
                }

                gf_dirent_free (&entries);
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
        gf_dirent_free (&entries);

        if (fd)
                fd_unref (fd);

        if (xdata)
                dict_unref (xdata);

        if (ret < 0) {
                /* On failure clear dirty status flag.
                 * In the next lookup inspect_directory_xattr
                 * can set the status flag and fix the
                 * dirty directory
                 */
                if (ctx)
                        mq_set_ctx_dirty_status (ctx, _gf_false);
        } else if (dirty) {
                mq_mark_dirty (this, loc, 0);
        }

        if (locked)
                mq_lock (this, loc, F_UNLCK);

        if (updated)
                mq_initiate_quota_blocking_txn (this, loc, NULL);

        return ret;
}

int32_t
mq_update_dirty_inode_txn (xlator_t *this, loc_t *loc, quota_inode_ctx_t *ctx)
{
        int32_t          ret        = -1;
        gf_boolean_t     status     = _gf_true;

        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = mq_test_and_set_ctx_dirty_status (ctx, &status);
        if (ret < 0 || status == _gf_true)
                goto out;

        ret = mq_synctask (this, mq_update_dirty_inode_task, _gf_true, loc);
out:
        if (ret < 0 && status == _gf_false)
                mq_set_ctx_dirty_status (ctx, _gf_false);

        return ret;
}

int32_t
mq_inspect_directory_xattr (xlator_t *this, quota_inode_ctx_t *ctx,
                            inode_contribution_t *contribution, loc_t *loc,
                            dict_t *dict, struct iatt buf)
{
        int32_t               ret                          = -1;
        int8_t                dirty                        = -1;
        quota_meta_t          size                         = {0, };
        quota_meta_t          contri                       = {0, };
        quota_meta_t          delta                        = {0, };
        char                  contri_key[QUOTA_KEY_MAX]    = {0, };
        char                  size_key[QUOTA_KEY_MAX]      = {0, };
        gf_boolean_t          status                       = _gf_false;

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret < 0) {
                /* dirty is set only on the first file write operation
                 * so ignore this error
                 */
                ret = 0;
                dirty = 0;
        }

        GET_SIZE_KEY (this, size_key, ret);
        if (ret < 0)
                goto out;
        ret = _quota_dict_get_meta (this, dict, size_key, &size,
                                    IA_IFDIR, _gf_false);
        if (ret < 0)
                goto create_xattr;

        if (!loc_is_root(loc)) {
                GET_CONTRI_KEY (this, contri_key, contribution->gfid, ret);
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
                ctx->dirty = dirty;
        }
        UNLOCK (&ctx->lock);

        ret = mq_get_ctx_updation_status (ctx, &status);
        if (ret < 0 || status == _gf_true) {
                /* If the update txn is in progress abort inspection */
                ret = 0;
                goto out;
        }

        mq_compute_delta (&delta, &size, &contri);

        if (dirty) {
                ret = mq_update_dirty_inode_txn (this, loc, ctx);
                goto out;
        }

        if (!loc_is_root(loc) &&
            !quota_meta_is_null (&delta))
                mq_initiate_quota_txn (this, loc, NULL);

        ret = 0;
        goto out;

create_xattr:
        if (ret < 0)
                ret = mq_create_xattrs_txn (this, loc, NULL);

out:
        return ret;
}

int32_t
mq_inspect_file_xattr (xlator_t *this, quota_inode_ctx_t *ctx,
                       inode_contribution_t *contribution, loc_t *loc,
                       dict_t *dict, struct iatt buf)
{
        int32_t               ret                          = -1;
        quota_meta_t          size                         = {0, };
        quota_meta_t          contri                       = {0, };
        quota_meta_t          delta                        = {0, };
        char                  contri_key[QUOTA_KEY_MAX]    = {0, };
        gf_boolean_t          status                       = _gf_false;

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

        GET_CONTRI_KEY (this, contri_key, contribution->gfid, ret);
        if (ret < 0)
                goto out;

        ret = _quota_dict_get_meta (this, dict, contri_key, &contri,
                                    IA_IFREG, _gf_true);
        if (ret < 0) {
                ret = mq_create_xattrs_txn (this, loc, NULL);
        } else {
                LOCK (&contribution->lock);
                {
                        contribution->contribution = contri.size;
                        contribution->file_count = contri.file_count;
                        contribution->dir_count = contri.dir_count;
                }
                UNLOCK (&contribution->lock);

                ret = mq_get_ctx_updation_status (ctx, &status);
                if (ret < 0 || status == _gf_true) {
                        /* If the update txn is in progress abort inspection */
                        ret = 0;
                        goto out;
                }

                mq_compute_delta (&delta, &size, &contri);
                if (!quota_meta_is_null (&delta))
                        mq_initiate_quota_txn (this, loc, NULL);
        }
        /* TODO: revist this code when fixing hardlinks */

out:
        return ret;
}

int32_t
mq_xattr_state (xlator_t *this, loc_t *origin_loc, dict_t *dict,
                struct iatt buf)
{
        int32_t               ret             = -1;
        quota_inode_ctx_t    *ctx             = NULL;
        loc_t                 loc             = {0, };
        inode_contribution_t *contribution    = NULL;

        ret = mq_prevalidate_txn (this, origin_loc, &loc, &ctx, &buf);
        if (ret < 0 || loc.parent == NULL)
                goto out;

        if (!loc_is_root(&loc)) {
                contribution = mq_add_new_contribution_node (this, ctx, &loc);
                if (contribution == NULL) {
                        if (!gf_uuid_is_null (loc.inode->gfid))
                                gf_log (this->name, GF_LOG_WARNING,
                                        "cannot add a new contribution node "
                                        "(%s)", uuid_utoa (loc.gfid));
                        ret = -1;
                        goto out;
                }
        }

        if (buf.ia_type == IA_IFDIR)
                mq_inspect_directory_xattr (this, ctx, contribution, &loc, dict,
                                            buf);
        else
                mq_inspect_file_xattr (this, ctx, contribution, &loc, dict,
                                       buf);

out:
        loc_wipe (&loc);

        if (contribution)
                GF_REF_PUT (contribution);

        return ret;
}

int32_t
mq_req_xattr (xlator_t *this, loc_t *loc, dict_t *dict,
              char *contri_key, char *size_key)
{
        int32_t               ret                = -1;
        char                  key[QUOTA_KEY_MAX] = {0, };

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", dict, out);

        if (!loc_is_root(loc)) {
                ret = mq_dict_set_contribution (this, dict, loc, NULL,
                                                contri_key);
                if (ret < 0)
                        goto out;
        }

        GET_SIZE_KEY (this, key, ret);
        if (ret < 0)
                goto out;
        if (size_key)
                strncpy (size_key, key, QUOTA_KEY_MAX);

        ret = dict_set_uint64 (dict, key, 0);
        if (ret < 0)
                goto out;

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, 0);

out:
        if (ret < 0)
                gf_log_callingfn (this->name, GF_LOG_ERROR, "dict set failed");

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
                list_del_init (&contri->contri_list);
                GF_REF_PUT (contri);
        }

        LOCK_DESTROY (&ctx->lock);
        GF_FREE (ctx);
out:
        return 0;
}
