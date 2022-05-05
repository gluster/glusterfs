/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "posix.h"
#include <sys/uio.h>
#include "posix-messages.h"

#ifdef HAVE_LIBAIO
#include <libaio.h>

void
__posix_fd_set_odirect(fd_t *fd, struct posix_fd *pfd, int opflags, int direct)
{
    int odirect = 0;
    int flags = 0;
    int ret = 0;

    odirect = pfd->odirect;

    if ((fd->flags | opflags) & O_DIRECT) {
        /* Always use O_DIRECT if requested. */
        odirect = 1;
    } else
        /* Use hint from the caller. */
        odirect = direct;

    if (!odirect && pfd->odirect) {
        flags = fcntl(pfd->fd, F_GETFL);
        ret = fcntl(pfd->fd, F_SETFL, (flags & (~O_DIRECT)));
        pfd->odirect = 0;
    }

    if (odirect && !pfd->odirect) {
        flags = fcntl(pfd->fd, F_GETFL);
        ret = fcntl(pfd->fd, F_SETFL, (flags | O_DIRECT));
        pfd->odirect = 1;
    }

    if (ret) {
        gf_msg(THIS->name, GF_LOG_WARNING, errno, P_MSG_FCNTL_FAILED,
               "fcntl() failed. fd=%d flags=%d pfd->odirect=%d", pfd->fd, flags,
               pfd->odirect);
    }
}

struct posix_aio_cb {
    struct iocb iocb;
    call_frame_t *frame;
    struct iobuf *iobuf;
    struct iobref *iobref;
    struct iatt prebuf;
    int _fd;
    fd_t *fd;
    int op;
    off_t offset;
};

static struct posix_aio_cb *
posix_aio_cb_init(call_frame_t *frame, fd_t *fd, int _fd, int op)
{
    struct posix_aio_cb *paiocb;

    paiocb = GF_CALLOC(1, sizeof(*paiocb), gf_posix_mt_paiocb);
    if (!paiocb)
        return NULL;

    paiocb->frame = frame;
    paiocb->fd = fd_ref(fd);
    paiocb->_fd = _fd;
    paiocb->op = op;

    paiocb->iocb.data = paiocb;
    paiocb->iocb.aio_fildes = _fd;
    paiocb->iocb.aio_reqprio = 0;

    return paiocb;
}

static void
posix_aio_cb_fini(struct posix_aio_cb *paiocb)
{
    if (paiocb) {
        if (paiocb->iobuf)
            iobuf_unref(paiocb->iobuf);
        if (paiocb->iobref)
            iobref_unref(paiocb->iobref);
        if (paiocb->fd)
            fd_unref(paiocb->fd);
        GF_FREE(paiocb);
    }
}

static int
posix_aio_readv_complete(struct posix_aio_cb *paiocb, int res)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    struct iobuf *iobuf = NULL;
    struct iatt postbuf = {
        0,
    };
    int _fd = -1;
    int op_ret = -1;
    int op_errno = 0;
    struct iovec iov;
    int ret = 0;
    off_t offset = 0;
    struct posix_private *priv = NULL;
    fd_t *fd = NULL;

    GF_ASSERT(paiocb);

    frame = paiocb->frame;
    this = frame->this;
    priv = this->private;
    iobuf = paiocb->iobuf;
    fd = paiocb->fd;
    _fd = paiocb->_fd;
    offset = paiocb->offset;

    if (res < 0) {
        op_ret = -1;
        op_errno = -res;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_READV_FAILED,
               "readv(async) failed fd=%d,size=%lu,offset=%llu (%d)", _fd,
               paiocb->iocb.u.c.nbytes, (unsigned long long)paiocb->offset,
               res);
        goto out;
    }

    ret = posix_fdstat(this, fd->inode, _fd, &postbuf, _gf_true);
    if (ret != 0) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%d", _fd);
        goto out;
    }

    op_ret = res;
    op_errno = 0;

    paiocb->iobref = iobref_new();
    if (!paiocb->iobref) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto out;
    }

    iobref_add(paiocb->iobref, iobuf);

    iov.iov_base = iobuf_ptr(iobuf);
    iov.iov_len = op_ret;

    /* Hack to notify higher layers of EOF. */
    if (!postbuf.ia_size || (offset + iov.iov_len) >= postbuf.ia_size)
        op_errno = ENOENT;

    GF_ATOMIC_ADD(priv->read_value, op_ret);

