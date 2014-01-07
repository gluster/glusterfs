/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "statedump.h"
#include "inode.h"

#include "fd.h"

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"
#include "afr-self-heald.h"
#include "pump.h"

#define AFR_ICTX_OPENDIR_DONE_MASK     0x0000000100000000ULL
#define AFR_ICTX_READ_CHILD_MASK       0x00000000FFFFFFFFULL
#define AFR_STATISTICS_HISTORY_SIZE    50
int
afr_lookup_done_success_action (call_frame_t *frame, xlator_t *this,
                                gf_boolean_t fail_conflict);
void
afr_children_copy (int32_t *dst, int32_t *src, unsigned int child_count)
{
        int     i = 0;

        for (i = 0; i < child_count; i++)
                dst[i] = src[i];
}

void
afr_xattr_req_prepare (xlator_t *this, dict_t *xattr_req, const char *path)
{
        int             i           = 0;
        afr_private_t   *priv       = NULL;
        int             ret         = 0;

        priv   = this->private;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_set_uint64 (xattr_req, priv->pending_key[i],
                                       3 * sizeof(int32_t));
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: Unable to set dict value for %s",
                                path, priv->pending_key[i]);
                /* 3 = data+metadata+entry */
        }
        ret = dict_set_int32 (xattr_req, GF_GFIDLESS_LOOKUP, 1);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: failed to set gfidless "
                        "lookup", path);
        }
}

int
afr_lookup_xattr_req_prepare (afr_local_t *local, xlator_t *this,
                              dict_t *xattr_req, loc_t *loc, void **gfid_req)
{
        int     ret = -ENOMEM;

        GF_ASSERT (gfid_req);

        *gfid_req = NULL;
        local->xattr_req = dict_new ();
        if (!local->xattr_req)
                goto out;
        if (xattr_req)
                dict_copy (xattr_req, local->xattr_req);

        afr_xattr_req_prepare (this, local->xattr_req, loc->path);
        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_INODELK_COUNT, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_INODELK_COUNT);
        }
        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_ENTRYLK_COUNT, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_ENTRYLK_COUNT);
        }

        ret = dict_set_uint32 (local->xattr_req, GLUSTERFS_PARENT_ENTRYLK, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_PARENT_ENTRYLK);
        }

        ret = dict_get_ptr (local->xattr_req, "gfid-req", gfid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: failed to get the gfid from dict", loc->path);
                *gfid_req = NULL;
        } else {
                if (loc->parent != NULL)
                        dict_del (local->xattr_req, "gfid-req");
        }
        ret = 0;
out:
        return ret;
}

void
afr_lookup_save_gfid (uuid_t dst, void* new, const loc_t *loc)
{
        inode_t  *inode = NULL;

        inode = loc->inode;
        if (inode && !uuid_is_null (inode->gfid))
                uuid_copy (dst, inode->gfid);
        else if (!uuid_is_null (loc->gfid))
                uuid_copy (dst, loc->gfid);
        else if (new && !uuid_is_null (new))
                uuid_copy (dst, new);
}

int
afr_errno_count (int32_t *children, int *child_errno,
                 unsigned int child_count, int32_t op_errno)
{
        int i = 0;
        int errno_count = 0;
        int child = 0;

        for (i = 0; i < child_count; i++) {
                if (children) {
                        child = children[i];
                        if (child == -1)
                                break;
                } else {
                        child = i;
                }
                if (child_errno[child] == op_errno)
                        errno_count++;
        }
        return errno_count;
}

int32_t
afr_set_dict_gfid (dict_t *dict, uuid_t gfid)
{
        int ret       = 0;
        uuid_t *pgfid = NULL;

        GF_ASSERT (gfid);

        pgfid = GF_CALLOC (1, sizeof (uuid_t), gf_common_mt_char);
        if (!pgfid) {
                ret = -1;
                goto out;
        }

        uuid_copy (*pgfid, gfid);

        ret = dict_set_dynptr (dict, "gfid-req", pgfid, sizeof (uuid_t));
        if (ret)
                gf_log (THIS->name, GF_LOG_ERROR, "gfid set failed");

out:
        if (ret && pgfid)
                GF_FREE (pgfid);

        return ret;
}

void
afr_inode_ctx_destroy (afr_inode_ctx_t *ctx)
{
        if (!ctx)
                return;
        GF_FREE (ctx->fresh_children);
        GF_FREE (ctx);
}

afr_inode_ctx_t*
__afr_inode_ctx_get (inode_t *inode, xlator_t *this)
{
        int             ret      = 0;
        uint64_t        ctx_addr = 0;
        afr_inode_ctx_t *ctx     = NULL;
        afr_private_t   *priv    = NULL;

        priv = this->private;
        ret = __inode_ctx_get (inode, this, &ctx_addr);
        if (ret < 0)
                ctx_addr = 0;
        if (ctx_addr != 0) {
                ctx = (afr_inode_ctx_t*) (long) ctx_addr;
                goto out;
        }
        ctx = GF_CALLOC (1, sizeof (*ctx),
                         gf_afr_mt_inode_ctx_t);
        if (!ctx)
                goto fail;
        ctx->fresh_children = GF_CALLOC (priv->child_count,
                                         sizeof (*ctx->fresh_children),
                                         gf_afr_mt_int32_t);
        if (!ctx->fresh_children)
                goto fail;
        ret = __inode_ctx_put (inode, this, (uint64_t)ctx);
        if (ret) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "failed to "
                                  "set the inode ctx (%s)",
                                  uuid_utoa (inode->gfid));
                goto fail;
        }

out:
        return ctx;

fail:
        afr_inode_ctx_destroy (ctx);
        return NULL;
}

afr_inode_ctx_t*
afr_inode_ctx_get (inode_t *inode, xlator_t *this)
{
        afr_inode_ctx_t *ctx = NULL;

        LOCK (&inode->lock);
        {
                ctx = __afr_inode_ctx_get (inode, this);
        }
        UNLOCK (&inode->lock);
        return ctx;
}

void
afr_inode_get_ctx_params (xlator_t *this, inode_t *inode,
                          afr_inode_params_t *params)
{
        GF_ASSERT (inode);
        GF_ASSERT (params);

        afr_inode_ctx_t *ctx = NULL;
        afr_private_t   *priv = NULL;
        int             i = 0;
        int32_t         read_child = -1;
        int32_t         *fresh_children = NULL;

        priv = this->private;
        LOCK (&inode->lock);
        {
                ctx = __afr_inode_ctx_get (inode, this);
                if (!ctx)
                        goto unlock;
                switch (params->op) {
                case AFR_INODE_GET_READ_CTX:
                        fresh_children = params->u.read_ctx.children;
                        read_child = (int32_t)(ctx->masks &
                                               AFR_ICTX_READ_CHILD_MASK);
                        params->u.read_ctx.read_child = read_child;
                        if (!fresh_children)
                                goto unlock;
                        for (i = 0; i < priv->child_count; i++)
                                fresh_children[i] = ctx->fresh_children[i];
                        break;
                case AFR_INODE_GET_OPENDIR_DONE:
                        params->u.value = _gf_false;
                        if (ctx->masks & AFR_ICTX_OPENDIR_DONE_MASK)
                                params->u.value = _gf_true;
                        break;
                default:
                        GF_ASSERT (0);
                        break;
                }
        }
unlock:
        UNLOCK (&inode->lock);
}

gf_boolean_t
afr_is_split_brain (xlator_t *this, inode_t *inode)
{
        afr_inode_ctx_t *ctx = NULL;
        gf_boolean_t    spb  = _gf_false;

        ctx = afr_inode_ctx_get (inode, this);
        if (!ctx)
                goto out;
        if ((ctx->mdata_spb == SPB) || (ctx->data_spb == SPB))
                spb = _gf_true;
out:
        return spb;
}

gf_boolean_t
afr_is_opendir_done (xlator_t *this, inode_t *inode)
{
        afr_inode_params_t params = {0};

        params.op = AFR_INODE_GET_OPENDIR_DONE;
        afr_inode_get_ctx_params (this, inode, &params);
        return params.u.value;
}

int32_t
afr_inode_get_read_ctx (xlator_t *this, inode_t *inode, int32_t *fresh_children)
{
        afr_inode_params_t      params = {0};

        params.op = AFR_INODE_GET_READ_CTX;
        params.u.read_ctx.children = fresh_children;
        afr_inode_get_ctx_params (this, inode, &params);
        return params.u.read_ctx.read_child;
}

void
afr_inode_ctx_set_read_child (afr_inode_ctx_t *ctx, int32_t read_child)
{
        uint64_t        remaining_mask = 0;
        uint64_t        mask         = 0;

        remaining_mask = (~AFR_ICTX_READ_CHILD_MASK & ctx->masks);
        mask = (AFR_ICTX_READ_CHILD_MASK & read_child);
        ctx->masks = remaining_mask | mask;
}

void
afr_inode_ctx_set_read_ctx (afr_inode_ctx_t *ctx, int32_t read_child,
                            int32_t *fresh_children, int32_t child_count)
{
        int             i            = 0;

        afr_inode_ctx_set_read_child (ctx, read_child);
        for (i = 0; i < child_count; i++) {
                if (fresh_children)
                        ctx->fresh_children[i] = fresh_children[i];
                else
                        ctx->fresh_children[i] = -1;
        }
}

void
afr_inode_ctx_rm_stale_children (afr_inode_ctx_t *ctx, int32_t *stale_children,
                                 int32_t child_count)
{
        int             i            = 0;
        int32_t         read_child   = -1;

        GF_ASSERT (stale_children);
        for (i = 0; i < child_count; i++) {
                if (stale_children[i] == -1)
                        break;
                afr_children_rm_child (ctx->fresh_children,
                                       stale_children[i], child_count);
        }
        read_child = (int32_t)(ctx->masks & AFR_ICTX_READ_CHILD_MASK);
        if (!afr_is_child_present (ctx->fresh_children, child_count,
                                   read_child))
                afr_inode_ctx_set_read_child (ctx, ctx->fresh_children[0]);
}

void
afr_inode_ctx_set_opendir_done (afr_inode_ctx_t *ctx)
{
        uint64_t        remaining_mask = 0;
        uint64_t        mask = 0;

        remaining_mask = (~AFR_ICTX_OPENDIR_DONE_MASK & ctx->masks);
        mask = (0xFFFFFFFFFFFFFFFFULL & AFR_ICTX_OPENDIR_DONE_MASK);
        ctx->masks = remaining_mask | mask;
}

void
afr_inode_set_ctx_params (xlator_t *this, inode_t *inode,
                          afr_inode_params_t *params)
{
        GF_ASSERT (inode);
        GF_ASSERT (params);

        afr_inode_ctx_t *ctx            = NULL;
        afr_private_t   *priv           = NULL;
        int32_t         read_child      = -1;
        int32_t         *fresh_children = NULL;
        int32_t         *stale_children = NULL;

        priv = this->private;
        LOCK (&inode->lock);
        {
                ctx = __afr_inode_ctx_get (inode, this);
                if (!ctx)
                        goto unlock;
                switch (params->op) {
                case AFR_INODE_SET_READ_CTX:
                        read_child = params->u.read_ctx.read_child;
                        fresh_children = params->u.read_ctx.children;
                        afr_inode_ctx_set_read_ctx (ctx, read_child,
                                                    fresh_children,
                                                    priv->child_count);
                        break;
                case AFR_INODE_RM_STALE_CHILDREN:
                        stale_children = params->u.read_ctx.children;
                        afr_inode_ctx_rm_stale_children (ctx,
                                                         stale_children,
                                                         priv->child_count);
                        break;
                case AFR_INODE_SET_OPENDIR_DONE:
                        afr_inode_ctx_set_opendir_done (ctx);
                        break;
                default:
                        GF_ASSERT (0);
                        break;
                }
        }
unlock:
        UNLOCK (&inode->lock);
}

void
afr_set_split_brain (xlator_t *this, inode_t *inode, afr_spb_state_t mdata_spb,
                     afr_spb_state_t data_spb)
{
        afr_inode_ctx_t *ctx = NULL;

        ctx = afr_inode_ctx_get (inode, this);
        if (mdata_spb != DONT_KNOW)
                ctx->mdata_spb = mdata_spb;
        if (data_spb != DONT_KNOW)
                ctx->data_spb = data_spb;
}

void
afr_set_opendir_done (xlator_t *this, inode_t *inode)
{
        afr_inode_params_t params = {0};

        params.op = AFR_INODE_SET_OPENDIR_DONE;
        afr_inode_set_ctx_params (this, inode, &params);
}

void
afr_inode_set_read_ctx (xlator_t *this, inode_t *inode, int32_t read_child,
                        int32_t *fresh_children)
{
        afr_inode_params_t params = {0};
        afr_private_t      *priv  = NULL;

        priv = this->private;
        GF_ASSERT (read_child >= 0);
        GF_ASSERT (fresh_children);
        GF_ASSERT (afr_is_child_present (fresh_children, priv->child_count,
                                         read_child));

        params.op = AFR_INODE_SET_READ_CTX;
        params.u.read_ctx.read_child     = read_child;
        params.u.read_ctx.children = fresh_children;
        afr_inode_set_ctx_params (this, inode, &params);
}

void
afr_inode_rm_stale_children (xlator_t *this, inode_t *inode,
                             int32_t *stale_children)
{
        afr_inode_params_t params = {0};

        GF_ASSERT (stale_children);

        params.op = AFR_INODE_RM_STALE_CHILDREN;
        params.u.read_ctx.children = stale_children;
        afr_inode_set_ctx_params (this, inode, &params);
}

