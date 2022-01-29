/*
   Copyright (c) 2020 Kadalu.IO <https://kadalu.io>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/logging.h>
#include <glusterfs/syncop.h>
#include <glusterfs/defaults.h>

#include "simple-quota.h"

#define QUOTA_USAGE_KEY "glusterfs.quota.total-usage"
#define QUOTA_FILES_KEY "glusterfs.quota.total-files"
#define QUOTA_RESET_KEY "glusterfs.quota.reset"
#define QUOTA_ALLOW_FOPS_KEY "glusterfs.quota.disable-check"

static int64_t
sync_data_to_disk(xlator_t *this, sq_inode_t *ictx)
{
    sq_private_t *priv = this->private;
    loc_t loc = {0};
    int ret = -1;

    if (priv->use_backend)
        return 0;

    if (!ictx || !ictx->ns) {
        return 0;
    }

    int64_t size = GF_ATOMIC_FETCH_AND(ictx->pending_update, 0);
    int64_t total_consumption = (ictx->xattr_size + size);
    if (!size) {
        /* No changes since last update, no need to update */
        return total_consumption;
    }

    dict_t *dict = dict_new();
    if (!dict) {
        GF_ATOMIC_ADD(ictx->pending_update, size);
        return total_consumption;
    }

    if (total_consumption < 0) {
        /* Some bug, this should have not happened */
        gf_msg(this->name, GF_LOG_INFO, 0, 0,
               "quota usage is below zero (%" PRId64 "), resetting to 0",
               total_consumption);
        total_consumption = 0;
    }

    ret = dict_set_int64(dict, SQUOTA_SIZE_KEY, total_consumption);
    if (IS_ERROR(ret)) {
        dict_unref(dict);
        GF_ATOMIC_ADD(ictx->pending_update, size);
        return total_consumption;
    }

    /* Send the request to actual gfid */
    loc.inode = inode_ref(ictx->ns);
    gf_uuid_copy(loc.gfid, ictx->ns->gfid);

    gf_msg(this->name, GF_LOG_DEBUG, 0, 0, "%s: Writing size of %" PRId64,
           uuid_utoa(ictx->ns->gfid), total_consumption);

    /* As we are doing only operation from server side */
    ret = syncop_setxattr(FIRST_CHILD(this), &loc, dict, 0, NULL, NULL);
    if (IS_SUCCESS(ret)) {
        ictx->xattr_size = total_consumption;
        if (priv->no_distribute)
            ictx->total_size = total_consumption;
    } else {
        GF_ATOMIC_ADD(ictx->pending_update, size);
        gf_log(this->name, GF_LOG_ERROR, "%s: Quota value update failed %d %s",
               uuid_utoa(ictx->ns->gfid), ret, strerror(ret));
    }

    inode_unref(ictx->ns);
    dict_unref(dict);

    return total_consumption;
}

static void
sync_data_from_priv(xlator_t *this, sq_private_t *priv)
{
    sq_inode_t *tmp;
    sq_inode_t *tmp2;

    if (list_empty(&priv->ns_list)) {
        return;
    }
    list_for_each_entry_safe(tmp, tmp2, &priv->ns_list, priv_list)
    {
        sync_data_to_disk(this, tmp);
        tmp = NULL;
    }
    return;
}

static sq_inode_t *
sq_set_ns_hardlimit(xlator_t *this, inode_t *inode, int64_t limit, int64_t size,
                    bool set_namespace)
{
    sq_private_t *priv = this->private;
    sq_inode_t *sq_ctx;
    int ret = -1;

    sq_ctx = GF_MALLOC(sizeof(sq_inode_t), gf_common_mt_char);
    if (!sq_ctx)
        goto out;
    INIT_LIST_HEAD(&sq_ctx->priv_list);
    sq_ctx->hard_lim = limit;
    sq_ctx->xattr_size = size;
    sq_ctx->total_size = size; /* Initialize it to this number for now */
    GF_ATOMIC_INIT(sq_ctx->pending_update, 0);

    sq_ctx->ns = NULL;
    if (set_namespace)
        sq_ctx->ns = inode;

    ret = inode_ctx_put(inode, this, (uint64_t)(uintptr_t)sq_ctx);
    if (IS_ERROR(ret)) {
        GF_FREE(sq_ctx);
	sq_ctx = NULL;
        goto out;
    }

    LOCK(&priv->lock);
    {
        list_add_tail(&sq_ctx->priv_list, &priv->ns_list);
    }
    UNLOCK(&priv->lock);

    gf_msg(this->name, GF_LOG_INFO, 0, 0,
           "%s: hardlimit set (%" PRId64 ", %" PRId64 ")",
           uuid_utoa(inode->gfid), limit, size);
out:
    return sq_ctx;
}