out:
    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, &iov, 1, &postbuf,
                        paiocb->iobref, NULL);

    posix_aio_cb_fini(paiocb);

    return 0;
}

static int
posix_aio_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset, uint32_t flags, dict_t *xdata)
{
    int32_t op_errno = EINVAL;
    int _fd = -1;
    struct posix_fd *pfd = NULL;
    int ret = -1;
    struct posix_aio_cb *paiocb = NULL;
    struct posix_private *priv = NULL;
    struct iocb *iocb = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);

    priv = this->private;

    ret = posix_fd_ctx_get(fd, this, &pfd, &op_errno);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
               "pfd is NULL from fd=%p", fd);
        goto err;
    }
    _fd = pfd->fd;

    if (!size) {
        op_errno = EINVAL;
        gf_msg(this->name, GF_LOG_WARNING, op_errno, P_MSG_INVALID_ARGUMENT,
               "size=%" GF_PRI_SIZET, size);
        goto err;
    }

    paiocb = posix_aio_cb_init(frame, fd, _fd, GF_FOP_READ);
    if (!paiocb) {
        op_errno = ENOMEM;
        goto err;
    }

    paiocb->iobuf = iobuf_get2(this->ctx->iobuf_pool, size);
    if (!paiocb->iobuf) {
        op_errno = ENOMEM;
        goto err;
    }

    paiocb->offset = offset;

    paiocb->iocb.aio_lio_opcode = IO_CMD_PREAD;
    paiocb->iocb.u.c.buf = iobuf_ptr(paiocb->iobuf);
    paiocb->iocb.u.c.nbytes = size;
    paiocb->iocb.u.c.offset = offset;

    iocb = &paiocb->iocb;

    LOCK(&fd->lock);
    {
        __posix_fd_set_odirect(
            fd, pfd, flags,
            (DIRECT_ALIGNED(size, priv) && DIRECT_ALIGNED(offset, priv) &&
             DIRECT_ALIGNED(iobuf_ptr(paiocb->iobuf), priv)));

        ret = io_submit(priv->ctxp, 1, &iocb);
    }
    UNLOCK(&fd->lock);

    if (ret != 1) {
        op_errno = -ret;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_IO_SUBMIT_FAILED,
               "io_submit() returned %d", ret);
        goto err;
    }

    return 0;
err:
    STACK_UNWIND_STRICT(readv, frame, -1, op_errno, 0, 0, 0, 0, 0);

    posix_aio_cb_fini(paiocb);

    return 0;
}

static int
posix_aio_writev_complete(struct posix_aio_cb *paiocb, int res)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    struct iatt prebuf = {
        0,
    };
    struct iatt postbuf = {
        0,
    };
    int _fd = -1;
    int op_ret = -1;
    int op_errno = 0;
    int ret = 0;
    struct posix_private *priv = NULL;
    fd_t *fd = NULL;

    GF_ASSERT(paiocb);

    frame = paiocb->frame;
    this = frame->this;
    priv = this->private;
    prebuf = paiocb->prebuf;
    fd = paiocb->fd;
    _fd = paiocb->_fd;

    if (res < 0) {
        op_ret = -1;
        op_errno = -res;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_WRITEV_FAILED,
               "writev(async) failed fd=%d,offset=%llu (%d)", _fd,
               (unsigned long long)paiocb->offset, res);

        goto out;
    }

    ret = posix_fdstat(this, fd->inode, _fd, &postbuf, _gf_true);
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
    STACK_UNWIND_STRICT(writev, frame, op_ret, op_errno, &prebuf, &postbuf,
                        NULL);

    posix_aio_cb_fini(paiocb);

    return 0;
}