gf_boolean_t
afr_is_source_child (int32_t *sources, int32_t child_count, int32_t child)
{
        gf_boolean_t             source_xattrs = _gf_false;

        GF_ASSERT (child < child_count);

        if ((child >= 0) && (child < child_count) &&
             sources[child]) {
                source_xattrs = _gf_true;
        }
        return source_xattrs;
}

gf_boolean_t
afr_is_child_present (int32_t *success_children, int32_t child_count,
                      int32_t child)
{
        gf_boolean_t             success_child = _gf_false;
        int                      i = 0;

        GF_ASSERT (child < child_count);

        for (i = 0; i < child_count; i++) {
                if (success_children[i] == -1)
                        break;
                if (child == success_children[i]) {
                        success_child = _gf_true;
                        break;
                }
        }
        return success_child;
}

gf_boolean_t
afr_is_read_child (int32_t *success_children, int32_t *sources,
                   int32_t child_count, int32_t child)
{
        gf_boolean_t             success_child = _gf_false;
        gf_boolean_t             source        = _gf_false;

        if (child < 0) {
                return _gf_false;
        }

        GF_ASSERT (success_children);
        GF_ASSERT (child_count > 0);

        success_child = afr_is_child_present (success_children, child_count,
                                              child);
        if (!success_child)
                goto out;
        if (NULL == sources) {
                source = _gf_true;
                goto out;
        }
        source = afr_is_source_child (sources, child_count, child);
out:
        return (success_child && source);
}

int32_t
afr_hash_child (int32_t *success_children, int32_t child_count,
                unsigned int hmode, uuid_t gfid)
{
        uuid_t  gfid_copy = {0,};
        pid_t pid;

        if (!hmode) {
                return -1;
        }

        if (gfid) {
               uuid_copy(gfid_copy,gfid);
        }
        if (hmode > 1) {
                /*
                 * Why getpid?  Because it's one of the cheapest calls
                 * available - faster than gethostname etc. - and returns a
                 * constant-length value that's sure to be shorter than a UUID.
                 * It's still very unlikely to be the same across clients, so
                 * it still provides good mixing.  We're not trying for
                 * perfection here.  All we need is a low probability that
                 * multiple clients won't converge on the same subvolume.
                 */
                pid = getpid();
                memcpy (gfid_copy, &pid, sizeof(pid));
        }

        return SuperFastHash((char *)gfid_copy,
                             sizeof(gfid_copy)) % child_count;
}

/* If sources is NULL the xattrs are assumed to be of source for all
 * success_children.
 */
int
afr_select_read_child_from_policy (int32_t *success_children,
                                   int32_t child_count, int32_t prev_read_child,
                                   int32_t config_read_child, int32_t *sources,
                                   unsigned int hmode, uuid_t gfid)
{
        int32_t                  read_child   = -1;
        int                      i            = 0;

        GF_ASSERT (success_children);

        read_child = config_read_child;
        if (afr_is_read_child (success_children, sources, child_count,
                               read_child))
                goto out;

        read_child = prev_read_child;
        if (afr_is_read_child (success_children, sources, child_count,
                               read_child))
                goto out;

        read_child = afr_hash_child (success_children, child_count,
                                     hmode, gfid);
        if (afr_is_read_child (success_children, sources, child_count,
                               read_child)) {
                goto out;
        }

        for (i = 0; i < child_count; i++) {
                read_child = success_children[i];
                if (read_child < 0)
                        break;
                if (afr_is_read_child (success_children, sources, child_count,
                                       read_child))
                        goto out;
        }
        read_child = -1;

out:
        return read_child;
}

/* This function should be used when all the success_children are sources
 */
void
afr_set_read_ctx_from_policy (xlator_t *this, inode_t *inode,
                              int32_t *fresh_children, int32_t prev_read_child,
                              int32_t config_read_child, uuid_t gfid)
{
        int                      read_child = -1;
        afr_private_t            *priv = NULL;

        priv = this->private;
        read_child = afr_select_read_child_from_policy (fresh_children,
                                                        priv->child_count,
                                                        prev_read_child,
                                                        config_read_child,
                                                        NULL,
                                                        priv->hash_mode, gfid);
        if (read_child >= 0)
                afr_inode_set_read_ctx (this, inode, read_child,
                                        fresh_children);
}

/* afr_next_call_child ()
 * This is a common function used by all the read-type fops
 * This function should not be called with the inode's read_children array.
 * The fop's handler should make a copy of the inode's read_children,
 * preferred read_child into the local vars, because while this function is
 * in execution there is a chance for inode's read_ctx to change.
 */
int32_t
afr_next_call_child (int32_t *fresh_children, unsigned char *child_up,
                     size_t child_count, int32_t *last_index,
                     int32_t read_child)
{
        int             next_index      = 0;
        int32_t         next_call_child = -1;

        GF_ASSERT (last_index);

        next_index = *last_index;
retry:
        next_index++;
        if ((next_index >= child_count) ||
           (fresh_children[next_index] == -1))
                goto out;
        if ((fresh_children[next_index] == read_child) ||
           (!child_up[fresh_children[next_index]]))
                goto retry;
        *last_index = next_index;
        next_call_child = fresh_children[next_index];
out:
        return next_call_child;
}

 /* This function should not be called with the inode's read_children array.
 * The fop's handler should make a copy of the inode's read_children,
 * preferred read_child into the local vars, because while this function is
 * in execution there is a chance for inode's read_ctx to change.
 */
int32_t
afr_get_call_child (xlator_t *this, unsigned char *child_up, int32_t read_child,
                    int32_t *fresh_children,
                    int32_t *call_child, int32_t *last_index)
{
        int             ret   = 0;
        afr_private_t   *priv = NULL;
        int             i     = 0;

        GF_ASSERT (child_up);
        GF_ASSERT (call_child);
        GF_ASSERT (last_index);
        GF_ASSERT (fresh_children);

        if (read_child < 0) {
                ret = -EIO;
                goto out;
        }
        priv = this->private;
        *call_child = -1;
        *last_index = -1;

        if (child_up[read_child]) {
                *call_child = read_child;
        } else {
                for (i = 0; i < priv->child_count; i++) {
                        if (fresh_children[i] == -1)
                                break;
                        if (child_up[fresh_children[i]]) {
                                *call_child = fresh_children[i];
                                ret = 0;
                                break;
                        }
                }

                if (*call_child == -1) {
                        ret = -ENOTCONN;
                        goto out;
                }

                *last_index = i;
        }
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d, call_child: %d, "
                "last_index: %d", ret, *call_child, *last_index);
        return ret;
}

void
afr_reset_xattr (dict_t **xattr, unsigned int child_count)
{
        unsigned int i = 0;

        if (!xattr)
                goto out;
        for (i = 0; i < child_count; i++) {
                if (xattr[i]) {
                        dict_unref (xattr[i]);
                        xattr[i] = NULL;
                }
        }
out:
        return;
}

void
afr_xattr_array_destroy (dict_t **xattr, unsigned int child_count)
{
        afr_reset_xattr (xattr, child_count);
        GF_FREE (xattr);
}

void
afr_local_sh_cleanup (afr_local_t *local, xlator_t *this)
{
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;

        sh = &local->self_heal;
        priv = this->private;

        if (sh->data_sh_info && strcmp (sh->data_sh_info, ""))
                GF_FREE (sh->data_sh_info);

        if (sh->metadata_sh_info && strcmp (sh->metadata_sh_info, ""))
                GF_FREE (sh->metadata_sh_info);

        GF_FREE (sh->buf);

        GF_FREE (sh->parentbufs);

        if (sh->inode)
                inode_unref (sh->inode);

        afr_xattr_array_destroy (sh->xattr, priv->child_count);

        GF_FREE (sh->child_errno);

        afr_matrix_cleanup (sh->pending_matrix, priv->child_count);
        afr_matrix_cleanup (sh->delta_matrix, priv->child_count);

        GF_FREE (sh->sources);

        GF_FREE (sh->success);

        GF_FREE (sh->locked_nodes);

        if (sh->healing_fd) {
                fd_unref (sh->healing_fd);
                sh->healing_fd = NULL;
        }

        GF_FREE ((char *)sh->linkname);

        GF_FREE (sh->success_children);

        GF_FREE (sh->fresh_children);

        GF_FREE (sh->fresh_parent_dirs);

        loc_wipe (&sh->parent_loc);
        loc_wipe (&sh->lookup_loc);

        GF_FREE (sh->checksum);

        GF_FREE (sh->write_needed);
        if (sh->healing_fd)
                fd_unref (sh->healing_fd);
}


void
afr_local_transaction_cleanup (afr_local_t *local, xlator_t *this)
{
        afr_private_t *priv    = NULL;
        int           i        = 0;

        priv = this->private;

        afr_matrix_cleanup (local->pending, priv->child_count);
        afr_matrix_cleanup (local->transaction.txn_changelog,
                            priv->child_count);

        GF_FREE (local->internal_lock.locked_nodes);

        for (i = 0; local->internal_lock.inodelk[i].domain; i++) {
                GF_FREE (local->internal_lock.inodelk[i].locked_nodes);
        }

        GF_FREE (local->internal_lock.lower_locked_nodes);

        afr_entry_lockee_cleanup (&local->internal_lock);

        GF_FREE (local->transaction.pre_op);
        GF_FREE (local->transaction.eager_lock);

        GF_FREE (local->transaction.basename);
        GF_FREE (local->transaction.new_basename);

        loc_wipe (&local->transaction.parent_loc);
        loc_wipe (&local->transaction.new_parent_loc);

        GF_FREE (local->transaction.postop_piggybacked);
}


void
afr_local_cleanup (afr_local_t *local, xlator_t *this)
{
        afr_private_t * priv = NULL;

        if (!local)
                return;

        afr_local_sh_cleanup (local, this);

        afr_local_transaction_cleanup (local, this);

        priv = this->private;

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->xattr_req)
                dict_unref (local->xattr_req);

        if (local->dict)
                dict_unref (local->dict);

	GF_FREE(local->replies);

        GF_FREE (local->child_up);

        GF_FREE (local->child_errno);

        GF_FREE (local->fresh_children);

        { /* lookup */
                if (local->cont.lookup.xattrs) {
                        afr_reset_xattr (local->cont.lookup.xattrs,
                                         priv->child_count);
                        GF_FREE (local->cont.lookup.xattrs);
                        local->cont.lookup.xattrs = NULL;
                }

                if (local->cont.lookup.xattr) {
                        dict_unref (local->cont.lookup.xattr);
                }

                if (local->cont.lookup.inode) {
                        inode_unref (local->cont.lookup.inode);
                }

                GF_FREE (local->cont.lookup.postparents);

                GF_FREE (local->cont.lookup.bufs);

                GF_FREE (local->cont.lookup.success_children);

                GF_FREE (local->cont.lookup.sources);
                afr_matrix_cleanup (local->cont.lookup.pending_matrix,
                                    priv->child_count);
        }

        { /* getxattr */
                GF_FREE (local->cont.getxattr.name);
        }

        { /* lk */
                GF_FREE (local->cont.lk.locked_nodes);
        }

        { /* create */
                if (local->cont.create.fd)
                        fd_unref (local->cont.create.fd);
                if (local->cont.create.params)
                        dict_unref (local->cont.create.params);
        }

        { /* mknod */
                if (local->cont.mknod.params)
                        dict_unref (local->cont.mknod.params);
        }

        { /* mkdir */
                if (local->cont.mkdir.params)
                        dict_unref (local->cont.mkdir.params);
        }

        { /* symlink */
                if (local->cont.symlink.params)
                        dict_unref (local->cont.symlink.params);
        }

        { /* writev */
                GF_FREE (local->cont.writev.vector);
        }

        { /* setxattr */
                if (local->cont.setxattr.dict)
                        dict_unref (local->cont.setxattr.dict);
        }

        { /* fsetxattr */
                if (local->cont.fsetxattr.dict)
                        dict_unref (local->cont.fsetxattr.dict);
        }

        { /* removexattr */
                GF_FREE (local->cont.removexattr.name);
        }
        { /* xattrop */
                if (local->cont.xattrop.xattr)
                        dict_unref (local->cont.xattrop.xattr);
        }
        { /* fxattrop */
                if (local->cont.fxattrop.xattr)
                        dict_unref (local->cont.fxattrop.xattr);
        }
        { /* symlink */
                GF_FREE (local->cont.symlink.linkpath);
        }

        { /* opendir */
                GF_FREE (local->cont.opendir.checksum);
        }

        { /* readdirp */
                if (local->cont.readdir.dict)
                        dict_unref (local->cont.readdir.dict);
        }

        if (local->xdata_req)
                dict_unref (local->xdata_req);

        if (local->xdata_rsp)
                dict_unref (local->xdata_rsp);
}


int
afr_frame_return (call_frame_t *frame)
{
        afr_local_t *local = NULL;
        int          call_count = 0;

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_count = --local->call_count;
        }
        UNLOCK (&frame->lock);

        return call_count;
}

int
afr_set_elem_count_get (unsigned char *elems, int child_count)
{
        int i   = 0;
        int ret = 0;

        for (i = 0; i < child_count; i++)
                if (elems[i])
                        ret++;
        return ret;
}

/**
 * up_children_count - return the number of children that are up
 */

unsigned int
afr_up_children_count (unsigned char *child_up, unsigned int child_count)
{
        return afr_set_elem_count_get (child_up, child_count);
}

unsigned int
afr_locked_children_count (unsigned char *children, unsigned int child_count)
{
        return afr_set_elem_count_get (children, child_count);
}

unsigned int
afr_pre_op_done_children_count (unsigned char *pre_op,
                                unsigned int child_count)
{
        return afr_set_elem_count_get (pre_op, child_count);
}

