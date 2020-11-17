/*
   Copyright (c) 2020 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "posix.h"
#include "posix-messages.h"
#include "posix-io-uring.h"
#include "posix-handle.h"

#ifdef HAVE_LIBURING
#include <liburing.h>

struct posix_uring_ctx;
typedef void(fop_unwind_f)(struct posix_uring_ctx *, int32_t);
typedef void(fop_prep_f)(struct io_uring_sqe *sqe, struct posix_uring_ctx *);
static int
posix_io_uring_submit(xlator_t *this, struct posix_uring_ctx *ctx);

struct posix_uring_ctx {
    call_frame_t *frame;
    struct iatt prebuf;
    dict_t *xdata;
    fd_t *fd;
    int _fd;
    int op;

    union {
        struct {
            struct iovec *iov;
            int count;
            off_t offset;
        } write;

        struct {
            struct iobuf *iobuf;
            struct iovec iovec;
            off_t offset;
        } read;

        struct {
            int32_t datasync;
        } fsync;
    } fop;

    fop_prep_f *prepare;
    fop_unwind_f *unwind;
};

static void
posix_io_uring_ctx_free(struct posix_uring_ctx *ctx)
{
    if (!ctx)
        return;
    if (ctx->fd)
        fd_unref(ctx->fd);
    if (ctx->xdata)
        dict_unref(ctx->xdata);
    switch (ctx->op) {
        case GF_FOP_READ:
            if (ctx->fop.read.iobuf)
                iobuf_unref(ctx->fop.read.iobuf);
            break;
        default:
            break;
    }
    GF_FREE(ctx);
}

struct posix_uring_ctx *
posix_io_uring_ctx_init(call_frame_t *frame, xlator_t *this, fd_t *fd, int op,
                        fop_prep_f prepare, fop_unwind_f unwind,
                        int32_t *op_errno, dict_t *xdata)
{
    struct posix_uring_ctx *ctx = NULL;
    struct posix_fd *pfd = NULL;
    int ret = 0;

    ctx = GF_CALLOC(1, sizeof(*ctx), gf_posix_mt_uring_ctx);
    if (!ctx) {
        return NULL;
    }

    ctx->frame = frame;
    ctx->fd = fd_ref(fd);
    ctx->prepare = prepare;
    ctx->unwind = unwind;
    if (xdata)
        ctx->xdata = dict_ref(xdata);
    ctx->op = op;

    ret = posix_fd_ctx_get(fd, this, &pfd, op_errno);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, *op_errno, P_MSG_PFD_NULL,
               "pfd is NULL from fd=%p", fd);
        goto err;
    }
    ctx->_fd = pfd->fd;

    /* TODO: Explore filling up pre and post bufs using IOSQE_IO_LINK*/
    if ((op == GF_FOP_WRITE) || (op == GF_FOP_FSYNC)) {
        if (posix_fdstat(this, fd->inode, pfd->fd, &ctx->prebuf) != 0) {
            *op_errno = errno;
            gf_msg(this->name, GF_LOG_ERROR, *op_errno, P_MSG_FSTAT_FAILED,
                   "fstat failed on fd=%p", fd);
            goto err;
        }
    }

    return ctx;

err:
    posix_io_uring_ctx_free(ctx);
    return NULL;
}

static void
posix_io_uring_readv_complete(struct posix_uring_ctx *ctx, int32_t res)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    struct posix_private *priv = NULL;
    struct iobref *iobref = NULL;
    struct iobuf *iobuf = NULL;
    struct iatt postbuf = {
        0,
    };
    struct iovec iov = {
        0,
    };
    fd_t *fd = NULL;
    int _fd = -1;
    int ret = 0;
    int op_ret = -1;
    int op_errno = 0;
    off_t offset = 0;

    frame = ctx->frame;
    this = frame->this;
    priv = this->private;
    fd = ctx->fd;
    _fd = ctx->_fd;
    iobuf = ctx->fop.read.iobuf;
    offset = ctx->fop.read.offset;

    if (res < 0) {
        op_ret = -1;
        op_errno = -res;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_READV_FAILED,
               "readv(async) failed fd=%d.", _fd);
        goto out;
    }

    ret = posix_fdstat(this, fd->inode, _fd, &postbuf);
    if (ret != 0) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%d", _fd);
        goto out;
    }

    op_ret = res;
    op_errno = 0;

    iobref = iobref_new();
    if (!iobref) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto out;
    }

    iobref_add(iobref, iobuf);

    iov.iov_base = iobuf_ptr(iobuf);
    iov.iov_len = op_ret;

    /* Hack to notify higher layers of EOF. */
    if (!postbuf.ia_size || (offset + iov.iov_len) >= postbuf.ia_size)
        op_errno = ENOENT;

    GF_ATOMIC_ADD(priv->read_value, op_ret);
    // response xdata is used only for cloudsync, so ignore for now.
