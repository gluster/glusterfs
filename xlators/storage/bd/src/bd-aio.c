/*
  Copyright IBM, Corp. 2013

  This file is part of GlusterFS.

  Author: M. Mohan Kumar <mohan@in.ibm.com>

  Based on posix-aio.c

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <lvm2app.h>
#include <sys/uio.h>

#include "xlator.h"
#include "glusterfs.h"
#include "defaults.h"
#include "bd.h"
#include "bd-aio.h"

#ifdef HAVE_LIBAIO
#include <libaio.h>
#include "bd-mem-types.h"

struct bd_aio_cb {
        struct iocb     iocb;
        call_frame_t   *frame;
        struct iobuf   *iobuf;
        struct iobref  *iobref;
        struct iatt     prebuf;
        int             op;
        off_t           offset;
        fd_t           *fd;
};

void
__bd_fd_set_odirect (fd_t *fd, bd_fd_t *bd_fd, int opflags,
                     off_t offset, size_t size)
{
        int odirect = 0;
        int flags = 0;
        int ret = 0;

        odirect = bd_fd->odirect;

        if ((fd->flags|opflags) & O_DIRECT) {
                /* if instructed, use O_DIRECT always */
                odirect = 1;
        } else {
                /* else use O_DIRECT when feasible */
                if ((offset|size) & 0xfff)
                        odirect = 0;
                else
                        odirect = 1;
        }

        if (!odirect && bd_fd->odirect) {
                flags = fcntl (bd_fd->fd, F_GETFL);
                ret = fcntl (bd_fd->fd, F_SETFL, (flags & (~O_DIRECT)));
                bd_fd->odirect = 0;
        }

        if (odirect && !bd_fd->odirect) {
                flags = fcntl (bd_fd->fd, F_GETFL);
                ret = fcntl (bd_fd->fd, F_SETFL, (flags | O_DIRECT));
                bd_fd->odirect = 1;
        }

        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "fcntl() failed (%s). fd=%d flags=%d pfd->odirect=%d",
                        strerror (errno), bd_fd->fd, flags, bd_fd->odirect);
        }
}

int
bd_aio_readv_complete (struct bd_aio_cb *paiocb, int res, int res2)
{
        call_frame_t   *frame = NULL;
        xlator_t       *this = NULL;
        struct iobuf   *iobuf = NULL;
        struct iatt     postbuf = {0,};
        int             op_ret = -1;
        int             op_errno = 0;
        struct iovec    iov;
        struct iobref  *iobref = NULL;
        off_t           offset = 0;
        bd_attr_t      *bdatt = NULL;

        frame = paiocb->frame;
        this = frame->this;
        iobuf = paiocb->iobuf;
        offset = paiocb->offset;

        if (res < 0) {
                op_ret = -1;
                op_errno = -res;
                gf_log (this->name, GF_LOG_ERROR,
                        "readv(async) failed fd=%p,size=%lu,offset=%llu (%d/%s)",
                        paiocb->fd, paiocb->iocb.u.c.nbytes,
                        (unsigned long long) paiocb->offset,
                        res, strerror (op_errno));
                goto out;
        }

        bd_inode_ctx_get (paiocb->fd->inode, this, &bdatt);
        memcpy (&postbuf, &bdatt->iatt, sizeof (struct iatt));

        op_ret = res;
        op_errno = 0;

        iobref = iobref_new ();
        if (!iobref) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf_ptr (iobuf);
        iov.iov_len = op_ret;

        /* Hack to notify higher layers of EOF. */
        if (!postbuf.ia_size || (offset + iov.iov_len) >= postbuf.ia_size)
                op_errno = ENOENT;

out:
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, &iov, 1,
                             &postbuf, iobref, NULL);
        if (iobuf)
                iobuf_unref (iobuf);
        if (iobref)
                iobref_unref (iobref);

        GF_FREE (paiocb);

        return 0;
}