gf_boolean_t
afr_is_fresh_lookup (loc_t *loc, xlator_t *this)
{
        uint64_t          ctx = 0;
        int32_t           ret = 0;

        GF_ASSERT (loc);
        GF_ASSERT (this);
        GF_ASSERT (loc->inode);

        ret = inode_ctx_get (loc->inode, this, &ctx);
        if (0 == ret)
                return _gf_false;
        return _gf_true;
}

void
afr_update_loc_gfids (loc_t *loc, struct iatt *buf, struct iatt *postparent)
{
        GF_ASSERT (loc);
        GF_ASSERT (buf);

        uuid_copy (loc->gfid, buf->ia_gfid);
        if (postparent)
                uuid_copy (loc->pargfid, postparent->ia_gfid);
}

/*
 * Quota size xattrs are not maintained by afr. There is a
 * possibility that they differ even when both the directory changelog xattrs
 * suggest everything is fine. So if there is at least one 'source' check among
 * the sources which has the maximum quota size. Otherwise check among all the
 * available ones for maximum quota size. This way if there is a source and
 * stale copies it always votes for the 'source'.
 * */

static void
afr_handle_quota_size (afr_local_t *local, xlator_t *this,
                       dict_t *rsp_dict)
{
        int32_t       *sources       = NULL;
        dict_t        *xattr         = NULL;
        data_t        *max_data      = NULL;
        int64_t       max_quota_size = -1;
        data_t        *data          = NULL;
        int64_t       *size          = NULL;
        int64_t       quota_size     = -1;
        afr_private_t *priv          = NULL;
        int           i              = 0;
        int           ret            = -1;
        gf_boolean_t  source_present = _gf_false;

        priv    = this->private;
        sources = local->cont.lookup.sources;

        if (rsp_dict == NULL) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "%s: Invalid "
                                  "response dictionary", local->loc.path);
                return;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (sources[i]) {
                        source_present = _gf_true;
                        break;
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                /*
                 * If there is at least one source lets check
                 * for maximum quota sizes among sources, otherwise take the
                 * maximum of the ones present to be on the safer side.
                 */
                if (source_present && !sources[i])
                        continue;

                xattr = local->cont.lookup.xattrs[i];
                if (!xattr)
                        continue;

                data = dict_get (xattr, QUOTA_SIZE_KEY);
                if (!data)
                        continue;

                size = (int64_t*)data->data;
                quota_size = ntoh64(*size);
                gf_log (this->name, GF_LOG_DEBUG, "%s: %d, size: %"PRId64,
                        local->loc.path, i, quota_size);
                if (quota_size > max_quota_size) {
                        if (max_data)
                                data_unref (max_data);

                        max_quota_size = quota_size;
                        max_data = data_ref (data);
                }
        }

        if (max_data) {
                ret = dict_set (rsp_dict, QUOTA_SIZE_KEY, max_data);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "%s: Failed to set "
                                "quota size", local->loc.path);
                }

                data_unref (max_data);
        }
}

int
afr_lookup_build_response_params (afr_local_t *local, xlator_t *this)
{
        struct iatt     *buf = NULL;
        struct iatt     *postparent = NULL;
        dict_t          **xattr = NULL;
        int32_t         *success_children = NULL;
        int32_t         *sources = NULL;
        afr_private_t   *priv = NULL;
        int32_t         read_child = -1;
        int             ret = 0;
        int             i = 0;

        GF_ASSERT (local);

        buf = &local->cont.lookup.buf;
        postparent = &local->cont.lookup.postparent;
        xattr = &local->cont.lookup.xattr;
        priv = this->private;

        read_child = afr_inode_get_read_ctx (this, local->cont.lookup.inode,
                                             local->fresh_children);
        if (read_child < 0) {
                ret = -1;
                goto out;
        }
        success_children = local->cont.lookup.success_children;
        sources = local->cont.lookup.sources;
        memset (sources, 0, sizeof (*sources) * priv->child_count);
        afr_children_intersection_get (local->fresh_children, success_children,
                                       sources, priv->child_count);
        if (!sources[read_child]) {
                read_child = -1;
                for (i = 0; i < priv->child_count; i++) {
                        if (sources[i]) {
                                read_child = i;
                                break;
                        }
                }
        }
        if (read_child < 0) {
                ret = -1;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Building lookup response from %d",
                read_child);
        if (!*xattr)
                *xattr = dict_ref (local->cont.lookup.xattrs[read_child]);

        *buf = local->cont.lookup.bufs[read_child];
        *postparent = local->cont.lookup.postparents[read_child];

        if (dict_get (local->xattr_req, QUOTA_SIZE_KEY))
                afr_handle_quota_size (local, this, *xattr);

        if (IA_INVAL == local->cont.lookup.inode->ia_type) {
                /* fix for RT #602 */
                local->cont.lookup.inode->ia_type = buf->ia_type;
        }
out:
        return ret;
}

static void
afr_lookup_update_lk_counts (afr_local_t *local, xlator_t *this,
                            int child_index, dict_t *xattr)
{
        uint32_t inodelk_count = 0;
        uint32_t entrylk_count = 0;
        int      ret           = -1;
        uint32_t parent_entrylk = 0;

        GF_ASSERT (local);
        GF_ASSERT (this);
        GF_ASSERT (xattr);
        GF_ASSERT (child_index >= 0);

        ret = dict_get_uint32 (xattr, GLUSTERFS_INODELK_COUNT,
                               &inodelk_count);
        if (ret == 0)
                local->inodelk_count += inodelk_count;

        ret = dict_get_uint32 (xattr, GLUSTERFS_ENTRYLK_COUNT,
                               &entrylk_count);
        if (ret == 0)
                local->entrylk_count += entrylk_count;
        ret = dict_get_uint32 (xattr, GLUSTERFS_PARENT_ENTRYLK,
                               &parent_entrylk);
        if (!ret)
                local->cont.lookup.parent_entrylk += parent_entrylk;
}

/*
 * It's important to maintain a commutative property on do_*_self_heal and
 * found*; once set, they must not be cleared by a subsequent iteration or
 * call, so that they represent a logical OR of all iterations and calls
 * regardless of child/key order.  That allows the caller to call us multiple
 * times without having to use a separate variable as a "reduce" accumulator.
 */
static void
afr_lookup_set_self_heal_params_by_xattr (afr_local_t *local, xlator_t *this,
                                          dict_t *xattr)
{
        afr_private_t *priv        = NULL;
        int            i           = 0;
        int            ret         = -1;
        void          *pending_raw = NULL;
        int32_t       *pending     = NULL;

        GF_ASSERT (local);
        GF_ASSERT (this);
        GF_ASSERT (xattr);

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_get_ptr (xattr, priv->pending_key[i],
                                    &pending_raw);
                if (ret != 0) {
                        continue;
                }
                pending = pending_raw;

                if (pending[AFR_METADATA_TRANSACTION]) {
                        gf_log(this->name, GF_LOG_DEBUG,
                               "metadata self-heal is pending for %s.",
                               local->loc.path);
                        local->self_heal.do_metadata_self_heal = _gf_true;
                }

                if (pending[AFR_ENTRY_TRANSACTION]) {
                        gf_log(this->name, GF_LOG_DEBUG,
                               "entry self-heal is pending for %s.",
                               local->loc.path);
                        local->self_heal.do_entry_self_heal = _gf_true;
                }

                if (pending[AFR_DATA_TRANSACTION]) {
                        gf_log(this->name, GF_LOG_DEBUG,
                               "data self-heal is pending for %s.",
                               local->loc.path);
                        local->self_heal.do_data_self_heal = _gf_true;
                }
        }
}

void
afr_lookup_check_set_metadata_split_brain (afr_local_t *local, xlator_t *this)
{
        int32_t                  *sources = NULL;
        afr_private_t            *priv = NULL;
        int32_t                  subvol_status = 0;
        int32_t                  *success_children   = NULL;
        dict_t                   **xattrs = NULL;
        struct iatt              *bufs = NULL;
        int32_t                  **pending_matrix = NULL;

        priv = this->private;

        sources = GF_CALLOC (priv->child_count, sizeof (*sources),
                             gf_afr_mt_int32_t);
        if (NULL == sources)
                goto out;
        success_children = local->cont.lookup.success_children;
        xattrs = local->cont.lookup.xattrs;
        bufs = local->cont.lookup.bufs;
        pending_matrix = local->cont.lookup.pending_matrix;
        afr_build_sources (this, xattrs, bufs, pending_matrix,
                           sources, success_children, AFR_METADATA_TRANSACTION,
                           &subvol_status, _gf_false);
        if (subvol_status & SPLIT_BRAIN)
                local->cont.lookup.possible_spb = _gf_true;
out:
        GF_FREE (sources);
}

static void
afr_detect_self_heal_by_iatt (afr_local_t *local, xlator_t *this,
                            struct iatt *buf, struct iatt *lookup_buf)
{
        if (PERMISSION_DIFFERS (buf, lookup_buf)) {
                /* mismatching permissions */
                gf_log (this->name, GF_LOG_DEBUG,
                        "permissions differ for %s ", local->loc.path);
                local->self_heal.do_metadata_self_heal = _gf_true;
        }

        if (OWNERSHIP_DIFFERS (buf, lookup_buf)) {
                /* mismatching permissions */
                local->self_heal.do_metadata_self_heal = _gf_true;
                gf_log (this->name, GF_LOG_DEBUG,
                        "ownership differs for %s ", local->loc.path);
        }

        if (SIZE_DIFFERS (buf, lookup_buf)
            && IA_ISREG (buf->ia_type)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "size differs for %s ", local->loc.path);
                local->self_heal.do_data_self_heal = _gf_true;
        }

        if (uuid_compare (buf->ia_gfid, lookup_buf->ia_gfid)) {
                /* mismatching gfid */
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: gfid different on subvolume", local->loc.path);
        }
}

static void
afr_detect_self_heal_by_split_brain_status (afr_local_t *local, xlator_t *this)
{
        gf_boolean_t split_brain = _gf_false;
        afr_self_heal_t *sh = NULL;

        sh = &local->self_heal;

        split_brain = afr_is_split_brain (this, local->cont.lookup.inode);
        split_brain = split_brain || local->cont.lookup.possible_spb;
        if ((local->success_count > 0) && split_brain &&
            IA_ISREG (local->cont.lookup.inode->ia_type)) {
                sh->force_confirm_spb = _gf_true;
                gf_log (this->name, GF_LOG_DEBUG,
                        "split brain detected during lookup of %s.",
                        local->loc.path);
        }
}

static void
afr_detect_self_heal_by_lookup_status (afr_local_t *local, xlator_t *this)
{
        GF_ASSERT (local);
        GF_ASSERT (this);

        if ((local->success_count > 0) && (local->enoent_count > 0)) {
                local->self_heal.do_metadata_self_heal = _gf_true;
                local->self_heal.do_data_self_heal     = _gf_true;
                local->self_heal.do_entry_self_heal    = _gf_true;
                local->self_heal.do_gfid_self_heal    = _gf_true;
                local->self_heal.do_missing_entry_self_heal    = _gf_true;
                gf_log(this->name, GF_LOG_DEBUG,
                       "entries are missing in lookup of %s.",
                       local->loc.path);
        }

        return;
}

gf_boolean_t
afr_can_self_heal_proceed (afr_self_heal_t *sh, afr_private_t *priv)
{
        GF_ASSERT (sh);
        GF_ASSERT (priv);

        if (sh->force_confirm_spb)
                return _gf_true;
        return (sh->do_gfid_self_heal
                || sh->do_missing_entry_self_heal
                || (afr_data_self_heal_enabled (priv->data_self_heal) &&
                    sh->do_data_self_heal)
                || (priv->metadata_self_heal && sh->do_metadata_self_heal)
                || (priv->entry_self_heal && sh->do_entry_self_heal));
}

afr_transaction_type
afr_transaction_type_get (ia_type_t ia_type)
{
        afr_transaction_type    type = AFR_METADATA_TRANSACTION;

        GF_ASSERT (ia_type != IA_INVAL);

        if (IA_ISDIR (ia_type)) {
                type = AFR_ENTRY_TRANSACTION;
        } else if (IA_ISREG (ia_type)) {
                type = AFR_DATA_TRANSACTION;
        }
        return type;
}

int
afr_lookup_select_read_child (afr_local_t *local, xlator_t *this,
                              int32_t *read_child)
{
        ia_type_t               ia_type        = IA_INVAL;
        int32_t                 source         = -1;
        int                     ret            = -1;
        dict_t                  **xattrs       = NULL;
        int32_t                 *success_children = NULL;
        afr_transaction_type    type           = AFR_METADATA_TRANSACTION;
        uuid_t                  *gfid          = NULL;

        GF_ASSERT (local);
        GF_ASSERT (this);
        GF_ASSERT (local->success_count > 0);

        success_children = local->cont.lookup.success_children;
        /*We can take the success_children[0] only because we already
         *handle the conflicting children other wise, we could select the
         *read_child based on wrong file type
         */
        ia_type = local->cont.lookup.bufs[success_children[0]].ia_type;
        type = afr_transaction_type_get (ia_type);
        xattrs = local->cont.lookup.xattrs;
        gfid = &local->cont.lookup.buf.ia_gfid;
        source = afr_lookup_select_read_child_by_txn_type (this, local, xattrs,
                                                           type, *gfid);
        if (source < 0) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to select source "
                        "for %s", local->loc.path);
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG, "Source selected as %d for %s",
                source, local->loc.path);
        *read_child = source;
        ret = 0;
out:
        return ret;
}