static inline void
sq_update_namespace(xlator_t *this, inode_t *ns, struct iatt *prebuf,
                    struct iatt *postbuf, int64_t size, char *fop)
{
    sq_private_t *priv = this->private;
    sq_inode_t *sq_ctx;
    uint64_t tmp_mq = 0;

    if (!ns || priv->use_backend)
        goto out;

    /* If the size is passed, use that instead */
    if (!size && postbuf && prebuf) {
        size = (postbuf->ia_blocks - prebuf->ia_blocks) * 512;
        gf_msg_debug(this->name, 0, "%s: %" PRId64 " - %" PRId64, fop,
                     postbuf->ia_blocks, prebuf->ia_blocks);
    }

    bool is_inode_linked = IATT_TYPE_VALID(ns->ia_type);
    inode_ctx_get(ns, this, &tmp_mq);
    sq_ctx = (sq_inode_t *)(uintptr_t)tmp_mq;
    if (!sq_ctx) {
        sq_ctx = sq_set_ns_hardlimit(this, ns, 0, size, is_inode_linked);
        if (!sq_ctx)
            goto out;
    }

    if (ns != sq_ctx->ns) {
        /* Set this, as it is possible to have linked a wrong
           inode pointer in lookup */
        gf_msg_debug(this->name, 0, "namespace not being set - %p %p", ns,
                     sq_ctx->ns);
        sq_ctx->ns = ns;
    }

    if (size) {
        GF_ATOMIC_ADD(sq_ctx->pending_update, size);
    }

out:
    return;
}

static inline void
sq_update_total_usage(xlator_t *this, inode_t *inode, int64_t val)
{
    uint64_t tmp_mq = 0;

    inode_ctx_get(inode, this, &tmp_mq);
    sq_inode_t *sq_ctx = (sq_inode_t *)(uintptr_t)tmp_mq;
    if (!sq_ctx) {
        sq_ctx = sq_set_ns_hardlimit(this, inode, 0, 0, true);
        if (!sq_ctx)
            goto out;
    }

    sq_ctx->total_size = val;
out:
    return;
}

static void
sq_update_brick_usage(xlator_t *this, inode_t *inode)
{
    uint64_t tmp_mq = 0;
    int64_t val;
    dict_t *dict = NULL;
    inode_ctx_get(inode, this, &tmp_mq);
    if (!tmp_mq) {
        goto out;
    }
    loc_t loc = { 0, };
    loc.inode = inode_ref(inode);
    int ret = syncop_getxattr(FIRST_CHILD(this), &loc, &dict, SQUOTA_SIZE_KEY,
                              NULL, NULL);
    inode_unref(inode);
    if (IS_ERROR(ret)) {
        goto out;
    }
    ret = dict_get_int64(dict, SQUOTA_SIZE_KEY, &val);
    if (IS_ERROR(ret)) {
        goto out;
    }

    sq_inode_t *sq_ctx = (sq_inode_t *)(uintptr_t)tmp_mq;
    sq_ctx->xattr_size = val;
    GF_ATOMIC_INIT(sq_ctx->pending_update, 0);
out:
    if (dict)
        dict_unref(dict);
    return;
}

static void
sq_update_hard_limit(xlator_t *this, inode_t *ns, int64_t limit, int64_t size)
{
    sq_inode_t *sq_ctx;
    uint64_t tmp_mq = 0;

    if (!ns)
        goto out;

    inode_ctx_get(ns, this, &tmp_mq);
    sq_ctx = (sq_inode_t *)(uintptr_t)tmp_mq;
    if (!sq_ctx) {
        sq_ctx = sq_set_ns_hardlimit(this, ns, limit, size,
                                     IATT_TYPE_VALID(ns->ia_type));
        if (!sq_ctx)
            goto out;
    }

    gf_msg(this->name, GF_LOG_INFO, 0, 0,
           "hardlimit update: %s %" PRId64 " %" PRId64, uuid_utoa(ns->gfid),
           limit, size);
    sq_ctx->hard_lim = limit;

    //GF_ASSERT(size > 0);

out:
    return;
}

