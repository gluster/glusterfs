/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "xlator.h"
#include "glusterfs.h"
#include "posix.h"
#include <sys/uio.h>

#ifdef HAVE_LIBAIO
#include <libaio.h>


struct posix_aio_cb {
        struct iocb     iocb;
        call_frame_t   *frame;
        struct iobuf   *iobuf;
        struct iobref  *iobref;
        struct iatt     prebuf;
        int             fd;
        int             op;
        off_t           offset;
        size_t          size;
};


int
posix_aio_readv_complete (struct posix_aio_cb *paiocb, int res, int res2)
{
        call_frame_t   *frame = NULL;
        xlator_t       *this = NULL;
        struct iobuf   *iobuf = NULL;
        struct iatt     prebuf = {0,};
        struct iatt     postbuf = {0,};
        int             _fd = -1;
        int             op_ret = -1;
        int             op_errno = 0;
        struct iovec    iov;
        struct iobref  *iobref = NULL;
        int             ret = 0;
        off_t           offset = 0;
        struct posix_private * priv = NULL;


        frame = paiocb->frame;
        this = frame->this;
        priv = this->private;
        iobuf = paiocb->iobuf;
        prebuf = paiocb->prebuf;
        _fd = paiocb->fd;
        offset = paiocb->offset;

        if (res < 0) {
                op_ret = -1;
                op_errno = -res;
                goto out;
        }

        ret = posix_fdstat (this, _fd, &postbuf);

        op_ret = res;
        op_errno = 0;

        iobref = iobref_new ();
        if (!iobref) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf_ptr (iobuf);
        iov.iov_len = op_ret;


        /* Hack to notify higher layers of EOF. */
        if (postbuf.ia_size == 0)
                op_errno = ENOENT;
        else if ((offset + iov.iov_len) == postbuf.ia_size)
                op_errno = ENOENT;
        else if (offset > postbuf.ia_size)
                op_errno = ENOENT;

        LOCK (&priv->lock);
        {
                priv->read_value += op_ret;
        }
        UNLOCK (&priv->lock);

out:
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, &iov, 1,
                             &postbuf, iobref);
        if (iobuf)
                iobuf_unref (iobuf);
        if (iobref)
                iobref_unref (iobref);

        FREE (paiocb);

        return 0;
}


int
posix_aio_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t offset)
{
        int32_t                op_errno   = EINVAL;
        int                    _fd        = -1;
        struct iobuf          *iobuf      = NULL;
        struct posix_fd *      pfd        = NULL;
        int                    ret        = -1;
        struct posix_aio_cb   *paiocb     = NULL;
        struct posix_private  *priv       = NULL;
        struct iocb           *iocb       = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        priv = this->private;

        ret = posix_fd_ctx_get_off (fd, this, &pfd, offset);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto err;
        }
        _fd = pfd->fd;

        if (!size) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING, "size=%"GF_PRI_SIZET, size);
                goto err;
        }

        iobuf = iobuf_get2 (this->ctx->iobuf_pool, size);
        if (!iobuf) {
                op_errno = ENOMEM;
                goto err;
        }

        paiocb = CALLOC (1, sizeof (*paiocb));
        if (!paiocb) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING, "out of memory :(");
                goto err;
        }


        paiocb->frame = frame;
        paiocb->iobuf = iobuf;
        paiocb->offset = offset;
        paiocb->fd = _fd;
        paiocb->op = GF_FOP_READ;

        paiocb->iocb.data = paiocb;
        paiocb->iocb.aio_fildes = _fd;
        paiocb->iocb.aio_lio_opcode = IO_CMD_PREAD;
        paiocb->iocb.aio_reqprio = 0;
        paiocb->iocb.u.c.buf = iobuf_ptr (iobuf);
        paiocb->iocb.u.c.nbytes = size;
        paiocb->iocb.u.c.offset = offset;

        iocb = &paiocb->iocb;

        ret = io_submit (priv->ctxp, 1, &iocb);
        if (ret != 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "io_submit() returned %d", ret);
                errno = -ret;
                goto err;
        }

        return 0;
err:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, 0, 0, 0, 0);
        if (iobuf)
                iobuf_unref (iobuf);

        if (paiocb)
                FREE (paiocb);

        return 0;
}


int
posix_aio_writev_complete (struct posix_aio_cb *paiocb, int res, int res2)
{
        call_frame_t   *frame = NULL;
        xlator_t       *this = NULL;
        struct iatt     prebuf = {0,};
        struct iatt     postbuf = {0,};
        int             _fd = -1;
        int             op_ret = -1;
        int             op_errno = 0;
        int             ret = 0;
        struct posix_private * priv = NULL;


        frame = paiocb->frame;
        this = frame->this;
        priv = this->private;
        prebuf = paiocb->prebuf;
        _fd = paiocb->fd;

        if (res < 0) {
                int flags;
                off_t offset;
                size_t size;

                offset = paiocb->offset;
                size = paiocb->size;
                flags = fcntl (_fd, F_GETFL);
                op_errno = -res;

                gf_log (THIS->name, GF_LOG_ERROR,
                        "pwrite (fd=%d (flags=%d), off=%llx) => -1 (%s)",
                        _fd, flags, (unsigned long long) offset,
                        strerror (op_errno));

                if (op_errno == EINVAL || op_errno == EFBIG) {
                        op_ret = size;
                        goto postop;
                }

                op_ret = -1;
                op_errno = -res;
                goto out;
        }
postop:
        ret = posix_fdstat (this, _fd, &postbuf);

        op_ret = res;
        op_errno = 0;

        LOCK (&priv->lock);
        {
                priv->write_value += op_ret;
        }
        UNLOCK (&priv->lock);

out:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, &prebuf, &postbuf);

        if (paiocb) {
                if (paiocb->iobref)
                        iobref_unref (paiocb->iobref);
                FREE (paiocb);
        }

        return 0;
}