static inline gf_boolean_t
afr_is_transaction_running (afr_local_t *local)
{
        GF_ASSERT (local->fop == GF_FOP_LOOKUP);
        return ((local->inodelk_count > 0) || (local->entrylk_count > 0));
}

void
afr_launch_self_heal (call_frame_t *frame, xlator_t *this, inode_t *inode,
                      gf_boolean_t background, ia_type_t ia_type, char *reason,
                      void (*gfid_sh_success_cbk) (call_frame_t *sh_frame,
                                                   xlator_t *this),
                      int (*unwind) (call_frame_t *frame, xlator_t *this,
                                     int32_t op_ret, int32_t op_errno,
                                     int32_t sh_failed))
{
        afr_local_t             *local = NULL;
        char                    sh_type_str[256] = {0,};
        char                    *bg = "";

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (inode);
        GF_ASSERT (ia_type != IA_INVAL);

        local = frame->local;
        local->self_heal.background = background;
        local->self_heal.type       = ia_type;
        local->self_heal.unwind     = unwind;
        local->self_heal.gfid_sh_success_cbk     = gfid_sh_success_cbk;

        afr_self_heal_type_str_get (&local->self_heal,
                                    sh_type_str,
                                    sizeof (sh_type_str));

        if (background)
                bg = "background";
        gf_log (this->name, GF_LOG_DEBUG,
                "%s %s self-heal triggered. path: %s, reason: %s", bg,
                sh_type_str, local->loc.path, reason);

        afr_self_heal (frame, this, inode);
}

unsigned int
afr_gfid_missing_count (const char *xlator_name, int32_t *success_children,
                        struct iatt *bufs, unsigned int child_count,
                        const char *path)
{
        unsigned int    gfid_miss_count   = 0;
        int             i              = 0;
        struct iatt     *child1        = NULL;

        for (i = 0; i < child_count; i++) {
                if (success_children[i] == -1)
                        break;
                child1 = &bufs[success_children[i]];
                if (uuid_is_null (child1->ia_gfid)) {
                        gf_log (xlator_name, GF_LOG_DEBUG, "%s: gfid is null"
                                " on subvolume %d", path, success_children[i]);
                        gfid_miss_count++;
                }
        }

        return gfid_miss_count;
}

static int
afr_lookup_gfid_missing_count (afr_local_t *local, xlator_t *this)
{
        int32_t         *success_children = NULL;
        afr_private_t   *priv          = NULL;
        struct iatt     *bufs          = NULL;
        int             miss_count     = 0;

        priv = this->private;
        bufs = local->cont.lookup.bufs;
        success_children = local->cont.lookup.success_children;

        miss_count =  afr_gfid_missing_count (this->name, success_children,
                                              bufs, priv->child_count,
                                              local->loc.path);
        return miss_count;
}

gf_boolean_t
afr_conflicting_iattrs (struct iatt *bufs, int32_t *success_children,
                        unsigned int child_count, const char *path,
                        const char *xlator_name)
{
        gf_boolean_t    conflicting    = _gf_false;
        int             i              = 0;
        struct iatt     *child1        = NULL;
        struct iatt     *child2        = NULL;
        uuid_t          *gfid          = NULL;

        for (i = 0; i < child_count; i++) {
                if (success_children[i] == -1)
                        break;
                child1 = &bufs[success_children[i]];
                if ((!gfid) && (!uuid_is_null (child1->ia_gfid)))
                        gfid = &child1->ia_gfid;

                if (i == 0)
                        continue;

                child2 = &bufs[success_children[i-1]];
                if (FILETYPE_DIFFERS (child1, child2)) {
                        gf_log (xlator_name, GF_LOG_DEBUG, "%s: filetype "
                                "differs on subvolumes (%d, %d)", path,
                                success_children[i-1], success_children[i]);
                        conflicting = _gf_true;
                        goto out;
                }
                if (!gfid || uuid_is_null (child1->ia_gfid))
                        continue;
                if (uuid_compare (*gfid, child1->ia_gfid)) {
                       gf_log (xlator_name, GF_LOG_DEBUG, "%s: gfid differs"
                               " on subvolume %d", path, success_children[i]);
                       conflicting = _gf_true;
                       goto out;
                }
        }
out:
        return conflicting;
}

/* afr_update_gfid_from_iatts: This function should be called only if the
 * iatts are not conflicting.
 */
void
afr_update_gfid_from_iatts (uuid_t uuid, struct iatt *bufs,
                            int32_t *success_children, unsigned int child_count)
{
        uuid_t          *gfid = NULL;
        int             i = 0;
        int             child = 0;

        for (i = 0; i < child_count; i++) {
                child = success_children[i];
                if (child == -1)
                        break;
                if ((!gfid) && (!uuid_is_null (bufs[child].ia_gfid))) {
                        gfid = &bufs[child].ia_gfid;
                } else if (gfid && (!uuid_is_null (bufs[child].ia_gfid))) {
                        if (uuid_compare (*gfid, bufs[child].ia_gfid)) {
                                GF_ASSERT (0);
                                goto out;
                        }
                }
        }
        if (gfid && (!uuid_is_null (*gfid)))
                uuid_copy (uuid, *gfid);
out:
        return;
}

static gf_boolean_t
afr_lookup_conflicting_entries (afr_local_t *local, xlator_t *this)
{
        afr_private_t           *priv = NULL;
        gf_boolean_t            conflict = _gf_false;

        priv = this->private;
        conflict =  afr_conflicting_iattrs (local->cont.lookup.bufs,
                                            local->cont.lookup.success_children,
                                            priv->child_count, local->loc.path,
                                            this->name);
        return conflict;
}

gf_boolean_t
afr_open_only_data_self_heal (char *data_self_heal)
{
        return !strcmp (data_self_heal, "open");
}

gf_boolean_t
afr_data_self_heal_enabled (char *data_self_heal)
{
        gf_boolean_t    enabled = _gf_false;

        if (gf_string2boolean (data_self_heal, &enabled) == -1) {
                enabled = !strcmp (data_self_heal, "open");
                GF_ASSERT (enabled);
        }

        return enabled;
}

static void
afr_lookup_set_self_heal_params (afr_local_t *local, xlator_t *this)
{
        int                     i = 0;
        struct iatt             *bufs = NULL;
        dict_t                  **xattr = NULL;
        afr_private_t           *priv = NULL;
        int32_t                 child1 = -1;
        int32_t                 child2 = -1;
        afr_self_heal_t         *sh = NULL;

        priv  = this->private;
        sh = &local->self_heal;

        afr_detect_self_heal_by_lookup_status (local, this);

        if (afr_lookup_gfid_missing_count (local, this))
                local->self_heal.do_gfid_self_heal    = _gf_true;

        if (_gf_true == afr_lookup_conflicting_entries (local, this))
                local->self_heal.do_missing_entry_self_heal    = _gf_true;
        else
                afr_update_gfid_from_iatts (local->self_heal.sh_gfid_req,
                                            local->cont.lookup.bufs,
                                            local->cont.lookup.success_children,
                                            priv->child_count);

        bufs = local->cont.lookup.bufs;
        for (i = 1; i < local->success_count; i++) {
                child1 = local->cont.lookup.success_children[i-1];
                child2 = local->cont.lookup.success_children[i];
                afr_detect_self_heal_by_iatt (local, this,
                                              &bufs[child1], &bufs[child2]);
        }

        xattr = local->cont.lookup.xattrs;
        for (i = 0; i < local->success_count; i++) {
                child1 = local->cont.lookup.success_children[i];
                afr_lookup_set_self_heal_params_by_xattr (local, this,
                                                          xattr[child1]);
        }
        if (afr_open_only_data_self_heal (priv->data_self_heal))
                sh->do_data_self_heal = _gf_false;
        if (sh->do_metadata_self_heal)
                afr_lookup_check_set_metadata_split_brain (local, this);
        afr_detect_self_heal_by_split_brain_status (local, this);
}

int
afr_self_heal_lookup_unwind (call_frame_t *frame, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             int32_t sh_failed)
{
        afr_local_t *local = NULL;
        int         ret    = -1;
        dict_t      *xattr = NULL;

        local = frame->local;

        if (op_ret == -1) {
                local->op_ret = -1;
		local->op_errno = afr_most_important_error(local->op_errno,
							   op_errno, _gf_true);

                goto out;
        } else {
                local->op_ret = 0;
        }

        afr_lookup_done_success_action (frame, this, _gf_true);
        xattr = local->cont.lookup.xattr;
        if (xattr) {
                ret = dict_set_int32 (xattr, "sh-failed", sh_failed);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "%s: Failed to set "
                                "sh-failed to %d", local->loc.path, sh_failed);

                if (local->self_heal.actual_sh_started == _gf_true &&
                    sh_failed == 0) {
                        ret = dict_set_int32 (xattr, "actual-sh-done", 1);
                        if (ret)
                                gf_log(this->name, GF_LOG_ERROR, "%s: Failed to"
                                       " set actual-sh-done to %d",
                                       local->loc.path,
                                       local->self_heal.actual_sh_started);
                }
        }
out:
        AFR_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
                          local->cont.lookup.inode, &local->cont.lookup.buf,
                          local->cont.lookup.xattr,
                          &local->cont.lookup.postparent);

        return 0;
}

//TODO: At the moment only lookup needs this, so not doing any checks, in the
// future we will have to do fop specific operations
void
afr_post_gfid_sh_success (call_frame_t *sh_frame, xlator_t *this)
{
        afr_local_t             *local = NULL;
        afr_local_t             *sh_local = NULL;
        afr_private_t           *priv = NULL;
        afr_self_heal_t         *sh = NULL;
        int                     i = 0;
        struct iatt             *lookup_bufs = NULL;
        struct iatt             *lookup_parentbufs = NULL;

        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;
        local = sh->orig_frame->local;
        lookup_bufs = local->cont.lookup.bufs;
        lookup_parentbufs = local->cont.lookup.postparents;
        priv = this->private;

        memcpy (lookup_bufs, sh->buf, priv->child_count * sizeof (*sh->buf));
        memcpy (lookup_parentbufs, sh->parentbufs,
                priv->child_count * sizeof (*sh->parentbufs));

        afr_reset_xattr (local->cont.lookup.xattrs, priv->child_count);
        if (local->cont.lookup.xattr) {
                dict_unref (local->cont.lookup.xattr);
                local->cont.lookup.xattr = NULL;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (sh->xattr[i])
                        local->cont.lookup.xattrs[i] = dict_ref (sh->xattr[i]);
        }

        afr_reset_children (local->cont.lookup.success_children,
                            priv->child_count);
        afr_children_copy (local->cont.lookup.success_children,
                           sh->fresh_children, priv->child_count);
}

static void
afr_lookup_perform_self_heal (call_frame_t *frame, xlator_t *this,
                              gf_boolean_t *sh_launched)
{
        unsigned int         up_count = 0;
        afr_private_t       *priv    = NULL;
        afr_local_t         *local   = NULL;
        char                *reason  = NULL;

        GF_ASSERT (sh_launched);
        *sh_launched = _gf_false;
        priv         = this->private;
        local        = frame->local;

        up_count  = afr_up_children_count (local->child_up, priv->child_count);
        if (up_count == 1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Only 1 child up - do not attempt to detect self heal");
                goto out;
        }

        afr_lookup_set_self_heal_params (local, this);
        if (afr_can_self_heal_proceed (&local->self_heal, priv)) {
                if  (afr_is_transaction_running (local) &&
                     /*Forcefully call afr_launch_self_heal (which will go on to
                       fail) for SB files.This prevents stale data being served
                       due to race in  afr_is_transaction_running() when
                       multiple clients access the same SB file*/
                     !local->cont.lookup.possible_spb &&
                     (!local->attempt_self_heal))
                        goto out;

                reason = "lookup detected pending operations";
                afr_launch_self_heal (frame, this, local->cont.lookup.inode,
                                      !local->foreground_self_heal,
                                      local->cont.lookup.buf.ia_type,
                                      reason, afr_post_gfid_sh_success,
                                      afr_self_heal_lookup_unwind);
                *sh_launched = _gf_true;
        }
out:
        return;
}

void
afr_get_fresh_children (int32_t *success_children, int32_t *sources,
                        int32_t *fresh_children, unsigned int child_count)
{
        unsigned int i = 0;
        unsigned int j = 0;

        GF_ASSERT (success_children);
        GF_ASSERT (sources);
        GF_ASSERT (fresh_children);

        afr_reset_children (fresh_children, child_count);
        for (i = 0; i < child_count; i++) {
                if (success_children[i] == -1)
                        break;
                if (afr_is_read_child (success_children, sources, child_count,
                                       success_children[i])) {
                        fresh_children[j] = success_children[i];
                        j++;
                }
        }
}

static int
afr_lookup_set_read_ctx (afr_local_t *local, xlator_t *this, int32_t read_child)
{
        afr_private_t           *priv = NULL;

        GF_ASSERT (read_child >= 0);

        priv = this->private;
        afr_get_fresh_children (local->cont.lookup.success_children,
                                local->cont.lookup.sources,
                                local->fresh_children, priv->child_count);
        afr_inode_set_read_ctx (this, local->cont.lookup.inode, read_child,
                                local->fresh_children);

        return 0;
}