static inline int
sq_check_usage(xlator_t *this, inode_t *inode, size_t new_size)
{
    sq_private_t *priv = this->private;
    sq_inode_t *sq_ctx;
    uint64_t tmp_mq = 0;

    inode_ctx_get(inode, this, &tmp_mq);
    if (!tmp_mq)
        return 0;

    sq_ctx = (sq_inode_t *)(uintptr_t)tmp_mq;
    /* If hardlimit is not set, allow writes */
    if (!sq_ctx->hard_lim)
        return 0;

    /* FIXME: lock? */
    int64_t compare_size = sq_ctx->total_size + new_size +
                           GF_ATOMIC_GET(sq_ctx->pending_update);
    if ((sq_ctx->hard_lim < compare_size) && !priv->allow_fops)
        return EDQUOT;

    return 0;
}

/* ====================================== */

static int
sq_forget(xlator_t *this, inode_t *inode)
{
    sq_private_t *priv = this->private;
    sq_inode_t *sq_ctx;
    uint64_t tmp_mq = 0;

    gf_log(this->name, GF_LOG_DEBUG,
           "%s: received forget, removing quota details",
           uuid_utoa(inode->gfid));

    inode_ctx_get(inode, this, &tmp_mq);
    if (!tmp_mq)
        return 0;
    sq_ctx = (sq_inode_t *)(uintptr_t)tmp_mq;

    LOCK(&priv->lock);
    {
        list_del_init(&sq_ctx->priv_list);
    }
    UNLOCK(&priv->lock);

    GF_FREE(sq_ctx);
    return 0;
}

int32_t
sq_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
    sq_private_t *priv = this->private;
    inode_t *inode = NULL;
    sq_inode_t *ctx = NULL;
    uint64_t tmp_mq = 0;
    int64_t usage = -1;
    int64_t avail = -1;
    int64_t blocks = 0;
    int64_t used = 0;

    inode = frame->local;

    if (IS_ERROR(op_ret))
        goto unwind;

    /*
     * We should never get here unless quota_statfs (below) sent us a
     * cookie, and it would only do so if the value was non-NULL.  This
     * check is therefore just routine defensive coding.
     */
    GF_VALIDATE_OR_GOTO("mq", inode, unwind);

    inode_ctx_get(inode, this, &tmp_mq);
    ctx = (sq_inode_t *)(uintptr_t)tmp_mq;
    if (!ctx || ctx->hard_lim <= 0) {
        goto unwind;
    }

    xdata = xdata ? dict_ref(xdata) : dict_new();
    if (!xdata) {
        goto unwind;
    }

    int ret = dict_set_int32(xdata, "simple-quota", 1);
    if (IS_ERROR(ret)) {
        gf_log(this->name, GF_LOG_WARNING,
               "failed to set dict with 'simple-quota'. Quota limits may "
               "not be properly displayed on client");
    }

    /* This check should be after setting the 'simple-quota' key in dict, so
       distribute can show the aggregate stats properly */
    if (priv->use_backend)
        goto unwind;

    /* This step is crucial for a proper sync of xattr at right intervals */
    if ((frame->root->pid == GF_CLIENT_PID_QUOTA_HELPER) ||
        priv->take_cmd_from_all_client) {
        used = sync_data_to_disk(this, ctx);
    } else {
        used = ctx->xattr_size + GF_ATOMIC_GET(ctx->pending_update);
    }

    { /* statfs is adjusted in this code block */
        usage = (used) / buf->f_bsize;

        blocks = (ctx->hard_lim / buf->f_bsize) + 1;
        buf->f_blocks = blocks;

        avail = buf->f_blocks - usage;
        avail = max(avail, 0);

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

unwind:
    frame->local = NULL;
    STACK_UNWIND_STRICT(statfs, frame, op_ret, op_errno, buf, xdata);

    if (xdata)
        dict_unref(xdata);
    if (inode)
        inode_unref(inode);

    return 0;
}

int32_t
sq_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    frame->local = inode_ref(loc->inode->ns_inode);
    /* This is required for setting 'ns' inode in ctx */
    sq_update_namespace(this, loc->inode->ns_inode, NULL, NULL, 0, "statfs");

    STACK_WIND(frame, sq_statfs_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->statfs, loc, xdata);
    return 0;
}

