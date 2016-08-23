/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "arbiter.h"
#include "arbiter-mem-types.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

static arbiter_inode_ctx_t *
__arbiter_inode_ctx_get (inode_t *inode, xlator_t *this)
{

        arbiter_inode_ctx_t *ctx = NULL;
        int ret = 0;
        uint64_t ctx_addr = 0;

        ret = __inode_ctx_get (inode, this, &ctx_addr);
        if (ret == 0) {
                ctx = (arbiter_inode_ctx_t *) (long) ctx_addr;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_arbiter_mt_inode_ctx_t);
        if (!ctx)
                goto out;

        ret = __inode_ctx_put (inode, this, (uint64_t)ctx);
        if (ret) {
                GF_FREE (ctx);
                ctx = NULL;
                gf_log_callingfn (this->name, GF_LOG_ERROR, "failed to "
                                  "set the inode ctx (%s)",
                                  uuid_utoa (inode->gfid));
        }
out:
        return ctx;
}

static arbiter_inode_ctx_t *
arbiter_inode_ctx_get (inode_t *inode, xlator_t *this)
{
        arbiter_inode_ctx_t *ctx = NULL;

        LOCK(&inode->lock);
        {
                ctx = __arbiter_inode_ctx_get (inode, this);
        }
        UNLOCK(&inode->lock);
        return ctx;
}

int32_t
arbiter_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        arbiter_inode_ctx_t *ctx = NULL;

        if (op_ret != 0)
                goto unwind;
        ctx = arbiter_inode_ctx_get (inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        memcpy (&ctx->iattbuf, buf, sizeof (ctx->iattbuf));

unwind:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        return 0;
}

int32_t
arbiter_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        STACK_WIND (frame, arbiter_lookup_cbk, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->lookup, loc, xdata);
        return 0;
}

int32_t
arbiter_readv (call_frame_t *frame,  xlator_t *this, fd_t *fd, size_t size,
               off_t offset, uint32_t flags, dict_t *xdata)
{
        STACK_UNWIND_STRICT (readv, frame, -1, ENOTCONN, NULL, 0, NULL, NULL,
                             NULL);
        return 0;
}

int32_t
arbiter_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                  dict_t *xdata)
{
        arbiter_inode_ctx_t *ctx      = NULL;
        struct iatt         *buf      = NULL;
        int32_t              op_ret   = 0;
        int32_t              op_errno = 0;

        ctx = arbiter_inode_ctx_get (loc->inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        buf = &ctx->iattbuf;
unwind:
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, buf, buf, NULL);
        return 0;
}

int32_t
arbiter_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   dict_t *xdata)

{
        arbiter_inode_ctx_t *ctx      = NULL;
        struct iatt         *buf      = NULL;
        int32_t              op_ret   = 0;
        int32_t              op_errno = 0;

        ctx = arbiter_inode_ctx_get (fd->inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        buf = &ctx->iattbuf;
unwind:
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, buf, buf,
                             NULL);
        return 0;
}

dict_t*
arbiter_fill_writev_xdata (fd_t *fd, dict_t *xdata, xlator_t *this)
{
        dict_t  *rsp_xdata = NULL;
        int32_t ret = 0;
        int is_append = 1;

        if (!fd || !fd->inode || gf_uuid_is_null (fd->inode->gfid)) {
                goto out;
        }

        if (!xdata)
                goto out;

        rsp_xdata = dict_new();
        if (!rsp_xdata)
                goto out;

        if (dict_get (xdata, GLUSTERFS_OPEN_FD_COUNT)) {
                ret = dict_set_uint32 (rsp_xdata, GLUSTERFS_OPEN_FD_COUNT,
                                       fd->inode->fd_count);
                if (ret < 0) {
                        gf_msg_debug (this->name, 0, "Failed to set dict value"
                                      " for GLUSTERFS_OPEN_FD_COUNT");
                }
        }
        if (dict_get (xdata, GLUSTERFS_WRITE_IS_APPEND)) {
                ret = dict_set_uint32 (rsp_xdata, GLUSTERFS_WRITE_IS_APPEND,
                                       is_append);
                if (ret < 0) {
                        gf_msg_debug (this->name, 0, "Failed to set dict value"
                                      " for GLUSTERFS_WRITE_IS_APPEND");
                }
        }
out:
        return rsp_xdata;
}