int
afr_lookup_done_success_action (call_frame_t *frame, xlator_t *this,
                                gf_boolean_t fail_conflict)
{
        int32_t             read_child = -1;
        int32_t             ret        = -1;
        afr_local_t         *local     = NULL;
        gf_boolean_t        fresh_lookup = _gf_false;

        local   = frame->local;
        fresh_lookup = local->cont.lookup.fresh_lookup;

        if (local->loc.parent == NULL)
                fail_conflict = _gf_true;

        if (afr_lookup_conflicting_entries (local, this)) {
                if (fail_conflict == _gf_false)
                        ret = 0;
                goto out;
        }

        ret = afr_lookup_select_read_child (local, this, &read_child);
        if (!afr_is_transaction_running (local) || fresh_lookup) {
                if (read_child < 0)
                        goto out;

                ret = afr_lookup_set_read_ctx (local, this, read_child);
                if (ret)
                        goto out;
        }

        ret = afr_lookup_build_response_params (local, this);
        if (ret)
                goto out;
        afr_update_loc_gfids (&local->loc,
                              &local->cont.lookup.buf,
                              &local->cont.lookup.postparent);

        ret = 0;
out:
        if (ret) {
                local->op_ret = -1;
                local->op_errno = EIO;
        }
        return ret;
}

int
afr_lookup_get_latest_subvol (afr_local_t *local, xlator_t *this)
{
        afr_private_t *priv = NULL;
        int32_t       *success_children = NULL;
        struct iatt   *bufs = NULL;
        int           i = 0;
        int           child = 0;
        int           lsubvol = -1;

        priv = this->private;
        success_children = local->cont.lookup.success_children;
        bufs = local->cont.lookup.bufs;
        for (i = 0; i < priv->child_count; i++) {
                child = success_children[i];
                if (child == -1)
                        break;
                if (uuid_is_null (bufs[child].ia_gfid))
                        continue;
                if (lsubvol < 0) {
                        lsubvol = child;
                } else if (bufs[lsubvol].ia_ctime < bufs[child].ia_ctime) {
                        lsubvol = child;
                } else if ((bufs[lsubvol].ia_ctime == bufs[child].ia_ctime) &&
                  (bufs[lsubvol].ia_ctime_nsec < bufs[child].ia_ctime_nsec)) {
                        lsubvol = child;
                }
        }
        return lsubvol;
}

void
afr_lookup_mark_other_entries_stale (afr_local_t *local, xlator_t *this,
                                     int subvol)
{
        afr_private_t *priv = NULL;
        int32_t       *success_children = NULL;
        struct iatt   *bufs = NULL;
        int           i = 0;
        int           child = 0;

        priv = this->private;
        success_children = local->cont.lookup.success_children;
        bufs = local->cont.lookup.bufs;
        memcpy (local->fresh_children, success_children,
                sizeof (*success_children) * priv->child_count);
        for (i = 0; i < priv->child_count; i++) {
                child = local->fresh_children[i];
                if (child == -1)
                        break;
                if (child == subvol)
                        continue;
                if (uuid_is_null (bufs[child].ia_gfid) &&
                    (bufs[child].ia_type == bufs[subvol].ia_type))
                        continue;
                afr_children_rm_child (success_children, child,
                                       priv->child_count);
                local->success_count--;
        }
        afr_reset_children (local->fresh_children, priv->child_count);
}

void
afr_succeed_lookup_on_latest_iatt (afr_local_t *local, xlator_t *this)
{
        int    lsubvol = 0;

        if (!afr_lookup_conflicting_entries (local, this))
                goto out;

        lsubvol = afr_lookup_get_latest_subvol (local, this);
        if (lsubvol < 0)
                goto out;
        afr_lookup_mark_other_entries_stale (local, this, lsubvol);
out:
        return;
}

gf_boolean_t
afr_is_entry_possibly_under_creation (afr_local_t *local, xlator_t *this)
{
        /*
         * We need to perform this test in lookup done and treat on going
         * create/DELETE as ENOENT.
         * Reason:
        Multiple clients A, B and C are attempting 'mkdir -p /mnt/a/b/c'

        1 Client A is in the middle of mkdir(/a). It has acquired lock.
          It has performed mkdir(/a) on one subvol, and second one is still
          in progress
        2 Client B performs a lookup, sees directory /a on one,
          ENOENT on the other, succeeds lookup.
        3 Client B performs lookup on /a/b on both subvols, both return ENOENT
          (one subvol because /a/b does not exist, another because /a
          itself does not exist)
        4 Client B proceeds to mkdir /a/b. It obtains entrylk on inode=/a with
          basename=b on one subvol, but fails on other subvol as /a is yet to
          be created by Client A.
        5 Client A finishes mkdir of /a on other subvol
        6 Client C also attempts to create /a/b, lookup returns ENOENT on
          both subvols.
        7 Client C tries to obtain entrylk on on inode=/a with basename=b,
          obtains on one subvol (where B had failed), and waits for B to unlock
          on other subvol.
        8 Client B finishes mkdir() on one subvol with GFID-1 and completes
          transaction and unlocks
        9 Client C gets the lock on the second subvol, At this stage second
          subvol already has /a/b created from Client B, but Client C does not
          check that in the middle of mkdir transaction
        10 Client C attempts mkdir /a/b on both subvols. It succeeds on
           ONLY ONE (where Client B could not get lock because of
           missing parent /a dir) with GFID-2, and gets EEXIST from ONE subvol.
        This way we have /a/b in GFID mismatch. One subvol got GFID-1 because
        Client B performed transaction on only one subvol (because entrylk()
        could not be obtained on second subvol because of missing parent dir --
        caused by premature/speculative succeeding of lookup() on /a when locks
        are detected). Other subvol gets GFID-2 from Client C because while
        it was waiting for entrylk() on both subvols, Client B was in the
        middle of creating mkdir() on only one subvol, and Client C does not
        "expect" this when it is between lock() and pre-op()/op() phase of the
        transaction.
         */
	if (local->cont.lookup.parent_entrylk && local->enoent_count)
		return _gf_true;

	return _gf_false;
}


static void
afr_lookup_done (call_frame_t *frame, xlator_t *this)
{
        int                 unwind = 1;
        afr_private_t       *priv  = NULL;
        afr_local_t         *local = NULL;
        int                 ret = -1;
        gf_boolean_t        sh_launched = _gf_false;
        gf_boolean_t        fail_conflict = _gf_false;
        int                 gfid_miss_count = 0;
        int                 enotconn_count = 0;
        int                 up_children_count = 0;

        priv  = this->private;
        local = frame->local;

	if (afr_is_entry_possibly_under_creation (local, this)) {
		local->op_ret = -1;
		local->op_errno = ENOENT;
		goto unwind;
	}

        if (local->op_ret < 0)
                goto unwind;

        if (local->cont.lookup.parent_entrylk && local->success_count > 1)
                afr_succeed_lookup_on_latest_iatt (local, this);

        gfid_miss_count = afr_lookup_gfid_missing_count (local, this);
        up_children_count = afr_up_children_count (local->child_up,
                                                   priv->child_count);
        enotconn_count = priv->child_count - up_children_count;
        if ((gfid_miss_count == local->success_count) &&
            (enotconn_count > 0)) {
                local->op_ret = -1;
                local->op_errno = EIO;
                gf_log (this->name, GF_LOG_ERROR, "Failing lookup for %s, "
                        "LOOKUP on a file without gfid is not allowed when "
                        "some of the children are down", local->loc.path);
                goto unwind;
        }

        if ((gfid_miss_count == local->success_count) &&
            uuid_is_null (local->cont.lookup.gfid_req)) {
                local->op_ret = -1;
                local->op_errno = ENODATA;
                gf_log (this->name, GF_LOG_ERROR, "%s: No gfid present",
                        local->loc.path);
                goto unwind;
        }

        if (gfid_miss_count && uuid_is_null (local->cont.lookup.gfid_req))
                fail_conflict = _gf_true;
        ret = afr_lookup_done_success_action (frame, this, fail_conflict);
        if (ret)
                goto unwind;
        uuid_copy (local->self_heal.sh_gfid_req, local->cont.lookup.gfid_req);

        afr_lookup_perform_self_heal (frame, this, &sh_launched);
        if (sh_launched) {
                unwind = 0;
                goto unwind;
        }

 unwind:
         if (unwind) {
                 AFR_STACK_UNWIND (lookup, frame, local->op_ret,
                                   local->op_errno, local->cont.lookup.inode,
                                   &local->cont.lookup.buf,
                                   local->cont.lookup.xattr,
                                   &local->cont.lookup.postparent);
        }
}

/*
 * During a lookup, some errors are more "important" than
 * others in that they must be given higher priority while
 * returning to the user.
 *
 * The hierarchy is ESTALE > EIO > ENOENT > others
 */
int32_t
afr_most_important_error(int32_t old_errno, int32_t new_errno,
			 gf_boolean_t eio)
{
	if (old_errno == ESTALE || new_errno == ESTALE)
		return ESTALE;
	if (eio && (old_errno == EIO || new_errno == EIO))
		return EIO;
	if (old_errno == ENOENT || new_errno == ENOENT)
		return ENOENT;

	return new_errno;
}

int32_t
afr_resultant_errno_get (int32_t *children,
                         int *child_errno, unsigned int child_count)
{
        int     i = 0;
        int32_t op_errno = 0;
        int     child = 0;

        for (i = 0; i < child_count; i++) {
                if (children) {
                        child = children[i];
                        if (child == -1)
                                break;
                } else {
                        child = i;
                }
		op_errno = afr_most_important_error(op_errno,
						    child_errno[child],
						    _gf_false);
        }
        return op_errno;
}

static void
afr_lookup_handle_error (afr_local_t *local, int32_t op_ret,  int32_t op_errno)
{
        GF_ASSERT (local);
        if (op_errno == ENOENT)
                local->enoent_count++;

	local->op_errno = afr_most_important_error(local->op_errno, op_errno,
						   _gf_false);

        if (local->op_errno == ESTALE) {
                local->op_ret = -1;
        }
}

static void
afr_set_root_inode_on_first_lookup (afr_local_t *local, xlator_t *this,
                                    inode_t *inode)
{
        afr_private_t           *priv = NULL;
        GF_ASSERT (inode);

        if (!__is_root_gfid (inode->gfid))
                goto out;
        if (!afr_is_fresh_lookup (&local->loc, this))
                goto out;
        priv = this->private;
        if ((priv->first_lookup)) {
                gf_log (this->name, GF_LOG_INFO, "added root inode");
                priv->root_inode = inode_ref (inode);
                priv->first_lookup = 0;
        }
out:
        return;
}

static void
afr_lookup_cache_args (afr_local_t *local, int child_index, dict_t *xattr,
                       struct iatt *buf, struct iatt *postparent)
{
        GF_ASSERT (child_index >= 0);
        local->cont.lookup.xattrs[child_index] = dict_ref (xattr);
        local->cont.lookup.postparents[child_index] = *postparent;
        local->cont.lookup.bufs[child_index] = *buf;
}

static void
afr_lookup_handle_first_success (afr_local_t *local, xlator_t *this,
                                 inode_t *inode, struct iatt *buf)
{
        local->cont.lookup.inode      = inode_ref (inode);
        local->cont.lookup.buf        = *buf;
        afr_set_root_inode_on_first_lookup (local, this, inode);
}

static int32_t
afr_discovery_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict,
                   dict_t *xdata)
{
        int              ret            = 0;
        char            *pathinfo       = NULL;
        gf_boolean_t     is_local        = _gf_false;
        afr_private_t   *priv           = NULL;
        int32_t          child_index    = -1;

        if (op_ret != 0) {
                goto out;
        }

        ret = dict_get_str (dict, GF_XATTR_PATHINFO_KEY, &pathinfo);
        if (ret != 0) {
                goto out;
        }

        ret = afr_local_pathinfo (pathinfo, &is_local);
        if (ret) {
                goto out;
        }

        priv = this->private;
        /*
         * Note that one local subvolume will override another here.  The only
         * way to avoid that would be to retain extra information about whether
         * the previous read_child is local, and it's just not worth it.  Even
         * the slowest local subvolume is far preferable to a remote one.
         */
        if (is_local) {
                child_index = (int32_t)(long)cookie;
                gf_log (this->name, GF_LOG_INFO,
                        "selecting local read_child %s",
                        priv->children[child_index]->name);
                priv->read_child = child_index;
        }

out:
        STACK_DESTROY(frame->root);
        return 0;
}

static void
afr_attempt_local_discovery (xlator_t *this, int32_t child_index)
{
        call_frame_t    *newframe = NULL;
        loc_t            tmploc = {0,};
        afr_private_t   *priv = this->private;

        newframe = create_frame(this,this->ctx->pool);
        if (!newframe) {
                return;
        }

        tmploc.gfid[sizeof(tmploc.gfid)-1] = 1;
        STACK_WIND_COOKIE (newframe, afr_discovery_cbk,
                           (void *)(long)child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->getxattr,
                           &tmploc, GF_XATTR_PATHINFO_KEY, NULL);
}

static void
afr_lookup_handle_success (afr_local_t *local, xlator_t *this, int32_t child_index,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *buf, dict_t *xattr,
                           struct iatt *postparent)
{
        afr_private_t   *priv   = this->private;

        if (local->success_count == 0) {
                if (local->op_errno != ESTALE) {
                        local->op_ret = op_ret;
                        local->op_errno = 0;
                }
                afr_lookup_handle_first_success (local, this, inode, buf);
        }
        afr_lookup_update_lk_counts (local, this,
                                     child_index, xattr);

        afr_lookup_cache_args (local, child_index, xattr,
                               buf, postparent);

        if (local->do_discovery && (priv->read_child == (-1))) {
                afr_attempt_local_discovery(this,child_index);
        }

        local->cont.lookup.success_children[local->success_count] = child_index;
        local->success_count++;
}