static int32_t
sq_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *prebuf, struct iatt *postbuf,
              dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        sq_update_namespace(this, namespace, prebuf, postbuf, 0, "writev");
    }

    frame->local = NULL;
    STACK_UNWIND_STRICT(writev, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    inode_unref(namespace);
    return 0;
}

int32_t
sq_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
          int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
          dict_t *xdata)
{
    size_t size = iov_length(vector, count);
    int32_t op_errno = sq_check_usage(this, fd->inode->ns_inode, size);

    if (op_errno)
        goto fail;

    frame->local = inode_ref(fd->inode->ns_inode);
    STACK_WIND(frame, sq_writev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
               flags, iobref, xdata);
    return 0;

fail:
    STACK_UNWIND_STRICT(writev, frame, -1, op_errno, NULL, NULL, NULL);
    return 0;
}

static int32_t
sq_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        sq_update_namespace(this, namespace, prebuf, postbuf, 0, "truncate");
    }

    frame->local = NULL;
    inode_unref(namespace);
    STACK_UNWIND_STRICT(truncate, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int32_t
sq_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
            dict_t *xdata)
{
    frame->local = inode_ref(loc->inode->ns_inode);
    STACK_WIND(frame, sq_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
    return 0;
}

static int32_t
sq_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        sq_update_namespace(this, namespace, prebuf, postbuf, 0, "ftruncate");
    }

    frame->local = NULL;
    inode_unref(namespace);
    STACK_UNWIND_STRICT(ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    return 0;
}

int32_t
sq_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             dict_t *xdata)
{
    frame->local = inode_ref(fd->inode->ns_inode);
    STACK_WIND(frame, sq_ftruncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
    return 0;
}

int32_t
sq_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        uint32_t nlink = 0;
        uint64_t blocks = 0;
        int ret = dict_get_uint32(xdata, GF_RESPONSE_LINK_COUNT_XDATA, &nlink);
        if ((nlink == 1)) {
            ret = dict_get_uint64(xdata, GF_GET_FILE_BLOCK_COUNT, &blocks);
            gf_msg(this->name, GF_LOG_DEBUG, 0, 0,
                   "reduce size by %" PRIu64 " blocks (ret: %d)", blocks, ret);
            sq_update_namespace(this, namespace, preparent, postparent,
                                -((blocks + 1) * 512), "unlink");
            /* extra 1 for create offset */
        }
    }

    frame->local = NULL;
    inode_unref(namespace);
    STACK_UNWIND_STRICT(unlink, frame, op_ret, op_errno, preparent, postparent,
                        xdata);
    return 0;
}

int
sq_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
          dict_t *xdata)
{
    /* Get the ns inode from parent, it won't cause any changes */
    xdata = xdata ? dict_ref(xdata) : dict_new();
    if (!xdata)
        goto wind;
    int ret = dict_set_uint32(xdata, GF_REQUEST_LINK_COUNT_XDATA, 1);
    if (IS_ERROR(ret))
        gf_log(this->name, GF_LOG_ERROR,
               "dict set failed (pargfid: %s, name: %s), "
               "still continuing",
               uuid_utoa(loc->pargfid), loc->name);

    ret = dict_set_uint64(xdata, GF_GET_FILE_BLOCK_COUNT, 1);
    if (IS_ERROR(ret))
        gf_log(this->name, GF_LOG_ERROR,
               "dict set failed (pargfid: %s, name: %s), "
               "still continuing",
               uuid_utoa(loc->pargfid), loc->name);

wind:
    frame->local = inode_ref(loc->parent->ns_inode);
    STACK_WIND(frame, sq_unlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
    if (xdata)
        dict_unref(xdata);
    return 0;
}

int32_t
sq_rmdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *preparent, struct iatt *postparent,
             dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        /* Just remove 1 4k block */
        // sq_update_namespace(this, namespace, preparent, postparent, -4096,
        // "rmdir");
    }

    frame->local = NULL;
    inode_unref(namespace);
    STACK_UNWIND_STRICT(rmdir, frame, op_ret, op_errno, preparent, postparent,
                        xdata);
    return 0;
}