int32_t
arbiter_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int32_t count, off_t off, uint32_t flags,
                struct iobref *iobref, dict_t *xdata)
{
        arbiter_inode_ctx_t *ctx      = NULL;
        struct iatt         *buf      = NULL;
        dict_t           *rsp_xdata   = NULL;
        int                  op_ret   = 0;
        int                  op_errno = 0;

        ctx = arbiter_inode_ctx_get (fd->inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        buf = &ctx->iattbuf;
        op_ret = iov_length (vector, count);
        rsp_xdata = arbiter_fill_writev_xdata (fd, xdata, this);
unwind:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, buf, buf,
                             rsp_xdata);
        if (rsp_xdata)
                dict_unref (rsp_xdata);
        return 0;
}

int32_t
arbiter_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  int32_t keep_size, off_t offset, size_t len, dict_t *xdata)
{
        arbiter_inode_ctx_t *ctx      = NULL;
        struct iatt         *buf      = NULL;
        int                  op_ret   = 0;
        int                  op_errno = 0;

        ctx = arbiter_inode_ctx_get (fd->inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        buf = &ctx->iattbuf;
unwind:
        STACK_UNWIND_STRICT(fallocate, frame, op_ret, op_errno, buf, buf, NULL);
        return 0;
}

int32_t
arbiter_discard (call_frame_t *frame, xlator_t *this, fd_t *fd,
                off_t offset, size_t len, dict_t *xdata)
{
        arbiter_inode_ctx_t *ctx      = NULL;
        struct iatt         *buf      = NULL;
        int                  op_ret   = 0;
        int                  op_errno = 0;

        ctx = arbiter_inode_ctx_get (fd->inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        buf = &ctx->iattbuf;
unwind:
        STACK_UNWIND_STRICT(discard, frame, op_ret, op_errno, buf, buf, NULL);
        return 0;
}

int32_t
arbiter_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  off_t offset, off_t len, dict_t *xdata)
{
        arbiter_inode_ctx_t *ctx      = NULL;
        struct iatt         *buf      = NULL;
        int                  op_ret   = 0;
        int                  op_errno = 0;

        ctx = arbiter_inode_ctx_get (fd->inode, this);
        if (!ctx) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }
        buf = &ctx->iattbuf;
unwind:
        STACK_UNWIND_STRICT(zerofill, frame, op_ret, op_errno, buf, buf, NULL);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int ret = -1;

        ret = xlator_mem_acct_init (this, gf_arbiter_mt_end + 1);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting "
                        "initialization failed.");
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{

        return 0;
}

int
arbiter_forget (xlator_t *this, inode_t *inode)
{
        arbiter_inode_ctx_t *ctx = NULL;
        uint64_t ctx_addr = 0;

        inode_ctx_del (inode, this, &ctx_addr);
        if (!ctx_addr)
                return 0;
        ctx = (arbiter_inode_ctx_t *) (long) ctx_addr;
        GF_FREE (ctx);
        return 0;
}

int32_t
init (xlator_t *this)
{

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "'arbiter' not configured with exactly one child");
                return -1;
        }

        if (!this->parents)
                gf_log (this->name, GF_LOG_ERROR,
                        "dangling volume. check volfile ");

        return 0;
}

void
fini (xlator_t *this)
{
        return;
}

struct xlator_fops fops = {
        .lookup = arbiter_lookup,
        .readv  = arbiter_readv,
        .truncate = arbiter_truncate,
        .writev = arbiter_writev,
        .ftruncate = arbiter_ftruncate,
        .fallocate = arbiter_fallocate,
        .discard = arbiter_discard,
        .zerofill = arbiter_zerofill,
};

struct xlator_cbks cbks = {
        .forget = arbiter_forget,
};

struct volume_options options[] = {
        { .key  = {NULL} },
};