int
afr_lookup_cbk (call_frame_t *frame, void *cookie,
                xlator_t *this,  int32_t op_ret,  int32_t op_errno,
                inode_t *inode,   struct iatt *buf, dict_t *xattr,
                struct iatt *postparent)
{
        afr_local_t *   local = NULL;
        int             call_count      = -1;
        int             child_index     = -1;

         child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                local = frame->local;

                if (op_ret == -1) {
                        afr_lookup_handle_error (local, op_ret, op_errno);
                        goto unlock;
                }
                afr_lookup_handle_success (local, this, child_index, op_ret,
                                           op_errno, inode, buf, xattr,
                                           postparent);

         }
unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
               afr_lookup_done (frame, this);
        }

         return 0;
}

int
afr_lookup_cont_init (afr_local_t *local, unsigned int child_count)
{
        int               ret            = -ENOMEM;
        struct iatt       *iatts         = NULL;
        int32_t           *success_children = NULL;
        int32_t           *sources       = NULL;
        int32_t           **pending_matrix = NULL;

        GF_ASSERT (local);
        local->cont.lookup.xattrs = GF_CALLOC (child_count,
                                               sizeof (*local->cont.lookup.xattr),
                                               gf_afr_mt_dict_t);
        if (NULL == local->cont.lookup.xattrs)
                goto out;

        iatts = GF_CALLOC (child_count, sizeof (*iatts), gf_afr_mt_iatt);
        if (NULL == iatts)
                goto out;
        local->cont.lookup.postparents = iatts;

        iatts = GF_CALLOC (child_count, sizeof (*iatts), gf_afr_mt_iatt);
        if (NULL == iatts)
                goto out;
        local->cont.lookup.bufs = iatts;

        success_children = afr_children_create (child_count);
        if (NULL == success_children)
                goto out;
        local->cont.lookup.success_children = success_children;

        local->fresh_children = afr_children_create (child_count);
        if (NULL == local->fresh_children)
                goto out;

        sources = GF_CALLOC (sizeof (*sources), child_count, gf_afr_mt_int32_t);
        if (NULL == sources)
                goto out;
        local->cont.lookup.sources = sources;

        pending_matrix = afr_matrix_create (child_count, child_count);
        if (NULL == pending_matrix)
                goto out;
        local->cont.lookup.pending_matrix = pending_matrix;

        ret = 0;
out:
        return ret;
}

int
afr_lookup (call_frame_t *frame, xlator_t *this,
            loc_t *loc, dict_t *xattr_req)
{
        afr_private_t  *priv      = NULL;
        afr_local_t    *local     = NULL;
        void           *gfid_req  = NULL;
        int            ret        = -1;
        int            i          = 0;
        int            call_count = 0;
        uint64_t       ctx        = 0;
        int32_t        op_errno   = 0;
                       priv       = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (local, out);

        local->op_ret = -1;

        frame->local = local;
        local->fop = GF_FOP_LOOKUP;

        loc_copy (&local->loc, loc);
        ret = loc_path (&local->loc, NULL);
        if (ret < 0) {
                op_errno = EINVAL;
                goto out;
        }

        if (local->loc.path &&
            (strcmp (local->loc.path, "/" GF_REPLICATE_TRASH_DIR) == 0)) {
                op_errno = EPERM;
                ret = -1;
                goto out;
        }

        ret = inode_ctx_get (local->loc.inode, this, &ctx);
        if (ret == 0) {
                /* lookup is a revalidate */

                local->read_child_index = afr_inode_get_read_ctx (this,
                                                               local->loc.inode,
                                                               NULL);
        } else {
                LOCK (&priv->read_child_lock);
                {
                        if (priv->hash_mode) {
                                local->read_child_index = -1;
                        }
                        else {
                                local->read_child_index =
                                        (++priv->read_child_rr) %
                                        (priv->child_count);
                        }
                }
                UNLOCK (&priv->read_child_lock);
                local->cont.lookup.fresh_lookup = _gf_true;
        }

        local->child_up = memdup (priv->child_up,
                                  sizeof (*local->child_up) * priv->child_count);
        if (NULL == local->child_up) {
                op_errno = ENOMEM;
                goto out;
        }

        ret = afr_lookup_cont_init (local, priv->child_count);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        local->call_count = afr_up_children_count (local->child_up,
                                                   priv->child_count);
        call_count = local->call_count;
        if (local->call_count == 0) {
                ret      = -1;
                op_errno = ENOTCONN;
                goto out;
        }

        /* By default assume ENOTCONN. On success it will be set to 0. */
        local->op_errno = ENOTCONN;

        ret = dict_get_int32 (xattr_req, "attempt-self-heal",
                              &local->attempt_self_heal);
        dict_del (xattr_req, "attempt-self-heal");

        ret = dict_get_int32 (xattr_req, "foreground-self-heal",
                              &local->foreground_self_heal);
        dict_del (xattr_req, "foreground-self-heal");

        ret = afr_lookup_xattr_req_prepare (local, this, xattr_req, &local->loc,
                                            &gfid_req);
        if (ret) {
                local->op_errno = -ret;
                goto out;
        }
        afr_lookup_save_gfid (local->cont.lookup.gfid_req, gfid_req,
                              &local->loc);
        local->fop = GF_FOP_LOOKUP;
        if (priv->choose_local && !priv->did_discovery) {
                if (gfid_req && __is_root_gfid(gfid_req)) {
                        local->do_discovery = _gf_true;
                        priv->did_discovery = _gf_true;
                }
        }
        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_lookup_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->lookup,
                                           &local->loc, local->xattr_req);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret)
                AFR_STACK_UNWIND (lookup, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL);

        return 0;
}


/* {{{ open */

int
__afr_fd_ctx_set (xlator_t *this, fd_t *fd)
{
        afr_private_t * priv   = NULL;
        int             ret    = -1;
        uint64_t        ctx    = 0;
        afr_fd_ctx_t *  fd_ctx = NULL;

        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        ret = __fd_ctx_get (fd, this, &ctx);

        if (ret == 0)
                goto out;

        fd_ctx = GF_CALLOC (1, sizeof (afr_fd_ctx_t),
                            gf_afr_mt_afr_fd_ctx_t);
        if (!fd_ctx) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->pre_op_done = GF_CALLOC (sizeof (*fd_ctx->pre_op_done),
                                         priv->child_count,
                                         gf_afr_mt_char);
        if (!fd_ctx->pre_op_done) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->pre_op_piggyback = GF_CALLOC (sizeof (*fd_ctx->pre_op_piggyback),
                                              priv->child_count,
                                              gf_afr_mt_char);
        if (!fd_ctx->pre_op_piggyback) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->opened_on = GF_CALLOC (sizeof (*fd_ctx->opened_on),
                                       priv->child_count,
                                       gf_afr_mt_int32_t);
        if (!fd_ctx->opened_on) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->lock_piggyback = GF_CALLOC (sizeof (*fd_ctx->lock_piggyback),
                                            priv->child_count,
                                            gf_afr_mt_char);
        if (!fd_ctx->lock_piggyback) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->lock_acquired = GF_CALLOC (sizeof (*fd_ctx->lock_acquired),
                                           priv->child_count,
                                           gf_afr_mt_char);
        if (!fd_ctx->lock_acquired) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->up_count   = priv->up_count;
        fd_ctx->down_count = priv->down_count;

        fd_ctx->locked_on = GF_CALLOC (sizeof (*fd_ctx->locked_on),
                                       priv->child_count,
                                       gf_afr_mt_char);
        if (!fd_ctx->locked_on) {
                ret = -ENOMEM;
                goto out;
        }

	pthread_mutex_init (&fd_ctx->delay_lock, NULL);
        INIT_LIST_HEAD (&fd_ctx->entries);
        fd_ctx->call_child = -1;

        INIT_LIST_HEAD (&fd_ctx->eager_locked);

        ret = __fd_ctx_set (fd, this, (uint64_t)(long) fd_ctx);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set fd ctx (%p)", fd);
out:
        return ret;
}


int
afr_fd_ctx_set (xlator_t *this, fd_t *fd)
{
        int ret = -1;

        LOCK (&fd->lock);
        {
                ret = __afr_fd_ctx_set (this, fd);
        }
        UNLOCK (&fd->lock);

        return ret;
}

/* {{{ flush */

int
afr_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *   local = NULL;
        int call_count  = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        if (local->success_count == 0) {
                                local->op_ret = op_ret;
                        }
                        local->success_count++;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND(flush, frame, local->op_ret,
				 local->op_errno, NULL);

        return 0;
}

static int
afr_flush_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int           i      = 0;
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;
        int call_count       = -1;

        priv = this->private;
        local = frame->local;
        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_flush_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->flush,
                                           local->fd, NULL);
                        if (!--call_count)
                                break;

                }
        }

        return 0;
}

int
afr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        afr_private_t *priv  = NULL;
        afr_local_t   *local = NULL;
        call_stub_t   *stub = NULL;
        int            ret        = -1;
        int            op_errno   = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

	AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
	local = frame->local;

	ret = afr_local_init(local, priv, &op_errno);
	if (ret < 0)
		goto out;

	local->fd = fd_ref(fd);
        stub = fop_flush_stub (frame, afr_flush_wrapper, fd, xdata);
        if (!stub) {
                ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        afr_delayed_changelog_wake_resume (this, fd, stub);
	ret = 0;

out:
	if (ret < 0)
		AFR_STACK_UNWIND(flush, frame, -1, op_errno, NULL);

        return 0;
}

/* }}} */


int
afr_cleanup_fd_ctx (xlator_t *this, fd_t *fd)
{
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx = NULL;
        int             ret = 0;

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx) {
                GF_FREE (fd_ctx->pre_op_done);

                GF_FREE (fd_ctx->opened_on);

                GF_FREE (fd_ctx->locked_on);

                GF_FREE (fd_ctx->pre_op_piggyback);
                GF_FREE (fd_ctx->lock_piggyback);

                GF_FREE (fd_ctx->lock_acquired);

		pthread_mutex_destroy (&fd_ctx->delay_lock);

                GF_FREE (fd_ctx);
        }

out:
        return 0;
}


int
afr_release (xlator_t *this, fd_t *fd)
{
        afr_locked_fd_t *locked_fd = NULL;
        afr_locked_fd_t *tmp       = NULL;
        afr_private_t   *priv      = NULL;

        priv = this->private;

        afr_cleanup_fd_ctx (this, fd);

        list_for_each_entry_safe (locked_fd, tmp, &priv->saved_fds,
                                  list) {

                if (locked_fd->fd == fd) {
                        list_del_init (&locked_fd->list);
                        GF_FREE (locked_fd);
                }

        }

        return 0;
}


/* {{{ fsync */

int
afr_fsync_unwind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        AFR_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);
        return 0;
}

int
afr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;
        int child_index = (long) cookie;
        int read_child  = 0;
	call_stub_t *stub = NULL;

        local = frame->local;

        read_child = afr_inode_get_read_ctx (this, local->fd->inode, NULL);

        LOCK (&frame->lock);
        {
                if (child_index == read_child) {
                        local->read_child_returned = _gf_true;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (local->success_count == 0) {
                                local->cont.inode_wfop.prebuf  = *prebuf;
                                local->cont.inode_wfop.postbuf = *postbuf;
                        }

                        if (child_index == read_child) {
                                local->cont.inode_wfop.prebuf  = *prebuf;
                                local->cont.inode_wfop.postbuf = *postbuf;
                        }

                        local->success_count++;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		/* Make a stub out of the frame, and register it
		   with the waking up post-op. When the call-stub resumes,
		   we are guaranteed that there was no post-op pending
		   (i.e changelogs were unset in the server). This is an
		   essential "guarantee", that fsync() returns only after
		   completely finishing EVERYTHING, including the delayed
		   post-op. This guarantee is expected by FUSE graph switching
		   for example.
		*/
		stub = fop_fsync_cbk_stub (frame, afr_fsync_unwind_cbk,
                                           local->op_ret, local->op_errno,
                                           &local->cont.inode_wfop.prebuf,
                                           &local->cont.inode_wfop.postbuf,
                                           xdata);
		if (!stub) {
			AFR_STACK_UNWIND (fsync, frame, -1, ENOMEM, 0, 0, 0);
			return 0;
		}

		/* If no new unstable writes happened between the
		   time we cleared the unstable write witness flag in afr_fsync
		   and now, calling afr_delayed_changelog_wake_up() should
		   wake up and skip over the fsync phase and go straight to
		   afr_changelog_post_op_now()
		*/
		afr_delayed_changelog_wake_resume (this, local->fd, stub);
        }

        return 0;
}


int
afr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
           int32_t datasync, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        local->fd             = fd_ref (fd);

	if (afr_fd_has_witnessed_unstable_write (this, fd)) {
		/* don't care. we only wanted to CLEAR the bit */
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_fsync_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fsync,
                                           fd, datasync, xdata);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

/* }}} */

/* {{{ fsync */

int32_t
afr_fsyncdir_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fsyncdir, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int32_t
afr_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
              int32_t datasync, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fsyncdir_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fsyncdir,
                                    fd, datasync, xdata);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (fsyncdir, frame, -1, op_errno, NULL);
        return 0;
}

/* }}} */

/* {{{ xattrop */

int32_t
afr_xattrop_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        if (!local->cont.xattrop.xattr)
                                local->cont.xattrop.xattr = dict_ref (xattr);
                        local->op_ret = 0;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (xattrop, frame, local->op_ret, local->op_errno,
                local->cont.xattrop.xattr, xdata);

        return 0;
}


int32_t
afr_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_xattrop_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->xattrop,
                                    loc, optype, xattr, xdata);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL, NULL);
        return 0;
}

/* }}} */

/* {{{ fxattrop */