int
sq_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flag,
         dict_t *xdata)
{
    frame->local = inode_ref(loc->parent->ns_inode);
    STACK_WIND(frame, sq_rmdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rmdir, loc, flag, xdata);
    return 0;
}

int32_t
sq_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd, inode_t *inode, struct iatt *buf,
              struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        sq_update_namespace(this, namespace, preparent, postparent, 512,
                            "create");
    }

    frame->local = NULL;
    STACK_UNWIND_STRICT(create, frame, op_ret, op_errno, fd, inode, buf,
                        preparent, postparent, xdata);
    inode_unref(namespace);
    return 0;
}

int32_t
sq_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    /* Check for 4k size */
    int32_t op_errno = sq_check_usage(this, loc->parent->ns_inode, 4096);

    if (op_errno)
        goto fail;

    frame->local = inode_ref(loc->parent->ns_inode);
    STACK_WIND(frame, sq_create_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;

fail:
    STACK_UNWIND_STRICT(create, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                        NULL, NULL);
    return 0;
}

int32_t
sq_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, inode_t *inode, struct iatt *buf,
             struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        // sq_update_namespace(this, namespace, preparent, postparent, 4096,
        // "mkdir");
    }

    frame->local = NULL;
    STACK_UNWIND_STRICT(mkdir, frame, op_ret, op_errno, inode, buf, preparent,
                        postparent, xdata);

    inode_unref(namespace);
    return 0;
}

int32_t
sq_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
         mode_t umask, dict_t *xdata)
{
    int32_t op_errno = sq_check_usage(this, loc->parent->ns_inode, 4096);
    if (op_errno)
        goto fail;

    frame->local = inode_ref(loc->parent->ns_inode);
    STACK_WIND(frame, sq_mkdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
    return 0;

fail:
    STACK_UNWIND_STRICT(mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                        NULL);
    return 0;
}

int32_t
sq_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, inode_t *cbk_inode, struct iatt *buf,
              dict_t *xdata, struct iatt *postparent)
{
    inode_t *inode = frame->local;
    if (!inode || !xdata)
        goto unwind;

    if (IS_ERROR(op_ret))
        goto unwind;

    int64_t limit = 0;
    int64_t size = 0;
    uint64_t val = 1;
    int ret = 0;

    ret = inode_ctx_set1(inode, this, &val);
    if (IS_ERROR(ret)) {
        gf_log(this->name, GF_LOG_WARNING,
               "failed to set the flag in inode ctx");
    }

    /* If the Quota Limit is set on a non namespace dir, then this should be
     * ignored */
    if (!dict_get_sizen(xdata, GF_NAMESPACE_KEY))
        goto unwind;

    ret = dict_get_int64(xdata, SQUOTA_SIZE_KEY, &size);
    if (ret) {
        gf_log(this->name, GF_LOG_DEBUG, "quota size not set (%s), ignored",
               uuid_utoa(inode->gfid));
    }

    ret = dict_get_int64(xdata, SQUOTA_LIMIT_KEY, &limit);
    if (ret) {
        gf_log(this->name, GF_LOG_DEBUG,
               "quota limit not set on namespace (%s), ignored",
               uuid_utoa(inode->gfid));
    }

    sq_update_hard_limit(this, inode, limit, size);

unwind:
    frame->local = NULL;

    STACK_UNWIND_STRICT(lookup, frame, op_ret, op_errno, cbk_inode, buf, xdata,
                        postparent);

    if (inode)
        inode_unref(inode);

    return 0;
}

int32_t
sq_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    /* Only in 1 time in lookup for a directory, send namespace and quota xattr
     */
    xdata = xdata ? dict_ref(xdata) : dict_new();
    if (!xdata)
        goto wind;

    if (IATT_TYPE_VALID(loc->inode->ia_type) &&
        !IA_ISDIR(loc->inode->ia_type)) {
        goto wind;
    }

    /* Only proceed on namespace inode */
    /* This check is not valid, as on fresh lookup, namespace wouldn't be set.
       It will get set in _cbk() */
    /*
    if (loc->inode->ns_inode != loc->inode) {
      goto wind;
    }
    */

    /* If we have validated the directory inode, good to ignore this */
    uint64_t val = 0;
    int ret = inode_ctx_get1(loc->inode, this, &val);
    if (val) {
        goto wind;
    }

    /* namespace key would be set in server-protocol's resolve itself */
    ret = dict_set_int64(xdata, SQUOTA_LIMIT_KEY, 0);
    if (IS_ERROR(ret)) {
        gf_log(this->name, GF_LOG_ERROR,
               "BUG: dict set failed (pargfid: %s, name: %s), "
               "still continuing",
               uuid_utoa(loc->pargfid), loc->name);
    }
    ret = dict_set_int64(xdata, SQUOTA_SIZE_KEY, 0);
    if (IS_ERROR(ret)) {
        gf_log(this->name, GF_LOG_ERROR,
               "BUG: dict set (quota size key) failed (pargfid: %s, name: %s), "
               "still continuing",
               uuid_utoa(loc->pargfid), loc->name);
    }

    /* Assumption: 'namespace' key would be set in server protocol */
    frame->local = inode_ref(loc->inode);