static int
posix_aio_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iovec *iov, int count, off_t offset, uint32_t flags,
                 struct iobref *iobref, dict_t *xdata)
{
    int32_t op_errno = EINVAL;
    int _fd = -1, i = 0;
    struct posix_fd *pfd = NULL;
    int ret = -1, direct = 0;
    struct posix_aio_cb *paiocb = NULL;
    struct posix_private *priv = NULL;
    struct iocb *iocb = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);

    priv = this->private;
    DISK_SPACE_CHECK_AND_GOTO(frame, priv, xdata, op_errno, op_errno, err);

    ret = posix_fd_ctx_get(fd, this, &pfd, &op_errno);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
               "pfd is NULL from fd=%p", fd);
        goto err;
    }
    _fd = pfd->fd;

    paiocb = posix_aio_cb_init(frame, fd, _fd, GF_FOP_WRITE);
    if (!paiocb) {
        op_errno = ENOMEM;
        goto err;
    }

    paiocb->offset = offset;

    paiocb->iobref = iobref_ref(iobref);
    paiocb->iocb.aio_lio_opcode = IO_CMD_PWRITEV;
    paiocb->iocb.u.v.vec = iov;
    paiocb->iocb.u.v.nr = count;
    paiocb->iocb.u.v.offset = offset;

    iocb = &paiocb->iocb;

    ret = posix_fdstat(this, fd->inode, _fd, &paiocb->prebuf, _gf_true);
    if (ret != 0) {
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%p", fd);
        goto err;
    }

    direct = DIRECT_ALIGNED(offset, priv);
    for (i = 0; direct && i < count; i++)
        direct = (direct && DIRECT_ALIGNED(iov[i].iov_base, priv) &&
                  DIRECT_ALIGNED(iov[i].iov_len, priv));

    LOCK(&fd->lock);
    {
        __posix_fd_set_odirect(fd, pfd, flags, direct);

        ret = io_submit(priv->ctxp, 1, &iocb);
    }
    UNLOCK(&fd->lock);

    if (ret != 1) {
        op_errno = -ret;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_IO_SUBMIT_FAILED,
               "io_submit() returned %d,gfid=%s", ret,
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    return 0;
err:
    STACK_UNWIND_STRICT(writev, frame, -1, op_errno, 0, 0, 0);

    posix_aio_cb_fini(paiocb);

    return 0;
}

static int
posix_aio_fsync_complete(struct posix_aio_cb *paiocb, int res)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    struct iatt prebuf = {
        0,
    };
    struct iatt postbuf = {
        0,
    };
    int _fd = -1;
    int op_ret = -1;
    int op_errno = 0;
    int ret = 0;
    fd_t *fd = NULL;

    GF_ASSERT(paiocb);

    frame = paiocb->frame;
    this = frame->this;
    prebuf = paiocb->prebuf;
    fd = paiocb->fd;
    _fd = paiocb->_fd;

    if (res < 0) {
        op_ret = -1;
        op_errno = -res;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSYNC_FAILED,
               "%s(async) failed fd=%d (%d)",
               (paiocb->iocb.aio_lio_opcode == IO_CMD_FDSYNC ? "fdatasync"
                                                             : "fsync"),
               _fd, res);
        goto out;
    }

    ret = posix_fdstat(this, fd->inode, _fd, &postbuf, _gf_true);
    if (ret != 0) {
        op_ret = -1;
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%d", _fd);
        goto out;
    }

    op_ret = res;
    op_errno = 0;

out:
    STACK_UNWIND_STRICT(fsync, frame, op_ret, op_errno, &prebuf, &postbuf,
                        NULL);

    posix_aio_cb_fini(paiocb);

    return 0;
}