int32_t
afr_fxattrop_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;

        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        if (!local->cont.fxattrop.xattr)
                                local->cont.fxattrop.xattr = dict_ref (xattr);

                        local->op_ret = 0;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fxattrop, frame, local->op_ret, local->op_errno,
                                  local->cont.fxattrop.xattr, xdata);

        return 0;
}


int32_t
afr_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fxattrop_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fxattrop,
                                    fd, optype, xattr, xdata);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (fxattrop, frame, -1, op_errno, NULL, NULL);
        return 0;
}

/* }}} */


int32_t
afr_inodelk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (inodelk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int32_t
afr_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd,
             struct gf_flock *flock, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_inodelk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->inodelk,
                                    volume, loc, cmd, flock, xdata);

                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (inodelk, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
afr_finodelk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  dict_t *xdata)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (finodelk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int32_t
afr_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *flock,
              dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_finodelk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->finodelk,
                                    volume, fd, cmd, flock, xdata);

                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (finodelk, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
afr_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (entrylk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int32_t
afr_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc,
             const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_entrylk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->entrylk,
                                    volume, loc, basename, cmd, type, xdata);

                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (entrylk, frame, -1, op_errno, NULL);
        return 0;
}



int32_t
afr_fentrylk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fentrylk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int32_t
afr_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd,
              const char *basename, entrylk_cmd cmd,
              entrylk_type type, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int ret = -1;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fentrylk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fentrylk,
                                    volume, fd, basename, cmd, type, xdata);

                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (fentrylk, frame, -1, op_errno, NULL);
        return 0;
}

int32_t
afr_statfs_cbk (call_frame_t *frame, void *cookie,
                xlator_t *this, int32_t op_ret, int32_t op_errno,
                struct statvfs *statvfs, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = 0;

        LOCK (&frame->lock);
        {
                local = frame->local;

                if (op_ret == 0) {
                        local->op_ret   = op_ret;

                        if (local->cont.statfs.buf_set) {
                                if (statvfs->f_bavail < local->cont.statfs.buf.f_bavail)
                                        local->cont.statfs.buf = *statvfs;
                        } else {
                                local->cont.statfs.buf = *statvfs;
                                local->cont.statfs.buf_set = 1;
                        }
                }

                if (op_ret == -1)
                        local->op_errno = op_errno;

        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (statfs, frame, local->op_ret, local->op_errno,
                                  &local->cont.statfs.buf, xdata);

        return 0;
}


int32_t
afr_statfs (call_frame_t *frame, xlator_t *this,
            loc_t *loc, dict_t *xdata)
{
        afr_private_t *  priv        = NULL;
        int              child_count = 0;
        afr_local_t   *  local       = NULL;
        int              i           = 0;
        int              ret = -1;
        int              call_count = 0;
        int32_t          op_errno    = 0;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        child_count = priv->child_count;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        call_count = local->call_count;

        for (i = 0; i < child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_statfs_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->statfs,
                                    loc, xdata);
                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
afr_lk_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                   dict_t *xdata)
{
        afr_local_t * local = NULL;
        int call_count = -1;

        local = frame->local;
        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  lock, xdata);

        return 0;
}


int32_t
afr_lk_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   * local = NULL;
        afr_private_t * priv  = NULL;
        int i = 0;
        int call_count = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_locked_nodes_count (local->cont.lk.locked_nodes,
                                             priv->child_count);

        if (call_count == 0) {
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  &local->cont.lk.ret_flock, NULL);
                return 0;
        }

        local->call_count = call_count;

        local->cont.lk.user_flock.l_type = F_UNLCK;

        for (i = 0; i < priv->child_count; i++) {
                if (local->cont.lk.locked_nodes[i]) {
                        STACK_WIND (frame, afr_lk_unlock_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->lk,
                                    local->fd, F_SETLK,
                                    &local->cont.lk.user_flock, NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int32_t
afr_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int child_index = -1;
/*        int            ret  = 0; */


        local = frame->local;
        priv  = this->private;

        child_index = (long) cookie;

        if (!child_went_down (op_ret, op_errno) && (op_ret == -1)) {
                local->op_ret   = -1;
                local->op_errno = op_errno;

                afr_lk_unlock (frame, this);
                return 0;
        }

        if (op_ret == 0) {
                local->op_ret        = 0;
                local->op_errno      = 0;
                local->cont.lk.locked_nodes[child_index] = 1;
                local->cont.lk.ret_flock = *lock;
        }

        child_index++;

        if (child_index < priv->child_count) {
                STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) child_index,
                                   priv->children[child_index],
                                   priv->children[child_index]->fops->lk,
                                   local->fd, local->cont.lk.cmd,
                                   &local->cont.lk.user_flock, xdata);
        } else if (local->op_ret == -1) {
                /* all nodes have gone down */

                AFR_STACK_UNWIND (lk, frame, -1, ENOTCONN,
                                  &local->cont.lk.ret_flock, NULL);
        } else {
                /* locking has succeeded on all nodes that are up */

                /* temporarily
                   ret = afr_mark_locked_nodes (this, local->fd,
                   local->cont.lk.locked_nodes);
                   if (ret)
                   gf_log (this->name, GF_LOG_DEBUG,
                   "Could not save locked nodes info in fdctx");

                   ret = afr_save_locked_fd (this, local->fd);
                   if (ret)
                   gf_log (this->name, GF_LOG_DEBUG,
                   "Could not save locked fd");

                */
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  &local->cont.lk.ret_flock, NULL);
        }

        return 0;
}


int
afr_lk (call_frame_t *frame, xlator_t *this,
        fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int i = 0;
        int32_t op_errno = 0;
        int     ret      = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->cont.lk.locked_nodes = GF_CALLOC (priv->child_count,
                                                 sizeof (*local->cont.lk.locked_nodes),
                                                 gf_afr_mt_char);

        if (!local->cont.lk.locked_nodes) {
                op_errno = ENOMEM;
                goto out;
        }

        local->fd            = fd_ref (fd);
        local->cont.lk.cmd   = cmd;
        local->cont.lk.user_flock = *flock;
        local->cont.lk.ret_flock = *flock;

        STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) 0,
                           priv->children[i],
                           priv->children[i]->fops->lk,
                           fd, cmd, flock, xdata);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int
afr_forget (xlator_t *this, inode_t *inode)
{
        uint64_t        ctx_addr = 0;
        afr_inode_ctx_t *ctx     = NULL;

        inode_ctx_get (inode, this, &ctx_addr);

        if (!ctx_addr)
                goto out;

        ctx = (afr_inode_ctx_t *)(long)ctx_addr;
        GF_FREE (ctx->fresh_children);
        GF_FREE (ctx);
out:
        return 0;
}

int
afr_priv_dump (xlator_t *this)
{
        afr_private_t *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];
        char  key[GF_DUMP_MAX_BUF_LEN];
        int   i = 0;


        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);
        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
        gf_proc_dump_add_section(key_prefix);
        gf_proc_dump_write("child_count", "%u", priv->child_count);
        gf_proc_dump_write("read_child_rr", "%u", priv->read_child_rr);
        for (i = 0; i < priv->child_count; i++) {
                sprintf (key, "child_up[%d]", i);
                gf_proc_dump_write(key, "%d", priv->child_up[i]);
                sprintf (key, "pending_key[%d]", i);
                gf_proc_dump_write(key, "%s", priv->pending_key[i]);
        }
        gf_proc_dump_write("data_self_heal", "%s", priv->data_self_heal);
        gf_proc_dump_write("metadata_self_heal", "%d", priv->metadata_self_heal);
        gf_proc_dump_write("entry_self_heal", "%d", priv->entry_self_heal);
        gf_proc_dump_write("data_change_log", "%d", priv->data_change_log);
        gf_proc_dump_write("metadata_change_log", "%d", priv->metadata_change_log);
        gf_proc_dump_write("entry-change_log", "%d", priv->entry_change_log);
        gf_proc_dump_write("read_child", "%d", priv->read_child);
        gf_proc_dump_write("favorite_child", "%d", priv->favorite_child);
        gf_proc_dump_write("wait_count", "%u", priv->wait_count);

        return 0;
}


/**
 * find_child_index - find the child's index in the array of subvolumes
 * @this: AFR
 * @child: child
 */

static int
find_child_index (xlator_t *this, xlator_t *child)
{
        afr_private_t *priv = NULL;
        int i = -1;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if ((xlator_t *) child == priv->children[i])
                        break;
        }

        return i;
}

int32_t
afr_notify (xlator_t *this, int32_t event,
            void *data, void *data2)
{
        afr_private_t   *priv               = NULL;
        int             i                   = -1;
        int             up_children         = 0;
        int             down_children       = 0;
        int             propagate           = 0;
        int             had_heard_from_all  = 0;
        int             have_heard_from_all = 0;
        int             idx                 = -1;
        int             ret                 = -1;
        int             call_psh            = 0;
        int             up_child            = AFR_ALL_CHILDREN;
        dict_t          *input              = NULL;
        dict_t          *output             = NULL;

        priv = this->private;

        if (!priv)
                return 0;

        /*
         * We need to reset this in case children come up in "staggered"
         * fashion, so that we discover a late-arriving local subvolume.  Note
         * that we could end up issuing N lookups to the first subvolume, and
         * O(N^2) overall, but N is small for AFR so it shouldn't be an issue.
         */
        priv->did_discovery = _gf_false;

        had_heard_from_all = 1;
        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i]) {
                        had_heard_from_all = 0;
                }
        }

        /* parent xlators dont need to know about every child_up, child_down
         * because of afr ha. If all subvolumes go down, child_down has
         * to be triggered. In that state when 1 subvolume comes up child_up
         * needs to be triggered. dht optimizes revalidate lookup by sending
         * it only to one of its subvolumes. When child up/down happens
         * for afr's subvolumes dht should be notified by child_modified. The
         * subsequent revalidate lookup happens on all the dht's subvolumes
         * which triggers afr self-heals if any.
         */
        idx = find_child_index (this, data);
        if (idx < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Received child_up "
                        "from invalid subvolume");
                goto out;
        }

        switch (event) {
        case GF_EVENT_CHILD_UP:
                LOCK (&priv->lock);
                {
                        /*
                         * This only really counts if the child was never up
                         * (value = -1) or had been down (value = 0).  See
                         * comment at GF_EVENT_CHILD_DOWN for a more detailed
                         * explanation.
                         */
                        if (priv->child_up[idx] != 1) {
                                priv->up_count++;
                        }
                        priv->child_up[idx] = 1;

                        call_psh = 1;
                        up_child = idx;
                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 1)
                                        up_children++;
                        if (up_children == 1) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "Subvolume '%s' came back up; "
                                        "going online.", ((xlator_t *)data)->name);
                        } else {
                                event = GF_EVENT_CHILD_MODIFIED;
                        }

                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_CHILD_DOWN:
                LOCK (&priv->lock);
                {
                        /*
                         * If a brick is down when we start, we'll get a
                         * CHILD_DOWN to indicate its initial state.  There
                         * was never a CHILD_UP in this case, so if we
                         * increment "down_count" the difference between than
                         * and "up_count" will no longer be the number of
                         * children that are currently up.  This has serious
                         * implications e.g. for quorum enforcement, so we
                         * don't increment these values unless the event
                         * represents an actual state transition between "up"
                         * (value = 1) and anything else.
                         */
                        if (priv->child_up[idx] == 1) {
                                priv->down_count++;
                        }
                        priv->child_up[idx] = 0;

                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 0)
                                        down_children++;
                        if (down_children == priv->child_count) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "All subvolumes are down. Going offline "
                                        "until atleast one of them comes back up.");
                        } else {
                                event = GF_EVENT_CHILD_MODIFIED;
                        }

                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_CHILD_CONNECTING:
                LOCK (&priv->lock);
                {
                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_TRANSLATOR_OP:
                input = data;
                output = data2;
                if (!had_heard_from_all) {
                        ret = -1;
                        goto out;
                }
                ret = afr_xl_op (this, input, output);
                goto out;
                break;

        default:
                propagate = 1;
                break;
        }

        /* have all subvolumes reported status once by now? */
        have_heard_from_all = 1;
        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i])
                        have_heard_from_all = 0;
        }

        /* if all subvols have reported status, no need to hide anything
           or wait for anything else. Just propagate blindly */
        if (have_heard_from_all)
                propagate = 1;

        if (!had_heard_from_all && have_heard_from_all) {
                /* This is the first event which completes aggregation
                   of events from all subvolumes. If at least one subvol
                   had come up, propagate CHILD_UP, but only this time
                */
                event = GF_EVENT_CHILD_DOWN;

                LOCK (&priv->lock);
                {
                        up_children = afr_up_children_count (priv->child_up,
                                                             priv->child_count);
                        for (i = 0; i < priv->child_count; i++) {
                                if (priv->last_event[i] == GF_EVENT_CHILD_UP) {
                                        event = GF_EVENT_CHILD_UP;
                                        break;
                                }

                                if (priv->last_event[i] ==
                                                GF_EVENT_CHILD_CONNECTING) {
                                        event = GF_EVENT_CHILD_CONNECTING;
                                        /* continue to check other events for CHILD_UP */
                                }
                        }
                }
                UNLOCK (&priv->lock);
        }

        ret = 0;
        if (propagate)
                ret = default_notify (this, event, data);
        if (call_psh && priv->shd.iamshd)
                afr_proactive_self_heal ((void*) (long) up_child);

out:
        return ret;
}

int
afr_first_up_child (unsigned char *child_up, size_t child_count)
{
        int         ret      = -1;
        int         i        = 0;

        GF_ASSERT (child_up);

        for (i = 0; i < child_count; i++) {
                if (child_up[i]) {
                        ret = i;
                        break;
                }
        }

        return ret;
}