wind:

    STACK_WIND(frame, sq_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);

    if (xdata)
        dict_unref(xdata);
    return 0;
}

int32_t
sq_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    inode_t *inode = frame->local;
    if (!inode)
        goto unwind;

    if (IS_ERROR(op_ret)) {
        goto unwind;
    }

    int64_t val = (int64_t)(uintptr_t)cookie;
    uint64_t setval = 1;
    if (val)
        sq_update_hard_limit(this, inode, val, 0);
    /* Setting this flag wouldn't bother lookup() call much */
    int ret = inode_ctx_set1(inode, this, &setval);
    if (IS_ERROR(ret)) {
        gf_log(this->name, GF_LOG_WARNING,
               "failed to set the flag in inode ctx");
    }

unwind:
    frame->local = NULL;
    STACK_UNWIND_STRICT(setxattr, frame, op_ret, op_errno, xdata);
    if (inode)
        inode_unref(inode);

    return 0;
}

int32_t
sq_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
            int32_t flags, dict_t *xdata)
{
    sq_private_t *priv = this->private;
    int64_t val = 0;
    int32_t op_errno = EPERM;

    int ret = dict_get_int64(dict, QUOTA_USAGE_KEY, &val);
    if (IS_SUCCESS(ret)) {
        /* if this operation is not sent on namespace, fail the operation */
        if (loc->inode != loc->inode->ns_inode) {
            gf_log(this->name, GF_LOG_WARNING,
                   "request sent on non-namespace inode (%s)", QUOTA_USAGE_KEY);
            goto err;
        }

        /* Fixes bug kadalu #476. Enable the check back after sometime. */
        /*
        if (!(priv->take_cmd_from_all_client ||
              (frame->root->pid == GF_CLIENT_PID_QUOTA_HELPER)))
            goto err;
        */

        /* we now know there is distribute on client */
        priv->no_distribute = false;

        sq_update_total_usage(this, loc->inode, val);

        /* CHECK: xdata NULL ok here ? */
        STACK_UNWIND_STRICT(setxattr, frame, 0, 0, NULL);
        return 0;
    }

    if (dict_get(dict, QUOTA_RESET_KEY)) {
        /* if this operation is not sent on namespace, fail the operation */
        if (loc->inode != loc->inode->ns_inode) {
            gf_log(this->name, GF_LOG_WARNING,
                   "request sent on non-namespace inode (%s)", QUOTA_USAGE_KEY);
            goto err;
        }

        /* Fixes bug kadalu #476. Enable the check back after sometime. */
        /*
        if (!(priv->take_cmd_from_all_client ||
              (frame->root->pid == GF_CLIENT_PID_QUOTA_HELPER)))
            goto err;
        */

        sq_update_brick_usage(this, loc->inode);

        /* CHECK: xdata NULL ok here ? */
        STACK_UNWIND_STRICT(setxattr, frame, 0, 0, NULL);
        return 0;
    }

    ret = dict_get_int64(dict, QUOTA_ALLOW_FOPS_KEY, &val);
    if (IS_SUCCESS(ret)) {
        /* Fixes bug kadalu #476. Enable the check back after sometime. */
        /*
        if (!(priv->take_cmd_from_all_client ||
              (frame->root->pid == GF_CLIENT_PID_QUOTA_HELPER)))
            goto err;
        */
        priv->allow_fops = val ? 1 : 0;

        STACK_UNWIND_STRICT(setxattr, frame, 0, 0, NULL);
        return 0;
    }

    ret = dict_get_int64(dict, SQUOTA_LIMIT_KEY, &val);
    if (IS_ERROR(ret))
        goto wind;

    /* For timebeing */
    /*
    if (!(priv->take_cmd_from_all_client ||
          (frame->root->pid == GF_CLIENT_PID_QUOTA_HELPER)))
        goto err;
    */
    /* if this operation is not sent on namespace, fail the operation */
    if (loc->inode != loc->inode->ns_inode) {
        gf_log(this->name, GF_LOG_WARNING,
               "request sent on non-namespace inode (%s)", SQUOTA_LIMIT_KEY);
        goto err;
    }

    frame->local = inode_ref(loc->inode);

wind:
    STACK_WIND_COOKIE(frame, sq_setxattr_cbk, (void *)(uintptr_t)val,
                      FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr, loc,
                      dict, flags, xdata);

    return 0;

err:
    STACK_UNWIND_STRICT(setxattr, frame, -1, op_errno, xdata);
    return 0;
}