out:
    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, &iov, 1, &postbuf,
                        iobref, NULL);
    if (iobref)
        iobref_unref(iobref);
    posix_io_uring_ctx_free(ctx);
}

static void
posix_prep_readv(struct io_uring_sqe *sqe, struct posix_uring_ctx *ctx)
{
    sqe->flags |= IOSQE_ASYNC;
    io_uring_prep_readv(sqe, ctx->_fd, &ctx->fop.read.iovec, 1,
                        ctx->fop.read.offset);
}

int
posix_io_uring_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                     off_t offset, uint32_t flags, dict_t *xdata)
{
    struct posix_uring_ctx *ctx = NULL;
    int32_t op_errno = ENOMEM;
    struct iobuf *iobuf = NULL;
    int ret = 0;

    ctx = posix_io_uring_ctx_init(
        frame, this, fd, GF_FOP_READ, posix_prep_readv,
        posix_io_uring_readv_complete, &op_errno, xdata);
    if (!ctx) {
        goto err;
    }

    iobuf = iobuf_get2(this->ctx->iobuf_pool, size);
    if (!iobuf) {
        op_errno = ENOMEM;
        goto err;
    }
    ctx->fop.read.iobuf = iobuf;
    ctx->fop.read.iovec.iov_base = iobuf_ptr(iobuf);
    ctx->fop.read.iovec.iov_len = size;
    ctx->fop.read.offset = offset;

    ret = posix_io_uring_submit(this, ctx);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, P_MSG_POSIX_IO_URING,
               "Failed to submit sqe");
        op_errno = -ret;
        goto err;
    }
    if (ret == 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, P_MSG_POSIX_IO_URING,
               "submit sqe got zero");
    }
    return 0;
err:
    STACK_UNWIND_STRICT(readv, frame, -1, op_errno, NULL, 1, NULL, NULL, NULL);
    posix_io_uring_ctx_free(ctx);
    return 0;
}

static void
posix_writev_fill_rsp_dict(struct posix_uring_ctx *ctx, xlator_t *this,
                           dict_t **rsp_xdata)
{
    int is_append = 0;

    if (ctx->xdata && dict_get(ctx->xdata, GLUSTERFS_WRITE_IS_APPEND)) {
        if (ctx->prebuf.ia_size == ctx->fop.write.offset ||
            (ctx->fd->flags & O_APPEND))
            is_append = 1;
    }
    *rsp_xdata = _fill_writev_xdata(ctx->fd, ctx->xdata, this, is_append);
}

static void
posix_io_uring_writev_complete(struct posix_uring_ctx *ctx, int32_t res)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    struct posix_private *priv = NULL;
    struct iatt postbuf = {
        0,
    };
    fd_t *fd = NULL;
    int _fd = -1;
    int ret = 0;
    int op_ret = -1;
    int op_errno = 0;
    dict_t *rsp_xdata = NULL;
    frame = ctx->frame;
    this = frame->this;
    priv = this->private;
    fd = ctx->fd;
    _fd = ctx->_fd;

    if (res < 0) {
        op_ret = -1;
        op_errno = -res;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_WRITEV_FAILED,
               "writev(async) failed fd=%d.", _fd);
        goto out;
    }

    ret = posix_fdstat(this, fd->inode, _fd, &postbuf);
    if (ret != 0) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%d", _fd);
        goto out;
    }

    op_ret = res;
    op_errno = 0;
    posix_writev_fill_rsp_dict(ctx, this, &rsp_xdata);
    GF_ATOMIC_ADD(priv->write_value, op_ret);
out:
    STACK_UNWIND_STRICT(writev, frame, op_ret, op_errno, &ctx->prebuf, &postbuf,
                        rsp_xdata);
    if (rsp_xdata)
        dict_unref(rsp_xdata);
    posix_io_uring_ctx_free(ctx);
}

static void
posix_prep_writev(struct io_uring_sqe *sqe, struct posix_uring_ctx *ctx)
{
    io_uring_prep_writev(sqe, ctx->_fd, ctx->fop.write.iov,
                         ctx->fop.write.count, ctx->fop.write.offset);
}