int
afr_local_init (afr_local_t *local, afr_private_t *priv, int32_t *op_errno)
{
        int     ret = -1;

        local->op_ret = -1;
        local->op_errno = EUCLEAN;

        local->child_up = GF_CALLOC (priv->child_count,
                                     sizeof (*local->child_up),
                                     gf_afr_mt_char);
        if (!local->child_up) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }

        memcpy (local->child_up, priv->child_up,
                sizeof (*local->child_up) * priv->child_count);
        local->call_count = afr_up_children_count (local->child_up,
                                                   priv->child_count);
        if (local->call_count == 0) {
                gf_log (THIS->name, GF_LOG_INFO, "no subvolumes up");
                if (op_errno)
                        *op_errno = ENOTCONN;
                goto out;
        }

        local->child_errno = GF_CALLOC (priv->child_count,
                                        sizeof (*local->child_errno),
                                        gf_afr_mt_int32_t);
        if (!local->child_errno) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }

        local->transaction.postop_piggybacked = GF_CALLOC (priv->child_count,
							   sizeof (int),
							   gf_afr_mt_int32_t);
        if (!local->transaction.postop_piggybacked) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }

	local->append_write = _gf_false;

        ret = 0;
out:
        return ret;
}

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type)
{
        int             ret = -ENOMEM;

        lk->locked_nodes = GF_CALLOC (sizeof (*lk->locked_nodes),
                                      child_count, gf_afr_mt_char);
        if (NULL == lk->locked_nodes)
                goto out;

        lk->lower_locked_nodes = GF_CALLOC (sizeof (*lk->lower_locked_nodes),
                                            child_count, gf_afr_mt_char);
        if (NULL == lk->lower_locked_nodes)
                goto out;

        lk->lock_op_ret   = -1;
        lk->lock_op_errno = EUCLEAN;
        lk->transaction_lk_type = lk_type;

        ret = 0;
out:
        return ret;
}

void
afr_matrix_cleanup (int32_t **matrix, unsigned int m)
{
        int             i         = 0;

        if (!matrix)
                goto out;
        for (i = 0; i < m; i++) {
                GF_FREE (matrix[i]);
        }

        GF_FREE (matrix);
out:
        return;
}

int32_t**
afr_matrix_create (unsigned int m, unsigned int n)
{
        int32_t         **matrix = NULL;
        int             i       = 0;

        matrix = GF_CALLOC (sizeof (*matrix), m, gf_afr_mt_int32_t);
        if (!matrix)
                goto out;

        for (i = 0; i < m; i++) {
                matrix[i] = GF_CALLOC (sizeof (*matrix[i]), n,
                                       gf_afr_mt_int32_t);
                if (!matrix[i])
                        goto out;
        }
        return matrix;
out:
        afr_matrix_cleanup (matrix, m);
        return NULL;
}

int
afr_inodelk_init (afr_inodelk_t *lk, char *dom, size_t child_count)
{
        int             ret = -ENOMEM;

        lk->domain = dom;
        lk->locked_nodes = GF_CALLOC (sizeof (*lk->locked_nodes),
                                      child_count, gf_afr_mt_char);
        if (NULL == lk->locked_nodes)
                goto out;
        ret = 0;
out:
        return ret;
}

int
afr_transaction_local_init (afr_local_t *local, xlator_t *this)
{
        int            child_up_count = 0;
        int            ret = -ENOMEM;
        afr_private_t *priv = NULL;

        priv = this->private;
        ret = afr_internal_lock_init (&local->internal_lock, priv->child_count,
                                      AFR_TRANSACTION_LK);
        if (ret < 0)
                goto out;

        if ((local->transaction.type == AFR_DATA_TRANSACTION) ||
            (local->transaction.type == AFR_METADATA_TRANSACTION)) {
                ret = afr_inodelk_init (&local->internal_lock.inodelk[0],
                                        this->name, priv->child_count);
                if (ret < 0)
                        goto out;
        }

        ret = -ENOMEM;
        child_up_count = afr_up_children_count (local->child_up,
                                                priv->child_count);
        if (priv->optimistic_change_log && child_up_count == priv->child_count)
                local->optimistic_change_log = 1;

        local->first_up_child = afr_first_up_child (local->child_up,
                                                    priv->child_count);

        local->transaction.eager_lock =
                GF_CALLOC (sizeof (*local->transaction.eager_lock),
                           priv->child_count,
                           gf_afr_mt_int32_t);

        if (!local->transaction.eager_lock)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children)
                goto out;

        local->transaction.pre_op = GF_CALLOC (sizeof (*local->transaction.pre_op),
                                               priv->child_count,
                                               gf_afr_mt_char);
        if (!local->transaction.pre_op)
                goto out;

        local->pending = afr_matrix_create (priv->child_count,
                                            AFR_NUM_CHANGE_LOGS);
        if (!local->pending)
                goto out;

        local->transaction.txn_changelog = afr_matrix_create (priv->child_count,
                                                           AFR_NUM_CHANGE_LOGS);
        if (!local->transaction.txn_changelog)
                goto out;

	INIT_LIST_HEAD (&local->transaction.eager_locked);

        ret = 0;
out:
        return ret;
}

void
afr_reset_children (int32_t *fresh_children, int32_t child_count)
{
        unsigned int i = 0;
        for (i = 0; i < child_count; i++)
                fresh_children[i] = -1;
}

int32_t*
afr_children_create (int32_t child_count)
{
        int32_t           *children = NULL;
        int               i               = 0;

        GF_ASSERT (child_count > 0);

        children = GF_CALLOC (child_count, sizeof (*children),
                              gf_afr_mt_int32_t);
        if (NULL == children)
                goto out;
        for (i = 0; i < child_count; i++)
                children[i] = -1;
out:
        return children;
}

void
afr_children_add_child (int32_t *children, int32_t child,
                        int32_t child_count)
{
        gf_boolean_t child_found = _gf_false;
        int          i               = 0;

        for (i = 0; i < child_count; i++) {
                if (children[i] == -1)
                        break;
                if (children[i] == child) {
                        child_found = _gf_true;
                        break;
                }
        }

        if (!child_found) {
                GF_ASSERT (i < child_count);
                children[i] = child;
        }
}

void
afr_children_rm_child (int32_t *children, int32_t child, int32_t child_count)
{
        int          i = 0;

        GF_ASSERT ((child >= 0) && (child < child_count));
        for (i = 0; i < child_count; i++) {
                if (children[i] == -1)
                        break;
                if (children[i] == child) {
                        if (i != (child_count - 1))
                                memmove (children + i, children + i + 1,
                                         sizeof (*children)*(child_count - i - 1));
                        children[child_count - 1] = -1;
                        break;
                }
        }
}

int
afr_get_children_count (int32_t *children, unsigned int child_count)
{
        int count = 0;
        int i = 0;

        for (i = 0; i < child_count; i++) {
                if (children[i] == -1)
                        break;
                count++;
        }
        return count;
}

void
afr_set_low_priority (call_frame_t *frame)
{
        frame->root->pid = LOW_PRIO_PROC_PID;
}

int
afr_child_fd_ctx_set (xlator_t *this, fd_t *fd, int32_t child,
                      int flags)
{
        int             ret = 0;
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx      = NULL;

        GF_ASSERT (fd && fd->inode);
        ret = afr_fd_ctx_set (this, fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not set fd ctx for fd=%p", fd);
                goto out;
        }

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not get fd ctx for fd=%p", fd);
                goto out;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;
        fd_ctx->opened_on[child] = AFR_FD_OPENED;
        if (!IA_ISDIR (fd->inode->ia_type)) {
                fd_ctx->flags            = flags;
        }
        ret = 0;
out:
        return ret;
}

gf_boolean_t
afr_have_quorum (char *logname, afr_private_t *priv)
{
        unsigned int        quorum = 0;

        GF_VALIDATE_OR_GOTO(logname,priv,out);

        quorum = priv->quorum_count;
        if (quorum != AFR_QUORUM_AUTO) {
                return (priv->up_count >= (priv->down_count + quorum));
        }

        quorum = priv->child_count / 2 + 1;
        if (priv->up_count >= (priv->down_count + quorum)) {
                return _gf_true;
        }

        /*
         * Special case for even numbers of nodes: if we have exactly half
         * and that includes the first ("senior-most") node, then that counts
         * as quorum even if it wouldn't otherwise.  This supports e.g. N=2
         * while preserving the critical property that there can only be one
         * such group.
         */
        if ((priv->child_count % 2) == 0) {
                quorum = priv->child_count / 2;
                if (priv->up_count >= (priv->down_count + quorum)) {
                        if (priv->child_up[0]) {
                                return _gf_true;
                        }
                }
        }

out:
        return _gf_false;
}

void
afr_priv_destroy (afr_private_t *priv)
{
        int            i           = 0;

        if (!priv)
                goto out;
        inode_unref (priv->root_inode);
        GF_FREE (priv->shd.pos);
        GF_FREE (priv->shd.pending);
        GF_FREE (priv->shd.inprogress);
//        for (i = 0; i < priv->child_count; i++)
//                if (priv->shd.timer && priv->shd.timer[i])
//                        gf_timer_call_cancel (this->ctx, priv->shd.timer[i]);
        GF_FREE (priv->shd.timer);

        if (priv->shd.healed)
                eh_destroy (priv->shd.healed);

        if (priv->shd.heal_failed)
                eh_destroy (priv->shd.heal_failed);

        if (priv->shd.split_brain)
                eh_destroy (priv->shd.split_brain);

        for (i = 0; i < priv->child_count; i++)
        {
                if (priv->shd.statistics[i])
                        eh_destroy (priv->shd.statistics[i]);
        }

        GF_FREE (priv->shd.statistics);

        GF_FREE (priv->shd.crawl_events);

        GF_FREE (priv->last_event);
        if (priv->pending_key) {
                for (i = 0; i < priv->child_count; i++)
                        GF_FREE (priv->pending_key[i]);
        }
        GF_FREE (priv->pending_key);
        GF_FREE (priv->children);
        GF_FREE (priv->child_up);
        LOCK_DESTROY (&priv->lock);
        LOCK_DESTROY (&priv->read_child_lock);
        pthread_mutex_destroy (&priv->mutex);
        GF_FREE (priv);
out:
        return;
}

int
xlator_subvolume_count (xlator_t *this)
{
        int i = 0;
        xlator_list_t *list = NULL;

        for (list = this->children; list; list = list->next)
                i++;
        return i;
}

inline gf_boolean_t
afr_is_errno_set (int *child_errno, int child)
{
        return child_errno[child];
}

inline gf_boolean_t
afr_is_errno_unset (int *child_errno, int child)
{
        return !afr_is_errno_set (child_errno, child);
}

void
afr_prepare_new_entry_pending_matrix (int32_t **pending,
                                      gf_boolean_t (*is_pending) (int *, int),
                                      int *ctx, struct iatt *buf,
                                      unsigned int child_count)
{
        int midx = 0;
        int idx  = 0;
        int i    = 0;

        midx = afr_index_for_transaction_type (AFR_METADATA_TRANSACTION);
        if (IA_ISDIR (buf->ia_type))
                idx = afr_index_for_transaction_type (AFR_ENTRY_TRANSACTION);
        else if (IA_ISREG (buf->ia_type))
                idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);
        else
                idx = -1;
        for (i = 0; i < child_count; i++) {
                if (is_pending (ctx, i)) {
                        pending[i][midx] = hton32 (1);
                        if (idx == -1)
                                continue;
                        pending[i][idx] = hton32 (1);
                }
        }
}

gf_boolean_t
afr_is_fd_fixable (fd_t *fd)
{
        if (!fd || !fd->inode)
                return _gf_false;
        else if (fd_is_anonymous (fd))
                return _gf_false;
        else if (uuid_is_null (fd->inode->gfid))
                return _gf_false;

        return _gf_true;
}

void
afr_handle_open_fd_count (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        inode_t         *inode = NULL;
        afr_inode_ctx_t *ctx   = NULL;

        local = frame->local;

        if (local->fd)
                inode = local->fd->inode;
        else
                inode = local->loc.inode;

        if (!inode)
                return;

        LOCK (&inode->lock);
        {
                ctx = __afr_inode_ctx_get (inode, this);
                ctx->open_fd_count = local->open_fd_count;
        }
        UNLOCK (&inode->lock);
}

int
afr_initialise_statistics (xlator_t *this)
{
        afr_private_t       *priv = NULL;
        int                 ret = -1;
        int                 i = 0;
        int                 child_count = 0;
        eh_t                *stats_per_brick = NULL;
        shd_crawl_event_t   ***shd_crawl_events = NULL;
        priv = this->private;

        priv->shd.statistics = GF_CALLOC (sizeof(eh_t *), priv->child_count,
                                          gf_common_mt_eh_t);
        if (!priv->shd.statistics) {
                ret = -1;
                goto out;
        }
        child_count = priv->child_count;
        for (i=0; i < child_count ; i++) {
                stats_per_brick = eh_new (AFR_STATISTICS_HISTORY_SIZE,
                                          _gf_false,
                                          _destroy_crawl_event_data);
                if (!stats_per_brick) {
                        ret = -1;
                        goto out;
                }
                priv->shd.statistics[i] = stats_per_brick;

        }

        shd_crawl_events = (shd_crawl_event_t***)(&priv->shd.crawl_events);
        *shd_crawl_events  = GF_CALLOC (sizeof(shd_crawl_event_t*),
                                        priv->child_count,
                                        gf_afr_mt_shd_crawl_event_t);

        if (!priv->shd.crawl_events) {
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;

}