int32_t
sq_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        /* Just remove 1 4k block */
        sq_update_namespace(this, namespace, prebuf, postbuf, 0, "fallocate");
    }

    frame->local = NULL;
    STACK_UNWIND_STRICT(fallocate, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    inode_unref(namespace);

    return 0;
}

int32_t
sq_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
             off_t offset, size_t len, dict_t *xdata)
{
    int32_t op_errno = sq_check_usage(this, fd->inode->ns_inode, len);
    if (op_errno)
        goto fail;

    frame->local = inode_ref(fd->inode->ns_inode);
    STACK_WIND(frame, sq_fallocate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fallocate, fd, mode, offset, len,
               xdata);
    return 0;
fail:
    STACK_UNWIND_STRICT(fallocate, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

int32_t
sq_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        /* Just remove 1 4k block */
        sq_update_namespace(this, namespace, prebuf, postbuf, 0, "discard");
    }

    frame->local = NULL;
    STACK_UNWIND_STRICT(discard, frame, op_ret, op_errno, prebuf, postbuf,
                        xdata);
    inode_unref(namespace);

    return 0;
}

int32_t
sq_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           size_t len, dict_t *xdata)
{
    frame->local = inode_ref(fd->inode->ns_inode);
    STACK_WIND(frame, sq_discard_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);
    return 0;
}

int32_t
sq_rename_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *buf, struct iatt *preoldparent,
              struct iatt *postoldparent, struct iatt *prenewparent,
              struct iatt *postnewparent, dict_t *xdata)
{
    inode_t *namespace = frame->local;

    if (IS_SUCCESS(op_ret)) {
        uint32_t nlink = 0;
        uint64_t blocks = 0;
        int ret = dict_get_uint32(xdata, GF_RESPONSE_LINK_COUNT_XDATA, &nlink);
        if ((nlink == 1)) {
            ret = dict_get_uint64(xdata, GF_GET_FILE_BLOCK_COUNT, &blocks);
            gf_msg(this->name, GF_LOG_DEBUG, 0, 0,
                   "reduce size by %" PRIu64 " blocks (ret: %d)", blocks, ret);
            sq_update_namespace(this, namespace, prenewparent, postnewparent,
                                -((blocks + 1) * 512), "unlink");
            /* extra 1 for create offset */
        }
    }

    frame->local = NULL;
    inode_unref(namespace);
    STACK_UNWIND_STRICT(rename, frame, op_ret, op_errno, buf, preoldparent,
                        postoldparent, prenewparent, postnewparent, xdata);
    return 0;
}

int32_t
sq_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
    /* Get the ns inode from parent, it won't cause any changes */
    xdata = xdata ? dict_ref(xdata) : dict_new();
    if (!xdata)
        goto wind;
    int ret = dict_set_uint32(xdata, GF_REQUEST_LINK_COUNT_XDATA, 1);
    if (IS_ERROR(ret))
        gf_log(this->name, GF_LOG_ERROR,
               "dict set failed (pargfid: %s, name: %s), "
               "still continuing",
               uuid_utoa(newloc->pargfid), newloc->name);

    ret = dict_set_uint64(xdata, GF_GET_FILE_BLOCK_COUNT, 1);
    if (IS_ERROR(ret))
        gf_log(this->name, GF_LOG_ERROR,
               "dict set failed (pargfid: %s, name: %s), "
               "still continuing",
               uuid_utoa(newloc->pargfid), newloc->name);