int
posix_io_uring_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
                      struct iovec *iov, int count, off_t offset,
                      uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
    struct posix_uring_ctx *ctx = NULL;
    int32_t op_errno = ENOMEM;
    int ret = 0;

    ctx = posix_io_uring_ctx_init(
        frame, this, fd, GF_FOP_WRITE, posix_prep_writev,
        posix_io_uring_writev_complete, &op_errno, xdata);
    if (!ctx) {
        goto err;
    }

    ctx->fop.write.iov = iov;
    ctx->fop.write.count = count;
    ctx->fop.write.offset = offset;

    ret = posix_io_uring_submit(this, ctx);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, P_MSG_POSIX_IO_URING,
               "Failed to submit sqe");
        op_errno = -ret;
        goto err;
    }
    if (ret == 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, P_MSG_POSIX_IO_URING,
               "submit sqe got zero");
    }
    return 0;
err:
    STACK_UNWIND_STRICT(writev, frame, -1, op_errno, 0, 0, 0);
    posix_io_uring_ctx_free(ctx);
    return 0;
}

static void
posix_io_uring_fsync_complete(struct posix_uring_ctx *ctx, int32_t res)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    struct posix_private *priv = NULL;
    struct iatt postbuf = {
        0,
    };
    fd_t *fd = NULL;
    int _fd = -1;
    int ret = 0;
    int op_ret = -1;
    int op_errno = 0;

    frame = ctx->frame;
    this = frame->this;
    priv = this->private;
    fd = ctx->fd;
    _fd = ctx->_fd;

    if (res < 0) {
        op_ret = -1;
        op_errno = -res;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSYNC_FAILED,
               "writev(async) failed fd=%d.", _fd);
        goto out;
    }

    ret = posix_fdstat(this, fd->inode, _fd, &postbuf);
    if (ret != 0) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%d", _fd);
        goto out;
    }

    op_ret = res;
    op_errno = 0;
    GF_ATOMIC_ADD(priv->write_value, op_ret);
out:
    STACK_UNWIND_STRICT(fsync, frame, op_ret, op_errno, &ctx->prebuf, &postbuf,
                        NULL);
    posix_io_uring_ctx_free(ctx);
}

static void
posix_prep_fsync(struct io_uring_sqe *sqe, struct posix_uring_ctx *ctx)
{
    io_uring_prep_fsync(sqe, ctx->_fd, ctx->fop.fsync.datasync);
}

int
posix_io_uring_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd,
                     int32_t datasync, dict_t *xdata)
{
    struct posix_uring_ctx *ctx = NULL;
    int32_t op_errno = ENOMEM;
    int ret = 0;

    ctx = posix_io_uring_ctx_init(
        frame, this, fd, GF_FOP_FSYNC, posix_prep_fsync,
        posix_io_uring_fsync_complete, &op_errno, xdata);
    if (!ctx) {
        goto err;
    }

    if (datasync)
        ctx->fop.fsync.datasync |= IORING_FSYNC_DATASYNC;

    ret = posix_io_uring_submit(this, ctx);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, P_MSG_POSIX_IO_URING,
               "Failed to submit sqe");
        op_errno = -ret;
        goto err;
    }
    if (ret == 0) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, P_MSG_POSIX_IO_URING,
               "submit sqe got zero");
    }
    return 0;
err:
    posix_io_uring_ctx_free(ctx);
    STACK_UNWIND_STRICT(fsync, frame, -1, op_errno, 0, 0, NULL);
    return 0;
}

static int
posix_io_uring_submit(xlator_t *this, struct posix_uring_ctx *ctx)
{
    struct posix_private *priv = this->private;
    struct io_uring_sqe *sqe = NULL;
    int ret = 0;

    pthread_mutex_lock(&priv->sq_mutex);
    {
        sqe = io_uring_get_sqe(&priv->ring);
        if (!sqe) {
            /*TODO: Retry until we get an sqe instead of failing. */
            pthread_mutex_unlock(&priv->sq_mutex);
            ret = -EAGAIN;
            gf_msg(this->name, GF_LOG_ERROR, -ret, P_MSG_POSIX_IO_URING,
                   "Failed to get sqe");
            goto out;
        }
        ctx->prepare(sqe, ctx);
        io_uring_sqe_set_data(sqe, ctx);
        ret = io_uring_submit(&priv->ring);
    }
    pthread_mutex_unlock(&priv->sq_mutex);

out:
    return ret;
}