int
bd_aio_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int32_t             op_errno   = EINVAL;
        int                 _fd        = -1;
        struct iobuf       *iobuf      = NULL;
        bd_fd_t            *bd_fd      = NULL;
        int                 ret        = -1;
        struct bd_aio_cb   *paiocb     = NULL;
        bd_priv_t          *priv       = NULL;
        struct iocb        *iocb       = NULL;
        bd_attr_t          *bdatt      = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        priv = this->private;

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd) {
                STACK_WIND (frame, default_readv_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->readv, fd, size, offset,
                            flags, xdata);
                return 0;
        }
        _fd = bd_fd->fd;
        bd_inode_ctx_get (fd->inode, this, &bdatt);
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

        paiocb = GF_CALLOC (1, sizeof (*paiocb), gf_bd_aio_cb);
        if (!paiocb) {
                op_errno = ENOMEM;
                goto err;
        }

        paiocb->frame = frame;
        paiocb->iobuf = iobuf;
        paiocb->offset = offset;
        paiocb->op = GF_FOP_READ;
        paiocb->fd = fd;

        paiocb->iocb.data = paiocb;
        paiocb->iocb.aio_fildes = _fd;
        paiocb->iocb.aio_lio_opcode = IO_CMD_PREAD;
        paiocb->iocb.aio_reqprio = 0;
        paiocb->iocb.u.c.buf = iobuf_ptr (iobuf);
        paiocb->iocb.u.c.nbytes = size;
        paiocb->iocb.u.c.offset = offset;

        iocb = &paiocb->iocb;

        LOCK (&fd->lock);
        {
                __bd_fd_set_odirect (fd, bd_fd, flags, offset, size);

                ret = io_submit (priv->ctxp, 1, &iocb);
        }
        UNLOCK (&fd->lock);

        if (ret != 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "io_submit() returned %d", ret);
                op_errno = -ret;
                goto err;
        }

        return 0;
err:
        STACK_UNWIND_STRICT (readv, frame, -1, op_errno, 0, 0, 0, 0, 0);
        if (iobuf)
                iobuf_unref (iobuf);

        if (paiocb)
                GF_FREE (paiocb);

        return 0;
}

int
bd_aio_writev_complete (struct bd_aio_cb *paiocb, int res, int res2)
{
        call_frame_t *frame    = NULL;
        xlator_t     *this     = NULL;
        struct iatt   prebuf   = {0,};
        struct iatt   postbuf  = {0,};
        int           op_ret   = -1;
        int           op_errno = 0;
        bd_attr_t    *bdatt    = NULL;

        frame = paiocb->frame;
        prebuf = paiocb->prebuf;
        this = frame->this;

        if (res < 0) {
                op_ret = -1;
                op_errno = -res;
                gf_log (this->name, GF_LOG_ERROR,
                        "writev(async) failed fd=%p,offset=%llu (%d/%s)",
                        paiocb->fd, (unsigned long long) paiocb->offset, res,
                        strerror (op_errno));

                goto out;
        }

        bd_inode_ctx_get (paiocb->fd->inode, this, &bdatt);
        bd_update_amtime (&bdatt->iatt, GF_SET_ATTR_MTIME);
        memcpy (&postbuf, &bdatt->iatt, sizeof (struct iatt));

        op_ret = res;
        op_errno = 0;

out:
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, &prebuf, &postbuf,
                             NULL);

        if (paiocb) {
                if (paiocb->iobref)
                        iobref_unref (paiocb->iobref);
                GF_FREE (paiocb);
        }

        return 0;
}

int
bd_aio_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *iov, int count, off_t offset, uint32_t flags,
               struct iobref *iobref, dict_t *xdata)
{
        int32_t             op_errno   = EINVAL;
        int                 _fd        = -1;
        bd_fd_t            *bd_fd      = NULL;
        int                 ret        = -1;
        struct bd_aio_cb   *paiocb     = NULL;
        bd_priv_t          *priv       = NULL;
        struct iocb        *iocb       = NULL;
        bd_attr_t          *bdatt      = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        priv = this->private;

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd) {
                STACK_WIND (frame, default_writev_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                            fd, iov, count, offset, flags, iobref, xdata);
                return 0;
        }

        bd_inode_ctx_get (fd->inode, this, &bdatt);

        _fd = bd_fd->fd;

        paiocb = GF_CALLOC (1, sizeof (*paiocb), gf_bd_aio_cb);
        if (!paiocb) {
                op_errno = ENOMEM;
                goto err;
        }


        paiocb->frame = frame;
        paiocb->offset = offset;
        paiocb->op = GF_FOP_WRITE;
        paiocb->fd = fd;

        paiocb->iocb.data = paiocb;
        paiocb->iocb.aio_fildes = _fd;
        paiocb->iobref = iobref_ref (iobref);
        paiocb->iocb.aio_lio_opcode = IO_CMD_PWRITEV;
        paiocb->iocb.aio_reqprio = 0;
        paiocb->iocb.u.v.vec = iov;
        paiocb->iocb.u.v.nr = count;
        paiocb->iocb.u.v.offset = offset;

        iocb = &paiocb->iocb;

        memcpy (&paiocb->prebuf, &bdatt->iatt, sizeof (struct iatt));
        LOCK (&fd->lock);
        {
                __bd_fd_set_odirect (fd, bd_fd, flags, offset,
                                     iov_length (iov, count));

                ret = io_submit (priv->ctxp, 1, &iocb);
        }
        UNLOCK (&fd->lock);

        if (ret != 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "io_submit() returned %d", ret);
                op_errno = -ret;
                goto err;
        }

        return 0;