wind:
    frame->local = inode_ref(newloc->parent->ns_inode);
    STACK_WIND(frame, sq_rename_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
    if (xdata)
        dict_unref(xdata);
    return 0;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    sq_private_t *priv = this->private;
    GF_OPTION_RECONF("pass-through", this->pass_through, options, bool, out);
    GF_OPTION_RECONF("use-backend", priv->use_backend, options, bool, out);

out:
    return 0;
}

int32_t
init(xlator_t *this)
{
    sq_private_t *priv;
    int ret = -1;

    if (!this->children || this->children->next) {
        gf_log(this->name, GF_LOG_ERROR,
               "FATAL: simple-quota should have exactly one child");
        goto out;
    }

    if (!this->parents) {
        gf_log(this->name, GF_LOG_WARNING, "dangling volume. check volfile ");
    }

    priv = GF_CALLOC(sizeof(sq_private_t), 1, gf_common_mt_char);
    if (!priv)
        goto out;

    GF_OPTION_INIT("pass-through", this->pass_through, bool, out);
    GF_OPTION_INIT("use-backend", priv->use_backend, bool, out);
    GF_OPTION_INIT("cmd-from-all-client", priv->take_cmd_from_all_client, bool,
                   out);

    /* By default assume this is true, if there is a setxattr to set
       the total usage, then mark it false  */
    priv->allow_fops = false;
    priv->no_distribute = true;
    INIT_LIST_HEAD(&priv->ns_list);
    LOCK_INIT(&priv->lock);
    this->private = priv;
    // quota_set_thread(this);

    gf_log(this->name, GF_LOG_DEBUG, "Simple Quota xlator loaded");
    ret = 0;

out:
    return ret;
}

void
fini(xlator_t *this)
{
    sq_private_t *priv = this->private;

    if (!priv)
        return;

    // sq_clear_thread(this, priv->quota_set_thread);
    gf_log(this->name, GF_LOG_TRACE, "calling fini, nothing major pending");
    this->private = NULL;
    GF_FREE(priv);

    return;
}

int
notify(xlator_t *this, int32_t event, void *data, ...)
{
    if (GF_EVENT_PARENT_DOWN == event) {
        gf_log(this->name, GF_LOG_DEBUG,
               "sending all pending information to disk");
        sync_data_from_priv(this, this->private);
    }

    return default_notify(this, event, data);
}

struct xlator_cbks cbks = {
    .forget = sq_forget,
};

struct xlator_fops fops = {
    /* Very critical fop */
    .statfs = sq_statfs,

    /* Required to handle hardlimit etc */
    .setxattr = sq_setxattr,

    /* Set the inode context with limit etc during first lookup */
    .lookup = sq_lookup,

    /* Implement a check for usage */
    .writev = sq_writev,
    .create = sq_create,
    .mkdir = sq_mkdir,

    /* use for update */
    .unlink = sq_unlink,
    .rmdir = sq_rmdir,
    .truncate =
        sq_truncate, /* not implementing a check as it would punch hole */
    .ftruncate = sq_ftruncate,
    .fallocate = sq_fallocate,
    .discard = sq_discard,

    /* rename should reduce the space used of target entry if present */
    .rename = sq_rename,
};

struct volume_options options[] = {
    {
        .key = {"pass-through"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "true",
        .op_version = {GD_OP_VERSION_11_0},
        .flags = OPT_FLAG_SETTABLE,
        .tags = {"quota", "simple-quota"},
        .description = "Enable/Disable simple-quota translator",
    },
    /* for handling 'inode quota', please use backend quota support */
    /* TODO: implement a inode quota specific check in entry fops */
    {
        .key = {"use-backend"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_11_0},
        .tags = {"quota", "simple-quota"},
        .description = "use backend fs's quota for accounting",
    },
    {
        .key = {"cmd-from-all-client"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "false",
        .op_version = {GD_OP_VERSION_11_0},
        .tags = {"quota", "simple-quota"},
        .description = "Allow all clients to send quota set commands.",
    },
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .op_version = {GD_OP_VERSION_11_0},
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .notify = notify,
    .reconfigure = reconfigure,
    .identifier = "simple-quota",
    .category = GF_EXPERIMENTAL,
};