static void *
posix_io_uring_thread(void *data)
{
    xlator_t *this = NULL;
    struct posix_private *priv = NULL;
    int ret = 0;
    int32_t res = 0;
    struct io_uring_cqe *cqe = NULL;
    struct posix_uring_ctx *ctx = NULL;

    this = data;
    THIS = this;
    priv = this->private;
    while (1) {
        pthread_mutex_lock(&priv->cq_mutex);
        {
            ret = io_uring_wait_cqe(&priv->ring, &cqe);
        }
        pthread_mutex_unlock(&priv->cq_mutex);
        if (ret != 0) {
            if (ret == -EINTR)
                continue;
            gf_msg(this->name, GF_LOG_WARNING, -ret, P_MSG_POSIX_IO_URING,
                   "Unable to get cqe. Exiting.");
            abort();
        }

        ctx = (struct posix_uring_ctx *)io_uring_cqe_get_data(cqe);
        if (priv->uring_thread_exit == _gf_true && ctx == NULL)
            pthread_exit(NULL);
        res = cqe->res;
        io_uring_cqe_seen(&priv->ring, cqe);
        ctx->unwind(ctx, res);
    }

    return NULL;
}

int
posix_io_uring_init(xlator_t *this)
{
    int ret = -1;
    unsigned flags = 0;
    struct posix_private *priv = this->private;

    // TODO:Try-out flags |= IORING_SETUP_IOPOLL;
    ret = io_uring_queue_init(POSIX_URING_MAX_ENTRIES, &priv->ring, flags);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_POSIX_IO_URING,
               "io_uring init failed.");
        goto out;
    }

    pthread_mutex_init(&priv->sq_mutex, NULL);
    pthread_mutex_init(&priv->cq_mutex, NULL);
    ret = gf_thread_create(&priv->uring_thread, NULL, posix_io_uring_thread,
                           this, "posix-iouring");
    if (ret != 0) {
        io_uring_queue_exit(&priv->ring);
        pthread_mutex_destroy(&priv->sq_mutex);
        pthread_mutex_destroy(&priv->cq_mutex);
        goto out;
    }

out:
    return ret;
}

static int
posix_io_uring_drain(struct posix_private *priv)
{
    struct io_uring_sqe *sqe = NULL;
    int ret = -1;

    priv->uring_thread_exit = _gf_true;
    sqe = io_uring_get_sqe(&priv->ring);
    if (!sqe)
        return ret;

    io_uring_sqe_set_flags(sqe, IOSQE_IO_DRAIN);
    io_uring_sqe_set_data(sqe, NULL);
    io_uring_prep_nop(sqe);
    ret = io_uring_submit(&priv->ring);

    return ret;
}

void
posix_io_uring_fini(xlator_t *this)
{
    struct posix_private *priv = this->private;

    posix_io_uring_drain(priv);
    (void)pthread_join(priv->uring_thread, NULL);
    io_uring_queue_exit(&priv->ring);
    pthread_mutex_destroy(&priv->sq_mutex);
    pthread_mutex_destroy(&priv->cq_mutex);
}

int
posix_io_uring_on(xlator_t *this)
{
    struct posix_private *priv = NULL;
    int ret = -1;

    priv = this->private;

    if (!priv->io_uring_init_done) {
        ret = posix_io_uring_init(this);
        if (ret == 0)
            priv->io_uring_capable = _gf_true;
        else
            priv->io_uring_capable = _gf_false;
        priv->io_uring_init_done = _gf_true;
    }

    if (priv->io_uring_capable) {
        this->fops->readv = posix_io_uring_readv;
        this->fops->writev = posix_io_uring_writev;
        this->fops->fsync = posix_io_uring_fsync;
        ret = 0;
    }

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_POSIX_IO_URING,
               "Posix io_uring init failed, falling back to the previous "
               "IO mechanism.");
    }
    return ret;
}

int
posix_io_uring_off(xlator_t *this)
{
    struct posix_private *priv = this->private;

    this->fops->readv = posix_readv;
    this->fops->writev = posix_writev;
    this->fops->fsync = posix_fsync;
    if (priv->io_uring_capable)
        posix_io_uring_fini(this);

    return 0;
}

#else
int
posix_io_uring_on(xlator_t *this)
{
    gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_AIO_UNAVAILABLE,
           "Linux io_uring not available at build-time. "
           "Continuing with synchronous IO");
    return -1;
}

int
posix_io_uring_off(xlator_t *this)
{
    return 0;
}

#endif