int
posix_aio_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iovec *iov, int count, off_t offset,
                  struct iobref *iobref)
{
        int32_t                op_errno   = EINVAL;
        int32_t                op_ret     = -1;
        int                    _fd        = -1;
        struct posix_fd *      pfd        = NULL;
        int                    ret        = -1;
        struct posix_aio_cb   *paiocb     = NULL;
        struct posix_private  *priv       = NULL;
        struct iocb           *iocb       = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        priv = this->private;

        ret = posix_fd_ctx_get_off (fd, this, &pfd, offset);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto err;
        }
        _fd = pfd->fd;

        paiocb = CALLOC (1, sizeof (*paiocb));
        if (!paiocb) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_WARNING, "out of memory :(");
                goto err;
        }


        paiocb->frame = frame;
        paiocb->offset = offset;
        paiocb->size = iov_length (iov, count);
        paiocb->fd = _fd;
        paiocb->op = GF_FOP_WRITE;

        paiocb->iocb.data = paiocb;
        paiocb->iocb.aio_fildes = _fd;
        paiocb->iobref = iobref_ref (iobref);
        paiocb->iocb.aio_lio_opcode = IO_CMD_PWRITEV;
        paiocb->iocb.aio_reqprio = 0;
        paiocb->iocb.u.v.vec = iov;
        paiocb->iocb.u.v.nr = count;
        paiocb->iocb.u.v.offset = offset;

        iocb = &paiocb->iocb;

        ret = posix_fdstat (this, _fd, &paiocb->prebuf);

        ret = io_submit (priv->ctxp, 1, &iocb);
        if (ret != 1) {
                int flags;
                flags = fcntl (_fd, F_GETFL);
                gf_log (THIS->name, GF_LOG_ERROR,
                        "io_submit (fd=%d (flags=%d), size=%d, off=%llx) => -1 (%s)",
                        _fd, flags, iov_length (iov, count),
                        (long long unsigned) offset, strerror (errno));
                if (errno != EINVAL && errno != EFBIG) {
                        op_errno = -errno;
                        goto err;
                } else {
                        op_ret = iov_length (iov, count);
                }

                goto err;
        }

        return 0;
err:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, 0, 0);

        if (paiocb) {
                if (paiocb->iobref)
                        iobref_unref (paiocb->iobref);
                FREE (paiocb);
        }

        return 0;
}



void *
posix_aio_thread (void *data)
{
        xlator_t              *this = NULL;
        struct posix_private  *priv = NULL;
        int                    ret = 0;
        struct io_event        event = {0, };
        struct posix_aio_cb   *paiocb = NULL;

        this = data;
        priv = this->private;

        for (;;) {
                memset (&event, 0, sizeof (event));
                ret = io_getevents (priv->ctxp, 1, 1, &event, NULL);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "io_getevents() returned %d", ret);
                        if (ret == -EINTR)
                                continue;
                        break;
                }

                paiocb = event.data;

                switch (paiocb->op) {
                case GF_FOP_READ:
                        posix_aio_readv_complete (paiocb, event.res, event.res2);
                        break;
                case GF_FOP_WRITE:
                        posix_aio_writev_complete (paiocb, event.res, event.res2);
                        break;
                default:
                        gf_log (this->name, GF_LOG_ERROR,
                                "unknown op %d found in piocb", paiocb->op);
                        break;
                }
        }

        return NULL;
}


int
posix_aio_init (xlator_t *this)
{
        struct posix_private *priv = NULL;
        int                   ret = 0;

        priv = this->private;

        ret = io_setup (65535, &priv->ctxp);
        if (ret == -1 && errno == ENOSYS) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Linux AIO not availble at run-time. Continuing with synchronous IO");
                ret = 0;
                goto out;
        }

        if (ret == -1)
                goto out;

        ret = pthread_create (&priv->aiothread, NULL,
                              posix_aio_thread, this);
        if (ret != 0) {
                io_destroy (priv->ctxp);
                goto out;
        }

        this->fops->readv  = posix_aio_readv;
        this->fops->writev = posix_aio_writev;
out:
        return ret;
}

#else


int
posix_aio_init (xlator_t *this)
{
        gf_log (this->name, GF_LOG_INFO,
                "Linux AIO not availble at build-time. Continuing with synchronous IO");
        return 0;
}

#endif