static int
posix_aio_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
                dict_t *xdata)
{
    int32_t op_errno = EINVAL;
    int _fd = -1, ret = -1;
    struct posix_fd *pfd = NULL;
    struct posix_aio_cb *paiocb = NULL;
    struct posix_private *priv = NULL;
    struct iocb *iocb = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);

    priv = this->private;

    ret = posix_fd_ctx_get(fd, this, &pfd, &op_errno);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
               "pfd is NULL from fd=%p", fd);
        goto err;
    }

    _fd = pfd->fd;

    paiocb = posix_aio_cb_init(frame, fd, _fd, GF_FOP_FSYNC);
    if (!paiocb) {
        op_errno = ENOMEM;
        goto err;
    }

    paiocb->iocb.aio_lio_opcode = datasync ? IO_CMD_FDSYNC : IO_CMD_FSYNC;

    iocb = &paiocb->iocb;

    ret = posix_fdstat(this, fd->inode, _fd, &paiocb->prebuf, _gf_false);
    if (ret != 0) {
        op_errno = errno;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_FSTAT_FAILED,
               "fstat failed on fd=%p", fd);
        goto err;
    }

    ret = io_submit(priv->ctxp, 1, &iocb);

    if (ret != 1) {
        op_errno = -ret;
        gf_msg(this->name, GF_LOG_ERROR, op_errno, P_MSG_IO_SUBMIT_FAILED,
               "io_submit() returned %d,gfid=%s", ret,
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    return 0;

err:
    STACK_UNWIND_STRICT(fsync, frame, -1, op_errno, 0, 0, 0);

    posix_aio_cb_fini(paiocb);

    return 0;
}

static void *
posix_aio_thread(void *data)
{
    xlator_t *this = NULL;
    struct posix_private *priv = NULL;
    int ret = 0;
    int i = 0;
    struct io_event events[POSIX_AIO_MAX_NR_GETEVENTS];
    struct io_event *event = NULL;
    struct posix_aio_cb *paiocb = NULL;

    this = data;
    THIS = this;
    priv = this->private;

    for (;;) {
        memset(&events[0], 0, sizeof(events));
        ret = io_getevents(priv->ctxp, 1, POSIX_AIO_MAX_NR_GETEVENTS,
                           &events[0], NULL);
        if (ret <= 0) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, P_MSG_IO_GETEVENTS_FAILED,
                   "io_getevents() returned %d", ret);
            if (ret == -EINTR)
                continue;
            break;
        }

        for (i = 0; i < ret; i++) {
            event = &events[i];

            paiocb = event->data;

            switch (paiocb->op) {
                case GF_FOP_READ:
                    posix_aio_readv_complete(paiocb, event->res);
                    break;
                case GF_FOP_WRITE:
                    posix_aio_writev_complete(paiocb, event->res);
                    break;
                case GF_FOP_FSYNC:
                    posix_aio_fsync_complete(paiocb, event->res);
                    break;
                default:
                    gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_UNKNOWN_OP,
                           "unknown op %d found in piocb", paiocb->op);
                    break;
            }
        }
    }

    return NULL;
}

static int
posix_aio_init(xlator_t *this)
{
    struct posix_private *priv = NULL;
    int ret = 0;

    priv = this->private;

    ret = io_setup(POSIX_AIO_MAX_NR_EVENTS, &priv->ctxp);
    if ((ret == -1 && errno == ENOSYS) || ret == -ENOSYS) {
        gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_AIO_UNAVAILABLE,
               "Linux AIO not available at run-time."
               " Continuing with synchronous IO");
        ret = 0;
        goto out;
    }

    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, P_MSG_IO_SETUP_FAILED,
               "io_setup() failed. ret=%d", ret);
        goto out;
    }

    ret = gf_thread_create(&priv->aiothread, NULL, posix_aio_thread, this,
                           "posixaio");
    if (ret != 0) {
        io_destroy(priv->ctxp);
        goto out;
    }
out:
    return ret;
}

int
posix_aio_on(xlator_t *this)
{
    struct posix_private *priv = NULL;
    int ret = 0;

    priv = this->private;

    if (!priv->aio_init_done) {
        ret = posix_aio_init(this);
        if (ret == 0)
            priv->aio_capable = _gf_true;
        else
            priv->aio_capable = _gf_false;
        priv->aio_init_done = _gf_true;
    }

    if (priv->aio_capable) {
        this->fops->readv = posix_aio_readv;
        this->fops->writev = posix_aio_writev;
        this->fops->fsync = posix_aio_fsync;
    }

    return ret;
}

int
posix_aio_off(xlator_t *this)
{
    this->fops->readv = posix_readv;
    this->fops->writev = posix_writev;
    this->fops->fsync = posix_fsync;

    return 0;
}

#else /* no HAVE_LIBAIO */

int
posix_aio_on(xlator_t *this)
{
    gf_msg(this->name, GF_LOG_INFO, 0, P_MSG_AIO_UNAVAILABLE,
           "Linux AIO not available at build-time."
           " Continuing with synchronous IO");
    return 0;
}

int
posix_aio_off(xlator_t *this)
{
    gf_msg(this->name, GF_LOG_INFO, 0, P_MSG_AIO_UNAVAILABLE,
           "Linux AIO not available at build-time."
           " Continuing with synchronous IO");
    return 0;
}

void
__posix_fd_set_odirect(fd_t *fd, struct posix_fd *pfd, int opflags, int direct)
{
    xlator_t *this = THIS;
    gf_msg(this->name, GF_LOG_INFO, 0, P_MSG_AIO_UNAVAILABLE,
           "Linux AIO not available at build-time."
           " Continuing with synchronous IO");
    return;
}

#endif /* HAVE_LIBAIO */