err:
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, 0, 0, 0);

        if (paiocb) {
                if (paiocb->iobref)
                        iobref_unref (paiocb->iobref);
                GF_FREE (paiocb);
        }

        return 0;
}

void *
bd_aio_thread (void *data)
{
        xlator_t           *this = NULL;
        bd_priv_t          *priv = NULL;
        int                 ret = 0;
        int                 i = 0;
        struct io_event    *event = NULL;
        struct bd_aio_cb   *paiocb = NULL;
        struct io_event     events[BD_AIO_MAX_NR_GETEVENTS];
        struct timespec     ts = {0, };

        this = data;
        THIS = this;
        priv = this->private;

        ts.tv_sec = 5;
        for (;;) {
                memset (&events[0], 0, sizeof (events));
                ret = io_getevents (priv->ctxp, 1, BD_AIO_MAX_NR_GETEVENTS,
                                    &events[0], &ts);
                if (ret < 0) {
                        if (ret == -EINTR)
                                continue;
                        gf_log (this->name, GF_LOG_ERROR,
                                "io_getevents() returned %d, exiting", ret);
                        break;
                }

                for (i = 0; i < ret; i++) {
                        event = &events[i];

                        paiocb = event->data;

                        switch (paiocb->op) {
                        case GF_FOP_READ:
                                bd_aio_readv_complete (paiocb, event->res,
                                                          event->res2);
                                break;
                        case GF_FOP_WRITE:
                                bd_aio_writev_complete (paiocb, event->res,
                                                           event->res2);
                                break;
                        default:
                                gf_log (this->name, GF_LOG_ERROR,
                                        "unknown op %d found in piocb",
                                        paiocb->op);
                                break;
                        }
                }
        }

        return NULL;
}

int
bd_aio_init (xlator_t *this)
{
        bd_priv_t *priv = NULL;
        int        ret = 0;

        priv = this->private;

        ret = io_setup (BD_AIO_MAX_NR_EVENTS, &priv->ctxp);
        if ((ret == -1 && errno == ENOSYS) || ret == -ENOSYS) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Linux AIO not available at run-time."
                        " Continuing with synchronous IO");
                ret = 0;
                goto out;
        }

        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "io_setup() failed. ret=%d, errno=%d",
                        ret, errno);
                goto out;
        }

        ret = pthread_create (&priv->aiothread, NULL,
                              bd_aio_thread, this);
        if (ret != 0) {
                io_destroy (priv->ctxp);
                goto out;
        }

        this->fops->readv  = bd_aio_readv;
        this->fops->writev = bd_aio_writev;
out:
        return ret;
}


int
bd_aio_on (xlator_t *this)
{
        bd_priv_t *priv = NULL;
        int        ret = 0;

        priv = this->private;

        if (!priv->aio_init_done) {
                ret = bd_aio_init (this);
                if (ret == 0)
                        priv->aio_capable = _gf_true;
                else
                        priv->aio_capable = _gf_false;
                priv->aio_init_done = _gf_true;
        }

        if (priv->aio_capable) {
                this->fops->readv  = bd_aio_readv;
                this->fops->writev = bd_aio_writev;
        }

        return ret;
}

int
bd_aio_off (xlator_t *this)
{
        this->fops->readv  = bd_readv;
        this->fops->writev = bd_writev;

        return 0;
}

#else

int
bd_aio_on (xlator_t *this)
{
        gf_log (this->name, GF_LOG_INFO,
                "Linux AIO not available at build-time."
                " Continuing with synchronous IO");
        return 0;
}

int
bd_aio_off (xlator_t *this)
{
        gf_log (this->name, GF_LOG_INFO,
                "Linux AIO not available at build-time."
                " Continuing with synchronous IO");
        return 0;
}

void
__bd_fd_set_odirect (fd_t *fd, struct bd_fd *pfd, int opflags,
                        off_t offset, size_t size)
{
        xlator_t        *this = THIS;
        gf_log (this->name, GF_LOG_INFO,
                "Linux AIO not available at build-time."
                " Continuing with synchronous IO");
        return;
}
#endif
